/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cpu_arm/cpu_arm_cortex_m55/include/cortex-m55.h"
#include "gs_memory.h"
#include "qemu-instance.h"

class TestMemory : public gs::gs_memory<>
{
public:
    using gs::gs_memory<>::gs_memory;

    bool read_bytes(uint8_t* data, uint64_t offset, uint64_t len) { return read(data, offset, len); }
};

class CpuArmCortexM55ShmemDmiByteStoreTest : public CpuTestBench<cpu_arm_cortexM55, CpuTesterMmio>
{
    static constexpr uint64_t DMI_ADDR = 0x90000000;
    static constexpr size_t DMI_SIZE = 4096;

    cci::cci_param<bool> p_enable_dmi;
    TestMemory m_dmi_mem;
    bool m_done = false;
    uint64_t m_error = 0;

public:
    static constexpr std::array<uint8_t, 24> PATTERN = {
        0x67, 0xa4, 0x79, 0x10, 0x4f, 0x5d, 0xf3, 0x0a, 0xc8, 0x9e, 0x37, 0x42,
        0x19, 0x80, 0xae, 0x52, 0x01, 0x23, 0x45, 0x89, 0xab, 0xcd, 0xef, 0x5a,
    };

    CpuArmCortexM55ShmemDmiByteStoreTest(const sc_core::sc_module_name& n)
        : CpuTestBench<cpu_arm_cortexM55, CpuTesterMmio>(n)
        , p_enable_dmi("enable_dmi", true, "Enable DMI for the shared-memory target")
        , m_dmi_mem("dmi_mem", DMI_SIZE)
    {
        for (auto& cpu : m_cpus) {
            cpu.p_start_powered_off = false;
            cpu.p_init_nsvtor = MEM_ADDR;
            cpu.p_init_svtor = MEM_ADDR;
        }

        m_dmi_mem.p_shmem = true;
        m_dmi_mem.p_shmem_prefix = "qbox_m55_dmi_byte_store_";
        m_dmi_mem.p_init_mem = true;
        m_dmi_mem.p_init_mem_val = 0;
        m_dmi_mem.p_dmi = p_enable_dmi.get_value();
        m_router.add_target(m_dmi_mem.socket, DMI_ADDR, DMI_SIZE);

        load_firmware_binary();
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        TEST_ASSERT(id == CpuTesterMmio::SOCKET_MMIO);
        TEST_ASSERT(addr == 0);

        m_error = data;
        m_done = true;
        sc_core::sc_stop();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        TEST_FAIL("Unexpected MMIO read");
        return 0;
    }

    virtual void end_of_simulation() override
    {
        CpuTestBench<cpu_arm_cortexM55, CpuTesterMmio>::end_of_simulation();

        std::array<uint8_t, PATTERN.size()> observed = {};
        TEST_ASSERT(m_dmi_mem.read_bytes(observed.data(), 0, observed.size()));

        TEST_ASSERT(m_done);
        if (m_error != 0) {
            std::cerr << "Cortex-M55 shared-memory DMI byte-store firmware error: check_index=" << std::dec
                      << (m_error >> 8) << " actual=0x" << std::hex << (m_error & 0xff) << std::dec << std::endl;
            std::cerr << "Cortex-M55 shared-memory DMI buffer first bytes:";
            for (uint8_t byte : observed) {
                std::cerr << " 0x" << std::hex << static_cast<unsigned>(byte);
            }
            std::cerr << std::dec << std::endl;
        }
        TEST_ASSERT(m_error == 0);
        for (size_t i = 0; i < PATTERN.size(); ++i) {
            TEST_ASSERT(observed[i] == PATTERN[i]);
        }
    }

private:
    void load_firmware_binary()
    {
#ifdef FIRMWARE_BIN_PATH
        const char* firmware_path = FIRMWARE_BIN_PATH;
        std::ifstream file(firmware_path, std::ios::binary | std::ios::ate);

        if (!file.is_open()) {
            SCP_FATAL(SCMOD) << "Failed to open Cortex-M55 firmware file: " << firmware_path;
            TEST_ASSERT(false);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> firmware_data(size);
        if (!file.read(reinterpret_cast<char*>(firmware_data.data()), size)) {
            SCP_FATAL(SCMOD) << "Failed to read Cortex-M55 firmware file: " << firmware_path;
            TEST_ASSERT(false);
        }

        m_mem.load.ptr_load(firmware_data.data(), MEM_ADDR, firmware_data.size());
#else
        SCP_FATAL(SCMOD) << "FIRMWARE_BIN_PATH is not defined";
        TEST_ASSERT(false);
#endif
    }
};

constexpr std::array<uint8_t, 24> CpuArmCortexM55ShmemDmiByteStoreTest::PATTERN;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexM55ShmemDmiByteStoreTest>(argc, argv); }
