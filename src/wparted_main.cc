/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "DualNomenclature.h"
#include "WParted_Win32_Core.h"
#include "WParted_WSL_Bridge.h"
#include "ApfsDriver.h"
#include "WParted_Boot_Scheduler.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <io.h>

static bool is_running_as_admin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

static bool is_launched_from_explorer()
{
    DWORD procList[2] = { 0 };
    DWORD count = GetConsoleProcessList(procList, 2);
    return count <= 1;
}

static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), NULL, 0);
    if (size <= 0) return std::wstring();
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &out[0], size);
    return out;
}

static bool elevate_self(int argc, char* argv[], bool launchedFromExplorer)
{
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return false;

    std::wstring params;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) params += L" ";
        std::wstring warg = Utf8ToWide(argv[i]);
        bool needQuotes = warg.empty() || (warg.find(L' ') != std::wstring::npos) || (warg.find(L'\t') != std::wstring::npos);
        if (needQuotes) params += L"\"";
        for (wchar_t c : warg) {
            if (c == L'"') params += L"\\\"";
            else params += c;
        }
        if (needQuotes) params += L"\"";
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (launchedFromExplorer) {
            // When launched from Explorer, the elevated process runs in its own console.
            // Terminate un-elevated launcher immediately to avoid leaving a dead blank console.
            if (sei.hProcess) CloseHandle(sei.hProcess);
            exit(0);
        } else {
            // When launched from command line, wait for child process to propagate exit code.
            if (sei.hProcess) {
                WaitForSingleObject(sei.hProcess, INFINITE);
                DWORD exitCode = 0;
                GetExitCodeProcess(sei.hProcess, &exitCode);
                CloseHandle(sei.hProcess);
                exit(exitCode);
            }
            exit(0);
        }
        return true;
    }
    return false;
}
#else
#include <unistd.h>
static bool is_running_as_admin() { return true; }
static bool is_launched_from_explorer() { return false; }
static std::wstring Utf8ToWide(const std::string& str) { return std::wstring(str.begin(), str.end()); }
static bool elevate_self(int, char*[], bool) { return false; }
#endif

static void PrintBanner()
{
    std::cout << "===============================================================================\n";
    std::cout << "  WParted 1.0.0 — Native Windows Partition Editor (Forked from GParted)\n";
    std::cout << "  Pure Win32 Storage Engine + WSL2 Linux Bridge + Read-Only APFS Driver\n";
    std::cout << "===============================================================================\n\n";
}

static void PrintHelp()
{
    std::cout << "Usage: wparted [OPTIONS]\n\n"
              << "Options / Commands:\n"
              << "  (no arguments)                        Interactive partition editor mode\n"
              << "  --list, list, l                       List all physical disks, partitions and filesystems\n"
              << "  --wsl-status, wsl, w                  Query WSL2 helper bridge status and distros\n"
              << "  --apfs-inspect <dev> <offset>         Inspect APFS container superblock\n"
              << "  --create-label <disk#> <gpt|msdos>    Initialize disk with GPT or MBR label\n"
              << "  --create-partition <disk#> <start_sec> <count> <flag> [name]  Create partition\n"
              << "  --delete-partition <disk#> <part#>    Delete partition\n"
              << "  --format <disk#> <part#> <fs_type> [label]  Format partition filesystem\n"
              << "  --resize <disk#> <part#> <new_bytes>  Resize/shrink/extend partition\n"
              << "  --check <disk#> <part#> [--fix]       Check and repair filesystem\n"
              << "  --toggle-flag <disk#> <part#> <flag> <1|0>  Toggle partition flags\n"
              << "  --schedule-boot <disk#> <src> <dst> <cnt> <sz>  Schedule boot-time move\n"
              << "  --cancel-boot                         Cancel scheduled early-boot sector relocations\n"
              << "  --help, -h, help                      Show this help message\n\n";
}

static void PrintDisks()
{
    std::cout << "[*] Scanning physical storage devices...\n\n";
    auto drives = WParted::WParted_Win32_Core::Instance().ScanDevices();

    if (drives.empty()) {
        std::cout << "No physical drives detected or access denied.\n";
        return;
    }

    for (const auto& d : drives) {
        std::cout << "Device: " << d.GetDisplayName() << "\n";
        std::wcout << L"  Win32 Path:    " << d.devicePath << L"\n";
        std::cout << "  Partitioning:  " << (d.isGpt ? "GPT" : (d.isMbr ? "MBR" : "RAW")) << "\n";
        std::cout << "  Sector Size:   Logical: " << d.logicalSectorSize << " bytes | Physical: " << d.physicalSectorSize << " bytes\n";
        std::cout << "  Bus Type:      " << d.busType << " | TRIM Supported: " << (d.isTrimSupported ? "Yes" : "No") << "\n";
        std::cout << "  Total Sectors: " << d.sectorCount << " (" << WParted::DualNomenclature::FormatByteSize(d.totalSizeBytes) << ")\n";

        if (d.partitions.empty()) {
            std::cout << "  No partitions found on this device.\n\n";
        } else {
            std::cout << "  Partitions:\n";
            std::cout << "  " << std::left << std::setw(32) << "Dual Name"
                      << std::setw(16) << "Size"
                      << std::setw(12) << "Filesystem"
                      << "Flags / Info\n";
            std::cout << "  -----------------------------------------------------------------------------\n";

            for (const auto& p : d.partitions) {
                std::string mountStr;
                for (wchar_t wc : p.driveLetter) {
                    if (wc >= 32 && wc <= 126) mountStr += static_cast<char>(wc);
                }
                std::string fsStr;
                for (wchar_t wc : p.fileSystemName) {
                    if (wc >= 32 && wc <= 126) fsStr += static_cast<char>(wc);
                }
                std::string dualPart = WParted::DualNomenclature::FormatDualPartitionName(
                    d.diskIndex, p.partitionNumber, mountStr, fsStr, p.partitionLength);

                std::string flagsStr;
                for (const auto& f : p.flags) {
                    if (!flagsStr.empty()) flagsStr += ", ";
                    flagsStr += f;
                }
                if (p.isBitLocker) {
                    if (!flagsStr.empty()) flagsStr += ", ";
                    flagsStr += "[BitLocker Encrypted]";
                }

                std::cout << "  " << std::left << std::setw(32) << p.linuxDevPath
                          << std::setw(16) << WParted::DualNomenclature::FormatByteSize(p.partitionLength)
                          << std::setw(12) << (fsStr.empty() ? "-" : fsStr)
                          << flagsStr << "\n";
            }
            std::cout << "\n";
        }
    }
}

static bool IsMutationAction(const std::string& action, const std::vector<std::string>& args)
{
    std::string act = action;
    if (act.rfind("--", 0) == 0) act = act.substr(2);

    if (act == "create-label" ||
        act == "create-partition" ||
        act == "delete-partition" ||
        act == "format" ||
        act == "resize" ||
        act == "toggle-flag" ||
        act == "schedule-boot" ||
        act == "cancel-boot") {
        return true;
    }

    if (act == "check") {
        for (const auto& a : args) {
            if (a == "--fix") return true;
        }
    }
    return false;
}

static std::vector<std::string> TokenizeCommand(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuote = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (inQuote) {
            if (c == quoteChar) {
                inQuote = false;
            } else {
                current += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                inQuote = true;
                quoteChar = c;
            } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

static int DispatchAction(const std::vector<std::string>& tokens, bool inInteractiveShell)
{
    if (tokens.empty()) return 0;

    std::string action = tokens[0];
    if (action.rfind("--", 0) == 0) {
        action = action.substr(2);
    }

    if (action == "help" || action == "h" || action == "?" || action == "-h") {
        PrintHelp();
        return 0;
    }

    if (action == "list" || action == "l" || action == "refresh") {
        PrintDisks();
        return 0;
    }

    if (action == "wsl-status" || action == "wsl" || action == "w") {
        std::cout << "[*] Querying WSL2 Bridge Status...\n";
        auto st = WParted::WParted_WSL_Bridge::Instance().QueryStatus();
        std::cout << "    WSL Installed: " << (st.isWslInstalled ? "Yes" : "No") << "\n";
        std::cout << "    WSL2 Default: " << (st.isWsl2Default ? "Yes" : "No") << "\n";
        std::cout << "    Helper Distro Installed: " << (st.isHelperDistroInstalled ? "Yes" : "No") << "\n";
        std::cout << "    Installed Distros: ";
        for (const auto& d : st.installedDistros) std::cout << d << " ";
        std::cout << "\n";
        return 0;
    }

    if (action == "apfs-inspect") {
        if (tokens.size() < 3) {
            std::cerr << "Usage: apfs-inspect <dev> <offset>\n";
            return 1;
        }
        std::string devPathStr = tokens[1];
        std::wstring devPath = Utf8ToWide(devPathStr);
        uint64_t offset = std::stoull(tokens[2]);

        std::cout << "[*] Inspecting APFS container on " << devPathStr << " at offset " << offset << "...\n";
        WParted::ApfsContainerInfo info;
        if (WParted::ApfsDriver::ReadContainerInfo(devPath, offset, info)) {
            std::cout << "    Container UUID: " << info.containerUuid << "\n";
            std::cout << "    Block Size: " << info.blockSize << " bytes\n";
            std::cout << "    Total Blocks: " << info.blockCount << "\n";
            std::cout << "    Total Capacity: " << WParted::DualNomenclature::FormatByteSize(info.totalBytes) << "\n";
            for (const auto& v : info.volumes) {
                std::cout << "    - Volume: " << v.volumeName << " (" << v.volumeUuid << ")\n";
                std::cout << "      FileVault: " << (v.isEncrypted ? "Enabled" : "Disabled") << "\n";
                std::cout << "      Used Space: " << WParted::DualNomenclature::FormatByteSize(v.usedBytes) << "\n";
            }
            return 0;
        } else {
            std::cerr << "[!] Failed to read APFS container header.\n";
            return 2;
        }
    }

    // Mutating actions check admin privileges
    if (IsMutationAction(action, tokens) && !is_running_as_admin()) {
        std::cerr << "[!] Error: Administrator privileges are required to modify storage partitions.\n";
        if (!inInteractiveShell) {
            std::cerr << "    Please re-run this program as an Administrator.\n";
        }
        return 1;
    }

    if (action == "create-label") {
        if (tokens.size() < 3) {
            std::cerr << "Usage: create-label <disk#> <gpt|msdos>\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        std::string labelType = tokens[2];
        std::cout << "[*] Creating disklabel '" << labelType << "' on Disk " << diskIndex << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().CreateDiskLabel(diskIndex, labelType);
        std::cout << (ok ? "[+] Disklabel created successfully.\n" : "[!] Failed to create disklabel.\n");
        return ok ? 0 : 1;
    }

    if (action == "create-partition") {
        if (tokens.size() < 5) {
            std::cerr << "Usage: create-partition <disk#> <start_sec> <count> <flag> [name]\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        uint64_t startSec = std::stoull(tokens[2]);
        uint64_t secCount = std::stoull(tokens[3]);
        std::string flagType = tokens[4];
        std::wstring partName = (tokens.size() >= 6) ? Utf8ToWide(tokens[5]) : L"";
        std::cout << "[*] Creating partition on Disk " << diskIndex << " from sector " << startSec << " (" << secCount << " sectors)...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().CreatePartition(diskIndex, startSec, secCount, flagType, partName);
        std::cout << (ok ? "[+] Partition created successfully.\n" : "[!] Failed to create partition.\n");
        return ok ? 0 : 1;
    }

    if (action == "delete-partition") {
        if (tokens.size() < 3) {
            std::cerr << "Usage: delete-partition <disk#> <part#>\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        int partNum = std::stoi(tokens[2]);
        std::cout << "[*] Deleting partition " << partNum << " on Disk " << diskIndex << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().DeletePartition(diskIndex, partNum);
        std::cout << (ok ? "[+] Partition deleted successfully.\n" : "[!] Failed to delete partition.\n");
        return ok ? 0 : 1;
    }

    if (action == "format") {
        if (tokens.size() < 4) {
            std::cerr << "Usage: format <disk#> <part#> <fs_type> [label]\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        int partNum = std::stoi(tokens[2]);
        std::string fsType = tokens[3];
        std::string label = (tokens.size() >= 5) ? tokens[4] : "";
        std::cout << "[*] Formatting Disk " << diskIndex << " Partition " << partNum << " as " << fsType << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().FormatFileSystem(diskIndex, partNum, fsType, label, cb);
        std::cout << "\n" << (ok ? "[+] Format completed successfully.\n" : "[!] Format failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "resize") {
        if (tokens.size() < 4) {
            std::cerr << "Usage: resize <disk#> <part#> <new_bytes>\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        int partNum = std::stoi(tokens[2]);
        uint64_t newSizeBytes = std::stoull(tokens[3]);
        std::cout << "[*] Resizing Disk " << diskIndex << " Partition " << partNum << " to "
                  << WParted::DualNomenclature::FormatByteSize(newSizeBytes) << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().ResizePartition(diskIndex, partNum, newSizeBytes, cb);
        std::cout << "\n" << (ok ? "[+] Resize completed successfully.\n" : "[!] Resize failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "check") {
        if (tokens.size() < 3) {
            std::cerr << "Usage: check <disk#> <part#> [--fix]\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        int partNum = std::stoi(tokens[2]);
        bool fixErrors = (tokens.size() >= 4 && tokens[3] == "--fix");
        std::cout << "[*] Running filesystem check on Disk " << diskIndex << " Partition " << partNum << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().CheckFileSystem(diskIndex, partNum, fixErrors, cb);
        std::cout << "\n" << (ok ? "[+] Filesystem check completed successfully.\n" : "[!] Check reported errors or failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "toggle-flag") {
        if (tokens.size() < 5) {
            std::cerr << "Usage: toggle-flag <disk#> <part#> <flag> <1|0>\n";
            return 1;
        }
        int diskIndex = std::stoi(tokens[1]);
        int partNum = std::stoi(tokens[2]);
        std::string flag = tokens[3];
        bool state = (std::stoi(tokens[4]) != 0);
        std::cout << "[*] Setting flag '" << flag << "' = " << (state ? "on" : "off")
                  << " on Disk " << diskIndex << " Partition " << partNum << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().ToggleFlag(diskIndex, partNum, flag, state);
        std::cout << (ok ? "[+] Flag updated successfully.\n" : "[!] Failed to update flag.\n");
        return ok ? 0 : 1;
    }

    if (action == "schedule-boot") {
        if (tokens.size() < 6) {
            std::cerr << "Usage: schedule-boot <disk#> <src> <dst> <cnt> <sz>\n";
            return 1;
        }
        WParted::BootJobOperation op = { 0 };
        op.opType = WParted::BOOT_OP_MOVE_SECTORS;
        op.diskIndex = static_cast<uint32_t>(std::stoul(tokens[1]));
        op.sourceSector = std::stoull(tokens[2]);
        op.targetSector = std::stoull(tokens[3]);
        op.sectorCount = std::stoull(tokens[4]);
        op.sectorSize = static_cast<uint32_t>(std::stoul(tokens[5]));

        std::cout << "[*] Scheduling native boot-time sector relocation on reboot...\n";
        bool ok = WParted::WParted_Boot_Scheduler::ScheduleBootJob({ op });
        std::cout << (ok ? "[+] Boot job scheduled successfully in BootExecute.\n" : "[!] Failed to schedule boot job.\n");
        return ok ? 0 : 1;
    }

    if (action == "cancel-boot") {
        std::cout << "[*] Canceling scheduled boot jobs and cleaning BootExecute...\n";
        bool ok = WParted::WParted_Boot_Scheduler::CancelBootJob();
        std::cout << (ok ? "[+] Scheduled boot job cancelled.\n" : "[!] Failed to cancel boot job.\n");
        return ok ? 0 : 1;
    }

    std::cerr << "Unknown command: '" << tokens[0] << "'. Type 'help' or 'h' for usage.\n";
    return 1;
}

static void InteractiveLoop()
{
    PrintDisks();

#if defined(_WIN32)
    bool isTty = (_isatty(_fileno(stdin)) != 0);
#else
    bool isTty = (isatty(STDIN_FILENO) != 0);
#endif

    while (true) {
        if (isTty) {
            std::cout << "Commands: [l] List  [w] WSL Status  [h] Help  [q] Quit  (or type any partition command)\n";
            std::cout << "wparted> ";
        }
        std::string line;
        if (!std::getline(std::cin, line)) break;

        auto tokens = TokenizeCommand(line);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            break;
        }

        DispatchAction(tokens, true);
        if (isTty) {
            std::cout << "\n";
        }
    }
}

int main(int argc, char* argv[])
{
    PrintBanner();

    bool launchedFromExplorer = is_launched_from_explorer();
    bool runningAsAdmin = is_running_as_admin();

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    bool isInteractive = args.empty() || (args.size() == 1 && args[0] == "--interactive");

    if (isInteractive) {
        if (!runningAsAdmin) {
            if (launchedFromExplorer) {
                // Double-clicked from Explorer/shortcut: try dynamic elevation
                elevate_self(argc, argv, true);
            }
            std::cout << "[*] Notice: Running with standard user privileges (read-only mode).\n";
            std::cout << "    Administrator privileges are required for partition modifications.\n\n";
        }

        InteractiveLoop();
        return 0;
    }

    std::string action = args[0];
    if (action == "--help" || action == "-h" || action == "/?" || action == "-?") {
        PrintHelp();
#if defined(_WIN32)
        if (launchedFromExplorer && _isatty(_fileno(stdin))) {
            std::cout << "\nPress Enter to exit...";
            std::string dummy;
            std::getline(std::cin, dummy);
        }
#endif
        return 0;
    }

    bool isMutation = IsMutationAction(action, args);
    if (!runningAsAdmin && isMutation) {
        if (elevate_self(argc, argv, launchedFromExplorer)) {
            return 0;
        }
        std::cerr << "[!] Error: Administrator privileges are required to modify storage partitions.\n";
        std::cerr << "    Please re-run this program as an Administrator.\n";
#if defined(_WIN32)
        if (launchedFromExplorer && _isatty(_fileno(stdin))) {
            std::cout << "\nPress Enter to exit...";
            std::string dummy;
            std::getline(std::cin, dummy);
        }
#endif
        return 1;
    }

    if (!runningAsAdmin) {
        std::cout << "[*] Notice: Running with standard user privileges (read-only mode).\n";
        std::cout << "    Administrator privileges are required for partition modifications.\n\n";
    }

    int exitCode = DispatchAction(args, false);

#if defined(_WIN32)
    if (launchedFromExplorer && _isatty(_fileno(stdin))) {
        std::cout << "\nPress Enter to exit...";
        std::string dummy;
        std::getline(std::cin, dummy);
    }
#endif

    return exitCode;
}
