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

#include "WinHelpers.h"
#include "WinDiskModel.h"
#include "MemDiskBackend.h"

#include "gtest/gtest.h"


namespace WParted
{
namespace WinHelpers
{

static const uint64_t MiB = 1024 * 1024;


static Target target()
{
	Target t;
	t.disk = 0;
	t.offset = MiB;
	t.length = 8 * MiB;
	return t;
}


TEST(WinHelpers, GParted_Commands)
{
	const Target t = target();
	EXPECT_EQ(display(Ext::create(t, "ext4", "my disk"), "/dev/sdb2"), "mke2fs -F -t ext4 -L 'my disk' /dev/sdb2");
	EXPECT_EQ(display(Ext::check_repair(t), "/dev/sdb2"), "e2fsck -f -y -v -C 0 /dev/sdb2");
	EXPECT_EQ(display(Ext::set_label(t, "it's"), "/dev/sdb2"), "tune2fs -L 'it'\\''s' /dev/sdb2");
	EXPECT_EQ(display(Ext::set_label(t, ""), "/dev/sdb2"), "tune2fs -L '' /dev/sdb2");
	EXPECT_EQ(display(Ext::new_uuid(t), "/dev/sdb2"), "tune2fs -U random /dev/sdb2");
	EXPECT_EQ(display(Ext::resize(t, 131072, false), "/dev/sdb2"), "resize2fs -p /dev/sdb2 131072K");
	EXPECT_EQ(display(Ext::resize(t, 0, true), "/dev/sdb2"), "resize2fs -p -b /dev/sdb2");
	EXPECT_EQ(display(Ext::info(t), "/dev/sdb2"), "dumpe2fs -h /dev/sdb2");
	EXPECT_EQ(display(Ext::min_size(t), "/dev/sdb2"), "resize2fs -P /dev/sdb2");

	// Every command WParted builds passes validation, with the right access.
	std::string error;
	for (const Command& c : { Ext::create(t, "ext3", "x"), Ext::check_repair(t), Ext::set_label(t, "x"),
	                          Ext::new_uuid(t), Ext::resize(t, 1024, true), Ext::resize(t, 0, false) })
	{
		EXPECT_TRUE(c.writable) << c.program;
		EXPECT_TRUE(validate(c, error)) << c.program << ": " << error;
	}
	for (const Command& c : { Ext::check_read_only(t), Ext::info(t), Ext::min_size(t) })
	{
		EXPECT_FALSE(c.writable) << c.program;
		EXPECT_TRUE(validate(c, error)) << c.program << ": " << error;
	}
}


TEST(WinHelpers, ValidationRefusesTheUnexpected)
{
	std::string error;
	Command c = Ext::check_repair(target());

	Command other = c;
	other.program = "cmd";
	EXPECT_FALSE(validate(other, error));
	other.program = "..\\mke2fs";
	EXPECT_FALSE(validate(other, error));

	Command no_device = c;
	no_device.args.pop_back();
	EXPECT_FALSE(validate(no_device, error));

	Command two_devices = c;
	two_devices.args.push_back(DEVICE);
	EXPECT_FALSE(validate(two_devices, error));

	Command no_target = c;
	no_target.target = Target();
	EXPECT_FALSE(validate(no_target, error));

	// Reading tools never get write access, writing ones never run read-only.
	Command info = Ext::info(target());
	info.writable = true;
	EXPECT_FALSE(validate(info, error));
	Command check = c;
	check.writable = false;
	EXPECT_FALSE(validate(check, error));
}


TEST(WinHelpers, LocateUsesTheLiveLayout)
{
	TestSupport::MemDiskBackend backend;
	backend.add_disk(0, 64 * MiB / 512);
	WinDisk::DiskLayout l;
	l.style = WinDisk::TableStyle::GPT;
	WinDisk::Guid::parse("11111111-2222-3333-4444-555555555555", l.gpt_disk_id);
	l.gpt_first_usable_offset = 34 * 512;
	l.gpt_usable_length = 64 * MiB - 2 * 34 * 512;
	l.gpt_max_entries = 128;
	WinDisk::LayoutEntry e;
	e.offset = 1 * MiB;
	e.length = 20 * MiB;
	e.gpt_type = WinDisk::TypeGuid::LINUX_DATA;
	WinDisk::Guid::parse("00000000-0000-0000-0000-000000000001", e.gpt_id);
	l.entries.push_back(e);
	std::string error;
	ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend.disk(0), l, true, error)) << error;

	WinDisk::DiskBackend* previous = WinDisk::get_backend();
	WinDisk::set_backend(&backend);
	Target t;
	EXPECT_TRUE(locate(0, 1 * MiB, t, error)) << error;
	EXPECT_EQ(t.offset, 1 * MiB);
	EXPECT_EQ(t.length, 20 * MiB);   // As on disk, whatever the caller thinks
	EXPECT_FALSE(locate(0, 2 * MiB, t, error));
	EXPECT_FALSE(locate(3, 1 * MiB, t, error));

	// Running needs the sandbox, which only exists on Windows; elsewhere the
	// command is checked and refused before anything is opened.
	Command bad = Ext::check_repair(t);
	bad.program = "format";
	WinSandbox::Result r = run(bad);
	EXPECT_FALSE(r.started);
	EXPECT_FALSE(r.failure.empty());
	WinDisk::set_backend(previous);
}


}  // namespace WinHelpers
}  // namespace WParted
