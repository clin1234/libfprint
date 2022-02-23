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

#include <fcntl.h>
#include <stdint.h>

static void start() {
  int fd = open ("/dev/tty1", O_RDONLY | O_NONBLOCK);

}


// Naive, but functional
static uint8_t xor(uint8_t* bytes, unsigned long sz) {
  uint8_t checksum = 0;
  for (unsigned long i = 0; i < sz; i++)
    checksum ^= bytes[i];
  return checksum;
}

static uint8_t xor_6_bytes(uint8_t* bytes) {
  return xor(bytes, 6);
}
