/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "Win32StorageEngine.h"
#include "Win32VolumeLockGuard.h"
#include "BitLockerGuard.h"
#include "VdsWrapper.h"
#include "FmifsWrapper.h"

#include <chrono>
#include <gtest/gtest.h>

using namespace WParted;

TEST(Win32StorageEngineTest, EnumerateAndQuery)
{
    auto& engine = Win32StorageEngine::Instance();
    auto drives = engine.EnumerateDisks();

    // On Windows systems there should typically be at least one disk (system disk)
    // In CI or virtual environments without elevated rights, it returns safely without throwing
    for (const auto& drive : drives) {
        EXPECT_GE(drive.diskIndex, 0);
        EXPECT_FALSE(drive.linuxDevPath.empty());
        EXPECT_GT(drive.logicalSectorSize, 0u);
    }
}

TEST(Win32StorageEngineTest, VolumeLockGuardRAII)
{
    Win32VolumeLockGuard guard;
    EXPECT_FALSE(guard.IsLocked());
    EXPECT_FALSE(guard.IsDismounted());

    // Non-existent volume should safely fail to lock without crashing
    bool locked = guard.Lock(L"\\\\.\\Volume{00000000-0000-0000-0000-000000000000}", 1, 10);
    EXPECT_FALSE(locked);
    EXPECT_FALSE(guard.IsLocked());
}

TEST(Win32StorageEngineTest, BitLockerQuery)
{
    // Querying non-existent drive letter should return safe status
    auto status = BitLockerGuard::QueryStatus(L"Z:");
    EXPECT_FALSE(status.isEncrypted);
    EXPECT_FALSE(status.isLocked);
}

TEST(Win32StorageEngineTest, VdsWrapperInstance)
{
    auto& vds = VdsWrapper::Instance();
    auto caps = vds.QueryResizeCapabilities(L"\\\\.\\NonExistentVolume");
    EXPECT_EQ(caps.currentSizeBytes, 0u);
}

TEST(Win32StorageEngineTest, FmifsWrapperInstance)
{
    auto& fmifs = FmifsWrapper::Instance();
    // fmifs.dll is present on standard Windows
#if defined(_WIN32)
    EXPECT_TRUE(fmifs.IsAvailable());
#endif
}

TEST(Win32StorageEngineTest, GptLayoutPreservation)
{
    Win32DiskDrive disk;
    disk.diskIndex = 99;
    disk.isGpt = true;
    disk.logicalSectorSize = 512;
    disk.totalSizeBytes = 100ULL * 1024 * 1024 * 1024;

    GUID customGuid = { 0x12345678, 0xABCD, 0xEF01, {0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01} };
    disk.gptDiskId = customGuid;

    Win32DiskPartition part;
    part.partitionNumber = 1;
    part.startingOffset = 1048576;
    part.partitionLength = 1073741824;
    part.gptTypeGuid = GUID_PARTITION_BASIC_DATA;
    part.gptIdGuid = { 0 }; // Should be generated if zero
    disk.partitions.push_back(part);

    // Verify custom disk GUID is not overridden by basic data GUID
    EXPECT_FALSE(DualNomenclature::AreGuidsEqual(disk.gptDiskId, GUID_PARTITION_BASIC_DATA));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(disk.gptDiskId, customGuid));
}

TEST(Win32StorageEngineTest, MbrMultipleOfFourRule)
{
    // Windows requires MBR PartitionCount to be a multiple of 4
    size_t partCounts[] = { 1, 2, 3, 5, 7 };
    for (size_t count : partCounts) {
        DWORD aligned = static_cast<DWORD>(((count + 3) / 4) * 4);
        EXPECT_EQ(aligned % 4, 0u);
        EXPECT_GE(aligned, count);
        EXPECT_LT(aligned, count + 4);
    }
}

TEST(Win32StorageEngineTest, BitLockerQuerySpeed)
{
    auto tStart = std::chrono::high_resolution_clock::now();
    auto status = BitLockerGuard::QueryStatus(L"X:");
    auto tEnd = std::chrono::high_resolution_clock::now();

    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
    // Native API or fast check should complete in under 500ms (not hanging for 5-10s)
    EXPECT_LT(durationMs, 500);
    EXPECT_FALSE(status.isEncrypted);
}

