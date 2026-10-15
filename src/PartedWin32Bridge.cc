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

/* Implementation of the libparted API subset used by the GParted core, on top of
 * the platform independent WinDiskModel and a DiskBackend (Windows partition
 * manager in production, in-memory disks in the unit tests).
 *
 * Guarantees, in addition to libparted semantics:
 * *   A device path only ever maps to the disk number it names; unknown paths are
 *     rejected, never defaulted.
 * *   The disk behind a disk number must be the same physical disk that was seen
 *     when the devices were last scanned.
 * *   A partition table is only written when it is still exactly the table the
 *     change was computed from, when the new table validates, and every write is
 *     verified by reading it back (and rolled back on mismatch).
 * *   Operations which are not implemented fail; they never report success.
 */

#include "parted/parted.h"
#include "WinPartedBridge.h"
#include "WinDiskBackend.h"
#include "WinDiskModel.h"
#include "WinDevNames.h"
#include "WinFsProbe.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>


using namespace WParted;
using WinDisk::DiskInfo;
using WinDisk::DiskLayout;
using WinDisk::DiskModel;
using WinDisk::PartKind;
using WinDisk::PartRecord;
using WinDisk::TableStyle;


namespace
{

const char WPARTED_PARTED_VERSION[] = "WParted Windows partition layer " WPARTED_VERSION_STRING;

PedExceptionHandler* g_exception_handler = nullptr;
thread_local int     g_quiet = 0;

std::mutex                         g_mutex;
std::map<int, DiskInfo>            g_registry;
std::map<std::string, PedDevice*>  g_device_cache;
std::map<std::string, std::string> g_readonly_reasons;


struct DevData
{
	int      index = -1;
	DiskInfo info;
};

struct PartData
{
	PartRecord  rec;
	std::string name_utf8;   // Storage for ped_partition_get_name()
	bool        attached = false;
};

struct DiskData
{
	DiskModel                  header;     // Disk level fields only, parts empty
	DiskLayout                 snapshot;   // Layout the PedDisk was created from
	std::string                readonly_reason;
	std::vector<PedPartition*> parts;      // Owned, in libparted order
};


PedDiskType g_type_msdos = { nullptr,      "msdos", nullptr, PED_DISK_TYPE_EXTENDED };
PedDiskType g_type_gpt   = { &g_type_msdos, "gpt",   nullptr, PED_DISK_TYPE_PARTITION_NAME };
PedDiskType g_type_loop  = { nullptr,      "loop",  nullptr, static_cast<PedDiskTypeFeature>(0) };


PedFileSystemType g_fs_types[] = {
	{ nullptr, "ntfs",           nullptr },
	{ nullptr, "fat16",          nullptr },
	{ nullptr, "fat32",          nullptr },
	{ nullptr, "exfat",          nullptr },
	{ nullptr, "refs",           nullptr },
	{ nullptr, "udf",            nullptr },
	{ nullptr, "ext2",           nullptr },
	{ nullptr, "ext3",           nullptr },
	{ nullptr, "ext4",           nullptr },
	{ nullptr, "btrfs",          nullptr },
	{ nullptr, "xfs",            nullptr },
	{ nullptr, "jfs",            nullptr },
	{ nullptr, "reiserfs",       nullptr },
	{ nullptr, "f2fs",           nullptr },
	{ nullptr, "nilfs2",         nullptr },
	{ nullptr, "hfs",            nullptr },
	{ nullptr, "hfs+",           nullptr },
	{ nullptr, "hfsx",           nullptr },
	{ nullptr, "linux-swap",     nullptr },
	{ nullptr, "linux-swap(v1)", nullptr },
	{ nullptr, "linux-swap(v0)", nullptr },
};


const char* const flag_names[] = {
	nullptr, "boot", "root", "swap", "hidden", "raid", "lvm", "lba", "hp-service", "palo",
	"prep", "msftres", "bios_grub", "atvrecv", "diag", "legacy_boot", "msftdata", "irst",
	"esp", "chromeos_kernel", "bls_boot", "linux-home", "no_automount"
};


std::string vformat(const char* fmt, va_list ap)
{
	char buf[4096];
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	return std::string(buf);
}


void report(PedExceptionType type, const std::string& message)
{
	if (g_quiet > 0)
		return;
	ped_exception_throw(type, PED_EXCEPTION_CANCEL, "%s", message.c_str());
}


DevData* dev_data(const PedDevice* dev)
{
	return dev ? static_cast<DevData*>(dev->arch_specific) : nullptr;
}

DiskData* disk_data(const PedDisk* disk)
{
	return disk ? static_cast<DiskData*>(disk->disk_specific) : nullptr;
}

PartData* part_data(const PedPartition* part)
{
	return part ? static_cast<PartData*>(part->disk_specific) : nullptr;
}


char* dup_string(const std::string& s)
{
	char* p = static_cast<char*>(std::malloc(s.size() + 1));
	if (p)
		std::memcpy(p, s.c_str(), s.size() + 1);
	return p;
}


WinDisk::SectorReader make_reader(int index, uint32_t sector_size, uint64_t base_lba,
                                  uint64_t limit_sectors)
{
	return [index, sector_size, base_lba, limit_sectors](uint64_t lba, uint32_t count,
	                                                     std::vector<uint8_t>& out) -> bool
	{
		WinDisk::DiskBackend* backend = WinDisk::get_backend();
		if (! backend || count == 0 || lba + count > limit_sectors)
			return false;
		out.assign(size_t(count) * sector_size, 0);
		std::string error;
		return backend->read_sectors(index, base_lba + lba, count, sector_size, out.data(), error);
	};
}


// Verify the disk number still refers to the disk seen at the last device scan.
bool verify_identity(int index, DiskInfo& info)
{
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	std::string error;
	if (! backend || ! backend->query_disk(index, info, error))
	{
		report(PED_EXCEPTION_ERROR, "Unable to access disk " + std::to_string(index) +
		                            (error.empty() ? std::string() : ": " + error));
		return false;
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_registry.find(index);
	if (it == g_registry.end())
	{
		report(PED_EXCEPTION_ERROR, "Disk " + std::to_string(index) +
		                            " was not present when the devices were scanned.  "
		                            "Refresh the devices first.");
		return false;
	}
	if (! it->second.same_disk(info))
	{
		report(PED_EXCEPTION_ERROR, "Disk number " + std::to_string(index) +
		                            " now refers to a different disk (" + info.model + ", was " +
		                            it->second.model + ").  Nothing was changed.  Refresh the "
		                            "devices before applying operations.");
		return false;
	}
	return true;
}


PedDeviceType device_type_for_bus(const std::string& bus)
{
	if (bus == "NVMe")                return PED_DEVICE_NVME;
	if (bus == "SD" || bus == "MMC")  return PED_DEVICE_SDMMC;
	if (bus == "Virtual")             return PED_DEVICE_VIRTBLK;
	if (bus == "ATA")                 return PED_DEVICE_IDE;
	return PED_DEVICE_SCSI;
}


void fill_device(PedDevice* dev, int index, const DiskInfo& info)
{
	std::string model = info.vendor.empty() ? info.model : info.vendor + " " + info.model;
	if (model.empty())
		model = "Unknown disk";

	std::free(dev->model);
	std::free(dev->path);
	dev->model            = dup_string(model);
	dev->path             = dup_string(WinDevNames::disk_path(index));
	dev->type             = device_type_for_bus(info.bus);
	dev->sector_size      = info.logical_sector_size;
	dev->phys_sector_size = info.physical_sector_size;
	dev->length           = static_cast<PedSector>(info.size_bytes / info.logical_sector_size);
	dev->read_only        = (info.read_only || info.offline) ? 1 : 0;
	dev->bios_geom.heads   = 255;
	dev->bios_geom.sectors = 63;
	PedSector cyl = dev->length / (255 * 63);
	dev->bios_geom.cylinders = static_cast<int>(std::min<PedSector>(cyl, 0x7FFFFFFF));
	dev->hw_geom = dev->bios_geom;

	DevData* dd = dev_data(dev);
	dd->index = index;
	dd->info  = info;
}


// Rebuild libparted order, renumber logicals when requested and relink.
void relink(PedDisk* disk, bool renumber_logicals)
{
	DiskData* dd = disk_data(disk);
	std::vector<PedPartition*> tops, logicals;
	for (PedPartition* p : dd->parts)
		(p->type == PED_PARTITION_LOGICAL ? logicals : tops).push_back(p);
	auto by_start = [](const PedPartition* a, const PedPartition* b) { return a->geom.start < b->geom.start; };
	std::stable_sort(tops.begin(), tops.end(), by_start);
	std::stable_sort(logicals.begin(), logicals.end(), by_start);
	if (renumber_logicals)
	{
		int n = 5;
		for (PedPartition* p : logicals)
		{
			p->num = n++;
			part_data(p)->rec.num = p->num;
		}
	}

	dd->parts.clear();
	PedPartition* extended = nullptr;
	for (PedPartition* p : tops)
	{
		dd->parts.push_back(p);
		if (p->type == PED_PARTITION_EXTENDED)
		{
			extended = p;
			dd->parts.insert(dd->parts.end(), logicals.begin(), logicals.end());
		}
	}
	if (! extended)
		dd->parts.insert(dd->parts.end(), logicals.begin(), logicals.end());  // Orphans; validation rejects

	PedPartition* prev = nullptr;
	for (PedPartition* p : dd->parts)
	{
		p->prev = prev;
		p->next = nullptr;
		p->part_list = nullptr;
		if (prev)
			prev->next = p;
		prev = p;
	}
	if (extended)
		extended->part_list = logicals.empty() ? nullptr : logicals.front();
	disk->part_list = dd->parts.empty() ? nullptr : dd->parts.front();
}


// Model of the disk including the given extra (not attached) partition.
DiskModel model_of(const PedDisk* disk, const PedPartition* extra = nullptr,
                   PedSector extra_start = -1, PedSector extra_end = -1)
{
	DiskData* dd = disk_data(disk);
	DiskModel m = dd->header;
	m.parts.clear();
	for (const PedPartition* p : dd->parts)
	{
		PartRecord r = part_data(p)->rec;
		r.num   = p->num;
		r.start = p->geom.start;
		r.end   = p->geom.end;
		if (p == extra && extra_start >= 0)
		{
			r.start = extra_start;
			r.end   = extra_end;
		}
		m.parts.push_back(r);
	}
	if (extra && std::find(dd->parts.begin(), dd->parts.end(), extra) == dd->parts.end())
	{
		PartRecord r = part_data(extra)->rec;
		r.num   = extra->num;
		r.start = extra_start >= 0 ? extra_start : extra->geom.start;
		r.end   = extra_start >= 0 ? extra_end : extra->geom.end;
		m.parts.push_back(r);
	}
	return m;
}


// Copy type related fields of model records back into the attached partitions.
void write_back_types(PedDisk* disk, const DiskModel& m)
{
	DiskData* dd = disk_data(disk);
	for (size_t i = 0; i < dd->parts.size() && i < m.parts.size(); i++)
	{
		PartRecord& r = part_data(dd->parts[i])->rec;
		r.mbr_type   = m.parts[i].mbr_type;
		r.mbr_boot   = m.parts[i].mbr_boot;
		r.type_guid  = m.parts[i].type_guid;
		r.attributes = m.parts[i].attributes;
		r.type_flags = m.parts[i].type_flags;
	}
}


PedPartition* new_ped_partition(PedDisk* disk, PedPartitionType type, const PedFileSystemType* fs_type,
                                PedSector start, PedSector end)
{
	PedPartition* part = static_cast<PedPartition*>(std::calloc(1, sizeof(PedPartition)));
	if (! part)
		return nullptr;
	PartData* pd = new PartData();
	part->disk          = disk;
	part->type          = type;
	part->fs_type       = fs_type;
	part->num           = -1;
	part->geom.dev      = disk ? disk->dev : nullptr;
	part->geom.start    = start;
	part->geom.end      = end;
	part->geom.length   = end - start + 1;
	part->disk_specific = pd;
	return part;
}


void free_ped_partition(PedPartition* part)
{
	if (! part)
		return;
	delete part_data(part);
	std::free(part);
}


const char* fs_name(const PedPartition* part)
{
	return (part && part->fs_type) ? part->fs_type->name : nullptr;
}


int index_in_disk(const PedPartition* part)
{
	DiskData* dd = disk_data(part->disk);
	if (! dd)
		return -1;
	for (size_t i = 0; i < dd->parts.size(); i++)
		if (dd->parts[i] == part)
			return static_cast<int>(i);
	return -1;
}


PedDisk* new_ped_disk(PedDevice* dev, const PedDiskType* type)
{
	PedDisk* disk = static_cast<PedDisk*>(std::calloc(1, sizeof(PedDisk)));
	if (! disk)
		return nullptr;
	disk->dev = dev;
	disk->type = type;
	disk->disk_specific = new DiskData();
	return disk;
}


bool logical_chain_in_disk_order(const DiskModel& m)
{
	int64_t prev_start = -1;
	int prev_num = 0;
	for (const PartRecord& p : m.parts)
	{
		if (p.kind != PartKind::LOGICAL)
			continue;
		if (p.start < prev_start || (prev_num && p.num != prev_num + 1))
			return false;
		prev_start = p.start;
		prev_num = p.num;
	}
	return true;
}

}  // anonymous namespace


// ---------------------------------------------------------------------------
// WParted extensions
// ---------------------------------------------------------------------------

namespace WParted
{
namespace WinPartedBridge
{

void refresh_device_registry()
{
	std::map<int, DiskInfo> found;
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (backend)
	{
		for (int index : backend->enumerate_disks())
		{
			DiskInfo info;
			std::string error;
			if (backend->query_disk(index, info, error) && info.size_bytes > 0 &&
			    info.logical_sector_size >= 512)
				found[index] = info;
		}
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	g_registry.swap(found);
	g_readonly_reasons.clear();
}


std::vector<DiskInfo> registered_disks()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<DiskInfo> out;
	for (const auto& kv : g_registry)
		out.push_back(kv.second);
	return out;
}


int device_index(const PedDevice* dev)
{
	DevData* dd = dev_data(dev);
	return dd ? dd->index : -1;
}


bool device_info(const PedDevice* dev, DiskInfo& info)
{
	DevData* dd = dev_data(dev);
	if (! dd)
		return false;
	info = dd->info;
	return true;
}


std::string disk_readonly_reason(const PedDisk* disk)
{
	DiskData* dd = disk_data(disk);
	return dd ? dd->readonly_reason : std::string("Unknown disk");
}


std::string device_readonly_reason(const std::string& device_path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_readonly_reasons.find(device_path);
	return it == g_readonly_reasons.end() ? std::string() : it->second;
}


QuietScope::QuietScope()
{
	g_quiet++;
}


QuietScope::~QuietScope()
{
	g_quiet--;
}


bool partition_extent(const PedPartition* part, uint64_t& offset, uint64_t& length)
{
	if (! part || ! part->disk || ! part->disk->dev)
		return false;
	uint64_t ss = static_cast<uint64_t>(part->disk->dev->sector_size);
	offset = uint64_t(part->geom.start) * ss;
	length = uint64_t(part->geom.length) * ss;
	return true;
}

}  // namespace WinPartedBridge
}  // namespace WParted


// ---------------------------------------------------------------------------
// libparted API
// ---------------------------------------------------------------------------

const char* ped_get_version(void)
{
	return WPARTED_PARTED_VERSION;
}


void ped_exception_set_handler(PedExceptionHandler* handler)
{
	g_exception_handler = handler;
}


PedExceptionOption ped_exception_throw(PedExceptionType ex_type, PedExceptionOption ex_opt,
                                       const char* message, ...)
{
	va_list ap;
	va_start(ap, message);
	std::string text = vformat(message, ap);
	va_end(ap);

	if (! g_exception_handler)
	{
		std::cerr << text << std::endl;
		return PED_EXCEPTION_UNHANDLED;
	}
	PedException ex;
	ex.message = &text[0];
	ex.type    = ex_type;
	ex.options = ex_opt;
	return g_exception_handler(&ex);
}


// --- Devices ---------------------------------------------------------------

PedDevice* ped_device_get(const char* name)
{
	if (! name)
		return nullptr;
	int index = WinDevNames::disk_index(name);
	if (index < 0)
		return nullptr;

	DiskInfo info;
	if (! verify_identity(index, info))
		return nullptr;

	std::string path = WinDevNames::disk_path(index);
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_device_cache.find(path);
	PedDevice* dev;
	if (it != g_device_cache.end())
	{
		dev = it->second;
	}
	else
	{
		dev = static_cast<PedDevice*>(std::calloc(1, sizeof(PedDevice)));
		if (! dev)
			return nullptr;
		dev->arch_specific = new DevData();
		g_device_cache[path] = dev;
	}
	fill_device(dev, index, info);
	return dev;
}


PedDevice* ped_device_get_next(const PedDevice* dev)
{
	int after = dev ? WParted::WinPartedBridge::device_index(dev) : -1;
	for (;;)
	{
		int next = -1;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			auto it = g_registry.upper_bound(after);
			if (it != g_registry.end())
				next = it->first;
		}
		if (next < 0)
			return nullptr;
		PedDevice* found = ped_device_get(WinDevNames::disk_path(next).c_str());
		if (found)
			return found;
		after = next;
	}
}


int ped_device_open(PedDevice* dev)
{
	if (! dev)
		return 0;
	dev->open_count++;
	return 1;
}


int ped_device_close(PedDevice* dev)
{
	if (! dev || dev->open_count <= 0)
		return 0;
	dev->open_count--;
	return 1;
}


void ped_device_destroy(PedDevice* dev)
{
	if (! dev)
		return;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (auto it = g_device_cache.begin(); it != g_device_cache.end(); ++it)
		{
			if (it->second == dev)
			{
				g_device_cache.erase(it);
				break;
			}
		}
	}
	delete dev_data(dev);
	std::free(dev->model);
	std::free(dev->path);
	std::free(dev);
}


int ped_device_read(const PedDevice* dev, void* buffer, PedSector start, PedSector count)
{
	DevData* dd = dev_data(dev);
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! dd || ! buffer || ! backend || start < 0 || count <= 0 || start + count > dev->length)
		return 0;
	std::string error;
	if (! backend->read_sectors(dd->index, uint64_t(start), uint64_t(count),
	                            static_cast<uint32_t>(dev->sector_size), buffer, error))
	{
		report(PED_EXCEPTION_ERROR, std::string("Error reading ") + dev->path + " sectors " +
		                            std::to_string(start) + "-" + std::to_string(start + count - 1) +
		                            ": " + error);
		return 0;
	}
	return 1;
}


int ped_device_write(PedDevice* dev, const void* buffer, PedSector start, PedSector count)
{
	DevData* dd = dev_data(dev);
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! dd || ! buffer || ! backend || start < 0 || count <= 0 || start + count > dev->length)
		return 0;
	if (dev->read_only)
	{
		report(PED_EXCEPTION_ERROR, std::string("Can't write to ") + dev->path +
		                            ", because it is opened read-only.");
		return 0;
	}
	std::string error;
	if (! backend->write_sectors(dd->index, uint64_t(start), uint64_t(count),
	                             static_cast<uint32_t>(dev->sector_size), buffer, error))
	{
		report(PED_EXCEPTION_ERROR, std::string("Error writing ") + dev->path + " sectors " +
		                            std::to_string(start) + "-" + std::to_string(start + count - 1) +
		                            ": " + error);
		return 0;
	}
	return 1;
}


int ped_device_sync(PedDevice* dev)
{
	// Writes are flushed by the backend before they return.
	return dev ? 1 : 0;
}


// --- Disk types ------------------------------------------------------------

PedDiskType* ped_disk_type_get(const char* name)
{
	if (! name)
		return nullptr;
	if (std::strcmp(name, "gpt") == 0)
		return &g_type_gpt;
	if (std::strcmp(name, "msdos") == 0)
		return &g_type_msdos;
	return nullptr;
}


PedDiskType* ped_disk_type_get_next(const PedDiskType* type)
{
	if (! type)
		return &g_type_gpt;
	return type->next;
}


int ped_disk_type_check_feature(const PedDiskType* disk_type, PedDiskTypeFeature feature)
{
	return (disk_type && (disk_type->features & feature)) ? 1 : 0;
}


// --- Disks -----------------------------------------------------------------

PedDisk* ped_disk_new(PedDevice* dev)
{
	DevData* dd = dev_data(dev);
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! dd || ! backend)
		return nullptr;

	const uint32_t ss = static_cast<uint32_t>(dev->sector_size);
	DiskLayout layout;
	std::string error;
	if (! backend->read_layout(dd->index, layout, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(dev->path) + ": unable to read the partition table: " + error);
		return nullptr;
	}

	WinDisk::RawTable raw;
	std::string raw_error;
	bool raw_ok = WinDisk::read_raw_table(make_reader(dd->index, ss, 0, uint64_t(dev->length)), ss,
	                                      dev->length, raw, raw_error);

	DiskModel model;
	if (! WinDisk::layout_to_model(layout, ss, dev->length, raw_ok ? &raw : nullptr, model, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(dev->path) + ": " + error);
		return nullptr;
	}
	if (model.style == TableStyle::RAW)
	{
		// Same message as libparted so GParted shows its usual "unrecognised" state.
		report(PED_EXCEPTION_ERROR, std::string(dev->path) + ": unrecognised disk label");
		return nullptr;
	}

	std::string reason;
	if (dev->read_only)
		reason = dd->info.read_only_reason.empty() ? std::string("The disk is write protected or offline.")
		                                           : dd->info.read_only_reason;
	else if (! raw_ok)
		reason = "The on-disk partition table could not be verified: " + raw_error;
	else if (! WinDisk::cross_check(model, raw, error))
		reason = "Windows and the on-disk partition table disagree: " + error;
	else if (! logical_chain_in_disk_order(model))
		reason = "The logical partitions are not chained in disk order.";
	else
		reason = WinDisk::unsupported_reason(model, &raw);

	const PedDiskType* type = model.superfloppy ? &g_type_loop
	                        : (model.style == TableStyle::GPT ? &g_type_gpt : &g_type_msdos);
	PedDisk* disk = new_ped_disk(dev, type);
	if (! disk)
		return nullptr;
	DiskData* data = disk_data(disk);
	data->snapshot = layout;
	data->readonly_reason = reason;
	data->header = model;
	data->header.parts.clear();

	if (model.superfloppy)
	{
		PedPartition* part = new_ped_partition(disk, PED_PARTITION_NORMAL, nullptr, 0, dev->length - 1);
		part->num = 1;
		PartData* pd = part_data(part);
		pd->rec.num = 1;
		pd->rec.start = 0;
		pd->rec.end = dev->length - 1;
		pd->rec.existing = true;
		pd->attached = true;
		data->parts.push_back(part);
	}
	else
	{
		for (const PartRecord& rec : model.parts)
		{
			PedPartitionType t = rec.kind == PartKind::EXTENDED ? PED_PARTITION_EXTENDED
			                   : rec.kind == PartKind::LOGICAL  ? PED_PARTITION_LOGICAL
			                                                    : PED_PARTITION_NORMAL;
			const PedFileSystemType* fs_type = nullptr;
			if (rec.kind != PartKind::EXTENDED)
			{
				WinFsProbe::Result probe;
				if (WinFsProbe::probe(make_reader(dd->index, ss, uint64_t(rec.start), uint64_t(rec.length())),
				                      ss, uint64_t(rec.length()), probe))
				{
					std::string name = probe.libparted_name();
					if (! name.empty())
						fs_type = ped_file_system_type_get(name.c_str());
				}
			}
			PedPartition* part = new_ped_partition(disk, t, fs_type, rec.start, rec.end);
			part->num = rec.num;
			PartData* pd = part_data(part);
			pd->rec = rec;
			pd->name_utf8 = WinDisk::utf16_to_utf8(rec.name);
			pd->attached = true;
			data->parts.push_back(part);
		}
	}
	relink(disk, false);

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_readonly_reasons[dev->path] = reason;
	}
	return disk;
}


PedDisk* ped_disk_new_fresh(PedDevice* dev, const PedDiskType* disk_type)
{
	DevData* dd = dev_data(dev);
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! dd || ! backend || (disk_type != &g_type_gpt && disk_type != &g_type_msdos))
		return nullptr;

	DiskLayout current;
	std::string error;
	if (! backend->read_layout(dd->index, current, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(dev->path) + ": unable to read the partition table: " + error);
		return nullptr;
	}

	PedDisk* disk = new_ped_disk(dev, disk_type);
	if (! disk)
		return nullptr;
	DiskData* data = disk_data(disk);
	data->snapshot = current;
	if (dev->read_only)
		data->readonly_reason = dd->info.read_only_reason.empty() ? std::string("The disk is write protected or offline.")
		                                                          : dd->info.read_only_reason;

	DiskModel& h = data->header;
	h.sector_size   = static_cast<uint32_t>(dev->sector_size);
	h.total_sectors = dev->length;
	h.fresh         = true;
	if (disk_type == &g_type_gpt)
	{
		h.style = TableStyle::GPT;
		h.gpt_disk_id = WinDisk::generate_guid();
		h.gpt_max_entries = 128;
		int64_t entry_sectors = (128 * 128 + h.sector_size - 1) / h.sector_size;
		h.first_usable = 2 + entry_sectors;
		h.last_usable  = h.total_sectors - 2 - entry_sectors;
	}
	else
	{
		h.style = TableStyle::MBR;
		uint32_t sig = 0;
		while (sig == 0)
		{
			WinDisk::Guid g = WinDisk::generate_guid();
			sig = g.data1;
		}
		h.mbr_signature = sig;
		h.first_usable = 1;
		h.last_usable  = h.total_sectors - 1;
	}
	relink(disk, false);
	return disk;
}


void ped_disk_destroy(PedDisk* disk)
{
	if (! disk)
		return;
	DiskData* dd = disk_data(disk);
	if (dd)
	{
		for (PedPartition* p : dd->parts)
			free_ped_partition(p);
		delete dd;
	}
	std::free(disk);
}


int ped_disk_commit_to_dev(PedDisk* disk)
{
	DiskData* data = disk_data(disk);
	DevData* dd = disk ? dev_data(disk->dev) : nullptr;
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! data || ! dd || ! backend)
		return 0;
	const char* path = disk->dev->path;
	const uint32_t ss = static_cast<uint32_t>(disk->dev->sector_size);

	if (! data->readonly_reason.empty())
	{
		report(PED_EXCEPTION_ERROR, std::string(path) + ": not modified: " + data->readonly_reason);
		return 0;
	}

	DiskInfo info;
	if (! verify_identity(dd->index, info))
		return 0;

	// The table on disk must still be the one this change was computed from.
	DiskLayout current;
	std::string error, difference;
	if (! backend->read_layout(dd->index, current, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(path) + ": unable to re-read the partition table: " + error);
		return 0;
	}
	if (! WinDisk::layouts_equivalent(current, data->snapshot, ss, difference))
	{
		report(PED_EXCEPTION_ERROR, std::string(path) + ": the partition table was changed by another "
		                            "program since it was read (" + difference + ").  Nothing was "
		                            "written.  Refresh the devices and try again.");
		return 0;
	}

	DiskModel target_model = model_of(disk);
	DiskLayout target;
	if (! WinDisk::model_to_layout(target_model, target, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(path) + ": invalid partition table, nothing was written: " + error);
		return 0;
	}
	if (! target_model.fresh && WinDisk::layouts_equivalent(current, target, ss, difference))
		return 1;  // Nothing changed on disk

	if (! backend->write_layout(dd->index, current, target, target_model.fresh, error))
	{
		report(PED_EXCEPTION_ERROR, std::string(path) + ": unable to write the partition table: " + error);
		return 0;
	}

	// Verify by reading back both the Windows view and the raw on-disk structures.
	DiskLayout after;
	DiskModel after_model;
	WinDisk::RawTable raw;
	std::string raw_error;
	bool verified = backend->read_layout(dd->index, after, error) &&
	                WinDisk::layout_to_model(after, ss, disk->dev->length, nullptr, after_model, error) &&
	                WinDisk::models_equivalent(target_model, after_model, false, error);
	if (verified)
	{
		bool raw_ok = WinDisk::read_raw_table(make_reader(dd->index, ss, 0, uint64_t(disk->dev->length)),
		                                      ss, disk->dev->length, raw, raw_error);
		verified = raw_ok && WinDisk::cross_check(after_model, raw, error);
		if (! raw_ok)
			error = raw_error;
	}
	if (! verified)
	{
		std::string message = std::string(path) + ": the partition table read back after writing "
		                      "does not match what was written (" + error + ").";
		std::string restore_error;
		DiskLayout now;
		if (! target_model.fresh && backend->read_layout(dd->index, now, restore_error) &&
		    backend->write_layout(dd->index, now, data->snapshot, false, restore_error))
			message += "  The previous partition table was restored.";
		else
			message += "  The previous partition table could NOT be restored" +
			           (restore_error.empty() ? std::string(".") : ": " + restore_error);
		report(PED_EXCEPTION_ERROR, message);
		return 0;
	}

	data->snapshot = after;
	DiskModel new_header;
	if (WinDisk::layout_to_model(after, ss, disk->dev->length, &raw, new_header, error))
	{
		new_header.parts.clear();
		data->header = new_header;
	}
	data->header.fresh = false;
	return 1;
}


int ped_disk_commit_to_os(PedDisk* disk)
{
	DevData* dd = disk ? dev_data(disk->dev) : nullptr;
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! dd || ! backend)
		return 0;
	std::string error;
	if (! backend->update_properties(dd->index, error))
	{
		report(PED_EXCEPTION_WARNING, std::string(disk->dev->path) +
		                              ": Windows could not be told to re-read the partition table: " + error);
		return 0;
	}
	return 1;
}


int ped_disk_commit(PedDisk* disk)
{
	return ped_disk_commit_to_dev(disk) && ped_disk_commit_to_os(disk);
}


PedPartition* ped_disk_next_partition(const PedDisk* disk, const PedPartition* part)
{
	if (! disk)
		return nullptr;
	if (! part)
		return disk->part_list;
	return part->next;
}


PedPartition* ped_disk_get_partition_by_sector(const PedDisk* disk, PedSector sect)
{
	DiskData* dd = disk_data(disk);
	if (! dd)
		return nullptr;
	// Logical partitions take precedence over their extended partition.
	for (PedPartition* p : dd->parts)
		if (p->type == PED_PARTITION_LOGICAL && sect >= p->geom.start && sect <= p->geom.end)
			return p;
	for (PedPartition* p : dd->parts)
		if (p->type != PED_PARTITION_LOGICAL && sect >= p->geom.start && sect <= p->geom.end)
			return p;
	return nullptr;
}


PedPartition* ped_disk_extended_partition(const PedDisk* disk)
{
	DiskData* dd = disk_data(disk);
	if (! dd)
		return nullptr;
	for (PedPartition* p : dd->parts)
		if (p->type == PED_PARTITION_EXTENDED)
			return p;
	return nullptr;
}


int ped_disk_get_max_primary_partition_count(const PedDisk* disk)
{
	if (! disk)
		return 0;
	if (disk->type == &g_type_gpt)
	{
		DiskData* dd = disk_data(disk);
		return dd ? static_cast<int>(dd->header.gpt_max_entries) : 128;
	}
	if (disk->type == &g_type_msdos)
		return 4;
	return 1;
}


int ped_disk_add_partition(PedDisk* disk, PedPartition* part, const PedConstraint* constraint)
{
	DiskData* dd = disk_data(disk);
	PartData* pd = part_data(part);
	if (! dd || ! pd || pd->attached || part->disk != disk)
		return 0;
	if (disk->type == &g_type_loop)
	{
		report(PED_EXCEPTION_ERROR, "Partitions can not be added to a disk without a partition table.");
		return 0;
	}
	if (constraint && part->geom.length < constraint->min_size)
	{
		report(PED_EXCEPTION_ERROR, "The partition is smaller than the minimum size required.");
		return 0;
	}

	// Partition number, as libparted assigns it.
	int number = -1;
	if (dd->header.style == TableStyle::GPT)
	{
		for (int n = 1; n <= static_cast<int>(dd->header.gpt_max_entries) && number < 0; n++)
		{
			bool used = false;
			for (PedPartition* p : dd->parts)
				used = used || p->num == n;
			if (! used)
				number = n;
		}
		if (pd->rec.unique_guid.is_null())
			pd->rec.unique_guid = WinDisk::generate_guid();
	}
	else if (part->type == PED_PARTITION_LOGICAL)
	{
		number = 5 + static_cast<int>(std::count_if(dd->parts.begin(), dd->parts.end(),
		                              [](const PedPartition* p) { return p->type == PED_PARTITION_LOGICAL; }));
	}
	else
	{
		for (int n = 1; n <= 4 && number < 0; n++)
		{
			bool used = false;
			for (PedPartition* p : dd->parts)
				used = used || (p->type != PED_PARTITION_LOGICAL && p->num == n);
			if (! used)
				number = n;
		}
	}
	if (number < 0)
	{
		report(PED_EXCEPTION_ERROR, "Too many primary partitions.");
		return 0;
	}

	part->num = number;
	pd->rec.num = number;
	DiskModel candidate = model_of(disk, part);
	std::string error;
	if (part->type == PED_PARTITION_LOGICAL)
	{
		// Validate with logical numbering by position, as it will be after relink.
		candidate.sort_parts();
		int n = 5;
		for (PartRecord& r : candidate.parts)
			if (r.kind == PartKind::LOGICAL)
				r.num = n++;
	}
	if (! WinDisk::validate_model(candidate, error))
	{
		part->num = -1;
		pd->rec.num = 0;
		report(PED_EXCEPTION_ERROR, "Unable to add the partition: " + error);
		return 0;
	}

	pd->attached = true;
	dd->parts.push_back(part);
	relink(disk, part->type == PED_PARTITION_LOGICAL);
	return 1;
}


int ped_disk_delete_partition(PedDisk* disk, PedPartition* part)
{
	DiskData* dd = disk_data(disk);
	if (! dd || ! part)
		return 0;
	auto it = std::find(dd->parts.begin(), dd->parts.end(), part);
	if (it == dd->parts.end())
		return 0;
	if (part->type == PED_PARTITION_EXTENDED)
	{
		for (PedPartition* p : dd->parts)
		{
			if (p->type == PED_PARTITION_LOGICAL)
			{
				report(PED_EXCEPTION_ERROR, "The extended partition still contains logical partitions.");
				return 0;
			}
		}
	}
	bool logical = part->type == PED_PARTITION_LOGICAL;
	dd->parts.erase(it);
	free_ped_partition(part);
	relink(disk, logical);
	return 1;
}


int ped_disk_set_partition_geom(PedDisk* disk, PedPartition* part, const PedConstraint* constraint,
                                PedSector start, PedSector end)
{
	DiskData* dd = disk_data(disk);
	if (! dd || ! part || index_in_disk(part) < 0 || start < 0 || end < start)
		return 0;
	if (constraint && end - start + 1 < constraint->min_size)
	{
		report(PED_EXCEPTION_ERROR, "The partition is smaller than the minimum size required.");
		return 0;
	}

	DiskModel candidate = model_of(disk, part, start, end);
	if (part->type == PED_PARTITION_LOGICAL)
	{
		candidate.sort_parts();
		int n = 5;
		for (PartRecord& r : candidate.parts)
			if (r.kind == PartKind::LOGICAL)
				r.num = n++;
	}
	std::string error;
	if (! WinDisk::validate_model(candidate, error))
	{
		report(PED_EXCEPTION_ERROR, "Unable to set the partition boundaries: " + error);
		return 0;
	}

	part->geom.start  = start;
	part->geom.end    = end;
	part->geom.length = end - start + 1;
	PartData* pd = part_data(part);
	pd->rec.start = start;
	pd->rec.end   = end;
	if (part->type == PED_PARTITION_LOGICAL)
		pd->rec.ebr_sector = -1;  // Recompute the EBR position for the new geometry
	relink(disk, part->type == PED_PARTITION_LOGICAL);
	return 1;
}


// --- Partitions ------------------------------------------------------------

PedPartition* ped_partition_new(const PedDisk* disk, PedPartitionType type, const PedFileSystemType* fs_type,
                                PedSector start, PedSector end)
{
	DiskData* dd = disk_data(disk);
	if (! dd || start < 0 || end < start)
		return nullptr;
	if (dd->header.style == TableStyle::GPT &&
	    (type == PED_PARTITION_LOGICAL || type == PED_PARTITION_EXTENDED))
	{
		report(PED_EXCEPTION_ERROR, "GPT partition tables do not support extended or logical partitions.");
		return nullptr;
	}
	if (type != PED_PARTITION_NORMAL && type != PED_PARTITION_LOGICAL && type != PED_PARTITION_EXTENDED)
		return nullptr;

	PedPartition* part = new_ped_partition(const_cast<PedDisk*>(disk), type, fs_type, start, end);
	if (! part)
		return nullptr;
	PartData* pd = part_data(part);
	pd->rec.kind  = type == PED_PARTITION_EXTENDED ? PartKind::EXTENDED
	              : type == PED_PARTITION_LOGICAL  ? PartKind::LOGICAL
	                                               : PartKind::PRIMARY;
	pd->rec.start = start;
	pd->rec.end   = end;

	DiskModel m = dd->header;
	m.parts.clear();
	WinDisk::set_system(m, pd->rec, fs_name(part));
	return part;
}


void ped_partition_destroy(PedPartition* part)
{
	PartData* pd = part_data(part);
	if (pd && pd->attached)
		return;  // Owned by its disk
	free_ped_partition(part);
}


char* ped_partition_get_path(const PedPartition* part)
{
	if (! part || ! part->disk || ! part->disk->dev || ! part->disk->dev->path)
		return nullptr;
	return dup_string(WinDevNames::partition_path(part->disk->dev->path, part->num));
}


const char* ped_partition_get_name(const PedPartition* part)
{
	PartData* pd = part_data(part);
	if (! pd || ! part->disk || part->disk->type != &g_type_gpt)
		return "";
	return pd->name_utf8.c_str();
}


int ped_partition_set_name(PedPartition* part, const char* name)
{
	PartData* pd = part_data(part);
	if (! pd || ! name || ! part->disk || part->disk->type != &g_type_gpt)
	{
		report(PED_EXCEPTION_ERROR, "Partition names are only supported on GPT partition tables.");
		return 0;
	}
	std::u16string u = WinDisk::utf8_to_utf16(name);
	if (u.size() > 36)
	{
		report(PED_EXCEPTION_ERROR, "GPT partition names are limited to 36 characters.");
		return 0;
	}
	pd->rec.name = u;
	pd->name_utf8 = name;
	return 1;
}


int ped_partition_is_flag_available(const PedPartition* part, PedPartitionFlag flag)
{
	if (! part || ! part->disk || part->disk->type == &g_type_loop)
		return 0;
	DiskData* dd = disk_data(part->disk);
	return WinDisk::flag_is_available(dd->header, part_data(part)->rec, flag) ? 1 : 0;
}


int ped_partition_get_flag(const PedPartition* part, PedPartitionFlag flag)
{
	if (! part || ! part->disk || part->disk->type == &g_type_loop)
		return 0;
	DiskData* dd = disk_data(part->disk);
	return WinDisk::flag_get(dd->header, part_data(part)->rec, flag) ? 1 : 0;
}


int ped_partition_set_flag(PedPartition* part, PedPartitionFlag flag, int state)
{
	if (! part || ! part->disk || part->disk->type == &g_type_loop)
		return 0;
	int idx = index_in_disk(part);
	if (idx < 0)
		return 0;
	DiskModel m = model_of(part->disk);
	if (! WinDisk::flag_set(m, m.parts[idx], flag, state != 0, fs_name(part)))
	{
		report(PED_EXCEPTION_ERROR, std::string("The flag '") + ped_partition_flag_get_name(flag) +
		                            "' is not available for this partition.");
		return 0;
	}
	write_back_types(part->disk, m);
	return 1;
}


int ped_partition_set_system(PedPartition* part, const PedFileSystemType* fs_type)
{
	if (! part || ! part->disk || part->disk->type == &g_type_loop)
		return 0;
	part->fs_type = fs_type;
	int idx = index_in_disk(part);
	if (idx < 0)
	{
		DiskModel m = disk_data(part->disk)->header;
		m.parts.clear();
		WinDisk::set_system(m, part_data(part)->rec, fs_name(part));
		return 1;
	}
	DiskModel m = model_of(part->disk);
	WinDisk::set_system(m, m.parts[idx], fs_name(part));
	write_back_types(part->disk, m);
	return 1;
}


const char* ped_partition_flag_get_name(PedPartitionFlag flag)
{
	if (flag < PED_PARTITION_FIRST_FLAG || flag > PED_PARTITION_LAST_FLAG)
		return "unknown";
	return flag_names[flag];
}


PedPartitionFlag ped_partition_flag_get_by_name(const char* name)
{
	if (name)
		for (int f = PED_PARTITION_FIRST_FLAG; f <= PED_PARTITION_LAST_FLAG; f++)
			if (std::strcmp(name, flag_names[f]) == 0)
				return static_cast<PedPartitionFlag>(f);
	return static_cast<PedPartitionFlag>(0);
}


PedPartitionFlag ped_partition_flag_next(PedPartitionFlag flag)
{
	int next = static_cast<int>(flag) + 1;
	if (next > PED_PARTITION_LAST_FLAG)
		return static_cast<PedPartitionFlag>(0);
	return static_cast<PedPartitionFlag>(next);
}


// --- Geometry and constraints ----------------------------------------------

PedGeometry* ped_geometry_new(const PedDevice* dev, PedSector start, PedSector length)
{
	if (! dev || start < 0 || length <= 0 || start + length > dev->length)
		return nullptr;
	PedGeometry* g = static_cast<PedGeometry*>(std::calloc(1, sizeof(PedGeometry)));
	if (! g)
		return nullptr;
	g->dev    = const_cast<PedDevice*>(dev);
	g->start  = start;
	g->length = length;
	g->end    = start + length - 1;
	return g;
}


PedGeometry* ped_geometry_duplicate(const PedGeometry* geom)
{
	return geom ? ped_geometry_new(geom->dev, geom->start, geom->length) : nullptr;
}


void ped_geometry_destroy(PedGeometry* geom)
{
	std::free(geom);
}


int ped_geometry_write(PedGeometry* geom, const void* buffer, PedSector offset, PedSector count)
{
	if (! geom || offset < 0 || count <= 0 || offset + count > geom->length)
		return 0;
	return ped_device_write(geom->dev, buffer, geom->start + offset, count);
}


PedConstraint* ped_constraint_any(const PedDevice* dev)
{
	if (! dev)
		return nullptr;
	PedConstraint* c = static_cast<PedConstraint*>(std::calloc(1, sizeof(PedConstraint)));
	if (c)
		c->max_size = dev->length;
	return c;
}


PedConstraint* ped_constraint_exact(const PedGeometry* geom)
{
	if (! geom)
		return nullptr;
	PedConstraint* c = static_cast<PedConstraint*>(std::calloc(1, sizeof(PedConstraint)));
	if (c)
	{
		c->start_range = ped_geometry_duplicate(geom);
		c->end_range   = ped_geometry_duplicate(geom);
		c->min_size    = geom->length;
		c->max_size    = geom->length;
	}
	return c;
}


void ped_constraint_destroy(PedConstraint* constraint)
{
	if (! constraint)
		return;
	ped_geometry_destroy(constraint->start_range);
	ped_geometry_destroy(constraint->end_range);
	std::free(constraint);
}


// --- File systems ------------------------------------------------------------
// WParted never uses libparted file system code.  Opening a file system always
// fails so the core can not mistakenly think libparted can resize anything.

PedFileSystemType* ped_file_system_type_get(const char* name)
{
	if (! name)
		return nullptr;
	for (PedFileSystemType& t : g_fs_types)
		if (std::strcmp(t.name, name) == 0)
			return &t;
	return nullptr;
}


PedFileSystem* ped_file_system_open(PedGeometry* geom)
{
	(void)geom;
	return nullptr;
}


int ped_file_system_close(PedFileSystem* fs)
{
	std::free(fs);
	return 1;
}


int ped_file_system_resize(PedFileSystem* fs, PedGeometry* geom, void* timer)
{
	(void)fs; (void)geom; (void)timer;
	report(PED_EXCEPTION_NO_FEATURE, "File system resizing through libparted is not supported by WParted.");
	return 0;
}


PedConstraint* ped_file_system_get_resize_constraint(const PedFileSystem* fs)
{
	(void)fs;
	return nullptr;
}
