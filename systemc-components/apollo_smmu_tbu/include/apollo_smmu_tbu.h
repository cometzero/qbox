/*
 * Apollo functional SMMU/TBU bridge.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <scp/report.h>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class apollo_smmu_tbu : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    tlm_utils::simple_target_socket<apollo_smmu_tbu, DEFAULT_TLM_BUSWIDTH> upstream;
    tlm_utils::simple_initiator_socket<apollo_smmu_tbu, DEFAULT_TLM_BUSWIDTH> downstream;

    cci::cci_param<uint32_t> p_stream_id;
    cci::cci_param<uint64_t> p_iova_base;
    cci::cci_param<uint64_t> p_pa_base;
    cci::cci_param<uint64_t> p_window_size;

    explicit apollo_smmu_tbu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , upstream("upstream")
        , downstream("downstream")
        , p_stream_id("stream_id", 1, "StreamID associated with this TBU")
        , p_iova_base("iova_base", 0x10000000ULL, "Base IOVA accepted by this TBU")
        , p_pa_base("pa_base", 0x00a00000ULL, "Translated physical base for the IOVA window")
        , p_window_size("window_size", 0x00600000ULL, "Size of translated IOVA window")
    {
        upstream.register_b_transport(this, &apollo_smmu_tbu::b_transport);
        upstream.register_transport_dbg(this, &apollo_smmu_tbu::transport_dbg);
    }

private:
    bool translate(uint64_t iova, uint64_t len, uint64_t& pa) const
    {
        const uint64_t base = p_iova_base.get_value();
        const uint64_t size = p_window_size.get_value();

        if (len == 0 || size == 0 || iova < base) {
            return false;
        }

        const uint64_t offset = iova - base;
        if (offset >= size || len > size - offset) {
            return false;
        }

        pa = p_pa_base.get_value() + offset;
        return true;
    }

    void log_translate(const char* op, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << p_stream_id.get_value()
                     << " translate " << op << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << p_stream_id.get_value() << " translate "
                  << op << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << std::dec << std::endl;
    }

    void log_fault(const char* op, uint64_t iova, uint64_t len)
    {
        SCP_ERR(()) << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex
                    << p_stream_id.get_value() << " " << op << " iova=0x" << iova << " len=0x" << len
                    << " window=[0x" << p_iova_base.get_value() << "..0x"
                    << (p_iova_base.get_value() + p_window_size.get_value()) << ")" << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " " << op << " iova=0x" << iova << " len=0x" << len << " window=[0x"
                  << p_iova_base.get_value() << "..0x" << (p_iova_base.get_value() + p_window_size.get_value())
                  << ")" << std::dec << std::endl;
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        const char* op = trans.is_read() ? "read" : (trans.is_write() ? "write" : "op");
        uint64_t pa = 0;

        if (!translate(iova, len, pa)) {
            log_fault(op, iova, len);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        log_translate(op, iova, pa, len);
        trans.set_address(pa);
        downstream->b_transport(trans, delay);
        trans.set_address(iova);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        uint64_t pa = 0;

        if (!translate(iova, len, pa)) {
            return 0;
        }

        trans.set_address(pa);
        const unsigned int ret = downstream->transport_dbg(trans);
        trans.set_address(iova);
        return ret;
    }
};

extern "C" void module_register();
