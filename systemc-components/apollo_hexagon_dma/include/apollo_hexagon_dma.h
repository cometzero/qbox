/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <module_factory_registery.h>
#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

class apollo_hexagon_dma : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    tlm_utils::simple_target_socket<apollo_hexagon_dma, DEFAULT_TLM_BUSWIDTH> regs;
    tlm_utils::simple_initiator_socket<apollo_hexagon_dma, DEFAULT_TLM_BUSWIDTH> dma;

    explicit apollo_hexagon_dma(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , regs("regs")
        , dma("dma")
    {
        regs.register_b_transport(this, &apollo_hexagon_dma::b_transport);
        regs.register_transport_dbg(this, &apollo_hexagon_dma::transport_dbg);
    }

private:
    enum : uint64_t {
        REG_SRC = 0x00,
        REG_DST = 0x04,
        REG_LEN = 0x08,
        REG_CTRL = 0x0c,
        REG_STATUS = 0x10,
        REG_RESULT = 0x14,
        REG_FW_DONE = 0x18,
    };

    enum : uint32_t {
        CTRL_START = 1,
        STATUS_IDLE = 0,
        STATUS_DONE = 1,
        STATUS_ERROR = 2,
        RESULT_OK = 0x444d414f,    // "DMAO"
        RESULT_BAD_LEN = 0xbad00001,
    };

    uint32_t m_src = 0;
    uint32_t m_dst = 0;
    uint32_t m_len = 0;
    uint32_t m_status = STATUS_IDLE;
    uint32_t m_result = 0;
    uint32_t m_fw_done = 0;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint32_t value = 0;
        const uint64_t addr = trans.get_address();
        const unsigned int len = trans.get_data_length();

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        trans.set_dmi_allowed(false);

        if (len != sizeof(uint32_t)) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&value, trans.get_data_ptr(), sizeof(value));
            write_reg(addr, value, delay);
        } else if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            value = read_reg(addr);
            std::memcpy(trans.get_data_ptr(), &value, sizeof(value));
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        }
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(trans, delay);
        return trans.is_response_ok() ? trans.get_data_length() : 0;
    }

    uint32_t read_reg(uint64_t addr) const
    {
        switch (addr) {
        case REG_SRC:
            return m_src;
        case REG_DST:
            return m_dst;
        case REG_LEN:
            return m_len;
        case REG_STATUS:
            return m_status;
        case REG_RESULT:
            return m_result;
        case REG_FW_DONE:
            return m_fw_done;
        default:
            return 0;
        }
    }

    void write_reg(uint64_t addr, uint32_t value, sc_core::sc_time& delay)
    {
        switch (addr) {
        case REG_SRC:
            m_src = value;
            break;
        case REG_DST:
            m_dst = value;
            break;
        case REG_LEN:
            m_len = value;
            break;
        case REG_CTRL:
            if (value & CTRL_START) {
                run_dma(delay);
            }
            break;
        case REG_FW_DONE:
            m_fw_done = value;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: firmware done magic=0x" << std::hex << m_fw_done;
            std::cerr << "APOLLO_HEXAGON_DMA: firmware done magic=0x" << std::hex << m_fw_done << std::dec
                      << std::endl;
            break;
        default:
            SCP_WARN(()) << "APOLLO_HEXAGON_DMA: ignored write offset=0x" << std::hex << addr << " value=0x" << value;
            break;
        }
    }

    void run_dma(sc_core::sc_time& delay)
    {
        m_status = STATUS_IDLE;
        m_result = 0;

        if (m_len == 0 || m_len > 4096) {
            m_status = STATUS_ERROR;
            m_result = RESULT_BAD_LEN;
            SCP_ERR(()) << "APOLLO_HEXAGON_DMA: invalid length " << std::dec << m_len;
            return;
        }

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: firmware requested DMA src=0x" << std::hex << m_src << " dst=0x"
                     << m_dst << " len=0x" << m_len;
        std::cerr << "APOLLO_HEXAGON_DMA: firmware requested DMA src=0x" << std::hex << m_src << " dst=0x" << m_dst
                  << " len=0x" << m_len << std::dec << std::endl;

        std::vector<uint8_t> buffer(m_len);
        do_dma(tlm::TLM_READ_COMMAND, m_src, buffer.data(), m_len, delay);
        do_dma(tlm::TLM_WRITE_COMMAND, m_dst, buffer.data(), m_len, delay);

        uint32_t first_word = 0;
        std::memcpy(&first_word, buffer.data(), std::min(sizeof(first_word), buffer.size()));
        m_result = RESULT_OK;
        m_status = STATUS_DONE;

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: DMA copy complete src=0x" << std::hex << m_src << " dst=0x" << m_dst
                     << " len=0x" << m_len << " first=0x" << first_word;
        std::cerr << "APOLLO_HEXAGON_DMA: DMA copy complete src=0x" << std::hex << m_src << " dst=0x" << m_dst
                  << " len=0x" << m_len << " first=0x" << first_word << std::dec << std::endl;
    }

    void do_dma(tlm::tlm_command command, uint64_t addr, uint8_t* data, uint32_t len, sc_core::sc_time& delay)
    {
        tlm::tlm_generic_payload trans;
        trans.set_command(command);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        dma->b_transport(trans, delay);
        if (!trans.is_response_ok()) {
            m_status = STATUS_ERROR;
            SCP_FATAL(()) << "APOLLO_HEXAGON_DMA: DMA transaction failed at addr=0x" << std::hex << addr;
        }
    }
};

extern "C" void module_register();
