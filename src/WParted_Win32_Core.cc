/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "WParted_Win32_Core.h"

#include <iostream>
#include <algorithm>

namespace WParted
{

WParted_Win32_Core& WParted_Win32_Core::Instance()
{
    static WParted_Win32_Core instance;
    return instance;
}

std::vector<Win32DiskDrive> WParted_Win32_Core::ScanDevices()
{
    return Win32StorageEngine::Instance().EnumerateDisks();
}

bool WParted_Win32_Core::CreateDiskLabel(int diskIndex, const std::string& labelType)
{
    bool asGpt = (labelType != "msdos" && labelType != "mbr");
    return Win32StorageEngine::Instance().InitializeDisk(diskIndex, asGpt);
}

bool WParted_Win32_Core::CreatePartition(int diskIndex,
                                         uint64_t startSector,
                                         uint64_t sectorCount,
                                         const std::string& flagOrType,
                                         const std::wstring& partitionName)
{
    Win32DiskDrive disk;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(diskIndex, disk)) {
        return false;
    }

    uint64_t sectorSize = disk.logicalSectorSize ? disk.logicalSectorSize : 512;
    uint64_t startOffset = startSector * sectorSize;
    uint64_t length = sectorCount * sectorSize;

    GUID typeGuid = GUID_PARTITION_BASIC_DATA;
    uint64_t attrs = 0;
    DualNomenclature::LinuxFlagToGpt(flagOrType, typeGuid, attrs);

    return Win32StorageEngine::Instance().CreatePartition(diskIndex, startOffset, length, typeGuid, partitionName);
}

bool WParted_Win32_Core::DeletePartition(int diskIndex, int partitionNumber)
{
    return Win32StorageEngine::Instance().DeletePartition(diskIndex, partitionNumber);
}

bool WParted_Win32_Core::FormatFileSystem(int diskIndex,
                                          int partitionNumber,
                                          const std::string& fsType,
                                          const std::string& label,
                                          CoreProgressCallback callback)
{
    Win32DiskDrive disk;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(diskIndex, disk) ||
        !Win32StorageEngine::Instance().ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    const Win32DiskPartition* targetPart = nullptr;
    for (const auto& p : disk.partitions) {
        if (p.partitionNumber == partitionNumber) {
            targetPart = &p;
            break;
        }
    }

    if (!targetPart) {
        return false;
    }

    std::wstring rootPath;
    if (!targetPart->driveLetter.empty()) {
        rootPath = targetPart->driveLetter;
        if (rootPath.back() != L'\\') rootPath += L'\\';
    } else if (!targetPart->volumeGuidPath.empty()) {
        rootPath = targetPart->volumeGuidPath;
    } else {
        rootPath = L"\\\\?\\GLOBALROOT\\Device\\Harddisk" + std::to_wstring(diskIndex) +
                   L"\\Partition" + std::to_wstring(partitionNumber) + L"\\";
    }

    std::wstring wFs(fsType.begin(), fsType.end());
    std::wstring wLabel(label.begin(), label.end());

    auto fmifsCallback = [callback](uint32_t pct, const std::string& msg) {
        if (callback) {
            callback(static_cast<int>(pct), msg);
        }
    };

    return FmifsWrapper::Instance().FormatVolume(rootPath, wFs, wLabel, true, 4096, fmifsCallback);
}

bool WParted_Win32_Core::ResizePartition(int diskIndex,
                                         int partitionNumber,
                                         uint64_t newSizeBytes,
                                         CoreProgressCallback callback)
{
    Win32DiskDrive disk;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(diskIndex, disk) ||
        !Win32StorageEngine::Instance().ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    Win32DiskPartition* targetPart = nullptr;
    for (auto& p : disk.partitions) {
        if (p.partitionNumber == partitionNumber) {
            targetPart = &p;
            break;
        }
    }

    if (!targetPart) {
        return false;
    }

    std::wstring volPath = targetPart->driveLetter.empty() ? targetPart->volumeGuidPath : targetPart->driveLetter;

    // BitLocker suspension guard if volume is encrypted
    std::unique_ptr<BitLockerSuspensionScope> blScope;
    if (!volPath.empty() && targetPart->isBitLocker) {
        if (callback) callback(10, "Suspending BitLocker protectors for safe resize...");
        blScope = std::make_unique<BitLockerSuspensionScope>(volPath);
    }

    uint64_t currentBytes = targetPart->partitionLength;

    if (newSizeBytes < currentBytes) {
        // Shrink operation: live online shrink filesystem FIRST, then update partition table
        uint64_t bytesToShrink = currentBytes - newSizeBytes;
        if (callback) callback(30, "Performing online filesystem shrink...");

        if (!volPath.empty()) {
            VdsWrapper::Instance().ShrinkVolumeOnline(volPath, bytesToShrink);
        }

        if (callback) callback(70, "Updating partition table geometry...");
        targetPart->partitionLength = newSizeBytes;
        return Win32StorageEngine::Instance().WriteDriveLayout(diskIndex, disk);
    } else if (newSizeBytes > currentBytes) {
        // Extend operation: update partition table FIRST, then extend filesystem
        uint64_t bytesToAdd = newSizeBytes - currentBytes;
        if (callback) callback(30, "Updating partition table geometry...");

        targetPart->partitionLength = newSizeBytes;
        if (!Win32StorageEngine::Instance().WriteDriveLayout(diskIndex, disk)) {
            return false;
        }

        if (callback) callback(70, "Performing online filesystem extend...");
        if (!volPath.empty()) {
            return VdsWrapper::Instance().ExtendVolumeOnline(volPath, bytesToAdd);
        }
        return true;
    }

    return true;
}

bool WParted_Win32_Core::CheckFileSystem(int diskIndex,
                                         int partitionNumber,
                                         bool fixErrors,
                                         CoreProgressCallback callback)
{
    Win32DiskDrive disk;
    if (!Win32StorageEngine::Instance().QueryDiskProperties(diskIndex, disk) ||
        !Win32StorageEngine::Instance().ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    const Win32DiskPartition* targetPart = nullptr;
    for (const auto& p : disk.partitions) {
        if (p.partitionNumber == partitionNumber) {
            targetPart = &p;
            break;
        }
    }

    if (!targetPart) {
        return false;
    }

    std::wstring rootPath = targetPart->driveLetter.empty() ? targetPart->volumeGuidPath : targetPart->driveLetter;
    if (rootPath.empty()) {
        rootPath = L"\\\\?\\GLOBALROOT\\Device\\Harddisk" + std::to_wstring(diskIndex) +
                   L"\\Partition" + std::to_wstring(partitionNumber) + L"\\";
    }

    std::wstring wFs = targetPart->fileSystemName.empty() ? L"NTFS" : targetPart->fileSystemName;

    auto fmifsCallback = [callback](uint32_t pct, const std::string& msg) {
        if (callback) {
            callback(static_cast<int>(pct), msg);
        }
    };

    return FmifsWrapper::Instance().CheckVolume(rootPath, wFs, fixErrors, false, false, fmifsCallback);
}

bool WParted_Win32_Core::ToggleFlag(int diskIndex, int partitionNumber, const std::string& flag, bool state)
{
    return Win32StorageEngine::Instance().TogglePartitionFlag(diskIndex, partitionNumber, flag, state);
}

} // namespace WParted
