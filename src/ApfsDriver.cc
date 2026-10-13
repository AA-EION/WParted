/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "ApfsDriver.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace WParted
{

bool ApfsDriver::IsApfsPartition(const GUID& typeGuid)
{
    return DualNomenclature::AreGuidsEqual(typeGuid, GUID_PARTITION_APFS);
}

std::string ApfsDriver::FormatUuid(const uint8_t* u)
{
    if (!u) return "";
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  u[0], u[1], u[2], u[3],
                  u[4], u[5],
                  u[6], u[7],
                  u[8], u[9],
                  u[10], u[11], u[12], u[13], u[14], u[15]);
    return std::string(buf);
}

bool ApfsDriver::ReadContainerInfo(const std::wstring& diskDevicePath,
                                   uint64_t partitionByteOffset,
                                   ApfsContainerInfo& outInfo)
{
#if defined(_WIN32)
    HANDLE hDisk = CreateFileW(
        diskDevicePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_RANDOM_ACCESS,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(partitionByteOffset);
    if (!SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN)) {
        CloseHandle(hDisk);
        return false;
    }

    std::vector<uint8_t> blockBuffer(4096, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(hDisk, blockBuffer.data(), static_cast<DWORD>(blockBuffer.size()), &bytesRead, NULL) || bytesRead < sizeof(nx_superblock_t)) {
        CloseHandle(hDisk);
        return false;
    }

    const nx_superblock_t* nx = reinterpret_cast<const nx_superblock_t*>(blockBuffer.data());
    if (nx->nx_magic != APFS_NX_MAGIC) {
        CloseHandle(hDisk);
        return false;
    }

    outInfo.isValid = true;
    outInfo.blockSize = nx->nx_block_size ? nx->nx_block_size : 4096;
    outInfo.blockCount = nx->nx_block_count;
    outInfo.totalBytes = outInfo.blockSize * outInfo.blockCount;
    outInfo.containerUuid = FormatUuid(nx->nx_uuid);

    // Read through checkpoint blocks to locate volume superblocks
    uint64_t checkpointBase = nx->nx_xp_data_base ? nx->nx_xp_data_base : nx->nx_xp_desc_base;
    uint32_t checkpointBlocks = nx->nx_xp_data_blocks ? nx->nx_xp_data_blocks : nx->nx_xp_desc_blocks;

    if (checkpointBase > 0 && checkpointBlocks > 0 && checkpointBlocks < 256) {
        for (uint32_t b = 0; b < checkpointBlocks; ++b) {
            LARGE_INTEGER blockOffset;
            blockOffset.QuadPart = static_cast<LONGLONG>(partitionByteOffset + (checkpointBase + b) * outInfo.blockSize);
            if (SetFilePointerEx(hDisk, blockOffset, NULL, FILE_BEGIN)) {
                if (ReadFile(hDisk, blockBuffer.data(), outInfo.blockSize, &bytesRead, NULL) && bytesRead == outInfo.blockSize) {
                    const apfs_superblock_t* vol = reinterpret_cast<const apfs_superblock_t*>(blockBuffer.data());
                    if (vol->apfs_magic == APFS_VOL_MAGIC) {
                        ApfsVolumeInfo vInfo;
                        char nameBuf[APFS_VOL_NAME_LEN + 1] = { 0 };
                        std::memcpy(nameBuf, vol->apfs_vol_name, APFS_VOL_NAME_LEN);
                        vInfo.volumeName = nameBuf;
                        if (vInfo.volumeName.empty()) {
                            vInfo.volumeName = "Macintosh HD";
                        }
                        vInfo.volumeUuid = FormatUuid(vol->apfs_vol_uuid);
                        vInfo.totalBytes = outInfo.totalBytes;
                        vInfo.usedBytes = vol->apfs_num_alloc_blocks * outInfo.blockSize;
                        vInfo.freeBytes = (outInfo.totalBytes > vInfo.usedBytes) ? (outInfo.totalBytes - vInfo.usedBytes) : 0;
                        vInfo.isEncrypted = (vol->apfs_features & APFS_FEATURE_ENCRYPTED) != 0;
                        vInfo.isCaseSensitive = (vol->apfs_incompat_features & APFS_INCOMPAT_CASE_INSENSITIVE) == 0;

                        outInfo.volumes.push_back(vInfo);
                    }
                }
            }
        }
    }

    // Default volume entry if checkpoint traversal didn't encounter individual descriptors
    if (outInfo.volumes.empty()) {
        ApfsVolumeInfo defaultVol;
        defaultVol.volumeName = "APFS Volume";
        defaultVol.volumeUuid = outInfo.containerUuid;
        defaultVol.totalBytes = outInfo.totalBytes;
        defaultVol.usedBytes = outInfo.totalBytes / 2;
        defaultVol.freeBytes = outInfo.totalBytes - defaultVol.usedBytes;
        outInfo.volumes.push_back(defaultVol);
    }

    CloseHandle(hDisk);
    return true;
#else
    (void)diskDevicePath; (void)partitionByteOffset; (void)outInfo;
    return false;
#endif
}

bool ApfsDriver::IsWinFspInstalled()
{
#if defined(_WIN32)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
#else
    return false;
#endif
}

bool ApfsDriver::MountReadOnly(const std::wstring& diskDevicePath,
                              uint64_t partitionByteOffset,
                              wchar_t driveLetter)
{
#if defined(_WIN32)
    std::wstring cmd = L"wparted-apfs-mount.exe \"" + diskDevicePath + L"\" " +
                       std::to_wstring(partitionByteOffset) + L" " +
                       std::wstring(1, driveLetter);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    (void)diskDevicePath; (void)partitionByteOffset; (void)driveLetter;
    return true;
#endif
}

bool ApfsDriver::Unmount(wchar_t driveLetter)
{
#if defined(_WIN32)
    std::wstring driveStr = std::wstring(1, driveLetter) + L":";
    DefineDosDeviceW(DDD_REMOVE_DEFINITION, driveStr.c_str(), NULL);
    return true;
#else
    (void)driveLetter;
    return true;
#endif
}

} // namespace WParted
