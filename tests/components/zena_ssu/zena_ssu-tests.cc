/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <zena_ssu.h>

namespace {

constexpr uint64_t ERR_FR = 0x000;
constexpr uint64_t ERR_CTRL = 0x008;
constexpr uint64_t ERR_STATUS = 0x010;
constexpr uint64_t ERR_IMPDEF = 0x800;
constexpr uint64_t SYS_KEY = 0x804;
constexpr uint64_t SYS_STATUS = 0x808;
constexpr uint64_t SYS_CTRL = 0x810;
constexpr uint64_t STATUS_DETAIL = 0x814;
constexpr uint64_t PIDR4 = 0xfd0;
constexpr uint64_t PIDR0 = 0xfe0;
constexpr uint64_t PIDR1 = 0xfe4;
constexpr uint64_t PIDR2 = 0xfe8;
constexpr uint64_t PIDR3 = 0xfec;
constexpr uint64_t CIDR0 = 0xff0;
constexpr uint64_t CIDR1 = 0xff4;
constexpr uint64_t CIDR2 = 0xff8;
constexpr uint64_t CIDR3 = 0xffc;

constexpr uint32_t SYS_KEY_VALUE = 0xbe;
constexpr uint32_t STATUS_V = 1u << 30;
constexpr uint32_t SYS_STATUS_TEST = 1u << 0;
constexpr uint32_t SYS_STATUS_SAFE = 1u << 1;
constexpr uint32_t SYS_STATUS_ERRN = 1u << 2;
constexpr uint32_t SYS_STATUS_ERRC = 1u << 3;

class FaultSource : public sc_core::sc_module
{
    bool m_pending = false;
    sc_core::sc_event m_event;

    void emit()
    {
        signal->write(m_pending);
    }

public:
    SC_HAS_PROCESS(FaultSource);

    InitiatorSignalSocket<bool> signal;

    explicit FaultSource(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , signal("signal")
    {
        SC_METHOD(emit);
        sensitive << m_event;
        dont_initialize();
    }

    void write(bool value)
    {
        m_pending = value;
        m_event.notify(sc_core::sc_time(1, sc_core::SC_PS));
    }
};

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

uint32_t access32(zena_ssu& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(zena_ssu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(zena_ssu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

void write32_keyed(zena_ssu& dut, uint64_t offset, uint32_t value)
{
    write32(dut, SYS_KEY, SYS_KEY_VALUE);
    write32(dut, offset, value);
}

}

TEST(ZenaSsuTest, ResetValuesMatchDocumentedRegisters)
{
    zena_ssu dut("ssu_reset");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, ERR_FR), 0x2u);
    EXPECT_EQ(read32(dut, ERR_CTRL), 0x1u);
    EXPECT_EQ(read32(dut, ERR_STATUS), 0u);
    EXPECT_EQ(read32(dut, ERR_IMPDEF), 0x71u);
    EXPECT_EQ(read32(dut, SYS_STATUS), SYS_STATUS_TEST);
    EXPECT_EQ(read32(dut, PIDR4), 0x04u);
    EXPECT_EQ(read32(dut, PIDR0), 0xd1u);
    EXPECT_EQ(read32(dut, PIDR1), 0xb3u);
    EXPECT_EQ(read32(dut, PIDR2), 0x0bu);
    EXPECT_EQ(read32(dut, PIDR3), 0x00u);
    EXPECT_EQ(read32(dut, CIDR0), 0x0du);
    EXPECT_EQ(read32(dut, CIDR1), 0xf0u);
    EXPECT_EQ(read32(dut, CIDR2), 0x05u);
    EXPECT_EQ(read32(dut, CIDR3), 0xb1u);
}

TEST(ZenaSsuTest, RegistersRequireSysKeyWhenConfigured)
{
    zena_ssu dut("ssu_key");
    dut.before_end_of_elaboration();

    write32(dut, ERR_CTRL, 0);
    EXPECT_EQ(read32(dut, ERR_CTRL), 0x1u);

    write32_keyed(dut, ERR_CTRL, 0);
    EXPECT_EQ(read32(dut, ERR_CTRL), 0u);
}

TEST(ZenaSsuTest, StatusDetailPreservesLowSixteenBits)
{
    zena_ssu dut("ssu_detail");
    dut.before_end_of_elaboration();

    write32_keyed(dut, STATUS_DETAIL, 0x1234abcd);
    EXPECT_EQ(read32(dut, STATUS_DETAIL), 0xabcdu);
}

TEST(ZenaSsuTest, SysCtrlUpdatesVisibleState)
{
    zena_ssu dut("ssu_sys_ctrl");
    dut.before_end_of_elaboration();

    write32_keyed(dut, SYS_CTRL, 1);
    EXPECT_EQ(read32(dut, SYS_STATUS), SYS_STATUS_SAFE);
    EXPECT_EQ(read32(dut, SYS_CTRL), 0u);
}

TEST(ZenaSsuTest, FaultInputsSetStatusAndSafetyOutput)
{
    zena_ssu dut("ssu_fault");
    TlmBinder tlm_binder("tlm_binder");
    FaultSource critical("critical_source");
    FaultSource non_critical("non_critical_source");
    SignalSink safety("safety_sink");

    tlm_binder.socket.bind(dut.target_socket);
    critical.signal.bind(dut.critical_in);
    non_critical.signal.bind(dut.non_critical_in);
    dut.safety_status.bind(safety.signal);
    dut.before_end_of_elaboration();

    critical.write(true);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_PS));
    EXPECT_EQ(read32(dut, SYS_STATUS), SYS_STATUS_ERRC);
    EXPECT_NE(read32(dut, ERR_STATUS) & STATUS_V, 0u);
    ASSERT_FALSE(safety.observed.empty());
    EXPECT_TRUE(safety.observed.back());

    write32_keyed(dut, ERR_STATUS, STATUS_V);
    EXPECT_EQ(read32(dut, SYS_STATUS), SYS_STATUS_SAFE);
    ASSERT_FALSE(safety.observed.empty());
    EXPECT_FALSE(safety.observed.back());

    write32_keyed(dut, ERR_IMPDEF, 0x73u);
    non_critical.write(true);
    sc_core::sc_start(sc_core::sc_time(2, sc_core::SC_PS));
    EXPECT_EQ(read32(dut, SYS_STATUS), SYS_STATUS_ERRN);
    EXPECT_NE(read32(dut, ERR_STATUS) & STATUS_V, 0u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
