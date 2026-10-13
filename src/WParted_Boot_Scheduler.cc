/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "WParted_Boot_Scheduler.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace WParted
{

std::wstring WParted_Boot_Scheduler::GetJobFilePath()
{
#if defined(_WIN32)
    WCHAR sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);
    return std::wstring(sysDir) + L"\\wparted-jobs.bin";
#else
    return L"wparted-jobs.bin";
#endif
}

bool WParted_Boot_Scheduler::IsBootAccessRequired(const std::wstring& driveLetterOrMountPoint, bool movingStartSector)
{
    if (movingStartSector) {
        return true;
    }
#if defined(_WIN32)
    WCHAR sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);
    if (driveLetterOrMountPoint.length() >= 2 && towupper(driveLetterOrMountPoint[0]) == towupper(sysDir[0])) {
        return true;
    }
#else
    (void)driveLetterOrMountPoint;
#endif
    return false;
}

bool WParted_Boot_Scheduler::ScheduleBootJob(const std::vector<BootJobOperation>& operations)
{
    if (operations.empty() || operations.size() > 16) {
        return false;
    }

    // Prepare header
    BootJobFileHeader header = { 0 };
    header.magic = BOOT_JOB_MAGIC;
    header.version = BOOT_JOB_VERSION;
    header.opCount = static_cast<uint32_t>(operations.size());

    for (size_t i = 0; i < operations.size(); ++i) {
        header.operations[i] = operations[i];
    }

    // Compute checksum
    header.headerCrc64 = CalculateCRC64(header.operations, header.opCount * sizeof(BootJobOperation));

    // Write binary job file
    std::wstring path = GetJobFilePath();
    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"wb");
#else
    fp = fopen("wparted-jobs.bin", "wb");
#endif

    if (!fp) {
        return false;
    }

    size_t written = fwrite(&header, 1, sizeof(header), fp);
    fclose(fp);

    if (written != sizeof(header)) {
        return false;
    }

#if defined(_WIN32)
    // Register in BootExecute
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, NULL, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
            std::vector<wchar_t> multiSz(size / sizeof(wchar_t) + 64, L'\0');
            if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, reinterpret_cast<LPBYTE>(multiSz.data()), &size) == ERROR_SUCCESS) {
                // Check if already present
                bool found = false;
                const wchar_t* p = multiSz.data();
                while (*p) {
                    if (wcsstr(p, L"wparted-boot") != nullptr) {
                        found = true;
                        break;
                    }
                    p += wcslen(p) + 1;
                }

                if (!found) {
                    // Find the second null terminator
                    size_t currLen = 0;
                    while (currLen < multiSz.size() && (multiSz[currLen] != L'\0' || multiSz[currLen + 1] != L'\0')) {
                        currLen++;
                    }
                    if (currLen > 0 && multiSz[currLen] == L'\0') {
                        currLen++; // past the terminating null of last entry
                    }

                    const wchar_t newEntry[] = L"autocheck wparted-boot";
                    size_t entryLen = wcslen(newEntry) + 1;
                    wmemcpy(&multiSz[currLen], newEntry, entryLen);
                    multiSz[currLen + entryLen] = L'\0'; // double null termination

                    DWORD newByteSize = static_cast<DWORD>((currLen + entryLen + 1) * sizeof(wchar_t));
                    RegSetValueExW(hKey, L"BootExecute", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(multiSz.data()), newByteSize);
                }
            }
        }
        RegCloseKey(hKey);
    }
#endif

    return true;
}

bool WParted_Boot_Scheduler::CancelBootJob()
{
#if defined(_WIN32)
    std::wstring path = GetJobFilePath();
    DeleteFileW(path.c_str());

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, NULL, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
            std::vector<wchar_t> multiSz(size / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, reinterpret_cast<LPBYTE>(multiSz.data()), &size) == ERROR_SUCCESS) {
                std::vector<wchar_t> newMultiSz;
                const wchar_t* p = multiSz.data();
                while (*p) {
                    if (wcsstr(p, L"wparted-boot") == nullptr) {
                        size_t l = wcslen(p) + 1;
                        newMultiSz.insert(newMultiSz.end(), p, p + l);
                    }
                    p += wcslen(p) + 1;
                }
                newMultiSz.push_back(L'\0'); // second null terminator

                DWORD newByteSize = static_cast<DWORD>(newMultiSz.size() * sizeof(wchar_t));
                RegSetValueExW(hKey, L"BootExecute", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(newMultiSz.data()), newByteSize);
            }
        }
        RegCloseKey(hKey);
    }
    return true;
#else
    return true;
#endif
}

bool WParted_Boot_Scheduler::IsBootJobScheduled()
{
#if defined(_WIN32)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, NULL, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
            std::vector<wchar_t> multiSz(size / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(hKey, L"BootExecute", NULL, &type, reinterpret_cast<LPBYTE>(multiSz.data()), &size) == ERROR_SUCCESS) {
                const wchar_t* p = multiSz.data();
                while (*p) {
                    if (wcsstr(p, L"wparted-boot") != nullptr) {
                        RegCloseKey(hKey);
                        return true;
                    }
                    p += wcslen(p) + 1;
                }
            }
        }
        RegCloseKey(hKey);
    }
#endif
    return false;
}

} // namespace WParted
