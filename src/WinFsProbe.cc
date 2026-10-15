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

#include "WinFsProbe.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>


namespace WParted
{
namespace WinFsProbe
{


namespace
{

// Byte addressed reader over a sector reader with a small cache.
class Bytes
{
public:
	Bytes(const WinDisk::SectorReader& reader, uint32_t sector_size, uint64_t length_sectors)
	 : m_reader(reader), m_sector_size(sector_size), m_length(uint64_t(length_sectors) * sector_size)
	{}

	// Copy 'n' bytes at byte 'offset' into 'out'.  False when outside the partition or
	// on read error.
	bool get(uint64_t offset, size_t n, std::vector<uint8_t>& out)
	{
		out.clear();
		if (n == 0 || offset + n > m_length || offset + n < offset)
			return false;
		uint64_t first = offset / m_sector_size;
		uint64_t last  = (offset + n - 1) / m_sector_size;
		for (uint64_t s = first; s <= last; s++)
		{
			const std::vector<uint8_t>* sec = sector(s);
			if (! sec)
				return false;
			uint64_t sec_start = s * m_sector_size;
			uint64_t from = (offset > sec_start) ? offset - sec_start : 0;
			uint64_t to   = std::min<uint64_t>(m_sector_size, offset + n - sec_start);
			out.insert(out.end(), sec->begin() + from, sec->begin() + to);
		}
		return out.size() == n;
	}

	bool match(uint64_t offset, const char* magic, size_t n)
	{
		std::vector<uint8_t> buf;
		return get(offset, n, buf) && std::memcmp(buf.data(), magic, n) == 0;
	}

	uint64_t length() const  { return m_length; }

private:
	const std::vector<uint8_t>* sector(uint64_t s)
	{
		auto it = m_cache.find(s);
		if (it != m_cache.end())
			return it->second.empty() ? nullptr : &it->second;
		std::vector<uint8_t> data;
		bool ok = m_reader(s, 1, data) && data.size() >= m_sector_size;
		if (! ok)
			data.clear();
		auto& slot = m_cache[s];
		slot.swap(data);
		return slot.empty() ? nullptr : &slot;
	}

	const WinDisk::SectorReader&             m_reader;
	uint32_t                                  m_sector_size;
	uint64_t                                  m_length;
	std::map<uint64_t, std::vector<uint8_t>> m_cache;
};


uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }


std::string uuid_lower(const uint8_t* u)
{
	char buf[40];
	std::snprintf(buf, sizeof(buf),
	              "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	              u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
	              u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
	return buf;
}


bool all_zero(const uint8_t* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}


std::string ascii_label(const uint8_t* p, size_t n, bool trim_spaces)
{
	std::string s;
	for (size_t i = 0; i < n && p[i]; i++)
		s.push_back(static_cast<char>(p[i]));
	if (trim_spaces)
		while (! s.empty() && s.back() == ' ')
			s.pop_back();
	return s;
}


std::string utf16le_label(const uint8_t* p, size_t chars)
{
	std::u16string u;
	for (size_t i = 0; i < chars; i++)
	{
		char16_t c = static_cast<char16_t>(le16(p + 2 * i));
		if (c == 0)
			break;
		u.push_back(c);
	}
	return WinDisk::utf16_to_utf8(u);
}


void set_label(Result& r, const std::string& label)
{
	r.label = label;
	r.have_label = ! label.empty();
}


// --- Individual probes -----------------------------------------------------

bool probe_luks(Bytes& b, Result& r)
{
	std::vector<uint8_t> hdr;
	if (! b.get(0, 512, hdr) || std::memcmp(hdr.data(), "LUKS\xBA\xBE", 6) != 0)
		return false;
	uint16_t version = be16(&hdr[6]);
	if (version != 1 && version != 2)
		return false;
	r.type = "crypto_LUKS";
	r.uuid = ascii_label(&hdr[168], 40, false);
	if (version == 2)
		set_label(r, ascii_label(&hdr[24], 48, false));
	return true;
}


bool probe_bitlocker(Bytes& b, Result& r)
{
	if (! b.match(3, "-FVE-FS-", 8))
		return false;
	r.type = "BitLocker";
	return true;
}


bool probe_ntfs(Bytes& b, Result& r)
{
	std::vector<uint8_t> bs;
	if (! b.get(0, 512, bs) || std::memcmp(&bs[3], "NTFS    ", 8) != 0)
		return false;
	uint16_t bps = le16(&bs[11]);
	if (bps < 256 || bps > 4096 || (bps & (bps - 1)) != 0)
		return false;
	r.type = "ntfs";
	char buf[20];
	std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(le64(&bs[0x48])));
	r.uuid = buf;
	// The NTFS volume label lives in the $Volume MFT record; on Windows it is
	// taken from the mounted volume instead.
	return true;
}


bool probe_refs(Bytes& b, Result& r)
{
	std::vector<uint8_t> bs;
	if (! b.get(0, 512, bs) || std::memcmp(&bs[3], "ReFS\0\0\0\0", 8) != 0)
		return false;
	r.type = "ReFS";
	return true;
}


bool probe_exfat(Bytes& b, Result& r)
{
	std::vector<uint8_t> bs;
	if (! b.get(0, 512, bs) || std::memcmp(&bs[3], "EXFAT   ", 8) != 0)
		return false;
	r.type = "exfat";
	uint32_t serial = le32(&bs[0x64]);
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%04X-%04X", serial >> 16, serial & 0xFFFF);
	r.uuid = buf;

	// Volume label: directory entry type 0x83 in the first cluster of the root
	// directory.
	uint8_t bps_shift = bs[0x6C];
	uint8_t spc_shift = bs[0x6D];
	if (bps_shift < 9 || bps_shift > 12 || spc_shift > 25 - bps_shift)
		return true;
	uint64_t bytes_per_sector = 1ULL << bps_shift;
	uint64_t cluster_bytes = bytes_per_sector << spc_shift;
	uint64_t heap_offset = uint64_t(le32(&bs[0x58])) * bytes_per_sector;
	uint32_t root_cluster = le32(&bs[0x60]);
	if (root_cluster < 2)
		return true;
	uint64_t root_offset = heap_offset + uint64_t(root_cluster - 2) * cluster_bytes;
	size_t scan = static_cast<size_t>(std::min<uint64_t>(cluster_bytes, 64 * 1024));
	std::vector<uint8_t> dir;
	if (! b.get(root_offset, scan, dir))
		return true;
	for (size_t off = 0; off + 32 <= dir.size(); off += 32)
	{
		uint8_t type = dir[off];
		if (type == 0x00)
			break;
		if (type == 0x83)
		{
			uint8_t chars = dir[off + 1];
			if (chars > 11)
				chars = 11;
			set_label(r, utf16le_label(&dir[off + 2], chars));
			break;
		}
	}
	return true;
}


bool probe_vfat(Bytes& b, Result& r)
{
	std::vector<uint8_t> bs;
	if (! b.get(0, 512, bs))
		return false;
	if (bs[510] != 0x55 || bs[511] != 0xAA)
		return false;
	if (bs[0] != 0xEB && bs[0] != 0xE9)
		return false;
	uint16_t bps      = le16(&bs[11]);
	uint8_t  spc      = bs[13];
	uint16_t reserved = le16(&bs[14]);
	uint8_t  fats     = bs[16];
	uint16_t root_ent = le16(&bs[17]);
	uint16_t tot16    = le16(&bs[19]);
	uint8_t  media    = bs[21];
	uint16_t fat16sz  = le16(&bs[22]);
	uint32_t tot32    = le32(&bs[32]);
	uint32_t fat32sz  = le32(&bs[36]);
	if (bps < 512 || bps > 4096 || (bps & (bps - 1)) != 0)
		return false;
	if (spc == 0 || (spc & (spc - 1)) != 0)
		return false;
	if (reserved == 0 || fats == 0 || fats > 2)
		return false;
	if (media != 0xF0 && media < 0xF8)
		return false;
	uint32_t fatsz = fat16sz ? fat16sz : fat32sz;
	uint32_t total = tot16 ? tot16 : tot32;
	if (fatsz == 0 || total == 0)
		return false;
	uint32_t root_dir_sectors = (uint32_t(root_ent) * 32 + bps - 1) / bps;
	uint64_t meta = uint64_t(reserved) + uint64_t(fats) * fatsz + root_dir_sectors;
	if (meta >= total)
		return false;
	uint64_t clusters = (total - meta) / spc;

	bool fat32 = clusters >= 65525 || (fat16sz == 0 && root_ent == 0);
	r.type = "vfat";
	r.sec_type = fat32 ? "" : "msdos";

	size_t serial_off = fat32 ? 0x43 : 0x27;
	size_t label_off  = fat32 ? 0x47 : 0x2B;
	size_t sig_off    = fat32 ? 0x42 : 0x26;
	if (bs[sig_off] == 0x29 || bs[sig_off] == 0x28)
	{
		uint32_t serial = le32(&bs[serial_off]);
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%04X-%04X", serial >> 16, serial & 0xFFFF);
		r.uuid = buf;
		if (bs[sig_off] == 0x29)
		{
			std::string label = ascii_label(&bs[label_off], 11, true);
			if (label != "NO NAME")
				set_label(r, label);
		}
	}
	return true;
}


bool probe_xfs(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(0, 512, sb) || std::memcmp(sb.data(), "XFSB", 4) != 0)
		return false;
	r.type = "xfs";
	r.uuid = uuid_lower(&sb[32]);
	set_label(r, ascii_label(&sb[108], 12, false));
	return true;
}


bool probe_lvm2(Bytes& b, Result& r)
{
	// Label header in one of the first 4 sectors of 512 bytes.
	for (uint64_t s = 0; s < 4; s++)
	{
		std::vector<uint8_t> lh;
		if (! b.get(s * 512, 512, lh))
			continue;
		if (std::memcmp(lh.data(), "LABELONE", 8) != 0 || std::memcmp(&lh[24], "LVM2 001", 8) != 0)
			continue;
		uint32_t offset = le32(&lh[20]);
		if (offset + 32 > 512)
			return false;
		const uint8_t* id = &lh[offset];
		std::string raw(reinterpret_cast<const char*>(id), 32);
		r.type = "LVM2_member";
		r.uuid = raw.substr(0, 6) + "-" + raw.substr(6, 4) + "-" + raw.substr(10, 4) + "-" +
		         raw.substr(14, 4) + "-" + raw.substr(18, 4) + "-" + raw.substr(22, 4) + "-" +
		         raw.substr(26, 6);
		return true;
	}
	return false;
}


bool probe_ext(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(1024, 1024, sb) || le16(&sb[0x38]) != 0xEF53)
		return false;
	uint32_t compat    = le32(&sb[0x5C]);
	uint32_t incompat  = le32(&sb[0x60]);
	uint32_t ro_compat = le32(&sb[0x64]);
	const uint32_t COMPAT_HAS_JOURNAL     = 0x0004;
	const uint32_t INCOMPAT_JOURNAL_DEV   = 0x0008;
	const uint32_t INCOMPAT_EXT3_SUPP     = 0x0002 | 0x0004 | 0x0010;  // filetype, recover, meta_bg
	const uint32_t RO_COMPAT_EXT3_SUPP    = 0x0001 | 0x0002 | 0x0004;  // sparse_super, large_file, btree_dir

	if (incompat & INCOMPAT_JOURNAL_DEV)
		r.type = "jbd";
	else if ((incompat & ~INCOMPAT_EXT3_SUPP) || (ro_compat & ~RO_COMPAT_EXT3_SUPP))
		r.type = "ext4";
	else if (compat & COMPAT_HAS_JOURNAL)
		r.type = "ext3";
	else
		r.type = "ext2";
	r.uuid = uuid_lower(&sb[0x68]);
	set_label(r, ascii_label(&sb[0x78], 16, false));
	return true;
}


bool probe_btrfs(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(65536, 4096, sb) || std::memcmp(&sb[64], "_BHRfS_M", 8) != 0)
		return false;
	r.type = "btrfs";
	r.uuid = uuid_lower(&sb[32]);
	set_label(r, ascii_label(&sb[299], 256, false));
	return true;
}


bool probe_swap(Bytes& b, Result& r)
{
	static const uint64_t page_sizes[] = { 4096, 8192, 16384, 65536 };
	for (uint64_t page : page_sizes)
	{
		if (b.match(page - 10, "SWAPSPACE2", 10) || b.match(page - 10, "SWAP-SPACE", 10))
		{
			r.type = "swap";
			std::vector<uint8_t> hdr;
			if (b.match(page - 10, "SWAPSPACE2", 10) && b.get(1024, 44, hdr))
			{
				if (! all_zero(&hdr[12], 16))
					r.uuid = uuid_lower(&hdr[12]);
				set_label(r, ascii_label(&hdr[28], 16, false));
			}
			return true;
		}
		if (b.match(page - 10, "S1SUSPEND", 9) || b.match(page - 10, "S2SUSPEND", 9) ||
		    b.match(page - 10, "ULSUSPEND", 9) || b.match(page - 10, "LINHIB0001", 10))
		{
			r.type = "swsuspend";
			return true;
		}
	}
	return false;
}


bool probe_f2fs(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(1024, 512, sb) || le32(&sb[0]) != 0xF2F52010)
		return false;
	r.type = "f2fs";
	return true;
}


bool probe_jfs(Bytes& b, Result& r)
{
	if (! b.match(32768, "JFS1", 4))
		return false;
	r.type = "jfs";
	return true;
}


bool probe_reiserfs(Bytes& b, Result& r)
{
	if (b.match(65536 + 52, "ReIsErFs", 8) || b.match(65536 + 52, "ReIsEr2Fs", 9) ||
	    b.match(65536 + 52, "ReIsEr3Fs", 9) || b.match(8192 + 52, "ReIsErFs", 8))
	{
		r.type = "reiserfs";
		return true;
	}
	if (b.match(65536, "ReIsEr4", 7))
	{
		r.type = "reiser4";
		return true;
	}
	return false;
}


bool probe_hfs(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(1024, 512, sb))
		return false;
	if (sb[0] == 'H' && (sb[1] == '+' || sb[1] == 'X'))
	{
		r.type = "hfsplus";  // HFS+ and HFSX
		return true;
	}
	if (sb[0] == 'B' && sb[1] == 'D')
	{
		// HFS wrapper which may embed HFS+.
		uint16_t embed_sig = be16(&sb[0x7C]);
		r.type = (embed_sig == 0x482B) ? "hfsplus" : "hfs";
		return true;
	}
	return false;
}


bool probe_apfs(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(0, 64, sb))
		return false;
	if (sb[24] == 0x01 && sb[25] == 0x00 && std::memcmp(&sb[32], "NXSB", 4) == 0)
	{
		r.type = "apfs";
		r.uuid = uuid_lower(&sb[72]);
		return true;
	}
	return false;
}


bool probe_iso9660(Bytes& b, Result& r)
{
	std::vector<uint8_t> pvd;
	if (! b.get(32768, 2048, pvd) || std::memcmp(&pvd[1], "CD001", 5) != 0)
		return false;
	r.type = "iso9660";
	set_label(r, ascii_label(&pvd[40], 32, true));
	return true;
}


bool probe_udf(Bytes& b, Result& r)
{
	// Volume recognition sequence descriptors at 32 KiB, 2 KiB apart.
	for (uint64_t off = 32768; off < 32768 + 2048 * 6; off += 2048)
	{
		if (b.match(off + 1, "NSR02", 5) || b.match(off + 1, "NSR03", 5))
		{
			r.type = "udf";
			return true;
		}
	}
	return false;
}


bool probe_nilfs2(Bytes& b, Result& r)
{
	std::vector<uint8_t> sb;
	if (! b.get(1024, 512, sb) || le16(&sb[6]) != 0x3434)
		return false;
	r.type = "nilfs2";
	r.uuid = uuid_lower(&sb[0x98]);
	set_label(r, ascii_label(&sb[0xA8], 80, false));
	return true;
}


bool probe_linux_raid(Bytes& b, Result& r)
{
	// mdadm metadata 1.1 (offset 0) and 1.2 (offset 4 KiB).
	static const uint64_t offsets[] = { 0, 4096 };
	for (uint64_t off : offsets)
	{
		std::vector<uint8_t> sb;
		if (b.get(off, 256, sb) && le32(&sb[0]) == 0xA92B4EFC && le32(&sb[4]) == 1)
		{
			r.type = "linux_raid_member";
			r.uuid = uuid_lower(&sb[16]);
			return true;
		}
	}
	return false;
}

}  // anonymous namespace


std::string Result::libparted_name() const
{
	if (type == "ntfs")                     return "ntfs";
	if (type == "vfat")                     return sec_type == "msdos" ? "fat16" : "fat32";
	if (type == "exfat")                    return "exfat";
	if (type == "ReFS")                     return "refs";
	if (type == "udf")                      return "udf";
	if (type == "ext2" || type == "ext3" ||
	    type == "ext4")                     return type;
	if (type == "btrfs" || type == "xfs" || type == "jfs" || type == "reiserfs" ||
	    type == "f2fs" || type == "nilfs2")  return type;
	if (type == "hfs")                      return "hfs";
	if (type == "hfsplus")                  return "hfs+";
	if (type == "swap")                     return "linux-swap";
	// Everything else (LUKS, BitLocker, APFS, LVM, RAID members, ...) is treated as
	// unknown by the partition type logic so its partition type is preserved.
	return "";
}


bool probe(const WinDisk::SectorReader& reader, uint32_t sector_size, uint64_t length_sectors,
           Result& result)
{
	result = Result();
	if (sector_size < 512 || length_sectors == 0)
		return false;

	Bytes b(reader, sector_size, length_sectors);

	typedef bool (*Probe)(Bytes&, Result&);
	static const Probe probes[] = {
		probe_luks, probe_bitlocker, probe_ntfs, probe_refs, probe_exfat, probe_xfs,
		probe_lvm2, probe_linux_raid, probe_ext, probe_btrfs, probe_swap, probe_f2fs,
		probe_jfs, probe_reiserfs, probe_hfs, probe_apfs, probe_iso9660, probe_udf,
		probe_nilfs2,
		probe_vfat  // Last: weakest signature
	};
	for (Probe p : probes)
	{
		Result r;
		if (p(b, r))
		{
			result = r;
			return true;
		}
	}
	return false;
}


}  // namespace WinFsProbe
}  // namespace WParted
