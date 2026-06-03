/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class host_smcf_mgi : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x10000;

    static constexpr uint32_t GRP_ID = 0x000;
    static constexpr uint32_t DATA_INFO = 0x008;
    static constexpr uint32_t FEAT0 = 0x010;
    static constexpr uint32_t FEAT1 = 0x018;
    static constexpr uint32_t SMP_EN = 0x030;
    static constexpr uint32_t MON_REQ = 0x060;
    static constexpr uint32_t MON_STAT = 0x070;
    static constexpr uint32_t MODE_REQ0 = 0x090;
    static constexpr uint32_t MODE_REQ1 = 0x098;
    static constexpr uint32_t MODE_REQ2 = 0x0a0;
    static constexpr uint32_t MODE_REQ3 = 0x0a8;
    static constexpr uint32_t MODE_STAT0 = 0x0c0;
    static constexpr uint32_t MODE_STAT1 = 0x0c8;
    static constexpr uint32_t MODE_STAT2 = 0x0d0;
    static constexpr uint32_t MODE_STAT3 = 0x0d8;
    static constexpr uint32_t IRQ_STAT = 0x100;
    static constexpr uint32_t ERR_CODE = 0x150;
    static constexpr uint32_t DVLD = 0xf00;
    static constexpr uint32_t IIDR = 0xfc0;
    static constexpr uint32_t AIDR = 0xfc8;

    static constexpr uint32_t FEAT0_DMA_IF = (1u << 28);
    static constexpr uint32_t FEAT0_PER_TIMER = (1u << 31);
    static constexpr uint32_t SMP_EN_ENABLE = (1u << 0);

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    uint32_t load32(uint32_t offset) const
    {
        uint32_t value = 0;
        std::memcpy(&value, &m_regs[offset], sizeof(value));
        return value;
    }

    void store32(uint32_t offset, uint32_t value)
    {
        std::memcpy(&m_regs[offset], &value, sizeof(value));
    }

    uint32_t monitor_mask() const
    {
        const auto count = p_monitor_count.get_value();
        if (count >= 32)
            return UINT32_MAX;
        return (1u << count) - 1u;
    }

    uint32_t make_grp_id() const
    {
        const auto count = p_monitor_count.get_value();
        const auto encoded_count = count == 0 ? 0 : count - 1;
        return (p_group_id.get_value() & 0x7ffu) |
            ((encoded_count & 0x1fu) << 16);
    }

    uint32_t make_data_info() const
    {
        const auto data_values = p_data_values_per_monitor.get_value();
        const auto data_width = p_data_width_bits.get_value();
        const auto encoded_values = data_values == 0 ? 0 : data_values - 1;
        const auto encoded_width = data_width == 0 ? 0 : data_width - 1;
        return (encoded_values & 0xffffu) | ((encoded_width & 0x3fu) << 22);
    }

    uint32_t make_feat0() const
    {
        uint32_t value = 0;
        if (p_dma_supported.get_value())
            value |= FEAT0_DMA_IF;
        if (p_periodic_timer_supported.get_value())
            value |= FEAT0_PER_TIMER;
        return value;
    }

    uint32_t make_feat1() const
    {
        const auto mode_registers = p_mode_registers.get_value();
        const auto mode_bits = p_mode_bits.get_value();
        const auto encoded_registers =
            mode_registers == 0 ? 0 : mode_registers - 1;
        const auto encoded_bits = mode_bits == 0 ? 0 : mode_bits - 1;
        return (encoded_registers & 0x7u) | ((encoded_bits & 0x1fu) << 8) |
            ((p_sample_delay_bits.get_value() & 0x3fu) << 16);
    }

    void reset_registers()
    {
        m_regs.fill(0);
        store32(GRP_ID, make_grp_id());
        store32(DATA_INFO, make_data_info());
        store32(FEAT0, make_feat0());
        store32(FEAT1, make_feat1());
        store32(DVLD, monitor_mask());
        store32(IIDR, p_iidr.get_value());
        store32(AIDR, p_aidr.get_value());
    }

    static bool is_read_only(uint32_t offset)
    {
        return offset == GRP_ID || offset == DATA_INFO || offset == FEAT0 ||
            offset == FEAT1 || offset == MON_STAT || offset == MODE_STAT0 ||
            offset == MODE_STAT1 || offset == MODE_STAT2 ||
            offset == MODE_STAT3 || offset == ERR_CODE || offset == DVLD ||
            offset == IIDR || offset == AIDR;
    }

    void mirror_mode_status(uint32_t offset, uint32_t value)
    {
        if (offset == MODE_REQ0)
            store32(MODE_STAT0, value);
        else if (offset == MODE_REQ1)
            store32(MODE_STAT1, value);
        else if (offset == MODE_REQ2)
            store32(MODE_STAT2, value);
        else if (offset == MODE_REQ3)
            store32(MODE_STAT3, value);
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (is_read_only(offset))
            return;

        if (offset == MON_REQ) {
            const auto mask = monitor_mask();
            store32(MON_REQ, value & mask);
            store32(MON_STAT, value & mask);
            return;
        }

        if (offset == SMP_EN) {
            store32(SMP_EN, value & SMP_EN_ENABLE);
            return;
        }

        if (offset == IRQ_STAT) {
            store32(IRQ_STAT, load32(IRQ_STAT) & ~value);
            return;
        }

        store32(offset, value);
        mirror_mode_status(offset, value);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_regs[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value);
            } else {
                std::memcpy(&m_regs[offset], data, len);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value())
            return;

        ++m_trace_count;
        uint32_t value = 0;
        if (len <= sizeof(value))
            std::memcpy(&value, trans.get_data_ptr(), len);

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" :
                                                                      "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<uint32_t> p_group_id;
    cci::cci_param<uint32_t> p_monitor_count;
    cci::cci_param<uint32_t> p_data_values_per_monitor;
    cci::cci_param<uint32_t> p_data_width_bits;
    cci::cci_param<bool> p_dma_supported;
    cci::cci_param<bool> p_periodic_timer_supported;
    cci::cci_param<uint32_t> p_mode_registers;
    cci::cci_param<uint32_t> p_mode_bits;
    cci::cci_param<uint32_t> p_sample_delay_bits;
    cci::cci_param<uint32_t> p_iidr;
    cci::cci_param<uint32_t> p_aidr;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<host_smcf_mgi, DEFAULT_TLM_BUSWIDTH>
        target_socket;

    explicit host_smcf_mgi(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_group_id("group_id", 0)
        , p_monitor_count("monitor_count", 1)
        , p_data_values_per_monitor("data_values_per_monitor", 12)
        , p_data_width_bits("data_width_bits", 32)
        , p_dma_supported("dma_supported", true)
        , p_periodic_timer_supported("periodic_timer_supported", true)
        , p_mode_registers("mode_registers", 1)
        , p_mode_bits("mode_bits", 1)
        , p_sample_delay_bits("sample_delay_bits", 0)
        , p_iidr("iidr", 0)
        , p_aidr("aidr", 0)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &host_smcf_mgi::b_transport);
        target_socket.register_transport_dbg(this,
                                             &host_smcf_mgi::transport_dbg);
    }

    void before_end_of_elaboration() override
    {
        reset_registers();
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
