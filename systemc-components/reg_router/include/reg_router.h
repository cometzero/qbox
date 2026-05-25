/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_REG_ROUTER_H
#define _GREENSOCS_BASE_COMPONENTS_REG_ROUTER_H

#include <cinttypes>
#include <iomanip>
#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <scp/helpers.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <module_factory_container.h>
#include <tlm-extensions/underlying-dmi.h>
#include <tlm_sockets_buswidth.h>
#include <router_if.h>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <limits>

#define ENABLE_MODULE_NAME_DBG_INFO 1
namespace gs {

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class reg_router : public sc_core::sc_module, public gs::router_if<BUSWIDTH>
{
    SCP_LOGGER(());
    SCP_LOGGER((DMI), "dmi");

    using typename gs::router_if<BUSWIDTH>::target_info;
    using initiator_socket_type = typename gs::router_if<BUSWIDTH>::template multi_passthrough_initiator_socket_spying<
        reg_router<BUSWIDTH>>;
    using gs::router_if<BUSWIDTH>::bound_targets;

    void register_boundto(std::string s) override
    {
        s = gs::router_if<BUSWIDTH>::nameFromSocket(s);
        std::shared_ptr<target_info> ti_ptr = std::make_shared<target_info>(); // Create shared_ptr
        ti_ptr->name = s;
        ti_ptr->index = bound_targets.size();
        SCP_DEBUG(()) << "Connecting : " << ti_ptr->name;
        auto tmp = name();
        int i;
        for (i = 0; i < s.length(); i++)
            if (s[i] != tmp[i]) break;
        ti_ptr->shortname = s.substr(i);
        bound_targets.push_back(ti_ptr); // Add shared_ptr
    }

public:
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = trans.get_address();

        std::shared_ptr<target_info> ti = decode_address(trans); // Use shared_ptr

        if (!ti) {
            SCP_WARN(())("Attempt to access unknown location in register memory at offset 0x{:x}", addr);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
        uint64_t mod_addr = 0;
        bool mod_found = false;
        const auto& mod_name = get_module_name(trans, mod_addr, mod_found);
        if (!mod_name.empty())
            SCP_DEBUG(()) << "b_transport: " << txn_command_str(trans) << " to Register at address: 0x" << std::hex
                          << trans.get_address() << " which belongs to Module: [" << mod_name << "]";
        if (m_pre_b_transport_callback) m_pre_b_transport_callback(mod_found, mod_addr);
#endif

        bool found_cb = do_callbacks(trans, delay);
        if (trans.get_response_status() >= tlm::TLM_INCOMPLETE_RESPONSE) {
            SCP_TRACEALL(()) << "call b_transport: " << txn_to_str(trans, false, found_cb);
            if (ti->use_offset) trans.set_address(addr - ti->address);
            initiator_socket[ti->index]->b_transport(trans, delay);
            if (ti->use_offset) trans.set_address(addr);
            SCP_TRACEALL(()) << "b_transport returned: " << txn_to_str(trans, false, found_cb);
        }
        if (trans.get_response_status() >= tlm::TLM_OK_RESPONSE) {
            do_callbacks(trans, delay);
        }
        if (cb_targets.size() > 0) {
            trans.set_dmi_allowed(false);
        }
    }

    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
    {
        sc_dt::uint64 addr = trans.get_address();
        // transport_dbg transactions should only be handled by register memory
        std::shared_ptr<target_info> ti = decode_address(trans); // Use shared_ptr

        if (!ti) {
            SCP_WARN(())("transport_dbg: Attempt to access unknown location in register memory at offset 0x{:x}", addr);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return 0;
        }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
        uint64_t mod_addr = 0;
        bool mod_found = false;
        const auto& mod_name = get_module_name(trans, mod_addr, mod_found);
        if (!mod_name.empty())
            SCP_DEBUG(()) << "transport_dbg: " << txn_command_str(trans) << " to Register at address: 0x" << std::hex
                          << trans.get_address() << " which belongs to Module: [" << mod_name << "]";
        if (m_pre_b_transport_callback) m_pre_b_transport_callback(mod_found, mod_addr);
#endif

        if (ti->use_offset) trans.set_address(addr - ti->address);
        SCP_TRACEALL(()) << "[REG-MEM] call transport_dbg [txn]: " << scp::scp_txn_tostring(trans);
        unsigned int ret = initiator_socket[ti->index]->transport_dbg(trans);
        if (ti->use_offset) trans.set_address(addr);
        return ret;
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();
        std::shared_ptr<target_info> ti = decode_address(trans);
        tlm::tlm_dmi mapped_range;

        if (ti) {
            mapped_range.set_start_address(ti->address);
            mapped_range.set_end_address(end_address(*ti));
        } else {
            mapped_range.set_start_address(addr);
            mapped_range.set_end_address(addr);
        }

        UnderlyingDMITlmExtension* u_dmi = nullptr;
        trans.get_extension(u_dmi);
        if (u_dmi) {
            u_dmi->add_dmi(this, mapped_range, ti ? gs::tlm_dmi_ex::dmi_mapped : gs::tlm_dmi_ex::dmi_nomap);
        }

        if (!ti) {
            SCP_TRACE((DMI)) << "[REG-MEM] DMI request to unmapped address 0x" << std::hex << addr;
            return false;
        }

        if (ti->use_offset) {
            trans.set_address(addr - ti->address);
        }

        SCP_TRACE((DMI)) << "[REG-MEM] call get_direct_mem_ptr [txn]: " << scp::scp_txn_tostring(trans);
        bool status = initiator_socket[ti->index]->get_direct_mem_ptr(trans, dmi_data);

        if (ti->use_offset) {
            trans.set_address(addr);
        }

        if (!status) {
            return false;
        }

        if (ti->use_offset) {
            dmi_data.set_start_address(ti->address + dmi_data.get_start_address());
            dmi_data.set_end_address(ti->address + dmi_data.get_end_address());
        }

        if (dmi_data.get_start_address() < mapped_range.get_start_address()) {
            dmi_data.set_dmi_ptr(dmi_data.get_dmi_ptr() +
                                 (mapped_range.get_start_address() - dmi_data.get_start_address()));
            dmi_data.set_start_address(mapped_range.get_start_address());
        }
        if (dmi_data.get_end_address() > mapped_range.get_end_address()) {
            dmi_data.set_end_address(mapped_range.get_end_address());
        }

        SCP_DEBUG((DMI)) << "[REG-MEM] providing DMI [0x" << std::hex << dmi_data.get_start_address() << " - 0x"
                         << dmi_data.get_end_address() << "]";
        return true;
    }

    void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end)
    {
        lazy_initialize();

        if (id >= 0 && static_cast<size_t>(id) < bound_targets.size()) {
            std::shared_ptr<target_info> ti = bound_targets[id];
            if (ti && ti->use_offset) {
                start += ti->address;
                end += ti->address;
            }
        }

        for (unsigned int i = 0; i < target_socket.size(); ++i) {
            target_socket[i]->invalidate_direct_mem_ptr(start, end);
        }
    }

    std::string txn_to_str(tlm::tlm_generic_payload& trans, bool is_callback = false, bool found_callback = false)
    {
        std::stringstream ss;
        std::string cmd = txn_command_str(trans);
        if (is_callback && found_callback) {
            if (cmd != "IGNORE" && cmd != "UNKNOWN") {
                if ((trans.get_response_status() == tlm::TLM_INCOMPLETE_RESPONSE))
                    ss << "[PRE-" << cmd << "] Register callbacks [txn]: ";
                else if ((trans.get_response_status() == tlm::TLM_OK_RESPONSE))
                    ss << "[POST-" << cmd << "] Register callbacks [txn]: ";
            }
        } else if (found_callback) {
            ss << "[REG-MEM] (" << cmd << " TO REG-MEM BEFORE " << cmd << " CB) [txn]: ";
        } else {
            ss << "[REG-MEM] (NO CB FOUND) [txn]: ";
        }

        ss << scp::scp_txn_tostring(trans);
        return ss.str();
    }

    std::string txn_command_str(const tlm::tlm_generic_payload& trans)
    {
        std::string cmd;
        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            cmd = "READ";
            break;
        case tlm::TLM_WRITE_COMMAND:
            cmd = "WRITE";
            break;
        case tlm::TLM_IGNORE_COMMAND:
            cmd = "IGNORE";
            break;
        default:
            cmd = "UNKNOWN";
            break;
        }
        return cmd;
    }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
    const std::string& get_module_name(const tlm::tlm_generic_payload& trans, uint64_t& mod_addr, bool& mod_found)
    {
        static const std::string empty;
        if (mod_addr_name_map.empty()) {
            return empty;
        }
        uint64_t addr = trans.get_address();
        auto search = mod_addr_name_map.equal_range(addr);
        if (search.first != mod_addr_name_map.end() && search.first->first == addr) {
            if ((addr - search.first->first) < search.first->second.first) {
                mod_addr = search.first->first;
                mod_found = true;
                return search.first->second.second;
            }
        } else if (search.second != mod_addr_name_map.begin()) {
            auto expected_node = std::prev(search.second);
            if ((addr - expected_node->first) < expected_node->second.first) {
                mod_addr = expected_node->first;
                mod_found = true;
                return expected_node->second.second;
            } else {
                mod_found = false;
            }
        } else {
            mod_found = false;
        }
        return empty;
    }
#endif

    bool do_callbacks(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        if (cb_targets.empty()) {
            return false;
        }

        sc_dt::uint64 addr = trans.get_address();
        sc_dt::uint64 len = trans.get_data_length();
        std::shared_ptr<target_info> ti; // Use shared_ptr

        auto search = cb_targets.equal_range(addr);
        if (search.first != cb_targets.end() && search.first->first == addr) {
            if ((addr - search.first->first) < search.first->second->size) {
                ti = search.first->second;
            }
        } else if (search.second != cb_targets.begin()) {
            auto expected_node = std::prev(search.second);
            if ((addr - expected_node->first) < expected_node->second->size) {
                ti = expected_node->second;
            } else {
                return false;
            }
        } else {
            return false;
        }
        SCP_TRACEALL(()) << "call b_transport: " << txn_to_str(trans, true, true);
        if (ti->use_offset) trans.set_address(addr - ti->address);
        initiator_socket[ti->index]->b_transport(trans, delay);
        if (ti->use_offset) trans.set_address(addr);
        SCP_TRACEALL(()) << "b_transport returned : " << txn_to_str(trans, true, true);
        if (trans.get_response_status() <= tlm::TLM_GENERIC_ERROR_RESPONSE) {
            SCP_WARN(()) << "Accesing register callback at addr: " << std::hex << addr
                         << " returned with: " << trans.get_response_string();
        }
        return true;
    }

    std::shared_ptr<target_info> decode_address(tlm::tlm_generic_payload& trans) override
    {
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();
        sc_dt::uint64 len = trans.get_data_length();

        for (auto ti : mem_targets) { // Iterate over shared_ptrs
            if (addr >= ti->address && (addr - ti->address) < ti->size) {
                return ti;
            }
        }
        return nullptr;
    }

protected:
    virtual void before_end_of_elaboration() override
    {
        if (!lazy_init) lazy_initialize();
    }

protected:
    void lazy_initialize() override
    {
        if (initialized) return;
        initialized = true;

        for (auto& ti_ptr : bound_targets) { // Iterate over shared_ptrs
            std::string name = ti_ptr->name;
            std::string src;

            ti_ptr->address = gs::cci_get<uint64_t>(m_broker, name + ".address");
            ti_ptr->size = gs::cci_get<uint64_t>(m_broker, name + ".size");
            ti_ptr->use_offset = gs::cci_get_d<bool>(m_broker, name + ".relative_addresses", true);
            ti_ptr->is_callback = gs::cci_get_d<bool>(m_broker, name + ".is_callback", false);
            ti_ptr->priority = gs::cci_get_d<uint32_t>(m_broker, name + ".priority", 0); // Added missing priority

            SCP_INFO(()) << "Address map " << ti_ptr->name + " at" << " address "
                         << "0x" << std::hex << ti_ptr->address << " size "
                         << "0x" << std::hex << ti_ptr->size << (ti_ptr->use_offset ? " (with relative address) " : "")
                         << (ti_ptr->is_callback ? " (callback) " : "");

            if (ti_ptr->is_callback) {
                auto insertion_pair = cb_targets.insert(std::make_pair(ti_ptr->address, ti_ptr)); // Insert shared_ptr
                if (!insertion_pair.second)
                    SCP_FATAL(()) << "a CB register with the same adress: 0x" << std::hex << ti_ptr->address
                                  << " was already bound!";
            } else {
                if (!mem_targets.empty()) {
                    for (const auto& mem : mem_targets) { // Iterate over shared_ptrs
                        if (((ti_ptr->address >= mem->address) && ((ti_ptr->address - mem->address) < mem->size)) ||
                            ((ti_ptr->address < mem->address) && ((ti_ptr->address + ti_ptr->size) > mem->address))) {
                            SCP_WARN(()) << ti_ptr->name << " overlaps with " << mem->name;
                        }
                    }
                }
                mem_targets.push_back(ti_ptr); // Add shared_ptr
            }
        }
    }

public:
    explicit reg_router(
        const sc_core::sc_module_name& nm,
        const std::map<uint64_t, std::pair<uint64_t, std::string>>& p_mod_addr_name_map =
            std::map<uint64_t, std::pair<uint64_t, std::string>>(),
        const std::function<void(bool, uint64_t)>& pre_b_transport_callback = std::function<void(bool, uint64_t)>(),
        cci::cci_broker_handle broker = cci::cci_get_broker())
        : sc_core::sc_module(nm)
        , initiator_socket("initiator_socket", [&](std::string s) -> void { register_boundto(s); })
        , target_socket("target_socket")
        , m_broker(broker)
        , lazy_init("lazy_init", false, "Initialize the reg_router lazily (eg. during simulation rather than BEOL)")
        , mod_addr_name_map(p_mod_addr_name_map)
        , m_pre_b_transport_callback(pre_b_transport_callback)
    {
        SCP_DEBUG(()) << "reg_router constructed";

        target_socket.register_b_transport(this, &reg_router::b_transport);
        target_socket.register_transport_dbg(this, &reg_router::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &reg_router::get_direct_mem_ptr);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &reg_router::invalidate_direct_mem_ptr);
        SCP_DEBUG((DMI)) << "reg_router Initializing DMI SCP reporting";
    }

    reg_router() = delete;

    reg_router(const reg_router&) = delete;

    ~reg_router() = default;

public:
    // make the sockets public for binding
    initiator_socket_type initiator_socket;
    tlm_utils::multi_passthrough_target_socket<reg_router<BUSWIDTH>, BUSWIDTH> target_socket;
    cci::cci_broker_handle m_broker;
    cci::cci_param<bool> lazy_init;

private:
    static sc_dt::uint64 end_address(const target_info& ti)
    {
        if (ti.size == 0) {
            return ti.address;
        }

        const sc_dt::uint64 max = std::numeric_limits<sc_dt::uint64>::max();
        if (ti.address > max - (ti.size - 1)) {
            return max;
        }

        return ti.address + ti.size - 1;
    }

    std::vector<std::shared_ptr<target_info>> mem_targets;
    std::map<sc_dt::uint64, std::shared_ptr<target_info>> cb_targets;
    std::map<uint64_t, std::pair<uint64_t, std::string>> mod_addr_name_map;
    std::function<void(bool, uint64_t)> m_pre_b_transport_callback;
    bool initialized = false;
};
} // namespace gs

extern "C" void module_register();
#endif
