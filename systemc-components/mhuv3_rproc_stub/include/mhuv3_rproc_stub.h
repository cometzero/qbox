/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <scp/report.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class mhuv3_rproc_stub : public sc_core::sc_module
{
    SCP_LOGGER();

    static constexpr uint64_t REG_SPACE_SIZE = 0x30000;
    static constexpr uint64_t CTRL_BLK_ID = 0x000;
    static constexpr uint64_t CTRL_FEAT_SPT0 = 0x010;
    static constexpr uint64_t CTRL_FEAT_SPT1 = 0x014;
    static constexpr uint64_t CTRL_DBCH_CFG0 = 0x020;
    static constexpr uint64_t CTRL_DBCH_INT_ST0 = 0x400;
    static constexpr uint64_t CTRL_IIDR = 0xfc8;
    static constexpr uint64_t CTRL_AIDR = 0xfcc;

    static constexpr uint64_t DBCW0 = 0x1000;
    static constexpr uint64_t PBX_DBCW_ST = DBCW0 + 0x00;
    static constexpr uint64_t PBX_DBCW_SET = DBCW0 + 0x0c;
    static constexpr uint64_t PBX_DBCW_INT_ST = DBCW0 + 0x10;
    static constexpr uint64_t PBX_DBCW_INT_CLR = DBCW0 + 0x14;
    static constexpr uint64_t PBX_DBCW_INT_EN = DBCW0 + 0x18;
    static constexpr uint64_t PBX_DBCW_CTRL = DBCW0 + 0x1c;

    static constexpr uint64_t MBX_DBCW_ST = DBCW0 + 0x00;
    static constexpr uint64_t MBX_DBCW_ST_MSK = DBCW0 + 0x04;
    static constexpr uint64_t MBX_DBCW_CLR = DBCW0 + 0x08;
    static constexpr uint64_t MBX_DBCW_MSK_ST = DBCW0 + 0x10;
    static constexpr uint64_t MBX_DBCW_MSK_SET = DBCW0 + 0x14;
    static constexpr uint64_t MBX_DBCW_MSK_CLR = DBCW0 + 0x18;
    static constexpr uint64_t MBX_DBCW_CTRL = DBCW0 + 0x1c;

    static inline mhuv3_rproc_stub* s_mbx = nullptr;

    cci::cci_param<std::string> p_frame;
    cci::cci_param<unsigned int> p_ack_bit;

    std::array<uint8_t, REG_SPACE_SIZE> m_regs {};
    uint32_t m_mbx_status = 0;
    uint32_t m_mbx_mask = 0xffffffffu;

    bool is_mbx() const { return p_frame.get_value() == "mbx"; }

    template <typename T>
    void store(uint64_t offset, T value)
    {
        if (offset + sizeof(T) <= m_regs.size()) {
            std::memcpy(&m_regs[offset], &value, sizeof(T));
        }
    }

    void signal_doorbell(unsigned int bit)
    {
        m_mbx_status |= (1u << bit);
        store<uint32_t>(MBX_DBCW_ST, m_mbx_status);
        store<uint32_t>(MBX_DBCW_ST_MSK, m_mbx_status & ~m_mbx_mask);
        store<uint32_t>(MBX_DBCW_MSK_ST, m_mbx_mask);
        store<uint32_t>(CTRL_DBCH_INT_ST0, (m_mbx_status & ~m_mbx_mask) ? 1 : 0);
        if (m_mbx_status & ~m_mbx_mask) {
            irq->write(true);
        }
    }

    void clear_mbx_doorbell(uint32_t mask)
    {
        m_mbx_status &= ~mask;
        store<uint32_t>(MBX_DBCW_ST, m_mbx_status);
        store<uint32_t>(MBX_DBCW_ST_MSK, m_mbx_status & ~m_mbx_mask);
        store<uint32_t>(CTRL_DBCH_INT_ST0, (m_mbx_status & ~m_mbx_mask) ? 1 : 0);
        if (!(m_mbx_status & ~m_mbx_mask)) {
            irq->write(false);
        }
    }

    void write32(uint64_t offset, uint32_t value)
    {
        if (!is_mbx() && offset == PBX_DBCW_SET) {
            store<uint32_t>(PBX_DBCW_ST, value);
            if (s_mbx) {
                s_mbx->signal_doorbell(p_ack_bit.get_value());
            }
            return;
        }

        if (!is_mbx() && offset == PBX_DBCW_INT_CLR) {
            store<uint32_t>(PBX_DBCW_INT_ST, 0);
            return;
        }

        if (is_mbx() && offset == MBX_DBCW_CLR) {
            clear_mbx_doorbell(value);
            return;
        }

        if (is_mbx() && offset == MBX_DBCW_MSK_SET) {
            m_mbx_mask |= value;
            store<uint32_t>(MBX_DBCW_MSK_ST, m_mbx_mask);
            store<uint32_t>(MBX_DBCW_ST_MSK, m_mbx_status & ~m_mbx_mask);
            return;
        }

        if (is_mbx() && offset == MBX_DBCW_MSK_CLR) {
            m_mbx_mask &= ~value;
            store<uint32_t>(MBX_DBCW_MSK_ST, m_mbx_mask);
            store<uint32_t>(MBX_DBCW_ST_MSK, m_mbx_status & ~m_mbx_mask);
            return;
        }

        store<uint32_t>(offset, value);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
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

protected:
    void end_of_elaboration() override
    {
        const bool mbx = is_mbx();

        store<uint32_t>(CTRL_BLK_ID, mbx ? 1 : 0);
        store<uint32_t>(CTRL_FEAT_SPT0, 1);
        store<uint32_t>(CTRL_FEAT_SPT1, 0);
        store<uint32_t>(CTRL_DBCH_CFG0, 0);
        store<uint32_t>(CTRL_IIDR, 0x0000043b);
        store<uint32_t>(CTRL_AIDR, 0x20);
        if (mbx) {
            m_mbx_mask = 0xffffffffu;
            store<uint32_t>(MBX_DBCW_MSK_ST, m_mbx_mask);
            s_mbx = this;
        }
    }

public:
    tlm_utils::simple_target_socket<mhuv3_rproc_stub, DEFAULT_TLM_BUSWIDTH> target_socket;
    tlm_utils::simple_initiator_socket<mhuv3_rproc_stub, DEFAULT_TLM_BUSWIDTH> initiator_socket;
    InitiatorSignalSocket<bool> irq;

    explicit mhuv3_rproc_stub(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_frame("frame", std::string("pbx"))
        , p_ack_bit("ack_bit", 2)
        , target_socket("target_socket")
        , initiator_socket("initiator_socket")
        , irq("irq")
    {
        target_socket.register_b_transport(this, &mhuv3_rproc_stub::b_transport);
    }
};

extern "C" void module_register();
