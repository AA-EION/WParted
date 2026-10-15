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

/* Win32Volumes
 *
 * Windows volume enumeration, in-use detection and exclusive locking.
 *
 * On Windows a file system is "mounted" by the file system driver as soon as
 * its volume arrives, with or without a drive letter.  WParted therefore uses
 * these definitions:
 *   - busy (GParted "mounted"): the volume has a drive letter or folder mount
 *     point, or holds the running Windows installation, an active paging,
 *     swap or hibernation file.
 *   - exclusive access for raw writes or partition changes: FSCTL_LOCK_VOLUME
 *     followed by FSCTL_DISMOUNT_VOLUME.  Never forced.
 */

#ifndef WPARTED_WIN32VOLUMES_H
#define WPARTED_WIN32VOLUMES_H

#if defined(_WIN32)

#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace WParted
{
namespace Win32Volumes
{


struct Extent
{
	uint32_t disk = 0;
	uint64_t offset = 0;
	uint64_t length = 0;
};


struct Volume
{
	std::wstring              guid_path;      // \\?\Volume{GUID}\ (trailing backslash)
	std::vector<std::wstring> mount_points;   // C:\ , D:\Mount\ ...
	std::vector<Extent>       extents;
	std::wstring              fs_name;        // NTFS, FAT32, exFAT, ReFS, ... empty if RAW
	std::wstring              label;
	uint32_t                  serial = 0;
	bool                      fs_read_only = false;
	bool                      system = false;     // Contains the running Windows
	bool                      paging = false;     // pagefile.sys / swapfile.sys / hiberfil.sys
	bool                      fs_mounted = false; // GetVolumeInformation succeeded

	bool busy() const  { return ! mount_points.empty() || system || paging; }
	std::string busy_reason() const;
	bool on_disk_range(uint32_t disk, uint64_t offset, uint64_t length) const;  // Overlaps
	bool exactly(uint32_t disk, uint64_t offset, uint64_t length) const;       // Single extent equal
};


std::vector<Volume> enumerate();

// The volume consisting of exactly this partition, if any.
bool find_partition_volume(uint32_t disk, uint64_t offset, uint64_t length, Volume& out);

// Wait (up to timeout_ms) for the volume of a partition to arrive after a partition
// table change.
bool wait_for_partition_volume(uint32_t disk, uint64_t offset, uint64_t length,
                               unsigned timeout_ms, Volume& out);

// Device path for CreateFile: \\?\Volume{GUID} without trailing backslash.
std::wstring device_path(const Volume& volume);

std::string to_utf8(const std::wstring& s);
std::wstring from_utf8(const std::string& s);
std::string last_error_message(unsigned long error_code);


// Exclusive lock of one volume.  Locks and dismounts in lock(); unlocks and closes
// in the destructor.
class Lock
{
public:
	Lock() = default;
	~Lock();
	Lock(const Lock&) = delete;
	Lock& operator=(const Lock&) = delete;

	bool lock(const Volume& volume, std::string& error);
	void release();

private:
	void*        m_handle = nullptr;
	bool         m_locked = false;
	std::wstring m_path;
};


// Lock and dismount every volume overlapping any of the ranges on the disk.  Fails,
// releasing everything, if any of them is busy or can not be locked.
bool lock_ranges(uint32_t disk, const std::vector<Extent>& ranges,
                 std::vector<std::unique_ptr<Lock>>& locks, std::string& error);


}  // namespace Win32Volumes
}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WIN32VOLUMES_H */
