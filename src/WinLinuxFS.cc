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

#include "WinLinuxFS.h"
#include "WinCapabilities.h"
#include "WinComponents.h"
#include "WinDevNames.h"
#include "WinDiskBackend.h"
#include "WinFsUsage.h"
#include "WinHelpers.h"
#include "WinSwap.h"
#include "OperationDetail.h"
#include "Partition.h"
#include "Utils.h"
#include "i18n.h"

#include <cstdio>
#include <functional>
#include <memory>


namespace WParted
{


namespace
{

// The partition as it is on disk now.
bool target_of(const Partition& p, WinHelpers::Target& t, std::string& error)
{
	int disk = WinDevNames::disk_index(p.device_path.raw());
	if (disk < 0 || p.sector_size <= 0 || p.sector_start < 0)
	{
		error = "Unable to locate " + p.get_path().raw() + " on a Windows disk";
		return false;
	}
	return WinHelpers::locate(disk, uint64_t(p.sector_start) * uint64_t(p.sector_size), t, error);
}


bool fail(OperationDetail& od, const Glib::ustring& error)
{
	od.add_child(OperationDetail(error, STATUS_ERROR, FONT_ITALIC));
	return false;
}


// Done by WParted itself (not by Windows, not through WSL) for this file system.
bool by_wparted(FSType fstype, Caps::Op op)
{
	Caps::Capability c = Caps::resolve(Utils::get_filesystem_string(fstype).raw(), op, Caps::DiskContext(),
	                                   WinComponents::current());
	return c.available() && c.provider == Caps::Provider::WPARTED;
}


WinDisk::SectorReader reader_for(const WinHelpers::Target& t, uint32_t sector_size)
{
	return [t, sector_size](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
	{
		uint64_t first = t.offset / sector_size;
		uint64_t sectors = t.length / sector_size;
		if (lba > sectors || count > sectors - lba)
			return false;
		WinDisk::DiskBackend* backend = WinDisk::get_backend();
		if (! backend)
			return false;
		out.resize(size_t(count) * sector_size);
		std::string error;
		return backend->read_sectors(t.disk, first + lba, count, sector_size, out.data(), error);
	};
}


struct ExtSuperblock
{
	uint64_t block_size = 0;
	uint64_t blocks = 0;
	bool     is_64bit = false;
};


bool read_ext_superblock(const WinHelpers::Target& t, uint32_t sector_size, ExtSuperblock& sb)
{
	if (sector_size == 0 || sector_size > 4096)
		return false;
	std::vector<uint8_t> buf;
	uint64_t lba = 1024 / sector_size;
	uint32_t count = sector_size >= 2048 ? 1 : 2048 / sector_size - static_cast<uint32_t>(lba);
	if (! reader_for(t, sector_size)(lba, count, buf))
		return false;
	const uint8_t* s = buf.data() + (1024 - lba * sector_size);
	auto le32 = [&](size_t o) { return uint32_t(s[o]) | uint32_t(s[o + 1]) << 8 | uint32_t(s[o + 2]) << 16 |
	                                   uint32_t(s[o + 3]) << 24; };
	if (s[0x38] != 0x53 || s[0x39] != 0xEF)
		return false;
	uint32_t log = le32(24);
	if (log > 6)
		return false;
	sb.block_size = 1024ULL << log;
	sb.is_64bit = le32(0x60) & 0x80;
	sb.blocks = le32(4);
	if (sb.is_64bit)
		sb.blocks |= uint64_t(le32(0x150)) << 32;
	return true;
}


// Run 'step' on a session over the partition, as one step in the details.
bool native_step(const Partition& partition, OperationDetail& od, const Glib::ustring& description,
                 const std::function<bool(WinDisk::DiskSession&, std::string&)>& step)
{
	od.add_child(OperationDetail(description, STATUS_EXECUTE, FONT_BOLD_ITALIC));
	OperationDetail& child = od.get_last_child();
	WinHelpers::Target t;
	std::string error;
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	std::unique_ptr<WinDisk::DiskSession> session;
	if (target_of(partition, t, error) && backend)
		session = backend->open_session(t.disk, t.offset, t.length, true,
		                                WinDisk::SessionRange::INSIDE_PARTITION, error);
	bool ok = session && step(*session, error);
	if (! ok)
		child.add_child(OperationDetail(error, STATUS_NONE, FONT_ITALIC));
	child.set_success_and_capture_errors(ok);
	return ok;
}

}  // anonymous namespace


// ---------------------------------------------------------------------------
// ext2, ext3, ext4

FS WinExtFS::get_filesystem_support()
{
	FS fs(m_specific_fstype);
	fs.busy = FS::GPARTED;

	if (by_wparted(m_specific_fstype, Caps::Op::INFO))
		fs.read = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::CREATE))
		fs.create = fs.create_with_label = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::CHECK))
		fs.check = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::LABEL))
		fs.write_label = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::UUID))
		fs.write_uuid = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::GROW) && fs.check)
		fs.grow = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::SHRINK) && fs.check && fs.read)
		fs.shrink = FS::EXTERNAL;
	if (by_wparted(m_specific_fstype, Caps::Op::MOVE))
		fs.move = FS::GPARTED;
	if (by_wparted(m_specific_fstype, Caps::Op::COPY))
		fs.copy = FS::GPARTED;

	// As GParted: ext2 and ext3 hold at most 2^32 - 1 blocks.
	if (m_specific_fstype == FS_EXT2 || m_specific_fstype == FS_EXT3)
		m_fs_limits.max_size = Utils::floor_size(16 * TEBIBYTE - 4 * KIBIBYTE, MEBIBYTE);
	return fs;
}


void WinExtFS::set_used_sectors(Partition& partition)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition, t, error))
	{
		partition.push_back_message(error);
		return;
	}
	const uint32_t ss = static_cast<uint32_t>(partition.sector_size);
	WinFsUsage::Usage usage;
	if (! WinFsUsage::read(Utils::get_filesystem_string(m_specific_fstype).raw(), reader_for(t, ss), ss,
	                       t.length, usage, error))
	{
		partition.push_back_message(error);
		return;
	}
	uint64_t free = usage.free;

	// As GParted: resize2fs won't shrink below its own estimate of the minimum
	// size, so count the space above that as free.
	ExtSuperblock sb;
	if (! partition.busy && WinComponents::current().wparted_tools && read_ext_superblock(t, ss, sb))
	{
		WinSandbox::Result r = WinHelpers::run(WinHelpers::Ext::min_size(t));
		long long minimum_blocks = -1;
		if (r.started && r.exit_code == 0)
		{
			size_t p = r.output.find("Estimated minimum size of the filesystem:");
			if (p != std::string::npos)
				std::sscanf(r.output.c_str() + p, "Estimated minimum size of the filesystem: %lld",
				            &minimum_blocks);
		}
		if (minimum_blocks > 0 && uint64_t(minimum_blocks) <= sb.blocks)
			free = (sb.blocks - uint64_t(minimum_blocks)) * sb.block_size;
		partition.fs_block_size = static_cast<Byte_Value>(sb.block_size);
	}
	if (free > usage.size)
		free = usage.size;
	partition.set_sector_usage(static_cast<Sector>(usage.size / ss), static_cast<Sector>(free / ss));
}


bool WinExtFS::write_label(const Partition& partition, OperationDetail& operationdetail)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition, t, error))
		return fail(operationdetail, error);
	return ! operationdetail.execute_helper(WinHelpers::Ext::set_label(t, partition.get_filesystem_label().raw()),
	                                        partition.get_path(), EXEC_CHECK_STATUS);
}


bool WinExtFS::write_uuid(const Partition& partition, OperationDetail& operationdetail)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition, t, error))
		return fail(operationdetail, error);
	return ! operationdetail.execute_helper(WinHelpers::Ext::new_uuid(t), partition.get_path(), EXEC_CHECK_STATUS);
}


bool WinExtFS::create(const Partition& new_partition, OperationDetail& operationdetail)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(new_partition, t, error))
		return fail(operationdetail, error);
	return ! operationdetail.execute_helper(
	        WinHelpers::Ext::create(t, Utils::get_filesystem_string(m_specific_fstype).raw(),
	                                new_partition.get_filesystem_label().raw()),
	        new_partition.get_path(), EXEC_CHECK_STATUS | EXEC_CANCEL_SAFE | EXEC_PROGRESS_STDOUT,
	        static_cast<StreamSlot>(sigc::mem_fun(*this, &WinExtFS::create_progress)));
}


bool WinExtFS::resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition_new, t, error))
		return fail(operationdetail, error);

	bool convert_to_64bit = false;
	if (m_specific_fstype == FS_EXT4 && partition_new.get_byte_length() >= 16 * TEBIBYTE)
	{
		ExtSuperblock sb;
		if (! read_ext_superblock(t, static_cast<uint32_t>(partition_new.sector_size), sb))
			return fail(operationdetail,
			            _("Failed to determine whether the ext4 file system has the 64bit feature enabled"));
		convert_to_64bit = ! sb.is_64bit;
	}

	uint64_t size_kib = 0;
	if (! fill_partition)
	{
		size_kib = static_cast<uint64_t>(partition_new.get_byte_length() / KIBIBYTE);
		if (size_kib == 0 || size_kib * KIBIBYTE > t.length)
			return fail(operationdetail, "The new file system size does not fit in the partition");
	}
	return ! operationdetail.execute_helper(WinHelpers::Ext::resize(t, size_kib, convert_to_64bit),
	                                        partition_new.get_path(), EXEC_CHECK_STATUS | EXEC_PROGRESS_STDOUT,
	                                        static_cast<StreamSlot>(sigc::mem_fun(*this, &WinExtFS::resize_progress)));
}


bool WinExtFS::check_repair(const Partition& partition, OperationDetail& operationdetail)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition, t, error))
		return fail(operationdetail, error);
	// Not EXEC_CANCEL_SAFE: stopping e2fsck in the middle of a repair is only
	// done when the user forces it.
	int exit_status = operationdetail.execute_helper(WinHelpers::Ext::check_repair(t), partition.get_path(),
	        EXEC_PROGRESS_STDOUT, static_cast<StreamSlot>(sigc::mem_fun(*this, &WinExtFS::check_repair_progress)));
	// e2fsck(8): 0 no errors, 1 errors corrected, 2 corrected and reboot needed.
	bool success = (exit_status == 0 || exit_status == 1 || exit_status == 2);
	operationdetail.get_last_child().set_success_and_capture_errors(success);
	return success;
}


// ---------------------------------------------------------------------------
// linux-swap

FS WinSwapFS::get_filesystem_support()
{
	FS fs(FS_LINUX_SWAP);
	fs.busy = FS::GPARTED;
	fs.read = FS::EXTERNAL;   // Swap space not in use is all free
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::CREATE))
		fs.create = fs.create_with_label = FS::EXTERNAL;
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::LABEL))
		fs.write_label = FS::EXTERNAL;
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::UUID))
		fs.write_uuid = FS::EXTERNAL;
	// Resizing swap space recreates it, keeping its label and UUID.
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::GROW))
		fs.grow = FS::EXTERNAL;
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::SHRINK))
		fs.shrink = FS::EXTERNAL;
	// Moved and copied by GParted's block copy, then recreated.
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::MOVE))
		fs.move = FS::GPARTED;
	if (by_wparted(FS_LINUX_SWAP, Caps::Op::COPY))
		fs.copy = FS::GPARTED;
	m_fs_limits = FS_Limits(WinSwap::MIN_PAGES * WinSwap::PAGE_SIZE, 0);
	return fs;
}


void WinSwapFS::set_used_sectors(Partition& partition)
{
	// Windows never uses Linux swap space.
	Sector fs_size = partition.get_sector_length();
	partition.set_sector_usage(fs_size, fs_size);
}


bool WinSwapFS::write_label(const Partition& partition, OperationDetail& operationdetail)
{
	std::string label = partition.get_filesystem_label().raw();
	return native_step(partition, operationdetail,
	                   Glib::ustring::compose(_("WParted: set the swap space label to \"%1\""), label),
	                   [&](WinDisk::DiskSession& s, std::string& e) { return WinSwap::set_label(s, label, e); });
}


bool WinSwapFS::write_uuid(const Partition& partition, OperationDetail& operationdetail)
{
	std::string uuid = WinSwap::random_uuid();
	return native_step(partition, operationdetail,
	                   Glib::ustring::compose(_("WParted: set the swap space UUID to %1"), uuid),
	                   [&](WinDisk::DiskSession& s, std::string& e)
	                   {
	                           if (uuid.empty())
	                           {
	                                   e = "Unable to generate a random UUID";
	                                   return false;
	                           }
	                           return WinSwap::set_uuid(s, uuid, e);
	                   });
}


bool WinSwapFS::create(const Partition& new_partition, OperationDetail& operationdetail)
{
	std::string label = new_partition.get_filesystem_label().raw();
	return native_step(new_partition, operationdetail, _("WParted: create Linux swap space"),
	                   [&](WinDisk::DiskSession& s, std::string& e) { return WinSwap::create(s, label, "", e); });
}


bool WinSwapFS::resize(const Partition& partition_new, OperationDetail& operationdetail, bool fill_partition)
{
	std::string label = partition_new.get_filesystem_label().raw();
	std::string uuid = partition_new.uuid.raw();
	uint8_t raw[16];
	if (! uuid.empty() && ! WinSwap::parse_uuid(uuid, raw))
		uuid.clear();
	return native_step(partition_new, operationdetail, _("WParted: recreate Linux swap space, keeping its label and UUID"),
	                   [&](WinDisk::DiskSession& s, std::string& e) { return WinSwap::create(s, label, uuid, e); });
}


// ---------------------------------------------------------------------------
// Usage only: btrfs, xfs, HFS+, APFS

FS WinUsageFS::get_filesystem_support()
{
	FS fs(m_fstype);
	fs.busy = FS::GPARTED;
	if (by_wparted(m_fstype, Caps::Op::INFO))
		fs.read = FS::EXTERNAL;
	if (by_wparted(m_fstype, Caps::Op::MOVE))
		fs.move = FS::GPARTED;
	if (by_wparted(m_fstype, Caps::Op::COPY))
		fs.copy = FS::GPARTED;
	return fs;
}


void WinUsageFS::set_used_sectors(Partition& partition)
{
	WinHelpers::Target t;
	std::string error;
	if (! target_of(partition, t, error))
	{
		partition.push_back_message(error);
		return;
	}
	const uint32_t ss = static_cast<uint32_t>(partition.sector_size);
	WinFsUsage::Usage usage;
	if (! WinFsUsage::read(Utils::get_filesystem_string(m_fstype).raw(), reader_for(t, ss), ss, t.length, usage,
	                       error))
	{
		partition.push_back_message(error);
		return;
	}
	partition.set_sector_usage(static_cast<Sector>(usage.size / ss),
	                           static_cast<Sector>(std::min(usage.free, usage.size) / ss));
}


}  // namespace WParted

#endif /* _WIN32 */
