/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cci/utils/broker.h>

#include <dma350.h>

namespace {

constexpr uint64_t DMASECINFO = 0xfb0;
constexpr uint64_t DMAINFO_IIDR = 0xfc8;
constexpr uint64_t DMAINFO_AIDR = 0xfcc;
constexpr uint64_t CH0_CMD = 0x1000;
constexpr uint64_t CH0_STATUS = 0x1004;
constexpr uint64_t CH0_CTRL = 0x100c;
constexpr uint64_t CH0_SRCADDR = 0x1010;
constexpr uint64_t CH0_DESADDR = 0x1018;
constexpr uint64_t CH0_XSIZE = 0x1020;
constexpr uint64_t CH0_XSIZEHI = 0x1024;
constexpr uint64_t CH0_DESTRANSCFG = 0x102c;
constexpr uint64_t CH0_XADDRINC = 0x1030;
constexpr uint64_t CH0_FILLVAL = 0x1038;
constexpr uint32_t CH_STATUS_STAT_DONE = 1u << 16;

class TestMemory : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<TestMemory, DEFAULT_TLM_BUSWIDTH> target_socket;
    std::vector<uint8_t> bytes;

    TestMemory(sc_core::sc_module_name name, size_t size)
        : sc_core::sc_module(name)
        , target_socket("target_socket")
        , bytes(size, 0)
    {
        target_socket.register_b_transport(this, &TestMemory::b_transport);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;

        const auto address = trans.get_address();
        const auto len = trans.get_data_length();
        auto* data = trans.get_data_ptr();

        if (data == nullptr || address + len > bytes.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, bytes.data() + address, len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(bytes.data() + address, data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

uint32_t access32(dma350& dut, uint64_t offset, tlm::tlm_command command, uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(dma350& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(dma350& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Dma350Test, ResetValuesCoverEarlyBl1Polling)
{
    dma350 dut("dma350");

    EXPECT_EQ(read32(dut, DMASECINFO), 0x30u);
    EXPECT_EQ(read32(dut, DMAINFO_IIDR), 0x3a00043bu);
    EXPECT_EQ(read32(dut, DMAINFO_AIDR), 0x0u);
    EXPECT_EQ(read32(dut, CH0_CTRL), 0x00200200u);
    EXPECT_EQ(read32(dut, CH0_DESTRANSCFG), 0x000f0400u);
}

TEST(Dma350Test, ChannelCommandCompletesImmediately)
{
    dma350 dut("dma350");

    write32(dut, CH0_CMD, 0xffffffffu);
    EXPECT_EQ(read32(dut, CH0_CMD), 0x0u);
}

TEST(Dma350Test, EnableCommandExecutesFillWrites)
{
    dma350 dut("dma350");
    TestMemory memory("memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    write32(dut, CH0_FILLVAL, 0xa5a55a5au);
    write32(dut, CH0_DESADDR, 0x20u);
    write32(dut, CH0_XADDRINC, 0x00010000u);
    write32(dut, CH0_XSIZE, 0x00020000u);
    write32(dut, CH0_CTRL, 0x01200603u);
    write32(dut, CH0_CMD, 0x1u);

    EXPECT_EQ(read32(dut, CH0_CMD), 0x0u);
    for (auto i = 0u; i < 16u; i += sizeof(uint32_t)) {
        uint32_t value = 0;
        std::memcpy(&value, memory.bytes.data() + 0x20 + i, sizeof(value));
        EXPECT_EQ(value, 0xa5a55a5au);
    }
}

TEST(Dma350Test, EnableCommandCopiesContiguousMemoryAndSetsDoneStatus)
{
    dma350 dut("dma350_copy");
    TestMemory memory("copy_memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t source[] = {
        0x10, 0x11, 0x12, 0x13,
        0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33,
        0x40, 0x41, 0x42, 0x43,
    };
    std::memcpy(memory.bytes.data() + 0x20, source, sizeof(source));

    write32(dut, CH0_SRCADDR, 0x20u);
    write32(dut, CH0_DESADDR, 0x80u);
    write32(dut, CH0_XADDRINC, 0x00010001u);
    write32(dut, CH0_XSIZE, sizeof(source) | (sizeof(source) << 16));
    write32(dut, CH0_CTRL, 0x00200200u);
    write32(dut, CH0_CMD, 0x1u);

    EXPECT_EQ(read32(dut, CH0_CMD), 0x0u);
    EXPECT_NE(read32(dut, CH0_STATUS) & CH_STATUS_STAT_DONE, 0u);
    EXPECT_EQ(std::memcmp(memory.bytes.data() + 0x80, source, sizeof(source)), 0);

    write32(dut, CH0_STATUS, CH_STATUS_STAT_DONE);
    EXPECT_EQ(read32(dut, CH0_STATUS) & CH_STATUS_STAT_DONE, 0u);
}

TEST(Dma350Test, EnableCommandCopiesXsizeHighBits)
{
    dma350 dut("dma350_copy_high");
    TestMemory memory("copy_high_memory", 0x24000);
    const uint32_t length = 0x10004;
    const uint32_t source = 0x20;
    const uint32_t dest = 0x12000;

    dut.initiator_socket.bind(memory.target_socket);

    for (uint32_t i = 0; i < length; ++i) {
        memory.bytes[source + i] = static_cast<uint8_t>(i ^ (i >> 8));
    }

    write32(dut, CH0_SRCADDR, source);
    write32(dut, CH0_DESADDR, dest);
    write32(dut, CH0_XADDRINC, 0x00010001u);
    write32(dut, CH0_XSIZE, (length & 0xffffu) | ((length & 0xffffu) << 16));
    write32(dut, CH0_XSIZEHI, ((length >> 16) & 0xffffu) | (((length >> 16) & 0xffffu) << 16));
    write32(dut, CH0_CTRL, 0x00200200u);
    write32(dut, CH0_CMD, 0x1u);

    EXPECT_NE(read32(dut, CH0_STATUS) & CH_STATUS_STAT_DONE, 0u);
    EXPECT_EQ(memory.bytes[dest], memory.bytes[source]);
    EXPECT_EQ(memory.bytes[dest + 0x8000], memory.bytes[source + 0x8000]);
    EXPECT_EQ(memory.bytes[dest + length - 1], memory.bytes[source + length - 1]);
}

TEST(Dma350Test, TraceCopyFilterSkipsFillOperations)
{
    dma350 dut("dma350_trace_copy");

    dut.p_trace = true;
    dut.p_trace_filter = std::string("copy");
    dut.p_trace_limit = 32;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    write32(dut, CH0_FILLVAL, 0xa5a55a5au);
    write32(dut, CH0_DESADDR, 0x20u);
    write32(dut, CH0_XADDRINC, 0x00010000u);
    write32(dut, CH0_XSIZE, 0x00020000u);
    write32(dut, CH0_CTRL, 0x01200603u);
    write32(dut, CH0_CMD, 0x1u);

    write32(dut, CH0_SRCADDR, 0x20u);
    write32(dut, CH0_DESADDR, 0x80u);
    write32(dut, CH0_XADDRINC, 0x00010001u);
    write32(dut, CH0_XSIZE, 0x10u | (0x10u << 16));
    write32(dut, CH0_CTRL, 0x00200200u);
    write32(dut, CH0_CMD, 0x1u);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_EQ(log.find(" fill"), std::string::npos);
    EXPECT_NE(log.find(" copy"), std::string::npos);
    EXPECT_NE(log.find("source=0x20"), std::string::npos);
    EXPECT_NE(log.find("dest=0x80"), std::string::npos);
}

TEST(Dma350Test, TraceAddressThresholdFiltersLowCopies)
{
    dma350 dut("dma350_trace_threshold");

    dut.p_trace = true;
    dut.p_trace_filter = std::string("copy");
    dut.p_trace_address_min = 0x70000000u;
    dut.p_trace_limit = 32;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    write32(dut, CH0_SRCADDR, 0x20u);
    write32(dut, CH0_DESADDR, 0x80u);
    write32(dut, CH0_XADDRINC, 0x00010001u);
    write32(dut, CH0_XSIZE, 0x10u | (0x10u << 16));
    write32(dut, CH0_CTRL, 0x00200200u);
    write32(dut, CH0_CMD, 0x1u);

    write32(dut, CH0_SRCADDR, 0xb0067000u);
    write32(dut, CH0_DESADDR, 0x70083c00u);
    write32(dut, CH0_XADDRINC, 0x00010001u);
    write32(dut, CH0_XSIZE, 0x10u | (0x10u << 16));
    write32(dut, CH0_CTRL, 0x00200200u);
    write32(dut, CH0_CMD, 0x1u);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_EQ(log.find("source=0x20"), std::string::npos);
    EXPECT_EQ(log.find("dest=0x80"), std::string::npos);
    EXPECT_NE(log.find("source=0xb0067000"), std::string::npos);
    EXPECT_NE(log.find("dest=0x70083c00"), std::string::npos);
}

TEST(Dma350Test, RejectsUnsupportedAndOutOfRangeTransactions)
{
    dma350 dut("dma350");
    tlm::tlm_generic_payload trans;
    uint8_t data[3] = {};
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_ptr(data);
    trans.set_data_length(sizeof(data));
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);

    trans.set_data_length(sizeof(uint32_t));
    trans.set_address(0x2000 - 1);
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
