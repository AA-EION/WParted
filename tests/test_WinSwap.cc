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

#include "WinSwap.h"
#include "WinFsProbe.h"

#include "gtest/gtest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif


using namespace WParted;


namespace
{

class MemSession : public WinDisk::DiskSession
{
public:
	MemSession(uint64_t length, bool writable, uint32_t sector_size = 512)
	 : data(length, 0xA5), m_writable(writable), m_sector_size(sector_size) {}

	uint64_t length() const override       { return data.size(); }
	uint32_t sector_size() const override  { return m_sector_size; }
	bool writable() const override         { return m_writable; }
	bool read(uint64_t offset, uint64_t length, void* buffer, std::string& error) override
	{
		if (offset > data.size() || length > data.size() - offset)
		{
			error = "range";
			return false;
		}
		std::memcpy(buffer, &data[offset], length);
		return true;
	}
	bool write(uint64_t offset, uint64_t length, const void* buffer, std::string& error) override
	{
		if (! m_writable || offset > data.size() || length > data.size() - offset ||
		    offset % m_sector_size || length % m_sector_size)
		{
			error = "refused";
			return false;
		}
		std::memcpy(&data[offset], buffer, length);
		writes++;
		return true;
	}
	bool flush(std::string&) override  { flushes++; return true; }

	std::vector<uint8_t> data;
	int writes = 0;
	int flushes = 0;

private:
	bool     m_writable;
	uint32_t m_sector_size;
};


WinFsProbe::Result probe(MemSession& s)
{
	WinFsProbe::Result r;
	WinDisk::SectorReader reader = [&](uint64_t lba, uint32_t count, std::vector<uint8_t>& out)
	{
		out.resize(size_t(count) * 512);
		std::string e;
		return s.read(lba * 512, out.size(), out.data(), e);
	};
	WinFsProbe::probe(reader, 512, s.length() / 512, r);
	return r;
}

const uint64_t MiB = 1024 * 1024;

}  // anonymous namespace


TEST(WinSwap, UuidRoundTrip)
{
	uint8_t raw[16];
	ASSERT_TRUE(WinSwap::parse_uuid("01234567-89ab-cdef-0123-456789ABCDEF", raw));
	EXPECT_EQ(raw[0], 0x01);
	EXPECT_EQ(raw[15], 0xEF);
	EXPECT_EQ(WinSwap::format_uuid(raw), "01234567-89ab-cdef-0123-456789abcdef");
	EXPECT_FALSE(WinSwap::parse_uuid("01234567-89ab-cdef-0123-456789abcde", raw));
	EXPECT_FALSE(WinSwap::parse_uuid("0123456789ab-cdef-0123-456789abcdef0", raw));
	EXPECT_FALSE(WinSwap::parse_uuid("g1234567-89ab-cdef-0123-456789abcdef", raw));

	std::string a = WinSwap::random_uuid();
	std::string b = WinSwap::random_uuid();
	ASSERT_EQ(a.size(), 36u);
	EXPECT_NE(a, b);
	EXPECT_EQ(a[14], '4');
	EXPECT_TRUE(std::strchr("89ab", a[19]) != nullptr);
}


TEST(WinSwap, CreateReadRelabel)
{
	MemSession s(64 * MiB, true);
	std::string error;
	ASSERT_TRUE(WinSwap::create(s, "swap", "01234567-89ab-cdef-0123-456789abcdef", error)) << error;
	EXPECT_EQ(s.flushes, 1);

	// Only the first page is written.
	for (size_t i = WinSwap::PAGE_SIZE; i < s.data.size(); i += 4093)
		ASSERT_EQ(s.data[i], 0xA5) << i;
	for (size_t i = 0; i < 1024; i++)
		ASSERT_EQ(s.data[i], 0) << i;

	WinSwap::Info info;
	ASSERT_TRUE(WinSwap::read(s, info, error)) << error;
	EXPECT_EQ(info.last_page, 64 * MiB / 4096 - 1);
	EXPECT_EQ(info.uuid, "01234567-89ab-cdef-0123-456789abcdef");
	EXPECT_EQ(info.label, "swap");

	WinFsProbe::Result r = probe(s);
	EXPECT_EQ(r.type, "swap");
	EXPECT_EQ(r.uuid, "01234567-89ab-cdef-0123-456789abcdef");
	EXPECT_EQ(r.label, "swap");

	ASSERT_TRUE(WinSwap::set_label(s, "sixteen-bytes-ok", error)) << error;
	ASSERT_TRUE(WinSwap::set_uuid(s, "fedcba98-7654-4321-8123-456789abcdef", error)) << error;
	ASSERT_TRUE(WinSwap::read(s, info, error));
	EXPECT_EQ(info.label, "sixteen-bytes-ok");
	EXPECT_EQ(info.uuid, "fedcba98-7654-4321-8123-456789abcdef");
	EXPECT_EQ(info.last_page, 64 * MiB / 4096 - 1);

	EXPECT_FALSE(WinSwap::set_label(s, "seventeen-bytes!!", error));
	EXPECT_FALSE(WinSwap::set_uuid(s, "not-a-uuid", error));
	ASSERT_TRUE(WinSwap::read(s, info, error));
	EXPECT_EQ(info.label, "sixteen-bytes-ok");
}


TEST(WinSwap, Refusals)
{
	std::string error;
	MemSession tiny(9 * 4096, true);
	EXPECT_FALSE(WinSwap::create(tiny, "", "", error));
	EXPECT_EQ(tiny.writes, 0);

	MemSession ro(16 * MiB, false);
	EXPECT_FALSE(WinSwap::create(ro, "", "", error));

	MemSession other(16 * MiB, true);
	EXPECT_FALSE(WinSwap::set_label(other, "x", error));   // No swap header
	EXPECT_FALSE(WinSwap::set_uuid(other, "01234567-89ab-cdef-0123-456789abcdef", error));
	EXPECT_EQ(other.writes, 0);

	MemSession big_sectors(16 * MiB, true, 4096);
	EXPECT_TRUE(WinSwap::create(big_sectors, "", "", error)) << error;
	WinSwap::Info info;
	ASSERT_TRUE(WinSwap::read(big_sectors, info, error));
	EXPECT_EQ(info.uuid.size(), 36u);
}


#if !defined(_WIN32)
// Byte for byte the same first page as util-linux mkswap, when it is installed.
TEST(WinSwap, SameAsMkswap)
{
	const char* mkswap = nullptr;
	for (const char* p : { "/usr/sbin/mkswap", "/sbin/mkswap" })
		if (std::ifstream(p))
			mkswap = p;
	if (! mkswap)
		GTEST_SKIP() << "mkswap not installed";

	char path[] = "/tmp/wparted-swap-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(ftruncate(fd, 32 * MiB), 0);
	close(fd);
	std::string cmd = std::string(mkswap) + " -q -L wpswap -U 01234567-89ab-cdef-0123-456789abcdef " + path +
	                  " > /dev/null 2>&1";
	int rc = std::system(cmd.c_str());
	std::vector<uint8_t> theirs(4096);
	{
		std::ifstream in(path, std::ios::binary);
		in.read(reinterpret_cast<char*>(theirs.data()), theirs.size());
	}
	unlink(path);
	ASSERT_EQ(rc, 0);

	MemSession s(32 * MiB, true);
	std::string error;
	ASSERT_TRUE(WinSwap::create(s, "wpswap", "01234567-89ab-cdef-0123-456789abcdef", error)) << error;
	std::vector<uint8_t> ours(s.data.begin(), s.data.begin() + 4096);
	EXPECT_EQ(ours, theirs);
}
#endif
