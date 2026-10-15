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

#include <cstring>
#include <vector>


namespace WParted
{
namespace WinFsUsage
{


// Standard CRC32C (Castagnoli, reflected, initial and final value ~0), as used
// by btrfs and XFS.  'crc' continues a previous result (0 to start).
uint32_t crc32c(const uint8_t* data, size_t length, uint32_t crc)
{
	static uint32_t table[256];
	static bool init = false;
	if (! init)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
			table[i] = c;
		}
		init = true;
	}
	crc = ~crc;
	for (size_t i = 0; i < length; i++)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}


// APFS object checksum over everything after the 8 byte checksum field.
uint64_t apfs_fletcher64(const uint8_t* block, size_t length)
{
	const uint64_t mod = 0xFFFFFFFFULL;
	uint64_t sum1 = 0, sum2 = 0;
	for (size_t i = 8; i + 4 <= length; i += 4)
	{
		uint32_t w = block[i] | (block[i + 1] << 8) | (block[i + 2] << 16) |
		             (static_cast<uint32_t>(block[i + 3]) << 24);
		sum1 = (sum1 + w) % mod;
		sum2 = (sum2 + sum1) % mod;
	}
	uint64_t c1 = mod - ((sum1 + sum2) % mod);
	uint64_t c2 = mod - ((sum1 + c1) % mod);
	return (c2 << 32) | c1;
}


namespace
{

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint64_t le64(const uint8_t* p) { return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32); }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
uint64_t be64(const uint8_t* p) { return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4); }


bool fail(std::string& error, const char* why)
{
	error = why;
	return false;
}


// Read 'length' bytes at byte 'offset' of the partition.
bool read_bytes(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t part_length,
                uint64_t offset, size_t length, std::vector<uint8_t>& out, std::string& error)
{
	if (ss == 0 || offset > part_length || length > part_length - offset || length > 64 * 1024 * 1024)
		return fail(error, "metadata outside of the partition");
	uint64_t first = offset / ss;
	uint64_t last = (offset + length + ss - 1) / ss;
	std::vector<uint8_t> buf;
	if (! reader(first, static_cast<uint32_t>(last - first), buf) || buf.size() < (last - first) * ss)
		return fail(error, "read error");
	size_t skip = static_cast<size_t>(offset - first * ss);
	out.assign(buf.begin() + skip, buf.begin() + skip + length);
	return true;
}


bool ext(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t len, Usage& out, std::string& error)
{
	std::vector<uint8_t> sb;
	if (! read_bytes(reader, ss, len, 1024, 1024, sb, error))
		return false;
	if (le16(&sb[56]) != 0xEF53)
		return fail(error, "no ext2/3/4 superblock");
	uint32_t log = le32(&sb[24]);
	if (log > 6)
		return fail(error, "invalid ext block size");
	uint64_t bs = 1024ULL << log;
	uint64_t blocks = le32(&sb[4]);
	uint64_t free_blocks = le32(&sb[12]);
	const uint32_t INCOMPAT_64BIT = 0x80;
	if (le32(&sb[96]) & INCOMPAT_64BIT)
	{
		blocks |= static_cast<uint64_t>(le32(&sb[0x150])) << 32;
		free_blocks |= static_cast<uint64_t>(le32(&sb[0x158])) << 32;
	}
	if (blocks == 0 || free_blocks > blocks || blocks > len / bs)
		return fail(error, "ext block counts do not fit the partition");
	out.size = blocks * bs;
	out.free = free_blocks * bs;
	return true;
}


bool btrfs(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t len, Usage& out, std::string& error)
{
	std::vector<uint8_t> sb;
	if (! read_bytes(reader, ss, len, 64 * 1024, 4096, sb, error))
		return false;
	if (std::memcmp(&sb[0x40], "_BHRfS_M", 8) != 0)
		return fail(error, "no btrfs superblock");
	if (le16(&sb[0xC4]) == 0 && crc32c(&sb[0x20], 4096 - 0x20) != le32(&sb[0]))
		return fail(error, "btrfs superblock checksum mismatch");
	uint64_t total = le64(&sb[0x70]);
	uint64_t used = le64(&sb[0x78]);
	if (le64(&sb[0x88]) != 1)
		return fail(error, "multi-device btrfs");
	if (total == 0 || used > total || total > len)
		return fail(error, "btrfs sizes do not fit the partition");
	out.size = total;
	out.free = total - used;
	return true;
}


bool xfs(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t len, Usage& out, std::string& error)
{
	std::vector<uint8_t> sb;
	if (! read_bytes(reader, ss, len, 0, 512, sb, error))
		return false;
	if (std::memcmp(&sb[0], "XFSB", 4) != 0)
		return fail(error, "no XFS superblock");
	uint32_t bs = be32(&sb[4]);
	uint64_t dblocks = be64(&sb[8]);
	uint64_t fdblocks = be64(&sb[144]);
	if (bs < 512 || bs > 65536 || (bs & (bs - 1)))
		return fail(error, "invalid XFS block size");
	if ((be16(&sb[100]) & 0xF) == 5)
	{
		uint16_t sectsize = be16(&sb[102]);
		if (sectsize < 512 || sectsize > 32768 || (sectsize & (sectsize - 1)))
			return fail(error, "invalid XFS sector size");
		std::vector<uint8_t> full;
		if (! read_bytes(reader, ss, len, 0, sectsize, full, error))
			return false;
		uint32_t want = le32(&full[224]);
		std::memset(&full[224], 0, 4);
		if (crc32c(full.data(), full.size()) != want)
			return fail(error, "XFS superblock checksum mismatch");
	}
	if (dblocks == 0 || fdblocks > dblocks || dblocks > len / bs)
		return fail(error, "XFS block counts do not fit the partition");
	out.size = dblocks * bs;
	out.free = fdblocks * bs;
	return true;
}


bool hfsplus(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t len, Usage& out, std::string& error)
{
	std::vector<uint8_t> vh;
	if (! read_bytes(reader, ss, len, 1024, 512, vh, error))
		return false;
	uint16_t sig = be16(&vh[0]);
	if (sig != 0x482B && sig != 0x4858)   // "H+", "HX"
		return fail(error, "no HFS+ volume header");
	uint32_t bs = be32(&vh[40]);
	uint64_t total = be32(&vh[44]);
	uint64_t free_blocks = be32(&vh[48]);
	if (bs < 512 || (bs & (bs - 1)) || total == 0 || free_blocks > total || total > len / bs)
		return fail(error, "HFS+ block counts do not fit the partition");
	out.size = total * bs;
	out.free = free_blocks * bs;
	return true;
}


// APFS: find the latest valid container superblock in the checkpoint
// descriptor area, then the space manager through that checkpoint's map.
bool apfs(const WinDisk::SectorReader& reader, uint32_t ss, uint64_t len, Usage& out, std::string& error)
{
	const uint32_t OBJECT_TYPE_NX_SUPERBLOCK  = 0x01;
	const uint32_t OBJECT_TYPE_CHECKPOINT_MAP = 0x0C;
	const uint32_t OBJECT_TYPE_SPACEMAN       = 0x05;
	const uint32_t NX_MAGIC                   = 0x4253584E;   // "NXSB"

	std::vector<uint8_t> nx;
	if (! read_bytes(reader, ss, len, 0, 4096, nx, error))
		return false;
	if (le32(&nx[32]) != NX_MAGIC)
		return fail(error, "no APFS container superblock");
	uint32_t bs = le32(&nx[36]);
	if (bs < 4096 || bs > 65536 || (bs & (bs - 1)))
		return fail(error, "invalid APFS block size");

	auto read_block = [&](uint64_t block, std::vector<uint8_t>& b) -> bool
	{
		if (block > len / bs)
			return fail(error, "APFS block outside of the partition");
		return read_bytes(reader, ss, len, block * bs, bs, b, error);
	};
	auto valid = [&](const std::vector<uint8_t>& b, uint32_t type) -> bool
	{
		return (le32(&b[24]) & 0xFFFF) == type && apfs_fletcher64(b.data(), b.size()) == le64(&b[0]);
	};

	if (! read_block(0, nx) || ! valid(nx, OBJECT_TYPE_NX_SUPERBLOCK))
		return fail(error, "APFS container superblock checksum mismatch");

	uint32_t desc_blocks = le32(&nx[104]);
	uint64_t desc_base = le64(&nx[112]);
	if (desc_blocks & 0x80000000u)
		return fail(error, "APFS checkpoint area is not contiguous");
	if (desc_blocks == 0 || desc_blocks > 65536)
		return fail(error, "invalid APFS checkpoint area");

	// Latest valid superblock in the descriptor area.
	std::vector<uint8_t> best;
	uint64_t best_xid = 0;
	std::vector<uint8_t> b;
	for (uint32_t i = 0; i < desc_blocks; i++)
	{
		if (! read_block(desc_base + i, b))
			return false;
		if (le32(&b[32]) == NX_MAGIC && valid(b, OBJECT_TYPE_NX_SUPERBLOCK) && le64(&b[16]) > best_xid)
		{
			best = b;
			best_xid = le64(&b[16]);
		}
	}
	if (best.empty())
		return fail(error, "no valid APFS checkpoint");

	uint64_t block_count = le64(&best[40]);
	uint32_t index = le32(&best[136]);
	uint32_t count = le32(&best[140]);
	uint64_t spaceman_oid = le64(&best[152]);
	if (le32(&best[36]) != bs || block_count == 0 || block_count > len / bs || count == 0 || count > desc_blocks ||
	    index >= desc_blocks)
		return fail(error, "APFS checkpoint does not fit the partition");

	// Checkpoint map blocks precede the superblock copy in the same checkpoint.
	uint64_t spaceman_addr = 0;
	uint32_t spaceman_size = 0;
	for (uint32_t k = 0; k + 1 < count && ! spaceman_addr; k++)
	{
		if (! read_block(desc_base + (index + k) % desc_blocks, b))
			return false;
		if (! valid(b, OBJECT_TYPE_CHECKPOINT_MAP) || le64(&b[16]) != best_xid)
			continue;
		uint32_t entries = le32(&b[36]);
		if (entries > (bs - 40) / 40)
			return fail(error, "invalid APFS checkpoint map");
		for (uint32_t e = 0; e < entries; e++)
		{
			const uint8_t* m = &b[40 + 40 * e];
			if (le64(&m[24]) == spaceman_oid && (le32(&m[0]) & 0xFFFF) == OBJECT_TYPE_SPACEMAN)
			{
				spaceman_size = le32(&m[8]);
				spaceman_addr = le64(&m[32]);
				break;
			}
		}
	}
	if (! spaceman_addr || spaceman_size < bs || spaceman_size > 16 * bs || spaceman_size % bs)
		return fail(error, "APFS space manager not found");

	std::vector<uint8_t> sm;
	if (spaceman_addr > len / bs || spaceman_size > len - spaceman_addr * bs ||
	    ! read_bytes(reader, ss, len, spaceman_addr * bs, spaceman_size, sm, error))
		return fail(error, "APFS space manager outside of the partition");
	if ((le32(&sm[24]) & 0xFFFF) != OBJECT_TYPE_SPACEMAN || apfs_fletcher64(sm.data(), sm.size()) != le64(&sm[0]))
		return fail(error, "APFS space manager checksum mismatch");
	uint64_t main_blocks = le64(&sm[48]);
	uint64_t main_free = le64(&sm[72]);
	if (le32(&sm[32]) != bs || main_blocks != block_count || main_free > main_blocks)
		return fail(error, "APFS space manager does not match the container");
	out.size = block_count * bs;
	out.free = main_free * bs;
	return true;
}

}  // anonymous namespace


bool supported(const std::string& fs)
{
	return fs == "ext2" || fs == "ext3" || fs == "ext4" || fs == "btrfs" || fs == "xfs" || fs == "hfs+" ||
	       fs == "apfs";
}


bool read(const std::string& fs, const WinDisk::SectorReader& reader, uint32_t sector_size,
          uint64_t length, Usage& out, std::string& error)
{
	out = Usage();
	if (fs == "ext2" || fs == "ext3" || fs == "ext4")
		return ext(reader, sector_size, length, out, error);
	if (fs == "btrfs")
		return btrfs(reader, sector_size, length, out, error);
	if (fs == "xfs")
		return xfs(reader, sector_size, length, out, error);
	if (fs == "hfs+")
		return hfsplus(reader, sector_size, length, out, error);
	if (fs == "apfs")
		return apfs(reader, sector_size, length, out, error);
	error = "usage of " + fs + " is not read by WParted";
	return false;
}


}  // namespace WinFsUsage
}  // namespace WParted
