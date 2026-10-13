/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "BitLockerGuard.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace WParted
{

#if defined(_WIN32)
static std::string ExecuteCommandCapture(const std::wstring& cmd)
{
    std::string result;
    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return result;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi = { 0 };
    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;

        char buffer[1024];
        DWORD bytesRead = 0;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result += buffer;
        }

        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (hWritePipe) CloseHandle(hWritePipe);
    if (hReadPipe) CloseHandle(hReadPipe);

    return result;
}

static std::wstring CleanDrivePath(const std::wstring& path)
{
    std::wstring drive = path;
    if (drive.length() >= 2 && drive[1] == L':') {
        return drive.substr(0, 2);
    }
    return drive;
}
#endif

BitLockerStatus BitLockerGuard::QueryStatus(const std::wstring& volumePath)
{
    BitLockerStatus status;
#if defined(_WIN32)
    std::wstring drive = CleanDrivePath(volumePath);

    // Fast check: If drive letter does not exist on system, avoid spawning manage-bde
    if (drive.length() == 2 && drive[1] == L':') {
        std::wstring root = drive + L"\\";
        UINT dt = GetDriveTypeW(root.c_str());
        if (dt == DRIVE_NO_ROOT_DIR || dt == DRIVE_UNKNOWN) {
            return status;
        }
    }

    // Fast native check via fveapi.dll (FveGetStatus / InternalFveIsVolumeEncrypted)
    typedef LONG (WINAPI *PFN_InternalFveIsVolumeEncrypted)(LPCWSTR VolumeName, BOOLEAN* IsEncrypted);
    HMODULE hFve = LoadLibraryW(L"fveapi.dll");
    if (hFve) {
        auto pfnIsEnc = reinterpret_cast<PFN_InternalFveIsVolumeEncrypted>(
            GetProcAddress(hFve, "InternalFveIsVolumeEncrypted"));
        if (pfnIsEnc) {
            BOOLEAN isEnc = FALSE;
            std::wstring rootPath = (drive.length() == 2 && drive[1] == L':') ? (drive + L"\\") : drive;
            if (pfnIsEnc(rootPath.c_str(), &isEnc) == 0 && !isEnc) {
                // Volume is verified NOT encrypted via native BitLocker API - skip external subprocess
                status.isEncrypted = false;
                status.isLocked = false;
                status.isProtectionOn = false;
                FreeLibrary(hFve);
                return status;
            }
        }
        FreeLibrary(hFve);
    }

    // Query detailed status via manage-bde if volume is encrypted
    std::wstring cmd = L"manage-bde.exe -status " + drive;
    std::string output = ExecuteCommandCapture(cmd);

    status.statusString = output;
    if (output.find("Fully Encrypted") != std::string::npos ||
        output.find("Encryption in Progress") != std::string::npos ||
        output.find("Used Space Only Encrypted") != std::string::npos) {
        status.isEncrypted = true;
    }

    if (output.find("Protection On") != std::string::npos) {
        status.isProtectionOn = true;
    } else if (output.find("Protection Off") != std::string::npos) {
        status.isProtectionOn = false;
    }

    if (output.find("Locked") != std::string::npos && output.find("Unlocked") == std::string::npos) {
        status.isLocked = true;
    }

    // Parse percentage e.g. "Percentage Encrypted: 100.0%"
    size_t pos = output.find("Percentage Encrypted:");
    if (pos != std::string::npos) {
        size_t colon = output.find(':', pos);
        if (colon != std::string::npos) {
            std::string sub = output.substr(colon + 1, 10);
            try {
                double pct = std::stod(sub);
                status.encryptionPercentage = static_cast<uint32_t>(pct);
            } catch (...) {}
        }
    }
#else
    (void)volumePath;
#endif
    return status;
}

bool BitLockerGuard::SuspendProtection(const std::wstring& volumePath, uint32_t rebootCount)
{
#if defined(_WIN32)
    std::wstring drive = CleanDrivePath(volumePath);
    std::wstring cmd = L"manage-bde.exe -protectors -disable " + drive + L" -rebootcount " + std::to_wstring(rebootCount);
    std::string output = ExecuteCommandCapture(cmd);
    return output.find("Key Protectors are suspended") != std::string::npos ||
           output.find("Protection Off") != std::string::npos;
#else
    (void)volumePath; (void)rebootCount;
    return true;
#endif
}

bool BitLockerGuard::ResumeProtection(const std::wstring& volumePath)
{
#if defined(_WIN32)
    std::wstring drive = CleanDrivePath(volumePath);
    std::wstring cmd = L"manage-bde.exe -protectors -enable " + drive;
    std::string output = ExecuteCommandCapture(cmd);
    return output.find("Key Protectors are enabled") != std::string::npos ||
           output.find("Protection On") != std::string::npos;
#else
    (void)volumePath;
    return true;
#endif
}

BitLockerSuspensionScope::BitLockerSuspensionScope(const std::wstring& volumePath)
    : m_volumePath(volumePath)
    , m_wasSuspended(false)
{
    BitLockerStatus status = BitLockerGuard::QueryStatus(volumePath);
    if (status.isEncrypted && status.isProtectionOn) {
        if (BitLockerGuard::SuspendProtection(volumePath, 1)) {
            m_wasSuspended = true;
        }
    }
}

BitLockerSuspensionScope::~BitLockerSuspensionScope()
{
    if (m_wasSuspended) {
        BitLockerGuard::ResumeProtection(m_volumePath);
    }
}

} // namespace WParted
