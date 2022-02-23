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

#ifndef _RASPY
#define _RASPY

/*
 * UART: 19200 baud, 8N1
 *
 * General
 * =======
 *
 * cmd: command
 * P1..P3: up to 3 parameters
 * Q1..Q3: response
 * user ID: unsigned 16-bit (1-0xffff)
 * permission: can be set to 1,2, or 3
 *
 * Q3: Generally, Q3 is valid/invalid information of the operation, it should be:
#define ACK_SUCCESS 0x00 //Success
#define ACK_FAIL 0x01 //Failed
#define ACK_FULL 0x04 //The database is full
#define ACK_NOUSER 0x05 //The user is not exist
#define ACK_USER_OCCUPIED 0x06 //The user was exist
#define ACK_FINGER_OCCUPIED 0x07 //The fingerprint was exist
#define ACK_TIMEOUT 0x08 //Time out
 *
 * 8 bytes
 * Send command:     0xf5 cmd P1 P2 P3 0 checksum 0xf5
 * Receive response: 0xf5 cmd Q1 Q2 Q3 0 checksum 0xf5
 * checksum: XOR of previous 6 bytes
 *
 * >8 bytes
 *  Header:
 *  Send command:     0xf5 cmd len (unsigned 16-bit) 0 0 checksum 0xf5
 *  Receive response: 0xf5 cmd len (unsigned 16-bit) Q3 0 checksum 0xf5
 *    checksum: XOR of previous 6 bytes
 * Data packet:
 *  Send command:     0xf5 data checksum 0xf5
 *  Receive response: 0xf5 data checksum 0xf5
 *  checksum: XOR of previous bytes
 *
 * Commands
 * ========
 *
 * Sleep mode: P1-P3 = 0, Q1-Q3 = 0, cmd = 0x2c
 *
 * Duplication mode (cmd=0x2d):
 *  Send: P1=0
 *
 * Add fingerprint (cmd=0x01):
 *  Send: P1,P2 = user ID, P3 = permission
 *  Receive: Q1,Q2=0,
 *    Q3 = success, fail, user_occupied, finger_occpuied, timeout
 *
 * Delete user (cmd=0x04):
 *  Send: P1,P2 = user ID, P3 = 0
 *  Receive: Q1,Q2 = 0, Q3 = success or fail
 *
 * Delete all users (cmd=0x05):
 *  Send: P1,P2 = 0,
 *    P3 = 0 to delete all, 1-3 to delete users having corresponding position
 *  Receive: Q1,Q2 = 0, Q3 = success or fail
 *
 * Find number of users (cmd=0x09):
 *  Send: P1,P2 = 0
 *    P3 = 0 to query user count, 0xff to query amount (of fingerprints?)
 *  Receive: Q1,Q2: count or amount, Q3 = 0xff if P3=0xff, else success or fail
 *
 * Query permission (cmd=0x0a):
 *  Send: P1,P2 = user ID, Q3 = 0
 *  Receive: Q1,Q2 = 0, Q3 = permission or no user
 *
 * Comparison level (cmd=0x28):
 *  Send: P1 = 0, P2 = 0 if P3=1 else new level (0-9),
 *    P3 = 0 to set level, 1 to query
 *  Receive: Q1 = 0, Q2 = current level, Q3 = success or fail
 *
 *  Default level: 5. Higher level, stricter comparison
 *
 * Fingerprint capture timeout (cmd=0x2e):
 *  Send: P1=0, P2=0 if P3=1 else timeout to be set,
 *    P3=0 to set, 1 to query
 *  Receive: Q1=0, Q2=timeout, Q3=suceess or fail
 * */

#endif
