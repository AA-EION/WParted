/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "DualNomenclature.h"

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace WParted
{

// EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
const GUID GUID_PARTITION_ESP = {
    0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};

// Microsoft Reserved: E3C9E316-0B5C-4DB8-817D-F92DF00215AE
const GUID GUID_PARTITION_MSFT_RESERVED = {
    0xE3C9E316, 0x0B5C, 0x4DB8, {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}
};

// Basic Data Partition: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
const GUID GUID_PARTITION_BASIC_DATA = {
    0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}
};

// Windows Recovery (WinRE / Diag): DE94BBA4-06D1-4D40-A16A-BFD50179D6AC
const GUID GUID_PARTITION_WINRE = {
    0xDE94BBA4, 0x06D1, 0x4D40, {0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC}
};

// Linux Data: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
const GUID GUID_PARTITION_LINUX_DATA = {
    0x0FC63DAF, 0x8483, 0x4772, {0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}
};

// Linux Swap: 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
const GUID GUID_PARTITION_LINUX_SWAP = {
    0x0657FD6D, 0xA4AB, 0x43C4, {0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}
};

// Linux LVM: E6D6D379-F507-44C2-A23C-238F2A3DF928
const GUID GUID_PARTITION_LINUX_LVM = {
    0xE6D6D379, 0xF507, 0x44C2, {0xA2, 0x3C, 0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28}
};

// Apple APFS: 7C3457EF-0000-11AA-AA11-00306543ECAC
const GUID GUID_PARTITION_APFS = {
    0x7C3457EF, 0x0000, 0x11AA, {0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}
};

bool DualNomenclature::AreGuidsEqual(const GUID& a, const GUID& b)
{
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

std::string DualNomenclature::DiskIndexToLinuxDev(int diskIndex)
{
    if (diskIndex < 0) {
        return "/dev/sd?";
    }
    std::string name = "/dev/sd";
    if (diskIndex < 26) {
        name += static_cast<char>('a' + diskIndex);
    } else {
        int first = (diskIndex / 26) - 1;
        int second = diskIndex % 26;
        name += static_cast<char>('a' + first);
        name += static_cast<char>('a' + second);
    }
    return name;
}

std::string DualNomenclature::PartitionToLinuxDev(int diskIndex, int partitionIndex)
{
    std::string dev = DiskIndexToLinuxDev(diskIndex);
    dev += std::to_string(partitionIndex);
    return dev;
}

std::string DualNomenclature::FormatByteSize(uint64_t bytes)
{
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 5) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(size < 10.0 && unitIndex > 0 ? 2 : 1) << size << " " << units[unitIndex];
    return ss.str();
}

std::string DualNomenclature::FormatDualDiskName(int diskIndex,
                                                 const std::string& busType,
                                                 const std::string& model,
                                                 uint64_t sizeBytes)
{
    std::string linuxDev = DiskIndexToLinuxDev(diskIndex);
    std::ostringstream ss;
    ss << linuxDev << " (Disk " << diskIndex;
    if (!busType.empty()) {
        ss << " - " << busType;
    }
    if (!model.empty()) {
        ss << " " << model;
    }
    if (sizeBytes > 0) {
        ss << " " << FormatByteSize(sizeBytes);
    }
    ss << ")";
    return ss.str();
}

std::string DualNomenclature::FormatDualPartitionName(int diskIndex,
                                                      int partitionIndex,
                                                      const std::string& mountPointOrDrive,
                                                      const std::string& fsType,
                                                      uint64_t sizeBytes)
{
    std::string linuxPart = PartitionToLinuxDev(diskIndex, partitionIndex);
    std::ostringstream ss;
    ss << linuxPart << " (";
    if (!mountPointOrDrive.empty()) {
        ss << mountPointOrDrive << " - ";
    }
    if (!fsType.empty()) {
        ss << fsType;
    } else {
        ss << "Unknown";
    }
    if (sizeBytes > 0) {
        ss << " " << FormatByteSize(sizeBytes);
    }
    ss << ")";
    return ss.str();
}

bool DualNomenclature::LinuxFlagToGpt(const std::string& flag, GUID& outGuid, uint64_t& outAttributes)
{
    std::string f = flag;
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return std::tolower(c); });

    outAttributes = 0;
    if (f == "boot" || f == "esp") {
        outGuid = GUID_PARTITION_ESP;
        return true;
    }
    if (f == "msftres" || f == "msr") {
        outGuid = GUID_PARTITION_MSFT_RESERVED;
        return true;
    }
    if (f == "msftdata" || f == "data") {
        outGuid = GUID_PARTITION_BASIC_DATA;
        return true;
    }
    if (f == "diag" || f == "recovery") {
        outGuid = GUID_PARTITION_WINRE;
        return true;
    }
    if (f == "hidden") {
        outGuid = GUID_PARTITION_BASIC_DATA;
        outAttributes = GPT_ATTR_HIDDEN;
        return true;
    }
    if (f == "linux" || f == "ext4" || f == "btrfs") {
        outGuid = GUID_PARTITION_LINUX_DATA;
        return true;
    }
    if (f == "swap") {
        outGuid = GUID_PARTITION_LINUX_SWAP;
        return true;
    }
    if (f == "lvm") {
        outGuid = GUID_PARTITION_LINUX_LVM;
        return true;
    }
    if (f == "apfs") {
        outGuid = GUID_PARTITION_APFS;
        return true;
    }
    return false;
}

std::vector<std::string> DualNomenclature::GptToLinuxFlags(const GUID& guid, uint64_t attributes)
{
    std::vector<std::string> flags;

    if (AreGuidsEqual(guid, GUID_PARTITION_ESP)) {
        flags.push_back("boot");
        flags.push_back("esp");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_MSFT_RESERVED)) {
        flags.push_back("msftres");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_BASIC_DATA)) {
        flags.push_back("msftdata");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_WINRE)) {
        flags.push_back("diag");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_LINUX_DATA)) {
        flags.push_back("linux");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_LINUX_SWAP)) {
        flags.push_back("swap");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_LINUX_LVM)) {
        flags.push_back("lvm");
    } else if (AreGuidsEqual(guid, GUID_PARTITION_APFS)) {
        flags.push_back("apfs");
    }

    if (attributes & GPT_ATTR_HIDDEN) {
        flags.push_back("hidden");
    }
    if (attributes & GPT_ATTR_READ_ONLY) {
        flags.push_back("readonly");
    }

    return flags;
}

bool DualNomenclature::StringToGuid(const std::string& str, GUID& guid)
{
    std::string clean;
    for (char c : str) {
        if (c != '{' && c != '}' && c != '-') {
            clean += c;
        }
    }
    if (clean.length() != 32) {
        return false;
    }

    unsigned long d1;
    unsigned int d2, d3;
    unsigned int d4[8];
    if (std::sscanf(clean.c_str(), "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                    &d1, &d2, &d3,
                    &d4[0], &d4[1], &d4[2], &d4[3],
                    &d4[4], &d4[5], &d4[6], &d4[7]) == 11) {
        guid.Data1 = static_cast<uint32_t>(d1);
        guid.Data2 = static_cast<uint16_t>(d2);
        guid.Data3 = static_cast<uint16_t>(d3);
        for (int i = 0; i < 8; ++i) {
            guid.Data4[i] = static_cast<uint8_t>(d4[i]);
        }
        return true;
    }
    return false;
}

std::string DualNomenclature::GuidToString(const GUID& guid)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1],
                  guid.Data4[2], guid.Data4[3], guid.Data4[4],
                  guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

} // namespace WParted
