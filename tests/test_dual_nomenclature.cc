/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "DualNomenclature.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace WParted;

TEST(DualNomenclatureTest, DiskIndexToLinuxDev)
{
    EXPECT_EQ(DualNomenclature::DiskIndexToLinuxDev(0), "/dev/sda");
    EXPECT_EQ(DualNomenclature::DiskIndexToLinuxDev(1), "/dev/sdb");
    EXPECT_EQ(DualNomenclature::DiskIndexToLinuxDev(25), "/dev/sdz");
    EXPECT_EQ(DualNomenclature::DiskIndexToLinuxDev(26), "/dev/sdaa");
    EXPECT_EQ(DualNomenclature::DiskIndexToLinuxDev(-1), "/dev/sd?");
}

TEST(DualNomenclatureTest, PartitionToLinuxDev)
{
    EXPECT_EQ(DualNomenclature::PartitionToLinuxDev(0, 1), "/dev/sda1");
    EXPECT_EQ(DualNomenclature::PartitionToLinuxDev(0, 2), "/dev/sda2");
    EXPECT_EQ(DualNomenclature::PartitionToLinuxDev(1, 3), "/dev/sdb3");
}

TEST(DualNomenclatureTest, FormatByteSize)
{
    EXPECT_EQ(DualNomenclature::FormatByteSize(512), "512.0 B");
    EXPECT_EQ(DualNomenclature::FormatByteSize(1024 * 1024), "1.00 MiB");
    EXPECT_EQ(DualNomenclature::FormatByteSize(1024ULL * 1024 * 1024), "1.00 GiB");
    EXPECT_EQ(DualNomenclature::FormatByteSize(2000ULL * 1024 * 1024 * 1024), "1.95 TiB");
}

TEST(DualNomenclatureTest, FormatDualNames)
{
    std::string diskName = DualNomenclature::FormatDualDiskName(0, "NVMe", "Samsung 990 PRO", 2000ULL * 1024 * 1024 * 1024);
    EXPECT_NE(diskName.find("/dev/sda"), std::string::npos);
    EXPECT_NE(diskName.find("Disk 0"), std::string::npos);
    EXPECT_NE(diskName.find("Samsung 990 PRO"), std::string::npos);

    std::string partName = DualNomenclature::FormatDualPartitionName(0, 1, "C:\\", "NTFS", 500ULL * 1024 * 1024 * 1024);
    EXPECT_NE(partName.find("/dev/sda1"), std::string::npos);
    EXPECT_NE(partName.find("C:\\"), std::string::npos);
    EXPECT_NE(partName.find("NTFS"), std::string::npos);
}

TEST(DualNomenclatureTest, LinuxFlagToGptAndBack)
{
    GUID espGuid;
    uint64_t attrs = 0;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("esp", espGuid, attrs));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(espGuid, GUID_PARTITION_ESP));

    auto flags = DualNomenclature::GptToLinuxFlags(espGuid, 0);
    EXPECT_TRUE(std::find(flags.begin(), flags.end(), "esp") != flags.end());
    EXPECT_TRUE(std::find(flags.begin(), flags.end(), "boot") != flags.end());

    GUID msrGuid;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("msftres", msrGuid, attrs));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(msrGuid, GUID_PARTITION_MSFT_RESERVED));

    GUID dataGuid;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("msftdata", dataGuid, attrs));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(dataGuid, GUID_PARTITION_BASIC_DATA));

    GUID diagGuid;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("diag", diagGuid, attrs));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(diagGuid, GUID_PARTITION_WINRE));

    GUID apfsGuid;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("apfs", apfsGuid, attrs));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(apfsGuid, GUID_PARTITION_APFS));

    GUID hiddenGuid;
    EXPECT_TRUE(DualNomenclature::LinuxFlagToGpt("hidden", hiddenGuid, attrs));
    EXPECT_EQ(attrs, GPT_ATTR_HIDDEN);
}

TEST(DualNomenclatureTest, GuidStringConversions)
{
    std::string guidStr = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
    GUID parsed;
    EXPECT_TRUE(DualNomenclature::StringToGuid(guidStr, parsed));
    EXPECT_TRUE(DualNomenclature::AreGuidsEqual(parsed, GUID_PARTITION_ESP));

    std::string formatted = DualNomenclature::GuidToString(parsed);
    EXPECT_EQ(formatted, guidStr);
}
