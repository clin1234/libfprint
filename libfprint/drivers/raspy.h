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
 * eigenvalues: 193 bytes in length
 *
 * Q3: status code when receiving ACK data; see Raspy_ACK_Status
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
 *  Send: P1=0, P2=0 if P3=1 else 1 to enable duplication 0 to disable,
		P3=0 to set mode, 1 to read current
 *	Receive: Q1=0, Q2 = current mode, Q3=success or fail
 *
 * Add fingerprint:
 *	1st command (cmd=0x01):
 *		Send: P1,P2 = user ID, P3 = permission
		Receive: Q1,Q2=0,
			Q3 = success, fail, user_occupied, finger_occpuied, timeout
	2nd command (cmd=0x02):
		Send: P1,P2 = user ID, P3 = permission
		Receive: Q1,Q2=0, Q3 = success, fail, timeout
	3rd command (cmd=0x03):
		Send: P1,P2 = user ID, P3 = permission
		Receive: Q1,Q2=0, Q3 = success, fail, timeout

 * Add user and upload eigenvalues:
 *	First two commands same as first two of Add fingerprint
 *	3rd command (cmd=0x06):
 *		Send: P1-P3=0
 *		Receieve:
 *			Header: Q1-Q2=193, Q3=sucess, fail, or timeout
 *			Data packet: 3 bytes of 0s before eigenvalues
 *		Data packet is receieved only when Q3 = success
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
 * Compare 1:1 (cmd=0x0b):
 *	Send: P1,P2= user ID, P3 = 0
 *	Receieve: Q1,Q2=0, Q3=success, fail, or timeout
 *
 * Compare 1:N (cmd=0x0c):
 *	Send: P1-P3=0
 *	Receieve: Q1,Q2 = user ID, Q3=permission, no user, or timeout
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
 * Get image and upload (cmd=0x24):
 *	Send: P1-P3=0
 *	Receieve:
 *		Header: Q1,Q2= 9800 (image length), Q3 = success, fail, or
timeout
		Data packet: data is image
 *
 * In DSP module, the pixels of fingerprint image are 280*280, every pixel is
represented by 8 bits. When uploading, DSP is skip pixels sampling in
horizontal/vertical direction to reduce data size, so that the image became
140*140, and just take the high 4 bits of pixel. each two pixels composited into
one byte for transferring (previous pixel high 4-bit, last pixel low 4-pixe).

Transmission starts line by line from the first line, each line starts from the
first pixel, totally transfer 140* 140/ 2 bytes of data

 * Get image and upload eigenvalues (cmd=0x23):
 *	Send and receive: same as cmd=0x06
 *
 * Download eigenvalues and compare with fingerprint acquired (cmd=0x44):
 *	Send:
 *		Header: P1-P2=193, P3=0
 *		Packet data: first 3 bytes are 0s, followed by eiegnvalues
 *	Receieve: Q1,Q2=0, Q3=success, fail, or timeout
 *
 * Download eigenvalues, compare 1:1 (cmd=0x42):
 *	Send:
 *		Header: P1-P2=193, P3=0
 *		Data packet: first 2 bytes are user ID, followed by 0,
 *			followed by eigenvalues
 *	Receieve: Q1-Q2=0, Q3=success or fail (cmd=0x43 for receiving bytes,
maybe erratum?)
 *
 * Download eigenvalues, compare 1:N (cmd=0x43):
 *	Send:
 *		Header: P1-P2=193, P3=0
 *		Data packet: first 3 bytes are 0s, followed by eigenvalues
 *	Receieve: Q1-Q2=user ID, Q3=permission or no user
 *
 * Upload eigenvalues from DSP (cmd=0x31):
 *	Send: P1-P2=user ID, P3=0
 *	Receieve:
 *		Header: Q1-Q2=192, Q3=success, fail, no user
 *		Data packet: first 2 bytes are user ID, followed by permission,
 *			followed by eigenvalues
 *
 * Download eigenvalues and save as user ID to DSP (cmd=0x41):
 *	Send:
 *		Header: P1-P2=193, P3=0
 *		Data packet: first 2 bytes are user ID, followed by permission,
 *			followed by eigenvalues
 *	Receieve: Q1-Q2 = user ID, Q3 = success or fail
 *
 * Query information of all users (cmd=0x2b):
 *	Send: P1-P3=0
 *	Receieve:
 *		Header: Q1,Q2=length (3*user ID+2), Q3=success or fail
 *		Data packet: first 2 bytes of data are user ID (max?),
 *			rest contains repeating 3-bytes of user ID and its
 *			permission, until user ID reaches user ID of first 2 bytes
 *
 * Fingerprint capture timeout (cmd=0x2e):
 *  Send: P1=0, P2=0 if P3=1 else timeout to be set,
 *    P3=0 to set, 1 to query
 *  Receive: Q1=0, Q2=timeout, Q3=suceess or fail
 *
 *	Timeout is t0 * timeout, where t0 is 0.2-0.3s
 *
 * Processes
 * =========
 * Add fingerprint
 * Send CMD=0x01:
 * -- Begin repetition
 * If database is full:
 *	Q3=full
 * Else Acquire fingerprint
 * If timeout, Q3=timeout
 * Else Process image
 * If Eigenvalue is less, Q3=fail
 * Else Q3=success
 * -- Repeat for CMD0x02.
 * For CMD=0x03, repeat above steps until Process image step.
 * If Q3!=fail, judge uniqueness (only do if duplication mode off)
 * If fingerprint already exists, Q3=user exists
 * Else add fingerprint, Q3=success
 *
 * Delete a user:
 * Send CMD=0x04 (delete user who has user ID)
 * If fail, Q3=fail
 * Else Q3=success
 *
 * Delete all users:
 * Send CMD=0x05 (delete all users)
 * If fail, Q3=fail
 * Else Q3=success
 *
 * Acquire image and upload eigenvalue:
 * Send CMD=0x23 (acquire fingerprint)
 * If timeout, Q3=timeout
 * Else Process image
 * If eigenvalue if less, Q3=fail
 * Else Receieve eigenvalues, Q3=success
 * */

struct _Raspy {
  const FpDevice *fp;
};

enum Raspy_ACK_Status {
  success,
  fail,
  database_full = 4,
  no_user,
  user_already_exists,
  fingeprint_already_exists,
  timeout
};

#endif
