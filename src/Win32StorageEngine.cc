/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "Win32StorageEngine.h"
#include "BitLockerGuard.h"

#include <iostream>
#include <vector>
#include <memory>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#pragma comment(lib, "setupapi.lib")
#endif

namespace WParted
{

Win32StorageEngine& Win32StorageEngine::Instance()
{
    static Win32StorageEngine instance;
    return instance;
}

Win32StorageEngine::Win32StorageEngine()
{
}

static std::string BusTypeToString(DWORD busType)
{
    switch (busType) {
        case 0x11: return "NVMe";  // BusTypeNvme
        case 0x0B: return "SATA";  // BusTypeSata
        case 0x07: return "USB";   // BusTypeUsb
        case 0x01: return "SCSI";  // BusTypeScsi
        case 0x03: return "ATA";   // BusTypeAta
        case 0x08: return "RAID";  // BusTypeRAID
        case 0x0E: return "SD";    // BusTypeSd
        case 0x0F: return "MMC";   // BusTypeMmc
        case 0x10: return "Virtual"; // BusTypeVirtual
        default:   return "Storage";
    }
}

std::vector<Win32DiskDrive> Win32StorageEngine::EnumerateDisks()
{
    std::vector<Win32DiskDrive> drives;

#if defined(_WIN32)
    // SetupDi enumeration for disk devices
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_DISK,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA interfaceData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_DISK, i, &interfaceData); ++i) {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, NULL, 0, &requiredSize, NULL);
            if (requiredSize == 0) continue;

            std::vector<BYTE> detailBuffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, pDetail, requiredSize, NULL, NULL)) {
                HANDLE hDisk = CreateFileW(
                    pDetail->DevicePath,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL
                );

                if (hDisk != INVALID_HANDLE_VALUE) {
                    STORAGE_DEVICE_NUMBER devNum = { 0 };
                    DWORD bytesReturned = 0;
                    if (DeviceIoControl(hDisk, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &devNum, sizeof(devNum), &bytesReturned, NULL)) {
                        Win32DiskDrive drive;
                        drive.diskIndex = devNum.DeviceNumber;
                        drive.devicePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(drive.diskIndex);
                        drive.linuxDevPath = DualNomenclature::DiskIndexToLinuxDev(drive.diskIndex);

                        CloseHandle(hDisk);

                        if (QueryDiskProperties(drive.diskIndex, drive)) {
                            ReadDriveLayout(drive.diskIndex, drive);
                            ResolveVolumeMounts(drive);
                            drives.push_back(drive);
                        }
                        continue;
                    }
                    CloseHandle(hDisk);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    // Direct PhysicalDrive scan fallback if SetupDi found no drives
    if (drives.empty()) {
        for (int i = 0; i < 32; ++i) {
            Win32DiskDrive drive;
            drive.diskIndex = i;
            drive.devicePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
            drive.linuxDevPath = DualNomenclature::DiskIndexToLinuxDev(i);

            if (QueryDiskProperties(i, drive)) {
                ReadDriveLayout(i, drive);
                ResolveVolumeMounts(drive);
                drives.push_back(drive);
            }
        }
    }
#endif

    return drives;
}

bool Win32StorageEngine::QueryDiskProperties(int diskIndex, Win32DiskDrive& outDisk)
{
#if defined(_WIN32)
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    HANDLE hDisk = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        hDisk = CreateFileW(
            path.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
    }

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Query Geometry & Size
    GET_LENGTH_INFORMATION lengthInfo = { 0 };
    DWORD bytesReturned = 0;
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof(lengthInfo), &bytesReturned, NULL)) {
        outDisk.totalSizeBytes = static_cast<uint64_t>(lengthInfo.Length.QuadPart);
    }

    DISK_GEOMETRY_EX geomEx = { 0 };
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geomEx, sizeof(geomEx), &bytesReturned, NULL)) {
        outDisk.logicalSectorSize = geomEx.Geometry.BytesPerSector ? geomEx.Geometry.BytesPerSector : 512;
        if (outDisk.totalSizeBytes == 0) {
            outDisk.totalSizeBytes = static_cast<uint64_t>(geomEx.DiskSize.QuadPart);
        }
    } else {
        outDisk.logicalSectorSize = 512;
    }

    if (outDisk.logicalSectorSize > 0) {
        outDisk.sectorCount = outDisk.totalSizeBytes / outDisk.logicalSectorSize;
    }

    // Query Device Properties (BusType, Model, Serial, Removable)
    STORAGE_PROPERTY_QUERY query = { StorageDeviceProperty, PropertyStandardQuery };
    std::vector<BYTE> buffer(4096, 0);

    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesReturned, NULL)) {
        PSTORAGE_DEVICE_DESCRIPTOR desc = reinterpret_cast<PSTORAGE_DEVICE_DESCRIPTOR>(buffer.data());
        outDisk.busType = BusTypeToString(desc->BusType);
        outDisk.isRemovable = (desc->RemovableMedia != FALSE);

        if (desc->ProductIdOffset > 0 && desc->ProductIdOffset < buffer.size()) {
            outDisk.model = reinterpret_cast<const char*>(buffer.data() + desc->ProductIdOffset);
            // Trim trailing spaces
            while (!outDisk.model.empty() && (outDisk.model.back() == ' ' || outDisk.model.back() == '\0')) {
                outDisk.model.pop_back();
            }
        }
        if (desc->VendorIdOffset > 0 && desc->VendorIdOffset < buffer.size()) {
            outDisk.vendorId = reinterpret_cast<const char*>(buffer.data() + desc->VendorIdOffset);
            while (!outDisk.vendorId.empty() && (outDisk.vendorId.back() == ' ' || outDisk.vendorId.back() == '\0')) {
                outDisk.vendorId.pop_back();
            }
        }
        if (desc->SerialNumberOffset > 0 && desc->SerialNumberOffset < buffer.size()) {
            outDisk.serialNumber = reinterpret_cast<const char*>(buffer.data() + desc->SerialNumberOffset);
            while (!outDisk.serialNumber.empty() && (outDisk.serialNumber.back() == ' ' || outDisk.serialNumber.back() == '\0')) {
                outDisk.serialNumber.pop_back();
            }
        }
    }

    // Query Alignment / Physical Sector Size (512e vs 4Kn)
    STORAGE_PROPERTY_QUERY alignQuery = { StorageAccessAlignmentProperty, PropertyStandardQuery };
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignDesc = { 0 };
    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &alignQuery, sizeof(alignQuery), &alignDesc, sizeof(alignDesc), &bytesReturned, NULL)) {
        outDisk.physicalSectorSize = alignDesc.BytesPerPhysicalSector ? alignDesc.BytesPerPhysicalSector : outDisk.logicalSectorSize;
    } else {
        outDisk.physicalSectorSize = outDisk.logicalSectorSize;
    }

    // Query TRIM capability
#ifndef StorageDeviceTrimProperty
#define StorageDeviceTrimProperty static_cast<STORAGE_PROPERTY_ID>(8)
#endif
    STORAGE_PROPERTY_QUERY trimQuery = { StorageDeviceTrimProperty, PropertyStandardQuery };
    struct {
        DWORD Version;
        DWORD Size;
        BOOLEAN TrimEnabled;
    } trimDesc = { 0 };
    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &trimQuery, sizeof(trimQuery), &trimDesc, sizeof(trimDesc), &bytesReturned, NULL)) {
        outDisk.isTrimSupported = (trimDesc.TrimEnabled != FALSE);
    }

    CloseHandle(hDisk);
    return true;
#else
    (void)diskIndex; (void)outDisk;
    return false;
#endif
}

bool Win32StorageEngine::ReadDriveLayout(int diskIndex, Win32DiskDrive& disk)
{
#if defined(_WIN32)
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    HANDLE hDisk = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<BYTE> buffer(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX), 0);
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesReturned, NULL)) {
        CloseHandle(hDisk);
        return false;
    }

    PDRIVE_LAYOUT_INFORMATION_EX pLayout = reinterpret_cast<PDRIVE_LAYOUT_INFORMATION_EX>(buffer.data());
    disk.partitions.clear();

    if (pLayout->PartitionStyle == PARTITION_STYLE_GPT) {
        disk.isGpt = true;
        disk.isMbr = false;
        disk.gptDiskId = pLayout->Gpt.DiskId;
        disk.gptStartingUsableOffset = static_cast<uint64_t>(pLayout->Gpt.StartingUsableOffset.QuadPart);
        disk.gptUsableLength = static_cast<uint64_t>(pLayout->Gpt.UsableLength.QuadPart);
        disk.gptMaxPartitionCount = pLayout->Gpt.MaxPartitionCount;

        for (DWORD i = 0; i < pLayout->PartitionCount; ++i) {
            const PARTITION_INFORMATION_EX& entry = pLayout->PartitionEntry[i];
            if (entry.PartitionLength.QuadPart == 0) continue;

            Win32DiskPartition part;
            part.partitionNumber = entry.PartitionNumber;
            part.startingOffset = static_cast<uint64_t>(entry.StartingOffset.QuadPart);
            part.partitionLength = static_cast<uint64_t>(entry.PartitionLength.QuadPart);
            part.isGpt = true;
            part.gptTypeGuid = entry.Gpt.PartitionType;
            part.gptIdGuid = entry.Gpt.PartitionId;
            part.gptAttributes = entry.Gpt.Attributes;
            part.gptName = entry.Gpt.Name;
            part.linuxDevPath = DualNomenclature::PartitionToLinuxDev(diskIndex, part.partitionNumber);
            part.flags = DualNomenclature::GptToLinuxFlags(part.gptTypeGuid, part.gptAttributes);

            disk.partitions.push_back(part);
        }
    } else if (pLayout->PartitionStyle == PARTITION_STYLE_MBR) {
        disk.isGpt = false;
        disk.isMbr = true;
        disk.mbrSignature = pLayout->Mbr.Signature;

        for (DWORD i = 0; i < pLayout->PartitionCount; ++i) {
            const PARTITION_INFORMATION_EX& entry = pLayout->PartitionEntry[i];
            if (entry.PartitionLength.QuadPart == 0 || entry.Mbr.PartitionType == 0) continue;

            Win32DiskPartition part;
            part.partitionNumber = entry.PartitionNumber;
            part.startingOffset = static_cast<uint64_t>(entry.StartingOffset.QuadPart);
            part.partitionLength = static_cast<uint64_t>(entry.PartitionLength.QuadPart);
            part.isGpt = false;
            part.mbrType = entry.Mbr.PartitionType;
            part.mbrBootIndicator = (entry.Mbr.BootIndicator != FALSE);
            part.linuxDevPath = DualNomenclature::PartitionToLinuxDev(diskIndex, part.partitionNumber);
            if (part.mbrBootIndicator) {
                part.flags.push_back("boot");
            }

            disk.partitions.push_back(part);
        }
    }

    CloseHandle(hDisk);
    return true;
#else
    (void)diskIndex; (void)disk;
    return false;
#endif
}

bool Win32StorageEngine::WriteDriveLayout(int diskIndex, const Win32DiskDrive& disk)
{
#if defined(_WIN32)
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    HANDLE hDisk = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    size_t count = disk.partitions.size();
    DWORD partitionCount = static_cast<DWORD>(count);
    if (!disk.isGpt) {
        // Windows IOCTL_DISK_SET_DRIVE_LAYOUT_EX strictly requires MBR PartitionCount to be a multiple of 4
        partitionCount = static_cast<DWORD>(((count + 3) / 4) * 4);
        if (partitionCount < 4) partitionCount = 4;
    }

    size_t bufSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + partitionCount * sizeof(PARTITION_INFORMATION_EX);
    std::vector<BYTE> buffer(bufSize, 0);
    PDRIVE_LAYOUT_INFORMATION_EX pLayout = reinterpret_cast<PDRIVE_LAYOUT_INFORMATION_EX>(buffer.data());

    pLayout->PartitionStyle = disk.isGpt ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
    pLayout->PartitionCount = partitionCount;

    if (disk.isGpt) {
        // Preserve original GPT DiskId or generate a unique random GUID (prevents Windows BCD boot failure)
        pLayout->Gpt.DiskId = disk.gptDiskId;
        if (DualNomenclature::AreGuidsEqual(pLayout->Gpt.DiskId, { 0 })) {
            CoCreateGuid(&pLayout->Gpt.DiskId);
        }

        uint32_t secSize = disk.logicalSectorSize ? disk.logicalSectorSize : 512;
        pLayout->Gpt.StartingUsableOffset.QuadPart = disk.gptStartingUsableOffset > 0 ?
            static_cast<LONGLONG>(disk.gptStartingUsableOffset) : static_cast<LONGLONG>(34 * secSize);
        pLayout->Gpt.UsableLength.QuadPart = disk.gptUsableLength > 0 ?
            static_cast<LONGLONG>(disk.gptUsableLength) : static_cast<LONGLONG>(disk.totalSizeBytes - (68 * secSize));
        pLayout->Gpt.MaxPartitionCount = disk.gptMaxPartitionCount > 0 ? disk.gptMaxPartitionCount : 128;

        for (size_t i = 0; i < count; ++i) {
            const auto& src = disk.partitions[i];
            auto& dst = pLayout->PartitionEntry[i];
            dst.PartitionStyle = PARTITION_STYLE_GPT;
            dst.StartingOffset.QuadPart = static_cast<LONGLONG>(src.startingOffset);
            dst.PartitionLength.QuadPart = static_cast<LONGLONG>(src.partitionLength);
            dst.PartitionNumber = static_cast<DWORD>(src.partitionNumber);
            dst.RewritePartition = TRUE;
            dst.Gpt.PartitionType = src.gptTypeGuid;
            dst.Gpt.PartitionId = src.gptIdGuid;
            if (DualNomenclature::AreGuidsEqual(dst.Gpt.PartitionId, { 0 })) {
                CoCreateGuid(&dst.Gpt.PartitionId);
            }
            dst.Gpt.Attributes = src.gptAttributes;
            wcsncpy_s(dst.Gpt.Name, src.gptName.c_str(), 36);
        }
    } else {
        // Preserve MBR disk signature
        pLayout->Mbr.Signature = disk.mbrSignature != 0 ? disk.mbrSignature : static_cast<DWORD>(GetTickCount64() ^ 0x57504152);
        for (size_t i = 0; i < count; ++i) {
            const auto& src = disk.partitions[i];
            auto& dst = pLayout->PartitionEntry[i];
            dst.PartitionStyle = PARTITION_STYLE_MBR;
            dst.StartingOffset.QuadPart = static_cast<LONGLONG>(src.startingOffset);
            dst.PartitionLength.QuadPart = static_cast<LONGLONG>(src.partitionLength);
            dst.PartitionNumber = static_cast<DWORD>(src.partitionNumber);
            dst.RewritePartition = TRUE;
            dst.Mbr.PartitionType = src.mbrType;
            dst.Mbr.BootIndicator = src.mbrBootIndicator ? TRUE : FALSE;
            dst.Mbr.RecognizedPartition = TRUE;
        }
        for (size_t i = count; i < partitionCount; ++i) {
            auto& dst = pLayout->PartitionEntry[i];
            dst.PartitionStyle = PARTITION_STYLE_MBR;
            dst.StartingOffset.QuadPart = 0;
            dst.PartitionLength.QuadPart = 0;
            dst.PartitionNumber = 0;
            dst.RewritePartition = FALSE;
            dst.Mbr.PartitionType = 0; // PARTITION_ENTRY_UNUSED
            dst.Mbr.BootIndicator = FALSE;
            dst.Mbr.RecognizedPartition = FALSE;
        }
    }

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDisk, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, buffer.data(), static_cast<DWORD>(bufSize), NULL, 0, &bytesReturned, NULL);
    CloseHandle(hDisk);

    if (ok) {
        UpdateDiskProperties(diskIndex);
    }

    return ok != FALSE;
#else
    (void)diskIndex; (void)disk;
    return false;
#endif
}

bool Win32StorageEngine::InitializeDisk(int diskIndex, bool asGpt)
{
#if defined(_WIN32)
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    HANDLE hDisk = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    CREATE_DISK createDisk = { 0 };
    createDisk.PartitionStyle = asGpt ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
    if (asGpt) {
        CoCreateGuid(&createDisk.Gpt.DiskId);
        createDisk.Gpt.MaxPartitionCount = 128;
    } else {
        createDisk.Mbr.Signature = static_cast<DWORD>(GetTickCount64() ^ 0x57504152);
    }

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDisk, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), NULL, 0, &bytesReturned, NULL);
    CloseHandle(hDisk);

    if (ok) {
        UpdateDiskProperties(diskIndex);
    }
    return ok != FALSE;
#else
    (void)diskIndex; (void)asGpt;
    return false;
#endif
}

bool Win32StorageEngine::UpdateDiskProperties(int diskIndex)
{
#if defined(_WIN32)
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);
    HANDLE hDisk = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytesReturned, NULL);
    CloseHandle(hDisk);
    return ok != FALSE;
#else
    (void)diskIndex;
    return true;
#endif
}

bool Win32StorageEngine::CreatePartition(int diskIndex,
                                         uint64_t startOffset,
                                         uint64_t length,
                                         const GUID& typeGuid,
                                         const std::wstring& name)
{
    Win32DiskDrive disk;
    if (!QueryDiskProperties(diskIndex, disk) || !ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    Win32DiskPartition newPart;
    newPart.partitionNumber = static_cast<int>(disk.partitions.size() + 1);
    newPart.startingOffset = startOffset;
    newPart.partitionLength = length;
    newPart.isGpt = disk.isGpt;
    newPart.gptTypeGuid = typeGuid;
    CoCreateGuid(&newPart.gptIdGuid);
    newPart.gptName = name;
    newPart.flags = DualNomenclature::GptToLinuxFlags(typeGuid, 0);

    disk.partitions.push_back(newPart);
    return WriteDriveLayout(diskIndex, disk);
}

bool Win32StorageEngine::DeletePartition(int diskIndex, int partitionNumber)
{
    Win32DiskDrive disk;
    if (!QueryDiskProperties(diskIndex, disk) || !ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    auto it = disk.partitions.begin();
    while (it != disk.partitions.end()) {
        if (it->partitionNumber == partitionNumber) {
            it = disk.partitions.erase(it);
        } else {
            ++it;
        }
    }

    return WriteDriveLayout(diskIndex, disk);
}

bool Win32StorageEngine::TogglePartitionFlag(int diskIndex, int partitionNumber, const std::string& flag, bool state)
{
    Win32DiskDrive disk;
    if (!QueryDiskProperties(diskIndex, disk) || !ReadDriveLayout(diskIndex, disk)) {
        return false;
    }

    for (auto& part : disk.partitions) {
        if (part.partitionNumber == partitionNumber) {
            if (disk.isGpt) {
                if (flag == "hidden") {
                    if (state) {
                        part.gptAttributes |= GPT_ATTR_HIDDEN;
                    } else {
                        part.gptAttributes &= ~GPT_ATTR_HIDDEN;
                    }
                } else {
                    GUID guid;
                    uint64_t attrs = 0;
                    if (DualNomenclature::LinuxFlagToGpt(flag, guid, attrs)) {
                        part.gptTypeGuid = guid;
                        part.gptAttributes |= attrs;
                    }
                }
                part.flags = DualNomenclature::GptToLinuxFlags(part.gptTypeGuid, part.gptAttributes);
            } else {
                if (flag == "boot") {
                    part.mbrBootIndicator = state;
                }
            }
            return WriteDriveLayout(diskIndex, disk);
        }
    }
    return false;
}

void Win32StorageEngine::ResolveVolumeMounts(Win32DiskDrive& disk)
{
#if defined(_WIN32)
    WCHAR szVolumeName[MAX_PATH] = { 0 };
    HANDLE hFind = FindFirstVolumeW(szVolumeName, ARRAYSIZE(szVolumeName));
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        // Strip trailing backslash for CreateFileW
        size_t len = wcslen(szVolumeName);
        std::wstring volDevice = szVolumeName;
        if (len > 0 && volDevice.back() == L'\\') {
            volDevice.pop_back();
        }

        HANDLE hVol = CreateFileW(
            volDevice.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hVol != INVALID_HANDLE_VALUE) {
            std::vector<BYTE> extBuf(sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT), 0);
            DWORD bytesReturned = 0;

            if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, extBuf.data(), static_cast<DWORD>(extBuf.size()), &bytesReturned, NULL)) {
                PVOLUME_DISK_EXTENTS pExt = reinterpret_cast<PVOLUME_DISK_EXTENTS>(extBuf.data());
                for (DWORD e = 0; e < pExt->NumberOfDiskExtents; ++e) {
                    if (static_cast<int>(pExt->Extents[e].DiskNumber) == disk.diskIndex) {
                        uint64_t extStart = static_cast<uint64_t>(pExt->Extents[e].StartingOffset.QuadPart);
                        // Find matching partition
                        for (auto& part : disk.partitions) {
                            if (part.startingOffset == extStart ||
                                (part.startingOffset <= extStart && part.startingOffset + part.partitionLength > extStart)) {
                                part.volumeGuidPath = szVolumeName;

                                // Drive letter
                                WCHAR szPathNames[MAX_PATH] = { 0 };
                                DWORD returnLength = 0;
                                if (GetVolumePathNamesForVolumeNameW(szVolumeName, szPathNames, ARRAYSIZE(szPathNames), &returnLength)) {
                                    part.driveLetter = szPathNames;
                                }

                                // Volume label and file system
                                WCHAR szLabel[MAX_PATH] = { 0 };
                                WCHAR szFs[MAX_PATH] = { 0 };
                                if (GetVolumeInformationW(szVolumeName, szLabel, ARRAYSIZE(szLabel), NULL, NULL, NULL, szFs, ARRAYSIZE(szFs))) {
                                    part.volumeLabel = szLabel;
                                    part.fileSystemName = szFs;
                                }

                                // Check BitLocker status
                                if (!part.driveLetter.empty()) {
                                    BitLockerStatus bl = BitLockerGuard::QueryStatus(part.driveLetter);
                                    part.isBitLocker = bl.isEncrypted;
                                }
                            }
                        }
                    }
                }
            }
            CloseHandle(hVol);
        }
    } while (FindNextVolumeW(hFind, szVolumeName, ARRAYSIZE(szVolumeName)));

    FindVolumeClose(hFind);
#else
    (void)disk;
#endif
}

} // namespace WParted
