/* SPDX-License-Identifier: BSD-3-Clause */

#include <systemc.h>
#include <cci_configuration>
#include <libgsutils.h>
#include <string>
#include <uart-pl011.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>

class CaptureBackend : public sc_core::sc_module
{
public:
    gs::biflow_socket<CaptureBackend> socket;
    std::string output;
    explicit CaptureBackend(sc_core::sc_module_name name)
        : sc_core::sc_module(name), socket("socket")
    {
        socket.register_b_transport(this, &CaptureBackend::receive);
    }
    void start_of_simulation() override { socket.can_receive_any(); }
    void receive(tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        output.append(reinterpret_cast<char*>(txn.get_data_ptr()), txn.get_data_length());
    }
};

class Pl011ResetBench : public TestBench
{
public:
    Pl011 uart;
    CaptureBackend backend;
    InitiatorTester initiator;
    TargetSignalSocket<bool> irq;
    InitiatorSignalSocket<bool> reset;
    bool irq_level = false;

    Pl011ResetBench(const sc_core::sc_module_name& n)
        : TestBench(n), uart("uart"), backend("backend"),
          initiator("initiator"), irq("irq"), reset("reset")
    {
        uart.backend_socket.bind(backend.socket);
        initiator.socket.bind(uart.socket);
        uart.irq.bind(irq);
        reset.bind(uart.reset);
        irq.register_value_changed_cb([this](bool value) { irq_level = value; });
    }

    void write(uint64_t offset, uint32_t value)
    {
        EXPECT_EQ(initiator.do_write(offset, value), tlm::TLM_OK_RESPONSE);
    }

    uint32_t read(uint64_t offset)
    {
        uint32_t value = 0;
        EXPECT_EQ(initiator.do_read(offset, value), tlm::TLM_OK_RESPONSE);
        return value;
    }

    void settle() { sc_core::wait(1, sc_core::SC_NS); }
};

TEST_BENCH(Pl011ResetBench, Pl011Reset)
{
    for (unsigned int run = 0; run < 2; ++run) {
        // TX IRQ is level-sensitive even when its last mask bit is removed.
        write(0x38, INT_TX);
        write(0, 'A');
        settle();
        ASSERT_TRUE(irq_level);
        write(0x38, 0);
        settle();
        EXPECT_FALSE(irq_level);
        EXPECT_EQ(read(0x40), 0u);
        EXPECT_EQ(read(0x3c), static_cast<uint32_t>(INT_TX));
        write(0x38, INT_TX);
        settle();
        EXPECT_TRUE(irq_level);

        write(0x24, 42);
        write(0x28, 7);
        write(0x2c, 0x70);
        write(0x48, 3);
        uart.pl011_put_fifo('R');
        // Leave an IRQ update queued when reset arrives.
        write(0, 'B');
        reset->write(true);
        EXPECT_FALSE(irq_level);
        settle();
        EXPECT_FALSE(irq_level);
        EXPECT_EQ(read(0x18), 0x90u);
        EXPECT_EQ(read(0x30), 0x300u);
        EXPECT_EQ(read(0x34), 0x12u);
        for (uint64_t offset : {0x24u, 0x28u, 0x2cu, 0x38u, 0x3cu, 0x40u, 0x48u})
            EXPECT_EQ(read(offset), 0u);
        EXPECT_EQ(read(0xfe8), 0x34u); // Configured revision survives reset.
        EXPECT_EQ(uart.s->read_count, 0);
        EXPECT_EQ(uart.s->read_pos, 0);
        write(0x38, INT_TX);
        write(0, 'X');
        settle();
        EXPECT_EQ(read(0x38), 0u);
        EXPECT_FALSE(irq_level);

        reset->write(false);
        settle();
        EXPECT_FALSE(irq_level);
        EXPECT_EQ(uart.s->read_count, 0); // Pre-reset accepted RX cannot return.
        write(0x38, INT_TX);
        write(0, 'C');
        settle();
        EXPECT_TRUE(irq_level);
        write(0x44, INT_TX);
        settle();
        EXPECT_FALSE(irq_level);
        EXPECT_EQ(backend.output, run == 0 ? "ABC" : "ABCABC");
        write(0x38, INT_RX);
        uart.pl011_put_fifo('D');
        settle();
        EXPECT_TRUE(irq_level);
        EXPECT_EQ(read(0), static_cast<uint32_t>('D'));
        settle();
        EXPECT_FALSE(irq_level);
    }
    sc_core::sc_stop();
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker({
        {"Pl011Reset.uart.revision", cci::cci_value(3)},
    });
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
