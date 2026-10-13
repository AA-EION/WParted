/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_APFS_DRIVER_H
#define WPARTED_APFS_DRIVER_H

#include "ApfsStructures.h"
#include "DualNomenclature.h"

#include <string>
#include <vector>
#include <cstdint>

namespace WParted
{

struct ApfsVolumeInfo
{
    std::string volumeName;
    std::string volumeUuid;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    bool isEncrypted = false;
    bool isCaseSensitive = false;
    std::vector<std::string> snapshots;
};

struct ApfsContainerInfo
{
    bool isValid = false;
    std::string containerUuid;
    uint32_t blockSize = 4096;
    uint64_t blockCount = 0;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    std::vector<ApfsVolumeInfo> volumes;
};

class ApfsDriver
{
public:
    // Check if partition GUID matches Apple APFS container
    static bool IsApfsPartition(const GUID& typeGuid);

    // Read and parse APFS Container Superblock and Volumes
    static bool ReadContainerInfo(const std::wstring& diskDevicePath,
                                  uint64_t partitionByteOffset,
                                  ApfsContainerInfo& outInfo);

    // Check if WinFsp is installed on system
    static bool IsWinFspInstalled();

    // Mount APFS partition as read-only drive letter via WinFsp service (wparted-apfs-mount.exe)
    static bool MountReadOnly(const std::wstring& diskDevicePath,
                              uint64_t partitionByteOffset,
                              wchar_t driveLetter);

    // Unmount drive letter
    static bool Unmount(wchar_t driveLetter);

    // Format binary 16-byte UUID to string
    static std::string FormatUuid(const uint8_t* uuid);
};

} // namespace WParted

#endif // WPARTED_APFS_DRIVER_H
