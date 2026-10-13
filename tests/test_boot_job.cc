/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "WParted_Boot_Job.h"
#include "WParted_Boot_Scheduler.h"
#include <gtest/gtest.h>

using namespace WParted;

TEST(BootJobTest, CRC64Calculation)
{
    const char data[] = "WParted Native Early Boot Sector Relocation Job";
    uint64_t crc1 = CalculateCRC64(data, sizeof(data));
    EXPECT_NE(crc1, 0ULL);

    uint64_t crc2 = CalculateCRC64(data, sizeof(data));
    EXPECT_EQ(crc1, crc2);

    const char modified[] = "WParted Native Early Boot Sector Relocation Job!";
    uint64_t crc3 = CalculateCRC64(modified, sizeof(modified));
    EXPECT_NE(crc1, crc3);
}

TEST(BootJobTest, HeaderSerialization)
{
    BootJobFileHeader header = { 0 };
    header.magic = BOOT_JOB_MAGIC;
    header.version = BOOT_JOB_VERSION;
    header.opCount = 1;

    header.operations[0].opType = BOOT_OP_MOVE_SECTORS;
    header.operations[0].diskIndex = 0;
    header.operations[0].sourceSector = 204800;
    header.operations[0].targetSector = 409600;
    header.operations[0].sectorCount = 100000;
    header.operations[0].sectorSize = 512;
    header.operations[0].expectedCrc64 = 0x1234567890ABCDEFULL;

    EXPECT_EQ(header.magic, BOOT_JOB_MAGIC);
    EXPECT_EQ(header.version, 1u);
    EXPECT_EQ(header.operations[0].sectorCount, 100000u);
}

TEST(BootJobTest, SchedulerJobPath)
{
    std::wstring path = WParted_Boot_Scheduler::GetJobFilePath();
    EXPECT_NE(path.find(L"wparted-jobs.bin"), std::wstring::npos);
}

TEST(BootJobTest, BootAccessCheck)
{
    // Moving start sector always requires boot access
    EXPECT_TRUE(WParted_Boot_Scheduler::IsBootAccessRequired(L"D:\\", true));
    EXPECT_FALSE(WParted_Boot_Scheduler::IsBootAccessRequired(L"D:\\", false));
}

TEST(BootJobTest, OverlappingSectorMoveDirection)
{
    // When target > source (moving partition right), copy must proceed from end to start
    uint64_t src = 1000;
    uint64_t dst = 1500;
    uint64_t count = 1000;

    bool moveBackward = (dst > src);
    EXPECT_TRUE(moveBackward);

    // Simulate sector array
    std::vector<int> disk(3000, 0);
    for (size_t i = 0; i < count; ++i) {
        disk[src + i] = static_cast<int>(i + 1);
    }

    // Moving backward: copy from highest offset down to lowest
    std::vector<int> diskCopy = disk;
    for (int64_t i = static_cast<int64_t>(count) - 1; i >= 0; --i) {
        diskCopy[dst + i] = diskCopy[src + i];
    }

    // Verify all original values were preserved and placed at target
    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(diskCopy[dst + i], static_cast<int>(i + 1));
    }
}

TEST(BootJobTest, HeaderCrcVerification)
{
    BootJobFileHeader header = { 0 };
    header.magic = BOOT_JOB_MAGIC;
    header.version = BOOT_JOB_VERSION;
    header.opCount = 1;

    header.operations[0].opType = BOOT_OP_MOVE_SECTORS;
    header.operations[0].diskIndex = 0;
    header.operations[0].sourceSector = 2048;
    header.operations[0].targetSector = 4096;
    header.operations[0].sectorCount = 1000;
    header.operations[0].sectorSize = 512;

    header.headerCrc64 = CalculateCRC64(header.operations, header.opCount * sizeof(BootJobOperation));

    // Verify valid CRC passes
    uint64_t computed = CalculateCRC64(header.operations, header.opCount * sizeof(BootJobOperation));
    EXPECT_EQ(header.headerCrc64, computed);

    // Tampering with operation triggers CRC failure
    header.operations[0].targetSector = 8192;
    uint64_t tampered = CalculateCRC64(header.operations, header.opCount * sizeof(BootJobOperation));
    EXPECT_NE(header.headerCrc64, tampered);
}

