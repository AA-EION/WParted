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

#include "WinCapabilities.h"
#include "WinFsUsage.h"

#include <algorithm>

// Marks strings for translation (xgettext --keyword=N_); translated when shown.
#ifndef N_
#define N_(s) s
#endif


namespace WParted
{
namespace Caps
{


const std::vector<Op>& all_ops()
{
	static const std::vector<Op> ops = { Op::INFO, Op::CREATE, Op::CHECK, Op::LABEL, Op::UUID, Op::GROW,
	                                     Op::SHRINK, Op::MOVE, Op::COPY, Op::BROWSE, Op::OPEN_RW };
	return ops;
}


const char* op_name(Op op)
{
	switch (op)
	{
		case Op::INFO:    return N_("Read usage");
		case Op::CREATE:  return N_("Create");
		case Op::CHECK:   return N_("Check");
		case Op::LABEL:   return N_("Label");
		case Op::UUID:    return N_("New UUID");
		case Op::GROW:    return N_("Grow");
		case Op::SHRINK:  return N_("Shrink");
		case Op::MOVE:    return N_("Move");
		case Op::COPY:    return N_("Copy");
		case Op::BROWSE:  return N_("Open files (read-only)");
		case Op::OPEN_RW: return N_("Open files (read/write)");
	}
	return "";
}


const char* provider_name(Provider p)
{
	switch (p)
	{
		case Provider::NONE:    return "";
		case Provider::WINDOWS: return "Windows";
		case Provider::WPARTED: return "WParted";
		case Provider::WSL:     return "WSL";
	}
	return "";
}


static bool one_of(const std::string& fs, std::initializer_list<const char*> names)
{
	for (const char* n : names)
		if (fs == n)
			return true;
	return false;
}


Family family_of(const std::string& fs)
{
	if (one_of(fs, { "ntfs", "fat16", "fat32", "exfat", "refs", "udf", "bitlocker" }))
		return Family::WINDOWS;
	if (one_of(fs, { "ext2", "ext3", "ext4", "linux-swap", "btrfs", "xfs", "f2fs", "luks", "lvm2 pv",
	                 "linux-raid", "bcache", "thin-pool", "jfs", "reiserfs", "reiser4", "nilfs2",
	                 "minix", "bcachefs", "zfs", "jbd", "linux-suspend" }))
		return Family::LINUX;
	if (one_of(fs, { "hfs+", "apfs", "hfs" }))
		return Family::APPLE;
	if (one_of(fs, { "iso9660", "ufs", "ataraid", "grub2 core.img" }))
		return Family::OTHER;
	return Family::NONE;
}


static bool writes(Op op)
{
	return op != Op::INFO && op != Op::BROWSE;
}


namespace
{

Capability make(State state, Provider provider, const char* reason, const char* fix = "")
{
	Capability c;
	c.state = state;
	c.provider = provider;
	c.reason = reason;
	c.fix = fix;
	return c;
}

Capability available(Provider provider)
{
	return make(State::AVAILABLE, provider, "");
}

Capability unsupported(const char* reason)
{
	return make(State::UNSUPPORTED, Provider::NONE, reason);
}

Capability not_applicable(const char* reason)
{
	return make(State::NOT_APPLICABLE, Provider::NONE, reason);
}


// Who would do the operation, before looking at the disk and the components.
Capability policy(const std::string& fs, Op op)
{
	switch (family_of(fs))
	{
		case Family::WINDOWS:
		{
			bool native = one_of(fs, { "ntfs", "fat16", "fat32", "exfat" });
			if (op == Op::BROWSE || op == Op::OPEN_RW)
				return not_applicable(N_("Windows opens this file system itself: assign it a drive letter."));
			if (op == Op::MOVE || op == Op::COPY)
				return native ? available(Provider::WPARTED)
				              : unsupported(N_("WParted doesn't move or copy ReFS, UDF or BitLocker volumes."));
			if (! native)
				return op == Op::INFO ? available(Provider::WINDOWS)
				                      : unsupported(N_("WParted doesn't change ReFS, UDF or BitLocker volumes."));
			if (op == Op::UUID)
				return unsupported(N_("Windows can't give an existing file system a new serial number."));
			if ((op == Op::GROW || op == Op::SHRINK) && fs != "ntfs")
				return unsupported(N_("Windows can only resize NTFS file systems."));
			return available(Provider::WINDOWS);
		}

		case Family::LINUX:
		{
			if (one_of(fs, { "ext2", "ext3", "ext4" }))
			{
				if (op == Op::OPEN_RW)
					return available(Provider::WSL);
				return available(Provider::WPARTED);
			}
			if (fs == "linux-swap")
			{
				if (op == Op::INFO)
					return not_applicable(N_("Swap space has no usage to show."));
				if (op == Op::CHECK)
					return not_applicable(N_("Swap space has nothing to check."));
				if (op == Op::BROWSE || op == Op::OPEN_RW)
					return not_applicable(N_("Swap space holds no files."));
				return available(Provider::WPARTED);
			}
			if (one_of(fs, { "btrfs", "xfs", "f2fs" }))
			{
				if (op == Op::INFO)
					return WinFsUsage::supported(fs)
					       ? available(Provider::WPARTED)
					       : unsupported(N_("WParted recognises this file system but can't read its usage."));
				if (op == Op::COPY && fs == "xfs")
					return unsupported(N_("XFS file systems are copied with xfs_copy, which WParted doesn't have on Windows."));
				if (op == Op::MOVE || op == Op::COPY)
					return available(Provider::WPARTED);
				if (op == Op::SHRINK && fs == "xfs")
					return unsupported(N_("XFS file systems can't be shrunk."));
				if (op == Op::SHRINK && fs == "f2fs")
					return unsupported(N_("F2FS file systems can't be shrunk."));
				if ((op == Op::UUID || op == Op::LABEL) && fs == "f2fs")
					return unsupported(N_("The label and UUID of an F2FS file system can only be set when creating it."));
				return available(Provider::WSL);
			}
			if (one_of(fs, { "luks", "lvm2 pv", "linux-raid", "bcache", "thin-pool" }))
			{
				return unsupported(N_("Encrypted (LUKS) volumes, LVM and RAID members are shown for information only."));
			}
			if (op == Op::MOVE || op == Op::COPY)
			{
				if (one_of(fs, { "jbd", "linux-suspend" }))
					return unsupported(N_("WParted doesn't move or copy this kind of partition."));
				return available(Provider::WPARTED);
			}
			if (op == Op::INFO)
				return unsupported(N_("WParted recognises this file system but can't read its usage."));
			return unsupported(N_("Neither WParted nor the WSL kernel can work with this file system."));
		}

		case Family::APPLE:
		{
			if (op == Op::INFO)
				return WinFsUsage::supported(fs)
				       ? available(Provider::WPARTED)
				       : unsupported(N_("WParted recognises this file system but can't read its usage."));
			if (op == Op::BROWSE)
				return fs == "hfs" ? unsupported(N_("Classic Mac OS (HFS) volumes can't be opened."))
				                   : available(Provider::WPARTED);
			return make(State::READ_ONLY, Provider::NONE, N_("macOS file systems are read-only in WParted."));
		}

		case Family::OTHER:
			return unsupported(N_("WParted only recognises this file system."));

		case Family::NONE:
			break;
	}
	return unsupported(N_("There is no file system WParted can work with here."));
}

}  // anonymous namespace


Capability resolve(const std::string& fs, Op op, const DiskContext& disk, const Components& comps)
{
	Capability c = policy(fs, op);
	if (c.state != State::AVAILABLE)
		return c;

	const bool w = writes(op);

	// Disk wide restrictions apply to every provider.
	if (disk.known && w && disk.read_only)
	{
		c = make(State::NOT_FOR_THIS_DISK, c.provider, N_("WParted only reads this disk."));
		c.detail = disk.read_only_reason;
		return c;
	}

	switch (c.provider)
	{
		case Provider::WINDOWS:
			// Online operations on volumes in use are decided by the GParted core
			// from the Windows file system support (online grow, label, ...).
			return c;

		case Provider::WPARTED:
		{
			if (disk.known && disk.partition_busy && (w || op == Op::BROWSE))
			{
				c = make(State::NOT_FOR_THIS_DISK, Provider::WPARTED, N_("The partition is in use by Windows."));
				c.detail = disk.busy_reason;
				return c;
			}
			if ((op == Op::MOVE || op == Op::COPY) && ! comps.block_copy)
				return unsupported(N_("Moving and copying partitions isn't available in this version of WParted."));
			bool helper = family_of(fs) == Family::LINUX && fs != "linux-swap" &&
			              op != Op::INFO && op != Op::MOVE && op != Op::COPY && op != Op::BROWSE;
			if (helper && ! comps.wparted_tools)
				return make(State::NEEDS_COMPONENT, Provider::WPARTED,
				            N_("WParted's Linux file system tools are missing from this installation."),
				            N_("Reinstall WParted."));
			if (op == Op::BROWSE && ! comps.file_reader)
				return make(State::NEEDS_COMPONENT, Provider::WPARTED,
				            N_("WParted's file reader is missing from this installation."),
				            N_("Reinstall WParted."));
			return c;
		}

		case Provider::WSL:
		{
			c.experimental = true;
			if (disk.known)
			{
				Capability d = make(State::NOT_FOR_THIS_DISK, Provider::WSL, "");
				d.experimental = true;
				if (disk.system_disk)
					d.reason = N_("WSL can't work on the disk Windows is installed on.");
				else if (disk.removable)
					d.reason = N_("WSL can't attach USB flash drives or memory cards.");
				else if (disk.partition_busy)
				{
					d.reason = N_("The partition is in use by Windows.");
					d.detail = disk.busy_reason;
				}
				else if (disk.other_volumes_in_use)
				{
					d.reason = N_("WSL takes the whole disk away from Windows, and other volumes on this disk are in use.");
					d.detail = disk.other_volumes;
				}
				if (*d.reason)
					return d;
			}
			if (! comps.wsl_kernel_fs.empty())
			{
				std::string kfs = one_of(fs, { "ext2", "ext3" }) ? std::string("ext4") : fs;
				if (std::find(comps.wsl_kernel_fs.begin(), comps.wsl_kernel_fs.end(), kfs) == comps.wsl_kernel_fs.end())
					return make(State::UNSUPPORTED, Provider::NONE, N_("The WSL kernel has no driver for this file system."));
			}
			if (! comps.experimental)
			{
				Capability n = make(State::NEEDS_COMPONENT, Provider::WSL, N_("Working through WSL is experimental."),
				                    N_("Turn on experimental features in Edit > Preferences."));
				n.experimental = true;
				return n;
			}
			if (! comps.wsl_usable)
			{
				Capability n = make(State::NEEDS_COMPONENT, Provider::WSL, N_("WSL 2 isn't installed or can't attach disks."),
				                    N_("View > File System Support shows how to set it up."));
				n.experimental = true;
				n.detail = comps.wsl_problem;
				return n;
			}
			if (! comps.wsl_distro_ready)
			{
				Capability n = make(State::NEEDS_COMPONENT, Provider::WSL, N_("WParted's Linux environment isn't set up in WSL."),
				                    N_("Set it up in View > File System Support."));
				n.experimental = true;
				return n;
			}
			return c;
		}

		case Provider::NONE:
			break;
	}
	return c;
}


Summary summarise(const std::string& fs, const DiskContext& disk, const Components& comps)
{
	Summary s;
	Family f = family_of(fs);
	s.recognised = f != Family::NONE;
	s.read_only = f == Family::APPLE;
	for (Op op : all_ops())
	{
		Capability c = resolve(fs, op, disk, comps);
		if (c.state == State::NOT_APPLICABLE)
			continue;
		if (c.available())
		{
			if (c.provider == Provider::WINDOWS)
				s.by_windows.push_back(op);
			else if (c.provider == Provider::WPARTED)
				s.by_wparted.push_back(op);
			else if (c.provider == Provider::WSL)
				s.by_wsl.push_back(op);
			continue;
		}
		auto g = std::find_if(s.unavailable.begin(), s.unavailable.end(), [&](const Summary::Group& x)
		{
			return std::string(x.reason) == c.reason && x.detail == c.detail;
		});
		if (g == s.unavailable.end())
		{
			s.unavailable.push_back(Summary::Group{ c.state, c.reason, c.fix, c.detail, {} });
			g = s.unavailable.end() - 1;
		}
		g->ops.push_back(op);
	}
	return s;
}


}  // namespace Caps
}  // namespace WParted
