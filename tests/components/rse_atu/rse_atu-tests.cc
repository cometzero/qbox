/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <array>
#include <limits>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>
#include <cci/utils/broker.h>

#include <rse_atu.h>

namespace {

constexpr uint64_t ATUBC = 0x000;
constexpr uint64_t ATUC = 0x004;
constexpr uint64_t ATUIS = 0x008;
constexpr uint64_t ATUIC = 0x010;
constexpr uint64_t ATUMA = 0x014;
constexpr uint64_t ATURSLA0 = 0x020;
constexpr uint64_t ATURELA0 = 0x0a0;
constexpr uint64_t ATURAV_L0 = 0x120;
constexpr uint64_t ATURAV_M0 = 0x1a0;
constexpr uint64_t ATUROBA0 = 0x220;
constexpr uint64_t REGION_STRIDE = 0x4;
constexpr uint64_t HOST_LOGICAL_BASE = 0x70000000;
constexpr uint64_t HOST_PHYSICAL_BASE = 0x38000000;
constexpr uint64_t LOW_REGION_LOGICAL_BASE = 0x6ff00000;
constexpr uint64_t LOW_REGION_PHYSICAL_BASE = 0x20000d5800000;
constexpr uint64_t HOST_SI_PIK_LOGICAL_BASE = 0x7540a000;
constexpr uint64_t HOST_SI_PIK_PHYSICAL_BASE = 0x400002a600000;
constexpr uint64_t TEST_REGION_SIZE = 0x2000;
constexpr uint32_t ATUROBA_AXPROT1_OFF = 2;
constexpr uint32_t ATU_ROBA_SET_1 = 3;
constexpr uint32_t ATU_DOMAIN_SECURE = 1u << 0;

class TestMemory : public sc_core::sc_module
{
    std::vector<uint8_t> m_mem;
    uint64_t m_base;
    uint64_t m_dmi_start;
    uint64_t m_dmi_end;

public:
    tlm_utils::simple_target_socket<TestMemory, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit TestMemory(sc_core::sc_module_name name,
                        uint64_t base = HOST_PHYSICAL_BASE,
                        size_t size = TEST_REGION_SIZE,
                        uint64_t dmi_start =
                            std::numeric_limits<uint64_t>::max(),
                        uint64_t dmi_end =
                            std::numeric_limits<uint64_t>::max())
        : sc_core::sc_module(name)
        , m_mem(size)
        , m_base(base)
        , m_dmi_start(dmi_start == std::numeric_limits<uint64_t>::max()
                          ? base
                          : dmi_start)
        , m_dmi_end(dmi_end == std::numeric_limits<uint64_t>::max()
                        ? base + size - 1
                        : dmi_end)
        , target_socket("target_socket")
    {
        target_socket.register_b_transport(this, &TestMemory::b_transport);
        target_socket.register_transport_dbg(this, &TestMemory::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &TestMemory::get_direct_mem_ptr);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        access(trans);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans) ? trans.get_data_length() : 0;
    }

    bool access(tlm::tlm_generic_payload& trans)
    {
        const uint64_t address = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || len > m_mem.size() || address < m_base ||
            address - m_base > m_mem.size() - len) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        const auto offset = static_cast<size_t>(address - m_base);
        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_mem[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&m_mem[offset], data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    bool get_direct_mem_ptr(tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        const uint64_t address = trans.get_address();
        if (address < m_base || address - m_base >= m_mem.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        dmi_data.allow_read_write();
        dmi_data.set_dmi_ptr(m_mem.data());
        dmi_data.set_start_address(m_dmi_start);
        dmi_data.set_end_address(m_dmi_end);
        dmi_data.set_read_latency(sc_core::SC_ZERO_TIME);
        dmi_data.set_write_latency(sc_core::SC_ZERO_TIME);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }
};

uint32_t access32(rse_atu& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
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

uint32_t read32(rse_atu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_atu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

tlm::tlm_response_status translate32(rse_atu& dut, uint64_t address,
                                     tlm::tlm_command command, uint32_t& value)
{
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(address);
    trans.set_command(command);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    dut.translation_b_transport(trans, delay);
    EXPECT_EQ(trans.get_address(), address);
    return trans.get_response_status();
}

void program_region_offset_pages(rse_atu& dut, uint32_t region,
                                 uint64_t logical, uint64_t offset_pages,
                                 uint64_t size, bool enable = true);

void program_region(rse_atu& dut, uint32_t region, uint64_t logical,
                    uint64_t physical, uint64_t size, bool enable = true)
{
    const uint8_t page_shift =
        static_cast<uint8_t>((read32(dut, ATUBC) >> 4) & 0xfu);
    const uint64_t add_value = physical - logical;
    const uint64_t shifted_add_value = add_value >> page_shift;
    program_region_offset_pages(dut, region, logical, shifted_add_value, size,
                                enable);
}

void program_region_offset_pages(rse_atu& dut, uint32_t region,
                                 uint64_t logical, uint64_t offset_pages,
                                 uint64_t size, bool enable)
{
    const uint8_t page_shift =
        static_cast<uint8_t>((read32(dut, ATUBC) >> 4) & 0xfu);
    const uint64_t register_offset = region * REGION_STRIDE;

    write32(dut, ATURSLA0 + register_offset,
            static_cast<uint32_t>(logical >> page_shift));
    write32(dut, ATURELA0 + register_offset,
            static_cast<uint32_t>(((logical + size) - 1) >> page_shift));
    write32(dut, ATURAV_L0 + register_offset,
            static_cast<uint32_t>(offset_pages));
    write32(dut, ATURAV_M0 + register_offset,
            static_cast<uint32_t>(offset_pages >> 32));
    if (enable) {
        write32(dut, ATUC, read32(dut, ATUC) | (1u << region));
    }
}

void program_region0(rse_atu& dut, uint64_t logical, uint64_t physical,
                     uint64_t size)
{
    program_region(dut, 0, logical, physical, size);
}

} // namespace

TEST(RseAtuTest, ResetBuildConfigurationMatchesTfMExpectations)
{
    rse_atu dut("rse_atu");

    EXPECT_EQ(read32(dut, ATUBC), 0x000000c5u);
}

TEST(RseAtuTest, BuildConfigPresetControlsPageSize)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_atu_test"));
    broker.set_preset_cci_value("rse_atu_8kb_pages.build_config",
                                cci::cci_value(0x000000d5u));

    rse_atu dut("rse_atu_8kb_pages");
    TestMemory memory("rse_atu_8kb_pages_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE,
                    TEST_REGION_SIZE);

    uint32_t write_value = 0x13572468;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x1000,
                          tlm::TLM_WRITE_COMMAND, write_value),
              tlm::TLM_OK_RESPONSE);

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x1000,
                          tlm::TLM_READ_COMMAND, read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, write_value);
}

TEST(RseAtuTest, BuildConfigPresetLimitsSupportedRegionCount)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_atu_test"));
    broker.set_preset_cci_value("rse_atu_8_regions.build_config",
                                cci::cci_value(0x000000c3u));

    rse_atu dut("rse_atu_8_regions");
    TestMemory memory("rse_atu_8_regions_memory", HOST_SI_PIK_PHYSICAL_BASE);

    dut.initiator_socket.bind(memory.target_socket);
    program_region(dut, 12, HOST_SI_PIK_LOGICAL_BASE,
                   HOST_SI_PIK_PHYSICAL_BASE, TEST_REGION_SIZE);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_SI_PIK_LOGICAL_BASE,
                          tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA),
              static_cast<uint32_t>(HOST_SI_PIK_LOGICAL_BASE));
}

TEST(RseAtuTest, RegionProgrammingRegistersAreWritable)
{
    rse_atu dut("rse_atu_region");

    write32(dut, ATURSLA0, 0x0006ff00u);
    write32(dut, ATURELA0, 0x0006ffffu);
    write32(dut, ATURAV_L0, 0x00001000u);
    write32(dut, ATUROBA0, 0x00000008u);
    write32(dut, ATUC, 0x00000001u);

    EXPECT_EQ(read32(dut, ATURSLA0), 0x0006ff00u);
    EXPECT_EQ(read32(dut, ATURELA0), 0x0006ffffu);
    EXPECT_EQ(read32(dut, ATURAV_L0), 0x00001000u);
    EXPECT_EQ(read32(dut, ATUROBA0), 0x00000008u);
    EXPECT_EQ(read32(dut, ATUC), 0x00000001u);
}

TEST(RseAtuTest, InterruptClearClearsStatusBits)
{
    rse_atu dut("rse_atu_irq");

    write32(dut, ATUIS, 0x00000001u);
    EXPECT_EQ(read32(dut, ATUIS), 0x00000000u);

    write32(dut, ATUIC, 0x00000001u);
    EXPECT_EQ(read32(dut, ATUIC), 0x00000001u);
    EXPECT_EQ(read32(dut, ATUIS), 0x00000000u);
}

TEST(RseAtuTest, RejectsOutOfRangeTransactions)
{
    rse_atu dut("rse_atu_bounds");
    tlm::tlm_generic_payload trans;
    uint32_t data = 0;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x1000 - 1);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    trans.set_data_length(sizeof(data));
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST(RseAtuTest, TranslatesEnabledRegionThroughInitiatorSocket)
{
    rse_atu dut("rse_atu_translate");
    TestMemory memory("rse_atu_translate_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE, TEST_REGION_SIZE);

    uint32_t write_value = 0xa5a55a5a;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x100, tlm::TLM_WRITE_COMMAND,
                          write_value),
              tlm::TLM_OK_RESPONSE);

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x100, tlm::TLM_READ_COMMAND,
                          read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, write_value);
}

TEST(RseAtuTest, TranslatesHighSiPikRegionOffset)
{
    rse_atu dut("rse_atu_translate_si_pik");
    TestMemory memory("rse_atu_translate_si_pik_memory",
                      HOST_SI_PIK_PHYSICAL_BASE);

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_SI_PIK_LOGICAL_BASE, HOST_SI_PIK_PHYSICAL_BASE,
                    TEST_REGION_SIZE);

    uint32_t write_value = 0x5a5ac3c3;
    EXPECT_EQ(translate32(dut, HOST_SI_PIK_LOGICAL_BASE, tlm::TLM_WRITE_COMMAND,
                          write_value),
              tlm::TLM_OK_RESPONSE);

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, HOST_SI_PIK_LOGICAL_BASE, tlm::TLM_READ_COMMAND,
                          read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, write_value);
}

TEST(RseAtuTest, SkipsEnabledRegionWhenLogicalAddressIsAboveEnd)
{
    rse_atu dut("rse_atu_translate_skip_low_region");
    TestMemory memory("rse_atu_translate_skip_low_region_memory",
                      HOST_SI_PIK_PHYSICAL_BASE);

    dut.initiator_socket.bind(memory.target_socket);
    program_region(dut, 0, LOW_REGION_LOGICAL_BASE, LOW_REGION_PHYSICAL_BASE,
                   0x10000);
    program_region(dut, 12, HOST_SI_PIK_LOGICAL_BASE,
                   HOST_SI_PIK_PHYSICAL_BASE, TEST_REGION_SIZE);

    uint32_t write_value = 0xc001cafe;
    EXPECT_EQ(translate32(dut, HOST_SI_PIK_LOGICAL_BASE, tlm::TLM_WRITE_COMMAND,
                          write_value),
              tlm::TLM_OK_RESPONSE);

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, HOST_SI_PIK_LOGICAL_BASE, tlm::TLM_READ_COMMAND,
                          read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, write_value);
}

TEST(RseAtuTest, RejectsUnmappedTranslationAndLatchesMismatchStatus)
{
    rse_atu dut("rse_atu_unmapped");
    TestMemory memory("rse_atu_unmapped_memory");

    dut.initiator_socket.bind(memory.target_socket);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE, tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
}

TEST(RseAtuTest, RejectsDisabledConfiguredRegionAndLatchesMismatchStatus)
{
    rse_atu dut("rse_atu_disabled_region");
    TestMemory memory("rse_atu_disabled_region_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region(dut, 0, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE,
                   TEST_REGION_SIZE, false);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE, tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA), static_cast<uint32_t>(HOST_LOGICAL_BASE));
}

TEST(RseAtuTest, RejectsAccessSpanningPastRegionEndAndLatchesMismatchStatus)
{
    rse_atu dut("rse_atu_region_span_fault");
    TestMemory memory("rse_atu_region_span_fault_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE, 0x1000);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0xffe,
                          tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA),
              static_cast<uint32_t>(HOST_LOGICAL_BASE + 0xffe));
}

TEST(RseAtuTest, RejectsOffsetPageShiftOverflowAndLatchesMismatchStatus)
{
    rse_atu dut("rse_atu_offset_shift_overflow");
    TestMemory memory("rse_atu_offset_shift_overflow_memory", 0);

    dut.initiator_socket.bind(memory.target_socket);
    const uint8_t page_shift =
        static_cast<uint8_t>((read32(dut, ATUBC) >> 4) & 0xfu);
    const uint64_t overflowing_offset_pages =
        (std::numeric_limits<uint64_t>::max() >> page_shift) + 1;
    program_region_offset_pages(dut, 0, HOST_LOGICAL_BASE,
                                overflowing_offset_pages,
                                TEST_REGION_SIZE);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE, tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA), static_cast<uint32_t>(HOST_LOGICAL_BASE));
}

TEST(RseAtuTest, RejectsNegativeOffsetUnderflowAndLatchesMismatchStatus)
{
    constexpr uint64_t LOW_LOGICAL_BASE = 0x1000;
    constexpr uint64_t NEGATIVE_OFFSET_BYTES = uint64_t{0} - 0x2000;
    rse_atu dut("rse_atu_negative_offset_underflow");
    TestMemory memory("rse_atu_negative_offset_underflow_memory", 0);

    dut.initiator_socket.bind(memory.target_socket);
    const uint8_t page_shift =
        static_cast<uint8_t>((read32(dut, ATUBC) >> 4) & 0xfu);
    program_region_offset_pages(dut, 0, LOW_LOGICAL_BASE,
                                NEGATIVE_OFFSET_BYTES >> page_shift,
                                0x1000);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, LOW_LOGICAL_BASE, tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA), static_cast<uint32_t>(LOW_LOGICAL_BASE));
}

TEST(RseAtuTest, RejectsDisallowedOutputSecurityDomain)
{
    rse_atu dut("rse_atu_permission_fault");
    TestMemory memory("rse_atu_permission_fault_memory");

    dut.initiator_socket.bind(memory.target_socket);
    dut.p_permitted_security_domains = ATU_DOMAIN_SECURE;
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE,
                    TEST_REGION_SIZE);
    write32(dut, ATUROBA0, ATU_ROBA_SET_1 << ATUROBA_AXPROT1_OFF);

    uint32_t data = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE, tlm::TLM_READ_COMMAND, data),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x1u);
    EXPECT_EQ(read32(dut, ATUMA), static_cast<uint32_t>(HOST_LOGICAL_BASE));
}

TEST(RseAtuTest, GrantsTranslatedDmiForEnabledRegion)
{
    rse_atu dut("rse_atu_dmi");
    TestMemory memory("rse_atu_dmi_memory");

    dut.initiator_socket.bind(memory.target_socket);
    dut.p_enable_dmi = true;
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE, TEST_REGION_SIZE);

    uint32_t value = 0xaabbccdd;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(HOST_LOGICAL_BASE + 0x100);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(dut.translation_get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(trans.get_address(), HOST_LOGICAL_BASE + 0x100);
    EXPECT_EQ(dmi.get_start_address(), HOST_LOGICAL_BASE);
    EXPECT_EQ(dmi.get_end_address(), HOST_LOGICAL_BASE + TEST_REGION_SIZE - 1);
    EXPECT_TRUE(dmi.is_read_allowed());
    EXPECT_TRUE(dmi.is_write_allowed());

    std::memcpy(dmi.get_dmi_ptr() + 0x100, &value, sizeof(value));

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x100, tlm::TLM_READ_COMMAND,
                          read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, value);
}

TEST(RseAtuTest, ClampsTranslatedDmiWhenDownstreamStartIsBelowOffset)
{
    constexpr uint64_t LOW_LOGICAL_BASE = 0x0;
    constexpr uint64_t LOW_PHYSICAL_BASE = 0x2000;
    rse_atu dut("rse_atu_dmi_downstream_underflow");
    TestMemory memory("rse_atu_dmi_downstream_underflow_memory", 0,
                      TEST_REGION_SIZE * 2, 0,
                      (TEST_REGION_SIZE * 2) - 1);

    dut.initiator_socket.bind(memory.target_socket);
    dut.p_enable_dmi = true;
    program_region0(dut, LOW_LOGICAL_BASE, LOW_PHYSICAL_BASE,
                    TEST_REGION_SIZE);

    uint32_t value = 0x11223344;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(LOW_LOGICAL_BASE + 0x100);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(dut.translation_get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(trans.get_address(), LOW_LOGICAL_BASE + 0x100);
    EXPECT_EQ(dmi.get_start_address(), LOW_LOGICAL_BASE);
    EXPECT_EQ(dmi.get_end_address(), LOW_LOGICAL_BASE + TEST_REGION_SIZE - 1);

    std::memcpy(dmi.get_dmi_ptr() + 0x100, &value, sizeof(value));

    uint32_t read_value = 0;
    EXPECT_EQ(translate32(dut, LOW_LOGICAL_BASE + 0x100,
                          tlm::TLM_READ_COMMAND, read_value),
              tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(read_value, value);
}

TEST(RseAtuTest, RejectsTranslatedDmiWhenRequestSpansDownstreamGrant)
{
    rse_atu dut("rse_atu_dmi_partial_grant");
    TestMemory memory("rse_atu_dmi_partial_grant_memory",
                      HOST_PHYSICAL_BASE, TEST_REGION_SIZE,
                      HOST_PHYSICAL_BASE + 0x100,
                      HOST_PHYSICAL_BASE + 0x101);

    dut.initiator_socket.bind(memory.target_socket);
    dut.p_enable_dmi = true;
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE,
                    TEST_REGION_SIZE);

    uint32_t value = 0;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(HOST_LOGICAL_BASE + 0x100);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    tlm::tlm_dmi dmi;
    EXPECT_FALSE(dut.translation_get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(trans.get_address(), HOST_LOGICAL_BASE + 0x100);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x0u);
}

TEST(RseAtuTest, RejectsNegativeOffsetUnderflowDmiWithoutLatchingMismatchStatus)
{
    constexpr uint64_t LOW_LOGICAL_BASE = 0x1000;
    constexpr uint64_t NEGATIVE_OFFSET_BYTES = uint64_t{0} - 0x2000;
    rse_atu dut("rse_atu_negative_offset_underflow_dmi");
    TestMemory memory("rse_atu_negative_offset_underflow_dmi_memory", 0);

    dut.initiator_socket.bind(memory.target_socket);
    dut.p_enable_dmi = true;
    const uint8_t page_shift =
        static_cast<uint8_t>((read32(dut, ATUBC) >> 4) & 0xfu);
    program_region_offset_pages(dut, 0, LOW_LOGICAL_BASE,
                                NEGATIVE_OFFSET_BYTES >> page_shift,
                                0x1000);

    uint32_t value = 0;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(LOW_LOGICAL_BASE);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    tlm::tlm_dmi dmi;
    EXPECT_FALSE(dut.translation_get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(trans.get_address(), LOW_LOGICAL_BASE);
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x0u);
}

TEST(RseAtuTest, RejectsUnmappedDmiWithoutLatchingMismatchStatus)
{
    rse_atu dut("rse_atu_unmapped_dmi");
    TestMemory memory("rse_atu_unmapped_dmi_memory");

    dut.initiator_socket.bind(memory.target_socket);

    uint32_t value = 0;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(HOST_LOGICAL_BASE);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    tlm::tlm_dmi dmi;
    EXPECT_FALSE(dut.translation_get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(read32(dut, ATUIS) & 0x1u, 0x0u);
}

TEST(RseAtuTest, TraceFilterAndAddressWindowSelectHostTranslation)
{
    rse_atu dut("rse_atu_trace_window");
    TestMemory memory("rse_atu_trace_window_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE, TEST_REGION_SIZE);
    dut.p_trace = true;
    dut.p_trace_filter = std::string("translation");
    dut.p_trace_address_min = HOST_LOGICAL_BASE + 0x100;
    dut.p_trace_address_max = HOST_LOGICAL_BASE + 0x1ff;
    dut.p_trace_limit = 16;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    uint32_t low_value = 0x11112222;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x20,
                          tlm::TLM_WRITE_COMMAND, low_value),
              tlm::TLM_OK_RESPONSE);

    uint32_t host_value = 0x33334444;
    EXPECT_EQ(translate32(dut, HOST_LOGICAL_BASE + 0x120,
                          tlm::TLM_WRITE_COMMAND, host_value),
              tlm::TLM_OK_RESPONSE);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_EQ(log.find("logical=0x70000020"), std::string::npos);
    EXPECT_NE(log.find("translate_write logical=0x70000120"),
              std::string::npos);
}

TEST(RseAtuTest, DmiTraceReportsTranslatedGrant)
{
    rse_atu dut("rse_atu_trace_dmi");
    TestMemory memory("rse_atu_trace_dmi_memory");

    dut.initiator_socket.bind(memory.target_socket);
    program_region0(dut, HOST_LOGICAL_BASE, HOST_PHYSICAL_BASE, TEST_REGION_SIZE);
    dut.p_enable_dmi = true;
    dut.p_trace = true;
    dut.p_trace_filter = std::string("dmi");
    dut.p_trace_address_min = HOST_LOGICAL_BASE;
    dut.p_trace_address_max = HOST_LOGICAL_BASE + TEST_REGION_SIZE - 1;
    dut.p_trace_limit = 16;

    uint32_t value = 0;
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(HOST_LOGICAL_BASE + 0x100);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    tlm::tlm_dmi dmi;
    const bool granted = dut.translation_get_direct_mem_ptr(trans, dmi);

    std::cerr.rdbuf(old_cerr);

    ASSERT_TRUE(granted);
    const auto log = captured.str();
    EXPECT_NE(log.find(" dmi logical=0x70000100"), std::string::npos);
    EXPECT_NE(log.find("status=ok"), std::string::npos);
    EXPECT_NE(log.find("reason=granted"), std::string::npos);
    EXPECT_NE(log.find("upstream=0x70000000-0x70001fff"),
              std::string::npos);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
