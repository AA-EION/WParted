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

/* WinNativeFS
 *
 * GParted FileSystem implementation for the file systems Windows itself
 * manages (NTFS, FAT16, FAT32, exFAT), using WinFsOps.  Used on Windows in
 * place of the upstream ntfs, fat16 and exfat classes, which drive the Linux
 * ntfs-3g, dosfstools and exfatprogs tools.
 */

#ifndef WPARTED_WINNATIVEFS_H
#define WPARTED_WINNATIVEFS_H

#if defined(_WIN32)

#include "FileSystem.h"


namespace WParted
{


class WinNativeFS : public FileSystem
{
public:
	explicit WinNativeFS(FSType fstype) : m_fstype(fstype) {}

	FS get_filesystem_support() override;
	FS_Limits get_filesystem_limits(const Partition& partition) const override;
	void set_used_sectors(Partition& partition) override;
	bool write_label(const Partition& partition, OperationDetail& operationdetail) override;
	bool create(const Partition& new_partition, OperationDetail& operationdetail) override;
	bool resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition) override;
	bool check_repair(const Partition& partition, OperationDetail& operationdetail) override;

	// True when Windows resizes this file system together with its partition (the
	// WParted core then skips its separate file system and partition steps).
	static bool resizes_with_partition(FSType fstype);

private:
	const char* windows_name() const;

	FSType m_fstype;
};


}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WINNATIVEFS_H */
