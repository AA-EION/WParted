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

/* WinDevNames
 *
 * Dual nomenclature.  Internally, and in the GParted user interface, Windows
 * disks are named like Linux SCSI disks so that the GParted core works
 * unchanged and Linux users feel at home:
 *
 *     \\.\PhysicalDrive0  <->  /dev/sda
 *     \\.\PhysicalDrive27 <->  /dev/sdab
 *     partition 3 of disk 0  ->  /dev/sda3
 *
 * Partition numbers follow libparted rules (GPT entry slot, MBR slot 1-4,
 * logical partitions from 5), NOT Windows partition numbers.  Operations on
 * partitions locate their Windows volume by disk number and byte offset, never
 * by number.
 */

#ifndef WPARTED_WINDEVNAMES_H
#define WPARTED_WINDEVNAMES_H

#include <string>


namespace WParted
{
namespace WinDevNames
{


// 0 -> "/dev/sda", 25 -> "/dev/sdz", 26 -> "/dev/sdaa".  Empty for negative index.
std::string disk_path(int disk_index);

// "/dev/sda" -> 0, "\\.\PhysicalDrive3" -> 3.  Returns -1 for anything else,
// including partition paths.  Never guesses.
int disk_index(const std::string& path);

// ("/dev/sda", 3) -> "/dev/sda3"
std::string partition_path(const std::string& disk_path, int partition_number);

// "/dev/sda3" -> disk 0, partition 3.  Returns false for anything else.
bool parse_partition_path(const std::string& path, int& disk_index, int& partition_number);

// "\\.\PhysicalDrive<N>"
std::string physical_drive_path(int disk_index);


}  // namespace WinDevNames
}  // namespace WParted

#endif /* WPARTED_WINDEVNAMES_H */
