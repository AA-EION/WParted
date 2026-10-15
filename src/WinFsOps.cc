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

/* GParted adapter for WinVolumeOps: converts Partition objects to disk
 * locations and reports progress and errors in the operation details.
 */

#if defined(_WIN32)

#include "WinFsOps.h"
#include "WinVolumeOps.h"
#include "WinDevNames.h"
#include "Utils.h"
#include "i18n.h"

#include <map>
#include <mutex>
#include <tuple>


namespace WParted
{
namespace WinFsOps
{

namespace
{

bool target_of(const Partition& p, WinVolumeOps::Target& t, std::string& error)
{
	t.disk = WinDevNames::disk_index(p.device_path.raw());
	if (t.disk < 0 || p.sector_size <= 0 || p.sector_start < 0 || p.sector_end < p.sector_start)
	{
		error = "Unable to locate " + p.get_path().raw() + " on a Windows disk";
		return false;
	}
	t.offset = uint64_t(p.sector_start) * uint64_t(p.sector_size);
	t.length = uint64_t(p.get_sector_length()) * uint64_t(p.sector_size);
	return true;
}


void detail(OperationDetail& od, const Glib::ustring& text, OperationDetailStatus status = STATUS_NONE)
{
	od.add_child(OperationDetail(text, status, FONT_ITALIC));
}


bool fail(OperationDetail& od, const std::string& error)
{
	detail(od, error, STATUS_ERROR);
	return false;
}


std::mutex g_cache_mutex;
std::map<std::tuple<int, uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> g_size_cache;

}  // anonymous namespace


void clear_cache()
{
	std::lock_guard<std::mutex> lock(g_cache_mutex);
	g_size_cache.clear();
}


bool format(const Partition& partition, const std::string& windows_fs, OperationDetail& od)
{
	WinVolumeOps::Target t;
	std::string error;
	if (! target_of(partition, t, error))
		return fail(od, error);
	detail(od, Glib::ustring::compose(_("Windows: quick format as %1"), windows_fs));
	if (! WinVolumeOps::format(t, windows_fs, partition.get_filesystem_label().raw(), error))
		return fail(od, error);
	return true;
}


bool set_label(const Partition& partition, OperationDetail& od)
{
	WinVolumeOps::Target t;
	std::string error;
	if (! target_of(partition, t, error) ||
	    ! WinVolumeOps::set_label(t, partition.get_filesystem_label().raw(), error))
		return fail(od, error);
	return true;
}


bool check_repair(const Partition& partition, OperationDetail& od)
{
	WinVolumeOps::Target t;
	std::string error;
	if (! target_of(partition, t, error))
		return fail(od, error);

	std::string command;
	int exit_code = -1;
	OperationDetail* output_od = nullptr;
	bool ok = WinVolumeOps::check_repair(t, [&](const std::string& text)
	{
		if (! output_od)
		{
			od.add_child(OperationDetail(command, STATUS_EXECUTE, FONT_BOLD_ITALIC));
			od.get_last_child().add_child(OperationDetail("", STATUS_NONE, FONT_MONOSPACE));
			output_od = &od.get_last_child().get_last_child();
		}
		output_od->set_description(text, FONT_MONOSPACE);
	}, command, exit_code, error);
	if (! ok)
		return fail(od, error);
	return true;
}


bool resize_partition_and_filesystem(const Partition& partition_old, const Partition& partition_new,
                                     OperationDetail& od)
{
	od.add_child(OperationDetail(Glib::ustring::compose(
	        _("resize partition and file system on %1 using Windows"), partition_old.get_path())));
	OperationDetail& child = od.get_last_child();

	WinVolumeOps::Target t_old, t_new;
	std::string error;
	bool ok = target_of(partition_old, t_old, error) && target_of(partition_new, t_new, error);
	if (ok && (t_old.disk != t_new.disk || t_old.offset != t_new.offset))
	{
		error = "Windows can only move the end of a partition";
		ok = false;
	}
	if (ok)
	{
		detail(child, Glib::ustring::compose(_("new size: %1"),
		                                     Utils::format_size(partition_new.get_sector_length(),
		                                                        partition_new.sector_size)));
		ok = WinVolumeOps::resize(t_old, t_new.length, error);
	}
	if (! ok)
		detail(child, error, STATUS_ERROR);
	clear_cache();
	child.set_success_and_capture_errors(ok);
	return ok;
}


bool grow_filesystem_to_partition(const Partition& partition, OperationDetail& od)
{
	WinVolumeOps::Target t;
	std::string error;
	bool already = false;
	if (! target_of(partition, t, error) || ! WinVolumeOps::grow_filesystem_to_partition(t, already, error))
		return fail(od, error);
	if (already)
		detail(od, _("The file system already fills the partition."));
	return true;
}


bool supported_size(const Partition& partition, uint64_t& min_bytes, uint64_t& max_bytes, std::string& error)
{
	WinVolumeOps::Target t;
	if (! target_of(partition, t, error))
		return false;
	auto key = std::make_tuple(t.disk, t.offset, t.length);
	{
		std::lock_guard<std::mutex> lock(g_cache_mutex);
		auto it = g_size_cache.find(key);
		if (it != g_size_cache.end())
		{
			min_bytes = it->second.first;
			max_bytes = it->second.second;
			return true;
		}
	}
	if (! WinVolumeOps::supported_size(t, min_bytes, max_bytes, error))
		return false;
	std::lock_guard<std::mutex> lock(g_cache_mutex);
	g_size_cache[key] = std::make_pair(min_bytes, max_bytes);
	return true;
}


bool usage(const Partition& partition, int64_t& fs_size, int64_t& fs_free, std::string& error)
{
	WinVolumeOps::Target t;
	return target_of(partition, t, error) && WinVolumeOps::usage(t, fs_size, fs_free, error);
}


bool mount(const Partition& partition, std::string& mount_point, std::string& error)
{
	WinVolumeOps::Target t;
	return target_of(partition, t, error) && WinVolumeOps::mount(t, mount_point, error);
}


bool unmount(const Partition& partition, std::string& error)
{
	WinVolumeOps::Target t;
	return target_of(partition, t, error) && WinVolumeOps::unmount(t, error);
}


}  // namespace WinFsOps
}  // namespace WParted

#endif /* _WIN32 */
