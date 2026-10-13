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
