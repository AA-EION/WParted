/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "WParted_WSL_Bridge.h"
#include <gtest/gtest.h>

using namespace WParted;

TEST(WslBridgeTest, ParseProgressPercentage)
{
    EXPECT_EQ(WParted_WSL_Bridge::ParseProgressPercentage("Pass 1: Checking inodes and blocks (50.0%)"), 50);
    EXPECT_EQ(WParted_WSL_Bridge::ParseProgressPercentage("resize2fs: [=======================>] (95.5%)"), 95);
    EXPECT_EQ(WParted_WSL_Bridge::ParseProgressPercentage("Checking filesystem: 100% complete"), 100);
    EXPECT_EQ(WParted_WSL_Bridge::ParseProgressPercentage("0%"), 0);
    EXPECT_EQ(WParted_WSL_Bridge::ParseProgressPercentage("Regular log line with no progress"), -1);
}

TEST(WslBridgeTest, QueryStatusSafe)
{
    auto& bridge = WParted_WSL_Bridge::Instance();
    auto status = bridge.QueryStatus();
    // QueryStatus executes without throw/crash regardless of WSL2 state
    SUCCEED();
}
