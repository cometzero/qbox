/*
 * Apollo Hexagon DMA component tests.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include <libgsutils.h>

#include <apollo_hexagon_dma.h>

#include <ports/target-signal-socket.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>

class ApolloHexagonDmaMemory : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<ApolloHexagonDmaMemory, DEFAULT_TLM_BUSWIDTH> socket;

    explicit ApolloHexagonDmaMemory(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name)
        , socket("socket")
    {
        socket.register_b_transport(this, &ApolloHexagonDmaMemory::b_transport);
        m_memory.fill(0);
    }

    void write32(uint64_t addr, uint32_t value)
    {
        ASSERT_LE(addr + sizeof(value), m_memory.size());
        std::memcpy(&m_memory[addr], &value, sizeof(value));
    }

    void write_bytes(uint64_t addr, const uint8_t* data, size_t len)
    {
        ASSERT_LE(addr + len, m_memory.size());
        std::memcpy(&m_memory[addr], data, len);
    }

    void read_bytes(uint64_t addr, uint8_t* data, size_t len) const
    {
        ASSERT_LE(addr + len, m_memory.size());
        std::memcpy(data, &m_memory[addr], len);
    }

private:
    std::array<uint8_t, 4096> m_memory {};

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        const uint64_t addr = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (addr > m_memory.size() || len > m_memory.size() - addr) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            std::memcpy(data, &m_memory[addr], len);
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        case tlm::TLM_WRITE_COMMAND:
            std::memcpy(&m_memory[addr], data, len);
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        default:
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
    }
};

class ApolloHexagonDmaTestBench : public TestBench
{
protected:
    enum : uint64_t {
        REG_STATUS = 0x10,
        REG_RESULT = 0x14,
        REG_JOB_STATUS = 0x54,
        REG_JOB_RESULT = 0x58,
        REG_JOB_QUEUE = 0x5c,
        REG_JOB_FENCE = 0x60,
        REG_IRQ_STATUS = 0x64,
        REG_IRQ_ACK = 0x68,
        REG_CMDQ_BASE_LO = 0x80,
        REG_CMDQ_BASE_HI = 0x84,
        REG_CMDQ_SIZE = 0x88,
        REG_CMDQ_HEAD = 0x8c,
        REG_CMDQ_TAIL = 0x90,
        REG_CMDQ_DOORBELL = 0x94,
        REG_CMDQ_STATUS = 0x98,
        REG_CMDQ_FENCE_VALUE = 0x9c,
        REG_CMDQ_FAULT_CODE = 0xa0,
        REG_CMDQ_FAULT_ADDR_LO = 0xa4,
        REG_CMDQ_FAULT_ADDR_HI = 0xa8,
    };

    enum : uint32_t {
        STATUS_IDLE = 0,
        JOB_STATUS_DONE = 1,
        JOB_STATUS_ERROR = 2,
        JOB_RESULT_VADD_OK = 0x56414444,
        QUEUE_DMA = 0,
        QUEUE_CNN = 1,
        CMDQ_FAULT_NONE = 0,
        CMDQ_FAULT_EMPTY = 1,
        CMDQ_FAULT_UNSUPPORTED_PACKET = 3,
        CMDQ_FAULT_MALFORMED_PACKET = 4,
        CMDQ_FAULT_DMA_ERROR = 5,
        CMDQ_PACKET_BYTES = 32,
        CMDQ_OPCODE_NOP = 0,
        CMDQ_OPCODE_COPY = 1,
        CMDQ_OPCODE_BARRIER = 2,
        CMDQ_OPCODE_SIGNAL_FENCE = 3,
        CMDQ_OPCODE_DISPATCH = 4,
        CMDQ_OPCODE_LOAD_EXECUTABLE = 5,
        CMDQ_DISPATCH_EXEC_SLOT_FLAG = 1u << 31,
        APKO_MAGIC = 0x4f4b5041,
        APKO_ABI_VERSION = 0,
        EXEC_FORMAT_APKO_V0 = 1,
        CMDQ_DISPATCH_KIND_CNN = 1,
        CMDQ_DISPATCH_KIND_VADD = 2,
        CMDQ_DISPATCH_KIND_MNIST = 3,
        JOB_RESULT_CNN_OK = 0x434e4e53,   // "CNNS"
        JOB_RESULT_MNIST_OK = 0x4d4e4953, // "MNIS"
    };

    apollo_hexagon_dma m_dma;
    InitiatorTester m_regs;
    ApolloHexagonDmaMemory m_memory;
    TargetSignalSocket<bool> m_irq_line;

    void write32(uint64_t addr, uint32_t value)
    {
        const auto status = m_regs.do_write_with_ptr(
            addr, reinterpret_cast<const uint8_t*>(&value), sizeof(value));

        ASSERT_EQ(tlm::TLM_OK_RESPONSE, status);
    }

    uint32_t read32(uint64_t addr)
    {
        uint32_t value = 0;
        const auto status = m_regs.do_read_with_ptr(
            addr, reinterpret_cast<uint8_t*>(&value), sizeof(value));

        EXPECT_EQ(tlm::TLM_OK_RESPONSE, status);
        return value;
    }

    void write_packet(uint64_t addr, const std::array<uint32_t, 8>& packet)
    {
        for (size_t i = 0; i < packet.size(); i++) {
            m_memory.write32(addr + i * sizeof(uint32_t), packet[i]);
        }
    }

public:
    explicit ApolloHexagonDmaTestBench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , m_dma("dma")
        , m_regs("regs")
        , m_memory("memory")
        , m_irq_line("irq_line")
    {
        m_regs.socket.bind(m_dma.regs);
        m_dma.dma.bind(m_memory.socket);
        m_dma.irq_out.bind(m_irq_line);
    }
};

TEST_BENCH(ApolloHexagonDmaTestBench, AsyncFenceDrivesIrqSignalUntilAck)
{
    EXPECT_FALSE(m_irq_line.read());
    EXPECT_EQ(0u, read32(REG_IRQ_STATUS));

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_JOB_STATUS, JOB_STATUS_DONE);

    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(1u, read32(REG_JOB_FENCE));

    write32(REG_JOB_QUEUE, QUEUE_CNN);
    write32(REG_JOB_STATUS, JOB_STATUS_DONE);

    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ((1u << QUEUE_DMA) | (1u << QUEUE_CNN), read32(REG_IRQ_STATUS));
    EXPECT_EQ(2u, read32(REG_JOB_FENCE));

    write32(REG_IRQ_ACK, 1u << QUEUE_DMA);
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_CNN, read32(REG_IRQ_STATUS));

    write32(REG_IRQ_ACK, 1u << QUEUE_CNN);
    EXPECT_FALSE(m_irq_line.read());
    EXPECT_EQ(0u, read32(REG_IRQ_STATUS));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDoorbellReportsEmptyFaultAndIrq)
{
    constexpr uint64_t cmdq_base = 0x100001000ull;

    EXPECT_FALSE(m_irq_line.read());

    write32(REG_JOB_QUEUE, QUEUE_CNN);
    write32(REG_CMDQ_BASE_LO, static_cast<uint32_t>(cmdq_base));
    write32(REG_CMDQ_BASE_HI, static_cast<uint32_t>(cmdq_base >> 32));
    write32(REG_CMDQ_SIZE, 0x1000);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, 0);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_EMPTY, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(static_cast<uint32_t>(cmdq_base), read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(static_cast<uint32_t>(cmdq_base >> 32), read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_CNN, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));

    write32(REG_IRQ_ACK, 1u << QUEUE_CNN);
    EXPECT_FALSE(m_irq_line.read());
    EXPECT_EQ(0u, read32(REG_IRQ_STATUS));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDoorbellReportsUnsupportedPacketFault)
{
    constexpr uint32_t cmdq_base = 0x200;
    constexpr uint32_t cmdq_head = 0x20;

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x100);
    write32(REG_CMDQ_HEAD, cmdq_head);
    write32(REG_CMDQ_TAIL, cmdq_head + CMDQ_PACKET_BYTES);
    write_packet(cmdq_base + cmdq_head, { 0xdeadbeefu, 0, 0, 0, 0, 0, 0, 0 });
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_UNSUPPORTED_PACKET, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(cmdq_base + cmdq_head, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDoorbellRejectsMalformedGeometry)
{
    constexpr uint32_t cmdq_base = 0x200;
    constexpr uint32_t bad_head = 0x120;

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x100);
    write32(REG_CMDQ_HEAD, bad_head);
    write32(REG_CMDQ_TAIL, bad_head);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_MALFORMED_PACKET, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(cmdq_base + bad_head, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueCopyReportsDmaFault)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t bad_src = 0xf00;
    constexpr uint32_t dst = 0x340;
    constexpr uint32_t bytes = 0x200;

    write_packet(cmdq_base, { CMDQ_OPCODE_COPY, 0, bad_src, 0, dst, 0, bytes, 0 });
    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_DMA_ERROR, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(bad_src, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_EQ(0u, read32(REG_CMDQ_HEAD));
    EXPECT_EQ(STATUS_IDLE, read32(REG_STATUS));
    EXPECT_EQ(0u, read32(REG_RESULT));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDispatchesVadd)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_addr = 0x300;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    const std::array<uint32_t, 8> input { 1, 2, 3, 4, 10, 20, 30, 40 };
    const std::array<uint32_t, 4> expected {
        0x41300000, 0x41b00000, 0x42040000, 0x42300000,
    };
    std::array<uint32_t, 4> output {};

    m_memory.write_bytes(input_addr, reinterpret_cast<const uint8_t*>(input.data()),
                         input.size() * sizeof(uint32_t));
    write_packet(cmdq_base,
                 { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_KIND_VADD,
                   input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(output_addr, reinterpret_cast<uint8_t*>(output.data()),
                        output.size() * sizeof(uint32_t));
    EXPECT_EQ(expected, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(CMDQ_PACKET_BYTES, read32(REG_CMDQ_HEAD));
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_JOB_STATUS));
    EXPECT_EQ(JOB_RESULT_VADD_OK, read32(REG_JOB_RESULT));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDispatchesCnn)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_addr = 0x300;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    const std::array<uint8_t, input_bytes> input {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c,
        0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c,
        0x1d, 0x1e, 0x1f, 0x20,
    };
    const std::array<uint8_t, output_bytes> expected {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c,
        0x0d, 0x0e, 0x0f, 0x10,
    };
    std::array<uint8_t, output_bytes> output {};

    m_memory.write_bytes(input_addr, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    write_packet(cmdq_base, { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_KIND_CNN,
                              input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(output_addr, reinterpret_cast<uint8_t*>(output.data()),
                        output.size() * sizeof(uint8_t));
    EXPECT_EQ(expected, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(JOB_RESULT_CNN_OK, read32(REG_JOB_RESULT));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDispatchesMnistLikeTransform)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_addr = 0x300;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 4 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    const std::array<uint8_t, input_bytes> input {
        0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb,
        0xcc, 0xdd, 0xee, 0xff,
    };
    const std::array<uint8_t, output_bytes> expected = {
        0xff, 0xee, 0xdd, 0xcc,
        0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44,
        0x33, 0x22, 0x11, 0x00,
    };
    std::array<uint8_t, output_bytes> output {};

    m_memory.write_bytes(input_addr, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    write_packet(cmdq_base, { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_KIND_MNIST,
                              input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(output_addr, reinterpret_cast<uint8_t*>(output.data()),
                        output.size() * sizeof(uint8_t));
    EXPECT_EQ(expected, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(JOB_RESULT_MNIST_OK, read32(REG_JOB_RESULT));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueLoadExecutableDispatchesVaddSlot)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_addr = 0x300;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    constexpr uint32_t slot = 1;
    const std::array<uint32_t, 8> input { 1, 2, 3, 4, 10, 20, 30, 40 };
    const std::array<uint32_t, 4> expected {
        0x41300000, 0x41b00000, 0x42040000, 0x42300000,
    };
    std::array<uint32_t, 4> output {};

    m_memory.write_bytes(input_addr, reinterpret_cast<const uint8_t*>(input.data()),
                         input.size() * sizeof(uint32_t));
    write_packet(cmdq_base,
                 { CMDQ_OPCODE_LOAD_EXECUTABLE, slot, APKO_MAGIC, APKO_ABI_VERSION,
                   EXEC_FORMAT_APKO_V0, CMDQ_DISPATCH_KIND_VADD, input_bytes, output_bytes });
    write_packet(cmdq_base + CMDQ_PACKET_BYTES,
                 { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_EXEC_SLOT_FLAG | slot,
                   input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, 2 * CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(output_addr, reinterpret_cast<uint8_t*>(output.data()),
                        output.size() * sizeof(uint32_t));
    EXPECT_EQ(expected, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(2u * CMDQ_PACKET_BYTES, read32(REG_CMDQ_HEAD));
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_JOB_STATUS));
    EXPECT_EQ(JOB_RESULT_VADD_OK, read32(REG_JOB_RESULT));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueLoadExecutableDispatchesCnnSlot)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_addr = 0x300;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 16 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    constexpr uint32_t slot = 1;
    const std::array<uint32_t, 16> input {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    };
    const std::array<uint32_t, 4> expected {
        0x42580000, 0x427c0000, 0x42b40000, 0x42c60000,
    };
    std::array<uint32_t, 4> output {};

    m_memory.write_bytes(input_addr, reinterpret_cast<const uint8_t*>(input.data()),
                         input.size() * sizeof(uint32_t));
    write_packet(cmdq_base,
                 { CMDQ_OPCODE_LOAD_EXECUTABLE, slot, APKO_MAGIC, APKO_ABI_VERSION,
                   EXEC_FORMAT_APKO_V0, CMDQ_DISPATCH_KIND_CNN, input_bytes, output_bytes });
    write_packet(cmdq_base + CMDQ_PACKET_BYTES,
                 { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_EXEC_SLOT_FLAG | slot,
                   input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, 2 * CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(output_addr, reinterpret_cast<uint8_t*>(output.data()),
                        output.size() * sizeof(uint32_t));
    EXPECT_EQ(expected, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(2u * CMDQ_PACKET_BYTES, read32(REG_CMDQ_HEAD));
    EXPECT_EQ(JOB_RESULT_CNN_OK, read32(REG_JOB_RESULT));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueLoadExecutableRejectsBadAbi)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);

    write_packet(cmdq_base,
                 { CMDQ_OPCODE_LOAD_EXECUTABLE, 1, APKO_MAGIC, APKO_ABI_VERSION + 1,
                   EXEC_FORMAT_APKO_V0, CMDQ_DISPATCH_KIND_VADD, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_MALFORMED_PACKET, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(cmdq_base, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_EQ(0u, read32(REG_CMDQ_HEAD));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueLoadExecutableRejectsBadDispatchKind)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);
    constexpr uint32_t bad_kind = 0x7u;

    write_packet(cmdq_base,
                 { CMDQ_OPCODE_LOAD_EXECUTABLE, 1, APKO_MAGIC, APKO_ABI_VERSION,
                   EXEC_FORMAT_APKO_V0, bad_kind, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_MALFORMED_PACKET, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(cmdq_base, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_EQ(0u, read32(REG_CMDQ_HEAD));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueDispatchReportsDmaFault)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t bad_input_addr = 0xff0;
    constexpr uint32_t output_addr = 0x380;
    constexpr uint32_t input_bytes = 8 * sizeof(uint32_t);
    constexpr uint32_t output_bytes = 4 * sizeof(uint32_t);

    write_packet(cmdq_base,
                 { CMDQ_OPCODE_DISPATCH, CMDQ_DISPATCH_KIND_VADD,
                   bad_input_addr, 0, output_addr, 0, input_bytes, output_bytes });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    EXPECT_EQ(JOB_STATUS_ERROR, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_DMA_ERROR, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(bad_input_addr, read32(REG_CMDQ_FAULT_ADDR_LO));
    EXPECT_EQ(0u, read32(REG_CMDQ_FAULT_ADDR_HI));
    EXPECT_EQ(0u, read32(REG_CMDQ_HEAD));
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

TEST_BENCH(ApolloHexagonDmaTestBench, CommandQueueExecutesCopyBarrierAndSignalFence)
{
    constexpr uint32_t cmdq_base = 0x100;
    constexpr uint32_t src = 0x300;
    constexpr uint32_t dst = 0x340;
    constexpr uint32_t bytes = 16;
    const std::array<uint8_t, bytes> input {
        0x10, 0x11, 0x12, 0x13,
        0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33,
        0x40, 0x41, 0x42, 0x43,
    };
    std::array<uint8_t, bytes> output {};

    m_memory.write_bytes(src, input.data(), input.size());
    write_packet(cmdq_base, { CMDQ_OPCODE_NOP, 0, 0, 0, 0, 0, 0, 0 });
    write_packet(cmdq_base + CMDQ_PACKET_BYTES,
                 { CMDQ_OPCODE_COPY, 0, src, 0, dst, 0, bytes, 0 });
    write_packet(cmdq_base + 2 * CMDQ_PACKET_BYTES,
                 { CMDQ_OPCODE_BARRIER, 0, 0, 0, 0, 0, 0, 0 });
    write_packet(cmdq_base + 3 * CMDQ_PACKET_BYTES,
                 { CMDQ_OPCODE_SIGNAL_FENCE, 0, 0, 0, 0, 0, 0, 0 });

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_CMDQ_BASE_LO, cmdq_base);
    write32(REG_CMDQ_SIZE, 0x200);
    write32(REG_CMDQ_HEAD, 0);
    write32(REG_CMDQ_TAIL, 4 * CMDQ_PACKET_BYTES);
    write32(REG_CMDQ_DOORBELL, 1);

    m_memory.read_bytes(dst, output.data(), output.size());
    EXPECT_EQ(input, output);
    EXPECT_EQ(JOB_STATUS_DONE, read32(REG_CMDQ_STATUS));
    EXPECT_EQ(CMDQ_FAULT_NONE, read32(REG_CMDQ_FAULT_CODE));
    EXPECT_EQ(4u * CMDQ_PACKET_BYTES, read32(REG_CMDQ_HEAD));
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(read32(REG_JOB_FENCE), read32(REG_CMDQ_FENCE_VALUE));
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker {};

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
