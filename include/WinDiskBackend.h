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

/* WinDiskBackend
 *
 * The small set of disk primitives the libparted replacement needs.  The real
 * implementation (Win32DiskBackend) talks to Windows; unit tests install an in
 * memory implementation.
 */

#ifndef WPARTED_WINDISKBACKEND_H
#define WPARTED_WINDISKBACKEND_H

#include "WinDiskModel.h"

#include <cstdint>
#include <string>
#include <vector>


namespace WParted
{
namespace WinDisk
{


struct DiskInfo
{
	int         index = -1;               // N of \\.\PhysicalDriveN
	std::string model;
	std::string vendor;
	std::string serial;
	std::string bus;                      // "NVMe", "SATA", "USB", ...
	std::string instance_id;              // PnP device instance id (stable identity)
	uint32_t    logical_sector_size = 512;
	uint32_t    physical_sector_size = 512;
	uint64_t    size_bytes = 0;
	bool        removable = false;
	bool        read_only = false;        // Write protected / read-only attribute
	bool        offline = false;          // Offline in Windows
	bool        system_disk = false;      // Holds the running Windows installation
	std::string read_only_reason;

	// Identity used to detect that a disk number now refers to a different disk.
	bool same_disk(const DiskInfo& other) const
	{
		return instance_id == other.instance_id && serial == other.serial &&
		       size_bytes == other.size_bytes && logical_sector_size == other.logical_sector_size &&
		       model == other.model;
	}
};


class DiskBackend
{
public:
	virtual ~DiskBackend() {}

	// Disk numbers of all disks, ascending.
	virtual std::vector<int> enumerate_disks() = 0;

	virtual bool query_disk(int index, DiskInfo& info, std::string& error) = 0;

	// IOCTL_DISK_GET_DRIVE_LAYOUT_EX.
	virtual bool read_layout(int index, DiskLayout& layout, std::string& error) = 0;

	// Raw sector access.  Writes must be refused unless the range lies completely
	// inside one existing partition (or the disk has no partition table) whose
	// volume, if any, is not in use; such a volume is locked and dismounted for the
	// duration of the write.
	virtual bool read_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                          void* buffer, std::string& error) = 0;
	virtual bool write_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                           const void* buffer, std::string& error) = 0;

	// Write a new layout.  'current' is the layout the change was computed from and
	// has already been verified to still be on disk.  When 'new_table' is set the
	// partition table style is (re)created first (IOCTL_DISK_CREATE_DISK).  Volumes
	// on partitions which are deleted or changed must be locked and dismounted
	// before writing; if that is not possible nothing may be written.
	virtual bool write_layout(int index, const DiskLayout& current, const DiskLayout& layout,
	                          bool new_table, std::string& error) = 0;

	// IOCTL_DISK_UPDATE_PROPERTIES.
	virtual bool update_properties(int index, std::string& error) = 0;
};


// The backend used by the libparted replacement.  On Windows a Win32 backend is
// installed automatically on first use.  Tests install their own.
DiskBackend* get_backend();
void set_backend(DiskBackend* backend);


}  // namespace WinDisk
}  // namespace WParted

#endif /* WPARTED_WINDISKBACKEND_H */
