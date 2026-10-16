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

#include "WinCopy.h"
#include "WinDiskModel.h"
#include "MemDiskBackend.h"

#include "gtest/gtest.h"

#include <vector>


namespace WParted
{
namespace WinDisk
{

static const uint64_t MiB = 1024 * 1024;


// Corrupts one byte of every write when asked to.
class FlakyBackend : public TestSupport::MemDiskBackend
{
public:
	bool corrupt = false;
	bool write_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                   const void* buffer, std::string& error) override
	{
		if (! corrupt)
			return MemDiskBackend::write_sectors(index, lba, count, sector_size, buffer, error);
		std::vector<uint8_t> copy(static_cast<const uint8_t*>(buffer),
		                          static_cast<const uint8_t*>(buffer) + count * sector_size);
		copy[0] ^= 0xFF;
		return MemDiskBackend::write_sectors(index, lba, count, sector_size, copy.data(), error);
	}
};


class CopyTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		backend.add_disk(0, 128 * MiB / 512);
		DiskLayout l;
		l.style = TableStyle::GPT;
		Guid::parse("11111111-2222-3333-4444-555555555555", l.gpt_disk_id);
		l.gpt_first_usable_offset = 34 * 512;
		l.gpt_usable_length = 128 * MiB - 2 * 34 * 512;
		l.gpt_max_entries = 128;
		// Partition 1: 1 - 41 MiB (a partition grown over the old and new place
		// of a move).  Partition 2: 50 - 70 MiB.
		l.entries.push_back(entry(1 * MiB, 40 * MiB, "00000000-0000-0000-0000-000000000001"));
		l.entries.push_back(entry(50 * MiB, 20 * MiB, "00000000-0000-0000-0000-000000000002"));
		std::string err;
		ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend.disk(0), l, true, err)) << err;
		previous = get_backend();
		set_backend(&backend);
	}
	void TearDown() override
	{
		set_backend(previous);
	}
	static LayoutEntry entry(uint64_t offset, uint64_t length, const char* id)
	{
		LayoutEntry e;
		e.offset = offset;
		e.length = length;
		e.gpt_type = TypeGuid::LINUX_DATA;
		Guid::parse(id, e.gpt_id);
		return e;
	}

	FlakyBackend backend;
	DiskBackend* previous = nullptr;
};


TEST_F(CopyTest, MoveInsideOnePartitionSharesOneSession)
{
	CopyContext c;
	std::string err;
	ASSERT_TRUE(c.open(0, 1 * MiB, 20 * MiB, 0, 11 * MiB, 20 * MiB, err)) << err;
	EXPECT_TRUE(c.shared());
	CopyContext::set_current(&c);
	EXPECT_EQ(&c, CopyContext::current());

	std::vector<uint8_t> buf(MiB, 0x42);
	EXPECT_TRUE(c.read(1 * MiB, MiB, buf.data(), err)) << err;
	EXPECT_TRUE(c.write_verified(30 * MiB, MiB, buf.data(), err)) << err;
	// Inside the session but outside the destination: refused.
	EXPECT_FALSE(c.write_verified(1 * MiB, MiB, buf.data(), err));
	EXPECT_FALSE(c.write_verified(31 * MiB, MiB, buf.data(), err));
	EXPECT_FALSE(c.read(0, MiB, buf.data(), err));

	c.close();
	EXPECT_EQ(nullptr, CopyContext::current());
	EXPECT_FALSE(c.is_open());
}


TEST_F(CopyTest, CopyToAnotherPartition)
{
	CopyContext c;
	std::string err;
	ASSERT_TRUE(c.open(0, 1 * MiB, 10 * MiB, 0, 50 * MiB, 10 * MiB, err)) << err;
	EXPECT_FALSE(c.shared());
	std::vector<uint8_t> buf(MiB, 0x17);
	EXPECT_TRUE(c.read(10 * MiB, MiB, buf.data(), err)) << err;
	EXPECT_TRUE(c.write_verified(59 * MiB, MiB, buf.data(), err)) << err;
	EXPECT_FALSE(c.write_verified(60 * MiB, MiB, buf.data(), err));
	EXPECT_FALSE(c.write_verified(5 * MiB, MiB, buf.data(), err));
	EXPECT_FALSE(c.read(11 * MiB, MiB, buf.data(), err));
}


TEST_F(CopyTest, RefusesRangesAcrossPartitions)
{
	CopyContext c;
	std::string err;
	// Destination into free space and over the end of partition 2.
	EXPECT_FALSE(c.open(0, 1 * MiB, 10 * MiB, 0, 42 * MiB, 10 * MiB, err));
	EXPECT_FALSE(c.open(0, 1 * MiB, 10 * MiB, 0, 65 * MiB, 10 * MiB, err));
	// A "move" from one partition into another is not one session either.
	EXPECT_FALSE(c.open(0, 35 * MiB, 10 * MiB, 0, 45 * MiB, 10 * MiB, err));
	EXPECT_FALSE(c.is_open());
}


TEST_F(CopyTest, ReadBackDetectsBadWrites)
{
	CopyContext c;
	std::string err;
	ASSERT_TRUE(c.open(0, 1 * MiB, 10 * MiB, 0, 50 * MiB, 10 * MiB, err)) << err;
	std::vector<uint8_t> buf(64 * 1024, 0x99);
	backend.corrupt = true;
	EXPECT_FALSE(c.write_verified(50 * MiB, buf.size(), buf.data(), err));
	EXPECT_NE(std::string::npos, err.find("differs")) << err;
}


}  // namespace WinDisk
}  // namespace WParted
