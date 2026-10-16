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
#include "WinSandbox.h"
#include "WinHelpers.h"
#include "WinSwap.h"
#include "WinFsProbe.h"
#include "WinFsUsage.h"
#include "WinCopy.h"

#include "gtest/gtest.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
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

	// As GParted's resize_move_partition().
	static bool set_geometry(int64_t sector_inside, int64_t start, int64_t end)
	{
		PedDevice* dev = ped_device_get(s_path.c_str());
		PedDisk* disk = dev ? ped_disk_new(dev) : nullptr;
		bool ok = false;
		if (disk)
		{
			PedPartition* p = ped_disk_get_partition_by_sector(disk, sector_inside);
			PedGeometry* g = ped_geometry_new(dev, start, end - start + 1);
			PedConstraint* c = ped_constraint_exact(g);
			ped_geometry_destroy(g);
			ok = p && ped_disk_set_partition_geom(disk, p, c, start, end) && ped_disk_commit(disk);
			ped_constraint_destroy(c);
			ped_disk_destroy(disk);
		}
		if (dev)
			ped_device_destroy(dev);
		if (! ok)
			ADD_FAILURE() << "set geometry: " << last_message();
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


// When a sandboxed run fails, find out which hardening measure is to blame so
// the CI log says what to fix.
static void diagnose(WinSandbox::Options opt)
{
	struct { unsigned flag; const char* name; } measures[] = {
		{ WinSandbox::Options::HARDEN_LOW_INTEGRITY, "low integrity" },
		{ WinSandbox::Options::HARDEN_DENY_ADMINS,   "deny-only Administrators" },
		{ WinSandbox::Options::HARDEN_JOB_UI_LIMITS, "job UI limits" },
		{ WinSandbox::Options::HARDEN_MITIGATIONS,   "process mitigations" },
	};
	for (const auto& m : measures)
	{
		opt.hardening = WinSandbox::Options::HARDEN_ALL & ~m.flag;
		WinSandbox::Result r = WinSandbox::run(opt);
		std::fprintf(stderr, "diagnosis: without %s: started=%d exit=0x%08x failure=%s\n", m.name,
		             r.started, static_cast<unsigned>(r.exit_code), r.failure.c_str());
	}
	opt.hardening = 0;
	WinSandbox::Result r = WinSandbox::run(opt);
	std::fprintf(stderr, "diagnosis: without any hardening: started=%d exit=0x%08x failure=%s\n",
	             r.started, static_cast<unsigned>(r.exit_code), r.failure.c_str());
}


// Runs without a test disk: the sandbox must keep a helper away from disks and
// volumes even though the test itself runs as administrator.
TEST(WinSandboxConfinement, HelperCannotOpenDisksOrWriteFiles)
{
	HANDLE h = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                       OPEN_EXISTING, 0, nullptr);
	bool admin = h != INVALID_HANDLE_VALUE;
	if (admin)
		CloseHandle(h);

	WinSandbox::Options opt;
	opt.program = "wparted-sandbox-probe";
	opt.timeout_ms = 60000;
	WinSandbox::Result r = WinSandbox::run(opt);
	ASSERT_TRUE(r.started) << r.failure;
	if (r.exit_code != 0)
		diagnose(opt);
	ASSERT_EQ(0, r.exit_code) << r.error;
	std::fprintf(stderr, "probe output:\n%s", r.output.c_str());
	EXPECT_NE(std::string::npos, r.output.find("disk=denied")) << r.output;
	EXPECT_NE(std::string::npos, r.output.find("volume=denied")) << r.output;
	EXPECT_NE(std::string::npos, r.output.find("file=denied")) << r.output;
	EXPECT_NE(std::string::npos, r.output.find("broker=none")) << r.output;
	if (! admin)
		std::fprintf(stderr, "note: the test is not running as administrator\n");
}


TEST(WinSandboxConfinement, HelperNamesAreChecked)
{
	EXPECT_EQ("", WinSandbox::helper_path("..\\..\\Windows\\System32\\cmd"));
	EXPECT_EQ("", WinSandbox::helper_path("C:\\Windows\\System32\\cmd"));
	EXPECT_EQ("", WinSandbox::helper_path("cmd"));
	WinSandbox::Options opt;
	opt.program = "../cmd";
	EXPECT_FALSE(WinSandbox::run(opt).started);
}


TEST_F(WinIntegration, J_SandboxedHelperOnlyReachesItsPartition)
{
	ASSERT_EQ(1, create(PED_PARTITION_NORMAL, 1 * MiB, 101 * MiB - 1, "ext4"));
	ASSERT_EQ(2, create(PED_PARTITION_NORMAL, 101 * MiB, 201 * MiB - 1, "ext4"));
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	std::string error;

	// Mark the first sector of partition 2 to check it is never written.
	std::vector<uint8_t> mark(512, 0xC3);
	ASSERT_TRUE(backend->write_sectors(s_disk, 101 * MiB, 1, 512, mark.data(), error)) << error;

	{
		auto session = backend->open_session(s_disk, 1 * 1024 * 1024, 100 * 1024 * 1024, true,
		                                     WinDisk::SessionRange::INSIDE_PARTITION, error);
		ASSERT_TRUE(session) << error;
		WinSandbox::Options opt;
		opt.program = "wparted-sandbox-probe";
		opt.args = { "write" };
		opt.session = session.get();
		opt.timeout_ms = 60000;
		WinSandbox::Result r = WinSandbox::run(opt);
		ASSERT_TRUE(r.started) << r.failure;
		std::fprintf(stderr, "probe output:\n%s", r.output.c_str());
		EXPECT_EQ(0, r.exit_code) << r.error;
		EXPECT_NE(std::string::npos, r.output.find("disk=denied")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("broker=ok size=104857600 ss=512 ro=0")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("write0=ok")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("write_past_end=refused")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("read_past_end=refused")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("write_huge=refused")) << r.output;
		EXPECT_GE(r.broker.refused, 3u);
		EXPECT_EQ(4096u, r.broker.bytes_written);
	}

	std::vector<uint8_t> sector(512);
	ASSERT_TRUE(backend->read_sectors(s_disk, 1 * MiB, 1, 512, sector.data(), error)) << error;
	EXPECT_EQ(0, std::memcmp(sector.data(), "WPARTED-SANDBOX", 15));
	ASSERT_TRUE(backend->read_sectors(s_disk, 101 * MiB, 1, 512, sector.data(), error)) << error;
	EXPECT_EQ(mark, sector);

	// Read-only session: the helper's write is refused.
	{
		auto session = backend->open_session(s_disk, 1 * 1024 * 1024, 100 * 1024 * 1024, false,
		                                     WinDisk::SessionRange::INSIDE_PARTITION, error);
		ASSERT_TRUE(session) << error;
		std::vector<uint8_t> zero(4096, 0);
		WinSandbox::Options opt;
		opt.program = "wparted-sandbox-probe";
		opt.args = { "write" };
		opt.session = session.get();
		opt.timeout_ms = 60000;
		WinSandbox::Result r = WinSandbox::run(opt);
		ASSERT_TRUE(r.started) << r.failure;
		EXPECT_NE(std::string::npos, r.output.find("ro=1")) << r.output;
		EXPECT_NE(std::string::npos, r.output.find("write0=fail")) << r.output;
		EXPECT_EQ(0u, r.broker.bytes_written);
	}

	// Sessions are refused across partition boundaries.
	EXPECT_FALSE(backend->open_session(s_disk, 100 * 1024 * 1024, 2 * 1024 * 1024, true,
	                                   WinDisk::SessionRange::INSIDE_PARTITION, error));
}


static WinDisk::SectorReader disk_reader(int disk, uint64_t offset, uint64_t length)
{
	return [=](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
	{
		if (lba * 512 > length || uint64_t(count) * 512 > length - lba * 512)
			return false;
		out.resize(size_t(count) * 512);
		std::string error;
		return WinDisk::get_backend()->read_sectors(disk, offset / 512 + lba, count, 512, out.data(), error);
	};
}


static long long dumpe2fs_value(const std::string& output, const char* key)
{
	size_t p = output.find(std::string("\n") + key + ":");
	if (p == std::string::npos)
		return -1;
	return std::atoll(output.c_str() + p + std::strlen(key) + 2);
}


// The bundled e2fsprogs tools and WParted's swap space writer on a real disk,
// as WParted's ext2/3/4 and linux-swap support runs them.
TEST_F(WinIntegration, K_Ext4AndSwapWithBundledTools)
{
	if (! WinHelpers::tools_installed())
	{
		if (std::getenv("WPARTED_REQUIRE_HELPERS"))
			FAIL() << "the bundled e2fsprogs tools are missing";
		GTEST_SKIP() << "the bundled e2fsprogs tools are not built";
	}
	new_table("gpt");
	ASSERT_EQ(1, create(PED_PARTITION_NORMAL, 1 * MiB, 257 * MiB - 1, "ext4"));
	ASSERT_EQ(2, create(PED_PARTITION_NORMAL, 257 * MiB, 321 * MiB - 1, "linux-swap"));
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	std::string error;

	std::vector<uint8_t> mark(512, 0x5A);
	ASSERT_TRUE(backend->write_sectors(s_disk, 257 * MiB, 1, 512, mark.data(), error)) << error;

	WinHelpers::Target t;
	ASSERT_TRUE(WinHelpers::locate(s_disk, 1 * MiB * 512, t, error)) << error;
	ASSERT_EQ(256ULL * 1024 * 1024, t.length);

	auto run = [&](const WinHelpers::Command& c) -> WinSandbox::Result
	{
		WinSandbox::Result r = WinHelpers::run(c);
		std::fprintf(stderr, "$ %s\n%s%s(exit %d, broker read=%llu written=%llu refused=%llu %s)\n",
		             WinHelpers::display(c, "/dev/test1").c_str(), r.output.c_str(), r.error.c_str(),
		             r.exit_code, static_cast<unsigned long long>(r.broker.bytes_read),
		             static_cast<unsigned long long>(r.broker.bytes_written),
		             static_cast<unsigned long long>(r.broker.refused), r.failure.c_str());
		return r;
	};

	WinSandbox::Result r = run(WinHelpers::Ext::create(t, "ext4", u8"wparted é"));
	ASSERT_TRUE(r.started) << r.failure;
	ASSERT_EQ(0, r.exit_code) << r.error;
	EXPECT_NE(std::string::npos, r.output.find("Writing inode tables: done")) << r.output;

	r = run(WinHelpers::Ext::check_read_only(t));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;
	EXPECT_EQ(0u, r.broker.bytes_written);

	r = run(WinHelpers::Ext::info(t));
	ASSERT_EQ(0, r.exit_code) << r.error;
	EXPECT_NE(std::string::npos, r.output.find(u8"Filesystem volume name:   wparted é")) << r.output;
	long long block_size = dumpe2fs_value(r.output, "Block size");
	ASSERT_GT(block_size, 0);
	EXPECT_EQ(256LL * 1024 * 1024 / block_size, dumpe2fs_value(r.output, "Block count"));

	WinFsProbe::Result probe;
	ASSERT_TRUE(WinFsProbe::probe(disk_reader(s_disk, t.offset, t.length), 512, t.length / 512, probe));
	EXPECT_EQ("ext4", probe.type);
	EXPECT_EQ(u8"wparted é", probe.label);

	r = run(WinHelpers::Ext::set_label(t, "renamed"));
	EXPECT_EQ(0, r.exit_code) << r.error;
	std::string old_uuid = probe.uuid;
	r = run(WinHelpers::Ext::new_uuid(t));
	EXPECT_EQ(0, r.exit_code) << r.error;
	ASSERT_TRUE(WinFsProbe::probe(disk_reader(s_disk, t.offset, t.length), 512, t.length / 512, probe));
	EXPECT_EQ("renamed", probe.label);
	EXPECT_NE(old_uuid, probe.uuid);

	r = run(WinHelpers::Ext::check_repair(t));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;

	r = run(WinHelpers::Ext::min_size(t));
	EXPECT_EQ(0, r.exit_code) << r.error;
	EXPECT_NE(std::string::npos, r.output.find("Estimated minimum size of the filesystem:")) << r.output;

	// Shrink to 128 MiB and grow back to the partition, as GParted does.
	r = run(WinHelpers::Ext::resize(t, 131072, false));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;
	r = run(WinHelpers::Ext::info(t));
	EXPECT_EQ(128LL * 1024 * 1024 / block_size, dumpe2fs_value(r.output, "Block count"));
	r = run(WinHelpers::Ext::resize(t, 0, false));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;
	r = run(WinHelpers::Ext::info(t));
	EXPECT_EQ(256LL * 1024 * 1024 / block_size, dumpe2fs_value(r.output, "Block count"));
	r = run(WinHelpers::Ext::check_read_only(t));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;

	WinFsUsage::Usage usage;
	ASSERT_TRUE(WinFsUsage::read("ext4", disk_reader(s_disk, t.offset, t.length), 512, t.length, usage, error))
	        << error;
	EXPECT_EQ(t.length, usage.size);
	EXPECT_GT(usage.free, usage.size / 2);

	// Nothing outside the partition was written.
	std::vector<uint8_t> sector(512);
	ASSERT_TRUE(backend->read_sectors(s_disk, 257 * MiB, 1, 512, sector.data(), error)) << error;
	EXPECT_EQ(mark, sector);

	// Swap space, written by WParted itself.
	WinHelpers::Target swap;
	ASSERT_TRUE(WinHelpers::locate(s_disk, 257 * MiB * 512, swap, error)) << error;
	{
		auto session = backend->open_session(swap.disk, swap.offset, swap.length, true,
		                                     WinDisk::SessionRange::INSIDE_PARTITION, error);
		ASSERT_TRUE(session) << error;
		ASSERT_TRUE(WinSwap::create(*session, "wpswap", "", error)) << error;
		ASSERT_TRUE(WinSwap::set_label(*session, "renamed", error)) << error;
	}
	ASSERT_TRUE(WinFsProbe::probe(disk_reader(s_disk, swap.offset, swap.length), 512, swap.length / 512, probe));
	EXPECT_EQ("swap", probe.type);
	EXPECT_EQ("renamed", probe.label);
	EXPECT_EQ(36u, probe.uuid.size());

	// The ext4 file system is untouched by the swap space next to it.
	r = run(WinHelpers::Ext::check_read_only(t));
	EXPECT_EQ(0, r.exit_code) << r.output << r.error;
}


// GParted's block copy as WParted runs it (copy_blocks() with a CopyContext):
// both directions, and NTFS boot sector hidden sectors, on a real NTFS volume.
static bool block_copy(WinDisk::CopyContext& c, uint64_t src, uint64_t dst, uint64_t length, std::string& error)
{
	const uint64_t chunk = 4 * 1024 * 1024;
	std::vector<uint8_t> buf(chunk);
	const bool backwards = dst > src;   // Overlapping move to the right: from the end
	for (uint64_t done = 0; done < length; )
	{
		uint64_t n = std::min(chunk, length - done);
		uint64_t off = backwards ? length - done - n : done;
		if (! c.read(src + off, n, buf.data(), error) || ! c.write_verified(dst + off, n, buf.data(), error))
			return false;
		done += n;
	}
	return c.flush(error);
}


static bool set_hidden_sectors(int disk, uint64_t offset, uint64_t length, uint32_t value, std::string& error)
{
	auto s = WinDisk::get_backend()->open_session(disk, offset, length, true, WinDisk::SessionRange::INSIDE_PARTITION,
	                                              error);
	if (! s)
		return false;
	std::vector<uint8_t> boot(512);
	if (! s->read(0, 512, boot.data(), error))
		return false;
	if (std::memcmp(&boot[3], "NTFS    ", 8) != 0)
	{
		error = "not NTFS";
		return false;
	}
	std::memcpy(&boot[0x1C], &value, 4);
	return s->write(0, 512, boot.data(), error) && s->flush(error);
}


TEST_F(WinIntegration, L_MoveAndCopyNtfs)
{
	new_table("gpt");
	const int64_t A_S = 1 * MiB, A_E = 129 * MiB - 1;      // Original place
	const int64_t B_S = 129 * MiB, B_E = 257 * MiB - 1;    // Moved there (overlap free move right)
	const int64_t C_S = 300 * MiB, C_E = 428 * MiB - 1;    // Copied there
	ASSERT_EQ(1, create(PED_PARTITION_NORMAL, A_S, A_E, "ntfs"));
	std::string error;
	ASSERT_TRUE(WinVolumeOps::format(target(A_S, A_E), "NTFS", "MOVEME", error)) << error;
	Win32Volumes::Volume v;
	ASSERT_TRUE(volume(A_S, A_E, v));
	const uint32_t serial = v.serial;
	ASSERT_TRUE(WinVolumeOps::unmount(target(A_S, A_E), error)) << error;

	// Move right by 128 MiB: grow over both places, copy, shrink to the new place.
	ASSERT_TRUE(set_geometry(A_S, A_S, B_E));
	{
		WinDisk::CopyContext c;
		ASSERT_TRUE(c.open(s_disk, uint64_t(A_S) * 512, 128ULL << 20, s_disk, uint64_t(B_S) * 512, 128ULL << 20, error))
		        << error;
		EXPECT_TRUE(c.shared());
		// Never outside the destination, even inside the grown partition.
		std::vector<uint8_t> junk(512, 0xEE);
		EXPECT_FALSE(c.write_verified(uint64_t(A_S) * 512, 512, junk.data(), error));
		ASSERT_TRUE(block_copy(c, uint64_t(A_S) * 512, uint64_t(B_S) * 512, 128ULL << 20, error)) << error;
	}
	ASSERT_TRUE(set_geometry(B_S, B_S, B_E));
	ASSERT_TRUE(set_hidden_sectors(s_disk, uint64_t(B_S) * 512, 128ULL << 20, uint32_t(B_S), error)) << error;

	ASSERT_TRUE(volume(B_S, B_E, v, 60000)) << "Windows did not mount the moved volume";
	EXPECT_EQ(L"NTFS", v.fs_name);
	EXPECT_EQ(L"MOVEME", v.label);
	EXPECT_EQ(serial, v.serial);
	ASSERT_TRUE(WinVolumeOps::unmount(target(B_S, B_E), error)) << error;
	std::string cmd, output;
	int exit_code = -1;
	EXPECT_TRUE(WinVolumeOps::check_repair(target(B_S, B_E), [&output](const std::string& o) { output = o; },
	                                       cmd, exit_code, error)) << error << "\n" << output;
	EXPECT_EQ(0, exit_code) << output;

	// Copy into a new partition: the source is locked and only read.
	ASSERT_EQ(2, create(PED_PARTITION_NORMAL, C_S, C_E, "ntfs"));
	{
		WinDisk::CopyContext c;
		ASSERT_TRUE(c.open(s_disk, uint64_t(B_S) * 512, 128ULL << 20, s_disk, uint64_t(C_S) * 512, 128ULL << 20, error))
		        << error;
		EXPECT_FALSE(c.shared());
		ASSERT_TRUE(block_copy(c, uint64_t(B_S) * 512, uint64_t(C_S) * 512, 128ULL << 20, error)) << error;
	}
	ASSERT_TRUE(set_hidden_sectors(s_disk, uint64_t(C_S) * 512, 128ULL << 20, uint32_t(C_S), error)) << error;
	ASSERT_TRUE(volume(C_S, C_E, v, 60000)) << "Windows did not mount the copy";
	EXPECT_EQ(L"NTFS", v.fs_name);
	EXPECT_EQ(L"MOVEME", v.label);
	ASSERT_TRUE(volume(B_S, B_E, v, 60000));
	EXPECT_EQ(L"MOVEME", v.label);

	// A copy over a partition boundary is refused.
	WinDisk::CopyContext bad;
	EXPECT_FALSE(bad.open(s_disk, uint64_t(B_S) * 512, 128ULL << 20, s_disk, uint64_t(C_S - MiB) * 512, 128ULL << 20,
	                      error));
}


}  // namespace WParted
