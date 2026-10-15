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

/* WinScan
 *
 * One snapshot, taken at each device scan, of everything the GParted core
 * learns from Linux special files on Linux (/proc/partitions, /proc/mounts,
 * blkid): disks, partitions with their location on disk, Windows volumes with
 * their drive letters, and file system signatures.  Replaces those sources on
 * Windows.
 */

#ifndef WPARTED_WINSCAN_H
#define WPARTED_WINSCAN_H

#include "WinFsProbe.h"

#include <cstdint>
#include <string>
#include <vector>


namespace WParted
{
namespace WinScan
{


struct Location
{
	std::string path;          // /dev/sdb3, or /dev/sdb for the whole disk
	std::string device_path;   // /dev/sdb
	int         disk = -1;     // PhysicalDrive number
	int         number = 0;    // libparted partition number, 0 for the whole disk
	uint64_t    offset = 0;    // Bytes
	uint64_t    length = 0;    // Bytes
	uint32_t    sector_size = 512;
	bool        extended = false;
};


struct VolumeState
{
	std::string              guid_path;     // \\?\Volume{GUID}\  (UTF-8)
	std::vector<std::string> mount_points;  // Drive letters and folder mount points
	std::string              fs_name;       // As reported by Windows: NTFS, FAT32, exFAT, ReFS ...
	std::string              label;
	uint32_t                 serial = 0;
	bool                     fs_mounted = false;
	bool                     read_only = false;
	bool                     system = false;
	bool                     paging = false;
	bool                     bitlocker = false;  // BitLocker encrypted (and unlocked when fs_mounted)
};


// Take a new snapshot.  Also refreshes the disk identity registry.
void load();

std::vector<std::string> device_paths();
std::vector<std::string> partition_paths(const std::string& device_path);

bool locate(const std::string& path, Location& out);
bool volume(const std::string& path, VolumeState& out);
bool probe(const std::string& path, WinFsProbe::Result& out);

// Busy as defined in Win32Volumes.h, with a human readable reason.
bool busy(const std::string& path, std::string& reason);


}  // namespace WinScan
}  // namespace WParted

#endif /* WPARTED_WINSCAN_H */
