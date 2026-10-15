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

/* Windows integration tests.  They create partition tables, partitions and file
 * systems through the real Windows partition manager and Storage Management API,
 * the same code paths the WParted GUI uses.
 *
 * DESTRUCTIVE.  They only run on the disk number given in WPARTED_TEST_VHD_DISK
 * and refuse to touch it unless it is a small file backed virtual disk (a VHD
 * or VHDX attached for the test).  See .github/workflows/build-and-release.yml.
 */

#include "parted/parted.h"
#include "WinPartedBridge.h"
#include "WinDiskBackend.h"
#include "WinDiskModel.h"
#include "WinDevNames.h"
#include "WinVolumeOps.h"
#include "Win32Volumes.h"

#include "gtest/gtest.h"

#include <windows.h>

#include <cstdlib>
#include <string>
#include <vector>


namespace WParted
{

using WinDisk::DiskLayout;
using WinDisk::TableStyle;
namespace TypeGuid = WinDisk::TypeGuid;

static const int64_t MiB = 2048;  // Sectors of 512 bytes; the test VHDX uses 512 byte sectors

static std::vector<std::string> g_messages;
static PedExceptionOption record_exception(PedException* ex)
{
	g_messages.push_back(ex->message);
	std::fprintf(stderr, "  [libparted layer] %s\n", ex->message);
	return PED_EXCEPTION_CANCEL;
}


class WinIntegration : public ::testing::Test
{
protected:
	static int         s_disk;
	static std::string s_path;
	static bool        s_ok;

	static void SetUpTestSuite()
	{
		ped_exception_set_handler(record_exception);
		const char* env = std::getenv("WPARTED_TEST_VHD_DISK");
		if (! env)
			return;
		s_disk = std::atoi(env);
		s_path = WinDevNames::disk_path(s_disk);

		WinDisk::DiskInfo info;
		std::string error;
		if (! WinDisk::get_backend()->query_disk(s_disk, info, error))
		{
			std::fprintf(stderr, "Cannot query disk %d: %s\n", s_disk, error.c_str());
			return;
		}
		std::fprintf(stderr, "Test disk %d: %s %s bus=%s size=%llu sector=%u serial=%s\n", s_disk,
		             info.vendor.c_str(), info.model.c_str(), info.bus.c_str(),
		             static_cast<unsigned long long>(info.size_bytes), info.logical_sector_size,
		             info.serial.c_str());
		// SAFETY: only ever a small virtual disk.
		if ((info.bus != "File Backed Virtual" && info.bus != "Virtual") ||
		    info.size_bytes > 8ULL * 1024 * 1024 * 1024 || info.system_disk || info.logical_sector_size != 512)
		{
			std::fprintf(stderr, "REFUSING: disk %d is not a small file backed virtual disk\n", s_disk);
			return;
		}
		WinPartedBridge::refresh_device_registry();
		s_ok = true;
	}

	void SetUp() override
	{
		if (! s_ok)
			GTEST_SKIP() << "WPARTED_TEST_VHD_DISK not set to a small virtual disk";
		g_messages.clear();
	}

	static std::string last_message()
	{
		return g_messages.empty() ? std::string("(no message)") : g_messages.back();
	}

	static DiskLayout layout()
	{
		DiskLayout l;
		std::string error;
		EXPECT_TRUE(WinDisk::get_backend()->read_layout(s_disk, l, error)) << error;
		return l;
	}

	static void new_table(const char* type)
	{
		PedDevice* dev = ped_device_get(s_path.c_str());
		ASSERT_NE(nullptr, dev) << last_message();
		PedDisk* disk = ped_disk_new_fresh(dev, ped_disk_type_get(type));
		ASSERT_NE(nullptr, disk) << last_message();
		ASSERT_TRUE(ped_disk_commit(disk)) << last_message();
		ped_disk_destroy(disk);
		ped_device_destroy(dev);
	}

	// Same sequence as GParted's create_partition() + set_partition_type().
	static int create(PedPartitionType type, int64_t start, int64_t end, const char* fs,
	                  PedPartitionFlag flag = static_cast<PedPartitionFlag>(0), const char* name = nullptr)
	{
		PedDevice* dev = ped_device_get(s_path.c_str());
		EXPECT_NE(nullptr, dev) << last_message();
		PedDisk* disk = dev ? ped_disk_new(dev) : nullptr;
		EXPECT_NE(nullptr, disk) << last_message();
		if (! disk)
			return -1;
		EXPECT_EQ("", WinPartedBridge::disk_readonly_reason(disk));
		PedPartition* part = ped_partition_new(disk, type,
		        type == PED_PARTITION_EXTENDED ? nullptr : ped_file_system_type_get("ext2"), start, end);
		PedGeometry* geom = ped_geometry_new(dev, start, end - start + 1);
		PedConstraint* c = ped_constraint_exact(geom);
		ped_geometry_destroy(geom);
		int num = -1;
		bool added = part && ped_disk_add_partition(disk, part, c);
		if (! added && part)
			ped_partition_destroy(part);
		if (added && ped_disk_commit(disk))
		{
			num = part->num;
			if (name)
				EXPECT_TRUE(ped_partition_set_name(part, name));
			if (type != PED_PARTITION_EXTENDED)
			{
				for (int f = PED_PARTITION_FIRST_FLAG; f <= PED_PARTITION_LAST_FLAG; f++)
					if (ped_partition_is_flag_available(part, static_cast<PedPartitionFlag>(f)))
						ped_partition_set_flag(part, static_cast<PedPartitionFlag>(f), 0);
				if (flag)
					EXPECT_TRUE(ped_partition_set_flag(part, flag, 1));
				else
					EXPECT_TRUE(ped_partition_set_system(part, ped_file_system_type_get(fs)));
			}
			if (! ped_disk_commit(disk))
			{
				ADD_FAILURE() << "set type commit: " << last_message();
				num = -1;
			}
		}
		else
		{
			ADD_FAILURE() << "add partition: " << last_message();
		}
		ped_constraint_destroy(c);
		ped_disk_destroy(disk);
		ped_device_destroy(dev);
		return num;
	}

	static bool remove(int64_t sector)
	{
		PedDevice* dev = ped_device_get(s_path.c_str());
		PedDisk* disk = dev ? ped_disk_new(dev) : nullptr;
		bool ok = false;
		if (disk)
		{
			PedPartition* p = ped_disk_get_partition_by_sector(disk, sector);
			ok = p && ped_disk_delete_partition(disk, p) && ped_disk_commit(disk);
			ped_disk_destroy(disk);
		}
		if (dev)
			ped_device_destroy(dev);
		return ok;
	}

	static WinVolumeOps::Target target(int64_t start, int64_t end)
	{
		WinVolumeOps::Target t;
		t.disk = s_disk;
		t.offset = uint64_t(start) * 512;
		t.length = uint64_t(end - start + 1) * 512;
		return t;
	}

	static bool volume(int64_t start, int64_t end, Win32Volumes::Volume& v, unsigned wait_ms = 30000)
	{
		return Win32Volumes::wait_for_partition_volume(s_disk, uint64_t(start) * 512,
		                                               uint64_t(end - start + 1) * 512, wait_ms, v);
	}
};

int         WinIntegration::s_disk = -1;
std::string WinIntegration::s_path;
bool        WinIntegration::s_ok = false;


// Partitions of the GPT test layout (sectors)
static const int64_t ESP_S  = 1 * MiB,   ESP_E  = 101 * MiB - 1;
static const int64_t MSR_S  = 101 * MiB, MSR_E  = 117 * MiB - 1;
static const int64_t DATA_S = 117 * MiB, DATA_E = 517 * MiB - 1;
static const int64_t DAT2_S = 517 * MiB, DAT2_E = 717 * MiB - 1;
static const int64_t LNX_S  = 717 * MiB, LNX_E  = 817 * MiB - 1;


TEST_F(WinIntegration, A_GptTableAndPartitions)
{
	new_table("gpt");
	DiskLayout l = layout();
	ASSERT_EQ(TableStyle::GPT, l.style);
	ASSERT_EQ(0u, l.entries.size()) << "Windows created partitions in a new GPT";

	EXPECT_EQ(1, create(PED_PARTITION_NORMAL, ESP_S, ESP_E, "fat32", PED_PARTITION_ESP, "EFI system partition"));
	EXPECT_EQ(2, create(PED_PARTITION_NORMAL, MSR_S, MSR_E, nullptr, PED_PARTITION_MSFT_RESERVED));
	EXPECT_EQ(3, create(PED_PARTITION_NORMAL, DATA_S, DATA_E, "ntfs", static_cast<PedPartitionFlag>(0), u8"Datos é"));
	EXPECT_EQ(4, create(PED_PARTITION_NORMAL, DAT2_S, DAT2_E, "ntfs"));
	EXPECT_EQ(5, create(PED_PARTITION_NORMAL, LNX_S, LNX_E, "ext4"));

	l = layout();
	ASSERT_EQ(5u, l.entries.size());
	EXPECT_EQ(TypeGuid::ESP, l.entries[0].gpt_type);
	EXPECT_EQ(u"EFI system partition", l.entries[0].gpt_name);
	EXPECT_EQ(TypeGuid::MSFT_RESERVED, l.entries[1].gpt_type);
	EXPECT_EQ(TypeGuid::BASIC_DATA, l.entries[2].gpt_type);
	EXPECT_EQ(u"Datos é", l.entries[2].gpt_name);
	EXPECT_EQ(TypeGuid::BASIC_DATA, l.entries[3].gpt_type);
	EXPECT_EQ(TypeGuid::LINUX_DATA, l.entries[4].gpt_type);
}


TEST_F(WinIntegration, B_FormatLabelCheckNtfs)
{
	std::string error;
	ASSERT_TRUE(WinVolumeOps::format(target(DATA_S, DATA_E), "NTFS", "WPTEST", error)) << error;
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(DATA_S, DATA_E, v));
	EXPECT_EQ(L"NTFS", v.fs_name);
	EXPECT_EQ(L"WPTEST", v.label);

	ASSERT_TRUE(WinVolumeOps::set_label(target(DATA_S, DATA_E), "Renamed", error)) << error;
	ASSERT_TRUE(volume(DATA_S, DATA_E, v));
	EXPECT_EQ(L"Renamed", v.label);

	int64_t size = 0, free = 0;
	ASSERT_TRUE(WinVolumeOps::usage(target(DATA_S, DATA_E), size, free, error)) << error;
	EXPECT_GT(size, 300LL * 1024 * 1024);
	EXPECT_GT(free, 0);

	// chkdsk refuses (and WParted refuses) volumes with drive letters; remove any
	// letter Windows assigned automatically.
	ASSERT_TRUE(WinVolumeOps::unmount(target(DATA_S, DATA_E), error)) << error;
	std::string cmd;
	int exit_code = -1;
	std::string output;
	EXPECT_TRUE(WinVolumeOps::check_repair(target(DATA_S, DATA_E),
	                                       [&output](const std::string& o) { output = o; },
	                                       cmd, exit_code, error)) << error << "\n" << output;
	EXPECT_LE(exit_code, 2) << output;
	std::fprintf(stderr, "%s\n%s\n", cmd.c_str(), output.c_str());
}


TEST_F(WinIntegration, C_FormatFat32AndExfat)
{
	std::string error;
	ASSERT_TRUE(WinVolumeOps::format(target(ESP_S, ESP_E), "FAT32", "ESP", error)) << error;
	ASSERT_TRUE(WinVolumeOps::format(target(DAT2_S, DAT2_E), "exFAT", "XF", error)) << error;
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(DAT2_S, DAT2_E, v));
	EXPECT_EQ(L"exFAT", v.fs_name);
}


TEST_F(WinIntegration, D_ResizeNtfsWithWindows)
{
	std::string error;
	uint64_t min_bytes = 0, max_bytes = 0;
	ASSERT_TRUE(WinVolumeOps::supported_size(target(DATA_S, DATA_E), min_bytes, max_bytes, error)) << error;
	EXPECT_LT(min_bytes, uint64_t(DATA_E - DATA_S + 1) * 512);
	EXPECT_GE(max_bytes, uint64_t(DATA_E - DATA_S + 1) * 512);

	const int64_t new_end = DATA_E - 64 * MiB;
	ASSERT_TRUE(WinVolumeOps::resize(target(DATA_S, DATA_E), uint64_t(new_end - DATA_S + 1) * 512, error)) << error;
	DiskLayout l = layout();
	ASSERT_EQ(5u, l.entries.size());
	EXPECT_EQ(uint64_t(new_end - DATA_S + 1) * 512, l.entries[2].length);

	// Out of range requests are refused without changes.
	EXPECT_FALSE(WinVolumeOps::resize(target(DATA_S, new_end), uint64_t(DAT2_E - DATA_S + 1) * 512, error));
	EXPECT_EQ(uint64_t(new_end - DATA_S + 1) * 512, layout().entries[2].length);

	ASSERT_TRUE(WinVolumeOps::resize(target(DATA_S, new_end), uint64_t(DATA_E - DATA_S + 1) * 512, error)) << error;
	EXPECT_EQ(uint64_t(DATA_E - DATA_S + 1) * 512, layout().entries[2].length);

	bool already = false;
	EXPECT_TRUE(WinVolumeOps::grow_filesystem_to_partition(target(DATA_S, DATA_E), already, error)) << error;
	EXPECT_TRUE(already);
}


TEST_F(WinIntegration, E_DriveLetters)
{
	std::string error, mp;
	ASSERT_TRUE(WinVolumeOps::mount(target(DATA_S, DATA_E), mp, error)) << error;
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(DATA_S, DATA_E, v));
	ASSERT_EQ(1u, v.mount_points.size());
	EXPECT_TRUE(v.busy());

	// A raw write into a volume with a drive letter must be refused.
	PedDevice* dev = ped_device_get(s_path.c_str());
	ASSERT_NE(nullptr, dev);
	std::vector<char> zeros(512 * 8, 0);
	EXPECT_FALSE(ped_device_write(dev, zeros.data(), DATA_S + 100, 8));

	ASSERT_TRUE(WinVolumeOps::unmount(target(DATA_S, DATA_E), error)) << error;
	ASSERT_TRUE(volume(DATA_S, DATA_E, v));
	EXPECT_TRUE(v.mount_points.empty());
	ped_device_destroy(dev);
}


TEST_F(WinIntegration, F_RawWritesLockAndStayInsidePartitions)
{
	PedDevice* dev = ped_device_get(s_path.c_str());
	ASSERT_NE(nullptr, dev);
	std::vector<char> zeros(512 * 8, 0);
	// Unused space between partitions and the GPT itself: refused.
	EXPECT_FALSE(ped_device_write(dev, zeros.data(), 100, 8));
	EXPECT_FALSE(ped_device_write(dev, zeros.data(), LNX_E + 10, 8));
	// Partition straddling: refused.
	EXPECT_FALSE(ped_device_write(dev, zeros.data(), DAT2_E - 3, 8));

	// Erasing signatures of the exFAT partition (as GParted does before a format):
	// its volume has no drive letter, so it is locked, dismounted and written.
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(DAT2_S, DAT2_E, v));
	std::string error;
	if (! v.mount_points.empty())
		ASSERT_TRUE(WinVolumeOps::unmount(target(DAT2_S, DAT2_E), error)) << error;
	EXPECT_TRUE(ped_device_write(dev, zeros.data(), DAT2_S, 8)) << last_message();
	ped_device_destroy(dev);
}


TEST_F(WinIntegration, G_DeletePreservesOtherPartitions)
{
	DiskLayout before = layout();
	ASSERT_EQ(5u, before.entries.size());
	ASSERT_TRUE(remove(LNX_S + 10)) << last_message();
	ASSERT_TRUE(remove(DAT2_S + 10)) << last_message();
	DiskLayout after = layout();
	ASSERT_EQ(3u, after.entries.size());
	for (int i = 0; i < 3; i++)
	{
		EXPECT_EQ(before.entries[i].offset, after.entries[i].offset);
		EXPECT_EQ(before.entries[i].length, after.entries[i].length);
		EXPECT_EQ(before.entries[i].gpt_type, after.entries[i].gpt_type);
		EXPECT_EQ(before.entries[i].gpt_id, after.entries[i].gpt_id);
		EXPECT_EQ(before.entries[i].gpt_name, after.entries[i].gpt_name);
		EXPECT_EQ(before.entries[i].gpt_attributes, after.entries[i].gpt_attributes);
	}
	EXPECT_EQ(before.gpt_disk_id, after.gpt_disk_id);

	// The NTFS data survived.
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(DATA_S, DATA_E, v));
	EXPECT_EQ(L"NTFS", v.fs_name);
	EXPECT_EQ(L"Renamed", v.label);
}


TEST_F(WinIntegration, H_MbrWithLogicalPartitions)
{
	new_table("msdos");
	ASSERT_EQ(TableStyle::MBR, layout().style);
	EXPECT_EQ(1, create(PED_PARTITION_NORMAL, 1 * MiB, 201 * MiB - 1, "ntfs"));
	EXPECT_EQ(2, create(PED_PARTITION_EXTENDED, 201 * MiB, 900 * MiB - 1, nullptr));
	EXPECT_EQ(5, create(PED_PARTITION_LOGICAL, 202 * MiB, 302 * MiB - 1, "ext4"));
	EXPECT_EQ(6, create(PED_PARTITION_LOGICAL, 303 * MiB, 403 * MiB - 1, "ntfs"));
	EXPECT_EQ(7, create(PED_PARTITION_LOGICAL, 404 * MiB, 504 * MiB - 1, "fat32"));

	// The libparted layer cross-checks the Windows layout against the raw EBR chain
	// on every read: a disk it can still modify proves they agree.
	PedDevice* dev = ped_device_get(s_path.c_str());
	PedDisk* disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk) << last_message();
	EXPECT_EQ("", WinPartedBridge::disk_readonly_reason(disk));
	int count = 0;
	for (PedPartition* p = ped_disk_next_partition(disk, nullptr); p; p = ped_disk_next_partition(disk, p))
		count++;
	EXPECT_EQ(5, count);
	ped_disk_destroy(disk);
	ped_device_destroy(dev);

	std::string error;
	ASSERT_TRUE(WinVolumeOps::format(target(303 * MiB, 403 * MiB - 1), "NTFS", "LOGICAL", error)) << error;

	ASSERT_TRUE(remove(250 * MiB)) << last_message();  // First logical
	dev = ped_device_get(s_path.c_str());
	disk = ped_disk_new(dev);
	ASSERT_NE(nullptr, disk) << last_message();
	EXPECT_EQ("", WinPartedBridge::disk_readonly_reason(disk));
	PedPartition* p = ped_disk_get_partition_by_sector(disk, 350 * MiB);
	ASSERT_NE(nullptr, p);
	EXPECT_EQ(5, p->num);  // Renumbered like libparted
	ped_disk_destroy(disk);
	ped_device_destroy(dev);

	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(303 * MiB, 403 * MiB - 1, v));
	EXPECT_EQ(L"NTFS", v.fs_name);
	EXPECT_EQ(L"LOGICAL", v.label);
}


TEST_F(WinIntegration, I_BackToGpt)
{
	// Re-creating the table of a disk whose volumes are not in use.
	std::string error;
	Win32Volumes::Volume v;
	for (const Win32Volumes::Volume& vol : Win32Volumes::enumerate())
		for (const Win32Volumes::Extent& e : vol.extents)
			if (static_cast<int>(e.disk) == s_disk)
				for (const std::wstring& mp : vol.mount_points)
					DeleteVolumeMountPointW(mp.c_str());
	new_table("gpt");
	EXPECT_EQ(TableStyle::GPT, layout().style);
	EXPECT_EQ(0u, layout().entries.size());
}


}  // namespace WParted
