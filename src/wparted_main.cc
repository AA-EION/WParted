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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#else
static bool is_running_as_admin() { return true; }
#endif

static void PrintBanner()
{
    std::cout << "===============================================================================\n";
    std::cout << "  WParted 1.0.0 — Native Windows Partition Editor (Forked from GParted)\n";
    std::cout << "  Pure Win32 Storage Engine + WSL2 Linux Bridge + Read-Only APFS Driver\n";
    std::cout << "===============================================================================\n\n";
}

int main(int argc, char* argv[])
{
    PrintBanner();

    if (!is_running_as_admin()) {
        std::cerr << "[!] Error: Administrator privileges are required to run WParted.\n";
        std::cerr << "    Please re-run this program as an Administrator.\n";
        return 1;
    }

    std::string action = "--list";
    if (argc > 1) {
        action = argv[1];
    }

    if (action == "--wsl-status") {
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

    if (action == "--apfs-inspect" && argc >= 4) {
        std::string devPathStr = argv[2];
        std::wstring devPath(devPathStr.begin(), devPathStr.end());
        uint64_t offset = std::stoull(argv[3]);

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
        } else {
            std::cerr << "[!] Failed to read APFS container header.\n";
            return 2;
        }
        return 0;
    }
    if (action == "--create-label" && argc >= 4) {
        int diskIndex = std::stoi(argv[2]);
        std::string labelType = argv[3];
        std::cout << "[*] Creating disklabel '" << labelType << "' on Disk " << diskIndex << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().CreateDiskLabel(diskIndex, labelType);
        std::cout << (ok ? "[+] Disklabel created successfully.\n" : "[!] Failed to create disklabel.\n");
        return ok ? 0 : 1;
    }

    if (action == "--create-partition" && argc >= 6) {
        int diskIndex = std::stoi(argv[2]);
        uint64_t startSec = std::stoull(argv[3]);
        uint64_t secCount = std::stoull(argv[4]);
        std::string flagType = argv[5];
        std::wstring partName = (argc >= 7) ? std::wstring(argv[6], argv[6] + strlen(argv[6])) : L"";
        std::cout << "[*] Creating partition on Disk " << diskIndex << " from sector " << startSec << " (" << secCount << " sectors)...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().CreatePartition(diskIndex, startSec, secCount, flagType, partName);
        std::cout << (ok ? "[+] Partition created successfully.\n" : "[!] Failed to create partition.\n");
        return ok ? 0 : 1;
    }

    if (action == "--delete-partition" && argc >= 4) {
        int diskIndex = std::stoi(argv[2]);
        int partNum = std::stoi(argv[3]);
        std::cout << "[*] Deleting partition " << partNum << " on Disk " << diskIndex << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().DeletePartition(diskIndex, partNum);
        std::cout << (ok ? "[+] Partition deleted successfully.\n" : "[!] Failed to delete partition.\n");
        return ok ? 0 : 1;
    }

    if (action == "--format" && argc >= 5) {
        int diskIndex = std::stoi(argv[2]);
        int partNum = std::stoi(argv[3]);
        std::string fsType = argv[4];
        std::string label = (argc >= 6) ? argv[5] : "";
        std::cout << "[*] Formatting Disk " << diskIndex << " Partition " << partNum << " as " << fsType << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().FormatFileSystem(diskIndex, partNum, fsType, label, cb);
        std::cout << "\n" << (ok ? "[+] Format completed successfully.\n" : "[!] Format failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "--resize" && argc >= 5) {
        int diskIndex = std::stoi(argv[2]);
        int partNum = std::stoi(argv[3]);
        uint64_t newSizeBytes = std::stoull(argv[4]);
        std::cout << "[*] Resizing Disk " << diskIndex << " Partition " << partNum << " to "
                  << WParted::DualNomenclature::FormatByteSize(newSizeBytes) << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().ResizePartition(diskIndex, partNum, newSizeBytes, cb);
        std::cout << "\n" << (ok ? "[+] Resize completed successfully.\n" : "[!] Resize failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "--check" && argc >= 4) {
        int diskIndex = std::stoi(argv[2]);
        int partNum = std::stoi(argv[3]);
        bool fixErrors = (argc >= 5 && std::string(argv[4]) == "--fix");
        std::cout << "[*] Running filesystem check on Disk " << diskIndex << " Partition " << partNum << "...\n";
        auto cb = [](int pct, const std::string& msg) {
            std::cout << "\r    Progress: " << pct << "% - " << msg << std::flush;
        };
        bool ok = WParted::WParted_Win32_Core::Instance().CheckFileSystem(diskIndex, partNum, fixErrors, cb);
        std::cout << "\n" << (ok ? "[+] Filesystem check completed successfully.\n" : "[!] Check reported errors or failed.\n");
        return ok ? 0 : 1;
    }

    if (action == "--toggle-flag" && argc >= 6) {
        int diskIndex = std::stoi(argv[2]);
        int partNum = std::stoi(argv[3]);
        std::string flag = argv[4];
        bool state = (std::stoi(argv[5]) != 0);
        std::cout << "[*] Setting flag '" << flag << "' = " << (state ? "on" : "off")
                  << " on Disk " << diskIndex << " Partition " << partNum << "...\n";
        bool ok = WParted::WParted_Win32_Core::Instance().ToggleFlag(diskIndex, partNum, flag, state);
        std::cout << (ok ? "[+] Flag updated successfully.\n" : "[!] Failed to update flag.\n");
        return ok ? 0 : 1;
    }

    if (action == "--schedule-boot" && argc >= 7) {
        WParted::BootJobOperation op = { 0 };
        op.opType = WParted::BOOT_OP_MOVE_SECTORS;
        op.diskIndex = static_cast<uint32_t>(std::stoul(argv[2]));
        op.sourceSector = std::stoull(argv[3]);
        op.targetSector = std::stoull(argv[4]);
        op.sectorCount = std::stoull(argv[5]);
        op.sectorSize = static_cast<uint32_t>(std::stoul(argv[6]));

        std::cout << "[*] Scheduling native boot-time sector relocation on reboot...\n";
        bool ok = WParted::WParted_Boot_Scheduler::ScheduleBootJob({ op });
        std::cout << (ok ? "[+] Boot job scheduled successfully in BootExecute.\n" : "[!] Failed to schedule boot job.\n");
        return ok ? 0 : 1;
    }

    if (action == "--cancel-boot") {
        std::cout << "[*] Canceling scheduled boot jobs and cleaning BootExecute...\n";
        bool ok = WParted::WParted_Boot_Scheduler::CancelBootJob();
        std::cout << (ok ? "[+] Scheduled boot job cancelled.\n" : "[!] Failed to cancel boot job.\n");
        return ok ? 0 : 1;
    }
    // Default: List all disks and partitions using Dual Nomenclature
    std::cout << "[*] Scanning physical storage devices...\n\n";
    auto drives = WParted::WParted_Win32_Core::Instance().ScanDevices();

    if (drives.empty()) {
        std::cout << "No physical drives detected or access denied.\n";
        return 0;
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

    return 0;
}
