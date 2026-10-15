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

#include "gtest/gtest.h"

#include <map>


namespace WParted
{
namespace Caps
{


static Components everything()
{
	Components c;
	c.wparted_tools = true;
	c.file_reader = true;
	c.wsl_usable = true;
	c.wsl_distro_ready = true;
	c.experimental = true;
	return c;
}


static const std::vector<std::string>& all_fs()
{
	static const std::vector<std::string> v = {
		"ntfs", "fat16", "fat32", "exfat", "refs", "udf", "bitlocker",
		"ext2", "ext3", "ext4", "linux-swap", "btrfs", "xfs", "f2fs", "luks", "lvm2 pv", "linux-raid",
		"bcache", "thin-pool", "jfs", "reiserfs", "reiser4", "nilfs2", "minix", "bcachefs", "zfs", "jbd",
		"linux-suspend", "hfs+", "apfs", "hfs", "iso9660", "ufs", "ataraid", "grub2 core.img",
		"unknown", "unallocated", "extended", "" };
	return v;
}


// Every combination of disk situation and installed components.
static std::vector<std::pair<DiskContext, Components>> contexts()
{
	std::vector<std::pair<DiskContext, Components>> out;
	for (int d = 0; d < 64; d++)
		for (int k = 0; k < 32; k++)
		{
			DiskContext disk;
			disk.known = d & 1;
			disk.read_only = d & 2;
			disk.system_disk = d & 4;
			disk.removable = d & 8;
			disk.other_volumes_in_use = d & 16;
			disk.partition_busy = d & 32;
			Components c;
			c.wparted_tools = k & 1;
			c.file_reader = k & 2;
			c.wsl_usable = k & 4;
			c.wsl_distro_ready = k & 8;
			c.experimental = k & 16;
			out.push_back({ disk, c });
		}
	return out;
}


TEST(Capabilities, WindowsFormatsNeverGoToWsl)
{
	for (const std::string& fs : all_fs())
	{
		if (family_of(fs) != Family::WINDOWS)
			continue;
		for (Op op : all_ops())
			for (const auto& ctx : contexts())
			{
				Capability c = resolve(fs, op, ctx.first, ctx.second);
				EXPECT_NE(Provider::WSL, c.provider) << fs << " " << op_name(op);
				EXPECT_FALSE(c.experimental) << fs << " " << op_name(op);
			}
	}
}


TEST(Capabilities, MacFileSystemsAreNeverWritten)
{
	for (const char* fs : { "hfs+", "apfs", "hfs" })
		for (Op op : all_ops())
			for (const auto& ctx : contexts())
			{
				Capability c = resolve(fs, op, ctx.first, ctx.second);
				if (op == Op::INFO || op == Op::BROWSE)
					EXPECT_NE(Provider::WSL, c.provider) << fs;
				else
					EXPECT_EQ(State::READ_ONLY, c.state) << fs << " " << op_name(op);
			}
}


TEST(Capabilities, ReadOnlyDisksAreNeverWritten)
{
	for (const std::string& fs : all_fs())
		for (Op op : all_ops())
		{
			if (op == Op::INFO || op == Op::BROWSE)
				continue;
			for (auto ctx : contexts())
			{
				if (! ctx.first.known || ! ctx.first.read_only)
					continue;
				EXPECT_FALSE(resolve(fs, op, ctx.first, ctx.second).available()) << fs << " " << op_name(op);
			}
		}
}


TEST(Capabilities, WslNeedsAnEligibleDiskAndEverySwitch)
{
	for (const std::string& fs : all_fs())
		for (Op op : all_ops())
			for (const auto& ctx : contexts())
			{
				Capability c = resolve(fs, op, ctx.first, ctx.second);
				if (c.provider != Provider::WSL || ! c.available())
					continue;
				EXPECT_TRUE(c.experimental);
				EXPECT_TRUE(ctx.second.experimental && ctx.second.wsl_usable && ctx.second.wsl_distro_ready);
				if (ctx.first.known)
					EXPECT_FALSE(ctx.first.system_disk || ctx.first.removable || ctx.first.partition_busy ||
					             ctx.first.other_volumes_in_use);
			}
}


TEST(Capabilities, BusyPartitionsOnlyChangedByWindows)
{
	for (const std::string& fs : all_fs())
		for (Op op : all_ops())
			for (auto ctx : contexts())
			{
				if (! ctx.first.known || ! ctx.first.partition_busy)
					continue;
				Capability c = resolve(fs, op, ctx.first, ctx.second);
				if (c.available() && op != Op::INFO)
					EXPECT_EQ(Provider::WINDOWS, c.provider) << fs << " " << op_name(op);
			}
}


// The full matrix with everything installed, for a disk WParted may write.
TEST(Capabilities, Matrix)
{
	struct Row
	{
		const char* fs;
		const char* cells;   // One character per Op in all_ops() order
	};
	// W = Windows, P = WParted, L = WSL, - = not possible, R = read-only, n = not applicable
	//                       INFO CREATE CHECK LABEL UUID GROW SHRINK MOVE COPY BROWSE OPEN_RW
	const Row rows[] = {
		{ "ntfs",          "WWWW-WWPPnn" },
		{ "fat32",         "WWWW---PPnn" },
		{ "fat16",         "WWWW---PPnn" },
		{ "exfat",         "WWWW---PPnn" },
		{ "refs",          "W--------nn" },
		{ "bitlocker",     "W--------nn" },
		{ "ext4",          "PPPPPPPPPPL" },
		{ "ext3",          "PPPPPPPPPPL" },
		{ "ext2",          "PPPPPPPPPPL" },
		{ "linux-swap",    "nPnPPPPPPnn" },
		{ "btrfs",         "PLLLLLLPPLL" },
		{ "xfs",           "PLLLLL-PPLL" },
		{ "f2fs",          "PLL--L-PPLL" },
		{ "luks",          "P----------" },
		{ "lvm2 pv",       "P----------" },
		{ "jfs",           "-------PP--" },
		{ "nilfs2",        "-------PP--" },
		{ "jbd",           "-----------" },
		{ "hfs+",          "PRRRRRRRRPR" },
		{ "apfs",          "PRRRRRRRRPR" },
		{ "hfs",           "PRRRRRRRR-R" },
		{ "iso9660",       "-----------" },
		{ "unknown",       "-----------" },
		{ "",              "-----------" },
	};
	DiskContext disk;
	disk.known = true;
	for (const Row& row : rows)
	{
		ASSERT_EQ(all_ops().size(), std::string(row.cells).size()) << row.fs;
		for (size_t i = 0; i < all_ops().size(); i++)
		{
			Op op = all_ops()[i];
			Capability c = resolve(row.fs, op, disk, everything());
			char got = '-';
			if (c.state == State::READ_ONLY)
				got = 'R';
			else if (c.state == State::NOT_APPLICABLE)
				got = 'n';
			else if (c.available())
				got = c.provider == Provider::WINDOWS ? 'W' : c.provider == Provider::WPARTED ? 'P' : 'L';
			EXPECT_EQ(row.cells[i], got) << row.fs << " / " << op_name(op) << ": " << c.reason;
			if (! c.available())
				EXPECT_STRNE("", c.reason) << row.fs << " / " << op_name(op);
		}
	}
}


TEST(Capabilities, ReasonsForWsl)
{
	Components comps = everything();
	DiskContext disk;
	disk.known = true;

	disk.system_disk = true;
	Capability c = resolve("btrfs", Op::GROW, disk, comps);
	EXPECT_EQ(State::NOT_FOR_THIS_DISK, c.state);
	EXPECT_STREQ("WSL can't work on the disk Windows is installed on.", c.reason);

	disk.system_disk = false;
	disk.removable = true;
	c = resolve("ext4", Op::OPEN_RW, disk, comps);
	EXPECT_STREQ("WSL can't attach USB flash drives or memory cards.", c.reason);

	disk.removable = false;
	disk.other_volumes_in_use = true;
	disk.other_volumes = "E:";
	c = resolve("xfs", Op::CHECK, disk, comps);
	EXPECT_EQ(State::NOT_FOR_THIS_DISK, c.state);
	EXPECT_EQ("E:", c.detail);

	// A disk that can never work with WSL is reported as such even when WSL is missing.
	disk.other_volumes_in_use = false;
	disk.system_disk = true;
	comps.wsl_usable = false;
	EXPECT_EQ(State::NOT_FOR_THIS_DISK, resolve("btrfs", Op::CREATE, disk, comps).state);

	disk.system_disk = false;
	c = resolve("btrfs", Op::CREATE, disk, comps);
	EXPECT_EQ(State::NEEDS_COMPONENT, c.state);
	EXPECT_STRNE("", c.fix);

	comps = everything();
	comps.experimental = false;
	c = resolve("btrfs", Op::CREATE, disk, comps);
	EXPECT_EQ(State::NEEDS_COMPONENT, c.state);
	EXPECT_TRUE(c.experimental);

	comps = everything();
	comps.wsl_kernel_fs = { "ext4", "xfs", "vfat" };
	EXPECT_EQ(State::UNSUPPORTED, resolve("btrfs", Op::GROW, disk, comps).state);
	EXPECT_TRUE(resolve("ext3", Op::OPEN_RW, disk, comps).available());
	EXPECT_TRUE(resolve("xfs", Op::GROW, disk, comps).available());
}


TEST(Capabilities, ReasonsForWparted)
{
	Components comps;
	DiskContext disk;
	disk.known = true;
	Capability c = resolve("ext4", Op::CHECK, disk, comps);
	EXPECT_EQ(State::NEEDS_COMPONENT, c.state);
	// Usage, moving and copying need no helper program.
	EXPECT_TRUE(resolve("ext4", Op::INFO, disk, comps).available());
	EXPECT_TRUE(resolve("ext4", Op::MOVE, disk, comps).available());
	EXPECT_TRUE(resolve("linux-swap", Op::CREATE, disk, comps).available());
	EXPECT_EQ(State::NEEDS_COMPONENT, resolve("apfs", Op::BROWSE, disk, comps).state);

	disk.partition_busy = true;
	disk.busy_reason = "mounted by another driver";
	c = resolve("ext4", Op::BROWSE, disk, everything());
	EXPECT_EQ(State::NOT_FOR_THIS_DISK, c.state);
	EXPECT_EQ("mounted by another driver", c.detail);

	disk.partition_busy = false;
	disk.read_only = true;
	disk.read_only_reason = "dynamic disk";
	EXPECT_TRUE(resolve("apfs", Op::BROWSE, disk, everything()).available());
	EXPECT_FALSE(resolve("ntfs", Op::LABEL, disk, everything()).available());
	EXPECT_EQ("dynamic disk", resolve("ntfs", Op::LABEL, disk, everything()).detail);
}


}  // namespace Caps
}  // namespace WParted
