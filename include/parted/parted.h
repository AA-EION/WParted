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

/* Windows replacement for the subset of the GNU libparted API used by the
 * GParted core.  The implementation (PartedWin32Bridge.cc) is not libparted:
 * it is a clean room implementation of the libparted semantics on top of the
 * Windows partition manager (IOCTL_DISK_GET/SET_DRIVE_LAYOUT_EX).  Names,
 * enumeration values and behaviour follow the libparted 3.6 public API so the
 * GParted core code can be used unchanged.
 */

#ifndef WPARTED_PARTED_H
#define WPARTED_PARTED_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t PedSector;

typedef enum _PedDeviceType {
	PED_DEVICE_UNKNOWN  = 0,
	PED_DEVICE_SCSI     = 1,
	PED_DEVICE_IDE      = 2,
	PED_DEVICE_FILE     = 5,
	PED_DEVICE_SDMMC    = 14,
	PED_DEVICE_VIRTBLK  = 15,
	PED_DEVICE_NVME     = 17
} PedDeviceType;

typedef struct _PedCHSGeometry {
	int cylinders;
	int heads;
	int sectors;
} PedCHSGeometry;

typedef struct _PedDevice {
	struct _PedDevice* next;
	char*              model;
	char*              path;
	PedDeviceType      type;
	long long          sector_size;
	long long          phys_sector_size;
	PedSector          length;
	int                open_count;
	int                read_only;
	int                external_mode;
	int                dirty;
	int                boot_dirty;
	PedCHSGeometry     hw_geom;
	PedCHSGeometry     bios_geom;
	short              host, did;
	void*              arch_specific;
} PedDevice;

typedef struct _PedGeometry {
	PedDevice* dev;
	PedSector  start;
	PedSector  length;
	PedSector  end;
} PedGeometry;

typedef struct _PedFileSystemType {
	struct _PedFileSystemType* next;
	const char*                name;
	void*                      ops;
} PedFileSystemType;

typedef struct _PedFileSystem {
	const PedFileSystemType* type;
	PedGeometry*             geom;
	int                      checked;
	void*                    type_specific;
} PedFileSystem;

typedef enum _PedPartitionType {
	PED_PARTITION_NORMAL    = 0x00,
	PED_PARTITION_LOGICAL   = 0x01,
	PED_PARTITION_EXTENDED  = 0x02,
	PED_PARTITION_FREESPACE = 0x04,
	PED_PARTITION_METADATA  = 0x08,
	PED_PARTITION_PROTECTED = 0x10
} PedPartitionType;

typedef enum _PedPartitionFlag {
	PED_PARTITION_BOOT              = 1,
	PED_PARTITION_ROOT              = 2,
	PED_PARTITION_SWAP              = 3,
	PED_PARTITION_HIDDEN            = 4,
	PED_PARTITION_RAID              = 5,
	PED_PARTITION_LVM               = 6,
	PED_PARTITION_LBA               = 7,
	PED_PARTITION_HPSERVICE         = 8,
	PED_PARTITION_PALO              = 9,
	PED_PARTITION_PREP              = 10,
	PED_PARTITION_MSFT_RESERVED     = 11,
	PED_PARTITION_BIOS_GRUB         = 12,
	PED_PARTITION_APPLE_TV_RECOVERY = 13,
	PED_PARTITION_DIAG              = 14,
	PED_PARTITION_LEGACY_BOOT       = 15,
	PED_PARTITION_MSFT_DATA         = 16,
	PED_PARTITION_IRST              = 17,
	PED_PARTITION_ESP               = 18,
	PED_PARTITION_CHROMEOS_KERNEL   = 19,
	PED_PARTITION_BLS_BOOT          = 20,
	PED_PARTITION_LINUX_HOME        = 21,
	PED_PARTITION_NO_AUTOMOUNT      = 22
} PedPartitionFlag;
#define PED_PARTITION_FIRST_FLAG PED_PARTITION_BOOT
#define PED_PARTITION_LAST_FLAG  PED_PARTITION_NO_AUTOMOUNT

typedef struct _PedPartition {
	struct _PedPartition*    prev;
	struct _PedPartition*    next;
	struct _PedDisk*         disk;
	PedGeometry              geom;
	int                      num;
	PedPartitionType         type;
	const PedFileSystemType* fs_type;
	struct _PedPartition*    part_list;
	void*                    disk_specific;
} PedPartition;

typedef enum _PedDiskTypeFeature {
	PED_DISK_TYPE_EXTENDED       = 1,
	PED_DISK_TYPE_PARTITION_NAME = 2
} PedDiskTypeFeature;

typedef struct _PedDiskType {
	struct _PedDiskType* next;
	const char*          name;
	const void*          ops;
	PedDiskTypeFeature   features;
} PedDiskType;

typedef struct _PedDisk {
	PedDevice*         dev;
	const PedDiskType* type;
	const int*         block_sizes;
	PedPartition*      part_list;
	void*              disk_specific;
	int                needs_clobber;
	int                update_mode;
} PedDisk;

typedef struct _PedConstraint {
	void*        start_align;
	void*        end_align;
	PedGeometry* start_range;
	PedGeometry* end_range;
	PedSector    min_size;
	PedSector    max_size;
} PedConstraint;

typedef enum _PedExceptionType {
	PED_EXCEPTION_INFORMATION = 1,
	PED_EXCEPTION_WARNING     = 2,
	PED_EXCEPTION_ERROR       = 3,
	PED_EXCEPTION_FATAL       = 4,
	PED_EXCEPTION_BUG         = 5,
	PED_EXCEPTION_NO_FEATURE  = 6
} PedExceptionType;

typedef enum _PedExceptionOption {
	PED_EXCEPTION_UNHANDLED = 0,
	PED_EXCEPTION_FIX       = 1,
	PED_EXCEPTION_YES       = 2,
	PED_EXCEPTION_NO        = 4,
	PED_EXCEPTION_OK        = 8,
	PED_EXCEPTION_RETRY     = 16,
	PED_EXCEPTION_IGNORE    = 32,
	PED_EXCEPTION_CANCEL    = 64
} PedExceptionOption;

typedef struct _PedException {
	char*              message;
	PedExceptionType   type;
	PedExceptionOption options;
} PedException;

typedef PedExceptionOption (PedExceptionHandler)(PedException* ex);


const char* ped_get_version(void);

PedDevice* ped_device_get(const char* name);
PedDevice* ped_device_get_next(const PedDevice* dev);
int        ped_device_open(PedDevice* dev);
int        ped_device_close(PedDevice* dev);
void       ped_device_destroy(PedDevice* dev);
int        ped_device_read(const PedDevice* dev, void* buffer, PedSector start, PedSector count);
int        ped_device_write(PedDevice* dev, const void* buffer, PedSector start, PedSector count);
int        ped_device_sync(PedDevice* dev);

PedDiskType* ped_disk_type_get(const char* name);
PedDiskType* ped_disk_type_get_next(const PedDiskType* type);
int          ped_disk_type_check_feature(const PedDiskType* disk_type, PedDiskTypeFeature feature);

PedDisk*      ped_disk_new(PedDevice* dev);
PedDisk*      ped_disk_new_fresh(PedDevice* dev, const PedDiskType* disk_type);
void          ped_disk_destroy(PedDisk* disk);
int           ped_disk_commit_to_dev(PedDisk* disk);
int           ped_disk_commit_to_os(PedDisk* disk);
int           ped_disk_commit(PedDisk* disk);
PedPartition* ped_disk_next_partition(const PedDisk* disk, const PedPartition* part);
PedPartition* ped_disk_get_partition_by_sector(const PedDisk* disk, PedSector sect);
PedPartition* ped_disk_extended_partition(const PedDisk* disk);
int           ped_disk_get_max_primary_partition_count(const PedDisk* disk);
int           ped_disk_add_partition(PedDisk* disk, PedPartition* part, const PedConstraint* constraint);
int           ped_disk_delete_partition(PedDisk* disk, PedPartition* part);
int           ped_disk_set_partition_geom(PedDisk* disk, PedPartition* part,
                                          const PedConstraint* constraint,
                                          PedSector start, PedSector end);

PedPartition*    ped_partition_new(const PedDisk* disk, PedPartitionType type,
                                   const PedFileSystemType* fs_type, PedSector start, PedSector end);
void             ped_partition_destroy(PedPartition* part);
char*            ped_partition_get_path(const PedPartition* part);
const char*      ped_partition_get_name(const PedPartition* part);
int              ped_partition_set_name(PedPartition* part, const char* name);
int              ped_partition_get_flag(const PedPartition* part, PedPartitionFlag flag);
int              ped_partition_set_flag(PedPartition* part, PedPartitionFlag flag, int state);
int              ped_partition_is_flag_available(const PedPartition* part, PedPartitionFlag flag);
int              ped_partition_set_system(PedPartition* part, const PedFileSystemType* fs_type);
const char*      ped_partition_flag_get_name(PedPartitionFlag flag);
PedPartitionFlag ped_partition_flag_get_by_name(const char* name);
PedPartitionFlag ped_partition_flag_next(PedPartitionFlag flag);

PedGeometry* ped_geometry_new(const PedDevice* dev, PedSector start, PedSector length);
PedGeometry* ped_geometry_duplicate(const PedGeometry* geom);
void         ped_geometry_destroy(PedGeometry* geom);
int          ped_geometry_write(PedGeometry* geom, const void* buffer, PedSector offset, PedSector count);

PedConstraint* ped_constraint_any(const PedDevice* dev);
PedConstraint* ped_constraint_exact(const PedGeometry* geom);
void           ped_constraint_destroy(PedConstraint* constraint);

PedFileSystemType* ped_file_system_type_get(const char* name);
PedFileSystem*     ped_file_system_open(PedGeometry* geom);
int                ped_file_system_close(PedFileSystem* fs);
int                ped_file_system_resize(PedFileSystem* fs, PedGeometry* geom, void* timer);
PedConstraint*     ped_file_system_get_resize_constraint(const PedFileSystem* fs);

void               ped_exception_set_handler(PedExceptionHandler* handler);
PedExceptionOption ped_exception_throw(PedExceptionType ex_type, PedExceptionOption ex_opt,
                                       const char* message, ...);

#ifdef __cplusplus
}
#endif

#endif /* WPARTED_PARTED_H */
