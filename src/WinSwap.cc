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

#if defined(_WIN32)
#define _CRT_RAND_S     // rand_s(): RtlGenRandom
#endif

#include "WinSwap.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>


namespace WParted
{
namespace WinSwap
{


namespace
{

const size_t OFF_VERSION   = 1024;
const size_t OFF_LAST_PAGE = 1028;
const size_t OFF_BAD_PAGES = 1032;
const size_t OFF_UUID      = 1036;
const size_t OFF_LABEL     = 1052;
const char   SIGNATURE[]   = "SWAPSPACE2";
const size_t SIG_SIZE      = 10;


void put32(uint8_t* p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = static_cast<uint8_t>(v >> (8 * i));
}


uint32_t get32(const uint8_t* p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}


bool random_bytes(uint8_t* out, size_t len)
{
#if defined(_WIN32)
	while (len)
	{
		unsigned int v;
		if (rand_s(&v) != 0)
			return false;
		size_t n = len < sizeof(v) ? len : sizeof(v);
		std::memcpy(out, &v, n);
		out += n;
		len -= n;
	}
	return true;
#else
	FILE* f = std::fopen("/dev/urandom", "rb");
	if (! f)
		return false;
	bool ok = std::fread(out, 1, len, f) == len;
	std::fclose(f);
	return ok;
#endif
}


// The first page, or false with 'error' set.
bool read_page(WinDisk::DiskSession& session, std::vector<uint8_t>& page, std::string& error)
{
	if (session.length() < uint64_t(PAGE_SIZE))
	{
		error = "The partition is too small for swap space";
		return false;
	}
	page.assign(PAGE_SIZE, 0);
	return session.read(0, PAGE_SIZE, page.data(), error);
}


bool write_page(WinDisk::DiskSession& session, const std::vector<uint8_t>& page, std::string& error)
{
	if (! session.writable())
	{
		error = "The partition is open read-only";
		return false;
	}
	const uint32_t ss = session.sector_size();
	if (ss == 0 || PAGE_SIZE % ss != 0)
	{
		error = "Unsupported sector size";
		return false;
	}
	return session.write(0, PAGE_SIZE, page.data(), error) && session.flush(error);
}


bool valid_header(const std::vector<uint8_t>& page)
{
	return std::memcmp(&page[PAGE_SIZE - SIG_SIZE], SIGNATURE, SIG_SIZE) == 0 &&
	       get32(&page[OFF_VERSION]) == 1;
}


bool check_label(const std::string& label, std::string& error)
{
	if (label.size() > LABEL_SIZE)
	{
		error = "Swap space labels can be at most 16 bytes long";
		return false;
	}
	return true;
}


void put_label(std::vector<uint8_t>& page, const std::string& label)
{
	std::memset(&page[OFF_LABEL], 0, LABEL_SIZE);
	std::memcpy(&page[OFF_LABEL], label.data(), label.size());
}


int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

}  // anonymous namespace


bool parse_uuid(const std::string& text, uint8_t out[16])
{
	if (text.size() != 36)
		return false;
	size_t j = 0;
	for (size_t i = 0; i < 36; )
	{
		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			if (text[i] != '-')
				return false;
			i++;
			continue;
		}
		int hi = hex_value(text[i]);
		int lo = hex_value(text[i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[j++] = static_cast<uint8_t>(hi << 4 | lo);
		i += 2;
	}
	return j == 16;
}


std::string format_uuid(const uint8_t in[16])
{
	static const char hex[] = "0123456789abcdef";
	std::string s;
	for (int i = 0; i < 16; i++)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
			s += '-';
		s += hex[in[i] >> 4];
		s += hex[in[i] & 15];
	}
	return s;
}


std::string random_uuid()
{
	uint8_t u[16];
	if (! random_bytes(u, sizeof(u)))
		return "";
	u[6] = static_cast<uint8_t>((u[6] & 0x0F) | 0x40);   // Version 4
	u[8] = static_cast<uint8_t>((u[8] & 0x3F) | 0x80);   // RFC 4122 variant
	return format_uuid(u);
}


bool create(WinDisk::DiskSession& session, const std::string& label, const std::string& uuid,
            std::string& error)
{
	if (! check_label(label, error))
		return false;
	std::string id = uuid.empty() ? random_uuid() : uuid;
	uint8_t raw_uuid[16];
	if (! parse_uuid(id, raw_uuid))
	{
		error = uuid.empty() ? "Unable to generate a random UUID" : "Invalid UUID: " + uuid;
		return false;
	}

	uint64_t pages = session.length() / PAGE_SIZE;
	if (pages < MIN_PAGES)
	{
		error = "The partition is too small for swap space (at least 40 KiB are needed)";
		return false;
	}
	uint64_t last_page = pages - 1;
	if (last_page > 0xFFFFFFFFu)
		last_page = 0xFFFFFFFFu;   // Largest version 1 swap header, as mkswap

	std::vector<uint8_t> page(PAGE_SIZE, 0);
	put32(&page[OFF_VERSION], 1);
	put32(&page[OFF_LAST_PAGE], static_cast<uint32_t>(last_page));
	put32(&page[OFF_BAD_PAGES], 0);
	std::memcpy(&page[OFF_UUID], raw_uuid, 16);
	put_label(page, label);
	std::memcpy(&page[PAGE_SIZE - SIG_SIZE], SIGNATURE, SIG_SIZE);
	return write_page(session, page, error);
}


bool read(WinDisk::DiskSession& session, Info& info, std::string& error)
{
	std::vector<uint8_t> page;
	if (! read_page(session, page, error))
		return false;
	if (! valid_header(page))
	{
		error = "No Linux swap space (version 1, 4 KiB pages) found";
		return false;
	}
	info = Info();
	info.page_size = PAGE_SIZE;
	info.last_page = get32(&page[OFF_LAST_PAGE]);
	static const uint8_t zero[16] = {};
	if (std::memcmp(&page[OFF_UUID], zero, 16) != 0)
		info.uuid = format_uuid(&page[OFF_UUID]);
	const char* l = reinterpret_cast<const char*>(&page[OFF_LABEL]);
	info.label.assign(l, strnlen(l, LABEL_SIZE));
	return true;
}


bool set_label(WinDisk::DiskSession& session, const std::string& label, std::string& error)
{
	if (! check_label(label, error))
		return false;
	std::vector<uint8_t> page;
	if (! read_page(session, page, error))
		return false;
	if (! valid_header(page))
	{
		error = "No Linux swap space (version 1, 4 KiB pages) found";
		return false;
	}
	put_label(page, label);
	return write_page(session, page, error);
}


bool set_uuid(WinDisk::DiskSession& session, const std::string& uuid, std::string& error)
{
	uint8_t raw[16];
	if (! parse_uuid(uuid, raw))
	{
		error = "Invalid UUID: " + uuid;
		return false;
	}
	std::vector<uint8_t> page;
	if (! read_page(session, page, error))
		return false;
	if (! valid_header(page))
	{
		error = "No Linux swap space (version 1, 4 KiB pages) found";
		return false;
	}
	std::memcpy(&page[OFF_UUID], raw, 16);
	return write_page(session, page, error);
}


}  // namespace WinSwap
}  // namespace WParted
