/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <mmu720ae.h>

mmu720ae::mmu720ae(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , p_profile("profile", "zena-css-cfg2")
    , p_stage("stage", "1")
    , p_trace("trace", false)
    , p_trace_limit("trace_limit", 64)
    , p_tbu_ace1_default_sid("tbu_ace1_default_sid", 0x00)
    , p_tbu_ace2_default_sid("tbu_ace2_default_sid", 0x20)
    , p_tbu_lti00_default_sid("tbu_lti00_default_sid", 0x40)
    , p_tbu_lti01_default_sid("tbu_lti01_default_sid", 0x60)
    , p_tbu_lti02_default_sid("tbu_lti02_default_sid", 0x80)
    , mem("mem")
    , reg_socket("reg_socket")
    , tbu_ace1_socket("tbu_ace1_socket")
    , tbu_ace2_socket("tbu_ace2_socket")
    , tbu_lti00_socket("tbu_lti00_socket")
    , tbu_lti01_socket("tbu_lti01_socket")
    , tbu_lti02_socket("tbu_lti02_socket")
    , downstream_socket("downstream_socket")
    , ptw_socket("ptw_socket")
    , irq_combined("irq_combined")
    , reset("reset")
{
    mem.register_b_transport(this, &mmu720ae::b_transport);
    mem.register_transport_dbg(this, &mmu720ae::transport_dbg);
    reg_socket.register_b_transport(this, &mmu720ae::b_transport);
    reg_socket.register_transport_dbg(this, &mmu720ae::transport_dbg);

    tbu_ace1_socket.register_b_transport(this, &mmu720ae::tbu_ace1_b_transport);
    tbu_ace1_socket.register_transport_dbg(this,
                                           &mmu720ae::tbu_ace1_transport_dbg);
    tbu_ace2_socket.register_b_transport(this, &mmu720ae::tbu_ace2_b_transport);
    tbu_ace2_socket.register_transport_dbg(this,
                                           &mmu720ae::tbu_ace2_transport_dbg);
    tbu_lti00_socket.register_b_transport(this,
                                          &mmu720ae::tbu_lti00_b_transport);
    tbu_lti00_socket.register_transport_dbg(
        this, &mmu720ae::tbu_lti00_transport_dbg);
    tbu_lti01_socket.register_b_transport(this,
                                          &mmu720ae::tbu_lti01_b_transport);
    tbu_lti01_socket.register_transport_dbg(
        this, &mmu720ae::tbu_lti01_transport_dbg);
    tbu_lti02_socket.register_b_transport(this,
                                          &mmu720ae::tbu_lti02_b_transport);
    tbu_lti02_socket.register_transport_dbg(
        this, &mmu720ae::tbu_lti02_transport_dbg);

    reset.register_value_changed_cb(
        [this](const bool& level) { doreset(level); });
}

void mmu720ae::doreset(bool level)
{
    if (level) {
        m_core.reset();
        update_irq();
    }
}

void mmu720ae::update_irq()
{
    if (irq_combined.size() != 0) {
        irq_combined->write(m_core.combined_irq_level());
    }
}

bool mmu720ae::access_registers(tlm::tlm_generic_payload& trans, bool debug)
{
    const auto len = trans.get_data_length();
    uint8_t* data = trans.get_data_ptr();

    qbox::mmu720ae::access_status status =
        qbox::mmu720ae::access_status::command_error;

    if (trans.get_command() == tlm::TLM_READ_COMMAND) {
        status = m_core.read(trans.get_address(), data, len, debug);
    } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
        status = m_core.write(trans.get_address(), data, len, debug);
    }

    switch (status) {
    case qbox::mmu720ae::access_status::ok:
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        trace_access(trans, debug);
        update_irq();
        return true;
    case qbox::mmu720ae::access_status::address_error:
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return false;
    case qbox::mmu720ae::access_status::command_error:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return false;
    }

    trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    return false;
}

void mmu720ae::b_transport(tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay)
{
    (void)delay;
    trans.set_dmi_allowed(false);
    access_registers(trans, false);
}

unsigned int mmu720ae::transport_dbg(tlm::tlm_generic_payload& trans)
{
    return access_registers(trans, true) ? trans.get_data_length() : 0;
}

bool mmu720ae::forward_bypass(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay, bool debug)
{
    if (debug) {
        const unsigned int done = downstream_socket->transport_dbg(trans);
        return done == trans.get_data_length();
    }

    downstream_socket->b_transport(trans, delay);
    return trans.get_response_status() == tlm::TLM_OK_RESPONSE;
}

uint32_t mmu720ae::clamp_sid(uint32_t sid) const
{
    return sid & 0xffu;
}

uint32_t mmu720ae::request_sid_or_default(tlm::tlm_generic_payload& trans,
                                          uint32_t default_sid,
                                          bool& fallback) const
{
    qbox::mmu720ae::request_attrs_extension* attrs = nullptr;

    trans.get_extension(attrs);
    if (attrs != nullptr && attrs->sid_valid) {
        fallback = false;
        return clamp_sid(attrs->sid);
    }

    fallback = true;
    return clamp_sid(default_sid);
}

bool mmu720ae::write_event_record(
    const qbox::mmu720ae::event_record& record,
    uint64_t address,
    sc_core::sc_time& delay)
{
    tlm::tlm_generic_payload event_trans;
    auto data = record.dwords;

    event_trans.set_address(address);
    event_trans.set_command(tlm::TLM_WRITE_COMMAND);
    event_trans.set_data_length(qbox::mmu720ae::EVTQ_ENT_BYTES);
    event_trans.set_streaming_width(qbox::mmu720ae::EVTQ_ENT_BYTES);
    event_trans.set_data_ptr(reinterpret_cast<unsigned char*>(data.data()));

    ptw_socket->b_transport(event_trans, delay);
    return event_trans.get_response_status() == tlm::TLM_OK_RESPONSE;
}

void mmu720ae::record_unimplemented_translation_fault(
    tlm::tlm_generic_payload& trans,
    sc_core::sc_time& delay,
    uint32_t sid,
    bool fallback_sid)
{
    qbox::mmu720ae::event_record record;
    uint64_t write_address = 0;
    const uint32_t event_sid = clamp_sid(sid);

    m_core.record_tbu_request_sid(event_sid, fallback_sid);

    const bool prepared = m_core.build_translation_fault_event(
        trans.get_address(), trans.is_read(), event_sid, record, write_address);
    if (!prepared) {
        m_core.record_event_queue_abort();
        update_irq();
        return;
    }

    const bool written = write_event_record(record, write_address, delay);
    m_core.complete_event_queue_write(written);
    update_irq();
}

void mmu720ae::tbu_b_transport(tlm::tlm_generic_payload& trans,
                               sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_ace1_default_sid.get_value());
}

void mmu720ae::tbu_b_transport_with_sid(tlm::tlm_generic_payload& trans,
                                        sc_core::sc_time& delay, uint32_t sid)
{
    bool fallback_sid = false;
    const uint32_t request_sid = request_sid_or_default(trans, sid,
                                                        fallback_sid);

    /*
     * With SMMUEN clear, requester traffic is allowed to bypass during reset
     * and early bring-up. Once software enables the SMMU, silent bypass would
     * hide missing translation behavior, so block the request until the STE/CD
     * table walker phase is implemented.
     */
    if (m_core.smmu_enabled()) {
        record_unimplemented_translation_fault(trans, delay, request_sid,
                                               fallback_sid);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        update_irq();
        return;
    }

    forward_bypass(trans, delay, false);
}

unsigned int mmu720ae::tbu_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans, p_tbu_ace1_default_sid.get_value());
}

unsigned int mmu720ae::tbu_transport_dbg_with_sid(
    tlm::tlm_generic_payload& trans, uint32_t sid)
{
    if (m_core.smmu_enabled()) {
        bool fallback_sid = false;
        const uint32_t request_sid = request_sid_or_default(trans, sid,
                                                            fallback_sid);
        m_core.record_tbu_request_sid(request_sid, fallback_sid);
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return 0;
    }

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    return forward_bypass(trans, delay, true) ? trans.get_data_length() : 0;
}

void mmu720ae::tbu_ace1_b_transport(tlm::tlm_generic_payload& trans,
                                    sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_ace1_default_sid.get_value());
}

void mmu720ae::tbu_ace2_b_transport(tlm::tlm_generic_payload& trans,
                                    sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_ace2_default_sid.get_value());
}

void mmu720ae::tbu_lti00_b_transport(tlm::tlm_generic_payload& trans,
                                     sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_lti00_default_sid.get_value());
}

void mmu720ae::tbu_lti01_b_transport(tlm::tlm_generic_payload& trans,
                                     sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_lti01_default_sid.get_value());
}

void mmu720ae::tbu_lti02_b_transport(tlm::tlm_generic_payload& trans,
                                     sc_core::sc_time& delay)
{
    tbu_b_transport_with_sid(trans, delay, p_tbu_lti02_default_sid.get_value());
}

unsigned int mmu720ae::tbu_ace1_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans,
                                      p_tbu_ace1_default_sid.get_value());
}

unsigned int mmu720ae::tbu_ace2_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans,
                                      p_tbu_ace2_default_sid.get_value());
}

unsigned int mmu720ae::tbu_lti00_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans,
                                      p_tbu_lti00_default_sid.get_value());
}

unsigned int mmu720ae::tbu_lti01_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans,
                                      p_tbu_lti01_default_sid.get_value());
}

unsigned int mmu720ae::tbu_lti02_transport_dbg(tlm::tlm_generic_payload& trans)
{
    return tbu_transport_dbg_with_sid(trans,
                                      p_tbu_lti02_default_sid.get_value());
}

void mmu720ae::trace_access(tlm::tlm_generic_payload& trans, bool debug)
{
    if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
        return;
    }

    ++m_trace_count;
    uint64_t value = 0;
    if (trans.get_data_ptr() != nullptr &&
        trans.get_data_length() <= sizeof(value)) {
        std::memcpy(&value, trans.get_data_ptr(), trans.get_data_length());
    }

    std::cerr << name() << " "
              << (debug ? "dbg_" : "")
              << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" :
                                                               "write")
              << " offset=0x" << std::hex << trans.get_address()
              << " len=0x" << trans.get_data_length()
              << " value=0x" << value
              << std::dec << std::endl;
}

extern "C" void module_register()
{
    GSC_MODULE_REGISTER_C(mmu720ae);
}
