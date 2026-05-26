/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <mhuv3_stub.h>
#include <ports/target-signal-socket.h>

namespace {

constexpr uint64_t SHMEM_BASE = 0x4000040000000;
constexpr uint64_t SHMEM_SIZE = 0x400;
constexpr uint64_t SCMI_STATUS = 0x04;
constexpr uint64_t SCMI_LENGTH = 0x14;
constexpr uint64_t SCMI_HEADER = 0x18;
constexpr uint64_t SCMI_PAYLOAD = 0x1c;
constexpr uint64_t CTRL_FEAT_SPT0 = 0x010;
constexpr uint64_t CTRL_FEAT_SPT1 = 0x014;
constexpr uint64_t CTRL_IIDR = 0xfc8;
constexpr uint64_t CTRL_AIDR = 0xfcc;
constexpr uint64_t CTRL_DBCH_INT_ST0 = 0x400;
constexpr uint64_t RPMSG_TEST_VRING = SHMEM_BASE + 0x100;
constexpr uint64_t RPMSG_TEST_BUFFER = SHMEM_BASE + 0x200;
constexpr uint64_t RPMSG_TEST_AVAIL = RPMSG_TEST_VRING + 0x20;
constexpr uint64_t RPMSG_TEST_USED = RPMSG_TEST_VRING + 0x30;
constexpr uint64_t DBCH_CFG0 = 0x20;
constexpr uint64_t DBCW_SET = 0x100c;
constexpr uint64_t DBCW_ST = 0x1000;
constexpr uint64_t DBCW_ST_MSK = 0x1004;
constexpr uint64_t DBCW_CLR = 0x1008;
constexpr uint64_t DBCW_INT_ST = 0x1010;
constexpr uint64_t DBCW_INT_CLR = 0x1014;
constexpr uint64_t DBCW_INT_EN = 0x1018;
constexpr uint64_t DBCW_STRIDE = 0x20;
constexpr uint32_t MHU_NOTIFY_VALUE = 1234;
constexpr uint32_t SCMI_PROTOCOL_POWER_DOMAIN = 0x11;
constexpr uint32_t SCMI_PROTOCOL_SYS_POWER = 0x12;
constexpr uint32_t SCMI_MESSAGE_PROTOCOL_VERSION = 0x0;
constexpr uint32_t SCMI_MESSAGE_PROTOCOL_MESSAGE_ATTRIBUTES = 0x2;
constexpr uint32_t SCMI_MESSAGE_POWER_STATE_SET = 0x4;
constexpr uint32_t SCMI_MESSAGE_POWER_STATE_GET = 0x5;
constexpr uint32_t SCMI_MESSAGE_SYS_POWER_STATE_SET = 0x3;
constexpr uint32_t SCMI_MESSAGE_SYS_POWER_STATE_NOTIFY = 0x5;
constexpr uint32_t SCMI_SYS_POWER_COLD_RESET = 0x1;

uint32_t scmi_header(uint32_t protocol, uint32_t message)
{
    return (protocol << 10) | message;
}

class TestMemory : public sc_core::sc_module
{
    std::array<uint8_t, SHMEM_SIZE> m_mem {};

public:
    tlm_utils::simple_target_socket<TestMemory, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit TestMemory(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , target_socket("target_socket")
    {
        target_socket.register_b_transport(this, &TestMemory::b_transport);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        const uint64_t address = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (address < SHMEM_BASE || address + len > SHMEM_BASE + m_mem.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        const auto offset = static_cast<size_t>(address - SHMEM_BASE);
        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_mem[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&m_mem[offset], data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    uint32_t read32(uint64_t offset) const
    {
        uint32_t value = 0;
        std::memcpy(&value, &m_mem[offset], sizeof(value));
        return value;
    }

    uint16_t read16(uint64_t offset) const
    {
        uint16_t value = 0;
        std::memcpy(&value, &m_mem[offset], sizeof(value));
        return value;
    }

    uint8_t read8(uint64_t offset) const
    {
        return m_mem[offset];
    }

    void write32(uint64_t offset, uint32_t value)
    {
        std::memcpy(&m_mem[offset], &value, sizeof(value));
    }

    void write16(uint64_t offset, uint16_t value)
    {
        std::memcpy(&m_mem[offset], &value, sizeof(value));
    }

    void write64(uint64_t offset, uint64_t value)
    {
        std::memcpy(&m_mem[offset], &value, sizeof(value));
    }
};

class TestInitiator : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<TestInitiator, DEFAULT_TLM_BUSWIDTH> initiator_socket;

    explicit TestInitiator(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , initiator_socket("initiator_socket")
    {
    }
};

class ResetSink : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> reset;
    unsigned int write_count = 0;
    bool saw_asserted = false;

    explicit ResetSink(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , reset("reset")
    {
        reset.register_value_changed_cb([this](const bool& value) {
            ++write_count;
            saw_asserted = saw_asserted || value;
        });
    }
};

uint32_t access32(TestInitiator& initiator, uint64_t offset, tlm::tlm_command command,
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
    initiator.initiator_socket->b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(TestInitiator& initiator, uint64_t offset)
{
    return access32(initiator, offset, tlm::TLM_READ_COMMAND);
}

void write32(TestInitiator& initiator, uint64_t offset, uint32_t value)
{
    (void)access32(initiator, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Mhuv3FrameModelTest, PbxAndMbxDoorbellRegistersAreReusable)
{
    mhuv3_stub::mhuv3_frame_model pbx;
    mhuv3_stub::mhuv3_frame_model mbx;

    pbx.configure(false, 4, 0x55aau, 0xaa55u, 0x11223344u, 0x55667788u);
    mbx.configure(true, 4, 0x55aau, 0xaa55u, 0x11223344u, 0x55667788u);

    EXPECT_EQ(pbx.read32(DBCH_CFG0), 3u);
    EXPECT_EQ(pbx.read32(CTRL_FEAT_SPT0), 0x55aau);
    EXPECT_EQ(pbx.read32(CTRL_FEAT_SPT1), 0xaa55u);
    EXPECT_EQ(pbx.read32(CTRL_IIDR), 0x11223344u);
    EXPECT_EQ(pbx.read32(CTRL_AIDR), 0x55667788u);
    EXPECT_EQ(mbx.read32(DBCH_CFG0), 3u);

    pbx.set_status_bits(0, 0x3u);
    pbx.set_int_enable(0, 0x1u);
    pbx.set_int_status_bits(0, 0x1u);
    pbx.refresh_combined_irq_regs();

    EXPECT_EQ(pbx.read32(DBCW_ST), 0x3u);
    EXPECT_EQ(pbx.read32(DBCW_INT_ST), 0x1u);
    EXPECT_EQ(pbx.read32(DBCW_INT_EN), 0x1u);
    EXPECT_EQ(pbx.read32(CTRL_DBCH_INT_ST0), 0x1u);

    mbx.set_status_bits(0, 0x3u);
    mbx.refresh_combined_irq_regs();
    EXPECT_EQ(mbx.read32(DBCW_ST), 0x3u);
    EXPECT_EQ(mbx.read32(DBCW_ST_MSK), 0x0u);
    EXPECT_EQ(mbx.read32(CTRL_DBCH_INT_ST0), 0x0u);

    mbx.clear_mask_bits(0, 0x1u);
    mbx.refresh_combined_irq_regs();
    EXPECT_EQ(mbx.read32(DBCW_ST_MSK), 0x1u);
    EXPECT_EQ(mbx.read32(CTRL_DBCH_INT_ST0), 0x1u);

    mbx.clear_status_bits(0, 0x1u);
    mbx.refresh_combined_irq_regs();
    EXPECT_EQ(mbx.read32(DBCW_ST), 0x2u);
    EXPECT_EQ(mbx.read32(DBCW_ST_MSK), 0x0u);
    EXPECT_EQ(mbx.read32(CTRL_DBCH_INT_ST0), 0x0u);
}

TEST(Mhuv3StubTest, RseBl2PowerDomainTransportRespondsAndSignalsAckBit)
{
    auto broker = cci::cci_get_global_broker(cci::cci_originator("mhuv3_stub_test"));
    broker.set_preset_cci_value("rse_si_pbx.pair", cci::cci_value(std::string("rse_si_cl0")));
    broker.set_preset_cci_value("rse_si_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("rse_si_pbx.tx_shmem", cci::cci_value(SHMEM_BASE));
    broker.set_preset_cci_value("rse_si_pbx.ack_bit", cci::cci_value(1u));
    broker.set_preset_cci_value("rse_si_pbx.assert_power_on_reset", cci::cci_value(true));
    broker.set_preset_cci_value("rse_si_pbx.power_domain_reset_count",
                                cci::cci_value(4u));
    broker.set_preset_cci_value("rse_si_mbx.pair", cci::cci_value(std::string("rse_si_cl0")));
    broker.set_preset_cci_value("rse_si_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("ap_rse_pbx.pair", cci::cci_value(std::string("ap_rse")));
    broker.set_preset_cci_value("ap_rse_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("ap_rse_pbx.protocol", cci::cci_value(std::string("doorbell")));
    broker.set_preset_cci_value("ap_rse_pbx.direct_boot_compat",
                                cci::cci_value(true));
    broker.set_preset_cci_value("ap_rse_mbx.pair", cci::cci_value(std::string("ap_rse")));
    broker.set_preset_cci_value("ap_rse_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("ap_rse_mbx.protocol", cci::cci_value(std::string("doorbell")));
    broker.set_preset_cci_value("si_cl1_pbx.pair", cci::cci_value(std::string("ap_si_cl1")));
    broker.set_preset_cci_value("si_cl1_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("si_cl1_pbx.protocol", cci::cci_value(std::string("doorbell")));
    broker.set_preset_cci_value("si_cl1_pbx.doorbell_ack_trigger_channel",
                                cci::cci_value(0u));
    broker.set_preset_cci_value("si_cl1_pbx.doorbell_ack_trigger_value",
                                cci::cci_value(0x8u));
    broker.set_preset_cci_value("si_cl1_pbx.doorbell_ack_channel",
                                cci::cci_value(0u));
    broker.set_preset_cci_value("si_cl1_pbx.doorbell_ack_value",
                                cci::cci_value(0x4u));
    broker.set_preset_cci_value("si_cl1_pbx.doorbell_ack_seed_address",
                                cci::cci_value(SHMEM_BASE + 0x40));
    const std::vector<unsigned int> resource_table_seed = {
        0x00000001u, 0x00000001u,
        0x00000000u, 0x00000000u,
        0x00000014u, 0x00000003u,
        0x00000007u, 0x00000000u,
        0x00000001u, 0x00000000u,
        0x00000000u, 0x00000200u,
        0xffffffffu, 0x00000010u,
        0x00000020u, 0x00000000u,
        0x00000000u, 0xffffffffu,
        0x00000010u, 0x00000020u,
        0x00000001u, 0x00000000u,
    };
    for (size_t i = 0; i < resource_table_seed.size(); ++i) {
        broker.set_preset_cci_value(
            "si_cl1_pbx.doorbell_ack_seed_words." + std::to_string(i + 1),
            cci::cci_value(resource_table_seed[i]));
    }
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_enable",
                                cci::cci_value(true));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_name",
                                cci::cci_value(std::string("ethsi1")));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_remote_addr",
                                cci::cci_value(0x400u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_vring_address",
                                cci::cci_value(RPMSG_TEST_VRING));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_vring_num",
                                cci::cci_value(2u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_vring_align",
                                cci::cci_value(16u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_signal_channel",
                                cci::cci_value(0u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_signal_value",
                                cci::cci_value(0x1u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_poll_period_ns",
                                cci::cci_value(1u));
    broker.set_preset_cci_value("si_cl1_pbx.rpmsg_ns_max_polls",
                                cci::cci_value(4u));
    broker.set_preset_cci_value("si_cl1_mbx.pair", cci::cci_value(std::string("ap_si_cl1")));
    broker.set_preset_cci_value("si_cl1_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("si_cl1_mbx.protocol", cci::cci_value(std::string("doorbell")));
    broker.set_preset_cci_value("deferred_pbx.pair", cci::cci_value(std::string("deferred")));
    broker.set_preset_cci_value("deferred_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("deferred_pbx.tx_shmem", cci::cci_value(SHMEM_BASE));
    broker.set_preset_cci_value("deferred_pbx.power_domain_reset_count",
                                cci::cci_value(2u));
    broker.set_preset_cci_value("deferred_pbx.power_domain_reset_assert_on_power_off",
                                cci::cci_value(false));
    broker.set_preset_cci_value("deferred_pbx.power_domain_reset_pulse_on_power_on",
                                cci::cci_value(true));
    broker.set_preset_cci_value("deferred_mbx.pair", cci::cci_value(std::string("deferred")));
    broker.set_preset_cci_value("deferred_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("bridge_ap_pbx.pair",
                                cci::cci_value(std::string("test_ap_to_rse")));
    broker.set_preset_cci_value("bridge_ap_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("bridge_ap_pbx.protocol",
                                cci::cci_value(std::string("doorbell-bridge")));
    broker.set_preset_cci_value("bridge_rse_mbx.pair",
                                cci::cci_value(std::string("test_ap_to_rse")));
    broker.set_preset_cci_value("bridge_rse_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("bridge_rse_mbx.protocol",
                                cci::cci_value(std::string("doorbell-bridge")));
    broker.set_preset_cci_value("bridge_rse_pbx.pair",
                                cci::cci_value(std::string("test_rse_to_ap")));
    broker.set_preset_cci_value("bridge_rse_pbx.frame", cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("bridge_rse_pbx.protocol",
                                cci::cci_value(std::string("doorbell-bridge")));
    broker.set_preset_cci_value("bridge_ap_mbx.pair",
                                cci::cci_value(std::string("test_rse_to_ap")));
    broker.set_preset_cci_value("bridge_ap_mbx.frame", cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("bridge_ap_mbx.protocol",
                                cci::cci_value(std::string("doorbell-bridge")));
    broker.set_preset_cci_value("limited_pbx.pair",
                                cci::cci_value(std::string("limited")));
    broker.set_preset_cci_value("limited_pbx.frame",
                                cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("limited_pbx.protocol",
                                cci::cci_value(std::string("doorbell-bridge")));
    broker.set_preset_cci_value("limited_pbx.channel_count",
                                cci::cci_value(4u));
    broker.set_preset_cci_value("limited_pbx.feat_spt0",
                                cci::cci_value(0x55aau));
    broker.set_preset_cci_value("limited_pbx.feat_spt1",
                                cci::cci_value(0xaa55u));
    broker.set_preset_cci_value("limited_pbx.iidr",
                                cci::cci_value(0x11223344u));
    broker.set_preset_cci_value("limited_pbx.aidr",
                                cci::cci_value(0x55667788u));
    broker.set_preset_cci_value("strict_pbx.pair",
                                cci::cci_value(std::string("strict")));
    broker.set_preset_cci_value("strict_pbx.frame",
                                cci::cci_value(std::string("pbx")));
    broker.set_preset_cci_value("strict_pbx.protocol",
                                cci::cci_value(std::string("doorbell")));
    broker.set_preset_cci_value("strict_mbx.pair",
                                cci::cci_value(std::string("strict")));
    broker.set_preset_cci_value("strict_mbx.frame",
                                cci::cci_value(std::string("mbx")));
    broker.set_preset_cci_value("strict_mbx.protocol",
                                cci::cci_value(std::string("doorbell")));

    TestMemory shmem("si_scmi_shmem");
    TestMemory unused("unused_shmem");
    TestMemory ap_pbx_unused("ap_pbx_unused_shmem");
    TestMemory ap_unused("ap_unused_shmem");
    TestMemory si_cl1_pbx_unused("si_cl1_pbx_unused_shmem");
    TestMemory si_cl1_unused("si_cl1_unused_shmem");
    TestMemory deferred_shmem("deferred_shmem");
    TestMemory deferred_unused("deferred_unused_shmem");
    TestMemory bridge_ap_pbx_unused("bridge_ap_pbx_unused_shmem");
    TestMemory bridge_rse_mbx_unused("bridge_rse_mbx_unused_shmem");
    TestMemory bridge_rse_pbx_unused("bridge_rse_pbx_unused_shmem");
    TestMemory bridge_ap_mbx_unused("bridge_ap_mbx_unused_shmem");
    TestMemory limited_unused("limited_unused_shmem");
    TestMemory strict_pbx_unused("strict_pbx_unused_shmem");
    TestMemory strict_mbx_unused("strict_mbx_unused_shmem");
    TestInitiator pbx_bus("pbx_bus");
    TestInitiator mbx_bus("mbx_bus");
    TestInitiator ap_pbx_bus("ap_pbx_bus");
    TestInitiator ap_mbx_bus("ap_mbx_bus");
    TestInitiator si_cl1_pbx_bus("si_cl1_pbx_bus");
    TestInitiator si_cl1_mbx_bus("si_cl1_mbx_bus");
    TestInitiator deferred_pbx_bus("deferred_pbx_bus");
    TestInitiator deferred_mbx_bus("deferred_mbx_bus");
    TestInitiator bridge_ap_pbx_bus("bridge_ap_pbx_bus");
    TestInitiator bridge_rse_mbx_bus("bridge_rse_mbx_bus");
    TestInitiator bridge_rse_pbx_bus("bridge_rse_pbx_bus");
    TestInitiator bridge_ap_mbx_bus("bridge_ap_mbx_bus");
    TestInitiator limited_bus("limited_bus");
    TestInitiator strict_pbx_bus("strict_pbx_bus");
    TestInitiator strict_mbx_bus("strict_mbx_bus");
    mhuv3_stub pbx("rse_si_pbx");
    mhuv3_stub mbx("rse_si_mbx");
    mhuv3_stub ap_pbx("ap_rse_pbx");
    mhuv3_stub ap_mbx("ap_rse_mbx");
    mhuv3_stub si_cl1_pbx("si_cl1_pbx");
    mhuv3_stub si_cl1_mbx("si_cl1_mbx");
    mhuv3_stub deferred_pbx("deferred_pbx");
    mhuv3_stub deferred_mbx("deferred_mbx");
    mhuv3_stub bridge_ap_pbx("bridge_ap_pbx");
    mhuv3_stub bridge_rse_mbx("bridge_rse_mbx");
    mhuv3_stub bridge_rse_pbx("bridge_rse_pbx");
    mhuv3_stub bridge_ap_mbx("bridge_ap_mbx");
    mhuv3_stub limited_pbx("limited_pbx");
    mhuv3_stub strict_pbx("strict_pbx");
    mhuv3_stub strict_mbx("strict_mbx");
    ResetSink ap_reset("ap_reset");
    ResetSink system_reset("system_reset");
    ResetSink ap_domain1_reset("ap_domain1_reset");
    ResetSink ap_domain2_reset("ap_domain2_reset");
    ResetSink ap_domain3_reset("ap_domain3_reset");
    ResetSink deferred_domain1_reset("deferred_domain1_reset");
    ResetSink si_cl1_pbx_irq("si_cl1_pbx_irq");

    pbx_bus.initiator_socket.bind(pbx.target_socket);
    mbx_bus.initiator_socket.bind(mbx.target_socket);
    ap_pbx_bus.initiator_socket.bind(ap_pbx.target_socket);
    ap_mbx_bus.initiator_socket.bind(ap_mbx.target_socket);
    si_cl1_pbx_bus.initiator_socket.bind(si_cl1_pbx.target_socket);
    si_cl1_mbx_bus.initiator_socket.bind(si_cl1_mbx.target_socket);
    deferred_pbx_bus.initiator_socket.bind(deferred_pbx.target_socket);
    deferred_mbx_bus.initiator_socket.bind(deferred_mbx.target_socket);
    bridge_ap_pbx_bus.initiator_socket.bind(bridge_ap_pbx.target_socket);
    bridge_rse_mbx_bus.initiator_socket.bind(bridge_rse_mbx.target_socket);
    bridge_rse_pbx_bus.initiator_socket.bind(bridge_rse_pbx.target_socket);
    bridge_ap_mbx_bus.initiator_socket.bind(bridge_ap_mbx.target_socket);
    limited_bus.initiator_socket.bind(limited_pbx.target_socket);
    strict_pbx_bus.initiator_socket.bind(strict_pbx.target_socket);
    strict_mbx_bus.initiator_socket.bind(strict_mbx.target_socket);
    pbx.initiator_socket.bind(shmem.target_socket);
    mbx.initiator_socket.bind(unused.target_socket);
    ap_pbx.initiator_socket.bind(ap_pbx_unused.target_socket);
    ap_mbx.initiator_socket.bind(ap_unused.target_socket);
    si_cl1_pbx.initiator_socket.bind(si_cl1_pbx_unused.target_socket);
    si_cl1_mbx.initiator_socket.bind(si_cl1_unused.target_socket);
    deferred_pbx.initiator_socket.bind(deferred_shmem.target_socket);
    deferred_mbx.initiator_socket.bind(deferred_unused.target_socket);
    bridge_ap_pbx.initiator_socket.bind(bridge_ap_pbx_unused.target_socket);
    bridge_rse_mbx.initiator_socket.bind(bridge_rse_mbx_unused.target_socket);
    bridge_rse_pbx.initiator_socket.bind(bridge_rse_pbx_unused.target_socket);
    bridge_ap_mbx.initiator_socket.bind(bridge_ap_mbx_unused.target_socket);
    limited_pbx.initiator_socket.bind(limited_unused.target_socket);
    strict_pbx.initiator_socket.bind(strict_pbx_unused.target_socket);
    strict_mbx.initiator_socket.bind(strict_mbx_unused.target_socket);
    pbx.power_on_reset.bind(ap_reset.reset);
    pbx.system_reset.bind(system_reset.reset);
    pbx.power_domain_reset[1].bind(ap_domain1_reset.reset);
    pbx.power_domain_reset[2].bind(ap_domain2_reset.reset);
    pbx.power_domain_reset[3].bind(ap_domain3_reset.reset);
    deferred_pbx.power_domain_reset[1].bind(deferred_domain1_reset.reset);
    si_cl1_pbx.irq.bind(si_cl1_pbx_irq.reset);

    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_TRUE(ap_reset.reset.read());
    EXPECT_FALSE(system_reset.reset.read());
    EXPECT_GE(read32(pbx_bus, DBCH_CFG0) + 1u, 2u);
    EXPECT_GE(read32(ap_pbx_bus, DBCH_CFG0) + 1u, 2u);
    EXPECT_EQ(read32(ap_pbx_bus, DBCH_CFG0), read32(ap_mbx_bus, DBCH_CFG0));
    EXPECT_EQ(read32(limited_bus, DBCH_CFG0), 3u);
    EXPECT_EQ(read32(limited_bus, CTRL_FEAT_SPT0), 0x55aau);
    EXPECT_EQ(read32(limited_bus, CTRL_FEAT_SPT1), 0xaa55u);
    EXPECT_EQ(read32(limited_bus, CTRL_IIDR), 0x11223344u);
    EXPECT_EQ(read32(limited_bus, CTRL_AIDR), 0x55667788u);
    const uint32_t strict_notify_channel = read32(strict_pbx_bus, DBCH_CFG0);
    const uint64_t strict_notify_base = 0x1000 +
                                        (strict_notify_channel * DBCW_STRIDE);
    write32(strict_pbx_bus, strict_notify_base + (DBCW_SET - 0x1000),
            MHU_NOTIFY_VALUE);
    EXPECT_EQ(read32(strict_pbx_bus, strict_notify_base), MHU_NOTIFY_VALUE);
    EXPECT_EQ(read32(strict_mbx_bus, DBCW_ST), 0u);
    EXPECT_EQ(read32(strict_mbx_bus, strict_notify_base), 0u);

    write32(limited_bus, DBCW_SET, 0x1u);
    EXPECT_EQ(read32(limited_bus, DBCW_ST), 0x1u);
    EXPECT_EQ(read32(strict_mbx_bus, DBCW_ST), 0u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t));
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                           SCMI_MESSAGE_PROTOCOL_VERSION));

    write32(pbx_bus, DBCW_SET, 2);

    EXPECT_EQ(shmem.read32(SCMI_STATUS), 1u);
    EXPECT_EQ(shmem.read32(SCMI_LENGTH), 12u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD), 0u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD + sizeof(uint32_t)), 0x00020000u);
    EXPECT_EQ(read32(mbx_bus, DBCW_ST), 2u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST), 0u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 4);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                           SCMI_MESSAGE_POWER_STATE_SET));
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), 2);
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t) * 2, 0x00000111);
    write32(pbx_bus, DBCW_SET, 2);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_FALSE(ap_reset.reset.read());
    EXPECT_FALSE(ap_domain1_reset.reset.read());
    EXPECT_FALSE(ap_domain2_reset.reset.read());
    EXPECT_FALSE(ap_domain3_reset.reset.read());

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 2);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                           SCMI_MESSAGE_POWER_STATE_GET));
    shmem.write32(SCMI_PAYLOAD, 2);
    write32(pbx_bus, DBCW_SET, 2);

    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD), 0u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD + sizeof(uint32_t)), 0x00000111u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 2);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_SYS_POWER,
                                           SCMI_MESSAGE_PROTOCOL_MESSAGE_ATTRIBUTES));
    shmem.write32(SCMI_PAYLOAD, SCMI_MESSAGE_SYS_POWER_STATE_NOTIFY);
    write32(pbx_bus, DBCW_SET, 2);

    EXPECT_EQ(shmem.read32(SCMI_STATUS), 1u);
    EXPECT_EQ(shmem.read32(SCMI_LENGTH), 12u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD), 0u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD + sizeof(uint32_t)), 0u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 2);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_SYS_POWER,
                                           SCMI_MESSAGE_SYS_POWER_STATE_NOTIFY));
    shmem.write32(SCMI_PAYLOAD, 1);
    write32(pbx_bus, DBCW_SET, 2);

    EXPECT_EQ(shmem.read32(SCMI_STATUS), 1u);
    EXPECT_EQ(shmem.read32(SCMI_LENGTH), 8u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD), 0u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 3);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_SYS_POWER,
                                           SCMI_MESSAGE_SYS_POWER_STATE_SET));
    shmem.write32(SCMI_PAYLOAD, 1);
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), SCMI_SYS_POWER_COLD_RESET);
    write32(pbx_bus, DBCW_SET, 2);
    sc_core::sc_start(sc_core::sc_time(3, sc_core::SC_NS));

    EXPECT_EQ(shmem.read32(SCMI_STATUS), 1u);
    EXPECT_EQ(shmem.read32(SCMI_LENGTH), 8u);
    EXPECT_EQ(shmem.read32(SCMI_PAYLOAD), 0u);
    EXPECT_FALSE(system_reset.reset.read());
    EXPECT_TRUE(system_reset.saw_asserted);
    EXPECT_GE(system_reset.write_count, 2u);

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 4);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                           SCMI_MESSAGE_POWER_STATE_SET));
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), 2);
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t) * 2, 0);
    write32(pbx_bus, DBCW_SET, 2);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_TRUE(ap_domain2_reset.reset.read());

    shmem.write32(SCMI_STATUS, 0);
    shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 4);
    shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                           SCMI_MESSAGE_POWER_STATE_SET));
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), 2);
    shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t) * 2, 0x00000111);
    write32(pbx_bus, DBCW_SET, 2);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_FALSE(ap_domain2_reset.reset.read());

    deferred_shmem.write32(SCMI_STATUS, 0);
    deferred_shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 4);
    deferred_shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                                    SCMI_MESSAGE_POWER_STATE_SET));
    deferred_shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), 1);
    deferred_shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t) * 2, 0);
    write32(deferred_pbx_bus, DBCW_SET, 1);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_FALSE(deferred_domain1_reset.reset.read());
    EXPECT_FALSE(deferred_domain1_reset.saw_asserted);

    deferred_shmem.write32(SCMI_STATUS, 0);
    deferred_shmem.write32(SCMI_LENGTH, sizeof(uint32_t) * 4);
    deferred_shmem.write32(SCMI_HEADER, scmi_header(SCMI_PROTOCOL_POWER_DOMAIN,
                                                    SCMI_MESSAGE_POWER_STATE_SET));
    deferred_shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t), 1);
    deferred_shmem.write32(SCMI_PAYLOAD + sizeof(uint32_t) * 2, 0x00010011);
    write32(deferred_pbx_bus, DBCW_SET, 1);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_NS));
    EXPECT_FALSE(deferred_domain1_reset.reset.read());
    EXPECT_TRUE(deferred_domain1_reset.saw_asserted);
    EXPECT_GE(deferred_domain1_reset.write_count, 2u);

    const uint32_t notify_channel = read32(ap_pbx_bus, DBCH_CFG0);
    const uint64_t notify_base = 0x1000 + (notify_channel * DBCW_STRIDE);

    write32(ap_pbx_bus, DBCW_SET, 12);
    write32(ap_pbx_bus, DBCW_SET + DBCW_STRIDE, 0x12340700);
    write32(ap_pbx_bus, DBCW_SET + (2 * DBCW_STRIDE), 0x11223344);
    write32(ap_pbx_bus, DBCW_SET + (3 * DBCW_STRIDE), 0x1);
    write32(ap_pbx_bus, notify_base + (DBCW_SET - 0x1000), MHU_NOTIFY_VALUE);

    EXPECT_EQ(read32(ap_pbx_bus, notify_base), 0u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST), 16u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST + DBCW_STRIDE), 0x12340700u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST + (2 * DBCW_STRIDE)), 0u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST + (3 * DBCW_STRIDE)), 0u);
    EXPECT_EQ(read32(ap_mbx_bus, DBCW_ST + (4 * DBCW_STRIDE)), 0u);
    EXPECT_EQ(read32(ap_mbx_bus, notify_base), MHU_NOTIFY_VALUE);

    write32(ap_mbx_bus, notify_base + (DBCW_CLR - 0x1000), MHU_NOTIFY_VALUE);
    EXPECT_EQ(read32(ap_mbx_bus, notify_base), 0u);

    const uint32_t bridge_notify_channel = read32(bridge_ap_pbx_bus, DBCH_CFG0);
    const uint64_t bridge_notify_base = 0x1000 + (bridge_notify_channel * DBCW_STRIDE);

    write32(bridge_ap_pbx_bus, DBCW_SET, 8);
    write32(bridge_ap_pbx_bus, DBCW_SET + DBCW_STRIDE, 0x44556677);
    write32(bridge_ap_pbx_bus, bridge_notify_base + (DBCW_SET - 0x1000),
            MHU_NOTIFY_VALUE);

    EXPECT_EQ(read32(bridge_rse_mbx_bus, DBCW_ST), 8u);
    EXPECT_EQ(read32(bridge_rse_mbx_bus, DBCW_ST + DBCW_STRIDE), 0x44556677u);
    EXPECT_EQ(read32(bridge_rse_mbx_bus, bridge_notify_base), MHU_NOTIFY_VALUE);
    EXPECT_EQ(read32(bridge_ap_mbx_bus, DBCW_ST), 0u);

    write32(bridge_rse_mbx_bus, DBCW_CLR, 0xffffffffu);
    write32(bridge_rse_mbx_bus, DBCW_CLR + DBCW_STRIDE, 0xffffffffu);
    write32(bridge_rse_mbx_bus, bridge_notify_base + (DBCW_CLR - 0x1000),
            0xffffffffu);
    EXPECT_EQ(read32(bridge_rse_mbx_bus, DBCW_ST), 0u);
    EXPECT_EQ(read32(bridge_ap_pbx_bus, DBCW_ST), 0u);
    EXPECT_EQ(read32(bridge_ap_pbx_bus, bridge_notify_base), 0u);

    write32(bridge_rse_pbx_bus, DBCW_SET, 4);
    write32(bridge_rse_pbx_bus, DBCW_SET + DBCW_STRIDE, 0x89abcdef);
    write32(bridge_rse_pbx_bus, bridge_notify_base + (DBCW_SET - 0x1000),
            MHU_NOTIFY_VALUE);

    EXPECT_EQ(read32(bridge_ap_mbx_bus, DBCW_ST), 4u);
    EXPECT_EQ(read32(bridge_ap_mbx_bus, DBCW_ST + DBCW_STRIDE), 0x89abcdefu);
    EXPECT_EQ(read32(bridge_ap_mbx_bus, bridge_notify_base), MHU_NOTIFY_VALUE);
    EXPECT_EQ(read32(bridge_rse_mbx_bus, DBCW_ST), 0u);

    si_cl1_pbx_unused.write64(0x100, RPMSG_TEST_BUFFER);
    si_cl1_pbx_unused.write32(0x108, 0x100);
    si_cl1_pbx_unused.write16(0x10c, 0x2);
    si_cl1_pbx_unused.write16(0x10e, 0);
    si_cl1_pbx_unused.write16(0x122, 1);
    si_cl1_pbx_unused.write16(0x124, 0);

    write32(si_cl1_pbx_bus, DBCW_INT_EN, 0x1);
    EXPECT_FALSE(si_cl1_pbx_irq.reset.read());
    write32(si_cl1_mbx_bus, DBCW_CLR, 0xffffffffu);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0u);
    EXPECT_FALSE(si_cl1_pbx_irq.reset.read());

    write32(si_cl1_pbx_bus, DBCW_SET, 0x8);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_ST) & 0x8u, 0x8u);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0u);
    EXPECT_FALSE(si_cl1_pbx_irq.reset.read());
    EXPECT_EQ(read32(si_cl1_mbx_bus, DBCW_ST) & 0x4u, 0x4u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x40), 0x00000001u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x44), 0x00000001u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x54), 0x00000003u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x58), 0x00000007u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x6c), 0x00000200u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x78), 0x00000020u);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_US));
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_ST) & 0x8u, 0u);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0x1u);
    EXPECT_TRUE(si_cl1_pbx_irq.reset.read());
    EXPECT_TRUE(si_cl1_pbx_irq.saw_asserted);
    EXPECT_EQ(read32(si_cl1_mbx_bus, DBCW_ST) & 0x1u, 0u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x200), 0u);

    write32(si_cl1_pbx_bus, DBCW_INT_CLR, 0x1u);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0u);
    EXPECT_FALSE(si_cl1_pbx_irq.reset.read());
    write32(si_cl1_pbx_bus, DBCW_SET, 0x1);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_ST) & 0x1u, 0x1u);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0u);
    EXPECT_FALSE(si_cl1_pbx_irq.reset.read());
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_US));
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_ST) & 0x1u, 0u);
    EXPECT_EQ(read32(si_cl1_pbx_bus, DBCW_INT_ST) & 0x1u, 0x1u);
    EXPECT_TRUE(si_cl1_pbx_irq.reset.read());
    EXPECT_EQ(read32(si_cl1_mbx_bus, DBCW_ST) & 0x1u, 0x1u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x200), 0x400u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x204), 53u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x20c), 40u);
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x210), 'e');
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x211), 't');
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x212), 'h');
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x213), 's');
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x214), 'i');
    EXPECT_EQ(si_cl1_pbx_unused.read8(0x215), '1');
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x230), 0x400u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x234), 0u);
    EXPECT_EQ(si_cl1_pbx_unused.read16(0x132), 1u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x134), 0u);
    EXPECT_EQ(si_cl1_pbx_unused.read32(0x138), 56u);
    write32(si_cl1_mbx_bus, DBCW_CLR, 0x5);
    EXPECT_EQ(read32(si_cl1_mbx_bus, DBCW_ST), 0u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
