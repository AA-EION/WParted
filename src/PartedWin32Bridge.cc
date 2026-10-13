/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 * Clean Room Win32 Storage Engine Bridge for GNU Parted ABI.
 */

#include "parted/parted.h"
#include "Win32StorageEngine.h"
#include "DualNomenclature.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#endif

using namespace WParted;

static const char* WPARTED_VERSION_STR = "WParted 1.0.0 (Pure Win32 Storage Engine)";

static PedDiskType g_diskTypeGpt = { nullptr, "gpt", static_cast<PedDiskTypeFeature>(PED_DISK_TYPE_PARTITION_NAME) };
static PedDiskType g_diskTypeMsdos = { &g_diskTypeGpt, "msdos", static_cast<PedDiskTypeFeature>(PED_DISK_TYPE_EXTENDED) };

static PedFileSystemType g_fsTypes[] = {
    { "ntfs" },
    { "fat32" },
    { "fat16" },
    { "exfat" },
    { "ext4" },
    { "ext3" },
    { "ext2" },
    { "btrfs" },
    { "xfs" },
    { "f2fs" },
    { "linux-swap" },
    { "apfs" },
    { "hfs+" },
    { "reiserfs" },
    { nullptr }
};

static std::vector<Win32DiskDrive> g_cachedDrives;
static size_t g_currentDriveIndex = 0;

const char* ped_get_version(void)
{
    return WPARTED_VERSION_STR;
}

static int ParseDiskIndexFromPath(const char* path)
{
    if (!path) return -1;
    std::string p(path);
    // \\.\PhysicalDriveN
    size_t pos = p.find("PhysicalDrive");
    if (pos != std::string::npos) {
        return std::atoi(p.c_str() + pos + 13);
    }
    // /dev/sda, /dev/sdb...
    if (p.rfind("/dev/sd", 0) == 0 && p.length() >= 8) {
        return p[7] - 'a';
    }
    return 0;
}

static PedDevice* CreatePedDeviceFromDrive(const Win32DiskDrive& drive)
{
    PedDevice* dev = static_cast<PedDevice*>(std::calloc(1, sizeof(PedDevice)));
    if (!dev) return nullptr;

    std::string modelStr = drive.model.empty() ? "Storage Device" : drive.model;
    dev->model = _strdup(modelStr.c_str());
    dev->path = _strdup(drive.linuxDevPath.c_str());
    dev->sector_size = drive.logicalSectorSize ? drive.logicalSectorSize : 512;
    dev->phys_sector_size = drive.physicalSectorSize ? drive.physicalSectorSize : 512;
    dev->length = drive.sectorCount;
    dev->type = PED_DEVICE_SCSI;
    return dev;
}

PedDevice* ped_device_get(const char* name)
{
    int idx = ParseDiskIndexFromPath(name);
    if (idx < 0) return nullptr;

    Win32DiskDrive drive;
    if (Win32StorageEngine::Instance().QueryDiskProperties(idx, drive)) {
        return CreatePedDeviceFromDrive(drive);
    }
    return nullptr;
}

PedDevice* ped_device_get_next(const PedDevice* dev)
{
    if (!dev) {
        g_cachedDrives = Win32StorageEngine::Instance().EnumerateDisks();
        g_currentDriveIndex = 0;
    } else {
        g_currentDriveIndex++;
    }

    if (g_currentDriveIndex < g_cachedDrives.size()) {
        return CreatePedDeviceFromDrive(g_cachedDrives[g_currentDriveIndex]);
    }
    return nullptr;
}

int ped_device_open(PedDevice* dev)
{
    if (!dev) return 0;
    dev->open_count++;
    return 1;
}

int ped_device_close(PedDevice* dev)
{
    if (!dev) return 0;
    if (dev->open_count > 0) dev->open_count--;
    return 1;
}

void ped_device_destroy(PedDevice* dev)
{
    if (!dev) return;
    if (dev->model) std::free(dev->model);
    if (dev->path) std::free(dev->path);
    std::free(dev);
}

int ped_device_read(const PedDevice* dev, void* buffer, PedSector start, PedSector count)
{
#if defined(_WIN32)
    if (!dev || !buffer) return 0;
    int idx = ParseDiskIndexFromPath(dev->path);
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(idx);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER offset;
    offset.QuadPart = start * dev->sector_size;
    SetFilePointerEx(h, offset, NULL, FILE_BEGIN);

    DWORD bytesToRead = static_cast<DWORD>(count * dev->sector_size);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, buffer, bytesToRead, &bytesRead, NULL);
    CloseHandle(h);
    return (ok && bytesRead == bytesToRead) ? 1 : 0;
#else
    (void)dev; (void)buffer; (void)start; (void)count;
    return 1;
#endif
}

int ped_device_write(PedDevice* dev, const void* buffer, PedSector start, PedSector count)
{
#if defined(_WIN32)
    if (!dev || !buffer) return 0;
    int idx = ParseDiskIndexFromPath(dev->path);
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(idx);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER offset;
    offset.QuadPart = start * dev->sector_size;
    SetFilePointerEx(h, offset, NULL, FILE_BEGIN);

    DWORD bytesToWrite = static_cast<DWORD>(count * dev->sector_size);
    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(h, buffer, bytesToWrite, &bytesWritten, NULL);
    CloseHandle(h);
    return (ok && bytesWritten == bytesToWrite) ? 1 : 0;
#else
    (void)dev; (void)buffer; (void)start; (void)count;
    return 1;
#endif
}

int ped_device_sync(PedDevice* dev)
{
    (void)dev;
    return 1;
}

PedDiskType* ped_disk_type_get(const char* name)
{
    if (!name) return nullptr;
    if (std::strcmp(name, "gpt") == 0) return &g_diskTypeGpt;
    if (std::strcmp(name, "msdos") == 0) return &g_diskTypeMsdos;
    return &g_diskTypeGpt;
}

PedDiskType* ped_disk_type_get_next(const PedDiskType* disk_type)
{
    if (!disk_type) return &g_diskTypeMsdos;
    return disk_type->next;
}

int ped_disk_type_check_feature(const PedDiskType* disk_type, PedDiskTypeFeature feature)
{
    if (!disk_type) return 0;
    return (disk_type->features & feature) ? 1 : 0;
}

static const PedFileSystemType* FindFsType(const std::string& name)
{
    for (size_t i = 0; g_fsTypes[i].name != nullptr; ++i) {
        if (name == g_fsTypes[i].name) return &g_fsTypes[i];
    }
    return nullptr;
}

PedDisk* ped_disk_new(PedDevice* dev)
{
    if (!dev) return nullptr;
    int idx = ParseDiskIndexFromPath(dev->path);

    Win32DiskDrive drive;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(idx, drive)) {
        return nullptr;
    }
    Win32StorageEngine::Instance().ReadDriveLayout(idx, drive);

    PedDisk* disk = static_cast<PedDisk*>(std::calloc(1, sizeof(PedDisk)));
    if (!disk) return nullptr;

    disk->dev = dev;
    disk->type = drive.isGpt ? &g_diskTypeGpt : &g_diskTypeMsdos;

    PedPartition* prevPart = nullptr;
    for (const auto& p : drive.partitions) {
        PedPartition* part = static_cast<PedPartition*>(std::calloc(1, sizeof(PedPartition)));
        if (!part) continue;

        part->disk = disk;
        part->num = p.partitionNumber;
        part->type = PED_PARTITION_NORMAL;
        part->geom.dev = dev;
        part->geom.start = p.startingOffset / dev->sector_size;
        part->geom.length = p.partitionLength / dev->sector_size;
        part->geom.end = part->geom.start + part->geom.length - 1;

        std::string fsStr;
        for (wchar_t wc : p.fileSystemName) {
            if (wc >= 32 && wc <= 126) fsStr += static_cast<char>(std::tolower(wc));
        }
        part->fs_type = FindFsType(fsStr);

        if (!disk->part_list) {
            disk->part_list = part;
        } else if (prevPart) {
            prevPart->next = part;
            part->prev = prevPart;
        }
        prevPart = part;
    }

    return disk;
}

PedDisk* ped_disk_new_fresh(PedDevice* dev, const PedDiskType* type)
{
    if (!dev) return nullptr;
    int idx = ParseDiskIndexFromPath(dev->path);
    bool asGpt = (type && std::strcmp(type->name, "gpt") == 0);
    Win32StorageEngine::Instance().InitializeDisk(idx, asGpt);

    PedDisk* disk = static_cast<PedDisk*>(std::calloc(1, sizeof(PedDisk)));
    if (!disk) return nullptr;

    disk->dev = dev;
    disk->type = type ? type : &g_diskTypeGpt;
    return disk;
}

void ped_disk_destroy(PedDisk* disk)
{
    if (!disk) return;
    PedPartition* cur = disk->part_list;
    while (cur) {
        PedPartition* next = cur->next;
        std::free(cur);
        cur = next;
    }
    std::free(disk);
}

int ped_disk_commit_to_dev(PedDisk* disk)
{
    if (!disk || !disk->dev) return 0;
    int idx = ParseDiskIndexFromPath(disk->dev->path);

    Win32DiskDrive drive;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(idx, drive)) return 0;
    Win32StorageEngine::Instance().ReadDriveLayout(idx, drive);

    drive.isGpt = (disk->type && std::strcmp(disk->type->name, "gpt") == 0);
    drive.isMbr = !drive.isGpt;
    drive.partitions.clear();

    PedPartition* cur = disk->part_list;
    while (cur) {
        if (cur->type == PED_PARTITION_NORMAL) {
            Win32DiskPartition part;
            part.partitionNumber = cur->num;
            part.startingOffset = cur->geom.start * disk->dev->sector_size;
            part.partitionLength = cur->geom.length * disk->dev->sector_size;
            part.isGpt = drive.isGpt;
            part.gptTypeGuid = GUID_PARTITION_BASIC_DATA;
            CoCreateGuid(&part.gptIdGuid);
            drive.partitions.push_back(part);
        }
        cur = cur->next;
    }

    return Win32StorageEngine::Instance().WriteDriveLayout(idx, drive) ? 1 : 0;
}

int ped_disk_commit_to_os(PedDisk* disk)
{
    if (!disk || !disk->dev) return 0;
    int idx = ParseDiskIndexFromPath(disk->dev->path);
    return Win32StorageEngine::Instance().UpdateDiskProperties(idx) ? 1 : 0;
}

int ped_disk_commit(PedDisk* disk)
{
    return ped_disk_commit_to_dev(disk) && ped_disk_commit_to_os(disk);
}

PedPartition* ped_disk_next_partition(const PedDisk* disk, const PedPartition* part)
{
    if (!disk) return nullptr;
    if (!part) return disk->part_list;
    return part->next;
}

PedPartition* ped_disk_get_partition_by_sector(const PedDisk* disk, PedSector sect)
{
    if (!disk) return nullptr;
    PedPartition* cur = disk->part_list;
    while (cur) {
        if (sect >= cur->geom.start && sect <= cur->geom.end) return cur;
        cur = cur->next;
    }
    return nullptr;
}

PedPartition* ped_disk_extended_partition(const PedDisk* disk)
{
    (void)disk;
    return nullptr;
}

int ped_disk_get_max_primary_partition_count(const PedDisk* disk)
{
    if (disk && disk->type && std::strcmp(disk->type->name, "gpt") == 0) return 128;
    return 4;
}

int ped_disk_add_partition(PedDisk* disk, PedPartition* part, const PedConstraint* constraint)
{
    (void)constraint;
    if (!disk || !part) return 0;

    part->next = disk->part_list;
    if (disk->part_list) disk->part_list->prev = part;
    disk->part_list = part;
    part->disk = disk;
    return 1;
}

int ped_disk_delete_partition(PedDisk* disk, PedPartition* part)
{
    if (!disk || !part) return 0;
    if (part->prev) part->prev->next = part->next;
    if (part->next) part->next->prev = part->prev;
    if (disk->part_list == part) disk->part_list = part->next;
    std::free(part);
    return 1;
}

int ped_disk_set_partition_geom(PedDisk* disk, PedPartition* part, const PedConstraint* constraint, PedSector start, PedSector end)
{
    (void)disk; (void)constraint;
    if (!part) return 0;
    part->geom.start = start;
    part->geom.end = end;
    part->geom.length = (end >= start) ? (end - start + 1) : 0;
    return 1;
}

PedPartition* ped_partition_new(const PedDisk* disk, PedPartitionType type, const PedFileSystemType* fs_type, PedSector start, PedSector end)
{
    PedPartition* part = static_cast<PedPartition*>(std::calloc(1, sizeof(PedPartition)));
    if (!part) return nullptr;

    part->disk = const_cast<PedDisk*>(disk);
    part->type = type;
    part->fs_type = fs_type;
    part->geom.dev = disk ? disk->dev : nullptr;
    part->geom.start = start;
    part->geom.end = end;
    part->geom.length = (end >= start) ? (end - start + 1) : 0;
    return part;
}

void ped_partition_destroy(PedPartition* part)
{
    if (part) std::free(part);
}

const char* ped_partition_get_path(const PedPartition* part)
{
    if (!part || !part->disk || !part->disk->dev) return "";
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%d", part->disk->dev->path, part->num);
    return buf;
}

const char* ped_partition_get_name(const PedPartition* part)
{
    (void)part;
    return "Basic Data Partition";
}

int ped_partition_set_name(PedPartition* part, const char* name)
{
    (void)part; (void)name;
    return 1;
}

int ped_partition_get_flag(const PedPartition* part, PedPartitionFlag flag)
{
    (void)part; (void)flag;
    return 0;
}

int ped_partition_set_flag(PedPartition* part, PedPartitionFlag flag, int state)
{
    (void)part; (void)flag; (void)state;
    return 1;
}

int ped_partition_is_flag_available(const PedPartition* part, PedPartitionFlag flag)
{
    (void)part; (void)flag;
    return 1;
}

int ped_partition_set_system(PedPartition* part, const PedFileSystemType* fs_type)
{
    if (!part) return 0;
    part->fs_type = fs_type;
    return 1;
}

const char* ped_partition_flag_get_name(PedPartitionFlag flag)
{
    switch (flag) {
        case PED_PARTITION_BOOT: return "boot";
        case PED_PARTITION_ESP: return "esp";
        case PED_PARTITION_HIDDEN: return "hidden";
        case PED_PARTITION_LVM: return "lvm";
        case PED_PARTITION_SWAP: return "swap";
        case PED_PARTITION_MSFT_RESERVED: return "msftres";
        case PED_PARTITION_MSFT_DATA: return "msftdata";
        case PED_PARTITION_DIAG: return "diag";
        default: return "unknown";
    }
}

PedPartitionFlag ped_partition_flag_get_by_name(const char* name)
{
    if (!name) return static_cast<PedPartitionFlag>(0);
    if (std::strcmp(name, "boot") == 0) return PED_PARTITION_BOOT;
    if (std::strcmp(name, "esp") == 0) return PED_PARTITION_ESP;
    if (std::strcmp(name, "hidden") == 0) return PED_PARTITION_HIDDEN;
    if (std::strcmp(name, "lvm") == 0) return PED_PARTITION_LVM;
    if (std::strcmp(name, "swap") == 0) return PED_PARTITION_SWAP;
    if (std::strcmp(name, "msftres") == 0) return PED_PARTITION_MSFT_RESERVED;
    if (std::strcmp(name, "msftdata") == 0) return PED_PARTITION_MSFT_DATA;
    if (std::strcmp(name, "diag") == 0) return PED_PARTITION_DIAG;
    return static_cast<PedPartitionFlag>(0);
}

PedPartitionFlag ped_partition_flag_next(PedPartitionFlag flag)
{
    int next = static_cast<int>(flag) + 1;
    if (next > PED_PARTITION_NO_AUTOMOUNT) return static_cast<PedPartitionFlag>(0);
    return static_cast<PedPartitionFlag>(next);
}

PedGeometry* ped_geometry_new(PedDevice* dev, PedSector start, PedSector length)
{
    PedGeometry* g = static_cast<PedGeometry*>(std::calloc(1, sizeof(PedGeometry)));
    if (!g) return nullptr;
    g->dev = dev;
    g->start = start;
    g->length = length;
    g->end = start + length - 1;
    return g;
}

PedGeometry* ped_geometry_duplicate(const PedGeometry* geom)
{
    if (!geom) return nullptr;
    return ped_geometry_new(geom->dev, geom->start, geom->length);
}

void ped_geometry_destroy(PedGeometry* geom)
{
    if (geom) std::free(geom);
}

int ped_geometry_write(PedGeometry* geom, const void* buffer, PedSector offset, PedSector count)
{
    if (!geom || !geom->dev) return 0;
    return ped_device_write(geom->dev, buffer, geom->start + offset, count);
}

PedConstraint* ped_constraint_any(const PedDevice* dev)
{
    (void)dev;
    return static_cast<PedConstraint*>(std::calloc(1, sizeof(PedConstraint)));
}

PedConstraint* ped_constraint_exact(const PedGeometry* geom)
{
    (void)geom;
    return static_cast<PedConstraint*>(std::calloc(1, sizeof(PedConstraint)));
}

void ped_constraint_destroy(PedConstraint* constraint)
{
    if (constraint) std::free(constraint);
}

const PedFileSystemType* ped_file_system_type_get(const char* name)
{
    return FindFsType(name ? name : "ntfs");
}

PedFileSystem* ped_file_system_open(PedGeometry* geom)
{
    PedFileSystem* fs = static_cast<PedFileSystem*>(std::calloc(1, sizeof(PedFileSystem)));
    if (!fs) return nullptr;
    fs->geom = geom;
    fs->type = &g_fsTypes[0];
    return fs;
}

void ped_file_system_close(PedFileSystem* fs)
{
    if (fs) std::free(fs);
}

int ped_file_system_resize(PedFileSystem* fs, PedGeometry* geom, PedConstraint* timer)
{
    (void)fs; (void)geom; (void)timer;
    return 1;
}

PedConstraint* ped_file_system_get_resize_constraint(const PedFileSystem* fs)
{
    (void)fs;
    return static_cast<PedConstraint*>(std::calloc(1, sizeof(PedConstraint)));
}

void ped_exception_set_handler(PedExceptionHandler handler)
{
    (void)handler;
}
