/*
 * Raspy Finger driver for libfprint
 * Copyright (C) 2022 Charlie Lin <clin@rollins.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "raspy.h"

#include "drivers_api.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <termios.h>
#include <inttypes.h>

#define FP_COMPONENT "raspy"

struct _FpiDeviceRaspy {
  FpImageDevice par;
};

enum Permission { One = 1, Two, Three };

static int fd = -1;

static void get_fd_for_usb_serial(void) {
  // FIXME: assume no more than 10 USB serial devices are present
  char *tmp;
  int tmp_fd = -1;
  for (gushort i = 0; i < 10; i++) {
    int disc = snprintf(tmp, 13, "/dev/ttyUSB%d", i);
    // TODO: Something bad has occured, but how to report it... eh?
    if (disc > 13) {
    }
    tmp_fd = open(tmp, O_RDWR | O_NONBLOCK);
    if (tmp_fd != -1) {
      fd = tmp_fd;
      break;
    }
  }
}

// Naive, but functional
static uint8_t xor
    (uint8_t * bytes, unsigned long sz) {
      uint8_t checksum = 0;
      for (unsigned long i = 0; i < sz; i++) {
        if (bytes[i] == 0)
          continue;
        checksum ^= bytes[i];
      }
      return checksum;
    }

    static uint8_t xor_6_bytes(uint8_t *bytes) {
  return xor(bytes, 6);
}

static void send_command(uint_fast8_t cmd, const uint_fast8_t params[3],
                         _Bool sending_header_and_data) {
  uint_fast8_t eight_byte_data[8] = {0xf5, 0, 0, 0, 0, 0, 0, 0xf5};
  eight_byte_data[1] = cmd;
  for (int i = 0; i < 3; i++)
    eight_byte_data[2 + i] = params[i];
  eight_byte_data[6] = xor_6_bytes(eight_byte_data);

  ssize_t tmp = write(fd, eight_byte_data, sizeof(eight_byte_data));
  if (tmp == -1) {
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  }

  if (sending_header_and_data) {
    uint_fast8_t *variable_data = NULL;
    tmp = write(fd, variable_data, sizeof(*variable_data));
    if (tmp == -1) {
      fp_warn("Unexpected errno %s: %s", strerrorname_np(errno),
              strerror(errno));
    }
  }
}

static Response read_response(_Bool receiving_header_and_data) {
  uint_fast8_t eight[8];
  ssize_t tmp = read(fd, eight, 8);
  if (tmp == -1)
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  // XXX: assume 8 bytes are fully read
  //else if (tmp < 8) {}

  if (eight[0] != 0xf5 || eight[7] != 0xf5)
      fp_warn("First or last byte of header not 0xf5: got %x and %x", eight[0], eight[7]);
  if (eight[6] != xor_6_bytes(eight))
    fp_warn("Received incorrect XOR checksum: expected %x, got %x", xor_6_bytes(eight), eight[6]);

  // Members of a struct, e.g. res, that aren't explicitly initialized get
  // zero-initialized (members of type int set to 0, pointer members set to NULL)
  Response res = {.res = {eight[2], eight[3], eight[4]}};
  if (receiving_header_and_data) {
    res.payload_size = eight[2] << 8 | eight[3];
    // +3 accounts for auxiliary bytes surrounding data of interest: first and last bytes of 0xf5, and XOR checksum.
    size_t tmp_buf_len = sizeof(uint_fast8_t)*(res.payload_size+3);
    // Temporary buffer for sanity checking below
    uint_fast8_t* tmp_buf = malloc(tmp_buf_len);
    ssize_t bytes_read = read(fd, tmp_buf, tmp_buf_len);
    if (bytes_read == -1)
      fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
    else if (bytes_read < tmp_buf_len)
      fp_warn("Expected to read %zu bytes: instead got %zu  bytes", tmp_buf_len, bytes_read);

    if (tmp_buf[0] != 0xf5 || tmp_buf[7] != 0xf5)
      fp_warn("First or last byte of data not 0xf5: got %x and %x", tmp_buf[0], tmp_buf[7]);
    if (tmp_buf[tmp_buf_len-2] != xor(tmp_buf, tmp_buf_len-2))
      fp_warn("Received incorrect XOR checksum: expected %x, got %x", xor(tmp_buf, tmp_buf_len-2), tmp_buf[tmp_buf_len-2]);
    // Strip out above 3 bytes before copying real data
    res.payload_size = tmp_buf_len-sizeof(uint_fast8_t)*3;
    res.payload = malloc(res.payload_size);
    memmove(res.payload, tmp_buf+1, res.payload_size);
    free(tmp_buf);
    tmp_buf = NULL;
  }
  return res;
}

static void delete_user(uint_fast16_t user) {
  const uint_fast8_t params[] = {user >> 8, user & 0xff, 0};
  send_command(4, params, 0);
  Response res = read_response(0);
  switch (res.res[2]) {
  case fail: fp_warn("Deleting user ID %"PRIuFAST16" failed.", user);
  // Fallthrough desirable
  case success: return;
  }
}

G_DECLARE_FINAL_TYPE(FpiDeviceRaspy, fpi_device_raspy, FPI, DEVICE_RASPY,
                     FpImageDevice)
G_DEFINE_TYPE(FpiDeviceRaspy, fpi_device_raspy, FP_TYPE_IMAGE_DEVICE)

static void raspy_close(FpImageDevice *dev) {
  close(fd);
  FpiDeviceRaspy *self = FPI_DEVICE_RASPY(dev);
  GError *err = NULL;

  g_usb_device_release_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0,
                                 0, &err);

  fpi_image_device_close_complete(dev, err);
}

static void raspy_open(FpImageDevice *dev) {
  get_fd_for_usb_serial();
  struct termios term_attributes;
  tcgetattr(fd, &term_attributes);
  term_attributes.c_cflag |= PARODD & PARENB;
  int tmp = tcsetattr(fd, 0, &term_attributes);
  // BSD-specific function...
  int l = cfsetspeed(&term_attributes, B19200);

  FpiDeviceRaspy *self = FPI_DEVICE_RASPY(dev);
  GError *err = NULL;

  g_usb_device_claim_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0, 0,
                               &err);

  fpi_image_device_open_complete(dev, err);
}

static void add_fingerprint(void) {}

static void raspy_activate(FpImageDevice *dev) {}

static void raspy_deactivate(FpImageDevice *dev) {}

static void raspy_change_state(FpImageDevice *dev, FpiImageDeviceState state) {}

static uint_fast8_t query_timeout(void) {

}

// Product and vendor ID of bridge controller
static const FpIdEntry id_tab[] = {
    {.vid = 0x10c4, .pid = 0xea60},
};

/*const struct _something {
  _Bool sends_variable_bytes;
  uint_fast8_t command_id;
  void (*func)();
} list_of_funcs[] = {
    {.func = delete_user, .sends_variable_bytes = 0, .command_id = 4}};*/

static void fpi_device_raspy_init(FpiDeviceRaspy *self) {
  fp_dbg("Raspy initialized");
}
static void fpi_device_raspy_finalize(GObject *this) {
  fp_dbg("Raspy deinitialized");
}

static void fpi_device_raspy_class_init(FpiDeviceRaspyClass *klass) {
  FpDeviceClass *dev_class = FP_DEVICE_CLASS(klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS(klass);

  dev_class->id = "raspy";
  dev_class->full_name = "Waveshare Fingerprint Sensor (with CP210x USB-UART "
                         "bridge controller chip)";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_tab;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = 2;
  dev_class->features =
      FP_DEVICE_FEATURE_DUPLICATES_CHECK | FP_DEVICE_FEATURE_IDENTIFY |
      FP_DEVICE_FEATURE_ALWAYS_ON | FP_DEVICE_FEATURE_DUPLICATES_CHECK |
      FP_DEVICE_FEATURE_STORAGE_CLEAR | FP_DEVICE_FEATURE_VERIFY |
      FP_DEVICE_FEATURE_CAPTURE;

  // img_class->bz3_threshold = 24;
  img_class->img_height = img_class->img_width = 280;
  img_class->img_open = raspy_open;
  img_class->activate = raspy_activate;
  img_class->deactivate = raspy_deactivate;
  img_class->change_state = raspy_change_state;
  img_class->img_close = raspy_close;
}
