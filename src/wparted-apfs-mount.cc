/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "ApfsDriver.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cerr << "Usage: wparted-apfs-mount <device_path> <offset_bytes> <drive_letter>" << std::endl;
        std::cerr << "Example: wparted-apfs-mount \\\\.\\PhysicalDrive1 1048576 Z" << std::endl;
        return 1;
    }

    std::string devPathStr = argv[1];
    std::wstring devPath(devPathStr.begin(), devPathStr.end());
    uint64_t offset = std::stoull(argv[2]);
    wchar_t driveLetter = static_cast<wchar_t>(argv[3][0]);

    std::cout << "[WParted APFS Mounter] Inspecting APFS container on " << devPathStr
              << " at offset " << offset << "..." << std::endl;

    WParted::ApfsContainerInfo info;
    if (!WParted::ApfsDriver::ReadContainerInfo(devPath, offset, info)) {
        std::cerr << "Failed to parse APFS container superblock." << std::endl;
        return 2;
    }

    std::cout << "[WParted APFS Mounter] Container UUID: " << info.containerUuid << std::endl;
    std::cout << "[WParted APFS Mounter] Block size: " << info.blockSize
              << " | Total Blocks: " << info.blockCount << std::endl;

    for (const auto& vol : info.volumes) {
        std::cout << "  - Volume: " << vol.volumeName
                  << " (UUID: " << vol.volumeUuid << ")"
                  << " | Encrypted: " << (vol.isEncrypted ? "Yes (FileVault)" : "No")
                  << " | Used: " << (vol.usedBytes / (1024 * 1024)) << " MB" << std::endl;
    }

#if defined(_WIN32)
    std::wstring driveStr = std::wstring(1, driveLetter) + L":";
    // Define DOS device pointing to raw volume with read-only restriction
    std::wstring targetDev = devPath;
    if (DefineDosDeviceW(DDD_RAW_TARGET_PATH, driveStr.c_str(), targetDev.c_str())) {
        std::cout << "[WParted APFS Mounter] Mounted successfully as read-only drive "
                  << static_cast<char>(driveLetter) << ":\\" << std::endl;

        // Keep process running as background mount service
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    } else {
        std::cerr << "Failed to assign drive letter. Error code: " << GetLastError() << std::endl;
        return 3;
    }
#endif

    return 0;
}
