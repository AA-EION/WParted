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

#include "WinScan.h"
#include "WinPartedBridge.h"
#include "WinDiskBackend.h"
#include "WinDevNames.h"
#include "parted/parted.h"

#if defined(_WIN32)
#include "Win32Volumes.h"
#endif

#include <map>
#include <mutex>


namespace WParted
{
namespace WinScan
{

namespace
{

struct Snapshot
{
	std::vector<std::string>                    devices;
	std::map<std::string, std::vector<std::string>> partitions;  // device -> partition paths
	std::map<std::string, Location>             locations;   // path -> location
	std::map<std::string, VolumeState>          volumes;     // path -> volume
	std::map<std::string, WinFsProbe::Result>   probes;      // path -> signature
};

std::mutex g_mutex;
Snapshot   g_snapshot;


WinDisk::SectorReader reader_for(const Location& loc)
{
	return [loc](uint64_t lba, uint32_t count, std::vector<uint8_t>& out) -> bool
	{
		WinDisk::DiskBackend* backend = WinDisk::get_backend();
		uint64_t limit = loc.length / loc.sector_size;
		if (! backend || count == 0 || lba + count > limit)
			return false;
		out.assign(size_t(count) * loc.sector_size, 0);
		std::string error;
		return backend->read_sectors(loc.disk, loc.offset / loc.sector_size + lba, count, loc.sector_size,
		                             out.data(), error);
	};
}

}  // anonymous namespace


void load()
{
	WinPartedBridge::QuietScope quiet;
	WinPartedBridge::refresh_device_registry();

	Snapshot snap;
	for (const WinDisk::DiskInfo& info : WinPartedBridge::registered_disks())
	{
		std::string dev_path = WinDevNames::disk_path(info.index);
		snap.devices.push_back(dev_path);

		Location whole;
		whole.path        = dev_path;
		whole.device_path = dev_path;
		whole.disk        = info.index;
		whole.offset      = 0;
		whole.length      = info.size_bytes;
		whole.sector_size = info.logical_sector_size;
		snap.locations[dev_path] = whole;

		PedDevice* dev = ped_device_get(dev_path.c_str());
		if (! dev)
			continue;
		PedDisk* disk = ped_disk_new(dev);
		if (disk && std::string(disk->type->name) != "loop")
		{
			for (PedPartition* p = ped_disk_next_partition(disk, nullptr); p; p = ped_disk_next_partition(disk, p))
			{
				Location loc;
				loc.path        = WinDevNames::partition_path(dev_path, p->num);
				loc.device_path = dev_path;
				loc.disk        = info.index;
				loc.number      = p->num;
				loc.offset      = uint64_t(p->geom.start) * info.logical_sector_size;
				loc.length      = uint64_t(p->geom.length) * info.logical_sector_size;
				loc.sector_size = info.logical_sector_size;
				loc.extended    = p->type == PED_PARTITION_EXTENDED;
				snap.locations[loc.path] = loc;
				snap.partitions[dev_path].push_back(loc.path);
			}
		}
		if (disk)
			ped_disk_destroy(disk);
		ped_device_destroy(dev);
	}

	for (auto& kv : snap.locations)
	{
		const Location& loc = kv.second;
		if (loc.extended)
			continue;
		WinFsProbe::Result r;
		if (WinFsProbe::probe(reader_for(loc), loc.sector_size, loc.length / loc.sector_size, r))
			snap.probes[loc.path] = r;
	}

#if defined(_WIN32)
	for (const Win32Volumes::Volume& v : Win32Volumes::enumerate())
	{
		if (v.extents.size() != 1)
			continue;  // Spanned / striped volumes only exist on dynamic disks (read-only)
		const Win32Volumes::Extent& e = v.extents[0];
		for (const auto& kv : snap.locations)
		{
			const Location& loc = kv.second;
			if (loc.disk != static_cast<int>(e.disk) || loc.offset != e.offset || loc.length != e.length)
				continue;
			// A whole disk entry matches only a volume without partition table.
			if (loc.number == 0 && snap.partitions.count(loc.device_path))
				continue;
			VolumeState vs;
			vs.guid_path  = Win32Volumes::to_utf8(v.guid_path);
			for (const std::wstring& mp : v.mount_points)
				vs.mount_points.push_back(Win32Volumes::to_utf8(mp));
			vs.fs_name    = Win32Volumes::to_utf8(v.fs_name);
			vs.label      = Win32Volumes::to_utf8(v.label);
			vs.serial     = v.serial;
			vs.fs_mounted = v.fs_mounted;
			vs.read_only  = v.fs_read_only;
			vs.system     = v.system;
			vs.paging     = v.paging;
			auto pr = snap.probes.find(loc.path);
			vs.bitlocker  = pr != snap.probes.end() && pr->second.type == "BitLocker";
			snap.volumes[loc.path] = vs;
		}
	}
#endif

	std::lock_guard<std::mutex> lock(g_mutex);
	g_snapshot = snap;
}


std::vector<std::string> device_paths()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_snapshot.devices;
}


std::vector<std::string> partition_paths(const std::string& device_path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_snapshot.partitions.find(device_path);
	return it == g_snapshot.partitions.end() ? std::vector<std::string>() : it->second;
}


bool locate(const std::string& path, Location& out)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_snapshot.locations.find(path);
	if (it == g_snapshot.locations.end())
		return false;
	out = it->second;
	return true;
}


bool volume(const std::string& path, VolumeState& out)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_snapshot.volumes.find(path);
	if (it == g_snapshot.volumes.end())
		return false;
	out = it->second;
	return true;
}


bool probe(const std::string& path, WinFsProbe::Result& out)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_snapshot.probes.find(path);
	if (it == g_snapshot.probes.end())
		return false;
	out = it->second;
	return true;
}


bool busy(const std::string& path, std::string& reason)
{
	VolumeState v;
	if (! volume(path, v))
		return false;
	if (v.system)
		reason = "it holds the running Windows installation or its boot manager";
	else if (v.paging)
		reason = "it holds an active page, swap or hibernation file";
	else if (! v.mount_points.empty())
		reason = "it is mounted at " + v.mount_points.front();
	else
		return false;
	return true;
}


}  // namespace WinScan
}  // namespace WParted
