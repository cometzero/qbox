/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include <stdexcept>

#include <gtest/gtest.h>

#include <cciutils.h>
#include <monitor.h>

TEST(MonitorRuntimeConfiguration, RejectsNonLoopbackMutationPreset)
{
    gs::ConfigurableBroker broker({
        { "monitor.runtime_mutation", cci::cci_value(true) },
        { "monitor.bind_address", cci::cci_value("0.0.0.0") },
    });
    EXPECT_THROW((gs::monitor<32>("monitor")), std::invalid_argument);
}

int sc_main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
