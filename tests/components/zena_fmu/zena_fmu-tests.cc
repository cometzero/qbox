/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <zena_fmu.h>

namespace {

constexpr uint64_t RECORD_STRIDE = 0x40;
constexpr uint64_t ERR_CTLR = 0x008;
constexpr uint64_t ERR_STATUS = 0x010;
constexpr uint64_t ERR_IMPDEF = 0x8000;
constexpr uint64_t SYS_KEY = 0x8bfc;
constexpr uint64_t ERRGSR0_L = 0xe000;
constexpr uint64_t PIDR4 = 0xffd0;
constexpr uint64_t PIDR0 = 0xffe0;
constexpr uint64_t PIDR1 = 0xffe4;
constexpr uint64_t PIDR2 = 0xffe8;
constexpr uint64_t PIDR3 = 0xffec;
constexpr uint64_t CIDR0 = 0xfff0;
constexpr uint64_t CIDR1 = 0xfff4;
constexpr uint64_t CIDR2 = 0xfff8;
constexpr uint64_t CIDR3 = 0xfffc;

constexpr uint32_t SYS_KEY_VALUE = 0xbe;
constexpr uint32_t CTLR_RW_MASK = 0x00002119;
constexpr uint32_t STATUS_V = 1u << 30;
constexpr uint32_t STATUS_UE = 1u << 29;
constexpr uint32_t STATUS_CI = 1u << 19;
constexpr uint32_t IMPDEF_IE = 1u << 9;

class SignalSink : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> signal;
    std::vector<bool> observed;

    explicit SignalSink(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , signal("signal")
    {
        signal.register_value_changed_cb([this](bool value) {
            observed.push_back(value);
        });
    }
};

class TlmBinder : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<TlmBinder, DEFAULT_TLM_BUSWIDTH> socket;

    explicit TlmBinder(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , socket("socket")
    {
    }
};

uint32_t access32(zena_fmu& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(zena_fmu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(zena_fmu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

void write32_keyed(zena_fmu& dut, uint64_t offset, uint32_t value)
{
    write32(dut, SYS_KEY, SYS_KEY_VALUE);
    write32(dut, offset, value);
}

uint64_t record_offset(unsigned int index, uint64_t field)
{
    return index * RECORD_STRIDE + field;
}

uint64_t impdef_offset(unsigned int index)
{
    return ERR_IMPDEF + index * 0x8;
}

}

TEST(ZenaFmuTest, ResetValuesMatchDocumentedPcid)
{
    zena_fmu dut("fmu_reset");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, PIDR4), 0x04u);
    EXPECT_EQ(read32(dut, PIDR0), 0xd0u);
    EXPECT_EQ(read32(dut, PIDR1), 0xb3u);
    EXPECT_EQ(read32(dut, PIDR2), 0x0bu);
    EXPECT_EQ(read32(dut, PIDR3), 0x00u);
    EXPECT_EQ(read32(dut, CIDR0), 0x0du);
    EXPECT_EQ(read32(dut, CIDR1), 0xf0u);
    EXPECT_EQ(read32(dut, CIDR2), 0x05u);
    EXPECT_EQ(read32(dut, CIDR3), 0xb1u);
    EXPECT_EQ(read32(dut, record_offset(0, ERR_STATUS)), 0u);
}

TEST(ZenaFmuTest, RegistersRequireSysKeyWhenConfigured)
{
    zena_fmu dut("fmu_key");
    dut.before_end_of_elaboration();

    write32(dut, record_offset(0, ERR_CTLR), 0);
    EXPECT_EQ(read32(dut, record_offset(0, ERR_CTLR)), CTLR_RW_MASK);

    write32_keyed(dut, record_offset(0, ERR_CTLR), 0);
    EXPECT_EQ(read32(dut, record_offset(0, ERR_CTLR)), 0u);
}

TEST(ZenaFmuTest, StatusUsesWriteOneToClearBits)
{
    zena_fmu dut("fmu_w1c");
    dut.before_end_of_elaboration();

    write32_keyed(dut, impdef_offset(1), IMPDEF_IE);
    EXPECT_NE(read32(dut, record_offset(1, ERR_STATUS)) & STATUS_V, 0u);
    EXPECT_EQ(read32(dut, ERRGSR0_L) & (1u << 1), 1u << 1);

    write32_keyed(dut, record_offset(1, ERR_STATUS), STATUS_V | STATUS_UE);
    EXPECT_EQ(read32(dut, record_offset(1, ERR_STATUS)) & STATUS_V, 0u);
    EXPECT_EQ(read32(dut, ERRGSR0_L) & (1u << 1), 0u);
}

TEST(ZenaFmuTest, FaultSignalsFollowCriticalAndNonCriticalStatus)
{
    zena_fmu dut("fmu_signals");
    TlmBinder tlm_binder("tlm_binder");
    SignalSink critical("critical_sink");
    SignalSink non_critical("non_critical_sink");

    tlm_binder.socket.bind(dut.target_socket);
    dut.critical_irq.bind(critical.signal);
    dut.non_critical_irq.bind(non_critical.signal);
    dut.before_end_of_elaboration();
    sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_PS));

    write32_keyed(dut, impdef_offset(0), IMPDEF_IE);
    sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_PS));
    ASSERT_FALSE(critical.observed.empty());
    EXPECT_TRUE(critical.observed.back());
    EXPECT_TRUE(non_critical.observed.empty() || !non_critical.observed.back());
    EXPECT_NE(read32(dut, record_offset(0, ERR_STATUS)) & STATUS_CI, 0u);

    write32_keyed(dut, record_offset(0, ERR_STATUS), STATUS_V | STATUS_CI);
    sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_PS));
    ASSERT_FALSE(critical.observed.empty());
    EXPECT_FALSE(critical.observed.back());

    write32_keyed(dut, impdef_offset(1), IMPDEF_IE);
    sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_PS));
    ASSERT_FALSE(non_critical.observed.empty());
    EXPECT_TRUE(non_critical.observed.back());
    EXPECT_NE(read32(dut, record_offset(1, ERR_STATUS)) & STATUS_UE, 0u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
