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
#include "fpi-uart-transfer.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <termios.h>

#define FP_COMPONENT "raspy"

#include "fpi-log.h"

// Struct needed for the G_DECLARE_FINAL_TYPE, which is included in other source files of the bundled drivers
struct _FpiDeviceRaspy {
  FpImageDevice par;
};

static int fd = -1;

/* The scanner connects via the cp2x02 USB serial driver on Linux,
so this function does the heavy-lifting */
static void get_fd_for_usb_serial(void) {
  // FIXME: assume no more than 10 USB serial devices are present
  char *tmp;
  int tmp_fd = -1;
  for (gushort i = 0; i < 10; i++) {
  	// 13 is the length of the final string, including '\0'
    int disc = snprintf(tmp, 13, "/dev/ttyUSB%d", i);
    // TODO: Something bad has occurred, but how to report it... eh?
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
static uint8_t xor(uint8_t * bytes, unsigned long sz) {
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

/* Handler for sending the varying combinations of paramaters (up to 3) ,
and an optional parameter containing variable-length data */
static void send_command(uint_fast8_t cmd, const uint_fast8_t params[3],
                         uint_fast8_t *variable_data) {
  uint_fast8_t eight_byte_data[8] = {0xf5, 0, 0, 0, 0, 0, 0, 0xf5};
  eight_byte_data[1] = cmd;
  for (int i = 0; i < 3; i++)
    eight_byte_data[2 + i] = params[i];
  eight_byte_data[6] = xor_6_bytes(eight_byte_data);

  ssize_t tmp = write(fd, eight_byte_data, sizeof(eight_byte_data));
  if (tmp == -1) {
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  }

	// NULL is falsy, hence specifying the parameter itself is sufficient
  if (variable_data) {
    tmp = write(fd, variable_data, sizeof(*variable_data));
    if (tmp == -1) {
      fp_warn("Unexpected errno %s: %s", strerrorname_np(errno),
              strerror(errno));
    }
  }
}

/* Handler for receiving the the scanner's response data, with a bool parameter
indicating whether variable-length data will be receieved */
static Response read_response(_Bool receiving_header_and_data) {
  uint_fast8_t eight[8];
  ssize_t tmp = read(fd, eight, 8);
  if (tmp == -1)
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  // XXX: assume 8 bytes are fully read
  // else if (tmp < 8) {}

  if (eight[0] != 0xf5 || eight[7] != 0xf5)
    fp_warn("First or last byte of header not 0xf5: got %x and %x", eight[0],
            eight[7]);
  if (eight[6] != xor_6_bytes(eight))
    fp_warn("Received incorrect XOR checksum: expected %x, got %x",
            xor_6_bytes(eight), eight[6]);

  // Members of a struct, e.g. res, that aren't explicitly initialized get
  // zero-initialized (members of type int set to 0, pointer members set to
  // NULL)
  Response res = {.res = {eight[2], eight[3], eight[4]}};
  if (receiving_header_and_data) {
    res.payload_size = eight[2] << 8 | eight[3];
    // +3 accounts for auxiliary bytes surrounding data of interest: first and
    // last bytes of 0xf5, and XOR checksum.
    size_t tmp_buf_len = sizeof(uint_fast8_t) * (res.payload_size + 3);
    // Temporary buffer for sanity checking below
    uint_fast8_t *tmp_buf = malloc(tmp_buf_len);
    ssize_t bytes_read = read(fd, tmp_buf, tmp_buf_len);
    if (bytes_read == -1)
      fp_warn("Unexpected errno %s: %s", strerrorname_np(errno),
              strerror(errno));
    else if (bytes_read < tmp_buf_len)
      fp_warn("Expected to read %zu bytes: instead got %zu  bytes", tmp_buf_len,
              bytes_read);

    if (tmp_buf[0] != 0xf5 || tmp_buf[7] != 0xf5)
      fp_warn("First or last byte of data not 0xf5: got %x and %x", tmp_buf[0],
              tmp_buf[7]);
    if (tmp_buf[tmp_buf_len - 2] != xor(tmp_buf, tmp_buf_len - 2))
      fp_warn("Received incorrect XOR checksum: expected %x, got %x",
              xor(tmp_buf, tmp_buf_len - 2), tmp_buf[tmp_buf_len - 2]);
    // Strip out above 3 bytes before copying real data
    res.payload_size = tmp_buf_len - sizeof(uint_fast8_t) * 3;
    res.payload = malloc(res.payload_size);
    memmove(res.payload, tmp_buf + 1, res.payload_size);
    free(tmp_buf);
    tmp_buf = NULL;
  }
  return res;
}

// Following functions implement the actual commands to the scanner

static uint_fast8_t query_timeout(void) {
  const uint_fast8_t params[] = {0, 0, 1};
  send_command(0x0b, params, NULL);
  Response res = read_response(0);
  return res.res[1];
}

static Raspy_ACK_Status set_timeout(uint_fast8_t timeout) {
  const uint_fast8_t params[] = {0, timeout, 0};
  send_command(0x0b, params, NULL);
  Response res = read_response(0);
  return res.res[2];
}

static void delete_user(uint_fast16_t user) {
  const uint_fast8_t params[] = {user >> 8, user & 0xff, 0};
  send_command(4, params, 0);
  Response res = read_response(0);
  switch (res.res[2]) {
  case fail:
    fp_warn("Deleting user ID %" PRIuFAST16 " failed.", user);
  // Fallthrough desirable
  case success:
    return;
  }
}

static void delete_all_users(gushort *per) {
  /* Since per is a pointer, it can be null.
   * If null, delete all users, otherwise deference pointer
   * to delete users having permission level 1,2,3. */
  if (per)
    g_assert(*per > 0 && *per < 4);
  const uint_fast8_t params[] = {0, 0, !per ? 0 : *per};
  send_command(5, params, 0);
  Response res = read_response(0);
  switch (res.res[2]) {
  case fail:
    if (per)
      fp_warn("Deleting users having level %u failed.", *per);
    else
      fp_warn("Deleting all users failed");
  // Fallthrough desirable
  case success:
    return;
  }
}

// Count number of fingerprints? The manual's translation quality was questionable...
static uint_fast16_t number_of_users(_Bool count_fingerprints) {
  const uint_fast8_t params[] = {0, 0, count_fingerprints ? 0xff : 0};
  send_command(9, params, 0);
  Response res = read_response(0);
  switch (res.res[2]) {
  // Fallthrough desirable
  case 0xff:
  case success:
    return res.res[0] << 8 | res.res[1];
  case fail:
    fp_warn("Finding number of users failed");
  default:
    // Reasonable assumption: not that many users will be registered in sensor
    return UINT_FAST16_MAX;
  }
}

static _Bool compare_1_to_1(uint_fast16_t user) {
  const uint_fast8_t params[] = {user >> 8, user & 0xff, 0};
  send_command(0x0b, params, 0);

  Response res = read_response(0);
  switch (res.res[2]) {
  case success:
    return 1;
  case timeout:
    fp_warn("Timeout reached for fingerprint capture");
  // Fallthrough desirable
  case fail:;
  default:
    return 0;
  }
}

static uint_fast16_t compare_1_to_N(void) {
  const uint_fast8_t params[] = {0};
  send_command(0x0c, params, 0);

  Response res = read_response(0);
  uint_fast16_t tmp;
  switch (res.res[2]) {
  // Fallthrough desirable
  case timeout:
    fp_warn("Timeout reached for fingerprint capture");
  case no_user:
    return UINT_FAST16_MAX;
  default:
    tmp = res.res[0] << 8 | res.res[1];
    return tmp;
  }
}

static gushort query_permission(uint_fast16_t user) {
  const uint_fast8_t params[] = {user >> 8, user & 0xff, 0};
  send_command(0x0a, params, NULL);
  Response res = read_response(0);
  return res.res[2] == no_user ? G_MAXUSHORT : res.res[2];
}

static gushort query_comparison_level(void) {
  const uint_fast8_t params[] = {0, 0, 1};
  send_command(0x28, params, 0);

  Response res = read_response(0);
  return res.res[1];
}

static void set_comparison_level(gushort new_lvl) {
  g_assert(new_lvl >= 0 && new_lvl < 10);
  const uint_fast8_t params[] = {0, new_lvl, 0};
  send_command(0x28, params, 0);

  Response res = read_response(0);
  switch (res.res[2]) {
  case fail:
    fp_warn("Setting comparison level from %u to %u failed", res.res[1],
            new_lvl);
  }
}

static Image *get_fingerprint_image(void) {
  const uint_fast8_t params[] = {0, 0, 0};
  send_command(0x24, params, 0);

  Response res = read_response(1);
  Image *i = NULL;
  switch (res.res[2]) {
  case fail:
    fp_warn("Getting image failed");
    break;
  case timeout:
    fp_warn("Timeout reached for getting fingerprint image");
    break;
  case success:
    g_assert(res.payload_size == 9800);
    i = memcpy(i, res.payload, sizeof *i);
  }

  return i;
}

static Eigenvalues *get_fingerprint_image_upload_eighenvals(void) {
  const uint_fast8_t params[] = {0, 0, 0};
  send_command(0x23, params, 0);

  Response res = read_response(1);
  Eigenvalues *e = NULL;
  switch (res.res[2]) {
  case fail:
    fp_warn("Getting image failed");
    break;
  case timeout:
    fp_warn("Timeout reached for getting fingerprint image");
    break;
  case success:
    g_assert(res.payload_size == 193);
    memmove(e, res.payload + 3, sizeof *e);
  }

  return e;
}

static Raspy_ACK_Status add_fingerprint(uint_fast16_t user, gushort permiss) {
  g_assert(permiss >= 0 && permiss < 4);
  const uint_fast8_t params[] = {user >> 8, user & 0xff, permiss};

  for (gushort i = 1; i < 4; i++) {
    send_command(i, params, 0);
    Response res = read_response(0);
    if (res.res[2] != success)
      return res.res[2];
  }
  return success;
}

static uint_fast8_t *add_fingerprint_and_get_eigenvals(uint_fast16_t user,
                                                       gushort perms) {
  g_assert(perms >= 0 && perms < 4);
  const uint_fast8_t params[] = {user >> 8, user & 0xff, perms};
  Response res;
  for (gushort i = 1; i < 3; i++) {
    send_command(i, params, 0);
    res = read_response(0);
    if (res.res[2] != success)
      return NULL;
  }
  send_command(6, (uint_fast8_t[]){0, 0, 0}, 0);
  g_assert(res.payload_size == 193);
  static uint_fast8_t eigenvals[193];
  switch (res.res[2]) {
  case success:
    memmove(eigenvals, res.payload + 3, 193 * sizeof(uint_fast8_t));
    return eigenvals;
  // Fallthrough intentional
  case fail:
    fp_warn("Getting eigenvalues for user %" PRIuFAST16
            " with permission level %u failed",
            user, perms);
    return NULL;
  case timeout:
    fp_warn("Fingerprint timeout for user %" PRIuFAST16
            " with permission level %u failed",
            user, perms);
    return NULL;
  default:
    return NULL;
  }
}

static _Bool duplicates_allowed(void) {
  const uint_fast8_t params[3] = {0, 0, 1};
  send_command(0x2d, params, 0);
  Response res = read_response(0);
  return res.res[1];
}

static Raspy_ACK_Status set_duplication_mode(_Bool on) {
  const uint_fast8_t params[3] = {0, on, 0};
  send_command(0x2d, params, 0);
  Response res = read_response(0);
  return res.res[1];
}

static User *query_all_users(void) {
  const uint_fast8_t params[3] = {0};
  send_command(0x2b, params, NULL);
  Response r = read_response(1);
  uint_fast16_t max_uid;
  User *u = NULL;
  switch (r.res[2]) {
  case fail:
    fp_warn("Querying info for all users failed");
    break;
  case success:
    max_uid = r.payload[0] << 8 | r.payload[1];
    g_assert(r.payload_size == 3 * max_uid + 2);
    malloc(max_uid * sizeof *u);
    for (uint_fast16_t i = 0; i < max_uid; i++) {
      u[i].id = r.payload[3 * i] << 8 | r.payload[3 * i + 1];
      u[i].permission = r.payload[3 * i + 2];
      u[i].eigenvals = NULL;
    }
  }
  return u;
}

static Raspy_ACK_Status
download_evs_and_compare_with_fingerprint(Eigenvalues *e) {
  const uint_fast8_t pars[] = {0, 193, 0};
  uint_fast8_t tmp[196] = {0};
  memcpy(tmp + 3 * sizeof pars[0], e, sizeof *e);
  send_command(0x44, pars, tmp);
  Response r = read_response(0);
  return r.res[2];
}

static _Bool download_evs_and_compare_1_to_1(uint_fast16_t user,
                                             Eigenvalues *e) {
  const uint_fast8_t pars[] = {0, 193, 0};
  uint_fast8_t tmp[196] = {user >> 8, user & 0xff};
  memcpy(tmp + 3 * sizeof(uint_fast8_t), e, sizeof *e);
  send_command(0x42, pars, tmp);
  Response r = read_response(0);
  return r.res[2] == success ? 1 : 0;
}

static User *download_evs_and_compare_1_to_n(Eigenvalues *e) {
  const uint_fast8_t pars[] = {0, 193, 0};
  uint_fast8_t tmp[196] = {0};
  memcpy(tmp + 3 * sizeof(uint_fast8_t), e, sizeof *e);
  send_command(0x43, pars, tmp);
  Response r = read_response(0);
  User *u = NULL;
  u->eigenvals = NULL;
  if (r.res[2] != no_user) {
    u->id = r.res[0] << 8 | r.res[1];
    u->permission = r.res[2];
  }
  return u;
}

static _Bool downloads_evs_and_save_uid(User *u) {
  const uint_fast8_t pars[] = {0, 196, 0};
  uint_fast8_t tmp[pars[2]];
  g_assert(u->permission >= 0 && u->permission < 4);
  tmp[0] = u->id >> 8;
  tmp[1] = u->id & 0xff;
  tmp[2] = u->permission;
  memcpy(tmp + 3, u->eigenvals, sizeof *u->eigenvals);
  send_command(0x41, pars, tmp);
  Response r = read_response(0);
  return r.res[2] == success ? 1 : 0;
}

static User *upload_evs(uint_fast16_t user) {
  const uint_fast8_t pars[] = {user >> 8, user & 0xff, 0};
  send_command(0x31, pars, NULL);
  Response r = read_response(1);
  User *u = NULL;
  switch (r.res[2]) {
  case fail:
    fp_warn("Uploading eigenvalues for user %" PRIxFAST16 " failed.", user);
    break;
  case no_user:
    fp_warn("User %" PRIxFAST16 " doesn't exist", user);
    break;
  case success:
    g_assert(193 == r.payload_size);
    u->id = r.payload[0] << 8 | r.payload[1];
    u->permission = r.payload[2];
    memmove(u->eigenvals, r.payload + 3, 193 * sizeof(uint_fast8_t));
  }
  return u;
}

G_DECLARE_FINAL_TYPE(FpiDeviceRaspy, fpi_device_raspy, FPI, DEVICE_RASPY,
                     FpImageDevice)
G_DEFINE_TYPE(FpiDeviceRaspy, fpi_device_raspy, FP_TYPE_IMAGE_DEVICE)

// Close the fingerprint scanner, free heap-allocated resources
static void raspy_close(FpImageDevice *dev) {
  close(fd);
  FpiDeviceRaspy *self = FPI_DEVICE_RASPY(dev);
  GError *err = NULL;

  g_usb_device_release_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0,
                                 0, &err);

  fpi_image_device_close_complete(dev, err);
}

// Opens the fingerprint scanner for functioning
static void raspy_open(FpImageDevice *dev) {
  get_fd_for_usb_serial();
  struct termios term_attributes;
  tcgetattr(fd, &term_attributes);
  term_attributes.c_cflag |= PARODD & PARENB;
  int tmp = tcsetattr(fd, 0, &term_attributes);
  if (tmp == -1)
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  // BSD-specific function...
  tmp = cfsetspeed(&term_attributes, B19200);
  if (tmp == -1)
    fp_warn("Unexpected errno %s: %s", strerrorname_np(errno), strerror(errno));
  FpiDeviceRaspy *self = FPI_DEVICE_RASPY(dev);
  GError *err = NULL;

  g_usb_device_claim_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0, 0,
                               &err);

  fpi_image_device_open_complete(dev, err);
}

// Stubs not needed, it turns out
/*
static void raspy_activate(FpImageDevice *dev) {}

static void raspy_deactivate(FpImageDevice *dev) {}

static void raspy_change_state(FpImageDevice *dev, FpiImageDeviceState state) {}*/

// Product and vendor ID of bridge controller
static const FpIdEntry id_tab[] = {
    {.vid = 0x10c4, .pid = 0xea60},
};

/* Pseudo-RAII functions for the FpImageDeviceClass object.
For now, they are stubs, as most of the initialization 
occurs in fpi_device_raspy_class_init() */
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
  //img_class->activate = raspy_activate;
  //img_class->deactivate = raspy_deactivate;
  //img_class->change_state = raspy_change_state;
  img_class->img_close = raspy_close;
}

