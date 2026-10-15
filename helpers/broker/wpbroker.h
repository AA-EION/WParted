/* Copyright (C) 2026 WParted contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Client side of the WParted disk broker (see include/WinBroker.h).
 *
 * Helper programs started by WParted get two inherited pipe handles named by
 * the WPARTED_BROKER environment variable ("<read handle>,<write handle>" in
 * decimal).  They can only access the one partition WParted chose, through
 * these calls.  Offsets are relative to the start of the partition.
 */

#ifndef WPARTED_HELPERS_WPBROKER_H
#define WPARTED_HELPERS_WPBROKER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wpb_client wpb_client;

/* Device name helpers pass to the tools instead of a disk path. */
#define WPB_DEVICE_NAME "wparted-broker"

/* Connect using WPARTED_BROKER.  Returns NULL on failure. */
wpb_client* wpb_connect_env(void);

/* Connect over the given transport handles (Windows HANDLE values or POSIX
 * file descriptors). */
wpb_client* wpb_connect(intptr_t read_handle, intptr_t write_handle);

/* Partition size in bytes, sector size and flags (1 = read-only). */
uint64_t wpb_size(const wpb_client* c);
uint32_t wpb_sector_size(const wpb_client* c);
int      wpb_read_only(const wpb_client* c);

/* 0 on success, a negative broker status otherwise.  Any length; reads may
 * be unaligned; unaligned writes are done as read-modify-write of whole
 * sectors. */
int wpb_read(wpb_client* c, uint64_t offset, void* buffer, size_t length);
int wpb_write(wpb_client* c, uint64_t offset, const void* buffer, size_t length);
int wpb_flush(wpb_client* c);

/* Send CLOSE and release the client. */
void wpb_close(wpb_client* c);

/* Text for a broker status. */
const char* wpb_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif /* WPARTED_HELPERS_WPBROKER_H */
