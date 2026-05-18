/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <scp/report.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class mhuv3_stub : public sc_core::sc_module
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

    static constexpr uint64_t SCMI_CHAN_STATUS = 0x04;
    static constexpr uint64_t SCMI_LENGTH = 0x14;
    static constexpr uint64_t SCMI_HEADER = 0x18;
    static constexpr uint64_t SCMI_PAYLOAD = 0x1c;
    static constexpr uint32_t SCMI_CHAN_FREE = 1u;
    static constexpr uint32_t SCMI_SUCCESS = 0;
    static constexpr uint32_t SCMI_ERR_SUPPORT = static_cast<uint32_t>(-1);
    static constexpr uint8_t SCMI_PROTOCOL_BASE = 0x10;

    static inline mhuv3_stub* s_mbx = nullptr;

    cci::cci_param<std::string> p_frame;
    cci::cci_param<uint64_t> p_tx_shmem;
    cci::cci_param<uint64_t> p_rx_shmem;

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

    template <typename T>
    T load(uint64_t offset) const
    {
        T value {};
        if (offset + sizeof(T) <= m_regs.size()) {
            std::memcpy(&value, &m_regs[offset], sizeof(T));
        }
        return value;
    }

    bool mem_access(tlm::tlm_command command, uint64_t address, void* data, unsigned int len)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(static_cast<unsigned char*>(data));
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    uint32_t mem_read32(uint64_t address)
    {
        uint32_t value = 0;
        mem_access(tlm::TLM_READ_COMMAND, address, &value, sizeof(value));
        return value;
    }

    void mem_write(uint64_t address, const void* data, unsigned int len)
    {
        std::vector<uint8_t> tmp(len);
        std::memcpy(tmp.data(), data, len);
        mem_access(tlm::TLM_WRITE_COMMAND, address, tmp.data(), len);
    }

    void mem_write32(uint64_t address, uint32_t value)
    {
        mem_write(address, &value, sizeof(value));
    }

    static uint8_t msg_id(uint32_t header) { return header & 0xffu; }
    static uint8_t protocol_id(uint32_t header) { return (header >> 10) & 0xffu; }

    void write_scmi_response(uint32_t header, uint32_t status, const std::vector<uint8_t>& payload)
    {
        const uint64_t shmem = p_tx_shmem.get_value();
        const uint32_t length = 8 + payload.size();

        mem_write32(shmem + SCMI_LENGTH, length);
        mem_write32(shmem + SCMI_HEADER, header);
        mem_write32(shmem + SCMI_PAYLOAD, status);
        if (!payload.empty()) {
            mem_write(shmem + SCMI_PAYLOAD + sizeof(status), payload.data(), payload.size());
        }
        mem_write32(shmem + SCMI_CHAN_STATUS, SCMI_CHAN_FREE);
    }

    static void append_u32(std::vector<uint8_t>& out, uint32_t value)
    {
        out.push_back(value & 0xffu);
        out.push_back((value >> 8) & 0xffu);
        out.push_back((value >> 16) & 0xffu);
        out.push_back((value >> 24) & 0xffu);
    }

    static std::vector<uint8_t> fixed_string(const char* value)
    {
        std::vector<uint8_t> out(16, 0);
        std::strncpy(reinterpret_cast<char*>(out.data()), value, out.size() - 1);
        return out;
    }

    void respond_scmi()
    {
        const uint64_t shmem = p_tx_shmem.get_value();
        const uint32_t header = mem_read32(shmem + SCMI_HEADER);
        std::vector<uint8_t> payload;
        uint32_t status = SCMI_SUCCESS;

        if (protocol_id(header) != SCMI_PROTOCOL_BASE) {
            status = SCMI_ERR_SUPPORT;
        } else {
            switch (msg_id(header)) {
            case 0x0:
                append_u32(payload, 0x00020001);
                break;
            case 0x1:
                payload = {1, 1, 0, 0};
                break;
            case 0x2:
                append_u32(payload, 0);
                break;
            case 0x3:
                payload = fixed_string("QBox");
                break;
            case 0x4:
                payload = fixed_string("RD-Aspen");
                break;
            case 0x5:
                append_u32(payload, 1);
                break;
            case 0x6:
                append_u32(payload, 1);
                payload.push_back(SCMI_PROTOCOL_BASE);
                payload.insert(payload.end(), 3, 0);
                break;
            case 0x7:
                append_u32(payload, 0);
                {
                    auto name = fixed_string("platform");
                    payload.insert(payload.end(), name.begin(), name.end());
                }
                break;
            case 0x8:
                break;
            default:
                status = SCMI_ERR_SUPPORT;
                break;
            }
        }

        write_scmi_response(header, status, payload);
        if (s_mbx) {
            s_mbx->signal_doorbell(0);
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
            respond_scmi();
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

    void start_of_simulation() override
    {
        if (!is_mbx()) {
            mem_write32(p_tx_shmem.get_value() + SCMI_CHAN_STATUS, SCMI_CHAN_FREE);
            mem_write32(p_rx_shmem.get_value() + SCMI_CHAN_STATUS, SCMI_CHAN_FREE);
        }
    }

public:
    tlm_utils::simple_target_socket<mhuv3_stub, DEFAULT_TLM_BUSWIDTH> target_socket;
    tlm_utils::simple_initiator_socket<mhuv3_stub, DEFAULT_TLM_BUSWIDTH> initiator_socket;
    InitiatorSignalSocket<bool> irq;

    explicit mhuv3_stub(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_frame("frame", std::string("pbx"))
        , p_tx_shmem("tx_shmem", 0x00180000)
        , p_rx_shmem("rx_shmem", 0x00180100)
        , target_socket("target_socket")
        , initiator_socket("initiator_socket")
        , irq("irq")
    {
        target_socket.register_b_transport(this, &mhuv3_stub::b_transport);
    }
};

extern "C" void module_register();
