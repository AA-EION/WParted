/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "VdsWrapper.h"

#include <iostream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#ifndef __vdslun_h__
#define __vdslun_h__
#endif
#include <vds.h>
#endif

namespace WParted
{

VdsWrapper& VdsWrapper::Instance()
{
    static VdsWrapper instance;
    return instance;
}

VdsWrapper::VdsWrapper()
    : m_vdsInitialized(false)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_vdsInitialized = true;
    }
#endif
}

VdsWrapper::~VdsWrapper()
{
    UninitializeVds();
}

bool VdsWrapper::InitializeVds()
{
#if defined(_WIN32)
    if (!m_vdsInitialized) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        m_vdsInitialized = SUCCEEDED(hr);
    }
    return m_vdsInitialized;
#else
    return true;
#endif
}

void VdsWrapper::UninitializeVds()
{
#if defined(_WIN32)
    if (m_vdsInitialized) {
        CoUninitialize();
        m_vdsInitialized = false;
    }
#endif
}

static std::wstring NormalizeVolumeDevicePath(const std::wstring& volumePath)
{
    if (volumePath.rfind(L"\\\\.\\", 0) == 0 || volumePath.rfind(L"\\\\?\\", 0) == 0) {
        return volumePath;
    }
    if (volumePath.length() >= 2 && volumePath[1] == L':') {
        return L"\\\\.\\" + volumePath.substr(0, 2);
    }
    return volumePath;
}

VolumeResizeCapabilities VdsWrapper::QueryResizeCapabilities(const std::wstring& volumePath)
{
    VolumeResizeCapabilities caps;
#if defined(_WIN32)
    std::wstring devPath = NormalizeVolumeDevicePath(volumePath);

    HANDLE hVol = CreateFileW(
        devPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hVol == INVALID_HANDLE_VALUE) {
        hVol = CreateFileW(
            devPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL
        );
    }

    if (hVol != INVALID_HANDLE_VALUE) {
        // Query current geometry / size
        GET_LENGTH_INFORMATION lengthInfo = { 0 };
        DWORD bytesReturned = 0;
        if (DeviceIoControl(hVol, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof(lengthInfo), &bytesReturned, NULL)) {
            caps.currentSizeBytes = static_cast<uint64_t>(lengthInfo.Length.QuadPart);
            caps.maxSupportedSizeBytes = caps.currentSizeBytes; // can grow into adjacent unallocated
        }

        // Query shrink capabilities using FSCTL_SHRINK_VOLUME with ShrinkPrepare
        SHRINK_VOLUME_INFORMATION shrinkInfo = {};
        shrinkInfo.ShrinkRequestType = ShrinkPrepare;
        shrinkInfo.NewNumberOfSectors = 0;

        if (DeviceIoControl(hVol, FSCTL_SHRINK_VOLUME, &shrinkInfo, sizeof(shrinkInfo), &shrinkInfo, sizeof(shrinkInfo), &bytesReturned, NULL)) {
            // NewNumberOfSectors returns the minimum sectors
            DISK_GEOMETRY geom = { 0 };
            if (DeviceIoControl(hVol, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof(geom), &bytesReturned, NULL)) {
                caps.minSupportedSizeBytes = shrinkInfo.NewNumberOfSectors * geom.BytesPerSector;
                caps.canShrinkOnline = (caps.minSupportedSizeBytes < caps.currentSizeBytes);
            }
        }

        caps.canExtendOnline = true; // NTFS / ReFS online extend is supported
        CloseHandle(hVol);
    }
#else
    (void)volumePath;
#endif
    return caps;
}

bool VdsWrapper::ShrinkVolumeOnline(const std::wstring& volumePath, uint64_t bytesToShrink)
{
#if defined(_WIN32)
    std::wstring devPath = NormalizeVolumeDevicePath(volumePath);

    HANDLE hVol = CreateFileW(
        devPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hVol == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Get current size and sector size
    GET_LENGTH_INFORMATION lengthInfo = { 0 };
    DISK_GEOMETRY geom = { 0 };
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(hVol, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof(lengthInfo), &bytesReturned, NULL) ||
        !DeviceIoControl(hVol, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &geom, sizeof(geom), &bytesReturned, NULL)) {
        CloseHandle(hVol);
        return false;
    }

    uint64_t currentBytes = static_cast<uint64_t>(lengthInfo.Length.QuadPart);
    if (bytesToShrink >= currentBytes) {
        CloseHandle(hVol);
        return false;
    }

    uint64_t targetBytes = currentBytes - bytesToShrink;
    uint32_t sectorSize = geom.BytesPerSector ? geom.BytesPerSector : 512;
    uint64_t targetSectors = targetBytes / sectorSize;

    // Issue FSCTL_SHRINK_VOLUME with ShrinkCommit
    SHRINK_VOLUME_INFORMATION shrinkInfo = {};
    shrinkInfo.ShrinkRequestType = ShrinkCommit;
    shrinkInfo.NewNumberOfSectors = targetSectors;

    BOOL ok = DeviceIoControl(
        hVol,
        FSCTL_SHRINK_VOLUME,
        &shrinkInfo,
        sizeof(shrinkInfo),
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    CloseHandle(hVol);
    return ok != FALSE;
#else
    (void)volumePath; (void)bytesToShrink;
    return true;
#endif
}

bool VdsWrapper::ExtendVolumeOnline(const std::wstring& volumePath, uint64_t bytesToAdd)
{
#if defined(_WIN32)
    std::wstring devPath = NormalizeVolumeDevicePath(volumePath);

    HANDLE hVol = CreateFileW(
        devPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hVol == INVALID_HANDLE_VALUE) {
        return false;
    }

    STORAGE_DEVICE_NUMBER devNum = { 0 };
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hVol, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &devNum, sizeof(devNum), &bytesReturned, NULL)) {
        CloseHandle(hVol);
        return false;
    }

    // First grow the partition on the physical disk
    std::wstring diskPath = L"\\\\.\\PhysicalDrive" + std::to_wstring(devNum.DeviceNumber);
    HANDLE hDisk = CreateFileW(
        diskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk != INVALID_HANDLE_VALUE) {
        DISK_GROW_PARTITION growInfo = { 0 };
        growInfo.PartitionNumber = devNum.PartitionNumber;
        growInfo.BytesToGrow.QuadPart = static_cast<LONGLONG>(bytesToAdd);

        DeviceIoControl(hDisk, IOCTL_DISK_GROW_PARTITION, &growInfo, sizeof(growInfo), NULL, 0, &bytesReturned, NULL);
        CloseHandle(hDisk);
    }

    // Now issue FSCTL_EXTEND_VOLUME to expand the filesystem
    struct {
        DWORD NumberOfDiskExtents;
        DISK_EXTENT DiskExtents[1];
    } extendInfo = { 0 };
    extendInfo.NumberOfDiskExtents = 1;
    extendInfo.DiskExtents[0].DiskNumber = devNum.DeviceNumber;
    extendInfo.DiskExtents[0].ExtentLength.QuadPart = static_cast<LONGLONG>(bytesToAdd);

    BOOL ok = DeviceIoControl(
        hVol,
        FSCTL_EXTEND_VOLUME,
        &extendInfo,
        sizeof(extendInfo),
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    CloseHandle(hVol);
    return ok != FALSE;
#else
    (void)volumePath; (void)bytesToAdd;
    return true;
#endif
}

} // namespace WParted
