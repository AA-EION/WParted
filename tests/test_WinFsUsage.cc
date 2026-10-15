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

#include "WinFsUsage.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>


namespace WParted
{
namespace WinFsUsage
{


static WinDisk::SectorReader reader_for(const std::vector<uint8_t>& img, uint32_t ss = 512)
{
	return [&img, ss](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
	{
		if ((lba + count) * ss > img.size())
			return false;
		out.assign(img.begin() + lba * ss, img.begin() + (lba + count) * ss);
		return true;
	};
}


// Reads sectors straight from an image file, optionally flipping one byte.
struct FileImage
{
	std::string path;
	uint64_t    size = 0;
	uint64_t    flip_offset = UINT64_MAX;

	bool open(const std::string& dir, const std::string& name)
	{
		path = dir + "/" + name;
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (! f)
			return false;
		size = static_cast<uint64_t>(f.tellg());
		return true;
	}

	WinDisk::SectorReader reader() const
	{
		return [this](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
		{
			if ((lba + count) * 512 > size)
				return false;
			std::ifstream f(path, std::ios::binary);
			f.seekg(static_cast<std::streamoff>(lba * 512));
			out.resize(size_t(count) * 512);
			f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
			if (! f)
				return false;
			if (flip_offset >= lba * 512 && flip_offset < (lba + count) * 512)
				out[flip_offset - lba * 512] ^= 0x01;
			return true;
		};
	}
};


TEST(WinFsUsage, Crc32cKnownValues)
{
	// RFC 3720 test vectors
	std::vector<uint8_t> zeros(32, 0), ones(32, 0xFF), inc(32);
	for (int i = 0; i < 32; i++)
		inc[i] = static_cast<uint8_t>(i);
	EXPECT_EQ(0x8A9136AAu, crc32c(zeros.data(), zeros.size()));
	EXPECT_EQ(0x62A8AB43u, crc32c(ones.data(), ones.size()));
	EXPECT_EQ(0x46DD794Eu, crc32c(inc.data(), inc.size()));
	// Continuation gives the same result as one call
	EXPECT_EQ(crc32c(inc.data(), 32), crc32c(inc.data() + 10, 22, crc32c(inc.data(), 10)));
}


// Size and free space must equal what dumpe2fs, btrfs and xfs_db report.
TEST(WinFsUsage, MatchesLinuxTools)
{
	const char* dir = std::getenv("WPARTED_TEST_IMAGES");
	if (! dir)
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	std::ifstream list(std::string(dir) + "/usage.txt");
	ASSERT_TRUE(list) << "usage.txt missing";
	std::string line;
	int checked = 0;
	while (std::getline(list, line))
	{
		std::stringstream ss(line);
		std::string name, fs, size, free_space;
		std::getline(ss, name, '|');
		std::getline(ss, fs, '|');
		std::getline(ss, size, '|');
		std::getline(ss, free_space, '|');
		FileImage img;
		ASSERT_TRUE(img.open(dir, name)) << name;
		Usage u;
		std::string error;
		ASSERT_TRUE(read(fs, img.reader(), 512, img.size, u, error)) << name << ": " << error;
		EXPECT_EQ(std::stoull(size), u.size) << name;
		EXPECT_EQ(std::stoull(free_space), u.free) << name;
		checked++;

		// A corrupted superblock is never trusted.
		if (fs == "btrfs" || fs == "xfs")
		{
			FileImage bad = img;
			bad.flip_offset = fs == "btrfs" ? 64 * 1024 + 0x78 : 144;
			EXPECT_FALSE(read(fs, bad.reader(), 512, bad.size, u, error)) << name;
			EXPECT_NE(std::string::npos, error.find("checksum")) << error;
		}
	}
	EXPECT_GE(checked, 5);
}


TEST(WinFsUsage, HfsPlusFromMkfs)
{
	const char* dir = std::getenv("WPARTED_TEST_IMAGES");
	if (! dir)
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set";
	FileImage img;
	if (! img.open(dir, "hfsplus.img"))
		GTEST_SKIP() << "no hfsplus.img (hfsprogs not installed)";
	Usage u;
	std::string error;
	ASSERT_TRUE(read("hfs+", img.reader(), 512, img.size, u, error)) << error;
	EXPECT_EQ(img.size, u.size);
	EXPECT_GT(u.free, u.size / 2);
	EXPECT_LT(u.free, u.size);
}


TEST(WinFsUsage, RefusesMetadataOutsideThePartition)
{
	// ext4 superblock claiming more blocks than the partition holds
	std::vector<uint8_t> img(64 * 1024, 0);
	uint8_t* sb = &img[1024];
	sb[56] = 0x53; sb[57] = 0xEF;
	sb[24] = 2;                         // 4 KiB blocks
	sb[4] = 0x00; sb[5] = 0x01;         // 256 blocks = 1 MiB > 64 KiB
	Usage u;
	std::string error;
	EXPECT_FALSE(read("ext4", reader_for(img), 512, img.size(), u, error));
	// Partition too small for the superblock at all
	std::vector<uint8_t> tiny(1024, 0);
	EXPECT_FALSE(read("ext4", reader_for(tiny), 512, tiny.size(), u, error));
	EXPECT_FALSE(read("btrfs", reader_for(tiny), 512, tiny.size(), u, error));
	EXPECT_FALSE(read("apfs", reader_for(tiny), 512, tiny.size(), u, error));
	EXPECT_FALSE(read("ntfs", reader_for(img), 512, img.size(), u, error));
}


// A minimal APFS container: superblock, checkpoint descriptor area with a
// checkpoint map and a newer superblock, and the space manager it points to.
class ApfsSynthetic : public ::testing::Test
{
protected:
	static const uint32_t BS = 4096;
	std::vector<uint8_t> img;

	static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }
	static void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = uint8_t(v >> (8 * i)); }
	uint8_t* block(uint64_t n) { return &img[n * BS]; }

	void seal(uint64_t n)
	{
		put64(block(n), apfs_fletcher64(block(n), BS));
	}

	void superblock(uint64_t n, uint64_t xid)
	{
		uint8_t* b = block(n);
		std::memset(b, 0, BS);
		put64(b + 8, 1);         // oid
		put64(b + 16, xid);
		put32(b + 24, 0x80000001);   // ephemeral | NX_SUPERBLOCK
		put32(b + 32, 0x4253584E);
		put32(b + 36, BS);
		put64(b + 40, 256);      // blocks
		put32(b + 104, 4);       // descriptor blocks
		put64(b + 112, 1);       // descriptor base
		put32(b + 136, 0);       // checkpoint index
		put32(b + 140, 2);       // checkpoint length (map + superblock)
		put64(b + 152, 0x400);   // space manager oid
		seal(n);
	}

	void SetUp() override
	{
		img.assign(256 * BS, 0);
		superblock(0, 5);
		// Checkpoint map in descriptor block 0 (disk block 1)
		uint8_t* m = block(1);
		put64(m + 8, 0x401);
		put64(m + 16, 5);
		put32(m + 24, 0x8000000C);
		put32(m + 36, 1);        // one mapping
		put32(m + 40, 5);        // type SPACEMAN
		put32(m + 48, BS);       // size
		put64(m + 64, 0x400);    // oid
		put64(m + 72, 10);       // paddr
		seal(1);
		superblock(2, 5);
		// An older, valid superblock elsewhere in the area must be ignored.
		superblock(4, 3);
		// Space manager
		uint8_t* s = block(10);
		put64(s + 8, 0x400);
		put64(s + 16, 5);
		put32(s + 24, 0x80000005);
		put32(s + 32, BS);
		put64(s + 48, 256);      // sm_dev[0].sm_block_count
		put64(s + 72, 200);      // sm_dev[0].sm_free_count
		seal(10);
	}
};


TEST_F(ApfsSynthetic, ReadsFreeSpaceFromLatestCheckpoint)
{
	Usage u;
	std::string error;
	ASSERT_TRUE(read("apfs", reader_for(img), 512, img.size(), u, error)) << error;
	EXPECT_EQ(256u * BS, u.size);
	EXPECT_EQ(200u * BS, u.free);
}


TEST_F(ApfsSynthetic, RejectsBadChecksums)
{
	Usage u;
	std::string error;
	img[10 * BS + 72] = 201;     // free count changed without resealing
	EXPECT_FALSE(read("apfs", reader_for(img), 512, img.size(), u, error));
	EXPECT_NE(std::string::npos, error.find("checksum")) << error;
}


TEST_F(ApfsSynthetic, RejectsSpaceManagerOutsideContainer)
{
	uint8_t* m = block(1);
	put64(m + 72, 1000000);      // paddr far outside
	seal(1);
	Usage u;
	std::string error;
	EXPECT_FALSE(read("apfs", reader_for(img), 512, img.size(), u, error));
}


TEST_F(ApfsSynthetic, RejectsFreeCountAboveSize)
{
	put64(block(10) + 72, 300);
	seal(10);
	Usage u;
	std::string error;
	EXPECT_FALSE(read("apfs", reader_for(img), 512, img.size(), u, error));
}


}  // namespace WinFsUsage
}  // namespace WParted
