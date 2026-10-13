/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "ApfsDriver.h"
#include <gtest/gtest.h>

using namespace WParted;

TEST(ApfsParserTest, GuidIdentification)
{
    EXPECT_TRUE(ApfsDriver::IsApfsPartition(GUID_PARTITION_APFS));
    EXPECT_FALSE(ApfsDriver::IsApfsPartition(GUID_PARTITION_BASIC_DATA));
}

TEST(ApfsParserTest, Fletcher64Checksum)
{
    // Test known checksum vectors
    uint32_t data[4] = { 0x12345678, 0x9ABCDEF0, 0x0FEDCBA9, 0x87654321 };
    uint64_t c1 = Fletcher64(data, 4);
    EXPECT_NE(c1, 0ULL);

    // Identical data produces identical checksum
    uint64_t c2 = Fletcher64(data, 4);
    EXPECT_EQ(c1, c2);

    // Altered data produces different checksum
    data[0] ^= 0x1;
    uint64_t c3 = Fletcher64(data, 4);
    EXPECT_NE(c1, c3);
}

TEST(ApfsParserTest, FormatUuid)
{
    uint8_t rawUuid[16] = {
        0x7C, 0x34, 0x57, 0xEF,
        0x00, 0x00,
        0x11, 0xAA,
        0xAA, 0x11,
        0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
    };
    std::string str = ApfsDriver::FormatUuid(rawUuid);
    EXPECT_EQ(str, "7C3457EF-0000-11AA-AA11-00306543ECAC");
}

TEST(ApfsParserTest, SuperblockStructures)
{
    nx_superblock_t nx = { 0 };
    nx.nx_magic = APFS_NX_MAGIC;
    nx.nx_block_size = 4096;
    nx.nx_block_count = 250000000;

    EXPECT_EQ(nx.nx_magic, APFS_NX_MAGIC);
    EXPECT_EQ(nx.nx_block_size, 4096u);

    apfs_superblock_t vol = { 0 };
    vol.apfs_magic = APFS_VOL_MAGIC;
    vol.apfs_features = APFS_FEATURE_ENCRYPTED;

    EXPECT_EQ(vol.apfs_magic, APFS_VOL_MAGIC);
    EXPECT_TRUE((vol.apfs_features & APFS_FEATURE_ENCRYPTED) != 0);
}
