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

/* WinFsUsage
 *
 * Size and free space of Linux and macOS file systems, read from their on
 * disk metadata without running any tool: the numbers GParted gets on Linux
 * from dumpe2fs, btrfs, xfs_db and so on.
 *
 *   ext2/3/4  superblock block and free block counts
 *   btrfs     superblock total_bytes and bytes_used (CRC32C checked)
 *   xfs       superblock dblocks and fdblocks (CRC checked on v5)
 *   hfs+      volume header total and free blocks
 *   apfs      container block count and space manager free count from the
 *             latest valid checkpoint (Fletcher-64 checked)
 *
 * Everything is bounds checked; anything unexpected gives "unknown".
 */

#ifndef WPARTED_WINFSUSAGE_H
#define WPARTED_WINFSUSAGE_H

#include "WinDiskModel.h"

#include <cstdint>
#include <string>


namespace WParted
{
namespace WinFsUsage
{


struct Usage
{
	uint64_t size = 0;    // Bytes
	uint64_t free = 0;    // Bytes
};

// 'fs' is the GParted file system name.  'reader' reads sectors relative to the
// start of the partition, 'length' is the partition length in bytes.
bool read(const std::string& fs, const WinDisk::SectorReader& reader, uint32_t sector_size,
          uint64_t length, Usage& out, std::string& error);

bool supported(const std::string& fs);

// Exposed for tests.
uint32_t crc32c(const uint8_t* data, size_t length, uint32_t crc = 0);
uint64_t apfs_fletcher64(const uint8_t* block, size_t length);


}  // namespace WinFsUsage
}  // namespace WParted

#endif /* WPARTED_WINFSUSAGE_H */
