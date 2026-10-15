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

/* WinDiskModel
 *
 * Platform independent model of an MBR or GPT partition table as seen by
 * Windows (IOCTL_DISK_GET/SET_DRIVE_LAYOUT_EX) and as stored on disk.  This
 * is the heart of the WParted libparted replacement.  It is deliberately
 * free of any Windows API so that every rule in here can be unit tested on
 * any platform.
 *
 * Safety rules implemented here:
 * *   Every field of every partition entry (type, unique id, attributes,
 *     name, boot indicator, EBR positions, MBR slot) is preserved unless an
 *     operation explicitly changes it.
 * *   A model is only ever converted into a layout for writing after a full
 *     validation (bounds, overlaps, MBR limits, GPT entry limits).
 * *   The on-disk structures are parsed independently (raw MBR/EBR chain and
 *     GPT headers with CRC32 checks) and cross-checked against what Windows
 *     reports.  Any disagreement makes the disk read-only.
 */

#ifndef WPARTED_WINDISKMODEL_H
#define WPARTED_WINDISKMODEL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace WParted
{
namespace WinDisk
{


struct Guid
{
	uint32_t data1 = 0;
	uint16_t data2 = 0;
	uint16_t data3 = 0;
	uint8_t  data4[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	bool is_null() const;
	std::string to_string() const;  // Upper case, e.g. EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
	static bool parse(const std::string& text, Guid& out);
	static Guid from_bytes_le(const uint8_t* bytes);  // GPT on-disk (mixed endian) encoding
	void to_bytes_le(uint8_t* bytes) const;
};

bool operator==(const Guid& lhs, const Guid& rhs);
bool operator!=(const Guid& lhs, const Guid& rhs);


// Well known GPT partition type GUIDs.
namespace TypeGuid
{
	extern const Guid ESP;                // C12A7328-F81F-11D2-BA4B-00A0C93EC93B
	extern const Guid MSFT_RESERVED;      // E3C9E316-0B5C-4DB8-817D-F92DF00215AE
	extern const Guid BASIC_DATA;         // EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
	extern const Guid MSFT_RECOVERY;      // DE94BBA4-06D1-4D40-A16A-BFD50179D6AC
	extern const Guid LDM_METADATA;       // 5808C8AA-7E8F-42E0-85D2-E1E90434CFB3
	extern const Guid LDM_DATA;           // AF9B60A0-1431-4F62-BC68-3311714A69AD
	extern const Guid STORAGE_SPACES;     // E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D
	extern const Guid LINUX_DATA;         // 0FC63DAF-8483-4772-8E79-3D69D8477DE4
	extern const Guid LINUX_SWAP;         // 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
	extern const Guid LINUX_LVM;          // E6D6D379-F507-44C2-A23C-238F2A3DF928
	extern const Guid LINUX_RAID;         // A19D880F-05FC-4D3B-A006-743F0F84911E
	extern const Guid LINUX_HOME;         // 933AC7E1-2EB4-4F13-B844-0E14E2AEF915
	extern const Guid BIOS_GRUB;          // 21686148-6449-6E6F-744E-656564454649
	extern const Guid HP_SERVICE;         // E2A1E728-32E3-11D6-A682-7B03A0000000
	extern const Guid APPLE_HFS;          // 48465300-0000-11AA-AA11-00306543ECAC
	extern const Guid APPLE_APFS;         // 7C3457EF-0000-11AA-AA11-00306543ECAC
	extern const Guid APPLE_TV_RECOVERY;  // 5265636F-7665-11AA-AA11-00306543ECAC
	extern const Guid PREP;               // 9E1A2D38-C612-4316-AA26-8B49521E5A8B
	extern const Guid IRST;               // D3BFE2DE-3DAF-11DF-BA40-E3A556D89593
	extern const Guid CHROMEOS_KERNEL;    // FE3A2A5D-4F32-41A7-B725-ACCC3285A309
	extern const Guid BLS_BOOT;           // BC13C2FF-59E6-4262-A352-B275FD6F7172
}


// GPT partition attribute bits.
const uint64_t GPT_ATTR_PLATFORM_REQUIRED        = 0x0000000000000001ULL;
const uint64_t GPT_ATTR_LEGACY_BIOS_BOOTABLE     = 0x0000000000000004ULL;
const uint64_t GPT_BASIC_DATA_ATTR_READ_ONLY     = 0x1000000000000000ULL;
const uint64_t GPT_BASIC_DATA_ATTR_SHADOW_COPY   = 0x2000000000000000ULL;
const uint64_t GPT_BASIC_DATA_ATTR_HIDDEN        = 0x4000000000000000ULL;
const uint64_t GPT_BASIC_DATA_ATTR_NO_DRIVE_LETTER = 0x8000000000000000ULL;


// MBR partition type ids used by the flag logic.
namespace MbrType
{
	const uint8_t EMPTY         = 0x00;
	const uint8_t FAT12         = 0x01;
	const uint8_t FAT16_SMALL   = 0x04;
	const uint8_t EXTENDED      = 0x05;
	const uint8_t FAT16         = 0x06;
	const uint8_t NTFS          = 0x07;  // Also exFAT, ReFS, UDF (IFS)
	const uint8_t FAT32         = 0x0B;
	const uint8_t FAT32_LBA     = 0x0C;
	const uint8_t FAT16_LBA     = 0x0E;
	const uint8_t EXTENDED_LBA  = 0x0F;
	const uint8_t HIDDEN_BIT    = 0x10;
	const uint8_t COMPAQ_DIAG   = 0x12;
	const uint8_t MSFT_RECOVERY = 0x27;
	const uint8_t PREP          = 0x41;
	const uint8_t LDM           = 0x42;  // Windows dynamic disk
	const uint8_t LINUX_SWAP    = 0x82;
	const uint8_t LINUX         = 0x83;
	const uint8_t IRST          = 0x84;
	const uint8_t EXTENDED_LINUX= 0x85;
	const uint8_t LINUX_LVM     = 0x8E;
	const uint8_t APPLE_HFS     = 0xAF;
	const uint8_t DELL_DIAG     = 0xDE;
	const uint8_t BLS_BOOT      = 0xEA;
	const uint8_t GPT_PROTECTIVE= 0xEE;
	const uint8_t ESP           = 0xEF;
	const uint8_t PALO          = 0xF0;
	const uint8_t LINUX_RAID    = 0xFD;
	const uint8_t LINUX_LVM_OLD = 0xFE;

	bool is_extended(uint8_t type);
}


enum class TableStyle
{
	RAW,  // No partition table (uninitialised disk)
	MBR,
	GPT
};


// One entry of a Windows DRIVE_LAYOUT_INFORMATION_EX, kept platform independent.
struct LayoutEntry
{
	uint64_t       offset   = 0;      // Bytes
	uint64_t       length   = 0;      // Bytes
	uint32_t       number   = 0;      // Windows partition number (informational)
	bool           rewrite  = false;
	// MBR fields
	uint8_t        mbr_type = 0;
	bool           mbr_boot = false;
	bool           mbr_recognized = false;
	uint32_t       mbr_hidden_sectors = 0;
	// GPT fields
	Guid           gpt_type;
	Guid           gpt_id;
	uint64_t       gpt_attributes = 0;
	std::u16string gpt_name;
};


struct DiskLayout
{
	TableStyle               style = TableStyle::RAW;
	uint32_t                 mbr_signature = 0;
	Guid                     gpt_disk_id;
	uint64_t                 gpt_first_usable_offset = 0;  // Bytes
	uint64_t                 gpt_usable_length = 0;        // Bytes
	uint32_t                 gpt_max_entries = 0;
	std::vector<LayoutEntry> entries;
};


enum class PartKind
{
	PRIMARY,
	EXTENDED,
	LOGICAL
};


// One partition in the model.  Sectors are in units of the disk logical sector size.
struct PartRecord
{
	int            num   = 0;              // libparted/Linux style number (sdX<num>)
	PartKind       kind  = PartKind::PRIMARY;
	int64_t        start = 0;              // First sector, inclusive
	int64_t        end   = 0;              // Last sector, inclusive
	// MBR
	uint8_t        mbr_type = 0;
	bool           mbr_boot = false;
	int64_t        ebr_sector = -1;        // LOGICAL only: sector of its EBR, -1 to compute
	// GPT
	Guid           type_guid;
	Guid           unique_guid;
	uint64_t       attributes = 0;
	std::u16string name;
	// libparted style boolean flags which select the partition type (raid, lvm, esp,
	// ...) plus MBR hidden and lba.  Bit (1 << PedPartitionFlag).  Derived from the
	// on-disk type when read and combined with the file system type by set_system(),
	// exactly mirroring libparted's dos.c and gpt.c behaviour.
	uint32_t       type_flags = 0;
	// Bookkeeping
	bool           existing = false;       // Read from disk (as opposed to newly created)

	int64_t length() const  { return end - start + 1; }
};


struct DiskModel
{
	TableStyle              style = TableStyle::RAW;
	uint32_t                sector_size = 512;
	int64_t                 total_sectors = 0;
	uint32_t                mbr_signature = 0;
	Guid                    gpt_disk_id;
	int64_t                 first_usable = 0;   // GPT: first usable LBA. MBR: 1
	int64_t                 last_usable = 0;    // GPT: last usable LBA.  MBR: total_sectors - 1
	uint32_t                gpt_max_entries = 128;
	bool                    fresh = false;      // New empty table not yet on disk
	bool                    superfloppy = false;// Whole disk volume without partition
	                                            // table (libparted "loop" label)
	std::vector<PartRecord> parts;              // Primaries and extended sorted by start,
	                                            // each extended followed by its logicals.

	const PartRecord* find_by_num(int num) const;
	PartRecord* find_by_num(int num);
	const PartRecord* find_extended() const;
	void sort_parts();
};


// Sector reader used by the raw on-disk parsers.  Reads 'count' sectors starting at
// 'lba' into 'out' (resized to count * sector_size).
typedef std::function<bool(uint64_t lba, uint32_t count, std::vector<uint8_t>& out)> SectorReader;


// Independent parse of the on-disk structures, the same way libparted reads them.
struct RawTable
{
	TableStyle style = TableStyle::RAW;
	bool       hybrid_mbr = false;     // GPT with additional non-protective MBR entries
	bool       superfloppy = false;    // MBR-less volume starting at sector 0
	uint32_t   mbr_signature = 0;

	struct MbrSlot
	{
		uint8_t  type = 0;
		bool     boot = false;
		uint32_t lba_start = 0;
		uint32_t sectors = 0;
	};
	MbrSlot slots[4];

	struct Logical
	{
		uint64_t ebr_lba = 0;
		uint8_t  type = 0;
		bool     boot = false;
		uint64_t start = 0;
		uint64_t sectors = 0;
	};
	std::vector<Logical> logicals;

	struct GptEntry
	{
		uint32_t       index = 0;   // Zero based entry slot
		Guid           type;
		Guid           id;
		uint64_t       first_lba = 0;
		uint64_t       last_lba = 0;
		uint64_t       attributes = 0;
		std::u16string name;
	};
	Guid                  gpt_disk_guid;
	uint64_t              gpt_first_usable = 0;
	uint64_t              gpt_last_usable = 0;
	uint32_t              gpt_num_entries = 0;
	bool                  gpt_backup_ok = false;
	std::vector<GptEntry> gpt_entries;  // Non-empty entries only, in slot order
};


uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0);


// Parse the raw MBR / EBR chain / GPT structures.  Returns false with an error message
// when the structures are present but damaged or inconsistent.
bool read_raw_table(const SectorReader& reader, uint32_t sector_size, int64_t total_sectors,
                    RawTable& out, std::string& error);


// Convert the Windows reported layout into a model.  'raw' (may be null) is used to
// obtain libparted compatible partition numbers (GPT entry slots).
bool layout_to_model(const DiskLayout& layout, uint32_t sector_size, int64_t total_sectors,
                     const RawTable* raw, DiskModel& out, std::string& error);


// Cross-check the Windows view against the raw on-disk view.  Any difference in
// partition boundaries, types, ids or EBR positions is reported as an error.
bool cross_check(const DiskModel& model, const RawTable& raw, std::string& error);


// Validate a model completely.  Used before every write.
bool validate_model(const DiskModel& model, std::string& error);


// Convert a validated model into a Windows layout ready for IOCTL_DISK_SET_DRIVE_LAYOUT_EX.
bool model_to_layout(const DiskModel& model, DiskLayout& out, std::string& error);


// Compare two layouts for equality of everything that matters on disk.
bool layouts_equivalent(const DiskLayout& a, const DiskLayout& b, uint32_t sector_size,
                        std::string& difference);


// A byte range on the disk occupied by an existing partition which is deleted or whose
// boundaries or type change between two layouts.  Volumes on these ranges must be
// locked and dismounted before the new layout is written.
struct Extent
{
	uint64_t offset = 0;
	uint64_t length = 0;
};
std::vector<Extent> changed_extents(const DiskLayout& before, const DiskLayout& after);


// Which byte ranges a DiskSession (see WinDiskBackend.h) may cover.
enum class SessionRange
{
	// The range lies inside one existing partition (or anywhere on a disk without
	// a partition table).
	INSIDE_PARTITION,
	// The range is covered by one existing primary or GPT partition plus
	// unallocated space next to it.  Used while moving a partition: data is copied
	// into free space before the partition table is changed.  Never covers the
	// first MiB of the disk, partition table structures, extended partitions or
	// any other partition.
	PARTITION_AND_FREE_SPACE,
};

// Check a session range against the current layout.  'disk_size' in bytes.
bool session_range_allowed(const DiskLayout& layout, uint32_t sector_size, uint64_t disk_size,
                           uint64_t offset, uint64_t length, SessionRange range,
                           std::string& reason);


// Partition table properties which make a disk unsafe to modify with WParted.  Returns
// an empty string when the disk can be modified.
std::string unsupported_reason(const DiskModel& model, const RawTable* raw);


// libparted compatible flag handling.  'flag' values are PedPartitionFlag values.
// 'fs_name' is the libparted file system type name of the partition contents (may be
// null or empty when unknown).
bool flag_is_available(const DiskModel& model, const PartRecord& part, int flag);
bool flag_get(const DiskModel& model, const PartRecord& part, int flag);
bool flag_set(DiskModel& model, PartRecord& part, int flag, bool state, const char* fs_name);
void set_system(const DiskModel& model, PartRecord& part, const char* fs_name);


// Initialise type_flags of a record from its on-disk type.  Called for records read from
// disk.
void derive_type_flags(TableStyle style, PartRecord& part);


// Random (version 4) GUID for new partitions and tables.
Guid generate_guid();


// Compare the partitions of two models (boundaries, kinds, types, ids, attributes,
// names, boot flags and, when 'strict_ebr', EBR positions).
bool models_equivalent(const DiskModel& a, const DiskModel& b, bool strict_ebr,
                       std::string& difference);


// UTF-8 <-> UTF-16 helpers for GPT partition names.
std::u16string utf8_to_utf16(const std::string& text);
std::string utf16_to_utf8(const std::u16string& text);


}  // namespace WinDisk
}  // namespace WParted

#endif /* WPARTED_WINDISKMODEL_H */
