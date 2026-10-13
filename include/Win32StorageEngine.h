/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_WIN32_STORAGE_ENGINE_H
#define WPARTED_WIN32_STORAGE_ENGINE_H

#include "DualNomenclature.h"

#include <string>
#include <vector>
#include <cstdint>

namespace WParted
{

struct Win32DiskPartition
{
    int partitionNumber = 0;
    uint64_t startingOffset = 0;
    uint64_t partitionLength = 0;
    bool isGpt = true;

    // GPT Specific
    GUID gptTypeGuid = { 0 };
    GUID gptIdGuid = { 0 };
    uint64_t gptAttributes = 0;
    std::wstring gptName;

    // MBR Specific
    uint8_t mbrType = 0;
    bool mbrBootIndicator = false;

    // Volume & Mounted info
    std::wstring volumeGuidPath;
    std::wstring driveLetter;
    std::wstring fileSystemName;
    std::wstring volumeLabel;
    bool isBitLocker = false;

    // Presentation mapping
    std::string linuxDevPath;
    std::vector<std::string> flags;
};

struct Win32DiskDrive
{
    int diskIndex = 0;
    std::wstring devicePath;
    std::string linuxDevPath;
    std::string model;
    std::string serialNumber;
    std::string vendorId;
    std::string busType;
    uint32_t logicalSectorSize = 512;
    uint32_t physicalSectorSize = 512;
    uint64_t totalSizeBytes = 0;
    uint64_t sectorCount = 0;
    bool isGpt = false;
    bool isMbr = false;
    bool isTrimSupported = false;
    bool isRemovable = false;
    bool isSmartHealthy = true;

    // GPT / MBR Identifiers to preserve on layout write (prevents BCD corruption)
    GUID gptDiskId = { 0 };
    uint64_t gptStartingUsableOffset = 0;
    uint64_t gptUsableLength = 0;
    uint32_t gptMaxPartitionCount = 128;
    uint32_t mbrSignature = 0;

    std::vector<Win32DiskPartition> partitions;

    std::string GetDisplayName() const {
        return DualNomenclature::FormatDualDiskName(diskIndex, busType, model, totalSizeBytes);
    }
};

class Win32StorageEngine
{
public:
    static Win32StorageEngine& Instance();

    // Enumerate all physical disks and partitions via SetupDi and DeviceIoControl
    std::vector<Win32DiskDrive> EnumerateDisks();

    // Query disk bus, physical alignment, sector sizes, TRIM and SMART
    bool QueryDiskProperties(int diskIndex, Win32DiskDrive& outDisk);

    // Read MBR/GPT partition layout from disk
    bool ReadDriveLayout(int diskIndex, Win32DiskDrive& disk);

    // Atomically write updated layout to disk via IOCTL_DISK_SET_DRIVE_LAYOUT_EX
    bool WriteDriveLayout(int diskIndex, const Win32DiskDrive& disk);

    // Initialize raw uninitialized disk as GPT or MBR
    bool InitializeDisk(int diskIndex, bool asGpt);

    // Force Windows kernel cache to update without rebooting
    bool UpdateDiskProperties(int diskIndex);

    // Create a new partition on disk
    bool CreatePartition(int diskIndex,
                         uint64_t startOffset,
                         uint64_t length,
                         const GUID& typeGuid,
                         const std::wstring& name = L"");

    // Delete an existing partition on disk
    bool DeletePartition(int diskIndex, int partitionNumber);

    // Toggle a flag on a partition (e.g. boot, esp, msftres, hidden)
    bool TogglePartitionFlag(int diskIndex, int partitionNumber, const std::string& flag, bool state);

private:
    Win32StorageEngine();
    ~Win32StorageEngine() = default;

    Win32StorageEngine(const Win32StorageEngine&) = delete;
    Win32StorageEngine& operator=(const Win32StorageEngine&) = delete;

    void ResolveVolumeMounts(Win32DiskDrive& disk);
};

} // namespace WParted

#endif // WPARTED_WIN32_STORAGE_ENGINE_H
