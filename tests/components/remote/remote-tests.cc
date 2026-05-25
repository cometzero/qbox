/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include "remote-bench.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <cci/utils/broker.h>
#include <scp/report.h>

namespace {
constexpr size_t REMOTE_DMI_MAP_FILE_SIZE = 0x1000;

std::string create_remote_dmi_map_file()
{
    const char* tmpdir = std::getenv("TMPDIR");
    std::string path = std::string(tmpdir ? tmpdir : "/tmp") + "/qbox-remote-dmi-" + std::to_string(getpid()) + ".bin";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to create remote DMI map file: " + path);
    }
    file.seekp(REMOTE_DMI_MAP_FILE_SIZE - 1);
    file.put('\0');
    if (!file) {
        throw std::runtime_error("failed to size remote DMI map file: " + path);
    }
    return path;
}
} // namespace

// Simple load and store into the Target 1 and 2
TEST_BENCH(RemotePassTest, test_bench)
{
    SCP_INFO(SCMOD) << "Test 1";
    for (int i = 0; i < 10; i++) {
        do_write_read_check(0x11000);
    }
    SCP_INFO(SCMOD) << "Test 2";
    for (int i = 0; i < 10; i++) {
        do_write_read_check(0x22000);
    }
    SCP_INFO(SCMOD) << "Test 2l";
    for (int i = 0; i < 10; i++) {
        do_write_read_check_larger(0x22000 + (i * 8));
    }
    SCP_INFO(SCMOD) << "Test 3";
    for (int i = 0; i < 10; i++) {
        // do_remote_write_read_check(0x11000);
    }
    SCP_INFO(SCMOD) << "Test 4";
    for (int i = 0; i < 10; i++) {
        do_dmi_write_read_check(0x23000);
    }
    SCP_INFO(SCMOD) << "Looks OK";
    sc_core::sc_stop();
}

int sc_main(int argc, char* argv[])
{
    std::string remote_dmi_map_file = create_remote_dmi_map_file();

    gs::ConfigurableBroker m_broker({
        { "test_bench.mem1.target_socket.address", cci::cci_value(0x11000) },
        { "test_bench.mem1.target_socket.size", cci::cci_value(0x1000) },
        { "test_bench.pass.mem2.target_socket.address", cci::cci_value(0x22000) },
        { "test_bench.pass.mem2.target_socket.size", cci::cci_value(0x1000) },
        { "test_bench.pass.mem3.target_socket.address", cci::cci_value(0x23000) },
        { "test_bench.pass.mem3.target_socket.size", cci::cci_value(0x1000) },
        { "test_bench.local.target_socket.address", cci::cci_value(0x11000) },
        { "test_bench.local.target_socket.size", cci::cci_value(0x1000) },

        { "test_bench.mem1.log_level", cci::cci_value(4) },
        { "test_bench.pass.mem2.log_level", cci::cci_value(4) },
        { "test_bench.pass.mem3.log_level", cci::cci_value(4) },

        { "test_bench.mem1.shared_memory", cci::cci_value(true) },
        { "test_bench.pass.mem2.shared_memory", cci::cci_value(true) },
        { "test_bench.pass.mem3.map_file", cci::cci_value(remote_dmi_map_file) },

        { "test_bench.pass.tlm_initiator_ports_num", cci::cci_value(1) },
        { "test_bench.pass.tlm_target_ports_num", cci::cci_value(2) },

        { "test_bench.pass.remote_pass.tlm_initiator_ports_num", cci::cci_value(2) },
        { "test_bench.pass.remote_pass.tlm_target_ports_num", cci::cci_value(1) },

        { "test_bench.pass.target_socket_0.address", cci::cci_value(0x20000) },
        { "test_bench.pass.target_socket_0.size", cci::cci_value(0x10000) },
        { "test_bench.pass.target_socket_0.relative_addresses", cci::cci_value(false) },
        { "test_bench.pass.exec_path", cci::cci_value(add_suffix_to_executable(getexepath(), "-remote")) },
    });

    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    std::remove(remote_dmi_map_file.c_str());
    return ret;
}
