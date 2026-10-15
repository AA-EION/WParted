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

/* WinPartedBridge
 *
 * Windows specific extensions of the libparted replacement used by the WParted
 * core (device registry, read-only reasons, partition location).
 */

#ifndef WPARTED_WINPARTEDBRIDGE_H
#define WPARTED_WINPARTEDBRIDGE_H

#include "parted/parted.h"
#include "WinDiskBackend.h"

#include <string>
#include <vector>


namespace WParted
{
namespace WinPartedBridge
{


// Enumerate the disks and remember their identity.  Every later ped_device_get()
// verifies that the disk number still refers to the same physical disk.  Called
// when GParted (re)scans devices.
void refresh_device_registry();

// Disks found by the last refresh_device_registry(), sorted by disk number.
std::vector<WinDisk::DiskInfo> registered_disks();

// Disk number behind a device, -1 if unknown.
int device_index(const PedDevice* dev);

// Information about the disk behind a device.
bool device_info(const PedDevice* dev, WinDisk::DiskInfo& info);

// Why this disk must not be modified (empty when it may be modified).
std::string disk_readonly_reason(const PedDisk* disk);

// Reason recorded for a device path by the last ped_disk_new() on it.  Lets the core
// explain read-only devices after the PedDisk has been destroyed.
std::string device_readonly_reason(const std::string& device_path);

// Byte range of a partition on its disk.
bool partition_extent(const PedPartition* part, uint64_t& offset, uint64_t& length);

// While an instance exists on the current thread, errors are not passed to the
// libparted exception handler (used by background scans).
class QuietScope
{
public:
	QuietScope();
	~QuietScope();
	QuietScope(const QuietScope&) = delete;
	QuietScope& operator=(const QuietScope&) = delete;
};


}  // namespace WinPartedBridge
}  // namespace WParted

#endif /* WPARTED_WINPARTEDBRIDGE_H */
