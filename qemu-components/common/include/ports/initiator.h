/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_PORTS_INITIATOR_H
#define _LIBQBOX_PORTS_INITIATOR_H

#include <functional>
#include <limits>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cinttypes>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include <tlm>

#include <libqemu-cxx/libqemu-cxx.h>

#include <libgssync.h>

#include <scp/report.h>

#include <qemu-instance.h>
#include <memory_services.h>
#include <tlm-extensions/qemu-memtx-attrs.h>
#include <tlm-extensions/qemu-mr-hint.h>
#include <tlm-extensions/exclusive-access.h>
#include <tlm-extensions/shmem_extension.h>
#include <tlm-extensions/underlying-dmi.h>
#include <tlm_sockets_buswidth.h>

class QemuInitiatorIface
{
public:
    using TlmPayload = tlm::tlm_generic_payload;

    virtual void initiator_customize_tlm_payload(TlmPayload& payload) = 0;
    virtual void initiator_tidy_tlm_payload(TlmPayload& payload) = 0;
    virtual sc_core::sc_time initiator_get_local_time() = 0;
    virtual void initiator_set_local_time(const sc_core::sc_time&) = 0;
    virtual void initiator_async_run(qemu::Cpu::AsyncJobFn job) = 0;
};

/**
 * @class QemuInitiatorSocket<>
 *
 * @brief TLM-2.0 initiator socket specialisation for QEMU AddressSpace mapping
 *
 * @details This class is used to expose a QEMU AddressSpace object as a
 *          standard TLM-2.0 initiator socket. It creates a root memory region
 *          to map the whole address space, receives I/O accesses to it and
 *          forwards them as standard TLM-2.0 transactions.
 */
template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class QemuInitiatorSocket
    : public tlm::tlm_initiator_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>,
      public tlm::tlm_bw_transport_if<>
{
private:
    std::mutex m_mutex;
    std::vector<std::pair<sc_dt::uint64, sc_dt::uint64>> m_ranges;
    std::thread::id m_thread_id;

public:
    SCP_LOGGER(());

    using TlmInitiatorSocket = tlm::tlm_initiator_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1,
                                                         sc_core::SC_ZERO_OR_MORE_BOUND>;
    using TlmPayload = tlm::tlm_generic_payload;
    using MemTxResult = qemu::MemoryRegionOps::MemTxResult;
    using MemTxAttrs = qemu::MemoryRegionOps::MemTxAttrs;
    using DmiRegion = QemuInstanceDmiManager::DmiRegion;
    using DmiRegionAlias = QemuInstanceDmiManager::DmiRegionAlias;
    using QemuContainer = QemuInstanceDmiManager::QemuContainer;

    using DmiRegionAliasKey = uint64_t;

protected:
    QemuInstance& m_inst;
    QemuInitiatorIface& m_initiator;
    qemu::Device m_dev;
    gs::runonsysc m_on_sysc;
    int reentrancy = 0;
    std::string m_profile_file;
    bool m_profile_enabled = false;
    uint64_t m_profile_flush_interval = 0;

    struct profile_state {
        std::atomic<uint64_t> total_accesses{ 0 };
        std::atomic<uint64_t> read_accesses{ 0 };
        std::atomic<uint64_t> write_accesses{ 0 };
        std::atomic<uint64_t> bytes{ 0 };
        std::atomic<uint64_t> errors{ 0 };
        std::atomic<uint64_t> finished_errors{ 0 };
        std::atomic<uint64_t> read_fastpath{ 0 };
        std::atomic<uint64_t> exclusive_direct{ 0 };
        std::atomic<uint64_t> reentrant_direct{ 0 };
        std::atomic<uint64_t> debug{ 0 };
        std::atomic<uint64_t> regular{ 0 };
        std::atomic<uint64_t> local_fastpath{ 0 };
        std::atomic<uint64_t> read_fastpath_ns{ 0 };
        std::atomic<uint64_t> direct_ns{ 0 };
        std::atomic<uint64_t> debug_ns{ 0 };
        std::atomic<uint64_t> regular_ns{ 0 };
        std::atomic<uint64_t> local_fastpath_ns{ 0 };
        std::atomic<uint64_t> dmi_allowed{ 0 };
        std::atomic<uint64_t> dmi_hint_requests{ 0 };
        std::atomic<uint64_t> dmi_valid{ 0 };
        std::atomic<uint64_t> dmi_invalid{ 0 };
        std::atomic<uint64_t> dmi_alias_added{ 0 };
        std::atomic<uint64_t> dmi_alias_existing{ 0 };
        std::atomic<uint64_t> dmi_iommu_regions{ 0 };
        std::atomic<uint64_t> dmi_mmio_region_hits{ 0 };
        std::atomic<uint64_t> dmi_mapped_fallbacks{ 0 };
        std::atomic<uint64_t> dmi_nomap_fallbacks{ 0 };
        std::atomic<uint64_t> dmi_last_addr{ 0 };
        std::atomic<uint64_t> dmi_last_start{ 0 };
        std::atomic<uint64_t> dmi_last_end{ 0 };
        std::atomic<uint64_t> direct_file_aliases{ 0 };
        std::atomic<uint64_t> direct_file_alias_bytes{ 0 };
    };

    profile_state m_profile;
    struct addr_profile_bucket {
        uint64_t total = 0;
        uint64_t reads = 0;
        uint64_t writes = 0;
        uint64_t bytes = 0;
        uint64_t errors = 0;
    };

    bool m_addr_profile_enabled = false;
    uint64_t m_addr_profile_shift = 12;
    uint64_t m_addr_profile_limit = 64;
    std::unordered_map<uint64_t, addr_profile_bucket> m_addr_profile;

    std::atomic<bool> m_finished = false;

    std::shared_ptr<qemu::AddressSpace> m_as;
    std::shared_ptr<qemu::MemoryListener> m_listener;
    std::map<uint64_t, std::shared_ptr<qemu::IOMMUMemoryRegion>> m_mmio_mrs;

    class m_mem_obj
    {
    public:
        std::shared_ptr<qemu::MemoryRegion> m_root;
        m_mem_obj(qemu::LibQemu& inst) { m_root.reset(new qemu::MemoryRegion(inst.object_new<qemu::MemoryRegion>())); }
        m_mem_obj(std::shared_ptr<qemu::MemoryRegion> memory): m_root(std::move(memory)) {}
    };
    m_mem_obj* m_r = nullptr;

    struct DirectFileAlias {
        uint64_t address;
        uint64_t size;
        uint64_t file_offset;
        uint64_t region_address;
        uint64_t map_offset;
        uint64_t map_size;
        std::string path;
        bool read_only;
        uint8_t* map_ptr;
        uint8_t* ptr;
        QemuContainer container;
        qemu::MemoryRegion mr;

        DirectFileAlias(qemu::LibQemu& inst, uint64_t address, uint64_t size,
                        const std::string& path, uint64_t file_offset,
                        bool read_only, int priority)
            : address(address)
            , size(size)
            , file_offset(file_offset)
            , region_address(0)
            , map_offset(file_offset & ~uint64_t{ 0xfff })
            , map_size(size + (file_offset - map_offset))
            , path(path)
            , read_only(read_only)
            , map_ptr(nullptr)
            , ptr(nullptr)
            , container(inst.object_new_unparented<QemuContainer>())
            , mr(inst.object_new_unparented<qemu::MemoryRegion>())
        {
            if (size == 0 || map_size < size) {
                SCP_FATAL("QemuInitiatorSocket.DirectFileAlias")
                    << "Invalid direct file alias size for " << path;
            }
            if (address < file_offset - map_offset) {
                SCP_FATAL("QemuInitiatorSocket.DirectFileAlias")
                    << "Invalid direct file alias address/offset for " << path;
            }

            map_ptr = gs::MemoryServices::get().map_file_join(path, map_size, map_offset);
            ptr = map_ptr + (file_offset - map_offset);
            region_address = address - (file_offset - map_offset);
            mr.init_ram_ptr(container, "direct-file-alias", map_size, map_ptr);
            if (read_only) {
                mr.set_readonly(true);
            }
            mr.set_priority(priority);
        }

        DirectFileAlias(const DirectFileAlias&) = delete;
        DirectFileAlias& operator=(const DirectFileAlias&) = delete;
    };

    std::vector<std::unique_ptr<DirectFileAlias>> m_direct_file_aliases;

    // we use an ordered map to find and combine elements
    std::map<DmiRegionAliasKey, DmiRegionAlias::Ptr> m_dmi_aliases;
    std::vector<DmiRegionAlias::Ptr> m_retired_dmi_aliases;
    std::vector<std::shared_ptr<qemu::DmiRegionBase>> m_retired_iommu_aliases;
    using AliasesIterator = std::map<DmiRegionAliasKey, DmiRegionAlias::Ptr>::iterator;

    // Mutable overload: ordered map (std::map-like) floor lookup
    template <class Map>
    static inline auto find_region(Map& table, uint64_t addr) -> typename Map::iterator
    {
        if (table.empty()) return table.end();

        auto it = table.upper_bound(addr);
        if (it != table.begin()) {
            --it;
            const auto& e = it->second;
            const uint64_t masked_addr = (addr & ~e.addr_mask);

            if (masked_addr == it->first) {
                return it;
            }
        }
        return table.end();
    }

    void init_payload(TlmPayload& trans, tlm::tlm_command command, uint64_t addr, uint64_t* val, unsigned int size)
    {
        trans.set_command(command);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(val));
        trans.set_data_length(size);
        trans.set_streaming_width(size);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        m_initiator.initiator_customize_tlm_payload(trans);
    }

    static std::string trim_copy(const std::string& text)
    {
        const auto start = text.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }

        const auto end = text.find_last_not_of(" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    static bool parse_u64(const std::string& text, uint64_t& value)
    {
        const std::string trimmed = trim_copy(text);
        if (trimmed.empty()) {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const auto parsed = std::strtoull(trimmed.c_str(), &end, 0);
        if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
            return false;
        }

        value = static_cast<uint64_t>(parsed);
        return true;
    }

    static std::map<uint64_t, uint64_t> parse_mmio_read_fastpath()
    {
        std::map<uint64_t, uint64_t> entries;
        const char* env = std::getenv("QBOX_MMIO_READ_FASTPATH");
        if (env == nullptr || *env == '\0') {
            return entries;
        }

        std::stringstream stream(env);
        std::string item;
        while (std::getline(stream, item, ',')) {
            const auto sep = item.find('=');
            if (sep == std::string::npos) {
                continue;
            }

            uint64_t address = 0;
            uint64_t value = 0;
            if (!parse_u64(item.substr(0, sep), address) ||
                !parse_u64(item.substr(sep + 1), value)) {
                continue;
            }

            entries[address] = value;
        }

        return entries;
    }

    static const std::map<uint64_t, uint64_t>& mmio_read_fastpath_entries()
    {
        static const auto entries = parse_mmio_read_fastpath();
        return entries;
    }

    static std::vector<std::pair<uint64_t, uint64_t>> parse_mmio_direct_fastpath_ranges()
    {
        std::vector<std::pair<uint64_t, uint64_t>> ranges;
        const char* env = std::getenv("QBOX_MMIO_DIRECT_FASTPATH_RANGES");
        if (env == nullptr || *env == '\0') {
            return ranges;
        }

        std::stringstream stream(env);
        std::string item;
        while (std::getline(stream, item, ',')) {
            item = trim_copy(item);
            if (item.empty()) {
                continue;
            }

            uint64_t start = 0;
            uint64_t end = 0;
            const auto size_sep = item.find(':');
            const auto end_sep = item.find('-');
            if (size_sep != std::string::npos) {
                uint64_t size = 0;
                if (!parse_u64(item.substr(0, size_sep), start) ||
                    !parse_u64(item.substr(size_sep + 1), size) ||
                    size == 0 || start > std::numeric_limits<uint64_t>::max() - (size - 1)) {
                    continue;
                }
                end = start + size - 1;
            } else if (end_sep != std::string::npos) {
                if (!parse_u64(item.substr(0, end_sep), start) ||
                    !parse_u64(item.substr(end_sep + 1), end) ||
                    end < start) {
                    continue;
                }
            } else {
                continue;
            }

            ranges.emplace_back(start, end);
        }

        return ranges;
    }

    static const std::vector<std::pair<uint64_t, uint64_t>>& mmio_direct_fastpath_ranges()
    {
        static const auto ranges = parse_mmio_direct_fastpath_ranges();
        return ranges;
    }

    static std::string profile_dir()
    {
        const char* value = std::getenv("QBOX_QEMU_INITIATOR_PROFILE_DIR");
        return value == nullptr ? std::string() : std::string(value);
    }

    static std::string sanitize_name(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());
        for (unsigned char c : name) {
            if (std::isalnum(c) || c == '-' || c == '_') {
                result.push_back(static_cast<char>(c));
            } else {
                result.push_back('_');
            }
        }
        return result.empty() ? std::string("qemu-initiator") : result;
    }

    static std::string make_profile_file(const std::string& name, const void* self)
    {
        const std::string dir = profile_dir();
        if (dir.empty()) {
            return "";
        }

        std::ostringstream path;
        path << dir << "/" << sanitize_name(name) << "-" << self << ".json";
        return path.str();
    }

    static uint64_t profile_flush_interval()
    {
        const char* value = std::getenv("QBOX_PROFILE_FLUSH_INTERVAL");
        if (value == nullptr || *value == '\0') {
            return 65536;
        }

        char* end = nullptr;
        errno = 0;
        const uint64_t parsed = std::strtoull(value, &end, 0);
        if (errno != 0 || end == value || *end != '\0') {
            return 65536;
        }
        return parsed;
    }

    static bool env_enabled(const char* name, bool default_value = false)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return default_value;
        }

        const std::string text = trim_copy(value);
        return text == "1" || text == "true" || text == "TRUE" ||
               text == "yes" || text == "on";
    }

    static uint64_t env_u64(const char* name, uint64_t default_value)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return default_value;
        }

        uint64_t parsed = 0;
        return parse_u64(value, parsed) ? parsed : default_value;
    }

    static uint64_t now_ns()
    {
        using clock = std::chrono::steady_clock;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count());
    }

    static void add_ns(std::atomic<uint64_t>& bucket, uint64_t start)
    {
        if (start != 0) {
            bucket.fetch_add(now_ns() - start, std::memory_order_relaxed);
        }
    }

    void profile_record(tlm::tlm_command command, unsigned int size,
                        std::atomic<uint64_t>& path_count,
                        std::atomic<uint64_t>& path_ns,
                        uint64_t start, MemTxResult result)
    {
        if (!m_profile_enabled) {
            return;
        }

        const uint64_t total =
            m_profile.total_accesses.fetch_add(1, std::memory_order_relaxed) + 1;
        m_profile.bytes.fetch_add(size, std::memory_order_relaxed);
        if (command == tlm::TLM_READ_COMMAND) {
            m_profile.read_accesses.fetch_add(1, std::memory_order_relaxed);
        } else if (command == tlm::TLM_WRITE_COMMAND) {
            m_profile.write_accesses.fetch_add(1, std::memory_order_relaxed);
        }
        if (result != qemu::MemoryRegionOps::MemTxOK) {
            m_profile.errors.fetch_add(1, std::memory_order_relaxed);
        }
        path_count.fetch_add(1, std::memory_order_relaxed);
        add_ns(path_ns, start);
        if (m_profile_flush_interval != 0 &&
            (total % m_profile_flush_interval) == 0) {
            write_profile_file();
        }
    }

    void profile_addr_record(tlm::tlm_command command, uint64_t addr,
                             unsigned int size, MemTxResult result)
    {
        if (!m_profile_enabled || !m_addr_profile_enabled) {
            return;
        }

        const uint64_t key = addr >> m_addr_profile_shift;
        auto& bucket = m_addr_profile[key];
        bucket.total++;
        bucket.bytes += size;
        if (command == tlm::TLM_READ_COMMAND) {
            bucket.reads++;
        } else if (command == tlm::TLM_WRITE_COMMAND) {
            bucket.writes++;
        }
        if (result != qemu::MemoryRegionOps::MemTxOK) {
            bucket.errors++;
        }
    }

    static std::string json_hex(uint64_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << value;
        return out.str();
    }

    static bool try_mmio_read_fastpath(tlm::tlm_command command, uint64_t addr, uint64_t* val, unsigned int size,
                                       MemTxAttrs attrs)
    {
        if (command != tlm::TLM_READ_COMMAND || attrs.debug || val == nullptr ||
            size == 0 || size > sizeof(*val)) {
            return false;
        }

        const auto& entries = mmio_read_fastpath_entries();
        const auto it = entries.find(addr);
        if (it == entries.end()) {
            return false;
        }

        const uint64_t mask = size == sizeof(*val)
                                  ? std::numeric_limits<uint64_t>::max()
                                  : ((uint64_t{1} << (size * 8)) - 1);
        *val = it->second & mask;
        return true;
    }

    static bool use_mmio_direct_fastpath(uint64_t addr, unsigned int size, MemTxAttrs attrs)
    {
        if (attrs.debug || size == 0) {
            return false;
        }
        if (addr > std::numeric_limits<uint64_t>::max() - (size - 1)) {
            return false;
        }

        const uint64_t end = addr + size - 1;
        for (const auto& range : mmio_direct_fastpath_ranges()) {
            if (range.first <= addr && end <= range.second) {
                return true;
            }
        }
        return false;
    }

    static bool is_power_of_two(uint64_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    static bool split_direct_file_alias_entry(const std::string& entry,
                                              std::vector<std::string>& fields)
    {
        fields.clear();
        size_t start = 0;
        for (int index = 0; index < 4; ++index) {
            const size_t sep = entry.find(':', start);
            if (sep == std::string::npos) {
                return false;
            }
            fields.push_back(trim_copy(entry.substr(start, sep - start)));
            start = sep + 1;
        }
        fields.push_back(trim_copy(entry.substr(start)));
        return fields.size() == 5;
    }

    void add_direct_file_alias(uint64_t address, uint64_t size,
                               const std::string& path, uint64_t file_offset,
                               bool read_only, int priority)
    {
        if (m_r == nullptr || !m_r->m_root) {
            SCP_FATAL(()) << "Cannot add direct file alias before initiator init";
        }
        if (size == 0) {
            SCP_FATAL(()) << "Invalid zero-sized direct file alias for " << path;
        }
        if (address > std::numeric_limits<uint64_t>::max() - (size - 1)) {
            SCP_FATAL(()) << "Direct file alias range overflows at 0x" << std::hex << address;
        }

        std::unique_ptr<DirectFileAlias> alias(new DirectFileAlias(
            m_inst.get(), address, size, path, file_offset, read_only, priority));

        SCP_INFO(()) << "Adding direct file alias [0x" << std::hex << address
                     << "-0x" << (address + size - 1) << "] file=" << path
                     << " offset=0x" << file_offset
                     << " region=[0x" << alias->region_address
                     << "-0x" << (alias->region_address + alias->map_size - 1) << "]"
                     << " map_offset=0x" << alias->map_offset
                     << " access=" << (read_only ? "ro" : "rw")
                     << " priority=" << std::dec << priority;
        m_r->m_root->add_subregion_overlap(alias->mr, alias->region_address);
        m_profile.direct_file_aliases.fetch_add(1, std::memory_order_relaxed);
        m_profile.direct_file_alias_bytes.fetch_add(size, std::memory_order_relaxed);
        m_direct_file_aliases.push_back(std::move(alias));
    }

    static uint64_t mask_from_page_shift(uint64_t shift)
    {
        if (shift >= 63) {
            return std::numeric_limits<uint64_t>::max();
        }
        return (uint64_t{1} << shift) - 1;
    }

    static uint64_t dmi_translation_mask(uint64_t base_addr, uint64_t addr,
                                         const tlm::tlm_dmi& dmi_data,
                                         uint64_t min_page_shift)
    {
        uint64_t mask = mask_from_page_shift(min_page_shift);
        const uint64_t dmi_start = dmi_data.get_start_address();
        const uint64_t dmi_end = dmi_data.get_end_address();
        if (dmi_end < dmi_start || dmi_start < base_addr) {
            return mask;
        }

        const uint64_t dmi_size = dmi_end - dmi_start + 1;
        const uint64_t dmi_iova_start = dmi_start - base_addr;
        if (is_power_of_two(dmi_size) &&
            (dmi_iova_start & (dmi_size - 1)) == 0 &&
            dmi_iova_start <= addr && addr <= dmi_iova_start + dmi_size - 1) {
            mask = dmi_size - 1;
        }

        return mask;
    }

    void add_dmi_mr_alias(DmiRegionAlias::Ptr alias)
    {
        SCP_INFO(()) << "Adding " << *alias;
        qemu::MemoryRegion alias_mr = alias->get_alias_mr();
        alias_mr.set_priority(1);
        m_r->m_root->add_subregion_overlap(alias_mr, alias->get_start());
        alias->set_installed();
    }

    void del_dmi_mr_alias(const DmiRegionAlias::Ptr alias)
    {
        if (!alias->is_installed()) {
            return;
        }
        SCP_INFO(()) << "Removing " << *alias;
        m_r->m_root->del_subregion(alias->get_alias_mr());
        alias->clear_installed();
    }

    void clear_retired_aliases_locked()
    {
        m_retired_dmi_aliases.clear();
        m_retired_iommu_aliases.clear();
    }

    void invalidate_readonly_alias_after_write(uint64_t addr, unsigned int size)
    {
        if (size == 0) {
            return;
        }

        uint64_t end = addr + size - 1;
        if (end < addr) {
            end = std::numeric_limits<uint64_t>::max();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        invalidate_single_range(addr, end);
    }

    void remove_iommu_io_alias(std::shared_ptr<qemu::IOMMUMemoryRegion> iommumr, uint64_t alias_start)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = iommumr->m_dmi_aliases_io.find(alias_start);
        if (it == iommumr->m_dmi_aliases_io.end()) {
            return;
        }

        DmiRegionAlias::Ptr alias = std::static_pointer_cast<DmiRegionAlias>(it->second);
        if (alias->is_installed()) {
            SCP_INFO(()) << "Removing " << *alias;
            iommumr->m_root_io.del_subregion(alias->get_alias_mr());
            alias->clear_installed();
        }

        m_retired_iommu_aliases.push_back(it->second);
        iommumr->m_dmi_aliases_io.erase(it);
    }

    /**
     * @brief Use DMI data to set up a qemu IOMMU translate
     *
     * @param te pointer to translate block that will be filled in
     * @param iommumr memory region through which translation is being done
     * @param base_addr base address of the iommumr memory region in the address space
     * @param addr address to translate
     * @param flags QEMU read/write request flags
     * @param idx   index of translation block.
     *
     */
    void dmi_translate(qemu::IOMMUMemoryRegion::IOMMUTLBEntry* te, std::shared_ptr<qemu::IOMMUMemoryRegion> iommumr,
                       uint64_t base_addr, uint64_t addr, qemu::IOMMUMemoryRegion::IOMMUAccessFlags flags, int idx)
    {
        TlmPayload ltrans;
        uint64_t tmp;

        SCP_TRACE(())("dmi_translate for base 0x{:x} addr 0x{:x}", base_addr, addr);

        /*
         * Fast path : check to see if the TE is already cached, if so return it straight away.
         * NB, this happens rarely, as QEMU will cache the result itself, but
         * if the region returned previously covers more than a single min_page_sz, then QEMU will re-request
         * for the other pages.
         */
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            clear_retired_aliases_locked();

            auto it = find_region(iommumr->m_mapped_te, addr);
            if (it != iommumr->m_mapped_te.end()) {
                *te = it->second;
                // This is the DMI cache, so we must re-construct the actual required TE from this case.
                // It will likely have a 'stale' address.
                te->iova = addr;
                te->translated_addr = (it->second.translated_addr & ~(it->second.addr_mask)) +
                                      (addr & (it->second.addr_mask));

                SCP_TRACE(())
                ("FAST translate for 0x{:x} :  0x{:x}->0x{:x} (mask 0x{:x}) perm={}", addr, te->iova,
                 te->translated_addr, te->addr_mask, te->perm);

                return;
            }
        }
        /*
         * Slow path, use DMI to investigate the memory, and see what sort of TE we can set up
         *
         * There are 3 options
         * 1/ a real IOMMU region that should be mapped into the IOMMU address space
         * 2/ a 'dmi-able' region which is not an IOMMU (e.g. local memory)
         * 3/ a 'non-dmi-able' object (e.g. an MMIO device) - a minimum page size will be used for this.
         *
         */

        SCP_DEBUG(())("Doing Translate for {:x} (Absolute 0x{:x})", addr, addr + base_addr);

        gs::UnderlyingDMITlmExtension lu_dmi;
        init_payload(ltrans, tlm::TLM_IGNORE_COMMAND, base_addr + addr, &tmp, 0);
        ltrans.set_extension(&lu_dmi);
        tlm::tlm_dmi ldmi_data;

        if ((*this)->get_direct_mem_ptr(ltrans, ldmi_data)) {
            if (lu_dmi.has_dmi(gs::tlm_dmi_ex::dmi_iommu)) {
                // Add te to 'special' IOMMU address space
                tlm::tlm_dmi lu_dmi_data = lu_dmi.get_last(gs::tlm_dmi_ex::dmi_iommu);
                if (0 == iommumr->m_dmi_aliases_te.count(lu_dmi_data.get_start_address())) {
                    qemu::RcuReadLock l_rcu_read_lock = m_inst.get().rcu_read_lock_new();
                    // take our own memory here, dont use an alias as
                    // we may have different sizes for the underlying DMI

                    DmiRegion region = DmiRegion(lu_dmi_data, 0, m_inst.get());
                    SCP_DEBUG(())
                    ("Adding IOMMU DMI Region  start 0x{:x} - 0x{:x}", lu_dmi_data.get_start_address(),
                     lu_dmi_data.get_start_address() + region.get_size() - 1);
                    iommumr->m_root_te.add_subregion(region.get_mut_mr(), lu_dmi_data.get_start_address());
                    iommumr->m_dmi_aliases_te[lu_dmi_data.get_start_address()] = std::make_shared<DmiRegion>(region);
                }

                te->target_as = iommumr->m_as_te->get_ptr();
                auto mask = ldmi_data.get_end_address() - ldmi_data.get_start_address();
                te->addr_mask = mask;
                te->iova = addr;
                te->translated_addr = (lu_dmi_data.get_start_address() +
                                       (ldmi_data.get_dmi_ptr() - lu_dmi_data.get_dmi_ptr())) +
                                      (addr & mask);
                te->perm = (qemu::IOMMUMemoryRegion::IOMMUAccessFlags)ldmi_data.get_granted_access();

                SCP_DEBUG(())
                ("Translate IOMMU 0x{:x}->0x{:x} (mask 0x{:x})", te->iova, te->translated_addr, te->addr_mask);

            } else {
                // no underlying DMI, add a 1-1 passthrough to normal address space
                if (0 == iommumr->m_dmi_aliases_io.count(ldmi_data.get_start_address())) {
                    qemu::RcuReadLock l_rcu_read_lock = m_inst.get().rcu_read_lock_new();
                    QemuInstanceDmiManager::DmiWriteCallback write_cb;
                    if (ldmi_data.is_read_allowed() && !ldmi_data.is_write_allowed()) {
                        uint64_t alias_start = ldmi_data.get_start_address();
                        write_cb = [this, iommumr, alias_start](uint64_t addr, uint64_t data, unsigned int size,
                                                                MemTxAttrs attrs) {
                            MemTxResult result = qemu_io_write(addr, data, size, attrs);
                            remove_iommu_io_alias(iommumr, alias_start);
                            return result;
                        };
                    }
                    DmiRegionAlias::Ptr alias =
                        m_inst.get_dmi_manager().get_new_region_alias(ldmi_data, -1, 0, write_cb);
                    SCP_DEBUG(()) << "Adding DMI Region alias " << *alias;
                    qemu::MemoryRegion alias_mr = alias->get_alias_mr();
                    alias_mr.set_priority(1);
                    iommumr->m_root_io.add_subregion_overlap(alias_mr, alias->get_start());
                    alias->set_installed();
                    iommumr->m_dmi_aliases_io[alias->get_start()] = alias;
                }
                auto mask = dmi_translation_mask(base_addr, addr, ldmi_data,
                                                 iommumr->min_page_sz);
                te->target_as = iommumr->m_as_io->get_ptr();
                te->addr_mask = mask;
                te->iova = addr & ~mask;
                te->translated_addr = (addr & ~mask) + base_addr;
                te->perm = (qemu::IOMMUMemoryRegion::IOMMUAccessFlags)ldmi_data.get_granted_access();

                SCP_DEBUG(())
                ("Translate 1-1 passthrough 0x{:x}->0x{:x} (mask 0x{:x})", te->iova, te->translated_addr,
                 te->addr_mask);
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            /* It is possible that this region overlaps an existing region (a 1-1 MMIO).
             * QEMU will likely take this region, but both should be valid, and the other
             * region will be removed in due course
             */
            iommumr->m_mapped_te[addr & ~te->addr_mask] = *te;

            SCP_DEBUG(())
            ("Caching TE at addr 0x{:x} (mask {:x})", addr & ~te->addr_mask, te->addr_mask);

        } else {
            // No DMI at all, either an MMIO, or a DMI failure, setup for a 1-1 translation for the minimal page
            // in the normal address space

            te->target_as = iommumr->m_as_io->get_ptr();
            te->addr_mask = (1 << iommumr->min_page_sz) - 1;
            te->iova = addr & ~te->addr_mask;
            te->translated_addr = (addr & ~te->addr_mask) + base_addr;
            te->perm = qemu::IOMMUMemoryRegion::IOMMU_RW;

            if (iommumr->m_mapped_te.find(addr & ~te->addr_mask) != iommumr->m_mapped_te.end()) {
                SCP_FATAL(())("Trying to add a 1-1 mapping over an existing mapping");
            }
            // We need to add it so we can remove it (!)
            iommumr->m_mapped_te[addr & ~te->addr_mask] = *te;

            SCP_DEBUG(())
            ("Translate 1-1 limited passthrough  0x{:x}->0x{:x} (mask 0x{:x})", te->iova, te->translated_addr,
             te->addr_mask);
        }
        ltrans.clear_extension(&lu_dmi);
    }

    /**
     * @brief Request a DMI region, ask the QEMU instance DMI manager for a DMI
     * region alias for it and map it on the CPU address space.
     *
     * @param trans DMI allowed transation
     *
     * @details Ideal, the whole operation could be done on the SystemC thread for
     * simplicity. Unfortunately, QEMU misbehaves if the memory region alias
     * subregion is mapped on the root MR by another thread than the CPU
     * thread. This is related to some internal QEMU code taking different
     * paths depending on the current thread. Basically if the subregion add is
     * not done on the CPU thread, the modification won't be visible
     * immediately by the CPU so the next memory access may go through the I/O
     * path again.
     *
     * On the other hand we SHOULD do a bunch on the SystemC thread to ensure
     * validity of the DMI region and thus the alias until the point where we
     *  effectively map the alias onto the root MR. This is why we first create
     * the alias on the SystemC thread and return it to the CPU thread. Once
     * we're back to the CPU thread, we lock the DMI manager again and check
     * for the alias validity flag. If an invalidation happened in between,
     * this flag will be false and we know we can throw the entire DMI request.
     *
     * If the alias is valid after we took the lock, we can map it. If an
     * invalidation must occur, it will be done after we release the lock.
     *
     * @note We choose to protect all such areas with a mutex, allowing us to
     * process everything on the QEMU thread.
     *
     * @see QemuInstanceDmiManager for more information on why we need a global
     *      MR per DMI region.
     *
     * @note All DMI activity MUST happen from the CPU thread (from an MMIO read/write or 'safe
     * work') For 7.2 this may need to be safe aync work ????????
     *
     * @note Needs to be called with iothread locked as it will be doing several
     * updates and we dont want multiple DMI's
     *
     * @returns The DMI descriptor for the corresponding DMI region - this is used to help construct memory maps only.
     */
    tlm::tlm_dmi check_dmi_hint_locked(TlmPayload& trans)
    {
        assert(trans.is_dmi_allowed());
        if (m_profile_enabled) {
            m_profile.dmi_hint_requests.fetch_add(1, std::memory_order_relaxed);
            m_profile.dmi_last_addr.store(trans.get_address(), std::memory_order_relaxed);
        }
        tlm::tlm_dmi dmi_data;
        int shm_fd = -1;
        uint64_t shm_offset = 0;
        auto addr = trans.get_address();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            clear_retired_aliases_locked();
        }
        /* We got a DMI hint, lets just make sure this isn't in an existing m_mmio_mrs region
         * Because if it is, what probably happened is that we took the MMIO path
         * rather than getting a translation done. We should invalidate any mmio 1-1 mapping
         * for this address */
        for (auto m : m_mmio_mrs) {
            auto mr_start = m.first;
            auto mr_end = m.first + m.second->get_size() - 1;
            if (mr_start <= addr && addr <= mr_end) {
                // Use masked floor lookup to find the TE covering 'addr'
                auto it = find_region(m.second->m_mapped_te, addr - mr_start);
                if (it != m.second->m_mapped_te.end()) {
                    uint64_t removed_addr = it->first + mr_start;
                    SCP_TRACE(())("Suspected MMIO Region removed 0x{:x} (mask 0x{:x})", removed_addr,
                                  it->second.addr_mask);
                    m.second->iommu_unmap(&(it->second));
                    m.second->m_mapped_te.erase(it);
                }
                SCP_TRACE(())("Suspected MMIO Region(s) removed arround address 0x{:x}", addr);
                if (m_profile_enabled) {
                    m_profile.dmi_mmio_region_hits.fetch_add(1, std::memory_order_relaxed);
                }
                return dmi_data;
            }
        }

        SCP_INFO(()) << "DMI request for address 0x" << std::hex << trans.get_address();

        // It is 'safer' from the SystemC perspective to  m_on_sysc.run_on_sysc([this,
        // &trans]{...}).
        gs::UnderlyingDMITlmExtension u_dmi;

        trans.set_extension(&u_dmi);
        bool dmi_valid = (*this)->get_direct_mem_ptr(trans, dmi_data);
        trans.clear_extension(&u_dmi);
        if (!dmi_valid) {
            if (m_profile_enabled) {
                m_profile.dmi_invalid.fetch_add(1, std::memory_order_relaxed);
            }
            SCP_INFO(())("No DMI available for {:x}", trans.get_address());
            /* this is used by the map function below
             * - a better plan may be to tag memories to be mapped so we dont need this
             */
            if (u_dmi.has_dmi(gs::tlm_dmi_ex::dmi_mapped)) {
                if (m_profile_enabled) {
                    m_profile.dmi_mapped_fallbacks.fetch_add(1, std::memory_order_relaxed);
                }
                tlm::tlm_dmi first_map = u_dmi.get_first(gs::tlm_dmi_ex::dmi_mapped);
                return first_map;
            }
            if (u_dmi.has_dmi(gs::tlm_dmi_ex::dmi_nomap)) {
                if (m_profile_enabled) {
                    m_profile.dmi_nomap_fallbacks.fetch_add(1, std::memory_order_relaxed);
                }
                tlm::tlm_dmi first_nomap = u_dmi.get_first(gs::tlm_dmi_ex::dmi_nomap);
                return first_nomap;
            }
            return dmi_data;
        }

        /*
         * This is the 'special' case of IOMMU's which require an IOMMU memory region setup
         * The IOMMU will be constructed here, but not populated - that will happen in the callback
         * There will be a 'pair' of new regions, one to hold non iommu regions within this space,
         * the other to hold iommu regions themselves.
         *
         * In extreme circumstances, if the IOMMU DMI to this region previously failed, we may have
         * ended up with a normal DMI region here, which needs removing. We do that here, and then simply
         * return and wait for a new access to sort things out.
         */
        if (u_dmi.has_dmi(gs::tlm_dmi_ex::dmi_iommu)) {
            /* We have an IOMMU request setup an IOMMU region */
            if (m_profile_enabled) {
                m_profile.dmi_iommu_regions.fetch_add(1, std::memory_order_relaxed);
            }
            SCP_INFO(())("IOMMU DMI available for {:x}", trans.get_address());

            /* The first mapped DMI will be the scope of the IOMMU region from our perspective */
            tlm::tlm_dmi first_map = u_dmi.get_first(gs::tlm_dmi_ex::dmi_mapped);

            uint64_t start = first_map.get_start_address();
            uint64_t size = (first_map.get_end_address() - first_map.get_start_address()) + 1;
            auto itr = m_mmio_mrs.find(start);
            if (itr == m_mmio_mrs.end()) {
                // Better check for overlapping iommu's - they must be banned     !!

                qemu::RcuReadLock rcu_read_lock = m_inst.get().rcu_read_lock_new();

                /* invalidate any 'old' regions we happen to have mapped previously */
                invalidate_single_range(start, start + size - 1);

                SCP_INFO(())
                ("Adding IOMMU for VA 0x{:x} [0x{:x} - 0x{:x}]", trans.get_address(), start, start + size - 1);

                using namespace std::placeholders;
                qemu::MemoryRegionOpsPtr ops;
                ops = m_inst.get().memory_region_ops_new();
                ops->set_read_callback(std::bind(&QemuInitiatorSocket::qemu_io_read, this, _1, _2, _3, _4));
                ops->set_write_callback(std::bind(&QemuInitiatorSocket::qemu_io_write, this, _1, _2, _3, _4));
                ops->set_max_access_size(8);

                auto iommumr = std::make_shared<qemu::IOMMUMemoryRegion>(
                    m_inst.get().template object_new_unparented<qemu::IOMMUMemoryRegion>());

                iommumr->init(*iommumr, "dmi-manager-iommu", size, ops,
                              [=](qemu::IOMMUMemoryRegion::IOMMUTLBEntry* te, uint64_t addr,
                                  qemu::IOMMUMemoryRegion::IOMMUAccessFlags flags,
                                  int idx) { dmi_translate(te, iommumr, start, addr, flags, idx); });
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_mmio_mrs[start] = iommumr;
                }
                m_r->m_root->add_subregion(*iommumr, start);

            } else {
                // Previously when looking up a TE, we failed to get the lock, so the DMI failed, we ended up in a
                // limited passthrough. Which causes us to re-arrive here.... but, with a DMI hint. Hopefully next time
                // the TE is looked up, we'll get the lock and re-establish the translation. In any case we should do
                // nothing and simply return
                // Moving to a cached TE will improve speed and prevent this from happening?
                SCP_DEBUG(())
                ("Memory request should be directed via MMIO interface {:x} {:x}", start, trans.get_address());

                //                std::lock_guard<std::mutex> lock(m_mutex);

                uint64_t start_range = itr->first;
                uint64_t end_range = itr->first + itr->second->get_size() - 1;

                invalidate_direct_mem_ptr(start_range, end_range);
            }
            return dmi_data;
        }

        gs::ShmemIDExtension* shm_ext = trans.get_extension<gs::ShmemIDExtension>();
        // it's ok that ShmemIDExtension is not added to trans as this should only happen when
        // memory is a shared memory type.
        if (shm_ext) {
            shm_fd = shm_ext->m_fd;
            shm_offset = reinterpret_cast<uintptr_t>(dmi_data.get_dmi_ptr()) - shm_ext->m_mapped_addr;
        } else {
            const uint64_t dmi_size = (dmi_data.get_end_address() - dmi_data.get_start_address()) + 1;
            gs::MemoryServices::get().get_shmem_fd_offset_for_ptr(dmi_data.get_dmi_ptr(), dmi_size, shm_fd,
                                                                  shm_offset);
        }

        SCP_INFO(()) << "DMI Adding for address 0x" << std::hex << trans.get_address();

        // The upper limit is set within QEMU by the TBU
        // e.g. 1k small pages for ARM.
        // setting to 1/2 the size of the ARM TARGET_PAGE_SIZE,
        // Comment from QEMU code:
        /* The physical section number is ORed with a page-aligned
         * pointer to produce the iotlb entries.  Thus it should
         * never overflow into the page-aligned value.
         */
#define MAX_MAP 250

        // Current function may be called by the MMIO thread which does not hold
        // any RCU read lock. This is required in case of a memory transaction
        // commit on a TCG accelerated Qemu instance
        qemu::RcuReadLock rcu_read_lock = m_inst.get().rcu_read_lock_new();

        if (m_dmi_aliases.size() > MAX_MAP) {
            SCP_FATAL(())("Too many DMI regions requested, consider using an IOMMU");
        }
        uint64_t start = dmi_data.get_start_address();
        uint64_t end = dmi_data.get_end_address();
        if (m_profile_enabled) {
            m_profile.dmi_valid.fetch_add(1, std::memory_order_relaxed);
            m_profile.dmi_last_start.store(start, std::memory_order_relaxed);
            m_profile.dmi_last_end.store(end, std::memory_order_relaxed);
        }

        if (0 == m_dmi_aliases.count(start)) {
            SCP_INFO(()) << "Adding DMI for range [0x" << std::hex << dmi_data.get_start_address() << "-0x" << std::hex
                         << dmi_data.get_end_address() << "]";

            QemuInstanceDmiManager::DmiWriteCallback write_cb;
            if (dmi_data.is_read_allowed() && !dmi_data.is_write_allowed()) {
                write_cb = [this](uint64_t addr, uint64_t data, unsigned int size, MemTxAttrs attrs) {
                    MemTxResult result = qemu_io_write(addr, data, size, attrs);
                    invalidate_readonly_alias_after_write(addr, size);
                    return result;
                };
            }

            DmiRegionAlias::Ptr alias =
                m_inst.get_dmi_manager().get_new_region_alias(dmi_data, shm_fd, shm_offset, write_cb);

            m_dmi_aliases[start] = alias;
            add_dmi_mr_alias(m_dmi_aliases[start]);
            if (m_profile_enabled) {
                m_profile.dmi_alias_added.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            if (m_profile_enabled) {
                m_profile.dmi_alias_existing.fetch_add(1, std::memory_order_relaxed);
            }
            SCP_INFO(())("Already have DMI for 0x{:x}", start);
        }
        return dmi_data;
    }

    void check_qemu_mr_hint(TlmPayload& trans)
    {
        QemuMrHintTlmExtension* ext = nullptr;
        uint64_t mapping_addr;

        trans.get_extension(ext);

        if (ext == nullptr) {
            return;
        }

        qemu::MemoryRegion target_mr(ext->get_mr());

        if (target_mr.get_inst_id() != m_dev.get_inst_id()) {
            return;
        }

        mapping_addr = trans.get_address() - ext->get_offset();

        qemu::MemoryRegion mr(m_inst.get().template object_new<qemu::MemoryRegion>());

        mr.init_alias(m_dev, "mr-alias", target_mr, 0, target_mr.get_size());
        m_r->m_root->add_subregion(mr, mapping_addr);
    }

    void do_regular_access(TlmPayload& trans)
    {
        using sc_core::sc_time;

        uint64_t addr = trans.get_address();
        sc_time now = m_initiator.initiator_get_local_time();

        m_inst.get().unlock_iothread();
        m_on_sysc.run_on_sysc([this, &trans, &now] { (*this)->b_transport(trans, now); });
        m_inst.get().lock_iothread();
        /*
         * Reset transaction address before dmi check (could be altered by
         * b_transport).
         */
        trans.set_address(addr);
        check_qemu_mr_hint(trans);
        if (trans.is_dmi_allowed()) {
            check_dmi_hint_locked(trans);
        }

        m_initiator.initiator_set_local_time(now);
    }

    void do_debug_access(TlmPayload& trans)
    {
        m_inst.get().unlock_iothread();
        m_on_sysc.run_on_sysc([this, &trans] { (*this)->transport_dbg(trans); });
        m_inst.get().lock_iothread();
    }

    void do_direct_access(TlmPayload& trans)
    {
        sc_core::sc_time now = m_initiator.initiator_get_local_time();
        (*this)->b_transport(trans, now);
    }

    void do_local_fast_access(TlmPayload& trans)
    {
        uint64_t addr = trans.get_address();
        sc_core::sc_time now = m_initiator.initiator_get_local_time();

        (*this)->b_transport(trans, now);
        trans.set_address(addr);
        check_qemu_mr_hint(trans);
        if (trans.is_dmi_allowed()) {
            check_dmi_hint_locked(trans);
        }
        m_initiator.initiator_set_local_time(now);
    }

    MemTxResult qemu_io_access(tlm::tlm_command command, uint64_t addr, uint64_t* val, unsigned int size,
                               MemTxAttrs attrs)
    {
        const uint64_t profile_start = m_profile_enabled ? now_ns() : 0;
        if (m_finished) {
            if (m_profile_enabled) {
                profile_record(command, size, m_profile.finished_errors,
                               m_profile.direct_ns, profile_start,
                               qemu::MemoryRegionOps::MemTxError);
            }
            return qemu::MemoryRegionOps::MemTxError;
        }
        if (try_mmio_read_fastpath(command, addr, val, size, attrs)) {
            profile_record(command, size, m_profile.read_fastpath,
                           m_profile.read_fastpath_ns, profile_start,
                           qemu::MemoryRegionOps::MemTxOK);
            return qemu::MemoryRegionOps::MemTxOK;
        }

        TlmPayload trans;
        init_payload(trans, command, addr, val, size);
        QemuMemTxAttrsTlmExtension attrs_ext(attrs);
        trans.set_extension(&attrs_ext);
        std::atomic<uint64_t>* path_count = &m_profile.regular;
        std::atomic<uint64_t>* path_ns = &m_profile.regular_ns;

        if (trans.get_extension<ExclusiveAccessTlmExtension>()) {
            /* in the case of an exclusive access keep the iolock (and assume NO side-effects)
             * clearly dangerous, but exclusives are not guaranteed to work on IO space anyway
             */
            path_count = &m_profile.exclusive_direct;
            path_ns = &m_profile.direct_ns;
            do_direct_access(trans);
        } else {
            bool qemu_io_locked = m_inst.g_rec_qemu_io_lock.try_lock();
            if (!qemu_io_locked && !is_on_sysc()) {
                /* Allow only a single access, but handle re-entrant code,
                 * while allowing side-effects in SystemC (e.g. calling wait)
                 * [NB re-entrant code caused via memory listeners to
                 * creation of memory regions (due to DMI) in some models]
                 */
                m_inst.get().unlock_iothread();
                m_inst.g_rec_qemu_io_lock.lock();
                qemu_io_locked = true;
                m_inst.get().lock_iothread();
            }
            reentrancy++;

            /* Force re-entrant code to use a direct access (safe for reentrancy with no side effects) */
            if (use_mmio_direct_fastpath(addr, size, attrs)) {
                path_count = &m_profile.local_fastpath;
                path_ns = &m_profile.local_fastpath_ns;
                do_local_fast_access(trans);
            } else if (reentrancy > 1) {
                path_count = &m_profile.reentrant_direct;
                path_ns = &m_profile.direct_ns;
                do_direct_access(trans);
            } else if (attrs.debug) {
                path_count = &m_profile.debug;
                path_ns = &m_profile.debug_ns;
                do_debug_access(trans);
            } else {
                path_count = &m_profile.regular;
                path_ns = &m_profile.regular_ns;
                do_regular_access(trans);
            }

            reentrancy--;
            if (qemu_io_locked) {
                m_inst.g_rec_qemu_io_lock.unlock();
            }
        }
        trans.clear_extension(&attrs_ext);
        m_initiator.initiator_tidy_tlm_payload(trans);

        MemTxResult result = qemu::MemoryRegionOps::MemTxError;
        switch (trans.get_response_status()) {
        case tlm::TLM_OK_RESPONSE:
            result = qemu::MemoryRegionOps::MemTxOK;
            break;

        case tlm::TLM_ADDRESS_ERROR_RESPONSE:
            result = qemu::MemoryRegionOps::MemTxDecodeError;
            break;

        default:
            result = qemu::MemoryRegionOps::MemTxError;
            break;
        }

        if (trans.is_dmi_allowed() && m_profile_enabled) {
            m_profile.dmi_allowed.fetch_add(1, std::memory_order_relaxed);
        }
        profile_addr_record(command, addr, size, result);
        profile_record(command, size, *path_count, *path_ns, profile_start, result);
        return result;
    }

    void write_profile_file()
    {
        if (!m_profile_enabled || m_profile_file.empty()) {
            return;
        }

        std::ofstream out(m_profile_file, std::ios::out | std::ios::trunc);
        if (!out) {
            return;
        }

        std::vector<std::pair<uint64_t, addr_profile_bucket>> addr_buckets;
        if (m_addr_profile_enabled) {
            addr_buckets.reserve(m_addr_profile.size());
            for (const auto& entry : m_addr_profile) {
                addr_buckets.push_back(entry);
            }
            std::sort(addr_buckets.begin(), addr_buckets.end(),
                      [](const auto& lhs, const auto& rhs) {
                          if (lhs.second.total != rhs.second.total) {
                              return lhs.second.total > rhs.second.total;
                          }
                          return lhs.first < rhs.first;
                      });
            if (addr_buckets.size() > m_addr_profile_limit) {
                addr_buckets.resize(m_addr_profile_limit);
            }
        }

        out << "{\n"
            << "  \"socket\": \"" << TlmInitiatorSocket::name() << "\",\n"
            << "  \"total_accesses\": "
            << m_profile.total_accesses.load(std::memory_order_relaxed) << ",\n"
            << "  \"read_accesses\": "
            << m_profile.read_accesses.load(std::memory_order_relaxed) << ",\n"
            << "  \"write_accesses\": "
            << m_profile.write_accesses.load(std::memory_order_relaxed) << ",\n"
            << "  \"bytes\": "
            << m_profile.bytes.load(std::memory_order_relaxed) << ",\n"
            << "  \"errors\": "
            << m_profile.errors.load(std::memory_order_relaxed) << ",\n"
            << "  \"finished_errors\": "
            << m_profile.finished_errors.load(std::memory_order_relaxed) << ",\n"
            << "  \"read_fastpath\": "
            << m_profile.read_fastpath.load(std::memory_order_relaxed) << ",\n"
            << "  \"exclusive_direct\": "
            << m_profile.exclusive_direct.load(std::memory_order_relaxed) << ",\n"
            << "  \"reentrant_direct\": "
            << m_profile.reentrant_direct.load(std::memory_order_relaxed) << ",\n"
            << "  \"debug\": "
            << m_profile.debug.load(std::memory_order_relaxed) << ",\n"
            << "  \"regular\": "
            << m_profile.regular.load(std::memory_order_relaxed) << ",\n"
            << "  \"local_fastpath\": "
            << m_profile.local_fastpath.load(std::memory_order_relaxed) << ",\n"
            << "  \"read_fastpath_ns\": "
            << m_profile.read_fastpath_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"direct_ns\": "
            << m_profile.direct_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"debug_ns\": "
            << m_profile.debug_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"regular_ns\": "
            << m_profile.regular_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"local_fastpath_ns\": "
            << m_profile.local_fastpath_ns.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_allowed\": "
            << m_profile.dmi_allowed.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_hint_requests\": "
            << m_profile.dmi_hint_requests.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_valid\": "
            << m_profile.dmi_valid.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_invalid\": "
            << m_profile.dmi_invalid.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_alias_added\": "
            << m_profile.dmi_alias_added.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_alias_existing\": "
            << m_profile.dmi_alias_existing.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_iommu_regions\": "
            << m_profile.dmi_iommu_regions.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_mmio_region_hits\": "
            << m_profile.dmi_mmio_region_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_mapped_fallbacks\": "
            << m_profile.dmi_mapped_fallbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_nomap_fallbacks\": "
            << m_profile.dmi_nomap_fallbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_last_addr\": "
            << m_profile.dmi_last_addr.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_last_start\": "
            << m_profile.dmi_last_start.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_last_end\": "
            << m_profile.dmi_last_end.load(std::memory_order_relaxed) << ",\n"
            << "  \"direct_file_aliases\": "
            << m_profile.direct_file_aliases.load(std::memory_order_relaxed) << ",\n"
            << "  \"direct_file_alias_bytes\": "
            << m_profile.direct_file_alias_bytes.load(std::memory_order_relaxed) << ",\n"
            << "  \"address_profile_enabled\": "
            << (m_addr_profile_enabled ? "true" : "false") << ",\n"
            << "  \"address_profile_shift\": " << m_addr_profile_shift << ",\n"
            << "  \"address_profile_limit\": " << m_addr_profile_limit << ",\n"
            << "  \"address_profile\": [\n";
        for (size_t i = 0; i < addr_buckets.size(); ++i) {
            const uint64_t base = addr_buckets[i].first << m_addr_profile_shift;
            const uint64_t size = uint64_t{ 1 } << m_addr_profile_shift;
            const auto& bucket = addr_buckets[i].second;
            out << "    {"
                << "\"base\": " << base << ", "
                << "\"base_hex\": \"" << json_hex(base) << "\", "
                << "\"end_hex\": \"" << json_hex(base + size - 1) << "\", "
                << "\"total\": " << bucket.total << ", "
                << "\"reads\": " << bucket.reads << ", "
                << "\"writes\": " << bucket.writes << ", "
                << "\"bytes\": " << bucket.bytes << ", "
                << "\"errors\": " << bucket.errors << "}";
            if (i + 1 != addr_buckets.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n"
            << "}\n";
    }

public:
    MemTxResult qemu_io_read(uint64_t addr, uint64_t* val, unsigned int size, MemTxAttrs attrs)
    {
        return qemu_io_access(tlm::TLM_READ_COMMAND, addr, val, size, attrs);
    }

    MemTxResult qemu_io_write(uint64_t addr, uint64_t val, unsigned int size, MemTxAttrs attrs)
    {
        return qemu_io_access(tlm::TLM_WRITE_COMMAND, addr, &val, size, attrs);
    }

    bool is_on_sysc() const { return std::this_thread::get_id() == m_thread_id; }

    QemuInitiatorSocket(const char* name, QemuInitiatorIface& initiator, QemuInstance& inst)
        : TlmInitiatorSocket(name)
        , m_inst(inst)
        , m_initiator(initiator)
        , m_thread_id(std::this_thread::get_id())
        , m_on_sysc(sc_core::sc_gen_unique_name("initiator_run_on_sysc"))
        , m_profile_file(make_profile_file(name, this))
        , m_profile_enabled(!m_profile_file.empty())
        , m_profile_flush_interval(profile_flush_interval())
        , m_addr_profile_enabled(env_enabled("QBOX_QEMU_INITIATOR_ADDR_PROFILE"))
        , m_addr_profile_shift(env_u64("QBOX_QEMU_INITIATOR_ADDR_PROFILE_SHIFT", 12))
        , m_addr_profile_limit(env_u64("QBOX_QEMU_INITIATOR_ADDR_PROFILE_LIMIT", 64))
    {
        if (m_addr_profile_shift > 30) {
            m_addr_profile_shift = 30;
        }
        if (m_addr_profile_limit == 0) {
            m_addr_profile_limit = 64;
        }
        SCP_DEBUG(()) << "QemuInitiatorSocket constructor";
        TlmInitiatorSocket::bind(*static_cast<tlm::tlm_bw_transport_if<>*>(this));
    }

    void init(qemu::Device& dev, const char* prop)
    {
        using namespace std::placeholders;

        qemu::LibQemu& inst = m_inst.get();
        qemu::MemoryRegionOpsPtr ops;

        m_r = new m_mem_obj(inst); // oot = inst.object_new<qemu::MemoryRegion>();
        ops = inst.memory_region_ops_new();

        ops->set_read_callback(std::bind(&QemuInitiatorSocket::qemu_io_read, this, _1, _2, _3, _4));
        ops->set_write_callback(std::bind(&QemuInitiatorSocket::qemu_io_write, this, _1, _2, _3, _4));
        ops->set_max_access_size(8);

        m_r->m_root->init_io(dev, TlmInitiatorSocket::name(), std::numeric_limits<uint64_t>::max(), ops);
        dev.set_prop_link(prop, *m_r->m_root);

        m_dev = dev;
    }

    void install_direct_file_aliases(const std::string& spec, int priority = 20)
    {
        if (trim_copy(spec).empty()) {
            return;
        }

        std::stringstream stream(spec);
        std::string item;
        while (std::getline(stream, item, ';')) {
            item = trim_copy(item);
            if (item.empty()) {
                continue;
            }

            std::vector<std::string> fields;
            if (!split_direct_file_alias_entry(item, fields)) {
                SCP_FATAL(()) << "Invalid direct file alias entry: " << item;
            }

            uint64_t address = 0;
            uint64_t size = 0;
            uint64_t file_offset = 0;
            if (!parse_u64(fields[0], address) ||
                !parse_u64(fields[1], size) ||
                !parse_u64(fields[2], file_offset) ||
                size == 0) {
                SCP_FATAL(()) << "Invalid direct file alias numeric field: " << item;
            }

            const std::string access = fields[3];
            const bool read_only = access == "ro";
            if (!read_only && access != "rw") {
                SCP_FATAL(()) << "Invalid direct file alias access field: " << item;
            }
            if (fields[4].empty()) {
                SCP_FATAL(()) << "Invalid direct file alias empty path: " << item;
            }

            add_direct_file_alias(address, size, fields[4], file_offset,
                                  read_only, priority);
        }
    }

    bool direct_file_alias_ptr(uint64_t address, uint64_t size,
                               bool need_write, uint8_t*& ptr)
    {
        ptr = nullptr;
        if (size == 0 || address > std::numeric_limits<uint64_t>::max() - (size - 1)) {
            return false;
        }

        const uint64_t end = address + size - 1;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& alias : m_direct_file_aliases) {
            if (need_write && alias->read_only) {
                continue;
            }
            if (address < alias->address || end > alias->address + alias->size - 1) {
                continue;
            }
            ptr = alias->ptr + (address - alias->address);
            return ptr != nullptr;
        }
        return false;
    }

    void end_of_simulation()
    {
        write_profile_file();
        m_finished = true;
    }

    ~QemuInitiatorSocket()
    {
#if 0
    // This could happen during void end_of_simulation() but there is a race with other units trying
    // to pull down their DMI's
        if (m_r) {
            if (m_r->m_root) {
                m_r->m_root->removeSubRegions();
            }
            delete m_r;
            m_r = nullptr;
        }
#endif
        //        dmimgr_unlock();
        write_profile_file();
    }

    void qemu_map(qemu::MemoryListener& listener, uint64_t addr, uint64_t len)
    {
        // this function is relatively expensive, and called a lot, it should be done a different way and removed.
        if (m_finished) return;

        SCP_DEBUG(()) << "Mapping request for address [0x" << std::hex << addr << "-0x" << addr + len - 1 << "]";

        TlmPayload trans;
        uint64_t current_addr = addr;
        uint64_t temp;
        init_payload(trans, tlm::TLM_IGNORE_COMMAND, current_addr, &temp, 0);
        trans.set_dmi_allowed(true);

        while (current_addr < addr + len) {
            tlm::tlm_dmi dmi_data = check_dmi_hint_locked(trans);

            // Current addr is an absolute address while the dmi range might be relative
            // hence not necesseraly current_addr falls withing dmi_range address boundaries
            // TODO: is there a way to retrieve the dmi range block offset?
            SCP_INFO(()) << "0x" << std::hex << current_addr << " mapped [0x" << dmi_data.get_start_address() << "-0x"
                         << dmi_data.get_end_address() << "]";

            // The allocated range may not span the whole length required for mapping
            assert(dmi_data.get_end_address() >= current_addr);
            current_addr = dmi_data.get_end_address();
            if (current_addr >= addr + len) break; // Catch potential loop-rounds
            current_addr += 1;
            trans.set_address(current_addr);
        }

        m_initiator.initiator_tidy_tlm_payload(trans);
    }

    void init_global(qemu::Device& dev)
    {
        using namespace std::placeholders;

        qemu::LibQemu& inst = m_inst.get();
        qemu::MemoryRegionOpsPtr ops;
        ops = inst.memory_region_ops_new();

        ops->set_read_callback(std::bind(&QemuInitiatorSocket::qemu_io_read, this, _1, _2, _3, _4));
        ops->set_write_callback(std::bind(&QemuInitiatorSocket::qemu_io_write, this, _1, _2, _3, _4));
        ops->set_max_access_size(8);

        auto system_memory = inst.get_system_memory();
        system_memory->init_io(dev, TlmInitiatorSocket::name(), std::numeric_limits<uint64_t>::max() - 1, ops);
        m_r = new m_mem_obj(std::move(system_memory));

        m_as = inst.address_space_get_system_memory();
        // System memory has been changed from container to "io", this is relevant
        // for flatview, and to reflect that we can just update the topology
        m_as->update_topology();

        /* Sometimes memory regions are added to the global address space by (for instance) virt devices rempaiing
         * system memory such that they can directly access it. The mapped memory region needs it's DMI pointers
         * adjusted. qemu_map will do that remapping of the DMI.
         */
        m_listener = inst.memory_listener_new();
        m_listener->set_map_callback(std::bind(&QemuInitiatorSocket::qemu_map, this, _1, _2, _3));
        m_listener->register_as(m_as);

        m_dev = dev;
    }

    /* tlm::tlm_bw_transport_if<> */
    virtual tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase,
                                               sc_core::sc_time& t)
    {
        /* Should not be reached */
        assert(false);
        return tlm::TLM_COMPLETED;
    }

    virtual AliasesIterator remove_alias(AliasesIterator it)
    {
        DmiRegionAlias::Ptr r = it->second; /*
                                             * Invalidate this region. Do not bother with
                                             * partial invalidation as it's really not worth
                                             * it. Better let the target model returns sub-DMI
                                             * regions during future accesses.
                                             */

        /*
         * Mark the whole region this alias maps to as invalid. This has
         * the effect of marking all the other aliases mapping to the same
         * region as invalid too. If a DMI request for the same region is
         * already in progress, it will have a chance to detect it is now
         * invalid before mapping it on the QEMU root MR (see
         * check_dmi_hint comment).
         */
        // r->invalidate_region();

        assert(r->is_installed());
        //        if (!r->is_installed()) {
        /*
         * The alias is not mapped onto the QEMU root MR yet. Simply
         * skip it. It will be removed from m_dmi_aliases by
         * check_dmi_hint.
         */
        //            return it++;
        //        }

        /*
         * Remove the alias from the root MR. This is enough to perform
         * required invalidations on QEMU's side in a thread-safe manner.
         */
        del_dmi_mr_alias(r);

        /*
         * Remove the alias from the collection. The DmiRegionAlias object
         * is then destructed, leading to the destruction of the DmiRegion
         * shared pointer it contains. When no more alias reference this
         * region, it is in turn destructed, effectively destroying the
         * corresponding memory region in QEMU.
         */
        return m_dmi_aliases.erase(it);
    }

private:
    void invalidate_single_range(sc_dt::uint64 start_range, sc_dt::uint64 end_range)
    {
        auto it = m_dmi_aliases.upper_bound(start_range);

        if (it != m_dmi_aliases.begin()) {
            /*
             * Start with the preceding region, as it may already cross the
             * range we must invalidate.
             */
            it--;
        }
        while (it != m_dmi_aliases.end()) {
            DmiRegionAlias::Ptr r = it->second;

            if (r->get_start() > end_range) {
                /* We've got out of the invalidation range */
                break;
            }

            if (r->get_end() < start_range) {
                /* We are not in yet */
                it++;
                continue;
            }

            it = remove_alias(it);

            SCP_DEBUG(()) << "Invalidated region [0x" << std::hex << r->get_start() << ", 0x" << std::hex
                          << r->get_end() << "]";
        }
    }

    void invalidate_ranges_safe_cb()
    {
        if (m_finished) return;
        std::lock_guard<std::mutex> lock(m_mutex);

        SCP_DEBUG(()) << "Invalidating " << m_ranges.size() << " ranges";
        auto rit = m_ranges.begin();
        while (rit != m_ranges.end()) {
            invalidate_single_range(rit->first, rit->second);
            rit = m_ranges.erase(rit);
        }
    }

    bool region_match(uint64_t mr_rel_start, uint64_t mr_rel_end, uint64_t addr, uint64_t mask)
    {
        uint64_t end = addr + mask + 1;
        return (mr_rel_start <= end && mr_rel_end >= addr);
    }

public:
    virtual void invalidate_direct_mem_ptr(sc_dt::uint64 start_range, sc_dt::uint64 end_range)
    {
        if (m_finished) return;
        SCP_DEBUG(()) << "DMI invalidate [0x" << std::hex << start_range << ", 0x" << std::hex << end_range << "]";

        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto m : m_mmio_mrs) {
            auto mr_start = m.first;
            auto mr_end = m.first + m.second->get_size() - 1;
            // if the MR overlaps or is overlapped by the invalidation range
            if (start_range <= mr_end && mr_start <= end_range) {
                auto mr_rel_start = start_range - mr_start;
                auto mr_rel_end = end_range - mr_start;
                auto it = m.second->m_mapped_te.lower_bound(mr_rel_start);

                // Check the previous interval (it might still match)
                if (it != m.second->m_mapped_te.begin()) {
                    auto prev = std::prev(it);
                    // only checking if the start of the region is in the area requested.
                    if (region_match(mr_rel_start, mr_rel_end, prev->first, prev->second.addr_mask)) {
                        m.second->iommu_unmap(&(prev->second));
                        m.second->m_mapped_te.erase(prev);
                        SCP_TRACE(())("Region removed 0x{:x} (mask 0x{:x})", prev->first, it->second.addr_mask);
                    }
                }

                // Scan forward while region bases are <= end
                while (it != m.second->m_mapped_te.end() && it->first <= mr_rel_end) {
                    if (region_match(mr_rel_start, mr_rel_end, it->first, it->second.addr_mask)) {
                        m.second->iommu_unmap(&(it->second));
                        it = m.second->m_mapped_te.erase(it); // erase returns next iterator
                        SCP_TRACE(())("Region removed 0x{:x} (mask 0x{:x})", it->first, it->second.addr_mask);
                    } else {
                        ++it;
                    }
                }

                SCP_DEBUG(())("Region(s) removed in range [0x{:x} - 0x{:x}] from mr [0x{:x} - 0x{:x}]", start_range,
                              end_range, mr_start, mr_end);
                return;
            }
        }

        m_ranges.push_back(std::make_pair(start_range, end_range));
        m_initiator.initiator_async_run([&]() { invalidate_ranges_safe_cb(); });
    }

    virtual void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto m : m_mmio_mrs) {
            m.second->m_mapped_te.clear();
            auto it = m_dmi_aliases.begin();
            while (it != m_dmi_aliases.end()) {
                DmiRegionAlias::Ptr r = it->second;
                it = remove_alias(it);
            }
        }
    }
};

#endif
