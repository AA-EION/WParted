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

#include <algorithm>
#include <atomic>
#include <cstring>


namespace WParted
{
namespace WinDisk
{


namespace
{
std::atomic<CopyContext*> g_current{nullptr};
}


CopyContext* CopyContext::current()
{
	return g_current.load();
}


void CopyContext::set_current(CopyContext* context)
{
	g_current.store(context);
}


bool CopyContext::open(int src_disk, uint64_t src_offset, uint64_t src_length,
                       int dst_disk, uint64_t dst_offset, uint64_t dst_length, std::string& error)
{
	close();
	DiskBackend* backend = get_backend();
	if (! backend)
	{
		error = "No disk access";
		return false;
	}
	if (src_length == 0 || dst_length == 0)
	{
		error = "Nothing to copy";
		return false;
	}
	m_src_disk = src_disk;
	m_dst_disk = dst_disk;
	m_dst_offset = dst_offset;
	m_dst_limit = dst_offset + dst_length;

	// Moving inside one partition: one session over both ranges.  It is only
	// granted when both lie inside the same partition.
	if (src_disk == dst_disk)
	{
		uint64_t base = std::min(src_offset, dst_offset);
		uint64_t end = std::max(src_offset + src_length, dst_offset + dst_length);
		std::string why;
		std::unique_ptr<DiskSession> s = backend->open_session(dst_disk, base, end - base, true,
		                                                       SessionRange::INSIDE_PARTITION, why);
		if (s)
		{
			m_dst = std::move(s);
			m_shared = true;
			m_src_base = m_dst_base = base;
			m_src_length = m_dst_length = end - base;
			return true;
		}
	}

	m_src = backend->open_locked_reader(src_disk, src_offset, src_length, error);
	if (! m_src)
	{
		error = "Source: " + error;
		return false;
	}
	m_dst = backend->open_session(dst_disk, dst_offset, dst_length, true, SessionRange::INSIDE_PARTITION, error);
	if (! m_dst)
	{
		error = "Destination: " + error;
		m_src.reset();
		return false;
	}
	m_src_base = src_offset;
	m_src_length = src_length;
	m_dst_base = dst_offset;
	m_dst_length = dst_length;
	return true;
}


uint32_t CopyContext::src_sector_size() const
{
	DiskSession* s = m_shared ? m_dst.get() : m_src.get();
	return s ? s->sector_size() : 0;
}


uint32_t CopyContext::dst_sector_size() const
{
	return m_dst ? m_dst->sector_size() : 0;
}


bool CopyContext::read(uint64_t disk_offset, uint64_t length, void* buffer, std::string& error)
{
	DiskSession* s = m_shared ? m_dst.get() : m_src.get();
	if (! s || disk_offset < m_src_base || length > m_src_length ||
	    disk_offset - m_src_base > m_src_length - length)
	{
		error = "read outside of the copy source";
		return false;
	}
	return s->read(disk_offset - m_src_base, length, buffer, error);
}


bool CopyContext::write_verified(uint64_t disk_offset, uint64_t length, const void* buffer, std::string& error)
{
	// Only the destination range, also when the session covers more (moving).
	if (! m_dst || disk_offset < m_dst_offset || length > m_dst_limit - m_dst_offset ||
	    disk_offset - m_dst_offset > (m_dst_limit - m_dst_offset) - length)
	{
		error = "write outside of the copy destination";
		return false;
	}
	uint64_t rel = disk_offset - m_dst_base;
	if (! m_dst->write(rel, length, buffer, error))
		return false;
	m_verify.resize(length);
	if (! m_dst->read(rel, length, m_verify.data(), error))
		return false;
	if (std::memcmp(m_verify.data(), buffer, length) != 0)
	{
		error = "the data read back differs from the data written at byte " + std::to_string(disk_offset);
		return false;
	}
	return true;
}


bool CopyContext::flush(std::string& error)
{
	return ! m_dst || m_dst->flush(error);
}


void CopyContext::close()
{
	if (g_current.load() == this)
		g_current.store(nullptr);
	if (m_dst)
	{
		std::string ignored;
		m_dst->flush(ignored);
	}
	m_dst.reset();
	m_src.reset();
	m_shared = false;
}


}  // namespace WinDisk
}  // namespace WParted
