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

/* GParted FileSystem implementations on Windows for the Linux and macOS file
 * systems WParted handles itself:
 *
 *   WinExtFS    ext2, ext3, ext4: GParted's commands, run with the bundled
 *               e2fsprogs tools in the sandbox (WinHelpers.h);
 *   WinSwapFS   linux-swap: written by WParted (WinSwap.h);
 *   WinUsageFS  btrfs, xfs, HFS+, APFS: usage only, read by WParted
 *               (WinFsUsage.h).  macOS file systems are never written.
 *
 * What each one offers comes from the capability resolver (WinCapabilities.h),
 * so the menus and the explanations in the user interface always agree.
 */

#ifndef WPARTED_WINLINUXFS_H
#define WPARTED_WINLINUXFS_H

#if defined(_WIN32)

#include "FileSystem.h"
#include "ext2.h"


namespace WParted
{


class WinExtFS : public ext2
{
public:
	explicit WinExtFS(FSType fstype) : ext2(fstype) {}

	FS get_filesystem_support() override;
	void set_used_sectors(Partition& partition) override;
	bool write_label(const Partition& partition, OperationDetail& operationdetail) override;
	bool write_uuid(const Partition& partition, OperationDetail& operationdetail) override;
	bool create(const Partition& new_partition, OperationDetail& operationdetail) override;
	bool resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition) override;
	bool check_repair(const Partition& partition, OperationDetail& operationdetail) override;
};


class WinSwapFS : public FileSystem
{
public:
	FS get_filesystem_support() override;
	void set_used_sectors(Partition& partition) override;
	bool write_label(const Partition& partition, OperationDetail& operationdetail) override;
	bool write_uuid(const Partition& partition, OperationDetail& operationdetail) override;
	bool create(const Partition& new_partition, OperationDetail& operationdetail) override;
	bool resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition) override;
};


class WinUsageFS : public FileSystem
{
public:
	explicit WinUsageFS(FSType fstype) : m_fstype(fstype) {}

	FS get_filesystem_support() override;
	void set_used_sectors(Partition& partition) override;

private:
	FSType m_fstype;
};


}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WINLINUXFS_H */
