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
#include "parted/parted.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <sstream>


namespace WParted
{
namespace WinDisk
{


// ---------------------------------------------------------------------------
// GUIDs
// ---------------------------------------------------------------------------

static Guid make_guid(uint32_t d1, uint16_t d2, uint16_t d3,
                      uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                      uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7)
{
	Guid g;
	g.data1 = d1;
	g.data2 = d2;
	g.data3 = d3;
	g.data4[0] = b0; g.data4[1] = b1; g.data4[2] = b2; g.data4[3] = b3;
	g.data4[4] = b4; g.data4[5] = b5; g.data4[6] = b6; g.data4[7] = b7;
	return g;
}

namespace TypeGuid
{
	const Guid ESP               = make_guid(0xC12A7328, 0xF81F, 0x11D2, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B);
	const Guid MSFT_RESERVED     = make_guid(0xE3C9E316, 0x0B5C, 0x4DB8, 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE);
	const Guid BASIC_DATA        = make_guid(0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7);
	const Guid MSFT_RECOVERY     = make_guid(0xDE94BBA4, 0x06D1, 0x4D40, 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC);
	const Guid LDM_METADATA      = make_guid(0x5808C8AA, 0x7E8F, 0x42E0, 0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3);
	const Guid LDM_DATA          = make_guid(0xAF9B60A0, 0x1431, 0x4F62, 0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD);
	const Guid STORAGE_SPACES    = make_guid(0xE75CAF8F, 0xF680, 0x4CEE, 0xAF, 0xA3, 0xB0, 0x01, 0xE5, 0x6E, 0xFC, 0x2D);
	const Guid LINUX_DATA        = make_guid(0x0FC63DAF, 0x8483, 0x4772, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4);
	const Guid LINUX_SWAP        = make_guid(0x0657FD6D, 0xA4AB, 0x43C4, 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F);
	const Guid LINUX_LVM         = make_guid(0xE6D6D379, 0xF507, 0x44C2, 0xA2, 0x3C, 0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28);
	const Guid LINUX_RAID        = make_guid(0xA19D880F, 0x05FC, 0x4D3B, 0xA0, 0x06, 0x74, 0x3F, 0x0F, 0x84, 0x91, 0x1E);
	const Guid LINUX_HOME        = make_guid(0x933AC7E1, 0x2EB4, 0x4F13, 0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15);
	const Guid BIOS_GRUB         = make_guid(0x21686148, 0x6449, 0x6E6F, 0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49);
	const Guid HP_SERVICE        = make_guid(0xE2A1E728, 0x32E3, 0x11D6, 0xA6, 0x82, 0x7B, 0x03, 0xA0, 0x00, 0x00, 0x00);
	const Guid APPLE_HFS         = make_guid(0x48465300, 0x0000, 0x11AA, 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC);
	const Guid APPLE_APFS        = make_guid(0x7C3457EF, 0x0000, 0x11AA, 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC);
	const Guid APPLE_TV_RECOVERY = make_guid(0x5265636F, 0x7665, 0x11AA, 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC);
	const Guid PREP              = make_guid(0x9E1A2D38, 0xC612, 0x4316, 0xAA, 0x26, 0x8B, 0x49, 0x52, 0x1E, 0x5A, 0x8B);
	const Guid IRST              = make_guid(0xD3BFE2DE, 0x3DAF, 0x11DF, 0xBA, 0x40, 0xE3, 0xA5, 0x56, 0xD8, 0x95, 0x93);
	const Guid CHROMEOS_KERNEL   = make_guid(0xFE3A2A5D, 0x4F32, 0x41A7, 0xB7, 0x25, 0xAC, 0xCC, 0x32, 0x85, 0xA3, 0x09);
	const Guid BLS_BOOT          = make_guid(0xBC13C2FF, 0x59E6, 0x4262, 0xA3, 0x52, 0xB2, 0x75, 0xFD, 0x6F, 0x71, 0x72);
}


bool operator==(const Guid& lhs, const Guid& rhs)
{
	return lhs.data1 == rhs.data1 && lhs.data2 == rhs.data2 && lhs.data3 == rhs.data3 &&
	       std::memcmp(lhs.data4, rhs.data4, sizeof(lhs.data4)) == 0;
}


bool operator!=(const Guid& lhs, const Guid& rhs)
{
	return ! (lhs == rhs);
}


bool Guid::is_null() const
{
	return *this == Guid();
}


std::string Guid::to_string() const
{
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
	              static_cast<unsigned>(data1), data2, data3,
	              data4[0], data4[1], data4[2], data4[3],
	              data4[4], data4[5], data4[6], data4[7]);
	return std::string(buf);
}


static int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}


bool Guid::parse(const std::string& text, Guid& out)
{
	std::string s = text;
	if (s.size() == 38 && s.front() == '{' && s.back() == '}')
		s = s.substr(1, 36);
	if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
		return false;

	std::string hex;
	for (char c : s)
		if (c != '-')
			hex += c;
	if (hex.size() != 32)
		return false;

	uint8_t bytes[16];
	for (int i = 0; i < 16; i++)
	{
		int hi = hex_value(hex[2 * i]);
		int lo = hex_value(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
	}

	// Text form is big endian for every field.
	out.data1 = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
	out.data2 = static_cast<uint16_t>((bytes[4] << 8) | bytes[5]);
	out.data3 = static_cast<uint16_t>((bytes[6] << 8) | bytes[7]);
	std::memcpy(out.data4, bytes + 8, 8);
	return true;
}


Guid Guid::from_bytes_le(const uint8_t* b)
{
	Guid g;
	g.data1 = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
	g.data2 = static_cast<uint16_t>(b[4] | (b[5] << 8));
	g.data3 = static_cast<uint16_t>(b[6] | (b[7] << 8));
	std::memcpy(g.data4, b + 8, 8);
	return g;
}


void Guid::to_bytes_le(uint8_t* b) const
{
	b[0] = static_cast<uint8_t>(data1);
	b[1] = static_cast<uint8_t>(data1 >> 8);
	b[2] = static_cast<uint8_t>(data1 >> 16);
	b[3] = static_cast<uint8_t>(data1 >> 24);
	b[4] = static_cast<uint8_t>(data2);
	b[5] = static_cast<uint8_t>(data2 >> 8);
	b[6] = static_cast<uint8_t>(data3);
	b[7] = static_cast<uint8_t>(data3 >> 8);
	std::memcpy(b + 8, data4, 8);
}


Guid generate_guid()
{
	std::random_device rd;
	uint8_t bytes[16];
	for (int i = 0; i < 16; i += 4)
	{
		uint32_t r = rd();
		bytes[i]     = static_cast<uint8_t>(r);
		bytes[i + 1] = static_cast<uint8_t>(r >> 8);
		bytes[i + 2] = static_cast<uint8_t>(r >> 16);
		bytes[i + 3] = static_cast<uint8_t>(r >> 24);
	}
	Guid g = Guid::from_bytes_le(bytes);
	g.data3 = static_cast<uint16_t>((g.data3 & 0x0FFF) | 0x4000);       // Version 4
	g.data4[0] = static_cast<uint8_t>((g.data4[0] & 0x3F) | 0x80);     // RFC 4122 variant
	return g;
}


bool MbrType::is_extended(uint8_t type)
{
	return type == EXTENDED || type == EXTENDED_LBA || type == EXTENDED_LINUX;
}


// ---------------------------------------------------------------------------
// Little helpers
// ---------------------------------------------------------------------------

static uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }


static std::string num(int64_t n)
{
	return std::to_string(static_cast<long long>(n));
}


uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc)
{
	static uint32_t table[256];
	static bool initialised = false;
	if (! initialised)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		initialised = true;
	}

	crc = ~crc;
	for (size_t i = 0; i < length; i++)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}


std::u16string utf8_to_utf16(const std::string& text)
{
	std::u16string out;
	size_t i = 0;
	while (i < text.size())
	{
		uint32_t cp;
		unsigned char c = static_cast<unsigned char>(text[i]);
		int extra;
		if      (c < 0x80)           { cp = c;        extra = 0; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
		else                         { cp = 0xFFFD;   extra = 0; }
		i++;
		for (int k = 0; k < extra; k++)
		{
			if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
			{
				cp = 0xFFFD;
				break;
			}
			cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
			i++;
		}
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		}
		else
		{
			out.push_back(static_cast<char16_t>(cp));
		}
	}
	return out;
}


std::string utf16_to_utf8(const std::u16string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++)
	{
		uint32_t cp = text[i];
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() &&
		    text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
		{
			cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
			i++;
		}
		else if (cp >= 0xD800 && cp <= 0xDFFF)
		{
			cp = 0xFFFD;
		}

		if (cp < 0x80)
		{
			out.push_back(static_cast<char>(cp));
		}
		else if (cp < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}
	return out;
}


// ---------------------------------------------------------------------------
// DiskModel
// ---------------------------------------------------------------------------

const PartRecord* DiskModel::find_by_num(int n) const
{
	for (const PartRecord& p : parts)
		if (p.num == n)
			return &p;
	return nullptr;
}


PartRecord* DiskModel::find_by_num(int n)
{
	for (PartRecord& p : parts)
		if (p.num == n)
			return &p;
	return nullptr;
}


const PartRecord* DiskModel::find_extended() const
{
	for (const PartRecord& p : parts)
		if (p.kind == PartKind::EXTENDED)
			return &p;
	return nullptr;
}


void DiskModel::sort_parts()
{
	// Primaries and extended by start, each extended followed by its logicals in
	// order of their start.  This is the libparted iteration order.
	std::vector<PartRecord> tops;
	std::vector<PartRecord> logicals;
	for (const PartRecord& p : parts)
	{
		if (p.kind == PartKind::LOGICAL)
			logicals.push_back(p);
		else
			tops.push_back(p);
	}
	auto by_start = [](const PartRecord& a, const PartRecord& b) { return a.start < b.start; };
	std::stable_sort(tops.begin(), tops.end(), by_start);
	std::stable_sort(logicals.begin(), logicals.end(), by_start);

	parts.clear();
	for (const PartRecord& p : tops)
	{
		parts.push_back(p);
		if (p.kind == PartKind::EXTENDED)
			parts.insert(parts.end(), logicals.begin(), logicals.end());
	}
}


// ---------------------------------------------------------------------------
// Raw on-disk parsing
// ---------------------------------------------------------------------------

static bool looks_like_volume_boot_record(const std::vector<uint8_t>& s)
{
	if (s.size() < 512)
		return false;
	if (std::memcmp(&s[3], "NTFS    ", 8) == 0 ||
	    std::memcmp(&s[3], "EXFAT   ", 8) == 0 ||
	    std::memcmp(&s[3], "-FVE-FS-", 8) == 0 ||
	    std::memcmp(&s[3], "ReFS", 4) == 0)
		return true;

	// FAT: jump instruction plus a sane BIOS parameter block and a FAT type string.
	bool jump = s[0] == 0xEB || s[0] == 0xE9;
	uint16_t bytes_per_sector = le16(&s[11]);
	uint8_t  sectors_per_cluster = s[13];
	uint16_t reserved = le16(&s[14]);
	uint8_t  fats = s[16];
	bool bps_ok = bytes_per_sector >= 512 && bytes_per_sector <= 4096 &&
	              (bytes_per_sector & (bytes_per_sector - 1)) == 0;
	bool spc_ok = sectors_per_cluster != 0 && (sectors_per_cluster & (sectors_per_cluster - 1)) == 0;
	bool fat_string = std::memcmp(&s[0x36], "FAT", 3) == 0 || std::memcmp(&s[0x52], "FAT", 3) == 0;
	return jump && bps_ok && spc_ok && reserved > 0 && (fats == 1 || fats == 2) && fat_string;
}


static bool parse_gpt_header(const std::vector<uint8_t>& sec, uint64_t expected_lba,
                             uint32_t sector_size, uint32_t& num_entries, uint32_t& entry_size,
                             uint64_t& entries_lba, uint32_t& entries_crc, uint64_t& alt_lba,
                             uint64_t& first_usable, uint64_t& last_usable, Guid& disk_guid,
                             std::string& error)
{
	if (sec.size() < 92 || std::memcmp(&sec[0], "EFI PART", 8) != 0)
	{
		error = "GPT header signature not found";
		return false;
	}
	uint32_t header_size = le32(&sec[12]);
	if (header_size < 92 || header_size > sector_size)
	{
		error = "GPT header size " + num(header_size) + " is invalid";
		return false;
	}
	std::vector<uint8_t> copy(sec.begin(), sec.begin() + header_size);
	uint32_t stored_crc = le32(&copy[16]);
	copy[16] = copy[17] = copy[18] = copy[19] = 0;
	if (crc32(copy.data(), copy.size()) != stored_crc)
	{
		error = "GPT header CRC32 mismatch";
		return false;
	}
	uint64_t my_lba = le64(&sec[24]);
	if (my_lba != expected_lba)
	{
		error = "GPT header at LBA " + num(expected_lba) + " claims to be at LBA " + num(my_lba);
		return false;
	}
	alt_lba      = le64(&sec[32]);
	first_usable = le64(&sec[40]);
	last_usable  = le64(&sec[48]);
	disk_guid    = Guid::from_bytes_le(&sec[56]);
	entries_lba  = le64(&sec[72]);
	num_entries  = le32(&sec[80]);
	entry_size   = le32(&sec[84]);
	entries_crc  = le32(&sec[88]);
	if (entry_size < 128 || entry_size % 8 != 0 || entry_size > 4096 ||
	    num_entries == 0 || num_entries > 16384)
	{
		error = "GPT partition entry array geometry is invalid";
		return false;
	}
	if (first_usable > last_usable)
	{
		error = "GPT usable area is invalid";
		return false;
	}
	return true;
}


static bool read_gpt(const SectorReader& reader, uint32_t sector_size, int64_t total_sectors,
                     RawTable& out, std::string& error)
{
	std::vector<uint8_t> hdr;
	if (! reader(1, 1, hdr))
	{
		error = "Unable to read GPT header";
		return false;
	}

	uint32_t num_entries, entry_size, entries_crc;
	uint64_t entries_lba, alt_lba, first_usable, last_usable;
	Guid disk_guid;
	if (! parse_gpt_header(hdr, 1, sector_size, num_entries, entry_size, entries_lba, entries_crc,
	                       alt_lba, first_usable, last_usable, disk_guid, error))
	{
		error = "Primary " + error;
		return false;
	}
	if (last_usable >= static_cast<uint64_t>(total_sectors))
	{
		error = "GPT last usable sector " + num(last_usable) + " is beyond the end of the disk";
		return false;
	}

	uint64_t array_bytes = uint64_t(num_entries) * entry_size;
	uint32_t array_sectors = static_cast<uint32_t>((array_bytes + sector_size - 1) / sector_size);
	std::vector<uint8_t> array;
	if (! reader(entries_lba, array_sectors, array) || array.size() < array_bytes)
	{
		error = "Unable to read GPT partition entry array";
		return false;
	}
	if (crc32(array.data(), static_cast<size_t>(array_bytes)) != entries_crc)
	{
		error = "GPT partition entry array CRC32 mismatch";
		return false;
	}

	out.style            = TableStyle::GPT;
	out.gpt_disk_guid    = disk_guid;
	out.gpt_first_usable = first_usable;
	out.gpt_last_usable  = last_usable;
	out.gpt_num_entries  = num_entries;
	out.gpt_entries.clear();
	for (uint32_t i = 0; i < num_entries; i++)
	{
		const uint8_t* e = &array[size_t(i) * entry_size];
		Guid type = Guid::from_bytes_le(e);
		if (type.is_null())
			continue;
		RawTable::GptEntry entry;
		entry.index      = i;
		entry.type       = type;
		entry.id         = Guid::from_bytes_le(e + 16);
		entry.first_lba  = le64(e + 32);
		entry.last_lba   = le64(e + 40);
		entry.attributes = le64(e + 48);
		for (int k = 0; k < 36; k++)
		{
			char16_t c = static_cast<char16_t>(le16(e + 56 + 2 * k));
			if (c == 0)
				break;
			entry.name.push_back(c);
		}
		out.gpt_entries.push_back(entry);
	}

	// The backup header is checked but not required.  Windows rewrites both copies
	// when the layout is set.
	out.gpt_backup_ok = false;
	std::vector<uint8_t> backup;
	if (alt_lba > 1 && alt_lba < static_cast<uint64_t>(total_sectors) && reader(alt_lba, 1, backup))
	{
		uint32_t b_num, b_size, b_crc;
		uint64_t b_lba, b_alt, b_first, b_last;
		Guid b_guid;
		std::string b_error;
		out.gpt_backup_ok = parse_gpt_header(backup, alt_lba, sector_size, b_num, b_size, b_lba, b_crc,
		                                     b_alt, b_first, b_last, b_guid, b_error) &&
		                    b_guid == disk_guid && b_crc == entries_crc;
	}
	return true;
}


bool read_raw_table(const SectorReader& reader, uint32_t sector_size, int64_t total_sectors,
                    RawTable& out, std::string& error)
{
	out = RawTable();
	if (sector_size < 512 || total_sectors < 2)
	{
		error = "Invalid disk geometry";
		return false;
	}

	std::vector<uint8_t> mbr;
	if (! reader(0, 1, mbr) || mbr.size() < 512)
	{
		error = "Unable to read sector 0";
		return false;
	}
	if (mbr[510] != 0x55 || mbr[511] != 0xAA)
	{
		out.style = TableStyle::RAW;
		return true;
	}
	if (looks_like_volume_boot_record(mbr))
	{
		out.style = TableStyle::MBR;
		out.superfloppy = true;
		return true;
	}

	out.mbr_signature = le32(&mbr[440]);
	bool protective = false;
	bool others = false;
	for (int i = 0; i < 4; i++)
	{
		const uint8_t* e = &mbr[446 + 16 * i];
		RawTable::MbrSlot& slot = out.slots[i];
		if (e[0] != 0x00 && e[0] != 0x80)
		{
			error = "MBR entry " + num(i + 1) + " has an invalid boot indicator";
			return false;
		}
		slot.boot      = e[0] == 0x80;
		slot.type      = e[4];
		slot.lba_start = le32(e + 8);
		slot.sectors   = le32(e + 12);
		if (slot.type == MbrType::EMPTY)
			continue;
		if (slot.type == MbrType::GPT_PROTECTIVE)
			protective = true;
		else
			others = true;
	}

	if (protective)
	{
		out.hybrid_mbr = others;
		return read_gpt(reader, sector_size, total_sectors, out, error);
	}

	out.style = TableStyle::MBR;

	// Follow the EBR chain of the extended partition, if any.
	int ext_slot = -1;
	for (int i = 0; i < 4; i++)
	{
		if (out.slots[i].type != MbrType::EMPTY && MbrType::is_extended(out.slots[i].type))
		{
			if (ext_slot >= 0)
			{
				error = "MBR contains more than one extended partition";
				return false;
			}
			ext_slot = i;
		}
	}
	if (ext_slot < 0)
		return true;

	uint64_t ext_start = out.slots[ext_slot].lba_start;
	uint64_t ext_end   = ext_start + out.slots[ext_slot].sectors - 1;
	uint64_t ebr = ext_start;
	std::set<uint64_t> visited;
	for (int guard = 0; guard < 256; guard++)
	{
		if (ebr < ext_start || ebr > ext_end || visited.count(ebr))
		{
			error = "EBR chain is corrupt (bad link to sector " + num(ebr) + ")";
			return false;
		}
		visited.insert(ebr);

		std::vector<uint8_t> sec;
		if (! reader(ebr, 1, sec) || sec.size() < 512)
		{
			error = "Unable to read EBR at sector " + num(ebr);
			return false;
		}
		if (sec[510] != 0x55 || sec[511] != 0xAA)
		{
			// Windows writes no EBR for an extended partition without
			// logical partitions, leaving its first sector as it was.
			if (ebr == ext_start)
				break;
			error = "EBR at sector " + num(ebr) + " has no boot signature";
			return false;
		}
		const uint8_t* e0 = &sec[446];
		const uint8_t* e1 = &sec[446 + 16];
		if (e0[4] != MbrType::EMPTY && le32(e0 + 12) != 0)
		{
			RawTable::Logical log;
			log.ebr_lba = ebr;
			log.type    = e0[4];
			log.boot    = e0[0] == 0x80;
			log.start   = ebr + le32(e0 + 8);
			log.sectors = le32(e0 + 12);
			if (MbrType::is_extended(log.type))
			{
				error = "EBR at sector " + num(ebr) + " contains a nested extended partition";
				return false;
			}
			out.logicals.push_back(log);
		}
		if (e1[4] == MbrType::EMPTY || le32(e1 + 12) == 0)
			break;
		if (! MbrType::is_extended(e1[4]))
		{
			error = "EBR at sector " + num(ebr) + " has an invalid link entry";
			return false;
		}
		ebr = ext_start + le32(e1 + 8);
	}
	return true;
}


// ---------------------------------------------------------------------------
// Windows layout <-> model
// ---------------------------------------------------------------------------

static bool to_sectors(uint64_t bytes, uint32_t sector_size, int64_t& sectors)
{
	if (bytes % sector_size != 0)
		return false;
	sectors = static_cast<int64_t>(bytes / sector_size);
	return true;
}


bool layout_to_model(const DiskLayout& layout, uint32_t sector_size, int64_t total_sectors,
                     const RawTable* raw, DiskModel& out, std::string& error)
{
	out = DiskModel();
	out.style         = layout.style;
	out.sector_size   = sector_size;
	out.total_sectors = total_sectors;

	if (sector_size == 0 || total_sectors <= 0)
	{
		error = "Invalid disk geometry";
		return false;
	}

	if (layout.style == TableStyle::RAW)
		return true;

	if (layout.style == TableStyle::MBR)
	{
		out.mbr_signature = layout.mbr_signature;
		out.first_usable  = 1;
		out.last_usable   = total_sectors - 1;

		// Windows reports a whole disk volume without partition table
		// ("superfloppy") as an MBR disk with one partition at offset 0.
		for (const LayoutEntry& e : layout.entries)
		{
			if (e.length > 0 && e.offset == 0 && e.mbr_type != MbrType::EMPTY)
			{
				out.superfloppy = true;
				return true;
			}
		}

		// Entries 0..3 are the four MBR slots.
		const PartRecord* ext = nullptr;
		size_t n = layout.entries.size();
		for (size_t i = 0; i < 4 && i < n; i++)
		{
			const LayoutEntry& e = layout.entries[i];
			if (e.mbr_type == MbrType::EMPTY || e.length == 0)
				continue;
			PartRecord rec;
			rec.num      = static_cast<int>(i) + 1;
			rec.kind     = MbrType::is_extended(e.mbr_type) ? PartKind::EXTENDED : PartKind::PRIMARY;
			rec.mbr_type = e.mbr_type;
			rec.mbr_boot = e.mbr_boot;
			rec.existing = true;
			int64_t len;
			if (! to_sectors(e.offset, sector_size, rec.start) || ! to_sectors(e.length, sector_size, len))
			{
				error = "MBR entry " + num(rec.num) + " is not sector aligned";
				return false;
			}
			rec.end = rec.start + len - 1;
			derive_type_flags(TableStyle::MBR, rec);
			out.parts.push_back(rec);
		}
		for (const PartRecord& p : out.parts)
			if (p.kind == PartKind::EXTENDED)
				ext = &p;

		// Remaining entries are groups of four, one group per EBR: the logical
		// partition and the link to the next EBR.
		if (n > 4)
		{
			if (ext == nullptr)
			{
				error = "Windows reported logical partitions without an extended partition";
				return false;
			}
			int64_t ext_start = ext->start;
			int64_t ebr = ext_start;
			int next_num = 5;
			for (size_t g = 4; g < n; g += 4)
			{
				const LayoutEntry* logical = nullptr;
				const LayoutEntry* link = nullptr;
				for (size_t k = g; k < g + 4 && k < n; k++)
				{
					const LayoutEntry& e = layout.entries[k];
					if (e.mbr_type == MbrType::EMPTY || e.length == 0)
						continue;
					if (MbrType::is_extended(e.mbr_type))
					{
						if (link)
						{
							error = "EBR group has two link entries";
							return false;
						}
						link = &e;
					}
					else
					{
						if (logical)
						{
							error = "EBR group has two logical partitions";
							return false;
						}
						logical = &e;
					}
				}
				if (logical)
				{
					PartRecord rec;
					rec.num        = next_num++;
					rec.kind       = PartKind::LOGICAL;
					rec.mbr_type   = logical->mbr_type;
					rec.mbr_boot   = logical->mbr_boot;
					rec.ebr_sector = ebr;
					rec.existing   = true;
					int64_t len;
					if (! to_sectors(logical->offset, sector_size, rec.start) ||
					    ! to_sectors(logical->length, sector_size, len)          )
					{
						error = "Logical partition " + num(rec.num) + " is not sector aligned";
						return false;
					}
					rec.end = rec.start + len - 1;
					derive_type_flags(TableStyle::MBR, rec);
					out.parts.push_back(rec);
				}
				if (! link)
					break;
				int64_t next_ebr;
				if (! to_sectors(link->offset, sector_size, next_ebr))
				{
					error = "EBR link is not sector aligned";
					return false;
				}
				ebr = next_ebr;
			}
		}
		out.sort_parts();
		return true;
	}

	// GPT
	out.gpt_disk_id     = layout.gpt_disk_id;
	out.gpt_max_entries = layout.gpt_max_entries ? layout.gpt_max_entries : 128;
	int64_t first, len;
	if (! to_sectors(layout.gpt_first_usable_offset, sector_size, first) ||
	    ! to_sectors(layout.gpt_usable_length, sector_size, len)            )
	{
		error = "GPT usable area is not sector aligned";
		return false;
	}
	out.first_usable = first;
	out.last_usable  = first + len - 1;

	std::vector<const LayoutEntry*> used;
	for (const LayoutEntry& e : layout.entries)
		if (e.length > 0 && ! e.gpt_type.is_null())
			used.push_back(&e);

	// Use the on-disk entry slot numbers when every partition can be matched to
	// exactly one raw entry.  Otherwise number in Windows order.
	bool use_raw = raw && raw->style == TableStyle::GPT && raw->gpt_entries.size() == used.size();
	std::vector<int> nums(used.size(), 0);
	if (use_raw)
	{
		std::set<int> seen;
		for (size_t i = 0; i < used.size() && use_raw; i++)
		{
			int found = 0;
			for (const RawTable::GptEntry& r : raw->gpt_entries)
			{
				if (r.first_lba * sector_size == used[i]->offset && r.id == used[i]->gpt_id)
				{
					found = static_cast<int>(r.index) + 1;
					break;
				}
			}
			if (found == 0 || seen.count(found))
				use_raw = false;
			seen.insert(found);
			nums[i] = found;
		}
	}
	for (size_t i = 0; i < used.size(); i++)
	{
		const LayoutEntry& e = *used[i];
		PartRecord rec;
		rec.num         = use_raw ? nums[i] : static_cast<int>(i) + 1;
		rec.kind        = PartKind::PRIMARY;
		rec.type_guid   = e.gpt_type;
		rec.unique_guid = e.gpt_id;
		rec.attributes  = e.gpt_attributes;
		rec.name        = e.gpt_name;
		rec.existing    = true;
		int64_t plen;
		if (! to_sectors(e.offset, sector_size, rec.start) || ! to_sectors(e.length, sector_size, plen))
		{
			error = "GPT partition " + num(rec.num) + " is not sector aligned";
			return false;
		}
		rec.end = rec.start + plen - 1;
		derive_type_flags(TableStyle::GPT, rec);
		out.parts.push_back(rec);
	}
	out.sort_parts();
	return true;
}


bool cross_check(const DiskModel& model, const RawTable& raw, std::string& error)
{
	if (model.superfloppy || raw.superfloppy)
	{
		if (model.superfloppy != raw.superfloppy)
		{
			error = "Windows and the on-disk structures disagree about a whole disk volume";
			return false;
		}
		return true;
	}

	if (model.style != raw.style)
	{
		error = "Windows and the on-disk structures disagree about the partition table type";
		return false;
	}

	if (model.style == TableStyle::RAW)
		return true;

	if (model.style == TableStyle::MBR)
	{
		if (model.mbr_signature != raw.mbr_signature)
		{
			error = "MBR disk signature differs between Windows and the disk";
			return false;
		}
		for (int i = 0; i < 4; i++)
		{
			const RawTable::MbrSlot& slot = raw.slots[i];
			const PartRecord* rec = model.find_by_num(i + 1);
			bool slot_used = slot.type != MbrType::EMPTY && slot.sectors != 0;
			if (slot_used != (rec != nullptr))
			{
				error = "MBR slot " + num(i + 1) + " differs between Windows and the disk";
				return false;
			}
			if (! rec)
				continue;
			if (rec->start != int64_t(slot.lba_start) || rec->length() != int64_t(slot.sectors) ||
			    rec->mbr_type != slot.type || rec->mbr_boot != slot.boot)
			{
				error = "MBR partition " + num(i + 1) + " differs between Windows and the disk";
				return false;
			}
		}
		std::vector<const PartRecord*> logicals;
		for (const PartRecord& p : model.parts)
			if (p.kind == PartKind::LOGICAL)
				logicals.push_back(&p);
		if (logicals.size() != raw.logicals.size())
		{
			error = "Number of logical partitions differs between Windows (" + num(logicals.size()) +
			        ") and the disk (" + num(raw.logicals.size()) + ")";
			return false;
		}
		// Model logicals are sorted by start; the raw chain normally is too.  Match
		// each raw logical by start.
		for (const RawTable::Logical& r : raw.logicals)
		{
			const PartRecord* match = nullptr;
			for (const PartRecord* p : logicals)
				if (p->start == int64_t(r.start))
					match = p;
			if (! match || match->length() != int64_t(r.sectors) || match->mbr_type != r.type ||
			    match->mbr_boot != r.boot || match->ebr_sector != int64_t(r.ebr_lba))
			{
				error = "Logical partition at sector " + num(r.start) +
				        " differs between Windows and the disk";
				return false;
			}
		}
		return true;
	}

	// GPT
	if (model.gpt_disk_id != raw.gpt_disk_guid)
	{
		error = "GPT disk GUID differs between Windows and the disk";
		return false;
	}
	if (model.first_usable != int64_t(raw.gpt_first_usable) ||
	    model.last_usable  != int64_t(raw.gpt_last_usable)     )
	{
		error = "GPT usable area differs between Windows and the disk";
		return false;
	}
	if (model.parts.size() != raw.gpt_entries.size())
	{
		error = "Number of GPT partitions differs between Windows (" + num(model.parts.size()) +
		        ") and the disk (" + num(raw.gpt_entries.size()) + ")";
		return false;
	}
	for (const RawTable::GptEntry& r : raw.gpt_entries)
	{
		const PartRecord* match = nullptr;
		for (const PartRecord& p : model.parts)
			if (p.start == int64_t(r.first_lba) && p.unique_guid == r.id)
				match = &p;
		if (! match || match->end != int64_t(r.last_lba) || match->type_guid != r.type ||
		    match->attributes != r.attributes || match->name != r.name)
		{
			error = "GPT partition in entry " + num(r.index + 1) + " differs between Windows and the disk";
			return false;
		}
	}
	return true;
}


// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

static bool overlaps(const PartRecord& a, const PartRecord& b)
{
	return a.start <= b.end && b.start <= a.end;
}


bool validate_model(const DiskModel& m, std::string& error)
{
	if (m.sector_size < 512 || m.total_sectors <= 0)
	{
		error = "Invalid disk geometry";
		return false;
	}
	if (m.superfloppy)
	{
		error = "Partitions can not be changed on a whole disk volume without a partition table";
		return false;
	}
	if (m.style == TableStyle::RAW)
	{
		if (! m.parts.empty())
		{
			error = "Partitions require a partition table";
			return false;
		}
		return true;
	}

	for (const PartRecord& p : m.parts)
	{
		if (p.start > p.end)
		{
			error = "Partition " + num(p.num) + " has a negative length";
			return false;
		}
		if (p.start < m.first_usable || p.end > m.last_usable)
		{
			error = "Partition " + num(p.num) + " (sectors " + num(p.start) + "-" + num(p.end) +
			        ") is outside the usable area of the disk (sectors " + num(m.first_usable) +
			        "-" + num(m.last_usable) + ")";
			return false;
		}
	}

	if (m.style == TableStyle::GPT)
	{
		if (m.parts.size() > m.gpt_max_entries)
		{
			error = "Too many partitions for the GPT partition entry array";
			return false;
		}
		std::set<int> nums;
		std::set<std::string> ids;
		for (const PartRecord& p : m.parts)
		{
			if (p.kind != PartKind::PRIMARY)
			{
				error = "GPT does not support extended or logical partitions";
				return false;
			}
			if (p.num < 1 || p.num > static_cast<int>(m.gpt_max_entries) || nums.count(p.num))
			{
				error = "Invalid or duplicate GPT partition number " + num(p.num);
				return false;
			}
			nums.insert(p.num);
			if (p.type_guid.is_null())
			{
				error = "GPT partition " + num(p.num) + " has no type GUID";
				return false;
			}
			if (p.unique_guid.is_null() || p.unique_guid == m.gpt_disk_id ||
			    ids.count(p.unique_guid.to_string()))
			{
				error = "GPT partition " + num(p.num) + " has a null or duplicate unique GUID";
				return false;
			}
			ids.insert(p.unique_guid.to_string());
			if (p.name.size() > 36)
			{
				error = "GPT partition " + num(p.num) + " name is longer than 36 characters";
				return false;
			}
		}
		for (size_t i = 0; i < m.parts.size(); i++)
			for (size_t j = i + 1; j < m.parts.size(); j++)
				if (overlaps(m.parts[i], m.parts[j]))
				{
					error = "Partitions " + num(m.parts[i].num) + " and " + num(m.parts[j].num) + " overlap";
					return false;
				}
		return true;
	}

	// MBR
	const int64_t MAX32 = 0xFFFFFFFFLL;
	const PartRecord* ext = nullptr;
	std::set<int> prim_nums;
	std::vector<const PartRecord*> tops;
	std::vector<const PartRecord*> logicals;
	for (const PartRecord& p : m.parts)
	{
		if (p.mbr_type == MbrType::EMPTY)
		{
			error = "MBR partition " + num(p.num) + " has partition type 0 (unused)";
			return false;
		}
		if (p.start > MAX32 || p.length() > MAX32)
		{
			error = "MBR partition " + num(p.num) + " is beyond the 2 TiB limit of MBR partition tables";
			return false;
		}
		if (p.kind == PartKind::LOGICAL)
		{
			if (MbrType::is_extended(p.mbr_type))
			{
				error = "Logical partition " + num(p.num) + " has an extended partition type";
				return false;
			}
			logicals.push_back(&p);
			continue;
		}
		if (p.num < 1 || p.num > 4 || prim_nums.count(p.num))
		{
			error = "Invalid or duplicate primary partition number " + num(p.num);
			return false;
		}
		prim_nums.insert(p.num);
		if (p.kind == PartKind::EXTENDED)
		{
			if (ext)
			{
				error = "Only one extended partition is allowed";
				return false;
			}
			if (! MbrType::is_extended(p.mbr_type))
			{
				error = "Extended partition " + num(p.num) + " has a non extended partition type";
				return false;
			}
			ext = &p;
		}
		else if (MbrType::is_extended(p.mbr_type))
		{
			error = "Primary partition " + num(p.num) + " has an extended partition type";
			return false;
		}
		tops.push_back(&p);
	}
	for (size_t i = 0; i < tops.size(); i++)
		for (size_t j = i + 1; j < tops.size(); j++)
			if (overlaps(*tops[i], *tops[j]))
			{
				error = "Partitions " + num(tops[i]->num) + " and " + num(tops[j]->num) + " overlap";
				return false;
			}

	if (! logicals.empty() && ! ext)
	{
		error = "Logical partitions require an extended partition";
		return false;
	}
	std::sort(logicals.begin(), logicals.end(),
	          [](const PartRecord* a, const PartRecord* b) { return a->start < b->start; });
	int64_t prev_end = -1;
	for (size_t i = 0; i < logicals.size(); i++)
	{
		const PartRecord& p = *logicals[i];
		if (p.num != static_cast<int>(i) + 5)
		{
			error = "Logical partitions are not numbered consecutively from 5";
			return false;
		}
		// Room for the EBR before every logical partition.
		int64_t ebr = (i == 0) ? ext->start
		                       : (p.ebr_sector >= 0 ? p.ebr_sector : prev_end + 1);
		if (p.start <= ext->start || p.end > ext->end)
		{
			error = "Logical partition " + num(p.num) + " is not inside the extended partition";
			return false;
		}
		if (ebr >= p.start || ebr <= prev_end || ebr < ext->start)
		{
			error = "No space for the EBR before logical partition " + num(p.num);
			return false;
		}
		if (p.start - ebr > MAX32 || ebr - ext->start > MAX32)
		{
			error = "Logical partition " + num(p.num) + " is too far from its EBR";
			return false;
		}
		prev_end = p.end;
	}
	return true;
}


// The EBR sector actually used for each logical (sorted by start) when writing.
static std::vector<int64_t> ebr_positions(const PartRecord& ext, const std::vector<const PartRecord*>& logicals)
{
	std::vector<int64_t> out;
	int64_t prev_end = -1;
	for (size_t i = 0; i < logicals.size(); i++)
	{
		const PartRecord& p = *logicals[i];
		int64_t ebr;
		if (i == 0)
			ebr = ext.start;
		else if (p.ebr_sector > prev_end && p.ebr_sector < p.start)
			ebr = p.ebr_sector;
		else
			ebr = prev_end + 1;
		out.push_back(ebr);
		prev_end = p.end;
	}
	return out;
}


bool model_to_layout(const DiskModel& m, DiskLayout& out, std::string& error)
{
	if (! validate_model(m, error))
		return false;

	out = DiskLayout();
	out.style = m.style;
	const uint64_t ss = m.sector_size;

	if (m.style == TableStyle::RAW)
		return true;

	if (m.style == TableStyle::GPT)
	{
		out.gpt_disk_id             = m.gpt_disk_id;
		out.gpt_first_usable_offset = uint64_t(m.first_usable) * ss;
		out.gpt_usable_length       = uint64_t(m.last_usable - m.first_usable + 1) * ss;
		out.gpt_max_entries         = m.gpt_max_entries;
		std::vector<const PartRecord*> sorted;
		for (const PartRecord& p : m.parts)
			sorted.push_back(&p);
		std::sort(sorted.begin(), sorted.end(),
		          [](const PartRecord* a, const PartRecord* b) { return a->num < b->num; });
		for (const PartRecord* p : sorted)
		{
			LayoutEntry e;
			e.offset         = uint64_t(p->start) * ss;
			e.length         = uint64_t(p->length()) * ss;
			e.number         = static_cast<uint32_t>(p->num);
			e.rewrite        = true;
			e.gpt_type       = p->type_guid;
			e.gpt_id         = p->unique_guid;
			e.gpt_attributes = p->attributes;
			e.gpt_name       = p->name;
			out.entries.push_back(e);
		}
		return true;
	}

	// MBR: four primary slots then one group of four entries per logical partition.
	out.mbr_signature = m.mbr_signature;
	out.entries.resize(4);
	for (LayoutEntry& e : out.entries)
		e.rewrite = true;

	const PartRecord* ext = nullptr;
	std::vector<const PartRecord*> logicals;
	for (const PartRecord& p : m.parts)
	{
		if (p.kind == PartKind::LOGICAL)
		{
			logicals.push_back(&p);
			continue;
		}
		LayoutEntry& e = out.entries[p.num - 1];
		e.offset             = uint64_t(p.start) * ss;
		e.length             = uint64_t(p.length()) * ss;
		e.number             = (p.kind == PartKind::EXTENDED) ? 0 : static_cast<uint32_t>(p.num);
		e.mbr_type           = p.mbr_type;
		e.mbr_boot           = p.mbr_boot;
		e.mbr_recognized     = true;
		e.mbr_hidden_sectors = static_cast<uint32_t>(p.start);
		if (p.kind == PartKind::EXTENDED)
			ext = &p;
	}
	std::sort(logicals.begin(), logicals.end(),
	          [](const PartRecord* a, const PartRecord* b) { return a->start < b->start; });
	if (ext && ! logicals.empty())
	{
		std::vector<int64_t> ebrs = ebr_positions(*ext, logicals);
		for (size_t i = 0; i < logicals.size(); i++)
		{
			const PartRecord& p = *logicals[i];
			LayoutEntry log;
			log.offset             = uint64_t(p.start) * ss;
			log.length             = uint64_t(p.length()) * ss;
			log.number             = static_cast<uint32_t>(p.num);
			log.rewrite            = true;
			log.mbr_type           = p.mbr_type;
			log.mbr_boot           = p.mbr_boot;
			log.mbr_recognized     = true;
			log.mbr_hidden_sectors = static_cast<uint32_t>(p.start - ebrs[i]);

			LayoutEntry link;
			link.rewrite = true;
			if (i + 1 < logicals.size())
			{
				const PartRecord& next = *logicals[i + 1];
				link.offset             = uint64_t(ebrs[i + 1]) * ss;
				link.length             = uint64_t(next.end - ebrs[i + 1] + 1) * ss;
				link.mbr_type           = MbrType::EXTENDED;
				link.mbr_recognized     = true;
				link.mbr_hidden_sectors = static_cast<uint32_t>(ebrs[i + 1] - ext->start);
			}
			LayoutEntry empty;
			empty.rewrite = true;
			out.entries.push_back(log);
			out.entries.push_back(link);
			out.entries.push_back(empty);
			out.entries.push_back(empty);
		}
	}
	return true;
}


static bool records_equivalent(const PartRecord& a, const PartRecord& b, TableStyle style,
                               bool strict_ebr, std::string& difference)
{
	if (a.kind != b.kind || a.start != b.start || a.end != b.end)
	{
		difference = "partition boundaries differ (" + num(a.start) + "-" + num(a.end) + " vs " +
		             num(b.start) + "-" + num(b.end) + ")";
		return false;
	}
	if (style == TableStyle::MBR)
	{
		if (a.mbr_type != b.mbr_type || a.mbr_boot != b.mbr_boot)
		{
			difference = "MBR partition type or boot flag differs at sector " + num(a.start);
			return false;
		}
		if (a.kind == PartKind::LOGICAL && strict_ebr && a.ebr_sector != b.ebr_sector)
		{
			difference = "EBR position differs for logical partition at sector " + num(a.start);
			return false;
		}
		return true;
	}
	if (a.type_guid != b.type_guid || a.unique_guid != b.unique_guid ||
	    a.attributes != b.attributes || a.name != b.name)
	{
		difference = "GPT partition type, GUID, attributes or name differs at sector " + num(a.start);
		return false;
	}
	return true;
}


bool models_equivalent(const DiskModel& a, const DiskModel& b, bool strict_ebr, std::string& difference)
{
	if (a.style != b.style || a.superfloppy != b.superfloppy)
	{
		difference = "partition table type differs";
		return false;
	}
	if (a.style == TableStyle::MBR && a.mbr_signature != b.mbr_signature)
	{
		difference = "MBR disk signature differs";
		return false;
	}
	if (a.style == TableStyle::GPT && a.gpt_disk_id != b.gpt_disk_id)
	{
		difference = "GPT disk GUID differs";
		return false;
	}
	if (a.parts.size() != b.parts.size())
	{
		difference = "number of partitions differs (" + num(a.parts.size()) + " vs " + num(b.parts.size()) + ")";
		return false;
	}
	// Both are sorted the same way (sort_parts()).
	for (size_t i = 0; i < a.parts.size(); i++)
		if (! records_equivalent(a.parts[i], b.parts[i], a.style, strict_ebr, difference))
			return false;
	return true;
}


bool layouts_equivalent(const DiskLayout& a, const DiskLayout& b, uint32_t sector_size,
                        std::string& difference)
{
	// Total sectors is only used for validation, which is not run here.
	const int64_t huge = INT64_MAX / 2;
	DiskModel ma, mb;
	std::string error;
	if (! layout_to_model(a, sector_size, huge, nullptr, ma, error) ||
	    ! layout_to_model(b, sector_size, huge, nullptr, mb, error)    )
	{
		difference = error;
		return false;
	}
	return models_equivalent(ma, mb, true, difference);
}


std::vector<Extent> changed_extents(const DiskLayout& before, const DiskLayout& after)
{
	std::vector<Extent> out;
	bool same_style = before.style == after.style;
	for (const LayoutEntry& b : before.entries)
	{
		if (b.length == 0)
			continue;
		if (before.style == TableStyle::MBR && (b.mbr_type == MbrType::EMPTY || MbrType::is_extended(b.mbr_type)))
			continue;  // Unused slots and extended containers / EBR links hold no volume
		if (before.style == TableStyle::GPT && b.gpt_type.is_null())
			continue;

		bool kept = false;
		if (same_style)
		{
			for (const LayoutEntry& a : after.entries)
			{
				if (a.offset != b.offset || a.length != b.length)
					continue;
				if (before.style == TableStyle::MBR && a.mbr_type == b.mbr_type)
					kept = true;
				if (before.style == TableStyle::GPT && a.gpt_type == b.gpt_type && a.gpt_id == b.gpt_id)
					kept = true;
			}
		}
		if (! kept)
		{
			Extent ex;
			ex.offset = b.offset;
			ex.length = b.length;
			out.push_back(ex);
		}
	}
	return out;
}


std::string unsupported_reason(const DiskModel& model, const RawTable* raw)
{
	if (raw && raw->hybrid_mbr)
		return "The disk has a hybrid MBR (GPT plus MBR partition entries).  WParted will not "
		       "modify it because Windows would replace the hybrid MBR with a protective MBR.";

	for (const PartRecord& p : model.parts)
	{
		if (model.style == TableStyle::MBR && p.mbr_type == MbrType::LDM)
			return "This is a Windows dynamic disk.  Use Windows Disk Management to change it.";
		if (model.style == TableStyle::GPT &&
		    (p.type_guid == TypeGuid::LDM_METADATA || p.type_guid == TypeGuid::LDM_DATA))
			return "This is a Windows dynamic disk.  Use Windows Disk Management to change it.";
		if (model.style == TableStyle::GPT && p.type_guid == TypeGuid::STORAGE_SPACES)
			return "This disk is part of a Storage Spaces pool.  Use Windows Storage Spaces to change it.";
	}
	return "";
}


// ---------------------------------------------------------------------------
// libparted compatible flags
// ---------------------------------------------------------------------------

static uint32_t bit(int flag)
{
	return 1U << flag;
}


static bool fs_is(const char* fs, const char* prefix_or_name, bool prefix = false)
{
	if (! fs)
		return false;
	if (prefix)
		return std::strncmp(fs, prefix_or_name, std::strlen(prefix_or_name)) == 0;
	return std::strcmp(fs, prefix_or_name) == 0;
}


// --- GPT -------------------------------------------------------------------

struct GptTypeFlag
{
	int  flag;
	const Guid* guid;
};

static const GptTypeFlag gpt_type_flags[] = {
	{ PED_PARTITION_BOOT,              &TypeGuid::ESP },
	{ PED_PARTITION_ESP,               &TypeGuid::ESP },
	{ PED_PARTITION_BIOS_GRUB,         &TypeGuid::BIOS_GRUB },
	{ PED_PARTITION_RAID,              &TypeGuid::LINUX_RAID },
	{ PED_PARTITION_LVM,               &TypeGuid::LINUX_LVM },
	{ PED_PARTITION_SWAP,              &TypeGuid::LINUX_SWAP },
	{ PED_PARTITION_HPSERVICE,         &TypeGuid::HP_SERVICE },
	{ PED_PARTITION_MSFT_RESERVED,     &TypeGuid::MSFT_RESERVED },
	{ PED_PARTITION_MSFT_DATA,         &TypeGuid::BASIC_DATA },
	{ PED_PARTITION_DIAG,              &TypeGuid::MSFT_RECOVERY },
	{ PED_PARTITION_APPLE_TV_RECOVERY, &TypeGuid::APPLE_TV_RECOVERY },
	{ PED_PARTITION_PREP,              &TypeGuid::PREP },
	{ PED_PARTITION_IRST,              &TypeGuid::IRST },
	{ PED_PARTITION_CHROMEOS_KERNEL,   &TypeGuid::CHROMEOS_KERNEL },
	{ PED_PARTITION_BLS_BOOT,          &TypeGuid::BLS_BOOT },
	{ PED_PARTITION_LINUX_HOME,        &TypeGuid::LINUX_HOME },
};


static const Guid* gpt_flag_guid(int flag)
{
	for (const GptTypeFlag& f : gpt_type_flags)
		if (f.flag == flag)
			return f.guid;
	return nullptr;
}


static uint64_t gpt_attr_flag(int flag)
{
	switch (flag)
	{
		case PED_PARTITION_LEGACY_BOOT:  return GPT_ATTR_LEGACY_BIOS_BOOTABLE;
		case PED_PARTITION_HIDDEN:       return GPT_BASIC_DATA_ATTR_HIDDEN;
		case PED_PARTITION_NO_AUTOMOUNT: return GPT_BASIC_DATA_ATTR_NO_DRIVE_LETTER;
		default:                         return 0;
	}
}


static void gpt_set_system(PartRecord& part, const char* fs)
{
	// Type flags take precedence, as in libparted gpt_partition_set_system().
	for (const GptTypeFlag& f : gpt_type_flags)
	{
		if (part.type_flags & bit(f.flag))
		{
			part.type_guid = *f.guid;
			return;
		}
	}

	bool fs_known = fs && *fs;
	if (fs_known)
	{
		if (fs_is(fs, "fat", true) || fs_is(fs, "ntfs") || fs_is(fs, "exfat") ||
		    fs_is(fs, "udf") || fs_is(fs, "refs"))
		{
			part.type_guid = TypeGuid::BASIC_DATA;
			return;
		}
		if (fs_is(fs, "hfs", true))
		{
			part.type_guid = TypeGuid::APPLE_HFS;
			return;
		}
		if (std::strstr(fs, "swap"))
		{
			part.type_guid = TypeGuid::LINUX_SWAP;
			return;
		}
		part.type_guid = TypeGuid::LINUX_DATA;
		return;
	}

	// Unknown contents.  Deviation from libparted for safety: never turn an existing
	// Windows or foreign partition into a Linux one just because its contents could
	// not be identified.  Only partitions which had a flag type (now cleared) or no
	// type at all get the libparted default.
	if (part.type_guid.is_null())
	{
		part.type_guid = TypeGuid::LINUX_DATA;
		return;
	}
	for (const GptTypeFlag& f : gpt_type_flags)
	{
		if (part.type_guid == *f.guid && part.type_guid != TypeGuid::BASIC_DATA)
		{
			part.type_guid = TypeGuid::LINUX_DATA;
			return;
		}
	}
	// Keep current type (BASIC_DATA or any type unknown to WParted).
}


// --- MBR -------------------------------------------------------------------

static const int dos_type_flags[] = {
	PED_PARTITION_RAID, PED_PARTITION_LVM, PED_PARTITION_SWAP, PED_PARTITION_PALO,
	PED_PARTITION_PREP, PED_PARTITION_IRST, PED_PARTITION_ESP, PED_PARTITION_BLS_BOOT,
	PED_PARTITION_DIAG
};


static bool dos_is_hidden_type(uint8_t t)
{
	return t == 0x11 || t == 0x14 || t == 0x16 || t == 0x17 || t == 0x1B || t == 0x1C || t == 0x1E;
}


static bool dos_is_lba_type(uint8_t t)
{
	return t == MbrType::FAT32_LBA || t == MbrType::FAT16_LBA || t == MbrType::EXTENDED_LBA ||
	       t == 0x1C || t == 0x1E;
}


// Name of the file system implied by an MBR type when the contents are unknown.
static const char* dos_fs_from_type(uint8_t t)
{
	uint8_t base = dos_is_hidden_type(t) ? static_cast<uint8_t>(t & ~MbrType::HIDDEN_BIT) : t;
	switch (base)
	{
		case MbrType::NTFS:        return "ntfs";
		case MbrType::FAT32:
		case MbrType::FAT32_LBA:   return "fat32";
		case MbrType::FAT12:
		case MbrType::FAT16_SMALL:
		case MbrType::FAT16:
		case MbrType::FAT16_LBA:   return "fat16";
		case MbrType::APPLE_HFS:   return "hfs+";
		case MbrType::LINUX_SWAP:  return "linux-swap";
		default:                   return nullptr;
	}
}


static void dos_set_system(const PartRecord& before, PartRecord& part, const char* fs_in)
{
	uint32_t f = part.type_flags;
	const char* fs = (fs_in && *fs_in) ? fs_in : dos_fs_from_type(before.mbr_type);
	bool hidden = (f & bit(PED_PARTITION_HIDDEN)) != 0;
	bool lba    = (f & bit(PED_PARTITION_LBA)) != 0;

	if (hidden && ! (fs_is(fs, "fat", true) || fs_is(fs, "ntfs")))
	{
		hidden = false;
		part.type_flags &= ~bit(PED_PARTITION_HIDDEN);
	}

	if (part.kind == PartKind::EXTENDED)
	{
		part.type_flags &= bit(PED_PARTITION_LBA);
		part.mbr_type = lba ? MbrType::EXTENDED_LBA : MbrType::EXTENDED;
		return;
	}

	if (f & bit(PED_PARTITION_DIAG))
	{
		// Keep an existing diagnostics / recovery type.  Otherwise use the Windows
		// Recovery type for NTFS (WinRE) and the Compaq diagnostics type as
		// libparted does for anything else.
		if (before.mbr_type == MbrType::COMPAQ_DIAG || before.mbr_type == MbrType::MSFT_RECOVERY ||
		    before.mbr_type == MbrType::DELL_DIAG)
			part.mbr_type = before.mbr_type;
		else
			part.mbr_type = fs_is(fs, "ntfs") ? MbrType::MSFT_RECOVERY : MbrType::COMPAQ_DIAG;
		return;
	}
	if (f & bit(PED_PARTITION_LVM))      { part.mbr_type = MbrType::LINUX_LVM;  return; }
	if (f & bit(PED_PARTITION_SWAP))     { part.mbr_type = MbrType::LINUX_SWAP; return; }
	if (f & bit(PED_PARTITION_RAID))     { part.mbr_type = MbrType::LINUX_RAID; return; }
	if (f & bit(PED_PARTITION_PALO))     { part.mbr_type = MbrType::PALO;       return; }
	if (f & bit(PED_PARTITION_PREP))     { part.mbr_type = MbrType::PREP;       return; }
	if (f & bit(PED_PARTITION_IRST))     { part.mbr_type = MbrType::IRST;       return; }
	if (f & bit(PED_PARTITION_ESP))      { part.mbr_type = MbrType::ESP;        return; }
	if (f & bit(PED_PARTITION_BLS_BOOT)) { part.mbr_type = MbrType::BLS_BOOT;   return; }

	uint8_t hidden_bit = hidden ? MbrType::HIDDEN_BIT : 0;
	uint8_t current_base = dos_is_hidden_type(before.mbr_type)
	                       ? static_cast<uint8_t>(before.mbr_type & ~MbrType::HIDDEN_BIT)
	                       : before.mbr_type;
	bool current_lba = dos_is_lba_type(before.mbr_type);

	if (fs_is(fs, "fat16"))
	{
		// Keep FAT12 and small FAT16 ids when nothing relevant changed.
		if ((current_base == MbrType::FAT12 || current_base == MbrType::FAT16_SMALL ||
		     current_base == MbrType::FAT16 || current_base == MbrType::FAT16_LBA) &&
		    current_lba == lba)
			part.mbr_type = static_cast<uint8_t>(current_base | hidden_bit);
		else
			part.mbr_type = static_cast<uint8_t>((lba ? MbrType::FAT16_LBA : MbrType::FAT16) | hidden_bit);
	}
	else if (fs_is(fs, "fat32"))
	{
		part.mbr_type = static_cast<uint8_t>((lba ? MbrType::FAT32_LBA : MbrType::FAT32) | hidden_bit);
	}
	else if (fs_is(fs, "ntfs") || fs_is(fs, "hpfs") || fs_is(fs, "exfat") || fs_is(fs, "udf") ||
	         fs_is(fs, "refs"))
	{
		part.mbr_type = static_cast<uint8_t>(MbrType::NTFS | hidden_bit);
	}
	else if (fs_is(fs, "hfs", true))
	{
		part.mbr_type = MbrType::APPLE_HFS;
	}
	else if (fs && std::strstr(fs, "swap"))
	{
		part.mbr_type = MbrType::LINUX_SWAP;
	}
	else if (fs)
	{
		part.mbr_type = MbrType::LINUX;
	}
	else
	{
		// Unknown contents: keep a type unknown to the flag logic (e.g. 0x42, 0xA5),
		// default to Linux when the type was a (now cleared) flag type or unused.
		bool was_flag_type = before.mbr_type == MbrType::EMPTY ||
		                     before.mbr_type == MbrType::LINUX_LVM || before.mbr_type == MbrType::LINUX_LVM_OLD ||
		                     before.mbr_type == MbrType::LINUX_SWAP || before.mbr_type == MbrType::LINUX_RAID ||
		                     before.mbr_type == MbrType::PALO || before.mbr_type == MbrType::PREP ||
		                     before.mbr_type == MbrType::IRST || before.mbr_type == MbrType::ESP ||
		                     before.mbr_type == MbrType::BLS_BOOT || before.mbr_type == MbrType::COMPAQ_DIAG ||
		                     before.mbr_type == MbrType::MSFT_RECOVERY || before.mbr_type == MbrType::DELL_DIAG;
		part.mbr_type = was_flag_type ? MbrType::LINUX : before.mbr_type;
	}
}


void derive_type_flags(TableStyle style, PartRecord& part)
{
	part.type_flags = 0;
	if (style == TableStyle::GPT)
	{
		for (const GptTypeFlag& f : gpt_type_flags)
		{
			// "boot" and "esp" are aliases on GPT; only record esp so clearing
			// either one clears the type.
			if (f.flag == PED_PARTITION_BOOT)
				continue;
			if (part.type_guid == *f.guid)
				part.type_flags |= bit(f.flag);
		}
		return;
	}
	if (style != TableStyle::MBR)
		return;

	uint8_t t = part.mbr_type;
	if (dos_is_lba_type(t))
		part.type_flags |= bit(PED_PARTITION_LBA);
	if (part.kind == PartKind::EXTENDED)
		return;
	if (dos_is_hidden_type(t))
		part.type_flags |= bit(PED_PARTITION_HIDDEN);
	if (t == MbrType::LINUX_RAID)                                    part.type_flags |= bit(PED_PARTITION_RAID);
	if (t == MbrType::LINUX_LVM || t == MbrType::LINUX_LVM_OLD)      part.type_flags |= bit(PED_PARTITION_LVM);
	if (t == MbrType::LINUX_SWAP)                                    part.type_flags |= bit(PED_PARTITION_SWAP);
	if (t == MbrType::PALO)                                          part.type_flags |= bit(PED_PARTITION_PALO);
	if (t == MbrType::PREP)                                          part.type_flags |= bit(PED_PARTITION_PREP);
	if (t == MbrType::IRST)                                          part.type_flags |= bit(PED_PARTITION_IRST);
	if (t == MbrType::ESP)                                           part.type_flags |= bit(PED_PARTITION_ESP);
	if (t == MbrType::BLS_BOOT)                                      part.type_flags |= bit(PED_PARTITION_BLS_BOOT);
	if (t == MbrType::COMPAQ_DIAG || t == MbrType::MSFT_RECOVERY ||
	    t == MbrType::DELL_DIAG)                                     part.type_flags |= bit(PED_PARTITION_DIAG);
}


bool flag_is_available(const DiskModel& model, const PartRecord& part, int flag)
{
	if (model.style == TableStyle::GPT)
		return gpt_flag_guid(flag) != nullptr || gpt_attr_flag(flag) != 0;

	if (model.style != TableStyle::MBR)
		return false;

	switch (flag)
	{
		case PED_PARTITION_HIDDEN:
			return part.kind != PartKind::EXTENDED;
		case PED_PARTITION_LBA:
			return true;
		case PED_PARTITION_BOOT:
		case PED_PARTITION_RAID:
		case PED_PARTITION_LVM:
		case PED_PARTITION_SWAP:
		case PED_PARTITION_PALO:
		case PED_PARTITION_PREP:
		case PED_PARTITION_IRST:
		case PED_PARTITION_ESP:
		case PED_PARTITION_BLS_BOOT:
		case PED_PARTITION_DIAG:
			return part.kind != PartKind::EXTENDED;
		default:
			return false;
	}
}


bool flag_get(const DiskModel& model, const PartRecord& part, int flag)
{
	if (! flag_is_available(model, part, flag))
		return false;

	if (model.style == TableStyle::GPT)
	{
		uint64_t attr = gpt_attr_flag(flag);
		if (attr)
			return (part.attributes & attr) != 0;
		if (flag == PED_PARTITION_BOOT)
			flag = PED_PARTITION_ESP;
		return (part.type_flags & bit(flag)) != 0;
	}

	if (flag == PED_PARTITION_BOOT)
		return part.mbr_boot;
	return (part.type_flags & bit(flag)) != 0;
}


bool flag_set(DiskModel& model, PartRecord& part, int flag, bool state, const char* fs_name)
{
	if (! flag_is_available(model, part, flag))
		return false;

	PartRecord before = part;

	if (model.style == TableStyle::GPT)
	{
		uint64_t attr = gpt_attr_flag(flag);
		if (attr)
		{
			if (state)
				part.attributes |= attr;
			else
				part.attributes &= ~attr;
			return true;
		}
		if (flag == PED_PARTITION_BOOT)
			flag = PED_PARTITION_ESP;
		if (state)
			part.type_flags = bit(flag);  // Type flags are mutually exclusive
		else
			part.type_flags &= ~bit(flag);
		gpt_set_system(part, fs_name);
		return true;
	}

	// MBR
	if (flag == PED_PARTITION_BOOT)
	{
		part.mbr_boot = state;
		if (state)
			for (PartRecord& other : model.parts)
				if (other.num != part.num)
					other.mbr_boot = false;
		return true;
	}
	if (flag == PED_PARTITION_HIDDEN || flag == PED_PARTITION_LBA)
	{
		if (state)
			part.type_flags |= bit(flag);
		else
			part.type_flags &= ~bit(flag);
	}
	else
	{
		if (state)
		{
			for (int f : dos_type_flags)
				part.type_flags &= ~bit(f);
			part.type_flags |= bit(flag);
		}
		else
		{
			part.type_flags &= ~bit(flag);
		}
	}
	dos_set_system(before, part, fs_name);
	return true;
}


void set_system(const DiskModel& model, PartRecord& part, const char* fs_name)
{
	PartRecord before = part;
	if (model.style == TableStyle::GPT)
		gpt_set_system(part, fs_name);
	else if (model.style == TableStyle::MBR)
		dos_set_system(before, part, fs_name);
}


}  // namespace WinDisk
}  // namespace WParted
