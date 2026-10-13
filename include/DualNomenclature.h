/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_DUAL_NOMENCLATURE_H
#define WPARTED_DUAL_NOMENCLATURE_H

#include <string>
#include <vector>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
#endif

namespace WParted
{

// Well-known GPT Partition Type GUIDs
// EFI System Partition
extern const GUID GUID_PARTITION_ESP;
// Microsoft Reserved Partition (MSR)
extern const GUID GUID_PARTITION_MSFT_RESERVED;
// Basic Data Partition (Windows NTFS / FAT32 / exFAT)
extern const GUID GUID_PARTITION_BASIC_DATA;
// Windows Recovery Environment (WinRE / Diag)
extern const GUID GUID_PARTITION_WINRE;
// Linux Filesystem Data
extern const GUID GUID_PARTITION_LINUX_DATA;
// Linux Swap
extern const GUID GUID_PARTITION_LINUX_SWAP;
// Linux LVM
extern const GUID GUID_PARTITION_LINUX_LVM;
// Apple APFS Container
extern const GUID GUID_PARTITION_APFS;

// GPT Basic Data Attributes
constexpr uint64_t GPT_ATTR_READ_ONLY = 0x1000000000000000ULL;
constexpr uint64_t GPT_ATTR_SHADOW_COPY = 0x2000000000000000ULL;
constexpr uint64_t GPT_ATTR_HIDDEN = 0x4000000000000000ULL;
constexpr uint64_t GPT_ATTR_NO_DRIVE_LETTER = 0x8000000000000000ULL;

class DualNomenclature
{
public:
    // Convert disk index to Linux device node name (e.g., 0 -> "/dev/sda")
    static std::string DiskIndexToLinuxDev(int diskIndex);

    // Convert disk index and partition number to Linux partition node (e.g., 0, 1 -> "/dev/sda1")
    static std::string PartitionToLinuxDev(int diskIndex, int partitionIndex);

    // Format full dual nomenclature string for a disk
    // e.g. "/dev/sda (Disk 0 - NVMe Samsung 990 PRO 2TB)"
    static std::string FormatDualDiskName(int diskIndex,
                                          const std::string& busType,
                                          const std::string& model,
                                          uint64_t sizeBytes);

    // Format full dual nomenclature string for a partition
    // e.g. "/dev/sda1 (C:\ - Windows NTFS 1.8TB)"
    static std::string FormatDualPartitionName(int diskIndex,
                                               int partitionIndex,
                                               const std::string& mountPointOrDrive,
                                               const std::string& fsType,
                                               uint64_t sizeBytes);

    // Convert human readable byte count to KiB/MiB/GiB/TiB string
    static std::string FormatByteSize(uint64_t bytes);

    // Translate Linux flag names ("boot", "esp", "msftres", "msftdata", "diag", "hidden") to GPT GUID and attributes
    static bool LinuxFlagToGpt(const std::string& flag, GUID& outGuid, uint64_t& outAttributes);

    // Translate GPT GUID and attributes to Linux flag list
    static std::vector<std::string> GptToLinuxFlags(const GUID& guid, uint64_t attributes);

    // Parse GUID string representation
    static bool StringToGuid(const std::string& str, GUID& guid);

    // Format GUID to standard string representation
    static std::string GuidToString(const GUID& guid);

    // Compare two GUIDs
    static bool AreGuidsEqual(const GUID& a, const GUID& b);
};

} // namespace WParted

#endif // WPARTED_DUAL_NOMENCLATURE_H
