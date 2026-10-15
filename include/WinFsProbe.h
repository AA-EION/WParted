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

/* WinFsProbe
 *
 * Read only file system signature detection used on Windows instead of the
 * Linux blkid command.  Results use blkid names and formats (TYPE, SEC_TYPE,
 * UUID, LABEL) so the GParted FS_Info cache works unchanged.
 */

#ifndef WPARTED_WINFSPROBE_H
#define WPARTED_WINFSPROBE_H

#include "WinDiskModel.h"

#include <cstdint>
#include <string>


namespace WParted
{
namespace WinFsProbe
{


struct Result
{
	std::string type;            // blkid TYPE, e.g. "ntfs", "vfat", "ext4", "crypto_LUKS"
	std::string sec_type;        // blkid SEC_TYPE, e.g. "msdos" for FAT12/16
	std::string uuid;            // blkid UUID format
	std::string label;
	bool        have_label = false;

	// libparted file system type name for the partition table type logic, or empty
	// when libparted semantics should treat the contents as unknown.
	std::string libparted_name() const;
};


// 'reader' reads sectors relative to the start of the partition (or disk).
// 'length_sectors' is the size of the partition.  Returns false when nothing is
// recognised.
bool probe(const WinDisk::SectorReader& reader, uint32_t sector_size, uint64_t length_sectors,
           Result& result);


}  // namespace WinFsProbe
}  // namespace WParted

#endif /* WPARTED_WINFSPROBE_H */
