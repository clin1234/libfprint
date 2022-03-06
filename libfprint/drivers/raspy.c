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

#define FP_COMPONENT "raspy"

struct _FpDeviceRaspy {
  FpImageDevice par;
};

static int get_fd_for_usb_serial(void) {
  // FIXME: assume no more than 10 USB serial devices are present
  char* const tmp;
  int fd = -1;
  for (gushort i = 0; i < 10; i++) {
    int disc = snprintf(tmp, 13, "/dev/ttyUSB%d", i);
    // TODO: Something bad has occured, but how to report it... eh?
    if (disc > 13){}
    fd = open(tmp, O_RDWR | O_NONBLOCK);
    if (fd != -1) break;
  }
  return fd;
}

static void delete_user() {
}



G_DECLARE_FINAL_TYPE(FpDeviceRaspy, fp_device_raspy, FPI, DEVICE_RASPY,
                     FpImageDevice)
G_DEFINE_TYPE(FpDeviceRaspy, fp_device_raspy, FP_TYPE_IMAGE_DEVICE)

static void raspy_close(FpImageDevice* dev) {
  FpDeviceRaspy* self = FPI_DEVICE_RASPY(dev);
  GError* err = NULL;

  g_usb_device_release_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0, 0, &err);

  fpi_image_device_close_complete(dev, err);
}

static void raspy_open(FpImageDevice* dev){
  int fd = get_fd_for_usb_serial();
  struct termios term_attributes;
  tcgetattr(fd, &term_attributes);
  term_attributes.c_cflag |= PARODD & PARENB;
  int tmp = tcsetattr(fd, 0, &term_attributes);
  // BSD-specific function...
  int l = cfsetspeed(&term_attributes, B19200);

  FpDeviceRaspy* self = FPI_DEVICE_RASPY(dev);
  GError* err = NULL;

  g_usb_device_claim_interface(fpi_device_get_usb_device(FP_DEVICE(dev)), 0, 0, &err);

  fpi_image_device_open_complete(dev, err);
}

static void raspy_activate(FpImageDevice* dev) {

}

static void raspy_deactivate(FpImageDevice* dev) {

}

static void raspy_change_state(FpImageDevice* dev, FpiImageDeviceState state) {

}

// Naive, but functional
static uint8_t xor(uint8_t * bytes, unsigned long sz) {
  uint8_t checksum = 0;
  for (unsigned long i = 0; i < sz; i++)
    checksum ^= bytes[i];
  return checksum;
}

static uint8_t xor_6_bytes(uint8_t *bytes) {
  return xor(bytes, 6);
}

// Product and vendor ID of bridge controller
static const FpIdEntry id_tab[] = {
  {.vid = 0x10c4, .pid = 0xea60},
};

static void fpi_device_raspy_init(FpDeviceRaspy *self);
// static void fpi_device_raspy_finalize(GObject *this);
static void fpi_device_raspy_class_init(FpDeviceRaspyClass *klass) {
  FpDeviceClass *dev_class = FP_DEVICE_CLASS(klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS(klass);

  dev_class->id = "raspy";
  dev_class->full_name =
      "Waveshare Fingerprint Sensor (with CP210x USB-UART bridge controller chip)";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_tab;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = 2;
  dev_class->features =
      FP_DEVICE_FEATURE_DUPLICATES_CHECK | FP_DEVICE_FEATURE_IDENTIFY |
      FP_DEVICE_FEATURE_ALWAYS_ON | FP_DEVICE_FEATURE_DUPLICATES_CHECK |
      FP_DEVICE_FEATURE_STORAGE_CLEAR | FP_DEVICE_FEATURE_VERIFY |
      FP_DEVICE_FEATURE_CAPTURE;

  //img_class->bz3_threshold = 24;
  img_class->img_height = img_class->img_width = 280;
  img_class->img_open = raspy_open;
  img_class->activate = raspy_activate;
  img_class->deactivate = raspy_deactivate;
  img_class->change_state = raspy_change_state;
  img_class->img_close = raspy_close;
}
