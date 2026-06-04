/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <qemu-instance.h>
#include <ports/target.h>
#include <ports/target-signal-socket.h>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <cc3xx_core.h>

class qemu_cc3xx : public sc_core::sc_module
{
    using core_type = qbox::cc3xx::core;
    using MemTxResult = qemu::MemoryRegionOps::MemTxResult;
    using MemTxAttrs = qemu::MemoryRegionOps::MemTxAttrs;

    static constexpr uint64_t DEFAULT_WINDOW_SIZE = 0x2000;

    class qemu_container : public qemu::Object
    {
    public:
        static constexpr const char* const TYPE = "container";

        qemu_container() = default;
        qemu_container(const qemu_container& o) = default;
        qemu_container(const Object& o): Object(o) {}
    };

    struct qemu_memory_if : public core_type::memory_if {
        explicit qemu_memory_if(qemu_cc3xx& owner): m_owner(owner) {}

        bool read(uint64_t address, uint8_t* data, unsigned int len) override
        {
            return m_owner.memory_read(address, data, len);
        }

        bool write(uint64_t address, const uint8_t* data, unsigned int len) override
        {
            return m_owner.memory_write(address, data, len);
        }

        qemu_cc3xx& m_owner;
    };

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<unsigned int> p_trace_skip;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<uint64_t> p_trace_address_min;
    cci::cci_param<std::string> p_stats_file;
    cci::cci_param<unsigned int> p_stats_interval;
    cci::cci_param<uint64_t> p_size;

    QemuTargetSocket<> target_socket;
    tlm_utils::simple_initiator_socket<qemu_cc3xx, DEFAULT_TLM_BUSWIDTH> initiator_socket;
    TargetSignalSocket<bool> reset;

private:
    QemuInstance& m_inst;
    qemu_container m_container;
    qemu::MemoryRegion m_region;
    qemu::MemoryRegionOpsPtr m_ops;
    std::shared_ptr<qemu::AddressSpace> m_system_as;
    core_type m_core;
    qemu_memory_if m_memory;
    MemTxAttrs m_current_attrs;
    bool m_stats_error_reported = false;

    static MemTxResult map_status(core_type::access_status status)
    {
        switch (status) {
        case core_type::access_status::ok:
            return qemu::MemoryRegionOps::MemTxOK;
        case core_type::access_status::address_error:
            return qemu::MemoryRegionOps::MemTxDecodeError;
        case core_type::access_status::command_error:
        default:
            return qemu::MemoryRegionOps::MemTxError;
        }
    }

    bool tlm_memory_access(tlm::tlm_command command, uint64_t address,
                           uint8_t* data, unsigned int len)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    bool memory_read(uint64_t address, uint8_t* data, unsigned int len)
    {
        if (m_system_as &&
            m_system_as->read(address, data, len, m_current_attrs) ==
                qemu::MemoryRegionOps::MemTxOK) {
            return true;
        }

        return tlm_memory_access(tlm::TLM_READ_COMMAND, address, data, len);
    }

    bool memory_write(uint64_t address, const uint8_t* data, unsigned int len)
    {
        if (m_system_as &&
            m_system_as->write(address, data, len, m_current_attrs) ==
                qemu::MemoryRegionOps::MemTxOK) {
            return true;
        }

        return tlm_memory_access(tlm::TLM_WRITE_COMMAND, address,
                                 const_cast<uint8_t*>(data), len);
    }

    void sync_core_config()
    {
        core_type::trace_config trace;

        trace.enabled = p_trace.get_value();
        trace.limit = p_trace_limit.get_value();
        trace.skip = p_trace_skip.get_value();
        trace.filter = p_trace_filter.get_value();
        trace.address_min = p_trace_address_min.get_value();
        m_core.set_trace_config(trace);
        m_core.set_stats_interval(p_stats_file.get_value().empty() ? 0 :
                                  p_stats_interval.get_value());
    }

    MemTxResult qemu_read(uint64_t addr, uint64_t* data,
                          unsigned int len, MemTxAttrs attrs)
    {
        std::array<uint8_t, sizeof(uint64_t)> buffer{};
        core_type::access_result result;

        if (data == nullptr || len > buffer.size()) {
            return qemu::MemoryRegionOps::MemTxDecodeError;
        }

        sync_core_config();
        m_current_attrs = attrs;
        result = m_core.read(addr, buffer.data(), len, attrs.debug);
        if (result.status == core_type::access_status::ok) {
            uint64_t value = 0;
            std::memcpy(&value, buffer.data(), len);
            *data = value;
        }
        return map_status(result.status);
    }

    MemTxResult qemu_write(uint64_t addr, uint64_t data,
                           unsigned int len, MemTxAttrs attrs)
    {
        std::array<uint8_t, sizeof(uint64_t)> buffer{};
        core_type::access_result result;

        if (len > buffer.size()) {
            return qemu::MemoryRegionOps::MemTxDecodeError;
        }

        std::memcpy(buffer.data(), &data, len);
        sync_core_config();
        m_current_attrs = attrs;
        result = m_core.write(addr, buffer.data(), len, attrs.debug);
        return map_status(result.status);
    }

    void report_stats_error_once(const char* action, const std::string& path)
    {
        if (m_stats_error_reported) {
            return;
        }

        std::cerr << name() << " unable to " << action
                  << " stats_file=" << path << std::endl;
        m_stats_error_reported = true;
    }

    void write_stats_file()
    {
        const std::string path = p_stats_file.get_value();
        if (path.empty()) {
            return;
        }

        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            report_stats_error_once("open", path);
            return;
        }

        m_core.write_stats_json(out, name());
    }

    void doreset(bool value)
    {
        if (value) {
            m_core.reset(true);
            write_stats_file();
        }
    }

public:
    qemu_cc3xx(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qemu_cc3xx(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qemu_cc3xx(const sc_core::sc_module_name& name, QemuInstance& inst)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_skip("trace_skip", 0)
        , p_trace_filter("trace_filter", "all")
        , p_trace_address_min("trace_address_min", 0)
        , p_stats_file("stats_file", "")
        , p_stats_interval("stats_interval", 0)
        , p_size("size", DEFAULT_WINDOW_SIZE)
        , target_socket("target_socket", inst)
        , initiator_socket("initiator_socket")
        , reset("reset")
        , m_inst(inst)
        , m_container(inst.get().object_new_unparented<qemu_container>())
        , m_region(inst.get().object_new_unparented<qemu::MemoryRegion>())
        , m_ops(inst.get().memory_region_ops_new())
        , m_system_as(inst.get().address_space_get_system_memory())
        , m_core(this->name())
        , m_memory(*this)
    {
        m_ops->set_read_callback([this](uint64_t addr, uint64_t* data,
                                         unsigned int len, MemTxAttrs attrs) {
            return qemu_read(addr, data, len, attrs);
        });
        m_ops->set_write_callback([this](uint64_t addr, uint64_t data,
                                          unsigned int len, MemTxAttrs attrs) {
            return qemu_write(addr, data, len, attrs);
        });
        m_ops->set_max_access_size(8);
        m_region.init_io(m_container, this->name(), p_size.get_value(), m_ops);
        target_socket.init_with_mr(m_region);
        m_core.set_memory(&m_memory);
        m_core.set_stats_flush_callback([this] { write_stats_file(); });
        reset.register_value_changed_cb([this](bool value) { doreset(value); });
    }

    ~qemu_cc3xx() override
    {
        write_stats_file();
    }
};

extern "C" void module_register();
