/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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
    std::string m_profile_file;
    bool m_profile_enabled = false;
    MemTxAttrs m_current_attrs;
    bool m_stats_error_reported = false;
    bool m_profile_error_reported = false;
    std::map<uint64_t, tlm::tlm_dmi> m_dmi_cache;
    std::mutex m_dmi_cache_mutex;

    struct profile_state {
        std::atomic<uint64_t> read_callbacks{ 0 };
        std::atomic<uint64_t> write_callbacks{ 0 };
        std::atomic<uint64_t> read_callback_ns{ 0 };
        std::atomic<uint64_t> write_callback_ns{ 0 };
        std::atomic<uint64_t> address_space_read_attempts{ 0 };
        std::atomic<uint64_t> address_space_read_hits{ 0 };
        std::atomic<uint64_t> address_space_read_ns{ 0 };
        std::atomic<uint64_t> address_space_write_attempts{ 0 };
        std::atomic<uint64_t> address_space_write_hits{ 0 };
        std::atomic<uint64_t> address_space_write_ns{ 0 };
        std::atomic<uint64_t> tlm_read_attempts{ 0 };
        std::atomic<uint64_t> tlm_read_hits{ 0 };
        std::atomic<uint64_t> tlm_read_ns{ 0 };
        std::atomic<uint64_t> tlm_write_attempts{ 0 };
        std::atomic<uint64_t> tlm_write_hits{ 0 };
        std::atomic<uint64_t> tlm_write_ns{ 0 };
        std::atomic<uint64_t> dmi_cache_read_hits{ 0 };
        std::atomic<uint64_t> dmi_cache_write_hits{ 0 };
        std::atomic<uint64_t> dmi_cache_requests{ 0 };
        std::atomic<uint64_t> dmi_cache_insertions{ 0 };
        std::atomic<uint64_t> dmi_cache_invalidations{ 0 };
    };

    profile_state m_profile;

    static uint64_t now_ns()
    {
        using clock = std::chrono::steady_clock;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count());
    }

    static bool env_flag_enabled(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return false;
        }
        const std::string text(value);
        return text != "0" && text != "false" && text != "FALSE";
    }

    static std::string profile_file()
    {
        const char* value = std::getenv("QBOX_CC3XX_PROFILE_FILE");
        return value == nullptr ? std::string() : std::string(value);
    }

    bool profile_enabled() const
    {
        return m_profile_enabled;
    }

    static void add_ns(std::atomic<uint64_t>& bucket, uint64_t start)
    {
        if (start != 0) {
            bucket.fetch_add(now_ns() - start, std::memory_order_relaxed);
        }
    }

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

    bool dmi_cache_lookup(uint64_t address, unsigned int len, tlm::tlm_dmi& dmi)
    {
        if (len == 0 || address > std::numeric_limits<uint64_t>::max() - (len - 1)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_dmi_cache_mutex);
        auto it = m_dmi_cache.upper_bound(address);
        if (it == m_dmi_cache.begin()) {
            return false;
        }

        --it;
        const uint64_t end = address + len - 1;
        if (address >= it->second.get_start_address() &&
            end <= it->second.get_end_address()) {
            dmi = it->second;
            return true;
        }
        return false;
    }

    bool dmi_cache_access(tlm::tlm_command command, uint64_t address,
                          uint8_t* data, unsigned int len)
    {
        tlm::tlm_dmi dmi;
        if (!dmi_cache_lookup(address, len, dmi)) {
            return false;
        }

        uint8_t* ptr = dmi.get_dmi_ptr() + (address - dmi.get_start_address());
        if (command == tlm::TLM_READ_COMMAND) {
            if (!dmi.is_read_allowed()) {
                return false;
            }
            std::memcpy(data, ptr, len);
            if (profile_enabled()) {
                m_profile.dmi_cache_read_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return true;
        }

        if (command == tlm::TLM_WRITE_COMMAND) {
            if (!dmi.is_write_allowed()) {
                return false;
            }
            std::memcpy(ptr, data, len);
            if (profile_enabled()) {
                m_profile.dmi_cache_write_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return true;
        }

        return false;
    }

    void request_dmi_cache(uint64_t address, unsigned int len)
    {
        if (initiator_socket.size() == 0 || len == 0) {
            return;
        }

        uint8_t dummy = 0;
        tlm::tlm_generic_payload trans;
        tlm::tlm_dmi dmi;

        trans.set_command(tlm::TLM_IGNORE_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(&dummy);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        if (profile_enabled()) {
            m_profile.dmi_cache_requests.fetch_add(1, std::memory_order_relaxed);
        }

        if (!initiator_socket->get_direct_mem_ptr(trans, dmi) || dmi.is_none_allowed()) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_dmi_cache_mutex);
        auto it = m_dmi_cache.find(dmi.get_start_address());
        if (it != m_dmi_cache.end() &&
            it->second.get_end_address() == dmi.get_end_address()) {
            return;
        }

        m_dmi_cache[dmi.get_start_address()] = dmi;
        if (profile_enabled()) {
            m_profile.dmi_cache_insertions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        std::lock_guard<std::mutex> lock(m_dmi_cache_mutex);
        auto it = m_dmi_cache.upper_bound(start);
        if (it != m_dmi_cache.begin()) {
            --it;
        }

        while (it != m_dmi_cache.end()) {
            if (it->second.get_start_address() > end) {
                break;
            }
            if (it->second.get_end_address() < start) {
                ++it;
                continue;
            }
            it = m_dmi_cache.erase(it);
            if (profile_enabled()) {
                m_profile.dmi_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    bool memory_read(uint64_t address, uint8_t* data, unsigned int len)
    {
        const bool profile = profile_enabled();
        if (m_system_as) {
            const uint64_t start = profile ? now_ns() : 0;
            if (profile) {
                m_profile.address_space_read_attempts.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_system_as->read(address, data, len, m_current_attrs) ==
                    qemu::MemoryRegionOps::MemTxOK) {
                if (profile) {
                    m_profile.address_space_read_hits.fetch_add(1, std::memory_order_relaxed);
                }
                add_ns(m_profile.address_space_read_ns, start);
                return true;
            }
            add_ns(m_profile.address_space_read_ns, start);
        }

        if (dmi_cache_access(tlm::TLM_READ_COMMAND, address, data, len)) {
            return true;
        }
        request_dmi_cache(address, len);
        if (dmi_cache_access(tlm::TLM_READ_COMMAND, address, data, len)) {
            return true;
        }

        const uint64_t start = profile ? now_ns() : 0;
        if (profile) {
            m_profile.tlm_read_attempts.fetch_add(1, std::memory_order_relaxed);
        }
        const bool ok = tlm_memory_access(tlm::TLM_READ_COMMAND, address, data, len);
        if (ok) {
            if (profile) {
                m_profile.tlm_read_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
        add_ns(m_profile.tlm_read_ns, start);
        return ok;
    }

    bool memory_write(uint64_t address, const uint8_t* data, unsigned int len)
    {
        const bool profile = profile_enabled();
        if (m_system_as) {
            const uint64_t start = profile ? now_ns() : 0;
            if (profile) {
                m_profile.address_space_write_attempts.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_system_as->write(address, data, len, m_current_attrs) ==
                    qemu::MemoryRegionOps::MemTxOK) {
                if (profile) {
                    m_profile.address_space_write_hits.fetch_add(1, std::memory_order_relaxed);
                }
                add_ns(m_profile.address_space_write_ns, start);
                return true;
            }
            add_ns(m_profile.address_space_write_ns, start);
        }

        if (dmi_cache_access(tlm::TLM_WRITE_COMMAND, address,
                             const_cast<uint8_t*>(data), len)) {
            return true;
        }
        request_dmi_cache(address, len);
        if (dmi_cache_access(tlm::TLM_WRITE_COMMAND, address,
                             const_cast<uint8_t*>(data), len)) {
            return true;
        }

        const uint64_t start = profile ? now_ns() : 0;
        if (profile) {
            m_profile.tlm_write_attempts.fetch_add(1, std::memory_order_relaxed);
        }
        const bool ok = tlm_memory_access(tlm::TLM_WRITE_COMMAND, address,
                                          const_cast<uint8_t*>(data), len);
        if (ok) {
            if (profile) {
                m_profile.tlm_write_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
        add_ns(m_profile.tlm_write_ns, start);
        return ok;
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
        m_core.set_timing_stats(env_flag_enabled("QBOX_CC3XX_TIMING_STATS") ||
                                profile_enabled());
    }

    MemTxResult qemu_read(uint64_t addr, uint64_t* data,
                          unsigned int len, MemTxAttrs attrs)
    {
        const bool profile = profile_enabled();
        const uint64_t timing = profile ? now_ns() : 0;
        std::array<uint8_t, sizeof(uint64_t)> buffer{};
        core_type::access_result result;

        if (profile) {
            m_profile.read_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
        if (data == nullptr || len > buffer.size()) {
            add_ns(m_profile.read_callback_ns, timing);
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
        add_ns(m_profile.read_callback_ns, timing);
        return map_status(result.status);
    }

    MemTxResult qemu_write(uint64_t addr, uint64_t data,
                           unsigned int len, MemTxAttrs attrs)
    {
        const bool profile = profile_enabled();
        const uint64_t timing = profile ? now_ns() : 0;
        std::array<uint8_t, sizeof(uint64_t)> buffer{};
        core_type::access_result result;

        if (profile) {
            m_profile.write_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
        if (len > buffer.size()) {
            add_ns(m_profile.write_callback_ns, timing);
            return qemu::MemoryRegionOps::MemTxDecodeError;
        }

        std::memcpy(buffer.data(), &data, len);
        sync_core_config();
        m_current_attrs = attrs;
        result = m_core.write(addr, buffer.data(), len, attrs.debug);
        add_ns(m_profile.write_callback_ns, timing);
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
            write_profile_file();
            return;
        }

        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            report_stats_error_once("open", path);
            return;
        }

        m_core.write_stats_json(out, name());
        write_profile_file();
    }

    void report_profile_error_once(const char* action, const std::string& path)
    {
        if (m_profile_error_reported) {
            return;
        }

        std::cerr << name() << " unable to " << action
                  << " profile_file=" << path << std::endl;
        m_profile_error_reported = true;
    }

    void write_profile_file()
    {
        if (m_profile_file.empty()) {
            return;
        }

        std::ofstream out(m_profile_file, std::ios::out | std::ios::trunc);
        if (!out) {
            report_profile_error_once("open", m_profile_file);
            return;
        }

        out << "{\n"
            << "  \"module\": \"" << name() << "\",\n"
            << "  \"read_callbacks\": "
            << m_profile.read_callbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"write_callbacks\": "
            << m_profile.write_callbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"read_callback_ns\": "
            << m_profile.read_callback_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"write_callback_ns\": "
            << m_profile.write_callback_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_read_attempts\": "
            << m_profile.address_space_read_attempts.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_read_hits\": "
            << m_profile.address_space_read_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_read_ns\": "
            << m_profile.address_space_read_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_write_attempts\": "
            << m_profile.address_space_write_attempts.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_write_hits\": "
            << m_profile.address_space_write_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_space_write_ns\": "
            << m_profile.address_space_write_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_read_attempts\": "
            << m_profile.tlm_read_attempts.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_read_hits\": "
            << m_profile.tlm_read_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_read_ns\": "
            << m_profile.tlm_read_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_write_attempts\": "
            << m_profile.tlm_write_attempts.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_write_hits\": "
            << m_profile.tlm_write_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_write_ns\": "
            << m_profile.tlm_write_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_cache_read_hits\": "
            << m_profile.dmi_cache_read_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_cache_write_hits\": "
            << m_profile.dmi_cache_write_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_cache_requests\": "
            << m_profile.dmi_cache_requests.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_cache_insertions\": "
            << m_profile.dmi_cache_insertions.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_cache_invalidations\": "
            << m_profile.dmi_cache_invalidations.load(std::memory_order_relaxed) << "\n"
            << "}\n";
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
        , m_profile_file(profile_file())
        , m_profile_enabled(!m_profile_file.empty())
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
        initiator_socket.register_invalidate_direct_mem_ptr(
            this, &qemu_cc3xx::invalidate_direct_mem_ptr);
        m_core.set_memory(&m_memory);
        m_core.set_stats_flush_callback([this] { write_stats_file(); });
        reset.register_value_changed_cb([this](bool value) { doreset(value); });
    }

    ~qemu_cc3xx() override
    {
        write_stats_file();
        write_profile_file();
    }
};

extern "C" void module_register();
