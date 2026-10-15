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

/* In memory disks emulating the Windows partition manager for unit tests.
 *
 * The encoder and decoder here are deliberately written independently of the
 * production WinDiskModel parser, following the Windows documented
 * DRIVE_LAYOUT_INFORMATION_EX conventions: MBR entries 0-3 are the MBR slots,
 * then one group of four entries per EBR (logical partition, link to next
 * EBR, two unused).  GPT entries are written to slots in array order.
 */

#ifndef WPARTED_TEST_MEMDISKBACKEND_H
#define WPARTED_TEST_MEMDISKBACKEND_H

#include "WinDiskBackend.h"

#include <map>
#include <string>
#include <vector>


namespace WParted
{
namespace TestSupport
{


// Sparse disk image: only sectors which were written are stored.
struct MemDisk
{
	WinDisk::DiskInfo                         info;
	uint64_t                                  total_sectors = 0;
	std::map<uint64_t, std::vector<uint8_t>>  sectors;
	int                                       layout_writes = 0;

	uint32_t ss() const  { return info.logical_sector_size; }
	void read(uint64_t lba, uint8_t* out) const;
	void write(uint64_t lba, const uint8_t* in);
	uint8_t* sector(uint64_t lba);   // Writable sector, created on demand
	void zero(uint64_t lba, uint64_t count);
	bool                 corrupt_next_write = false;  // Drop the last partition when writing
};


class MemDiskBackend : public WinDisk::DiskBackend
{
public:
	// Add a disk of 'sectors' sectors, all zero (RAW).
	MemDisk& add_disk(int index, uint64_t sectors, uint32_t sector_size = 512,
	                  const std::string& serial = "SERIAL");
	MemDisk& disk(int index)  { return m_disks.at(index); }
	void remove_disk(int index)  { m_disks.erase(index); }

	std::vector<int> enumerate_disks() override;
	bool query_disk(int index, WinDisk::DiskInfo& info, std::string& error) override;
	bool read_layout(int index, WinDisk::DiskLayout& layout, std::string& error) override;
	bool read_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                  void* buffer, std::string& error) override;
	bool write_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                   const void* buffer, std::string& error) override;
	bool write_layout(int index, const WinDisk::DiskLayout& current, const WinDisk::DiskLayout& layout,
	                  bool new_table, std::string& error) override;
	bool update_properties(int index, std::string& error) override;

	// Encode a layout directly onto a disk image (used to build test fixtures).
	static bool encode(MemDisk& disk, const WinDisk::DiskLayout& layout, bool new_table, std::string& error);
	static bool decode(const MemDisk& disk, WinDisk::DiskLayout& layout, std::string& error);

private:
	std::map<int, MemDisk> m_disks;
};


}  // namespace TestSupport
}  // namespace WParted

#endif /* WPARTED_TEST_MEMDISKBACKEND_H */
