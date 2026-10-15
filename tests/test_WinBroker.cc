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

/* Disk sessions, the session range policy, the broker server and the C broker
 * client used by helper programs.
 */

#include "WinBroker.h"
#include "WinDiskModel.h"
#include "WinSandbox.h"
#include "MemDiskBackend.h"
#include "wpbroker.h"

#include "gtest/gtest.h"

#include <cstring>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace WParted
{
namespace Broker
{

using WinDisk::DiskLayout;
using WinDisk::LayoutEntry;
using WinDisk::SessionRange;
using WinDisk::TableStyle;

static const uint64_t MiB = 1024 * 1024;


static DiskLayout gpt_layout(uint64_t disk_size)
{
	DiskLayout l;
	l.style = TableStyle::GPT;
	WinDisk::Guid::parse("11111111-2222-3333-4444-555555555555", l.gpt_disk_id);
	l.gpt_first_usable_offset = 34 * 512;
	l.gpt_usable_length = disk_size - 2 * 34 * 512;
	l.gpt_max_entries = 128;
	return l;
}


static LayoutEntry gpt_entry(uint64_t offset, uint64_t length, const char* id)
{
	LayoutEntry e;
	e.offset = offset;
	e.length = length;
	e.gpt_type = WinDisk::TypeGuid::LINUX_DATA;
	WinDisk::Guid::parse(id, e.gpt_id);
	return e;
}


TEST(SessionRange, InsidePartition)
{
	const uint64_t size = 64 * MiB;
	DiskLayout l = gpt_layout(size);
	l.entries.push_back(gpt_entry(1 * MiB, 8 * MiB, "00000000-0000-0000-0000-000000000001"));
	l.entries.push_back(gpt_entry(9 * MiB, 8 * MiB, "00000000-0000-0000-0000-000000000002"));
	std::string why;

	EXPECT_TRUE(session_range_allowed(l, 512, size, 1 * MiB, 8 * MiB, SessionRange::INSIDE_PARTITION, why));
	EXPECT_TRUE(session_range_allowed(l, 512, size, 2 * MiB, 512, SessionRange::INSIDE_PARTITION, why));
	// Across the boundary of two partitions
	EXPECT_FALSE(session_range_allowed(l, 512, size, 8 * MiB, 2 * MiB, SessionRange::INSIDE_PARTITION, why));
	// Free space, the GPT header and the disk end
	EXPECT_FALSE(session_range_allowed(l, 512, size, 20 * MiB, MiB, SessionRange::INSIDE_PARTITION, why));
	EXPECT_FALSE(session_range_allowed(l, 512, size, 0, 512, SessionRange::INSIDE_PARTITION, why));
	EXPECT_FALSE(session_range_allowed(l, 512, size, size - 512, 512, SessionRange::INSIDE_PARTITION, why));
	// Unaligned or empty ranges
	EXPECT_FALSE(session_range_allowed(l, 512, size, 1 * MiB + 1, 512, SessionRange::INSIDE_PARTITION, why));
	EXPECT_FALSE(session_range_allowed(l, 512, size, 1 * MiB, 0, SessionRange::INSIDE_PARTITION, why));
	// Overflowing length
	EXPECT_FALSE(session_range_allowed(l, 512, size, 1 * MiB, UINT64_MAX - 511, SessionRange::INSIDE_PARTITION, why));
}


TEST(SessionRange, MoveIntoFreeSpace)
{
	const uint64_t size = 64 * MiB;
	DiskLayout l = gpt_layout(size);
	l.entries.push_back(gpt_entry(4 * MiB, 8 * MiB, "00000000-0000-0000-0000-000000000001"));
	l.entries.push_back(gpt_entry(20 * MiB, 8 * MiB, "00000000-0000-0000-0000-000000000002"));
	std::string why;

	// Partition 1 plus free space on both sides
	EXPECT_TRUE(session_range_allowed(l, 512, size, 1 * MiB, 19 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why)) << why;
	// Reaching into partition 2
	EXPECT_FALSE(session_range_allowed(l, 512, size, 4 * MiB, 17 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	// The first MiB holds partition tables and boot loaders
	EXPECT_FALSE(session_range_allowed(l, 512, size, 512 * 1024, 12 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	// Free space only: there must be exactly one partition being moved
	EXPECT_FALSE(session_range_allowed(l, 512, size, 30 * MiB, 4 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	// Backup GPT at the end
	EXPECT_FALSE(session_range_allowed(l, 512, size, 20 * MiB, size - 20 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	// No partition table at all
	DiskLayout raw;
	EXPECT_FALSE(session_range_allowed(raw, 512, size, 1 * MiB, MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	EXPECT_TRUE(session_range_allowed(raw, 512, size, 0, MiB, SessionRange::INSIDE_PARTITION, why));
}


TEST(SessionRange, MbrExtendedPartitionsAreOffLimitsForMoves)
{
	const uint64_t size = 64 * MiB;
	DiskLayout l;
	l.style = TableStyle::MBR;
	LayoutEntry p1, ext, log;
	p1.offset = 1 * MiB;  p1.length = 8 * MiB;  p1.mbr_type = 0x83;
	ext.offset = 20 * MiB; ext.length = 30 * MiB; ext.mbr_type = 0x05;
	log.offset = 21 * MiB; log.length = 8 * MiB; log.mbr_type = 0x83;
	l.entries = { p1, ext, LayoutEntry(), LayoutEntry(), log };
	std::string why;

	EXPECT_TRUE(session_range_allowed(l, 512, size, 1 * MiB, 18 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why)) << why;
	EXPECT_FALSE(session_range_allowed(l, 512, size, 1 * MiB, 20 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	EXPECT_FALSE(session_range_allowed(l, 512, size, 21 * MiB, 9 * MiB, SessionRange::PARTITION_AND_FREE_SPACE, why));
	// Inside the logical partition is fine for ordinary sessions
	EXPECT_TRUE(session_range_allowed(l, 512, size, 21 * MiB, 8 * MiB, SessionRange::INSIDE_PARTITION, why));
	// ... but not the extended container itself
	EXPECT_FALSE(session_range_allowed(l, 512, size, 20 * MiB, 512, SessionRange::INSIDE_PARTITION, why));
}


class BrokerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		backend.add_disk(0, 64 * MiB / 512);
		DiskLayout l = gpt_layout(64 * MiB);
		l.entries.push_back(gpt_entry(1 * MiB, 8 * MiB, "00000000-0000-0000-0000-000000000001"));
		std::string err;
		ASSERT_TRUE(TestSupport::MemDiskBackend::encode(backend.disk(0), l, true, err)) << err;
		// Recognisable content around the partition
		std::vector<uint8_t> pattern(512);
		for (uint64_t lba = 2047; lba <= 2048 + 16384; lba++)
		{
			for (size_t i = 0; i < pattern.size(); i++)
				pattern[i] = static_cast<uint8_t>(lba + i);
			backend.write_sectors(0, lba, 1, 512, pattern.data(), err);
		}
	}

	std::unique_ptr<WinDisk::DiskSession> open(bool writable)
	{
		std::string err;
		auto s = backend.open_session(0, 1 * MiB, 8 * MiB, writable, SessionRange::INSIDE_PARTITION, err);
		EXPECT_TRUE(s) << err;
		return s;
	}

	uint8_t disk_byte(uint64_t offset)
	{
		std::vector<uint8_t> s(512);
		std::string err;
		backend.read_sectors(0, offset / 512, 1, 512, s.data(), err);
		return s[offset % 512];
	}

	TestSupport::MemDiskBackend backend;
};


TEST_F(BrokerTest, SessionRefusesRangesOutsidePartitions)
{
	std::string err;
	EXPECT_FALSE(backend.open_session(0, 512 * 1024, 2 * MiB, true, SessionRange::INSIDE_PARTITION, err));
	EXPECT_FALSE(backend.open_session(0, 20 * MiB, MiB, false, SessionRange::INSIDE_PARTITION, err));
	EXPECT_FALSE(backend.open_session(7, 1 * MiB, MiB, false, SessionRange::INSIDE_PARTITION, err));
}


TEST_F(BrokerTest, ServerChecksEveryRequest)
{
	auto session = open(false);
	ASSERT_TRUE(session);
	Server server(*session);
	Reply rep;
	std::vector<uint8_t> out;

	Request info;
	info.op = OP_INFO;
	EXPECT_TRUE(server.handle(info, nullptr, rep, out));
	ASSERT_EQ(INFO_SIZE, out.size());
	EXPECT_EQ(STATUS_OK, rep.status);
	EXPECT_EQ(8 * MiB, out[0] | (uint64_t)out[1] << 8 | (uint64_t)out[2] << 16 | (uint64_t)out[3] << 24);
	EXPECT_EQ(INFO_FLAG_READ_ONLY, out[12]);

	// Unaligned read of the first bytes of the partition (disk LBA 2048)
	Request rd;
	rd.op = OP_READ;
	rd.offset = 3;
	rd.length = 10;
	EXPECT_TRUE(server.handle(rd, nullptr, rep, out));
	EXPECT_EQ(STATUS_OK, rep.status);
	ASSERT_EQ(10u, out.size());
	EXPECT_EQ(static_cast<uint8_t>(2048 + 3), out[0]);

	// Reads outside of the partition
	rd.offset = 8 * MiB - 4;
	rd.length = 8;
	EXPECT_TRUE(server.handle(rd, nullptr, rep, out));
	EXPECT_EQ(STATUS_RANGE, rep.status);
	EXPECT_TRUE(out.empty());
	rd.offset = UINT64_MAX - 2;
	rd.length = 8;
	EXPECT_TRUE(server.handle(rd, nullptr, rep, out));
	EXPECT_EQ(STATUS_RANGE, rep.status);
	rd.offset = 0;
	rd.length = MAX_TRANSFER + 1;
	EXPECT_TRUE(server.handle(rd, nullptr, rep, out));
	EXPECT_EQ(STATUS_TOO_BIG, rep.status);

	// Writes on a read-only session
	std::vector<uint8_t> data(512, 0xEE);
	Request wr;
	wr.op = OP_WRITE;
	wr.offset = 0;
	wr.length = 512;
	EXPECT_TRUE(server.handle(wr, data.data(), rep, out));
	EXPECT_EQ(STATUS_READ_ONLY, rep.status);
	EXPECT_EQ(static_cast<uint8_t>(2048), disk_byte(1 * MiB));

	// Unknown operations and bad magic end the conversation
	Request bad;
	bad.op = 99;
	EXPECT_FALSE(server.handle(bad, nullptr, rep, out));
	EXPECT_EQ(STATUS_PROTOCOL, rep.status);
	bad.op = OP_INFO;
	bad.magic = 0;
	EXPECT_FALSE(server.handle(bad, nullptr, rep, out));
	EXPECT_EQ(STATUS_PROTOCOL, rep.status);
}


TEST_F(BrokerTest, WritableSessionWritesOnlyWholeSectorsInside)
{
	auto session = open(true);
	ASSERT_TRUE(session);
	Server server(*session);
	Reply rep;
	std::vector<uint8_t> out;
	std::vector<uint8_t> data(1024, 0xAB);

	Request wr;
	wr.op = OP_WRITE;
	wr.offset = 512;
	wr.length = 1024;
	EXPECT_TRUE(server.handle(wr, data.data(), rep, out));
	EXPECT_EQ(STATUS_OK, rep.status);
	EXPECT_EQ(0xAB, disk_byte(1 * MiB + 512));
	EXPECT_EQ(0xAB, disk_byte(1 * MiB + 1535));
	EXPECT_NE(0xAB, disk_byte(1 * MiB + 1536));

	wr.offset = 100;
	wr.length = 512;
	EXPECT_TRUE(server.handle(wr, data.data(), rep, out));
	EXPECT_EQ(STATUS_ALIGNMENT, rep.status);

	// The sector just after the partition is never touched
	wr.offset = 8 * MiB;
	wr.length = 512;
	EXPECT_TRUE(server.handle(wr, data.data(), rep, out));
	EXPECT_EQ(STATUS_RANGE, rep.status);
	EXPECT_NE(0xAB, disk_byte(9 * MiB));
	EXPECT_EQ(2u, server.statistics().refused);
}


// Channel over a pair of pipes, used to run the C client against the server.
class PipeChannel : public Channel
{
public:
#if defined(_WIN32)
	PipeChannel(HANDLE rd, HANDLE wr) : m_rd(rd), m_wr(wr) {}
	bool read_exact(void* buffer, size_t length) override
	{
		uint8_t* p = static_cast<uint8_t*>(buffer);
		while (length)
		{
			DWORD n = 0;
			if (! ReadFile(m_rd, p, static_cast<DWORD>(length), &n, nullptr) || n == 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
	bool write_all(const void* buffer, size_t length) override
	{
		const uint8_t* p = static_cast<const uint8_t*>(buffer);
		while (length)
		{
			DWORD n = 0;
			if (! WriteFile(m_wr, p, static_cast<DWORD>(length), &n, nullptr) || n == 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
private:
	HANDLE m_rd, m_wr;
#else
	PipeChannel(int rd, int wr) : m_rd(rd), m_wr(wr) {}
	bool read_exact(void* buffer, size_t length) override
	{
		uint8_t* p = static_cast<uint8_t*>(buffer);
		while (length)
		{
			ssize_t n = ::read(m_rd, p, length);
			if (n <= 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
	bool write_all(const void* buffer, size_t length) override
	{
		const uint8_t* p = static_cast<const uint8_t*>(buffer);
		while (length)
		{
			ssize_t n = ::write(m_wr, p, length);
			if (n <= 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
private:
	int m_rd, m_wr;
#endif
};


struct PipePair
{
	intptr_t client_rd, client_wr, server_rd, server_wr;

	PipePair()
	{
#if defined(_WIN32)
		HANDLE a_rd, a_wr, b_rd, b_wr;
		CreatePipe(&a_rd, &a_wr, nullptr, 0);   // client -> server
		CreatePipe(&b_rd, &b_wr, nullptr, 0);   // server -> client
		client_wr = reinterpret_cast<intptr_t>(a_wr);
		server_rd = reinterpret_cast<intptr_t>(a_rd);
		server_wr = reinterpret_cast<intptr_t>(b_wr);
		client_rd = reinterpret_cast<intptr_t>(b_rd);
#else
		int a[2], b[2];
		EXPECT_EQ(0, pipe(a));
		EXPECT_EQ(0, pipe(b));
		client_wr = a[1];
		server_rd = a[0];
		server_wr = b[1];
		client_rd = b[0];
#endif
	}

	static void close_one(intptr_t& h)
	{
		if (h == -1)
			return;
#if defined(_WIN32)
		CloseHandle(reinterpret_cast<HANDLE>(h));
#else
		close(static_cast<int>(h));
#endif
		h = -1;
	}

	// End of stream for the server.
	void close_client()
	{
		close_one(client_wr);
		close_one(client_rd);
	}

	~PipePair()
	{
		close_one(client_rd);
		close_one(client_wr);
		close_one(server_rd);
		close_one(server_wr);
	}

	PipeChannel server_channel()
	{
#if defined(_WIN32)
		return PipeChannel(reinterpret_cast<HANDLE>(server_rd), reinterpret_cast<HANDLE>(server_wr));
#else
		return PipeChannel(static_cast<int>(server_rd), static_cast<int>(server_wr));
#endif
	}
};


TEST_F(BrokerTest, ClientLibraryAgainstServer)
{
	auto session = open(true);
	ASSERT_TRUE(session);
	PipePair pipes;
	bool orderly = false;
	Statistics stats;
	std::thread server_thread([&]()
	{
		Server server(*session);
		PipeChannel ch = pipes.server_channel();
		orderly = server.serve(ch);
		stats = server.statistics();
	});

	wpb_client* c = wpb_connect(pipes.client_rd, pipes.client_wr);
	ASSERT_NE(nullptr, c);
	EXPECT_EQ(8 * MiB, wpb_size(c));
	EXPECT_EQ(512u, wpb_sector_size(c));
	EXPECT_EQ(0, wpb_read_only(c));

	// Unaligned write across sector boundaries: read-modify-write by the client
	const char msg[] = "WParted broker: unaligned write across three sectors .....";
	std::vector<uint8_t> big(1100, 0x5A);
	std::memcpy(big.data(), msg, sizeof(msg));
	EXPECT_EQ(0, wpb_write(c, 300, big.data(), big.size()));
	std::vector<uint8_t> back(big.size());
	EXPECT_EQ(0, wpb_read(c, 300, back.data(), back.size()));
	EXPECT_EQ(big, back);
	// Bytes around the write keep their old content
	uint8_t before = 0, after = 0;
	EXPECT_EQ(0, wpb_read(c, 299, &before, 1));
	EXPECT_EQ(0, wpb_read(c, 1400, &after, 1));
	EXPECT_EQ(static_cast<uint8_t>(2048 + 299), before);
	EXPECT_EQ(static_cast<uint8_t>(2050 + 1400 - 1024), after);

	// Large transfer split into chunks
	std::vector<uint8_t> chunked(6 * MiB, 0x11);
	EXPECT_EQ(0, wpb_write(c, MiB, chunked.data(), chunked.size()));
	std::vector<uint8_t> chunked_back(chunked.size());
	EXPECT_EQ(0, wpb_read(c, MiB, chunked_back.data(), chunked_back.size()));
	EXPECT_EQ(chunked, chunked_back);

	// Outside the partition: refused by the client before sending, and the disk
	// after the partition is unchanged
	uint8_t one = 0xFF;
	EXPECT_NE(0, wpb_write(c, 8 * MiB, &one, 1));
	EXPECT_NE(0, wpb_read(c, 8 * MiB - 1, back.data(), 2));
	EXPECT_EQ(0, wpb_flush(c));
	wpb_close(c);
	pipes.close_client();
	server_thread.join();
	EXPECT_TRUE(orderly);
	EXPECT_EQ(0u, stats.refused);
	EXPECT_EQ(static_cast<uint8_t>(16384 + 2048), disk_byte(9 * MiB));
}


TEST_F(BrokerTest, ReadOnlyClientCannotWrite)
{
	auto session = open(false);
	ASSERT_TRUE(session);
	PipePair pipes;
	std::thread server_thread([&]()
	{
		Server server(*session);
		PipeChannel ch = pipes.server_channel();
		server.serve(ch);
	});
	wpb_client* c = wpb_connect(pipes.client_rd, pipes.client_wr);
	ASSERT_NE(nullptr, c);
	EXPECT_EQ(1, wpb_read_only(c));
	std::vector<uint8_t> data(512, 1);
	EXPECT_NE(0, wpb_write(c, 0, data.data(), data.size()));
	wpb_close(c);
	pipes.close_client();
	server_thread.join();
	EXPECT_EQ(static_cast<uint8_t>(2048), disk_byte(1 * MiB));
}


TEST(Sandbox, CommandLineQuoting)
{
	using WinSandbox::quote_argument;
	EXPECT_EQ(L"plain", quote_argument(L"plain"));
	EXPECT_EQ(L"\"\"", quote_argument(L""));
	EXPECT_EQ(L"\"two words\"", quote_argument(L"two words"));
	EXPECT_EQ(L"\"say \\\"hi\\\"\"", quote_argument(L"say \"hi\""));
	// Backslashes are only special before a quote
	EXPECT_EQ(L"C:\\dir\\file", quote_argument(L"C:\\dir\\file"));
	EXPECT_EQ(L"\"C:\\my dir\\\\\"", quote_argument(L"C:\\my dir\\"));
	EXPECT_EQ(L"\"a\\\\\\\"b\"", quote_argument(L"a\\\"b"));
	// A label trying to inject another argument stays one argument
	EXPECT_EQ(L"\"x\\\" -O ^has_journal \\\"\"", quote_argument(L"x\" -O ^has_journal \""));
}


}  // namespace Broker
}  // namespace WParted
