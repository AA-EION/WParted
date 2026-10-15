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

/* WinFsOps
 *
 * Native Windows file system operations for partitions, using only documented
 * Windows interfaces:
 *   Windows Storage Management API (WMI root\Microsoft\Windows\Storage)
 *     MSFT_Volume.Format, MSFT_Partition.GetSupportedSize, MSFT_Partition.Resize
 *   chkdsk.exe from the Windows system directory (check and repair)
 *   SetVolumeLabelW, GetDiskFreeSpaceExW, SetVolumeMountPointW,
 *   DeleteVolumeMountPointW, FSCTL_EXTEND_VOLUME
 *
 * Every operation locates its target volume from the partition's disk number
 * and exact byte range, never from a partition number or drive letter.
 */

#ifndef WPARTED_WINFSOPS_H
#define WPARTED_WINFSOPS_H

#if defined(_WIN32)

#include "Partition.h"
#include "OperationDetail.h"

#include <cstdint>
#include <string>


namespace WParted
{
namespace WinFsOps
{


// Windows file system names for MSFT_Volume.Format: "NTFS", "FAT32", "FAT", "exFAT".
bool format(const Partition& partition, const std::string& windows_fs, OperationDetail& operationdetail);

bool set_label(const Partition& partition, OperationDetail& operationdetail);

bool check_repair(const Partition& partition, OperationDetail& operationdetail);

// Grow or shrink partition and NTFS file system together (the start must not change).
bool resize_partition_and_filesystem(const Partition& partition_old, const Partition& partition_new,
                                     OperationDetail& operationdetail);

// Grow the NTFS file system to fill its partition.
bool grow_filesystem_to_partition(const Partition& partition, OperationDetail& operationdetail);

// Sizes Windows allows for resizing this partition.  Cached per device scan.
bool supported_size(const Partition& partition, uint64_t& min_bytes, uint64_t& max_bytes,
                    std::string& error);

// File system size and free space of the partition's volume (needs no drive letter).
bool usage(const Partition& partition, int64_t& fs_size, int64_t& fs_free, std::string& error);

// Assign the first free drive letter.  'mount_point' receives e.g. "E:\".
bool mount(const Partition& partition, std::string& mount_point, std::string& error);

// Remove all drive letters and folder mount points of the partition's volume.
bool unmount(const Partition& partition, std::string& error);

// Forget cached supported sizes (called at each device scan).
void clear_cache();


}  // namespace WinFsOps
}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WINFSOPS_H */
