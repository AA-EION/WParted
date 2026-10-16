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

/* WinCopy
 *
 * Disk access for GParted's block copy (moving and copying partitions) on
 * Windows.  For the whole copy:
 *
 *   - moving inside one partition (GParted first grows the partition over the
 *     old and the new place): one writable session over both, which keeps the
 *     partition's volume locked and dismounted;
 *   - copying: the source through a locked read-only session (Windows can't
 *     change it meanwhile), the destination through a writable session on the
 *     destination partition;
 *   - every block written is read back and compared.
 *
 * Writes outside the destination range, and sessions crossing partition
 * boundaries, are refused as everywhere else.
 */

#ifndef WPARTED_WINCOPY_H
#define WPARTED_WINCOPY_H

#include "WinDiskBackend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace WParted
{
namespace WinDisk
{


class CopyContext
{
public:
	CopyContext() = default;
	~CopyContext()  { close(); }
	CopyContext(const CopyContext&) = delete;
	CopyContext& operator=(const CopyContext&) = delete;

	// Byte ranges on the disks.
	bool open(int src_disk, uint64_t src_offset, uint64_t src_length,
	          int dst_disk, uint64_t dst_offset, uint64_t dst_length, std::string& error);
	bool is_open() const  { return m_dst != nullptr; }
	bool shared() const   { return m_shared; }

	// Offsets are bytes from the start of the disk.
	bool read(uint64_t disk_offset, uint64_t length, void* buffer, std::string& error);
	bool write_verified(uint64_t disk_offset, uint64_t length, const void* buffer, std::string& error);
	bool flush(std::string& error);
	void close();

	uint32_t src_sector_size() const;
	uint32_t dst_sector_size() const;

	// The context the block copy of the current operation uses (GUI thread sets
	// it around the copy, the copy thread uses it).  nullptr: none.
	static CopyContext* current();
	static void set_current(CopyContext* context);

private:
	std::unique_ptr<DiskSession> m_src;     // == m_dst's session when shared
	std::unique_ptr<DiskSession> m_dst;
	bool                         m_shared = false;
	int                          m_src_disk = -1;
	int                          m_dst_disk = -1;
	uint64_t                     m_src_base = 0, m_src_length = 0;   // Session ranges
	uint64_t                     m_dst_base = 0, m_dst_length = 0;
	uint64_t                     m_dst_offset = 0, m_dst_limit = 0;   // Writable part
	std::vector<uint8_t>         m_verify;
};


}  // namespace WinDisk
}  // namespace WParted

#endif /* WPARTED_WINCOPY_H */
