/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <array>
#include <cstdint>
#include <cstring>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <mmu720ae.h>
#include <mmu720ae_regs.h>
#include <mmu720ae_tlm_extensions.h>

namespace {

class TestMemory : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<TestMemory, DEFAULT_TLM_BUSWIDTH>
        target_socket;
    unsigned int accesses = 0;
    uint64_t last_address = 0;
    std::array<uint64_t, qbox::mmu720ae::EVTQ_ENT_DWORDS> last_dwords {};

    explicit TestMemory(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , target_socket("target_socket")
    {
        target_socket.register_b_transport(this, &TestMemory::b_transport);
        target_socket.register_transport_dbg(this, &TestMemory::transport_dbg);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        ++accesses;
        last_address = trans.get_address();
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND &&
            trans.get_data_length() == qbox::mmu720ae::EVTQ_ENT_BYTES) {
            std::memcpy(last_dwords.data(),
                        trans.get_data_ptr(),
                        qbox::mmu720ae::EVTQ_ENT_BYTES);
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        ++accesses;
        last_address = trans.get_address();
        return trans.get_data_length();
    }
};

class TestRequester : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<TestRequester, DEFAULT_TLM_BUSWIDTH>
        initiator_socket;

    explicit TestRequester(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , initiator_socket("initiator_socket")
    {
    }

    tlm::tlm_response_status write(uint64_t address)
    {
        tlm::tlm_generic_payload trans;
        uint32_t data = 0xa5a55a5a;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        trans.set_address(address);
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_data_length(sizeof(data));
        trans.set_streaming_width(sizeof(data));
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

        initiator_socket->b_transport(trans, delay);
        return trans.get_response_status();
    }
};

uint32_t access32(mmu720ae& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    uint32_t data = value;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint64_t access64(mmu720ae& dut, uint64_t offset, tlm::tlm_command command,
                  uint64_t value = 0)
{
    tlm::tlm_generic_payload trans;
    uint64_t data = value;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

void write32(mmu720ae& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

void write64(mmu720ae& dut, uint64_t offset, uint64_t value)
{
    (void)access64(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

uint32_t read32(mmu720ae& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

tlm::tlm_response_status tbu_write(mmu720ae& dut, uint64_t address)
{
    tlm::tlm_generic_payload trans;
    uint32_t data = 0xa5a55a5a;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(address);
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    dut.tbu_b_transport(trans, delay);
    return trans.get_response_status();
}

tlm::tlm_response_status tbu_write_with_sid(mmu720ae& dut, uint64_t address,
                                            uint32_t sid)
{
    tlm::tlm_generic_payload trans;
    qbox::mmu720ae::request_attrs_extension attrs;
    uint32_t data = 0xa5a55a5a;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    attrs.sid = sid;
    attrs.sid_valid = true;

    trans.set_address(address);
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    trans.set_extension(&attrs);

    dut.tbu_b_transport(trans, delay);
    trans.clear_extension(&attrs);
    return trans.get_response_status();
}

} // namespace

TEST(Mmu720AeTbuTest, DisabledSmmuBypassesRequesterTraffic)
{
    mmu720ae dut("mmu720ae_tbu_disabled");
    TestMemory memory("memory_disabled");
    dut.downstream_socket.bind(memory.target_socket);

    EXPECT_EQ(tbu_write(dut, 0x12345000), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(memory.accesses, 1u);
    EXPECT_EQ(memory.last_address, 0x12345000u);
}

TEST(Mmu720AeTbuTest, EnabledSmmuDoesNotSilentlyBypassWithoutWalker)
{
    mmu720ae dut("mmu720ae_tbu_enabled");
    TestMemory memory("memory_enabled");
    dut.downstream_socket.bind(memory.target_socket);

    write32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL,
            qbox::mmu720ae::IRQ_CTRL_GERROR_IRQEN);
    write32(dut,
            qbox::mmu720ae::ARM_SMMU_CR0,
            qbox::mmu720ae::CR0_CMDQEN | qbox::mmu720ae::CR0_EVTQEN |
                qbox::mmu720ae::CR0_SMMUEN);
    EXPECT_FALSE(dut.core_for_test().combined_irq_level());

    EXPECT_EQ(tbu_write(dut, 0x12345000), tlm::TLM_COMMAND_ERROR_RESPONSE);
    EXPECT_EQ(memory.accesses, 0u);
    EXPECT_NE(read32(dut, qbox::mmu720ae::ARM_SMMU_GERROR) &
                  qbox::mmu720ae::GERROR_EVTQ_ABT_ERR,
              0u);
    EXPECT_EQ(dut.core_for_test().event_queue_abort_count(), 1u);
    EXPECT_EQ(dut.core_for_test().event_record_count(), 0u);
    EXPECT_TRUE(dut.core_for_test().combined_irq_level());

    write32(dut, qbox::mmu720ae::ARM_SMMU_GERRORN,
            qbox::mmu720ae::GERROR_EVTQ_ABT_ERR);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_GERROR) &
                  qbox::mmu720ae::GERROR_EVTQ_ABT_ERR,
              0u);
    EXPECT_FALSE(dut.core_for_test().combined_irq_level());
}

TEST(Mmu720AeTbuTest, EnabledSmmuWritesTranslationFaultEventRecord)
{
    static constexpr uint64_t eventq_base = 0x100000;
    static constexpr uint64_t iova = 0x12345000;

    mmu720ae dut("mmu720ae_tbu_evtq");
    TestMemory memory("memory_evtq_downstream");
    TestMemory event_memory("memory_evtq_records");
    dut.downstream_socket.bind(memory.target_socket);
    dut.ptw_socket.bind(event_memory.target_socket);

    write64(dut,
            qbox::mmu720ae::ARM_SMMU_EVTQ_BASE,
            eventq_base | 4u);
    write32(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_PROD, 0);
    write32(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_CONS, 0);
    write32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL,
            qbox::mmu720ae::IRQ_CTRL_EVTQ_IRQEN);
    write32(dut,
            qbox::mmu720ae::ARM_SMMU_CR0,
            qbox::mmu720ae::CR0_EVTQEN | qbox::mmu720ae::CR0_SMMUEN);

    EXPECT_EQ(tbu_write(dut, iova), tlm::TLM_COMMAND_ERROR_RESPONSE);

    EXPECT_EQ(memory.accesses, 0u);
    EXPECT_EQ(event_memory.accesses, 1u);
    EXPECT_EQ(event_memory.last_address, eventq_base);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_PROD), 1u);
    EXPECT_TRUE(dut.core_for_test().combined_irq_level());
    EXPECT_EQ(event_memory.last_dwords[0] & qbox::mmu720ae::EVTQ_0_ID_MASK,
              qbox::mmu720ae::EVT_ID_TRANSLATION_FAULT);
    EXPECT_EQ(event_memory.last_dwords[0] >> qbox::mmu720ae::EVTQ_0_SID_SHIFT,
              0u);
    EXPECT_EQ(event_memory.last_dwords[1] &
                  (1ull << qbox::mmu720ae::EVTQ_1_RNW_SHIFT),
              0u);
    EXPECT_EQ(event_memory.last_dwords[2], iova);
    EXPECT_EQ(dut.core_for_test().event_record_count(), 1u);
    EXPECT_EQ(dut.core_for_test().event_queue_abort_count(), 0u);

    write32(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_CONS, 1);
    EXPECT_FALSE(dut.core_for_test().combined_irq_level());
}

TEST(Mmu720AeTbuTest, Ace2SocketUsesDefaultSidForFaultEvent)
{
    static constexpr uint64_t eventq_base = 0x200000;
    static constexpr uint64_t iova = 0x23456000;

    mmu720ae dut("mmu720ae_tbu_ace2_sid");
    TestRequester requester("requester_ace2");
    TestMemory memory("memory_ace2_downstream");
    TestMemory event_memory("memory_ace2_evtq");
    requester.initiator_socket.bind(dut.tbu_ace2_socket);
    dut.downstream_socket.bind(memory.target_socket);
    dut.ptw_socket.bind(event_memory.target_socket);

    write64(dut,
            qbox::mmu720ae::ARM_SMMU_EVTQ_BASE,
            eventq_base | 4u);
    write32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL,
            qbox::mmu720ae::IRQ_CTRL_EVTQ_IRQEN);
    write32(dut,
            qbox::mmu720ae::ARM_SMMU_CR0,
            qbox::mmu720ae::CR0_EVTQEN | qbox::mmu720ae::CR0_SMMUEN);

    EXPECT_EQ(requester.write(iova), tlm::TLM_COMMAND_ERROR_RESPONSE);

    EXPECT_EQ(memory.accesses, 0u);
    EXPECT_EQ(event_memory.accesses, 1u);
    EXPECT_EQ(event_memory.last_address, eventq_base);
    EXPECT_EQ(event_memory.last_dwords[0] >> qbox::mmu720ae::EVTQ_0_SID_SHIFT,
              0x20u);
    EXPECT_EQ(event_memory.last_dwords[2], iova);
    EXPECT_EQ(dut.core_for_test().tbu_request_count(), 1u);
    EXPECT_EQ(dut.core_for_test().tbu_fallback_sid_count(), 1u);
    EXPECT_EQ(dut.core_for_test().event_record_count(), 1u);
    EXPECT_EQ(dut.core_for_test().last_tbu_sid(), 0x20u);
}

TEST(Mmu720AeTbuTest, Ace1UsesSidExtensionForFaultEvent)
{
    static constexpr uint64_t eventq_base = 0x300000;
    static constexpr uint64_t iova = 0x34567000;
    static constexpr uint32_t sid = 0x5a;

    mmu720ae dut("mmu720ae_tbu_sid_extension");
    TestMemory memory("memory_sid_ext_downstream");
    TestMemory event_memory("memory_sid_ext_evtq");
    dut.downstream_socket.bind(memory.target_socket);
    dut.ptw_socket.bind(event_memory.target_socket);

    write64(dut,
            qbox::mmu720ae::ARM_SMMU_EVTQ_BASE,
            eventq_base | 4u);
    write32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL,
            qbox::mmu720ae::IRQ_CTRL_EVTQ_IRQEN);
    write32(dut,
            qbox::mmu720ae::ARM_SMMU_CR0,
            qbox::mmu720ae::CR0_EVTQEN | qbox::mmu720ae::CR0_SMMUEN);

    EXPECT_EQ(tbu_write_with_sid(dut, iova, sid),
              tlm::TLM_COMMAND_ERROR_RESPONSE);

    EXPECT_EQ(memory.accesses, 0u);
    EXPECT_EQ(event_memory.accesses, 1u);
    EXPECT_EQ(event_memory.last_address, eventq_base);
    EXPECT_EQ(event_memory.last_dwords[0] >> qbox::mmu720ae::EVTQ_0_SID_SHIFT,
              sid);
    EXPECT_EQ(event_memory.last_dwords[2], iova);
    EXPECT_EQ(dut.core_for_test().tbu_request_count(), 1u);
    EXPECT_EQ(dut.core_for_test().tbu_fallback_sid_count(), 0u);
    EXPECT_EQ(dut.core_for_test().event_record_count(), 1u);
    EXPECT_EQ(dut.core_for_test().last_tbu_sid(), sid);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
