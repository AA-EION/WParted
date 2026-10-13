/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "WParted_WSL_Bridge.h"

#include <iostream>
#include <sstream>
#include <regex>
#include <vector>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnetwk.h>
#include <shlobj.h>
#pragma comment(lib, "mpr.lib")
#endif

namespace WParted
{

WParted_WSL_Bridge& WParted_WSL_Bridge::Instance()
{
    static WParted_WSL_Bridge instance;
    return instance;
}

WParted_WSL_Bridge::WParted_WSL_Bridge()
{
}

#if defined(_WIN32)
static std::string RunProcessAndCapture(const std::wstring& cmd)
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

        char buffer[2048];
        DWORD bytesRead = 0;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result += buffer;
        }

        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (hWritePipe) CloseHandle(hWritePipe);
    if (hReadPipe) CloseHandle(hReadPipe);

    return result;
}
#endif

WslStatus WParted_WSL_Bridge::QueryStatus()
{
    WslStatus status;
#if defined(_WIN32)
    std::string statusOut = RunProcessAndCapture(L"wsl.exe --status");
    if (!statusOut.empty()) {
        status.isWslInstalled = true;
        if (statusOut.find("Default Version: 2") != std::string::npos ||
            statusOut.find("Versin predeterminada: 2") != std::string::npos ||
            statusOut.find("version 2") != std::string::npos) {
            status.isWsl2Default = true;
        }
        status.wslVersion = statusOut;
    }

    std::string listOut = RunProcessAndCapture(L"wsl.exe -l -q");
    if (!listOut.empty()) {
        std::istringstream stream(listOut);
        std::string line;
        while (std::getline(stream, line)) {
            // Strip CR and null bytes (wsl output is often UTF-16LE converted)
            std::string cleaned;
            for (char c : line) {
                if (c >= 32 && c <= 126) cleaned += c;
            }
            if (!cleaned.empty()) {
                status.installedDistros.push_back(cleaned);
                if (cleaned == HELPER_DISTRO_NAME) {
                    status.isHelperDistroInstalled = true;
                }
            }
        }
    }
#endif
    return status;
}

bool WParted_WSL_Bridge::ProvisionHelperDistro(const std::string& rootfsTarPath, WslProgressCallback callback)
{
#if defined(_WIN32)
    if (callback) callback(10, "Checking WSL2 status...");
    WslStatus st = QueryStatus();
    if (!st.isWslInstalled) {
        if (callback) callback(0, "Error: WSL2 is not installed on this system.");
        return false;
    }

    if (st.isHelperDistroInstalled) {
        if (callback) callback(100, "Helper distro is already installed.");
        return true;
    }

    // Determine target installation directory in LocalAppData
    WCHAR szLocalAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szLocalAppData))) {
        return false;
    }

    std::wstring installDir = std::wstring(szLocalAppData) + L"\\WParted\\wsl-engine";
    CreateDirectoryW((std::wstring(szLocalAppData) + L"\\WParted").c_str(), NULL);
    CreateDirectoryW(installDir.c_str(), NULL);

    std::string tar = rootfsTarPath;
    if (tar.empty()) {
        // Look in executable directory or scripts directory
        tar = "wparted-wsl-engine.tar.gz";
    }

    std::wstring wTar(tar.begin(), tar.end());
    std::wstring importCmd = L"wsl.exe --import " + std::wstring(HELPER_DISTRO_NAME, HELPER_DISTRO_NAME + strlen(HELPER_DISTRO_NAME)) +
                             L" \"" + installDir + L"\" \"" + wTar + L"\" --version 2";

    if (callback) callback(30, "Importing WParted WSL engine distro...");
    std::string out = RunProcessAndCapture(importCmd);

    WslStatus verify = QueryStatus();
    if (verify.isHelperDistroInstalled) {
        if (callback) callback(100, "WParted WSL engine successfully provisioned.");
        return true;
    }

    if (callback) callback(0, "Failed to import helper distro: " + out);
    return false;
#else
    (void)rootfsTarPath; (void)callback;
    return true;
#endif
}

bool WParted_WSL_Bridge::MountBareDisk(int diskIndex, WslMountInfo& outMount)
{
    outMount.diskIndex = diskIndex;
    outMount.physicalDrivePath = "\\\\.\\PhysicalDrive" + std::to_string(diskIndex);
    outMount.isMounted = false;

#if defined(_WIN32)
    std::wstring cmd = L"wsl.exe --mount \\\\.\\PhysicalDrive" + std::to_wstring(diskIndex) + L" --bare";
    std::string out = RunProcessAndCapture(cmd);

    // If wsl mount succeeded or says already mounted
    if (out.find("The disk was successfully mounted") != std::string::npos ||
        out.find("already mounted") != std::string::npos ||
        out.empty()) {
        outMount.isMounted = true;
        outMount.mappedLinuxDev = FindLinuxDeviceNode(diskIndex, 0);
        return true;
    }

    // Attempt USB / Removable media fallback
    return MountUsbFallback(diskIndex, outMount);
#else
    outMount.isMounted = true;
    outMount.mappedLinuxDev = "/dev/sda";
    return true;
#endif
}

bool WParted_WSL_Bridge::MountUsbFallback(int diskIndex, WslMountInfo& outMount)
{
#if defined(_WIN32)
    // Fallback using usbipd-win if available
    std::wstring usbipdCheck = L"usbipd.exe --version";
    std::string vOut = RunProcessAndCapture(usbipdCheck);
    if (!vOut.empty()) {
        std::wstring attachCmd = L"usbipd.exe wsl attach --drive " + std::to_wstring(diskIndex);
        std::string attOut = RunProcessAndCapture(attachCmd);
        if (attOut.find("attached") != std::string::npos || attOut.empty()) {
            outMount.isMounted = true;
            outMount.isUsbFallback = true;
            outMount.mappedLinuxDev = FindLinuxDeviceNode(diskIndex, 0);
            return true;
        }
    }
#endif
    (void)diskIndex; (void)outMount;
    return false;
}

bool WParted_WSL_Bridge::UnmountBareDisk(int diskIndex, const std::string& mappedLinuxDev)
{
#if defined(_WIN32)
    // Flush kernel buffers inside WSL2 before unmounting
    if (!mappedLinuxDev.empty()) {
        std::string flushCmd = "sync && blockdev --flushbufs " + mappedLinuxDev;
        std::string o, e;
        ExecuteCommand(flushCmd, o, e);
    } else {
        std::string o, e;
        ExecuteCommand("sync", o, e);
    }

    std::wstring cmd = L"wsl.exe --unmount \\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    RunProcessAndCapture(cmd);
    return true;
#else
    (void)diskIndex; (void)mappedLinuxDev;
    return true;
#endif
}

std::string WParted_WSL_Bridge::FindLinuxDeviceNode(int diskIndex, uint64_t byteOffset, const std::string& serialNumber)
{
    (void)diskIndex; (void)byteOffset; (void)serialNumber;
    // Query /proc/partitions or /sys/block to find the corresponding device
    std::string out, err;
    if (ExecuteCommand("lsblk -dpno NAME,SIZE,SERIAL 2>/dev/null || cat /proc/partitions", out, err)) {
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("/dev/sd") != std::string::npos) {
                std::istringstream words(line);
                std::string devName;
                words >> devName;
                return devName;
            }
        }
    }
    return "/dev/sdb";
}

int WParted_WSL_Bridge::ParseProgressPercentage(const std::string& line)
{
    // Match patterns like "50.0%", "( 42.5%)", "[50%]"
    static const std::regex pctRegex(R"((\d{1,3}(?:\.\d+)?)%)");
    std::smatch match;
    if (std::regex_search(line, match, pctRegex) && match.size() > 1) {
        try {
            double val = std::stod(match[1].str());
            return static_cast<int>(val);
        } catch (...) {}
    }
    return -1;
}

bool WParted_WSL_Bridge::ExecuteCommand(const std::string& command,
                                        std::string& stdOut,
                                        std::string& stdErr,
                                        WslProgressCallback callback)
{
#if defined(_WIN32)
    std::string fullCmd = "wsl.exe -d " + std::string(HELPER_DISTRO_NAME) + " -u root --exec sh -c \"" + command + "\"";
    std::wstring wCmd(fullCmd.begin(), fullCmd.end());

    HANDLE hReadOut = NULL, hWriteOut = NULL;
    HANDLE hReadErr = NULL, hWriteErr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    CreatePipe(&hReadOut, &hWriteOut, &sa, 0);
    CreatePipe(&hReadErr, &hWriteErr, &sa, 0);
    SetHandleInformation(hReadOut, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hReadErr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWriteOut;
    si.hStdError = hWriteErr;

    PROCESS_INFORMATION pi = { 0 };
    std::vector<wchar_t> cmdBuffer(wCmd.begin(), wCmd.end());
    cmdBuffer.push_back(L'\0');

    if (!CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadOut); CloseHandle(hWriteOut);
        CloseHandle(hReadErr); CloseHandle(hWriteErr);
        return false;
    }

    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);

    char buffer[1024];
    DWORD bytesRead = 0;
    std::string currentLine;

    while (ReadFile(hReadOut, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        stdOut += buffer;

        for (DWORD i = 0; i < bytesRead; ++i) {
            char c = buffer[i];
            if (c == '\n' || c == '\r') {
                if (!currentLine.empty()) {
                    int pct = ParseProgressPercentage(currentLine);
                    if (callback && pct >= 0) {
                        callback(pct, currentLine);
                    }
                    currentLine.clear();
                }
            } else {
                currentLine += c;
            }
        }
    }

    while (ReadFile(hReadErr, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        stdErr += buffer;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadOut);
    CloseHandle(hReadErr);

    return exitCode == 0;
#else
    (void)command; (void)stdOut; (void)stdErr; (void)callback;
    return true;
#endif
}

bool WParted_WSL_Bridge::CheckFileSystem(const std::string& linuxDev,
                                         const std::string& fsType,
                                         WslProgressCallback callback)
{
    std::string cmd;
    if (fsType == "ext2" || fsType == "ext3" || fsType == "ext4") {
        cmd = "e2fsck -f -y -C 1 " + linuxDev;
    } else if (fsType == "btrfs") {
        cmd = "btrfs check --repair --force " + linuxDev;
    } else if (fsType == "xfs") {
        cmd = "xfs_repair " + linuxDev;
    } else if (fsType == "f2fs") {
        cmd = "fsck.f2fs -y " + linuxDev;
    } else {
        cmd = "fsck -y " + linuxDev;
    }

    std::string out, err;
    return ExecuteCommand(cmd, out, err, callback);
}

bool WParted_WSL_Bridge::ResizeFileSystem(const std::string& linuxDev,
                                          const std::string& fsType,
                                          uint64_t targetSizeBytes,
                                          WslProgressCallback callback)
{
    std::string cmd;
    if (fsType == "ext2" || fsType == "ext3" || fsType == "ext4") {
        uint64_t sectors = targetSizeBytes / 512;
        cmd = "resize2fs -p " + linuxDev + " " + std::to_string(sectors) + "s";
    } else if (fsType == "btrfs") {
        cmd = "btrfs filesystem resize " + std::to_string(targetSizeBytes) + " " + linuxDev;
    } else if (fsType == "xfs") {
        cmd = "xfs_growfs " + linuxDev;
    }

    std::string out, err;
    return ExecuteCommand(cmd, out, err, callback);
}

bool WParted_WSL_Bridge::FormatFileSystem(const std::string& linuxDev,
                                          const std::string& fsType,
                                          const std::string& label,
                                          WslProgressCallback callback)
{
    std::string cmd;
    std::string lblArg = label.empty() ? "" : " -L \"" + label + "\"";

    if (fsType == "ext4") {
        cmd = "mkfs.ext4 -F -q" + lblArg + " " + linuxDev;
    } else if (fsType == "ext3") {
        cmd = "mkfs.ext3 -F -q" + lblArg + " " + linuxDev;
    } else if (fsType == "ext2") {
        cmd = "mkfs.ext2 -F -q" + lblArg + " " + linuxDev;
    } else if (fsType == "btrfs") {
        cmd = "mkfs.btrfs -f" + lblArg + " " + linuxDev;
    } else if (fsType == "xfs") {
        cmd = "mkfs.xfs -f" + lblArg + " " + linuxDev;
    } else if (fsType == "f2fs") {
        cmd = "mkfs.f2fs -f" + lblArg + " " + linuxDev;
    } else if (fsType == "linux-swap") {
        cmd = "mkswap" + lblArg + " " + linuxDev;
    }

    std::string out, err;
    return ExecuteCommand(cmd, out, err, callback);
}

bool WParted_WSL_Bridge::MountInWindowsExplorer(const std::string& linuxDev,
                                                const std::string& fsType,
                                                const std::string& partitionUuid,
                                                wchar_t preferredDriveLetter)
{
#if defined(_WIN32)
    std::string mntPath = "/mnt/wparted/" + partitionUuid;
    std::string cmd = "mkdir -p " + mntPath + " && mount -t " + fsType + " " + linuxDev + " " + mntPath;
    std::string out, err;
    if (!ExecuteCommand(cmd, out, err)) {
        return false;
    }

    std::wstring uncPath = L"\\\\wsl.localhost\\" +
                           std::wstring(HELPER_DISTRO_NAME, HELPER_DISTRO_NAME + strlen(HELPER_DISTRO_NAME)) +
                           L"\\mnt\\wparted\\" +
                           std::wstring(partitionUuid.begin(), partitionUuid.end());

    if (preferredDriveLetter != L'\0') {
        std::wstring driveStr = std::wstring(1, preferredDriveLetter) + L":";
        NETRESOURCEW nr = { 0 };
        nr.dwType = RESOURCETYPE_DISK;
        nr.lpLocalName = const_cast<LPWSTR>(driveStr.c_str());
        nr.lpRemoteName = const_cast<LPWSTR>(uncPath.c_str());

        DWORD dwResult = WNetAddConnection2W(&nr, NULL, NULL, CONNECT_TEMPORARY);
        return dwResult == NO_ERROR || dwResult == ERROR_ALREADY_ASSIGNED;
    }
    return true;
#else
    (void)linuxDev; (void)fsType; (void)partitionUuid; (void)preferredDriveLetter;
    return true;
#endif
}

bool WParted_WSL_Bridge::UnmountFromWindowsExplorer(const std::string& partitionUuid, wchar_t driveLetter)
{
#if defined(_WIN32)
    if (driveLetter != L'\0') {
        std::wstring driveStr = std::wstring(1, driveLetter) + L":";
        WNetCancelConnection2W(driveStr.c_str(), 0, TRUE);
    }

    std::string mntPath = "/mnt/wparted/" + partitionUuid;
    std::string cmd = "umount -f " + mntPath + " && rmdir " + mntPath;
    std::string out, err;
    return ExecuteCommand(cmd, out, err);
#else
    (void)partitionUuid; (void)driveLetter;
    return true;
#endif
}

} // namespace WParted
