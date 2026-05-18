/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <scp/report.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class smmuv3_stub : public sc_core::sc_module
{
    SCP_LOGGER();

    static constexpr uint64_t PAGE1_OFFSET = 0x10000;
    static constexpr uint64_t REG_SPACE_SIZE = PAGE1_OFFSET + 0x1000;

    static constexpr uint64_t ARM_SMMU_IDR0 = 0x0000;
    static constexpr uint64_t ARM_SMMU_IDR1 = 0x0004;
    static constexpr uint64_t ARM_SMMU_IDR3 = 0x000c;
    static constexpr uint64_t ARM_SMMU_IDR5 = 0x0014;
    static constexpr uint64_t ARM_SMMU_IIDR = 0x0018;
    static constexpr uint64_t ARM_SMMU_AIDR = 0x001c;
    static constexpr uint64_t ARM_SMMU_CR0 = 0x0020;
    static constexpr uint64_t ARM_SMMU_CR0ACK = 0x0024;
    static constexpr uint64_t ARM_SMMU_GBPA = 0x0044;
    static constexpr uint64_t ARM_SMMU_IRQ_CTRL = 0x0050;
    static constexpr uint64_t ARM_SMMU_IRQ_CTRLACK = 0x0054;
    static constexpr uint64_t ARM_SMMU_CMDQ_PROD = 0x0098;
    static constexpr uint64_t ARM_SMMU_CMDQ_CONS = 0x009c;
    static constexpr uint64_t ARM_SMMU_EVTQ_PROD = PAGE1_OFFSET + 0x00a8;
    static constexpr uint64_t ARM_SMMU_EVTQ_CONS = PAGE1_OFFSET + 0x00ac;

    static constexpr uint32_t GBPA_UPDATE = 1u << 31;

    std::array<uint8_t, REG_SPACE_SIZE> m_regs {};

    template <typename T>
    void store(uint64_t offset, T value)
    {
        if (offset + sizeof(T) > m_regs.size()) {
            return;
        }
        std::memcpy(&m_regs[offset], &value, sizeof(T));
    }

    template <typename T>
    T load(uint64_t offset) const
    {
        T value {};
        if (offset + sizeof(T) <= m_regs.size()) {
            std::memcpy(&value, &m_regs[offset], sizeof(T));
        }
        return value;
    }

    uint32_t read32(uint64_t offset) const
    {
        return load<uint32_t>(offset);
    }

    void write32(uint64_t offset, uint32_t value)
    {
        if (offset == ARM_SMMU_GBPA) {
            value &= ~GBPA_UPDATE;
        }

        store<uint32_t>(offset, value);

        switch (offset) {
        case ARM_SMMU_CR0:
            store<uint32_t>(ARM_SMMU_CR0ACK, value);
            break;
        case ARM_SMMU_IRQ_CTRL:
            store<uint32_t>(ARM_SMMU_IRQ_CTRLACK, value);
            break;
        case ARM_SMMU_CMDQ_PROD:
            store<uint32_t>(ARM_SMMU_CMDQ_CONS, value);
            break;
        case ARM_SMMU_EVTQ_PROD:
            store<uint32_t>(ARM_SMMU_EVTQ_CONS, value);
            break;
        default:
            break;
        }
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint64_t offset = trans.get_address();
        unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        trans.set_dmi_allowed(false);
        if (offset + len > m_regs.size() || (len != 1 && len != 2 && len != 4 && len != 8)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_regs[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == 4) {
                uint32_t value;
                std::memcpy(&value, data, sizeof(value));
                write32(offset, value);
            } else {
                std::memcpy(&m_regs[offset], data, len);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

public:
    tlm_utils::simple_target_socket<smmuv3_stub, DEFAULT_TLM_BUSWIDTH> target_socket;
    InitiatorSignalSocket<bool> irq;

    explicit smmuv3_stub(sc_core::sc_module_name name)
        : sc_core::sc_module(name), target_socket("target_socket"), irq("irq")
    {
        target_socket.register_b_transport(this, &smmuv3_stub::b_transport);

        store<uint32_t>(ARM_SMMU_IDR0,
                        (1u << 1) |        /* Stage-1 translation */
                            (2u << 2) |    /* AArch64 table format */
                            (1u << 4) |    /* Coherent access */
                            (1u << 12) |   /* 16-bit ASID */
                            (2u << 21));   /* Little-endian tables */
        store<uint32_t>(ARM_SMMU_IDR1,
                        (8u << 21) |       /* 256-entry command queue */
                            (6u << 16) |   /* 64-entry event queue */
                            8u);           /* 8-bit StreamID */
        store<uint32_t>(ARM_SMMU_IDR3, 0);
        store<uint32_t>(ARM_SMMU_IDR5, (1u << 4) | (1u << 5) | (1u << 6) | 5u);
        store<uint32_t>(ARM_SMMU_IIDR, 0);
        store<uint32_t>(ARM_SMMU_AIDR, 1);
    }
};

extern "C" void module_register();
