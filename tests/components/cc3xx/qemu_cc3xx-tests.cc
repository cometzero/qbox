/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <gtest/gtest.h>

#include <qemu_cc3xx.h>

TEST(QemuCc3xxTest, HeaderExportsQemuCc3xxType)
{
    EXPECT_GT(sizeof(qemu_cc3xx*), 0u);
}

extern "C" int sc_main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
