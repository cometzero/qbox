/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <cstring>

#include "memory-bench.h"
#include <memory_services.h>
#include <cci/utils/broker.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
int current_pid()
{
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}
} // namespace

// Simple read into the memory
TEST_BENCH(MemoryTestBench, SimpleWriteRead)
{
    SCP_INFO(SCMOD) << "SimpleWriteRead";
    uint8_t data;
    ASSERT_EQ(m_initiator.do_write(0, 0x04), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.do_read(0, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0x04);
}

// Transaction outside of the target address space
TEST_BENCH(MemoryTestBench, SimpleOverlapWrite)
{
    ASSERT_EQ(m_initiator.do_write<uint8_t>(MEMORY_SIZE + 1, 0x04), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

// Transaction that crosses the boundary
TEST_BENCH(MemoryTestBench, SimpleCrossesBoundary)
{
    ASSERT_EQ(m_initiator.do_write<uint16_t>(MEMORY_SIZE - 1, 0xFFFF), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

// Simple write and read into the memory with the Debug Transport Interface
TEST_BENCH(MemoryTestBench, SimpleWriteReadDebug)
{
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    uint64_t data64;

    ASSERT_EQ(m_initiator.do_write<uint8_t>(0, 0x12, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(uint8_t));
    ASSERT_EQ(m_initiator.do_read(0, data8, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(data8));
    ASSERT_EQ(data8, 0x12);

    ASSERT_EQ(m_initiator.do_write<uint16_t>(0, 0x1234, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(uint16_t));
    ASSERT_EQ(m_initiator.do_read(0, data16, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(data16));
    ASSERT_EQ(data16, 0x1234);

    ASSERT_EQ(m_initiator.do_write<uint32_t>(0, 0x12345678, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(uint32_t));
    ASSERT_EQ(m_initiator.do_read(0, data32, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(data32));
    ASSERT_EQ(data32, 0x12345678);

    ASSERT_EQ(m_initiator.do_write<uint64_t>(0, 0x1122334455667788, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(uint64_t));
    ASSERT_EQ(m_initiator.do_read(0, data64, true), tlm ::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(data64));
    ASSERT_EQ(data64, 0x1122334455667788);
}

// Debug Transport Interface transaction outside of the target address space
TEST_BENCH(MemoryTestBench, SimpleOverlapWriteDebug)
{
    ASSERT_EQ(m_initiator.do_write<uint8_t>(MEMORY_SIZE + 1, 0x04, true), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), 0);
}

// Debug Transport Interface transaction that crosses the boundary
TEST_BENCH(MemoryTestBench, SimpleCrossesBoundaryDebug)
{
    ASSERT_EQ(m_initiator.do_write<uint16_t>(MEMORY_SIZE - 1, 0xFFFF, true), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), 0);
}

// Write into memory with Blocking Transport and read with Debug Transport Interface
TEST_BENCH(MemoryTestBench, WriteBlockingReadDebug)
{
    uint8_t data;
    ASSERT_EQ(m_initiator.do_write<uint8_t>(0, 0x04), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.do_read(0, data, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(data));
    ASSERT_EQ(data, 0x04);
}

// Write into memory with Debug Transport Interface and read with Blocking Transport
TEST_BENCH(MemoryTestBench, WriteDebugReadBlocking)
{
    uint8_t data;
    ASSERT_EQ(m_initiator.do_write<uint8_t>(0, 0x04, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), sizeof(uint8_t));
    ASSERT_EQ(m_initiator.do_read(0, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0x04);
}

// Request for DMI access to memory
TEST_BENCH(MemoryTestBench, SimpleDmi)
{
    /* Valid DMI request */
    do_good_dmi_request_and_check(0, 0, MEMORY_SIZE - 1);

    /* Out-of-bound DMI request */
    do_bad_dmi_request_and_check(MEMORY_SIZE);
}

// Write and Read into the mémory with the Direct Memory Interface
TEST_BENCH(MemoryTestBench, DmiWriteRead)
{
    uint8_t data = 0x04;
    uint8_t data_read;

    /* Valid DMI request */
    do_good_dmi_request_and_check(0, 0, MEMORY_SIZE - 1);

    /* Write with DMI */
    dmi_write_or_read(0, &data, sizeof(data), false);
    ASSERT_EQ(data, 0x04);

    /* Read with DMI */
    dmi_write_or_read(0, &data_read, sizeof(data), true);
    ASSERT_EQ(data, data_read);
}

TEST_BENCH(MemoryTestBench, RseRomLoadIsReadableAndRejectsWrites)
{
    uint8_t image[] = {0x10, 0x32, 0x54, 0x76};
    uint8_t data = 0;

    m_target.p_rom = true;
    m_target.load.ptr_load(image, 0x20, sizeof(image));

    ASSERT_EQ(m_initiator.do_read(0x22, data), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(data, 0x54);

    ASSERT_EQ(m_initiator.do_write<uint8_t>(0x22, 0xa5),
              tlm::TLM_COMMAND_ERROR_RESPONSE);
    ASSERT_EQ(m_initiator.do_read(0x22, data), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(data, 0x54);
}

TEST_BENCH(MemoryTestBench, RseProvisioningBundleLoadsAtConfiguredOffset)
{
    uint8_t bundle[] = {0xde, 0xad, 0xbe, 0xef};
    uint8_t data[sizeof(bundle)] = {};
    tlm::tlm_generic_payload trans;

    m_target.load.ptr_load(bundle, 0x80, sizeof(bundle));

    ASSERT_EQ(m_initiator.do_read_with_txn_and_ptr(
                  trans, 0x80, data, sizeof(data)),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(std::memcmp(data, bundle, sizeof(bundle)), 0);
}

TEST(MemoryServicesTest, SharedMemoryCreateReturnsFd)
{
#ifdef _WIN32
    const std::string memname = "qbox-memory-services-test-" + std::to_string(current_pid());
#else
    const std::string memname = "/qbox-memory-services-test-" + std::to_string(current_pid());
#endif
    int fd = -1;

    uint8_t* ptr = gs::MemoryServices::get().map_mem_create(memname, 4096, &fd);

    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(fd, gs::MemoryServices::get().get_shmem_fd(memname));
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
