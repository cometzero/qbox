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

class gicx00_multiview : public sc_core::sc_module
{
    static constexpr uint64_t DIST_BYTES = 0x10000;
    static constexpr uint64_t REDIST_BYTES = 0x20000;
    static constexpr unsigned int REDIST_COUNT = 16;

    static constexpr uint32_t GICD_CTLR = 0x0000;
    static constexpr uint32_t GICD_CFGID = 0xf000;
    static constexpr uint32_t GICD_IVIEWR_BASE = 0xf600;
    static constexpr uint32_t GICD_IVIEWR_LIMIT = 0xfa00;
    static constexpr uint64_t GICD_CFGID_VIEW = 1ull << 53;

    static constexpr uint32_t GICR_PWRR = 0x0024;
    static constexpr uint32_t GICR_VIEWR = 0x002c;

    using target_socket_t =
        tlm_utils::simple_target_socket_b<
            gicx00_multiview, DEFAULT_TLM_BUSWIDTH,
            tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

    std::array<uint8_t, DIST_BYTES> m_dist_regs {};
    std::array<std::array<uint8_t, REDIST_BYTES>, REDIST_COUNT> m_redist_regs {};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    template <size_t N>
    static uint32_t load32(const std::array<uint8_t, N>& regs, uint32_t offset)
    {
        uint32_t value = 0;
        std::memcpy(&value, &regs[offset], sizeof(value));
        return value;
    }

    template <size_t N>
    static void store32(std::array<uint8_t, N>& regs, uint32_t offset,
                        uint32_t value)
    {
        std::memcpy(&regs[offset], &value, sizeof(value));
    }

    template <size_t N>
    static void store64(std::array<uint8_t, N>& regs, uint32_t offset,
                        uint64_t value)
    {
        std::memcpy(&regs[offset], &value, sizeof(value));
    }

    void reset_registers()
    {
        m_dist_regs.fill(0);
        for (auto& regs : m_redist_regs) {
            regs.fill(0);
            store32(regs, GICR_PWRR, 0);
            store32(regs, GICR_VIEWR, 0);
        }

        store32(m_dist_regs, GICD_CTLR, 0);
        store64(m_dist_regs, GICD_CFGID, GICD_CFGID_VIEW);
    }

    void trace_access(const char* region, unsigned int index,
                      tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        ++m_trace_count;
        uint64_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        std::cerr << name() << " " << region;
        if (index != UINT32_MAX) {
            std::cerr << "[" << index << "]";
        }
        std::cerr << " " << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

    template <size_t N>
    bool access_array(std::array<uint8_t, N>& regs, const char* region,
                      unsigned int index, tlm::tlm_generic_payload& trans,
                      bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            offset + len > regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &regs[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&regs[offset], data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(region, index, trans, offset, len, debug);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    bool access_dist(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND &&
            offset >= GICD_IVIEWR_BASE && offset < GICD_IVIEWR_LIMIT &&
            trans.get_data_length() == sizeof(uint32_t)) {
            return access_array(m_dist_regs, "dist", UINT32_MAX, trans, debug);
        }
        return access_array(m_dist_regs, "dist", UINT32_MAX, trans, debug);
    }

    bool access_redist(unsigned int index, tlm::tlm_generic_payload& trans,
                       bool debug)
    {
        if (index >= m_redist_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        const uint64_t offset = trans.get_address();
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND &&
            trans.get_data_length() == sizeof(uint32_t) &&
            offset == GICR_PWRR) {
            uint32_t value = 0;
            std::memcpy(&value, trans.get_data_ptr(), sizeof(value));
            value &= ~0x1u;
            std::memcpy(trans.get_data_ptr(), &value, sizeof(value));
        }

        return access_array(m_redist_regs[index], "redist", index, trans, debug);
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;

    target_socket_t view0_dist;
    target_socket_t view0_redist_0;
    target_socket_t view0_redist_1;
    target_socket_t view0_redist_2;
    target_socket_t view0_redist_3;
    target_socket_t view0_redist_4;
    target_socket_t view0_redist_5;
    target_socket_t view0_redist_6;
    target_socket_t view0_redist_7;
    target_socket_t view0_redist_8;
    target_socket_t view0_redist_9;
    target_socket_t view0_redist_10;
    target_socket_t view0_redist_11;
    target_socket_t view0_redist_12;
    target_socket_t view0_redist_13;
    target_socket_t view0_redist_14;
    target_socket_t view0_redist_15;

    explicit gicx00_multiview(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 128)
        , view0_dist("view0_dist")
        , view0_redist_0("view0_redist_0")
        , view0_redist_1("view0_redist_1")
        , view0_redist_2("view0_redist_2")
        , view0_redist_3("view0_redist_3")
        , view0_redist_4("view0_redist_4")
        , view0_redist_5("view0_redist_5")
        , view0_redist_6("view0_redist_6")
        , view0_redist_7("view0_redist_7")
        , view0_redist_8("view0_redist_8")
        , view0_redist_9("view0_redist_9")
        , view0_redist_10("view0_redist_10")
        , view0_redist_11("view0_redist_11")
        , view0_redist_12("view0_redist_12")
        , view0_redist_13("view0_redist_13")
        , view0_redist_14("view0_redist_14")
        , view0_redist_15("view0_redist_15")
    {
        reset_registers();
        view0_dist.register_b_transport(this, &gicx00_multiview::b_transport_dist);
        view0_dist.register_transport_dbg(this, &gicx00_multiview::transport_dbg_dist);
        view0_redist_0.register_b_transport(this, &gicx00_multiview::b_transport_redist0);
        view0_redist_0.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist0);
        view0_redist_1.register_b_transport(this, &gicx00_multiview::b_transport_redist1);
        view0_redist_1.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist1);
        view0_redist_2.register_b_transport(this, &gicx00_multiview::b_transport_redist2);
        view0_redist_2.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist2);
        view0_redist_3.register_b_transport(this, &gicx00_multiview::b_transport_redist3);
        view0_redist_3.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist3);
        view0_redist_4.register_b_transport(this, &gicx00_multiview::b_transport_redist4);
        view0_redist_4.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist4);
        view0_redist_5.register_b_transport(this, &gicx00_multiview::b_transport_redist5);
        view0_redist_5.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist5);
        view0_redist_6.register_b_transport(this, &gicx00_multiview::b_transport_redist6);
        view0_redist_6.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist6);
        view0_redist_7.register_b_transport(this, &gicx00_multiview::b_transport_redist7);
        view0_redist_7.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist7);
        view0_redist_8.register_b_transport(this, &gicx00_multiview::b_transport_redist8);
        view0_redist_8.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist8);
        view0_redist_9.register_b_transport(this, &gicx00_multiview::b_transport_redist9);
        view0_redist_9.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist9);
        view0_redist_10.register_b_transport(this, &gicx00_multiview::b_transport_redist10);
        view0_redist_10.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist10);
        view0_redist_11.register_b_transport(this, &gicx00_multiview::b_transport_redist11);
        view0_redist_11.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist11);
        view0_redist_12.register_b_transport(this, &gicx00_multiview::b_transport_redist12);
        view0_redist_12.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist12);
        view0_redist_13.register_b_transport(this, &gicx00_multiview::b_transport_redist13);
        view0_redist_13.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist13);
        view0_redist_14.register_b_transport(this, &gicx00_multiview::b_transport_redist14);
        view0_redist_14.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist14);
        view0_redist_15.register_b_transport(this, &gicx00_multiview::b_transport_redist15);
        view0_redist_15.register_transport_dbg(this, &gicx00_multiview::transport_dbg_redist15);
    }

    void b_transport_dist(tlm::tlm_generic_payload& trans,
                          sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access_dist(trans, false);
    }

    unsigned int transport_dbg_dist(tlm::tlm_generic_payload& trans)
    {
        return access_dist(trans, true) ? trans.get_data_length() : 0;
    }

    void b_transport_redist(unsigned int index, tlm::tlm_generic_payload& trans,
                            sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access_redist(index, trans, false);
    }

    unsigned int transport_dbg_redist(unsigned int index,
                                      tlm::tlm_generic_payload& trans)
    {
        return access_redist(index, trans, true) ? trans.get_data_length() : 0;
    }

    void b_transport_redist0(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(0, trans, delay);
    }
    void b_transport_redist1(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(1, trans, delay);
    }
    void b_transport_redist2(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(2, trans, delay);
    }
    void b_transport_redist3(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(3, trans, delay);
    }
    void b_transport_redist4(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(4, trans, delay);
    }
    void b_transport_redist5(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(5, trans, delay);
    }
    void b_transport_redist6(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(6, trans, delay);
    }
    void b_transport_redist7(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(7, trans, delay);
    }
    void b_transport_redist8(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(8, trans, delay);
    }
    void b_transport_redist9(tlm::tlm_generic_payload& trans,
                             sc_core::sc_time& delay)
    {
        b_transport_redist(9, trans, delay);
    }
    void b_transport_redist10(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(10, trans, delay);
    }
    void b_transport_redist11(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(11, trans, delay);
    }
    void b_transport_redist12(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(12, trans, delay);
    }
    void b_transport_redist13(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(13, trans, delay);
    }
    void b_transport_redist14(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(14, trans, delay);
    }
    void b_transport_redist15(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
    {
        b_transport_redist(15, trans, delay);
    }

    unsigned int transport_dbg_redist0(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(0, trans);
    }
    unsigned int transport_dbg_redist1(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(1, trans);
    }
    unsigned int transport_dbg_redist2(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(2, trans);
    }
    unsigned int transport_dbg_redist3(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(3, trans);
    }
    unsigned int transport_dbg_redist4(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(4, trans);
    }
    unsigned int transport_dbg_redist5(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(5, trans);
    }
    unsigned int transport_dbg_redist6(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(6, trans);
    }
    unsigned int transport_dbg_redist7(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(7, trans);
    }
    unsigned int transport_dbg_redist8(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(8, trans);
    }
    unsigned int transport_dbg_redist9(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(9, trans);
    }
    unsigned int transport_dbg_redist10(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(10, trans);
    }
    unsigned int transport_dbg_redist11(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(11, trans);
    }
    unsigned int transport_dbg_redist12(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(12, trans);
    }
    unsigned int transport_dbg_redist13(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(13, trans);
    }
    unsigned int transport_dbg_redist14(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(14, trans);
    }
    unsigned int transport_dbg_redist15(tlm::tlm_generic_payload& trans)
    {
        return transport_dbg_redist(15, trans);
    }
};

extern "C" void module_register();
