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

#if defined(_WIN32)

#include "WinNativeFS.h"
#include "WinFsOps.h"
#include "WinCapabilities.h"
#include "WinComponents.h"
#include "OperationDetail.h"
#include "Partition.h"
#include "Utils.h"


namespace WParted
{


bool WinNativeFS::resizes_with_partition(FSType fstype)
{
	return fstype == FS_NTFS;
}


const char* WinNativeFS::windows_name() const
{
	switch (m_fstype)
	{
		case FS_NTFS:  return "NTFS";
		case FS_FAT16: return "FAT";
		case FS_FAT32: return "FAT32";
		case FS_EXFAT: return "exFAT";
		default:       return "";
	}
}


FS WinNativeFS::get_filesystem_support()
{
	FS fs(m_fstype);

	// Busy when the volume has a drive letter or mount point, or Windows uses it.
	fs.busy = FS::GPARTED;

	// Usage, label and creation work through the Windows volume, with or without
	// a drive letter.  Labels and UUIDs are read by FS_Info.
	fs.read               = FS::EXTERNAL;
	fs.online_read        = FS::EXTERNAL;
	fs.write_label        = FS::EXTERNAL;
	fs.online_write_label = FS::EXTERNAL;
	fs.create             = FS::EXTERNAL;
	fs.create_with_label  = FS::EXTERNAL;
	fs.check              = FS::EXTERNAL;

	if (m_fstype == FS_NTFS)
	{
		// Windows grows and shrinks NTFS together with its partition, also while
		// in use (e.g. C:).  Only the end of the partition can move.
		fs.grow          = FS::EXTERNAL;
		fs.shrink        = FS::EXTERNAL;
		fs.online_grow   = FS::EXTERNAL;
		fs.online_shrink = FS::EXTERNAL;
	}

	// Moving and copying: GParted's block copy, with the volumes locked and every
	// block verified (WinCopy.h), when the capability resolver allows it.
	const std::string name = Utils::get_filesystem_string(m_fstype).raw();
	const Caps::Components comps = WinComponents::current();
	if (Caps::resolve(name, Caps::Op::MOVE, Caps::DiskContext(), comps).available())
		fs.move = FS::GPARTED;
	if (Caps::resolve(name, Caps::Op::COPY, Caps::DiskContext(), comps).available())
		fs.copy = FS::GPARTED;

	switch (m_fstype)
	{
		case FS_NTFS:
			m_fs_limits = FS_Limits(8 * MEBIBYTE, 0);
			break;
		case FS_FAT16:
			m_fs_limits = FS_Limits(16 * MEBIBYTE, (4096 - 1) * MEBIBYTE);
			break;
		case FS_FAT32:
			// Windows formats FAT32 only up to 32 GiB.
			m_fs_limits = FS_Limits(33 * MEBIBYTE, 32 * GIBIBYTE);
			break;
		case FS_EXFAT:
			m_fs_limits = FS_Limits(8 * MEBIBYTE, 0);
			break;
		default:
			break;
	}
	return fs;
}


FS_Limits WinNativeFS::get_filesystem_limits(const Partition& partition) const
{
	FS_Limits limits = m_fs_limits;
	// For an existing NTFS ask Windows how far it can be shrunk (unmovable files)
	// and grown.
	if (m_fstype == FS_NTFS && partition.fstype == FS_NTFS && partition.status == STAT_REAL)
	{
		uint64_t min_bytes = 0, max_bytes = 0;
		std::string error;
		if (WinFsOps::supported_size(partition, min_bytes, max_bytes, error) && min_bytes > 0)
		{
			limits.min_size = std::max<Byte_Value>(limits.min_size, static_cast<Byte_Value>(min_bytes));
			limits.max_size = static_cast<Byte_Value>(max_bytes);
		}
	}
	return limits;
}


void WinNativeFS::set_used_sectors(Partition& partition)
{
	int64_t size = 0, free = 0;
	std::string error;
	if (WinFsOps::usage(partition, size, free, error))
		partition.set_sector_usage(size / partition.sector_size, free / partition.sector_size);
	else
		partition.push_back_message(error);
}


bool WinNativeFS::write_label(const Partition& partition, OperationDetail& operationdetail)
{
	return WinFsOps::set_label(partition, operationdetail);
}


bool WinNativeFS::create(const Partition& new_partition, OperationDetail& operationdetail)
{
	return WinFsOps::format(new_partition, windows_name(), operationdetail);
}


bool WinNativeFS::resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition)
{
	if (m_fstype == FS_NTFS && fill_partition)
		return WinFsOps::grow_filesystem_to_partition(partition_new, operationdetail);

	operationdetail.add_child(OperationDetail(
	        _("Resizing this file system separately from its partition is not supported on Windows"),
	        STATUS_ERROR, FONT_ITALIC));
	return false;
}


bool WinNativeFS::check_repair(const Partition& partition, OperationDetail& operationdetail)
{
	return WinFsOps::check_repair(partition, operationdetail);
}


}  // namespace WParted

#endif /* _WIN32 */
