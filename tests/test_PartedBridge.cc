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

/* End to end tests of the libparted replacement, driven exactly the way the
 * GParted core drives libparted, against in-memory disks.
 */

#include "parted/parted.h"
#include "WinPartedBridge.h"
#include "WinDiskModel.h"
#include "MemDiskBackend.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


namespace WParted
{

using WinDisk::DiskLayout;
using WinDisk::LayoutEntry;
using WinDisk::TableStyle;
namespace TypeGuid = WinDisk::TypeGuid;


static std::vector<std::string> g_messages;

static PedExceptionOption record_exception(PedException* ex)
{
	g_messages.push_back(ex->message);
	return PED_EXCEPTION_CANCEL;
}


class PartedBridgeTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		backend.reset(new TestSupport::MemDiskBackend());
		backend->add_disk(0, 2097152);                       // 1 GiB
		backend->add_disk(1, 4194304, 512, "SECOND");        // 2 GiB
		WinDisk::set_backend(backend.get());
		ped_exception_set_handler(record_exception);
		g_messages.clear();
		WinPartedBridge::refresh_device_registry();
	}

	void TearDown() override
	{
		WinDisk::set_backend(nullptr);
		ped_exception_set_handler(nullptr);
	}

	// GParted's new_disklabel()
	void make_label(const char* path, const char* type)
	{
		PedDevice* dev = ped_device_get(path);
		ASSERT_NE(nullptr, dev);
		PedDisk* disk = ped_disk_new_fresh(dev, ped_disk_type_get(type));
		ASSERT_NE(nullptr, disk);
		ASSERT_TRUE(ped_disk_commit(disk)) << last_message();
		ped_disk_destroy(disk);
		ped_device_destroy(dev);
	}

	// GParted's create_partition() followed by set_partition_type_using_fstype()
	int add(const char* path, PedPartitionType type, PedSector start, PedSector end, const char* fs = "ntfs")
	{
		PedDevice* dev = ped_device_get(path);
		EXPECT_NE(nullptr, dev);
		PedDisk* disk = ped_disk_new(dev);
		EXPECT_NE(nullptr, disk) << last_message();
		if (! disk)
			return -1;
		PedPartition* part = ped_partition_new(disk, type, type == PED_PARTITION_EXTENDED ? nullptr
		                                                     : ped_file_system_type_get("ext2"), start, end);
		EXPECT_NE(nullptr, part);
		PedGeometry* geom = ped_geometry_new(dev, start, end - start + 1);
		PedConstraint* constraint = ped_constraint_exact(geom);
		ped_geometry_destroy(geom);
		int num = -1;
		bool added = ped_disk_add_partition(disk, part, constraint);
		if (! added)
			ped_partition_destroy(part);
		if (added && ped_disk_commit(disk))
		{
			num = part->num;
			if (type != PED_PARTITION_EXTENDED)
			{
				for (int f = PED_PARTITION_FIRST_FLAG; f <= PED_PARTITION_LAST_FLAG; f++)
					if (ped_partition_is_flag_available(part, static_cast<PedPartitionFlag>(f)))
						ped_partition_set_flag(part, static_cast<PedPartitionFlag>(f), 0);
				ped_partition_set_system(part, ped_file_system_type_get(fs));
				EXPECT_TRUE(ped_disk_commit(disk)) << last_message();
			}
		}
		ped_constraint_destroy(constraint);
		ped_disk_destroy(disk);
		ped_device_destroy(dev);
		return num;
	}

	DiskLayout layout(int index)
	{
		DiskLayout l;
		std::string err;
		EXPECT_TRUE(backend->read_layout(index, l, err)) << err;
		return l;
	}

	std::string last_message() const
	{
		return g_messages.empty() ? std::string("(no message)") : g_messages.back();
	}

	std::unique_ptr<TestSupport::MemDiskBackend> backend;
};


TEST_F(PartedBridgeTest, DeviceEnumerationAndPaths)
{
	PedDevice* dev = ped_device_get_next(nullptr);
	ASSERT_NE(nullptr, dev);
	EXPECT_STREQ("/dev/sda", dev->path);
	EXPECT_EQ(2097152, dev->length);
	EXPECT_EQ(512, dev->sector_size);
	EXPECT_STREQ("WParted Test Disk", dev->model);
	dev = ped_device_get_next(dev);
	ASSERT_NE(nullptr, dev);
	EXPECT_STREQ("/dev/sdb", dev->path);
	EXPECT_EQ(nullptr, ped_device_get_next(dev));

	// Strict path handling: never default to disk 0.
	EXPECT_EQ(nullptr, ped_device_get("/dev/sda1"));
	EXPECT_EQ(nullptr, ped_device_get("/dev/nvme0n1"));
	EXPECT_EQ(nullptr, ped_device_get("garbage"));
	EXPECT_EQ(nullptr, ped_device_get("/dev/sdc"));
	PedDevice* by_win = ped_device_get("\\\\.\\PhysicalDrive1");
	ASSERT_NE(nullptr, by_win);
	EXPECT_STREQ("/dev/sdb", by_win->path);
}


TEST_F(PartedBridgeTest, UninitialisedDiskIsUnrecognised)
{
	PedDevice* dev = ped_device_get("/dev/sda");
	ASSERT_NE(nullptr, dev);
	EXPECT_EQ(nullptr, ped_disk_new(dev));
	EXPECT_NE(std::string::npos, last_message().find("unrecognised disk label"));
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, DiskIdentityChangeIsRefused)
{
	make_label("/dev/sdb", "gpt");
	// Disk 1 is replaced by a different disk with the same number (e.g. USB swap).
	backend->remove_disk(1);
	backend->add_disk(1, 4194304, 512, "OTHER");
	g_messages.clear();
	EXPECT_EQ(nullptr, ped_device_get("/dev/sdb"));
	EXPECT_NE(std::string::npos, last_message().find("different disk"));
}


TEST_F(PartedBridgeTest, GptCreateFlagsNamesAndPreservation)
{
	make_label("/dev/sda", "gpt");
	EXPECT_EQ(1, add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847, "fat32"));
	EXPECT_EQ(2, add("/dev/sda", PED_PARTITION_NORMAL, 206848, 239615, "ntfs"));
	EXPECT_EQ(3, add("/dev/sda", PED_PARTITION_NORMAL, 239616, 1048575, "ntfs"));
	EXPECT_EQ(4, add("/dev/sda", PED_PARTITION_NORMAL, 1048576, 1050623, "ext4"));

	// Flags and name, the way Win_GParted's Manage Flags and Name Partition do.
	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk) << last_message();
	EXPECT_STREQ("gpt", disk->type->name);
	PedPartition* esp = ped_disk_get_partition_by_sector(disk, 100000);
	PedPartition* msr = ped_disk_get_partition_by_sector(disk, 220000);
	PedPartition* data = ped_disk_get_partition_by_sector(disk, 500000);
	ASSERT_TRUE(esp && msr && data);
	EXPECT_EQ(1, esp->num);
	EXPECT_EQ(nullptr, esp->fs_type);  // No file system written yet
	ASSERT_TRUE(ped_partition_set_flag(esp, PED_PARTITION_ESP, 1));
	ASSERT_TRUE(ped_partition_set_flag(msr, PED_PARTITION_MSFT_RESERVED, 1));
	ASSERT_TRUE(ped_partition_set_flag(data, PED_PARTITION_HIDDEN, 1));
	ASSERT_TRUE(ped_partition_set_name(data, u8"Datos de usuario é"));
	ASSERT_TRUE(ped_disk_commit(disk)) << last_message();
	char* path = ped_partition_get_path(data);
	EXPECT_STREQ("/dev/sda3", path);
	std::free(path);
	ped_disk_destroy(disk);

	DiskLayout l = layout(0);
	ASSERT_EQ(TableStyle::GPT, l.style);
	ASSERT_EQ(4u, l.entries.size());
	EXPECT_EQ(TypeGuid::ESP, l.entries[0].gpt_type);
	EXPECT_EQ(TypeGuid::MSFT_RESERVED, l.entries[1].gpt_type);
	EXPECT_EQ(TypeGuid::BASIC_DATA, l.entries[2].gpt_type);
	EXPECT_EQ(WinDisk::GPT_BASIC_DATA_ATTR_HIDDEN, l.entries[2].gpt_attributes);
	EXPECT_EQ(u"Datos de usuario é", l.entries[2].gpt_name);
	EXPECT_EQ(TypeGuid::LINUX_DATA, l.entries[3].gpt_type);
	WinDisk::Guid id3 = l.entries[2].gpt_id;

	// Deleting one partition must not touch the type, id, name or attributes of any
	// other.  (The previous implementation rewrote every partition as Basic Data
	// with a new random GUID.)
	disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk);
	PedPartition* p4 = ped_disk_get_partition_by_sector(disk, 1049000);
	ASSERT_NE(nullptr, p4);
	ASSERT_TRUE(ped_disk_delete_partition(disk, p4));
	ASSERT_TRUE(ped_disk_commit(disk)) << last_message();
	ped_disk_destroy(disk);

	DiskLayout after = layout(0);
	ASSERT_EQ(3u, after.entries.size());
	for (int i = 0; i < 3; i++)
	{
		EXPECT_EQ(l.entries[i].gpt_type, after.entries[i].gpt_type);
		EXPECT_EQ(l.entries[i].gpt_id, after.entries[i].gpt_id);
		EXPECT_EQ(l.entries[i].gpt_attributes, after.entries[i].gpt_attributes);
		EXPECT_EQ(l.entries[i].gpt_name, after.entries[i].gpt_name);
	}
	EXPECT_EQ(id3, after.entries[2].gpt_id);
	EXPECT_EQ(l.gpt_disk_id, after.gpt_disk_id);

	// Flags read back as libparted reports them.
	disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk);
	esp = ped_disk_get_partition_by_sector(disk, 100000);
	data = ped_disk_get_partition_by_sector(disk, 500000);
	EXPECT_TRUE(ped_partition_get_flag(esp, PED_PARTITION_BOOT));
	EXPECT_TRUE(ped_partition_get_flag(esp, PED_PARTITION_ESP));
	EXPECT_TRUE(ped_partition_get_flag(data, PED_PARTITION_MSFT_DATA));
	EXPECT_TRUE(ped_partition_get_flag(data, PED_PARTITION_HIDDEN));
	EXPECT_FALSE(ped_partition_is_flag_available(data, PED_PARTITION_LBA));
	EXPECT_STREQ(u8"Datos de usuario é", ped_partition_get_name(data));
	ped_disk_destroy(disk);
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, UnknownGptTypesSurviveOtherChanges)
{
	make_label("/dev/sda", "gpt");
	ASSERT_EQ(1, add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847, "ntfs"));

	// Give partition 1 a type WParted knows nothing about.
	DiskLayout l = layout(0);
	ASSERT_TRUE(WinDisk::Guid::parse("01234567-89AB-CDEF-0123-456789ABCDEF", l.entries[0].gpt_type));
	l.entries[0].gpt_attributes = 0x00FF000000000001ULL;
	std::string err;
	ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend->disk(0), l, false, err)) << err;

	ASSERT_EQ(2, add("/dev/sda", PED_PARTITION_NORMAL, 206848, 411647, "ext4"));
	DiskLayout after = layout(0);
	ASSERT_EQ(2u, after.entries.size());
	EXPECT_EQ(l.entries[0].gpt_type, after.entries[0].gpt_type);
	EXPECT_EQ(l.entries[0].gpt_attributes, after.entries[0].gpt_attributes);
	EXPECT_EQ(l.entries[0].gpt_id, after.entries[0].gpt_id);
}


TEST_F(PartedBridgeTest, MbrPrimaryExtendedLogical)
{
	make_label("/dev/sda", "msdos");
	EXPECT_EQ(1, add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847, "ntfs"));
	EXPECT_EQ(2, add("/dev/sda", PED_PARTITION_EXTENDED, 206848, 2097151));
	EXPECT_EQ(5, add("/dev/sda", PED_PARTITION_LOGICAL, 208896, 411647, "ext4"));
	EXPECT_EQ(6, add("/dev/sda", PED_PARTITION_LOGICAL, 413696, 616447, "linux-swap"));
	EXPECT_EQ(7, add("/dev/sda", PED_PARTITION_LOGICAL, 618496, 2097151, "fat32"));

	DiskLayout l = layout(0);
	ASSERT_EQ(TableStyle::MBR, l.style);
	ASSERT_EQ(16u, l.entries.size());
	EXPECT_EQ(0x07, l.entries[0].mbr_type);
	EXPECT_EQ(0x05, l.entries[1].mbr_type);
	EXPECT_EQ(0x83, l.entries[4].mbr_type);
	EXPECT_EQ(0x82, l.entries[8].mbr_type);
	EXPECT_EQ(0x0B, l.entries[12].mbr_type);

	// Delete the middle logical: libparted renumbers the following logical.
	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk) << last_message();
	PedPartition* p6 = ped_disk_get_partition_by_sector(disk, 500000);
	ASSERT_NE(nullptr, p6);
	EXPECT_EQ(6, p6->num);
	EXPECT_EQ(PED_PARTITION_LOGICAL, p6->type);
	ASSERT_TRUE(ped_disk_delete_partition(disk, p6));
	PedPartition* p7 = ped_disk_get_partition_by_sector(disk, 1000000);
	EXPECT_EQ(6, p7->num);
	ASSERT_TRUE(ped_disk_commit(disk)) << last_message();

	// Iteration order: primary, extended, logicals.
	int expected_nums[] = { 1, 2, 5, 6 };
	int i = 0;
	for (PedPartition* p = ped_disk_next_partition(disk, nullptr); p; p = ped_disk_next_partition(disk, p))
		EXPECT_EQ(expected_nums[i++], p->num);
	EXPECT_EQ(4, i);
	EXPECT_EQ(2, ped_disk_extended_partition(disk)->num);
	ped_disk_destroy(disk);

	// The extended partition can not be deleted while it holds logicals.
	disk = ped_disk_new(dev);
	EXPECT_FALSE(ped_disk_delete_partition(disk, ped_disk_extended_partition(disk)));
	ped_disk_destroy(disk);

	DiskLayout after = layout(0);
	ASSERT_EQ(12u, after.entries.size());
	EXPECT_EQ(0x83, after.entries[4].mbr_type);
	EXPECT_EQ(0x0B, after.entries[8].mbr_type);
	EXPECT_EQ(l.mbr_signature, after.mbr_signature);
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, MbrBootFlagIsExclusive)
{
	make_label("/dev/sda", "msdos");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847, "ntfs");
	add("/dev/sda", PED_PARTITION_NORMAL, 206848, 411647, "ntfs");
	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	PedPartition* a = ped_disk_get_partition_by_sector(disk, 3000);
	PedPartition* b = ped_disk_get_partition_by_sector(disk, 300000);
	ASSERT_TRUE(ped_partition_set_flag(a, PED_PARTITION_BOOT, 1));
	ASSERT_TRUE(ped_partition_set_flag(b, PED_PARTITION_BOOT, 1));
	EXPECT_FALSE(ped_partition_get_flag(a, PED_PARTITION_BOOT));
	EXPECT_TRUE(ped_partition_get_flag(b, PED_PARTITION_BOOT));
	ASSERT_TRUE(ped_disk_commit(disk));
	ped_disk_destroy(disk);
	DiskLayout l = layout(0);
	EXPECT_FALSE(l.entries[0].mbr_boot);
	EXPECT_TRUE(l.entries[1].mbr_boot);
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, InvalidPartitionsAreRejected)
{
	make_label("/dev/sda", "gpt");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	g_messages.clear();
	// Overlap
	EXPECT_EQ(-1, add("/dev/sda", PED_PARTITION_NORMAL, 200000, 300000));
	EXPECT_NE(std::string::npos, last_message().find("overlap"));
	// Beyond the usable area (backup GPT)
	EXPECT_EQ(-1, add("/dev/sda", PED_PARTITION_NORMAL, 300000, 2097151));
	// GPT has no logical partitions
	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	EXPECT_EQ(nullptr, ped_partition_new(disk, PED_PARTITION_LOGICAL, nullptr, 300000, 400000));
	ped_disk_destroy(disk);
	ped_device_destroy(dev);
	EXPECT_EQ(1u, layout(0).entries.size());
}


TEST_F(PartedBridgeTest, ConcurrentChangeIsDetected)
{
	make_label("/dev/sda", "gpt");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk);

	// Another program changes the table after WParted read it.
	DiskLayout other = layout(0);
	other.entries[0].gpt_name = u"changed elsewhere";
	std::string err;
	ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend->disk(0), other, false, err));
	int writes = backend->disk(0).layout_writes;

	ped_partition_set_name(ped_disk_get_partition_by_sector(disk, 3000), "mine");
	g_messages.clear();
	EXPECT_FALSE(ped_disk_commit(disk));
	EXPECT_NE(std::string::npos, last_message().find("changed by another program"));
	EXPECT_EQ(writes, backend->disk(0).layout_writes);
	EXPECT_EQ(u"changed elsewhere", layout(0).entries[0].gpt_name);
	ped_disk_destroy(disk);
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, FailedVerificationRestoresPreviousTable)
{
	make_label("/dev/sda", "gpt");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	DiskLayout before = layout(0);

	backend->disk(0).corrupt_next_write = true;
	g_messages.clear();
	EXPECT_EQ(-1, add("/dev/sda", PED_PARTITION_NORMAL, 206848, 411647));
	EXPECT_NE(std::string::npos, last_message().find("previous partition table was restored"));
	DiskLayout after = layout(0);
	ASSERT_EQ(1u, after.entries.size());
	EXPECT_EQ(before.entries[0].gpt_id, after.entries[0].gpt_id);
}


TEST_F(PartedBridgeTest, DynamicDiskIsReadOnly)
{
	make_label("/dev/sda", "msdos");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	DiskLayout l = layout(0);
	l.entries[0].mbr_type = 0x42;
	std::string err;
	ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend->disk(0), l, false, err));

	PedDevice* dev = ped_device_get("/dev/sda");
	PedDisk* disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk);
	EXPECT_NE(std::string::npos, WinPartedBridge::disk_readonly_reason(disk).find("dynamic disk"));
	PedPartition* p = ped_disk_get_partition_by_sector(disk, 3000);
	ASSERT_NE(nullptr, p);
	ASSERT_TRUE(ped_partition_set_flag(p, PED_PARTITION_BOOT, 1));
	int writes = backend->disk(0).layout_writes;
	EXPECT_FALSE(ped_disk_commit(disk));
	EXPECT_EQ(writes, backend->disk(0).layout_writes);
	ped_disk_destroy(disk);
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, NoLibpartedFileSystemSupport)
{
	make_label("/dev/sda", "gpt");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	PedDevice* dev = ped_device_get("/dev/sda");
	PedGeometry* g = ped_geometry_new(dev, 2048, 204800);
	EXPECT_EQ(nullptr, ped_file_system_open(g));
	EXPECT_FALSE(ped_file_system_resize(nullptr, g, nullptr));
	ped_geometry_destroy(g);
	EXPECT_EQ(nullptr, ped_disk_type_get("loop"));
	EXPECT_EQ(nullptr, ped_disk_type_get("mac"));
	EXPECT_STREQ("gpt", ped_disk_type_get_next(nullptr)->name);
	EXPECT_STREQ("msdos", ped_disk_type_get_next(ped_disk_type_get_next(nullptr))->name);
	EXPECT_EQ(nullptr, ped_disk_type_get_next(ped_disk_type_get("msdos")));
	ped_device_destroy(dev);
}


TEST_F(PartedBridgeTest, FlagNamesMatchLibparted)
{
	EXPECT_STREQ("boot", ped_partition_flag_get_name(PED_PARTITION_BOOT));
	EXPECT_STREQ("msftres", ped_partition_flag_get_name(PED_PARTITION_MSFT_RESERVED));
	EXPECT_STREQ("msftdata", ped_partition_flag_get_name(PED_PARTITION_MSFT_DATA));
	EXPECT_STREQ("esp", ped_partition_flag_get_name(PED_PARTITION_ESP));
	EXPECT_STREQ("no_automount", ped_partition_flag_get_name(PED_PARTITION_NO_AUTOMOUNT));
	EXPECT_EQ(PED_PARTITION_DIAG, ped_partition_flag_get_by_name("diag"));
	EXPECT_EQ(0, ped_partition_flag_get_by_name("nonsense"));
	int n = 0;
	for (PedPartitionFlag f = ped_partition_flag_next(static_cast<PedPartitionFlag>(0)); f; f = ped_partition_flag_next(f))
		n++;
	EXPECT_EQ(22, n);
}


TEST_F(PartedBridgeTest, SectorWritesStayInsideTheDevice)
{
	make_label("/dev/sda", "gpt");
	add("/dev/sda", PED_PARTITION_NORMAL, 2048, 206847);
	PedDevice* dev = ped_device_get("/dev/sda");
	std::vector<char> buf(512 * 8, 'x');
	PedGeometry* g = ped_geometry_new(dev, 2048, 204800);
	EXPECT_TRUE(ped_geometry_write(g, buf.data(), 0, 8));
	EXPECT_FALSE(ped_geometry_write(g, buf.data(), 204800 - 4, 8));  // Past the end of the geometry
	EXPECT_FALSE(ped_device_write(dev, buf.data(), dev->length - 4, 8));
	ped_geometry_destroy(g);
	ped_device_destroy(dev);
}


}  // namespace WParted
