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

#include "MemDiskBackend.h"

#include <algorithm>
#include <cstring>


namespace WParted
{
namespace TestSupport
{

using WinDisk::DiskLayout;
using WinDisk::LayoutEntry;
using WinDisk::TableStyle;


namespace
{

void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF; }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | (uint64_t(get32(p + 4)) << 32); }

bool is_ext(uint8_t t) { return t == 0x05 || t == 0x0F || t == 0x85; }

void put_mbr_entry(uint8_t* e, bool boot, uint8_t type, uint32_t lba, uint32_t sectors)
{
	std::memset(e, 0, 16);
	if (type == 0)
		return;
	e[0] = boot ? 0x80 : 0x00;
	e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
	e[4] = type;
	e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
	put32(e + 8, lba);
	put32(e + 12, sectors);
}

}  // anonymous namespace


void MemDisk::read(uint64_t lba, uint8_t* out) const
{
	auto it = sectors.find(lba);
	if (it == sectors.end())
		std::memset(out, 0, ss());
	else
		std::memcpy(out, it->second.data(), ss());
}


void MemDisk::write(uint64_t lba, const uint8_t* in)
{
	std::memcpy(sector(lba), in, ss());
}


uint8_t* MemDisk::sector(uint64_t lba)
{
	std::vector<uint8_t>& s = sectors[lba];
	if (s.size() != ss())
		s.assign(ss(), 0);
	return s.data();
}


void MemDisk::zero(uint64_t lba, uint64_t count)
{
	for (uint64_t i = lba; i < lba + count; i++)
		sectors.erase(i);
}


MemDisk& MemDiskBackend::add_disk(int index, uint64_t sectors, uint32_t sector_size, const std::string& serial)
{
	MemDisk& d = m_disks[index];
	d = MemDisk();
	d.info.index = index;
	d.info.model = "Test Disk";
	d.info.vendor = "WParted";
	d.info.serial = serial;
	d.info.bus = "Virtual";
	d.info.instance_id = "TEST\\DISK\\" + std::to_string(index) + "&" + serial;
	d.info.logical_sector_size = sector_size;
	d.info.physical_sector_size = sector_size;
	d.info.size_bytes = sectors * sector_size;
	d.total_sectors = sectors;
	return d;
}


std::vector<int> MemDiskBackend::enumerate_disks()
{
	std::vector<int> out;
	for (const auto& kv : m_disks)
		out.push_back(kv.first);
	return out;
}


bool MemDiskBackend::query_disk(int index, WinDisk::DiskInfo& info, std::string& error)
{
	auto it = m_disks.find(index);
	if (it == m_disks.end())
	{
		error = "No such disk";
		return false;
	}
	info = it->second.info;
	return true;
}


bool MemDiskBackend::read_layout(int index, DiskLayout& layout, std::string& error)
{
	auto it = m_disks.find(index);
	if (it == m_disks.end())
	{
		error = "No such disk";
		return false;
	}
	return decode(it->second, layout, error);
}


bool MemDiskBackend::read_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
                                  void* buffer, std::string& error)
{
	auto it = m_disks.find(index);
	if (it == m_disks.end() || sector_size != it->second.ss() || lba + count > it->second.total_sectors)
	{
		error = "bad read";
		return false;
	}
	uint8_t* out = static_cast<uint8_t*>(buffer);
	for (uint64_t i = 0; i < count; i++)
		it->second.read(lba + i, out + i * sector_size);
	return true;
}


bool MemDiskBackend::write_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
                                   const void* buffer, std::string& error)
{
	auto it = m_disks.find(index);
	if (it == m_disks.end() || sector_size != it->second.ss() || lba + count > it->second.total_sectors)
	{
		error = "bad write";
		return false;
	}
	const uint8_t* in = static_cast<const uint8_t*>(buffer);
	for (uint64_t i = 0; i < count; i++)
		it->second.write(lba + i, in + i * sector_size);
	return true;
}


bool MemDiskBackend::write_layout(int index, const DiskLayout& current, const DiskLayout& layout,
                                  bool new_table, std::string& error)
{
	auto it = m_disks.find(index);
	if (it == m_disks.end())
	{
		error = "No such disk";
		return false;
	}
	MemDisk& d = it->second;
	(void)current;

	DiskLayout to_write = layout;
	if (d.corrupt_next_write)
	{
		d.corrupt_next_write = false;
		for (auto e = to_write.entries.rbegin(); e != to_write.entries.rend(); ++e)
		{
			if (e->length > 0)
			{
				*e = LayoutEntry();
				break;
			}
		}
	}
	d.layout_writes++;
	return encode(d, to_write, new_table, error);
}


bool MemDiskBackend::update_properties(int index, std::string& error)
{
	if (! m_disks.count(index))
	{
		error = "No such disk";
		return false;
	}
	return true;
}


bool MemDiskBackend::encode(MemDisk& d, const DiskLayout& layout, bool new_table, std::string& error)
{
	const uint32_t ss = d.ss();
	const uint64_t total = d.total_sectors;
	const uint64_t entry_sectors = (128 * 128 + ss - 1) / ss;

	// Windows like validation: no overlaps between data partitions.
	std::vector<std::pair<uint64_t, uint64_t>> ranges;
	for (const LayoutEntry& e : layout.entries)
	{
		if (e.length == 0)
			continue;
		if (layout.style == TableStyle::MBR && (e.mbr_type == 0 || is_ext(e.mbr_type)))
			continue;
		if (e.offset % ss || e.length % ss || e.offset + e.length > total * ss)
		{
			error = "invalid entry";
			return false;
		}
		ranges.push_back({e.offset, e.offset + e.length});
	}
	std::sort(ranges.begin(), ranges.end());
	for (size_t i = 1; i < ranges.size(); i++)
		if (ranges[i].first < ranges[i - 1].second)
		{
			error = "overlap";
			return false;
		}
	if (layout.style == TableStyle::MBR && layout.entries.size() % 4 != 0)
	{
		error = "MBR entry count not a multiple of 4";
		return false;
	}

	if (new_table)
	{
		d.zero(0, 2 + entry_sectors);
		d.zero(total - 1 - entry_sectors, 1 + entry_sectors);
	}

	if (layout.style == TableStyle::MBR)
	{
		uint8_t* mbr = d.sector(0);
		std::memset(mbr + 440, 0, 72);
		put32(mbr + 440, layout.mbr_signature);
		mbr[510] = 0x55;
		mbr[511] = 0xAA;
		const LayoutEntry* ext = nullptr;
		for (int i = 0; i < 4; i++)
		{
			const LayoutEntry* e = (i < static_cast<int>(layout.entries.size())) ? &layout.entries[i] : nullptr;
			if (! e || e->length == 0 || e->mbr_type == 0)
			{
				put_mbr_entry(mbr + 446 + 16 * i, false, 0, 0, 0);
				continue;
			}
			put_mbr_entry(mbr + 446 + 16 * i, e->mbr_boot, e->mbr_type,
			              static_cast<uint32_t>(e->offset / ss), static_cast<uint32_t>(e->length / ss));
			if (is_ext(e->mbr_type))
				ext = e;
		}
		if (ext)
		{
			uint64_t ext_start = ext->offset / ss;
			uint64_t ebr = ext_start;
			size_t groups = (layout.entries.size() - 4) / 4;
			if (groups == 0)
			{
				uint8_t* s = d.sector(ebr);
				std::memset(s, 0, ss);
				s[510] = 0x55;
				s[511] = 0xAA;
			}
			for (size_t g = 0; g < groups; g++)
			{
				const LayoutEntry* logical = nullptr;
				const LayoutEntry* link = nullptr;
				for (size_t k = 4 + 4 * g; k < 8 + 4 * g; k++)
				{
					const LayoutEntry& e = layout.entries[k];
					if (e.length == 0 || e.mbr_type == 0)
						continue;
					(is_ext(e.mbr_type) ? link : logical) = &e;
				}
				uint8_t* s = d.sector(ebr);
				std::memset(s, 0, ss);
				s[510] = 0x55;
				s[511] = 0xAA;
				if (logical)
					put_mbr_entry(s + 446, logical->mbr_boot, logical->mbr_type,
					              static_cast<uint32_t>(logical->offset / ss - ebr),
					              static_cast<uint32_t>(logical->length / ss));
				if (! link)
					break;
				put_mbr_entry(s + 446 + 16, false, 0x05,
				              static_cast<uint32_t>(link->offset / ss - ext_start),
				              static_cast<uint32_t>(link->length / ss));
				ebr = link->offset / ss;
			}
		}
		return true;
	}

	if (layout.style != TableStyle::GPT)
	{
		error = "unsupported style";
		return false;
	}

	uint64_t first = layout.gpt_first_usable_offset ? layout.gpt_first_usable_offset / ss : 2 + entry_sectors;
	uint64_t last  = layout.gpt_usable_length ? first + layout.gpt_usable_length / ss - 1 : total - 2 - entry_sectors;

	// Protective MBR
	uint8_t* pmbr = d.sector(0);
	std::memset(pmbr, 0, ss);
	put_mbr_entry(pmbr + 446, false, 0xEE, 1, static_cast<uint32_t>(std::min<uint64_t>(total - 1, 0xFFFFFFFFULL)));
	pmbr[510] = 0x55;
	pmbr[511] = 0xAA;

	std::vector<uint8_t> array(entry_sectors * ss, 0);
	size_t slot = 0;
	for (const LayoutEntry& e : layout.entries)
	{
		if (e.length == 0 || e.gpt_type.is_null())
			continue;
		uint8_t* p = &array[slot * 128];
		e.gpt_type.to_bytes_le(p);
		e.gpt_id.to_bytes_le(p + 16);
		put64(p + 32, e.offset / ss);
		put64(p + 40, (e.offset + e.length) / ss - 1);
		put64(p + 48, e.gpt_attributes);
		for (size_t k = 0; k < e.gpt_name.size() && k < 36; k++)
			put16(p + 56 + 2 * k, static_cast<uint16_t>(e.gpt_name[k]));
		slot++;
	}
	uint32_t array_crc = WinDisk::crc32(array.data(), 128 * 128);

	auto write_header = [&](uint64_t my_lba, uint64_t alt_lba, uint64_t entries_lba)
	{
		uint8_t* h = d.sector(my_lba);
		std::memset(h, 0, ss);
		std::memcpy(h, "EFI PART", 8);
		put32(h + 8, 0x00010000);
		put32(h + 12, 92);
		put64(h + 24, my_lba);
		put64(h + 32, alt_lba);
		put64(h + 40, first);
		put64(h + 48, last);
		layout.gpt_disk_id.to_bytes_le(h + 56);
		put64(h + 72, entries_lba);
		put32(h + 80, 128);
		put32(h + 84, 128);
		put32(h + 88, array_crc);
		put32(h + 16, WinDisk::crc32(h, 92));
		for (uint64_t i = 0; i < entry_sectors; i++)
			d.write(entries_lba + i, &array[i * ss]);
	};
	write_header(1, total - 1, 2);
	write_header(total - 1, 1, total - 1 - entry_sectors);
	return true;
}


bool MemDiskBackend::decode(const MemDisk& d, DiskLayout& layout, std::string& error)
{
	(void)error;
	layout = DiskLayout();
	const uint32_t ss = d.ss();
	std::vector<uint8_t> s0(ss);
	d.read(0, s0.data());
	const uint8_t* img = s0.data();
	if (img[510] != 0x55 || img[511] != 0xAA)
	{
		layout.style = TableStyle::RAW;
		return true;
	}
	if (std::memcmp(img + 3, "NTFS    ", 8) == 0 || std::memcmp(img + 3, "EXFAT   ", 8) == 0 ||
	    std::memcmp(img + 0x52, "FAT32", 5) == 0 || std::memcmp(img + 0x36, "FAT1", 4) == 0)
	{
		layout.style = TableStyle::MBR;
		LayoutEntry e;
		e.offset = 0;
		e.length = d.total_sectors * ss;
		e.mbr_type = 0x07;
		e.number = 1;
		layout.entries.push_back(e);
		for (int i = 0; i < 3; i++)
			layout.entries.push_back(LayoutEntry());
		return true;
	}

	bool gpt = false;
	for (int i = 0; i < 4; i++)
		gpt = gpt || img[446 + 16 * i + 4] == 0xEE;

	if (gpt)
	{
		std::vector<uint8_t> h(ss);
		d.read(1, h.data());
		layout.style = TableStyle::GPT;
		layout.gpt_disk_id = WinDisk::Guid::from_bytes_le(&h[56]);
		uint64_t first = get64(&h[40]), last = get64(&h[48]);
		layout.gpt_first_usable_offset = first * ss;
		layout.gpt_usable_length = (last - first + 1) * ss;
		layout.gpt_max_entries = get32(&h[80]);
		uint64_t entries_lba = get64(&h[72]);
		uint32_t n = get32(&h[80]), size = get32(&h[84]);
		std::vector<uint8_t> array((size_t(n) * size + ss - 1) / ss * ss);
		for (size_t i = 0; i < array.size() / ss; i++)
			d.read(entries_lba + i, &array[i * ss]);
		uint32_t number = 1;
		for (uint32_t i = 0; i < n; i++)
		{
			const uint8_t* p = &array[size_t(i) * size];
			WinDisk::Guid type = WinDisk::Guid::from_bytes_le(p);
			if (type.is_null())
				continue;
			LayoutEntry e;
			e.gpt_type = type;
			e.gpt_id = WinDisk::Guid::from_bytes_le(p + 16);
			e.offset = get64(p + 32) * ss;
			e.length = (get64(p + 40) - get64(p + 32) + 1) * ss;
			e.gpt_attributes = get64(p + 48);
			for (int k = 0; k < 36; k++)
			{
				uint16_t c = static_cast<uint16_t>(p[56 + 2 * k] | (p[57 + 2 * k] << 8));
				if (! c)
					break;
				e.gpt_name.push_back(static_cast<char16_t>(c));
			}
			e.number = number++;
			layout.entries.push_back(e);
		}
		return true;
	}

	layout.style = TableStyle::MBR;
	layout.mbr_signature = get32(img + 440);
	uint32_t number = 1;
	int ext_slot = -1;
	for (int i = 0; i < 4; i++)
	{
		const uint8_t* e = img + 446 + 16 * i;
		LayoutEntry le;
		if (e[4] != 0 && get32(e + 12) != 0)
		{
			le.mbr_type = e[4];
			le.mbr_boot = e[0] == 0x80;
			le.offset = uint64_t(get32(e + 8)) * ss;
			le.length = uint64_t(get32(e + 12)) * ss;
			le.mbr_recognized = true;
			if (is_ext(e[4]))
				ext_slot = i;
			else
				le.number = number++;
		}
		layout.entries.push_back(le);
	}
	if (ext_slot >= 0)
	{
		uint64_t ext_start = layout.entries[ext_slot].offset / ss;
		uint64_t ebr = ext_start;
		std::vector<uint8_t> s(ss);
		for (int guard = 0; guard < 128; guard++)
		{
			d.read(ebr, s.data());
			if (s[510] != 0x55 || s[511] != 0xAA)
				break;
			const uint8_t* e0 = &s[446];
			const uint8_t* e1 = &s[446 + 16];
			LayoutEntry log, link, empty;
			bool have_log = e0[4] != 0 && get32(e0 + 12) != 0;
			bool have_link = e1[4] != 0 && get32(e1 + 12) != 0;
			if (! have_log && ! have_link)
				break;
			if (have_log)
			{
				log.mbr_type = e0[4];
				log.mbr_boot = e0[0] == 0x80;
				log.offset = (ebr + get32(e0 + 8)) * ss;
				log.length = uint64_t(get32(e0 + 12)) * ss;
				log.mbr_recognized = true;
				log.number = number++;
			}
			uint64_t next = 0;
			if (have_link)
			{
				next = ext_start + get32(e1 + 8);
				link.mbr_type = e1[4];
				link.offset = next * ss;
				link.length = uint64_t(get32(e1 + 12)) * ss;
			}
			layout.entries.push_back(log);
			layout.entries.push_back(link);
			layout.entries.push_back(empty);
			layout.entries.push_back(empty);
			if (! have_link)
				break;
			ebr = next;
		}
	}
	return true;
}


}  // namespace TestSupport
}  // namespace WParted
