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
