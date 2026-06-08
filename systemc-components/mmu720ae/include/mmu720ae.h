/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <mmu720ae_core.h>
#include <mmu720ae_tlm_extensions.h>

class mmu720ae : public sc_core::sc_module
{
    using target_socket_type = tlm_utils::simple_target_socket_b<
        mmu720ae, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
        sc_core::SC_ZERO_OR_MORE_BOUND>;
    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        mmu720ae, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
        sc_core::SC_ZERO_OR_MORE_BOUND>;

public:
    cci::cci_param<std::string> p_profile;
    cci::cci_param<std::string> p_stage;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<uint32_t> p_tbu_ace1_default_sid;
    cci::cci_param<uint32_t> p_tbu_ace2_default_sid;
    cci::cci_param<uint32_t> p_tbu_lti00_default_sid;
    cci::cci_param<uint32_t> p_tbu_lti01_default_sid;
    cci::cci_param<uint32_t> p_tbu_lti02_default_sid;

    target_socket_type mem;
    target_socket_type reg_socket;
    target_socket_type tbu_ace1_socket;
    target_socket_type tbu_ace2_socket;
    target_socket_type tbu_lti00_socket;
    target_socket_type tbu_lti01_socket;
    target_socket_type tbu_lti02_socket;
    initiator_socket_type downstream_socket;
    initiator_socket_type ptw_socket;
    InitiatorSignalSocket<bool> irq_combined;
    TargetSignalSocket<bool> reset;

    explicit mmu720ae(sc_core::sc_module_name name);

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    unsigned int transport_dbg(tlm::tlm_generic_payload& trans);

    void tbu_b_transport(tlm::tlm_generic_payload& trans,
                         sc_core::sc_time& delay);
    unsigned int tbu_transport_dbg(tlm::tlm_generic_payload& trans);

    void doreset(bool level);

    qbox::mmu720ae::core& core_for_test() { return m_core; }

private:
    qbox::mmu720ae::core m_core;
    unsigned int m_trace_count = 0;

    bool access_registers(tlm::tlm_generic_payload& trans, bool debug);
    bool forward_bypass(tlm::tlm_generic_payload& trans,
                        sc_core::sc_time& delay, bool debug);
    uint32_t clamp_sid(uint32_t sid) const;
    uint32_t request_sid_or_default(tlm::tlm_generic_payload& trans,
                                    uint32_t default_sid,
                                    bool& fallback) const;
    bool write_event_record(const qbox::mmu720ae::event_record& record,
                            uint64_t address, sc_core::sc_time& delay);
    void record_unimplemented_translation_fault(tlm::tlm_generic_payload& trans,
                                                sc_core::sc_time& delay,
                                                uint32_t sid,
                                                bool fallback_sid);
    void tbu_b_transport_with_sid(tlm::tlm_generic_payload& trans,
                                  sc_core::sc_time& delay, uint32_t sid);
    unsigned int tbu_transport_dbg_with_sid(tlm::tlm_generic_payload& trans,
                                            uint32_t sid);
    void tbu_ace1_b_transport(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay);
    void tbu_ace2_b_transport(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay);
    void tbu_lti00_b_transport(tlm::tlm_generic_payload& trans,
                               sc_core::sc_time& delay);
    void tbu_lti01_b_transport(tlm::tlm_generic_payload& trans,
                               sc_core::sc_time& delay);
    void tbu_lti02_b_transport(tlm::tlm_generic_payload& trans,
                               sc_core::sc_time& delay);
    unsigned int tbu_ace1_transport_dbg(tlm::tlm_generic_payload& trans);
    unsigned int tbu_ace2_transport_dbg(tlm::tlm_generic_payload& trans);
    unsigned int tbu_lti00_transport_dbg(tlm::tlm_generic_payload& trans);
    unsigned int tbu_lti01_transport_dbg(tlm::tlm_generic_payload& trans);
    unsigned int tbu_lti02_transport_dbg(tlm::tlm_generic_payload& trans);
    void trace_access(tlm::tlm_generic_payload& trans, bool debug);
    void update_irq();
};

extern "C" void module_register();
