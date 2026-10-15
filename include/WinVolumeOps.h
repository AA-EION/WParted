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

/* WinVolumeOps
 *
 * Native Windows file system operations on the volume of one partition, using
 * only documented Windows interfaces:
 *   Windows Storage Management API (WMI namespace root\Microsoft\Windows\Storage)
 *     MSFT_Volume.Format, MSFT_Partition.GetSupportedSize, MSFT_Partition.Resize
 *   chkdsk.exe from the Windows system directory (check and repair)
 *   SetVolumeLabelW, GetDiskFreeSpaceExW, SetVolumeMountPointW,
 *   DeleteVolumeMountPointW, FSCTL_GET_NTFS_VOLUME_DATA, FSCTL_EXTEND_VOLUME
 *
 * Every operation locates its volume by disk number and exact partition byte
 * range, never by partition number or drive letter.  Volumes Windows depends on
 * (system, boot, paging) are refused where an operation would disrupt them.
 */

#ifndef WPARTED_WINVOLUMEOPS_H
#define WPARTED_WINVOLUMEOPS_H

#if defined(_WIN32)

#include <cstdint>
#include <functional>
#include <string>


namespace WParted
{
namespace WinVolumeOps
{


struct Target
{
	int      disk = -1;
	uint64_t offset = 0;   // Bytes
	uint64_t length = 0;   // Bytes
};


// Quick format.  'windows_fs' is "NTFS", "FAT32", "FAT" or "exFAT".  Waits up to 30
// seconds for the volume of a just created partition to arrive.
bool format(const Target& t, const std::string& windows_fs, const std::string& label, std::string& error);

// Empty label clears it.
bool set_label(const Target& t, const std::string& label, std::string& error);

// chkdsk /f /x.  'output' receives the chkdsk report as it is produced.  Success
// for chkdsk exit codes 0-2.
bool check_repair(const Target& t, std::function<void(const std::string& output)> output,
                  std::string& command_line, int& exit_code, std::string& error);

// Partition sizes Windows supports for resizing.
bool supported_size(const Target& t, uint64_t& min_bytes, uint64_t& max_bytes, std::string& error);

// Resize the partition and its NTFS file system together (end only).
bool resize(const Target& t, uint64_t new_length, std::string& error);

// Grow NTFS to fill its partition.  'already' is set when there was nothing to do.
bool grow_filesystem_to_partition(const Target& t, bool& already, std::string& error);

bool usage(const Target& t, int64_t& fs_size, int64_t& fs_free, std::string& error);

// Assign the first free drive letter, e.g. "E:\".
bool mount(const Target& t, std::string& mount_point, std::string& error);

// Remove all drive letters and folder mount points.
bool unmount(const Target& t, std::string& error);


}  // namespace WinVolumeOps
}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WINVOLUMEOPS_H */
