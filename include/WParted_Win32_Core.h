/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_WIN32_CORE_H
#define WPARTED_WIN32_CORE_H

#include "Win32StorageEngine.h"
#include "Win32VolumeLockGuard.h"
#include "BitLockerGuard.h"
#include "VdsWrapper.h"
#include "FmifsWrapper.h"

#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace WParted
{

using CoreProgressCallback = std::function<void(int percent, const std::string& statusMessage)>;

class WParted_Win32_Core
{
public:
    static WParted_Win32_Core& Instance();

    // Refresh and retrieve list of physical disks
    std::vector<Win32DiskDrive> ScanDevices();

    // Create a new partition table / disklabel ("gpt" or "msdos")
    bool CreateDiskLabel(int diskIndex, const std::string& labelType);

    // Create a new partition on disk
    bool CreatePartition(int diskIndex,
                         uint64_t startSector,
                         uint64_t sectorCount,
                         const std::string& flagOrType,
                         const std::wstring& partitionName = L"");

    // Delete a partition
    bool DeletePartition(int diskIndex, int partitionNumber);

    // Format a partition filesystem (NTFS, FAT32, exFAT, ReFS)
    bool FormatFileSystem(int diskIndex,
                          int partitionNumber,
                          const std::string& fsType,
                          const std::string& label,
                          CoreProgressCallback callback = nullptr);

    // Live online resize/shrink/extend
    bool ResizePartition(int diskIndex,
                         int partitionNumber,
                         uint64_t newSizeBytes,
                         CoreProgressCallback callback = nullptr);

    // Filesystem Check & Repair (Chkdsk)
    bool CheckFileSystem(int diskIndex,
                         int partitionNumber,
                         bool fixErrors,
                         CoreProgressCallback callback = nullptr);

    // Toggle partition flags (boot, esp, msftres, msftdata, diag, hidden)
    bool ToggleFlag(int diskIndex, int partitionNumber, const std::string& flag, bool state);

private:
    WParted_Win32_Core() = default;
    ~WParted_Win32_Core() = default;

    WParted_Win32_Core(const WParted_Win32_Core&) = delete;
    WParted_Win32_Core& operator=(const WParted_Win32_Core&) = delete;
};

} // namespace WParted

#endif // WPARTED_WIN32_CORE_H
