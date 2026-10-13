/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_WSL_BRIDGE_H
#define WPARTED_WSL_BRIDGE_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace WParted
{

struct WslStatus
{
    bool isWslInstalled = false;
    bool isWsl2Default = false;
    bool isHelperDistroInstalled = false;
    std::string wslVersion;
    std::vector<std::string> installedDistros;
};

struct WslMountInfo
{
    int diskIndex = -1;
    std::string physicalDrivePath;
    std::string mappedLinuxDev; // e.g. "/dev/sdd"
    bool isMounted = false;
    bool isUsbFallback = false;
    std::string vhdxWrapperPath;
};

using WslProgressCallback = std::function<void(int percent, const std::string& detail)>;

class WParted_WSL_Bridge
{
public:
    static constexpr const char* HELPER_DISTRO_NAME = "wparted-wsl-engine";

    static WParted_WSL_Bridge& Instance();

    // Query WSL2 installation status and helper distro availability
    WslStatus QueryStatus();

    // Provision the lightweight helper distro
    bool ProvisionHelperDistro(const std::string& rootfsTarPath = "",
                               WslProgressCallback callback = nullptr);

    // Mount physical disk to WSL2 with --bare attachment
    bool MountBareDisk(int diskIndex, WslMountInfo& outMount);

    // Safely flush buffers and unmount physical disk from WSL2
    bool UnmountBareDisk(int diskIndex, const std::string& mappedLinuxDev = "");

    // Dynamically discover which /dev/sdX corresponds to the disk and offset
    std::string FindLinuxDeviceNode(int diskIndex, uint64_t byteOffset, const std::string& serialNumber = "");

    // Execute arbitrary command inside wparted-wsl-engine with output streaming
    bool ExecuteCommand(const std::string& command,
                        std::string& stdOut,
                        std::string& stdErr,
                        WslProgressCallback callback = nullptr);

    // File System operations routed through WSL2
    bool CheckFileSystem(const std::string& linuxDev,
                         const std::string& fsType,
                         WslProgressCallback callback = nullptr);

    bool ResizeFileSystem(const std::string& linuxDev,
                          const std::string& fsType,
                          uint64_t targetSizeBytes,
                          WslProgressCallback callback = nullptr);

    bool FormatFileSystem(const std::string& linuxDev,
                          const std::string& fsType,
                          const std::string& label,
                          WslProgressCallback callback = nullptr);

    // Explorer Integration: Mount Linux volume to Explorer UNC / drive letter
    bool MountInWindowsExplorer(const std::string& linuxDev,
                                const std::string& fsType,
                                const std::string& partitionUuid,
                                wchar_t preferredDriveLetter = L'\0');

    bool UnmountFromWindowsExplorer(const std::string& partitionUuid,
                                    wchar_t driveLetter = L'\0');

    // Parse progress strings from Linux commands (e.g., e2fsck, resize2fs)
    static int ParseProgressPercentage(const std::string& line);

private:
    WParted_WSL_Bridge();
    ~WParted_WSL_Bridge() = default;

    WParted_WSL_Bridge(const WParted_WSL_Bridge&) = delete;
    WParted_WSL_Bridge& operator=(const WParted_WSL_Bridge&) = delete;

    std::string BuildWslExecCommand(const std::string& command);
    bool MountUsbFallback(int diskIndex, WslMountInfo& outMount);
};

} // namespace WParted

#endif // WPARTED_WSL_BRIDGE_H
