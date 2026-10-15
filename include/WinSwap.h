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

/* WinSwap
 *
 * Linux swap space, written by WParted itself: what mkswap and swaplabel from
 * util-linux do, for a partition reached through a DiskSession.  Version 1
 * swap header ("SWAPSPACE2"), 4 KiB pages, little-endian, as on x86 and ARM64.
 *
 *     page 0:   bytes 0..1023     zero (boot bits)
 *               1024              u32 version (1)
 *               1028              u32 last page
 *               1032              u32 number of bad pages (0)
 *               1036              u8[16] UUID
 *               1052              char[16] label
 *               page size - 10    "SWAPSPACE2"
 */

#ifndef WPARTED_WINSWAP_H
#define WPARTED_WINSWAP_H

#include "WinDiskBackend.h"

#include <cstdint>
#include <string>


namespace WParted
{
namespace WinSwap
{


const uint32_t PAGE_SIZE   = 4096;
const uint64_t MIN_PAGES   = 10;     // As mkswap
const size_t   LABEL_SIZE  = 16;


struct Info
{
	uint32_t    page_size = 0;
	uint64_t    last_page = 0;
	std::string uuid;           // Lower case, with dashes; empty when all zero
	std::string label;
};


// Write a new swap header over the first page.  An empty 'uuid' makes a random
// one.  Labels longer than 16 bytes and malformed UUIDs are refused.
bool create(WinDisk::DiskSession& session, const std::string& label, const std::string& uuid,
            std::string& error);

// Read the swap header.  False when there is none.
bool read(WinDisk::DiskSession& session, Info& info, std::string& error);

// Change the label or the UUID of existing swap space, as swaplabel.
bool set_label(WinDisk::DiskSession& session, const std::string& label, std::string& error);
bool set_uuid(WinDisk::DiskSession& session, const std::string& uuid, std::string& error);

// "01234567-89ab-cdef-0123-456789abcdef" <-> 16 bytes (RFC 4122 byte order).
bool parse_uuid(const std::string& text, uint8_t out[16]);
std::string format_uuid(const uint8_t in[16]);
std::string random_uuid();


}  // namespace WinSwap
}  // namespace WParted

#endif /* WPARTED_WINSWAP_H */
