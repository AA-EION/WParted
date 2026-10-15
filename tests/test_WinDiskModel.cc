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

#include "WinDiskModel.h"
#include "WinDevNames.h"
#include "parted/parted.h"
#include "MemDiskBackend.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <cstring>
#include <fstream>


namespace WParted
{
namespace WinDisk
{


static SectorReader image_reader(const std::vector<uint8_t>& img, uint32_t ss)
{
	return [&img, ss](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
	{
		if ((lba + count) * ss > img.size())
			return false;
		out.assign(img.begin() + lba * ss, img.begin() + (lba + count) * ss);
		return true;
	};
}


TEST(WinDevNames, DiskPathsRoundTrip)
{
	EXPECT_EQ("/dev/sda", WinDevNames::disk_path(0));
	EXPECT_EQ("/dev/sdz", WinDevNames::disk_path(25));
	EXPECT_EQ("/dev/sdaa", WinDevNames::disk_path(26));
	EXPECT_EQ("/dev/sdaz", WinDevNames::disk_path(51));
	EXPECT_EQ("/dev/sdba", WinDevNames::disk_path(52));
	EXPECT_EQ("/dev/sdzz", WinDevNames::disk_path(701));
	EXPECT_EQ("/dev/sdaaa", WinDevNames::disk_path(702));
	for (int i = 0; i < 3000; i++)
		ASSERT_EQ(i, WinDevNames::disk_index(WinDevNames::disk_path(i))) << i;
}


TEST(WinDevNames, RejectsEverythingElse)
{
	// The previous implementation silently mapped unknown names to disk 0.
	const char* bad[] = { "", "/dev/sd", "/dev/sda1", "/dev/sdA", "/dev/nvme0n1", "C:", "C:\\",
	                      "\\\\.\\PhysicalDrive", "\\\\.\\PhysicalDrive01", "\\\\.\\PhysicalDrive1x",
	                      "PhysicalDrive1", "/dev/sda/", "Copy of /dev/sda1", "/dev/sd1" };
	for (const char* b : bad)
		EXPECT_EQ(-1, WinDevNames::disk_index(b)) << b;
	EXPECT_EQ(0, WinDevNames::disk_index("\\\\.\\PhysicalDrive0"));
	EXPECT_EQ(12, WinDevNames::disk_index("\\\\.\\physicaldrive12"));
}


TEST(WinDevNames, PartitionPaths)
{
	int d = -1, p = -1;
	EXPECT_TRUE(WinDevNames::parse_partition_path("/dev/sdb12", d, p));
	EXPECT_EQ(1, d);
	EXPECT_EQ(12, p);
	EXPECT_FALSE(WinDevNames::parse_partition_path("/dev/sdb", d, p));
	EXPECT_FALSE(WinDevNames::parse_partition_path("/dev/sdb0", d, p));
	EXPECT_FALSE(WinDevNames::parse_partition_path("/dev/sdb01", d, p));
	EXPECT_EQ("/dev/sdab5", WinDevNames::partition_path("/dev/sdab", 5));
}


TEST(Guid, ParseAndFormat)
{
	Guid g;
	ASSERT_TRUE(Guid::parse("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", g));
	EXPECT_EQ(TypeGuid::BASIC_DATA, g);
	EXPECT_EQ("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", g.to_string());
	ASSERT_TRUE(Guid::parse("{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}", g));
	EXPECT_EQ(TypeGuid::ESP, g);
	EXPECT_FALSE(Guid::parse("C12A7328F81F11D2BA4B00A0C93EC93B", g));
	EXPECT_FALSE(Guid::parse("C12A7328-F81F-11D2-BA4B-00A0C93EC93", g));
	EXPECT_EQ("DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", TypeGuid::MSFT_RECOVERY.to_string());
	EXPECT_EQ("E3C9E316-0B5C-4DB8-817D-F92DF00215AE", TypeGuid::MSFT_RESERVED.to_string());

	uint8_t bytes[16];
	TypeGuid::BASIC_DATA.to_bytes_le(bytes);
	const uint8_t expected[16] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
	                               0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
	EXPECT_EQ(0, std::memcmp(bytes, expected, 16));
	EXPECT_EQ(TypeGuid::BASIC_DATA, Guid::from_bytes_le(expected));

	Guid r1 = generate_guid(), r2 = generate_guid();
	EXPECT_NE(r1, r2);
	EXPECT_EQ(0x4000, r1.data3 & 0xF000);
}


TEST(Crc32, KnownVector)
{
	const char* s = "123456789";
	EXPECT_EQ(0xCBF43926U, crc32(reinterpret_cast<const uint8_t*>(s), 9));
}


TEST(Utf16, RoundTrip)
{
	std::string s = u8"Datos \u00e9\u00f1 \U0001F600";
	EXPECT_EQ(s, utf16_to_utf8(utf8_to_utf16(s)));
	EXPECT_EQ(9u, utf8_to_utf16(u8"Datos \u00e9\u00f1 ").size());
}


// --- Model validation ---------------------------------------------------------

static DiskModel gpt_model(int64_t total = 2097152)
{
	DiskModel m;
	m.style = TableStyle::GPT;
	m.sector_size = 512;
	m.total_sectors = total;
	m.gpt_disk_id = generate_guid();
	m.first_usable = 34;
	m.last_usable = total - 34;
	return m;
}


static PartRecord gpt_part(int num, int64_t start, int64_t end, const Guid& type = TypeGuid::BASIC_DATA)
{
	PartRecord p;
	p.num = num;
	p.start = start;
	p.end = end;
	p.type_guid = type;
	p.unique_guid = generate_guid();
	return p;
}


TEST(Validate, GptOverlapAndBounds)
{
	DiskModel m = gpt_model();
	m.parts.push_back(gpt_part(1, 2048, 4095));
	m.parts.push_back(gpt_part(2, 4096, 8191));
	std::string err;
	EXPECT_TRUE(validate_model(m, err)) << err;

	m.parts[1].start = 4095;
	EXPECT_FALSE(validate_model(m, err));
	EXPECT_NE(std::string::npos, err.find("overlap"));

	m.parts[1].start = 4096;
	m.parts[1].end = m.last_usable + 1;
	EXPECT_FALSE(validate_model(m, err));

	m.parts[1].end = 8191;
	m.parts[1].unique_guid = m.parts[0].unique_guid;
	EXPECT_FALSE(validate_model(m, err));

	m.parts[1].unique_guid = generate_guid();
	m.parts[1].name = std::u16string(37, u'x');
	EXPECT_FALSE(validate_model(m, err));
}


static DiskModel mbr_model(int64_t total = 2097152)
{
	DiskModel m;
	m.style = TableStyle::MBR;
	m.sector_size = 512;
	m.total_sectors = total;
	m.mbr_signature = 0x12345678;
	m.first_usable = 1;
	m.last_usable = total - 1;
	return m;
}


static PartRecord mbr_part(int num, PartKind kind, int64_t start, int64_t end, uint8_t type)
{
	PartRecord p;
	p.num = num;
	p.kind = kind;
	p.start = start;
	p.end = end;
	p.mbr_type = type;
	return p;
}


TEST(Validate, MbrRules)
{
	std::string err;
	DiskModel m = mbr_model();
	m.parts.push_back(mbr_part(1, PartKind::PRIMARY, 2048, 206847, 0x07));
	m.parts.push_back(mbr_part(2, PartKind::EXTENDED, 206848, 2097151, 0x0F));
	m.parts.push_back(mbr_part(5, PartKind::LOGICAL, 208896, 411647, 0x83));
	m.parts.push_back(mbr_part(6, PartKind::LOGICAL, 413696, 2097151, 0x82));
	EXPECT_TRUE(validate_model(m, err)) << err;

	// No room for the EBR of the second logical.
	m.parts[3].start = 411648;
	EXPECT_FALSE(validate_model(m, err));
	m.parts[3].start = 413696;

	// Logical outside of extended
	m.parts[2].start = 100;
	EXPECT_FALSE(validate_model(m, err));
	m.parts[2].start = 208896;

	// Beyond 2 TiB
	DiskModel big = mbr_model(int64_t(5) << 32);
	big.parts.push_back(mbr_part(1, PartKind::PRIMARY, 2048, (int64_t(1) << 32) + 4096, 0x07));
	EXPECT_FALSE(validate_model(big, err));
	EXPECT_NE(std::string::npos, err.find("2 TiB"));

	// Type 0 is "unused" and would silently delete the partition.
	DiskModel z = mbr_model();
	z.parts.push_back(mbr_part(1, PartKind::PRIMARY, 2048, 4095, 0x00));
	EXPECT_FALSE(validate_model(z, err));

	// Two extended partitions
	DiskModel two = mbr_model();
	two.parts.push_back(mbr_part(1, PartKind::EXTENDED, 2048, 4095, 0x05));
	two.parts.push_back(mbr_part(2, PartKind::EXTENDED, 4096, 8191, 0x05));
	EXPECT_FALSE(validate_model(two, err));
}


TEST(Layout, MbrGroupsAndEbrPositions)
{
	DiskModel m = mbr_model();
	m.parts.push_back(mbr_part(1, PartKind::PRIMARY, 2048, 206847, 0x07));
	m.parts.push_back(mbr_part(3, PartKind::EXTENDED, 206848, 2097151, 0x0F));
	m.parts.push_back(mbr_part(5, PartKind::LOGICAL, 208896, 411647, 0x83));
	m.parts.push_back(mbr_part(6, PartKind::LOGICAL, 413696, 2097151, 0x82));
	DiskLayout l;
	std::string err;
	ASSERT_TRUE(model_to_layout(m, l, err)) << err;
	ASSERT_EQ(12u, l.entries.size());
	EXPECT_EQ(0x07, l.entries[0].mbr_type);
	EXPECT_EQ(0u, l.entries[1].length);             // Slot 2 unused, slot positions kept
	EXPECT_EQ(0x0F, l.entries[2].mbr_type);
	EXPECT_EQ(208896u * 512, l.entries[4].offset);  // First logical
	EXPECT_EQ(2048u, l.entries[4].mbr_hidden_sectors);
	EXPECT_EQ(0x05, l.entries[5].mbr_type);         // Link to second EBR
	EXPECT_EQ(411648u * 512, l.entries[5].offset);  // libparted places the EBR after the previous logical
	EXPECT_EQ(413696u * 512, l.entries[8].offset);
	EXPECT_EQ(0u, l.entries[9].length);

	DiskModel back;
	ASSERT_TRUE(layout_to_model(l, 512, m.total_sectors, nullptr, back, err)) << err;
	std::string diff;
	EXPECT_TRUE(models_equivalent(m, back, false, diff)) << diff;
	EXPECT_EQ(411648, back.find_by_num(6)->ebr_sector);
}


TEST(Layout, ChangedExtents)
{
	DiskLayout a, b;
	a.style = b.style = TableStyle::GPT;
	LayoutEntry e1, e2, e3;
	e1.offset = 1048576; e1.length = 1048576; e1.gpt_type = TypeGuid::ESP;        e1.gpt_id = generate_guid();
	e2.offset = 2097152; e2.length = 1048576; e2.gpt_type = TypeGuid::BASIC_DATA; e2.gpt_id = generate_guid();
	e3 = e2; e3.gpt_name = u"renamed"; e3.gpt_attributes = GPT_BASIC_DATA_ATTR_HIDDEN;
	a.entries = { e1, e2 };
	b.entries = { e3 };
	std::vector<Extent> c = changed_extents(a, b);
	ASSERT_EQ(1u, c.size());     // e1 deleted; e2 only renamed / attribute changed
	EXPECT_EQ(e1.offset, c[0].offset);

	b.entries[0].length += 512;  // Resized
	EXPECT_EQ(2u, changed_extents(a, b).size());
}


// --- Flags: libparted compatible semantics -------------------------------------

TEST(Flags, GptTypeFlags)
{
	DiskModel m = gpt_model();
	PartRecord p = gpt_part(1, 2048, 4095, TypeGuid::BASIC_DATA);
	derive_type_flags(TableStyle::GPT, p);
	EXPECT_TRUE(flag_get(m, p, PED_PARTITION_MSFT_DATA));
	EXPECT_FALSE(flag_get(m, p, PED_PARTITION_ESP));

	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_ESP, true, "fat32"));
	EXPECT_EQ(TypeGuid::ESP, p.type_guid);
	EXPECT_TRUE(flag_get(m, p, PED_PARTITION_BOOT));   // boot == esp on GPT
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_BOOT, false, "fat32"));
	EXPECT_EQ(TypeGuid::BASIC_DATA, p.type_guid);

	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_MSFT_RESERVED, true, nullptr));
	EXPECT_EQ(TypeGuid::MSFT_RESERVED, p.type_guid);
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_DIAG, true, "ntfs"));
	EXPECT_EQ(TypeGuid::MSFT_RECOVERY, p.type_guid);
	EXPECT_FALSE(flag_get(m, p, PED_PARTITION_MSFT_RESERVED));
}


TEST(Flags, GptAttributeFlagsUseWindowsBits)
{
	DiskModel m = gpt_model();
	PartRecord p = gpt_part(1, 2048, 4095, TypeGuid::BASIC_DATA);
	derive_type_flags(TableStyle::GPT, p);
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_HIDDEN, true, "ntfs"));
	EXPECT_EQ(GPT_BASIC_DATA_ATTR_HIDDEN, p.attributes);
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_NO_AUTOMOUNT, true, "ntfs"));
	EXPECT_EQ(GPT_BASIC_DATA_ATTR_HIDDEN | GPT_BASIC_DATA_ATTR_NO_DRIVE_LETTER, p.attributes);
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_LEGACY_BOOT, true, "ntfs"));
	EXPECT_TRUE(p.attributes & GPT_ATTR_LEGACY_BIOS_BOOTABLE);
	EXPECT_EQ(TypeGuid::BASIC_DATA, p.type_guid);  // Attributes never change the type
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_HIDDEN, false, "ntfs"));
	EXPECT_FALSE(p.attributes & GPT_BASIC_DATA_ATTR_HIDDEN);
}


TEST(Flags, GptUnknownTypePreserved)
{
	// Clearing flags on a partition whose contents are not recognised must not turn
	// a foreign partition into a Linux one (libparted would).
	DiskModel m = gpt_model();
	PartRecord p = gpt_part(1, 2048, 4095, TypeGuid::APPLE_APFS);
	derive_type_flags(TableStyle::GPT, p);
	ASSERT_TRUE(flag_set(m, p, PED_PARTITION_HIDDEN, true, nullptr));
	set_system(m, p, nullptr);
	EXPECT_EQ(TypeGuid::APPLE_APFS, p.type_guid);

	PartRecord bd = gpt_part(2, 8192, 9999, TypeGuid::BASIC_DATA);
	derive_type_flags(TableStyle::GPT, bd);
	ASSERT_TRUE(flag_set(m, bd, PED_PARTITION_MSFT_DATA, false, nullptr));  // e.g. BitLocker contents
	EXPECT_EQ(TypeGuid::BASIC_DATA, bd.type_guid);
}


TEST(Flags, GpartedFormatSequenceSwapToNtfs)
{
	// GParted's set_partition_type_using_fstype(): clear all flags (with the old
	// fs type), then set_system(new fs).  Must end up as Basic Data.
	DiskModel m = gpt_model();
	PartRecord p = gpt_part(1, 2048, 4095, TypeGuid::LINUX_SWAP);
	derive_type_flags(TableStyle::GPT, p);
	for (int f = PED_PARTITION_FIRST_FLAG; f <= PED_PARTITION_LAST_FLAG; f++)
	{
		if (flag_is_available(m, p, f))
		{
			ASSERT_TRUE(flag_set(m, p, f, false, "linux-swap"));
		}
	}
	set_system(m, p, "ntfs");
	EXPECT_EQ(TypeGuid::BASIC_DATA, p.type_guid);
	EXPECT_EQ(0u, p.attributes);
}


TEST(Flags, MbrTypes)
{
	DiskModel m = mbr_model();
	m.parts.push_back(mbr_part(1, PartKind::PRIMARY, 2048, 4095, 0x07));
	m.parts.push_back(mbr_part(2, PartKind::PRIMARY, 4096, 8191, 0x0B));
	for (PartRecord& p : m.parts)
		derive_type_flags(TableStyle::MBR, p);

	PartRecord& ntfs = m.parts[0];
	ASSERT_TRUE(flag_set(m, ntfs, PED_PARTITION_HIDDEN, true, "ntfs"));
	EXPECT_EQ(0x17, ntfs.mbr_type);
	ASSERT_TRUE(flag_set(m, ntfs, PED_PARTITION_HIDDEN, false, "ntfs"));
	EXPECT_EQ(0x07, ntfs.mbr_type);
	ASSERT_TRUE(flag_set(m, ntfs, PED_PARTITION_DIAG, true, "ntfs"));
	EXPECT_EQ(0x27, ntfs.mbr_type);  // Windows Recovery Environment
	ASSERT_TRUE(flag_set(m, ntfs, PED_PARTITION_DIAG, false, "ntfs"));
	EXPECT_EQ(0x07, ntfs.mbr_type);

	PartRecord& fat = m.parts[1];
	ASSERT_TRUE(flag_set(m, fat, PED_PARTITION_LBA, true, "fat32"));
	EXPECT_EQ(0x0C, fat.mbr_type);
	ASSERT_TRUE(flag_set(m, fat, PED_PARTITION_ESP, true, "fat32"));
	EXPECT_EQ(0xEF, fat.mbr_type);
	ASSERT_TRUE(flag_set(m, fat, PED_PARTITION_ESP, false, "fat32"));
	EXPECT_EQ(0x0C, fat.mbr_type);

	// Boot is exclusive
	ASSERT_TRUE(flag_set(m, m.parts[0], PED_PARTITION_BOOT, true, "ntfs"));
	ASSERT_TRUE(flag_set(m, m.parts[1], PED_PARTITION_BOOT, true, "fat32"));
	EXPECT_FALSE(m.parts[0].mbr_boot);
	EXPECT_TRUE(m.parts[1].mbr_boot);

	// Unknown contents keep their type (0x42 would otherwise become 0x83)
	PartRecord ldm = mbr_part(3, PartKind::PRIMARY, 9000, 9999, 0x42);
	derive_type_flags(TableStyle::MBR, ldm);
	set_system(m, ldm, nullptr);
	EXPECT_EQ(0x42, ldm.mbr_type);

	// Hidden is not available on the extended partition
	PartRecord ext = mbr_part(4, PartKind::EXTENDED, 10000, 20000, 0x05);
	EXPECT_FALSE(flag_is_available(m, ext, PED_PARTITION_HIDDEN));
	ASSERT_TRUE(flag_set(m, ext, PED_PARTITION_LBA, true, nullptr));
	EXPECT_EQ(0x0F, ext.mbr_type);
}


// --- Raw parsing against images produced by real tools ------------------------

static bool load_image(const char* name, std::vector<uint8_t>& img)
{
	const char* dir = std::getenv("WPARTED_TEST_IMAGES");
	if (! dir)
		return false;
	std::ifstream f(std::string(dir) + "/" + name, std::ios::binary);
	if (! f)
		return false;
	f.seekg(0, std::ios::end);
	img.resize(static_cast<size_t>(f.tellg()));
	f.seekg(0);
	f.read(reinterpret_cast<char*>(img.data()), static_cast<std::streamsize>(img.size()));
	return true;
}


TEST(RawTable, SfdiskMbrWithLogicals)
{
	std::vector<uint8_t> img;
	if (! load_image("mbr.img", img))
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	RawTable raw;
	std::string err;
	ASSERT_TRUE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err)) << err;
	EXPECT_EQ(TableStyle::MBR, raw.style);
	EXPECT_EQ(0x0A0B0C0Du, raw.mbr_signature);
	EXPECT_EQ(0x07, raw.slots[0].type);
	EXPECT_TRUE(raw.slots[0].boot);
	EXPECT_EQ(2048u, raw.slots[0].lba_start);
	EXPECT_EQ(0x05, raw.slots[1].type);
	ASSERT_EQ(3u, raw.logicals.size());
	EXPECT_EQ(0x83, raw.logicals[0].type);
	EXPECT_EQ(0x82, raw.logicals[1].type);
	EXPECT_EQ(0x0C, raw.logicals[2].type);
	for (const RawTable::Logical& l : raw.logicals)
		EXPECT_LT(l.ebr_lba, l.start);
}


TEST(RawTable, SgdiskGpt)
{
	std::vector<uint8_t> img;
	if (! load_image("gpt.img", img))
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	RawTable raw;
	std::string err;
	ASSERT_TRUE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err)) << err;
	EXPECT_EQ(TableStyle::GPT, raw.style);
	EXPECT_TRUE(raw.gpt_backup_ok);
	EXPECT_FALSE(raw.hybrid_mbr);
	Guid disk;
	ASSERT_TRUE(Guid::parse("11111111-2222-3333-4444-555555555555", disk));
	EXPECT_EQ(disk, raw.gpt_disk_guid);
	ASSERT_EQ(4u, raw.gpt_entries.size());
	EXPECT_EQ(TypeGuid::ESP, raw.gpt_entries[0].type);
	EXPECT_EQ(u"EFI system partition", raw.gpt_entries[0].name);
	EXPECT_EQ(TypeGuid::MSFT_RESERVED, raw.gpt_entries[1].type);
	EXPECT_EQ(TypeGuid::BASIC_DATA, raw.gpt_entries[2].type);
	EXPECT_EQ(GPT_BASIC_DATA_ATTR_HIDDEN | GPT_BASIC_DATA_ATTR_NO_DRIVE_LETTER, raw.gpt_entries[2].attributes);
	EXPECT_EQ(u"Datos \u00e9", raw.gpt_entries[2].name);
	EXPECT_EQ(5u, raw.gpt_entries[3].index);  // Slot 6 used, slots 4-5 empty
	EXPECT_EQ(TypeGuid::MSFT_RECOVERY, raw.gpt_entries[3].type);
	EXPECT_TRUE(raw.gpt_entries[3].attributes & GPT_ATTR_PLATFORM_REQUIRED);
}


TEST(RawTable, CorruptGptIsReported)
{
	std::vector<uint8_t> img;
	if (! load_image("gpt.img", img))
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	img[2 * 512 + 3] ^= 0xFF;  // Damage the first partition entry
	RawTable raw;
	std::string err;
	EXPECT_FALSE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err));
	EXPECT_NE(std::string::npos, err.find("CRC32"));
}


static void put_entry(uint8_t* e, uint8_t type, uint32_t lba, uint32_t sectors)
{
	e[4] = type;
	for (int i = 0; i < 4; i++)
	{
		e[8 + i]  = static_cast<uint8_t>(lba >> (8 * i));
		e[12 + i] = static_cast<uint8_t>(sectors >> (8 * i));
	}
}


static void sign(std::vector<uint8_t>& img, uint64_t lba)
{
	img[lba * 512 + 510] = 0x55;
	img[lba * 512 + 511] = 0xAA;
}


TEST(RawTable, EmptyExtendedWithoutEbr)
{
	// Windows writes no EBR for an extended partition without logicals.
	std::vector<uint8_t> img(4096 * 512, 0);
	put_entry(&img[446], 0x07, 2048, 1024);
	put_entry(&img[446 + 16], 0x0F, 3072, 1024);
	sign(img, 0);
	RawTable raw;
	std::string err;
	ASSERT_TRUE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err)) << err;
	EXPECT_EQ(TableStyle::MBR, raw.style);
	EXPECT_EQ(0x0F, raw.slots[1].type);
	EXPECT_TRUE(raw.logicals.empty());
}


TEST(RawTable, UnsignedEbrInChainIsReported)
{
	// First EBR is valid and links to a second one without a signature.
	std::vector<uint8_t> img(4096 * 512, 0);
	put_entry(&img[446], 0x0F, 2048, 2048);
	sign(img, 0);
	put_entry(&img[2048 * 512 + 446], 0x07, 1, 511);
	put_entry(&img[2048 * 512 + 446 + 16], 0x05, 512, 512);
	sign(img, 2048);
	RawTable raw;
	std::string err;
	EXPECT_FALSE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err));
	EXPECT_NE(std::string::npos, err.find("no boot signature")) << err;
}


TEST(RawTable, HybridMbrDetected)
{
	std::vector<uint8_t> img;
	if (! load_image("gpt.img", img))
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	// Add a second MBR entry next to the protective one.
	uint8_t* e = &img[446 + 16];
	e[4] = 0x07;
	e[8] = 0x00; e[9] = 0x10;  // LBA 4096
	e[12] = 0x00; e[13] = 0x08;
	RawTable raw;
	std::string err;
	ASSERT_TRUE(read_raw_table(image_reader(img, 512), 512, img.size() / 512, raw, err)) << err;
	EXPECT_TRUE(raw.hybrid_mbr);
	DiskModel m;
	m.style = TableStyle::GPT;
	EXPECT_FALSE(unsupported_reason(m, &raw).empty());
}


}  // namespace WinDisk
}  // namespace WParted
