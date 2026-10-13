/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "Win32VolumeLockGuard.h"

#include <iostream>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <restartmanager.h>
#pragma comment(lib, "rstrtmgr.lib")
#endif

namespace WParted
{

Win32VolumeLockGuard::Win32VolumeLockGuard()
    : m_hVolume(INVALID_HANDLE_VALUE)
    , m_isLocked(false)
    , m_isDismounted(false)
{
}

Win32VolumeLockGuard::Win32VolumeLockGuard(const std::wstring& volumePath)
    : m_hVolume(INVALID_HANDLE_VALUE)
    , m_isLocked(false)
    , m_isDismounted(false)
{
    Lock(volumePath);
}

Win32VolumeLockGuard::~Win32VolumeLockGuard()
{
    Unlock();
}

Win32VolumeLockGuard::Win32VolumeLockGuard(Win32VolumeLockGuard&& other) noexcept
    : m_hVolume(other.m_hVolume)
    , m_volumePath(std::move(other.m_volumePath))
    , m_isLocked(other.m_isLocked)
    , m_isDismounted(other.m_isDismounted)
    , m_lockingProcesses(std::move(other.m_lockingProcesses))
{
    other.m_hVolume = INVALID_HANDLE_VALUE;
    other.m_isLocked = false;
    other.m_isDismounted = false;
}

Win32VolumeLockGuard& Win32VolumeLockGuard::operator=(Win32VolumeLockGuard&& other) noexcept
{
    if (this != &other) {
        Unlock();
        m_hVolume = other.m_hVolume;
        m_volumePath = std::move(other.m_volumePath);
        m_isLocked = other.m_isLocked;
        m_isDismounted = other.m_isDismounted;
        m_lockingProcesses = std::move(other.m_lockingProcesses);

        other.m_hVolume = INVALID_HANDLE_VALUE;
        other.m_isLocked = false;
        other.m_isDismounted = false;
    }
    return *this;
}

bool Win32VolumeLockGuard::Lock(const std::wstring& volumePath, uint32_t maxRetries, uint32_t retryDelayMs)
{
#if defined(_WIN32)
    Unlock();
    m_volumePath = volumePath;

    // Open handle to volume
    m_hVolume = CreateFileW(
        volumePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (m_hVolume == INVALID_HANDLE_VALUE) {
        // Fallback with READ-only access if volume cannot be opened RW
        m_hVolume = CreateFileW(
            volumePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL
        );
        if (m_hVolume == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    // Try locking with backoff
    DWORD bytesReturned = 0;
    for (uint32_t attempt = 0; attempt < maxRetries; ++attempt) {
        if (DeviceIoControl(m_hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
            m_isLocked = true;
            return true;
        }

        // Query Restart Manager to see what is locking it
        m_lockingProcesses = QueryLockingProcesses(volumePath);

        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs * (attempt + 1)));
    }

    return false;
#else
    (void)volumePath; (void)maxRetries; (void)retryDelayMs;
    return true;
#endif
}

bool Win32VolumeLockGuard::Dismount()
{
#if defined(_WIN32)
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    if (DeviceIoControl(m_hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        m_isDismounted = true;
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool Win32VolumeLockGuard::AllowExtendedDasdIo()
{
#if defined(_WIN32)
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hVolume, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &bytesReturned, NULL) != FALSE;
#else
    return true;
#endif
}

void Win32VolumeLockGuard::Unlock()
{
#if defined(_WIN32)
    if (m_hVolume != INVALID_HANDLE_VALUE) {
        if (m_isLocked) {
            DWORD bytesReturned = 0;
            DeviceIoControl(m_hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
            m_isLocked = false;
        }
        CloseHandle(m_hVolume);
        m_hVolume = INVALID_HANDLE_VALUE;
    }
    m_isDismounted = false;
#endif
}

std::vector<LockingProcessInfo> Win32VolumeLockGuard::QueryLockingProcesses(const std::wstring& volumePath)
{
    std::vector<LockingProcessInfo> result;
#if defined(_WIN32)
    DWORD dwSession = 0;
    WCHAR szSessionKey[CCH_RM_SESSION_KEY + 1] = { 0 };

    DWORD dwError = RmStartSession(&dwSession, 0, szSessionKey);
    if (dwError != ERROR_SUCCESS) {
        return result;
    }

    LPCWSTR rgsFileNames[1] = { volumePath.c_str() };
    dwError = RmRegisterResources(dwSession, 1, rgsFileNames, 0, NULL, 0, NULL);
    if (dwError == ERROR_SUCCESS) {
        DWORD dwReason = 0;
        UINT nProcInfoNeeded = 0;
        UINT nProcInfo = 0;

        dwError = RmGetList(dwSession, &nProcInfoNeeded, &nProcInfo, NULL, &dwReason);
        if (dwError == ERROR_MORE_DATA && nProcInfoNeeded > 0) {
            std::vector<RM_PROCESS_INFO> rgAppInfos(nProcInfoNeeded);
            nProcInfo = nProcInfoNeeded;
            dwError = RmGetList(dwSession, &nProcInfoNeeded, &nProcInfo, rgAppInfos.data(), &dwReason);
            if (dwError == ERROR_SUCCESS) {
                for (UINT i = 0; i < nProcInfo; ++i) {
                    LockingProcessInfo info;
                    info.processId = rgAppInfos[i].Process.dwProcessId;
                    info.appName = rgAppInfos[i].strAppName;
                    info.serviceShortName = rgAppInfos[i].strServiceShortName;
                    result.push_back(info);
                }
            }
        }
    }

    RmEndSession(dwSession);
#else
    (void)volumePath;
#endif
    return result;
}

} // namespace WParted
