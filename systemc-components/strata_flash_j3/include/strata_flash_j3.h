/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cci_configuration>
#include <loader.h>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class strata_flash_j3 : public sc_core::sc_module
{
    static constexpr uint8_t CMD_READ_ARRAY = 0xff;
    static constexpr uint8_t CMD_READ_ID_CODE = 0x90;
    static constexpr uint8_t CMD_READ_QUERY = 0x98;
    static constexpr uint8_t CMD_READ_STATUS_REG = 0x70;
    static constexpr uint8_t CMD_CLEAR_STATUS_REG = 0x50;
    static constexpr uint8_t CMD_WRITE_TO_BUFFER = 0xe8;
    static constexpr uint8_t CMD_WORD_PROGRAM = 0x40;
    static constexpr uint8_t CMD_BLOCK_ERASE = 0x20;
    static constexpr uint8_t CMD_LOCK_UNLOCK = 0x60;
    static constexpr uint8_t CMD_BLOCK_ERASE_ACK = 0xd0;
    static constexpr uint8_t CMD_LOCK_BLOCK = 0x01;
    static constexpr uint8_t STATUS_READY = 0x80;
    static constexpr uint8_t MANUFACTURER_ID = 0x89;
    static constexpr uint8_t DEVICE_CODE = 0x18;
    static constexpr unsigned int WRITE_BUFFER_MAX = 256;

    enum class mode {
        read_array,
        read_id,
        read_query,
        read_status,
    };

    enum class pending_command {
        none,
        program,
        write_buffer_count,
        write_buffer_data,
        erase,
        lock,
    };

    struct dmi_range {
        uint64_t start;
        uint64_t end;
    };

    std::vector<uint8_t> m_data;
    mode m_mode = mode::read_array;
    pending_command m_pending = pending_command::none;
    uint8_t m_status = STATUS_READY;
    unsigned int m_trace_count = 0;
    std::string m_open_dmi_ranges;
    std::vector<dmi_range> m_dmi_ranges;
    bool m_dmi_ranges_valid = true;
    bool m_dmi_ranges_error_reported = false;
    bool m_dmi_granted = false;
    std::string m_open_backing_file;
    bool m_stats_collecting = false;
    bool m_stats_collecting_known = false;
    bool m_trace_enabled = false;
    unsigned int m_trace_limit_value = 64;
    bool m_enable_dmi_value = false;
    bool m_program_ff_sets_bits_value = false;
    bool m_program_ff_erases_sector_value = false;
    bool m_defer_backing_write_value = false;
    uint64_t m_sector_size_value = 0x1000;
    std::string m_backing_file_value;
    bool m_backing_dirty = false;
    uint64_t m_backing_dirty_start = 0;
    uint64_t m_backing_dirty_end = 0;
    bool m_backing_error_reported = false;
    bool m_stats_error_reported = false;
    uint64_t m_read_accesses = 0;
    uint64_t m_write_accesses = 0;
    uint64_t m_dmi_read_hints = 0;
    uint64_t m_dmi_requests = 0;
    uint64_t m_dmi_grants = 0;
    uint64_t m_dmi_reject_disabled = 0;
    uint64_t m_dmi_reject_state = 0;
    uint64_t m_dmi_reject_command = 0;
    uint64_t m_dmi_reject_range = 0;
    uint64_t m_dmi_invalidations = 0;
    uint64_t m_command_writes = 0;
    uint64_t m_read_array_cmds = 0;
    uint64_t m_read_id_cmds = 0;
    uint64_t m_read_query_cmds = 0;
    uint64_t m_read_status_cmds = 0;
    uint64_t m_clear_status_cmds = 0;
    uint64_t m_write_buffer_cmds = 0;
    uint64_t m_write_buffer_count_writes = 0;
    uint64_t m_write_buffer_data_writes = 0;
    uint64_t m_write_buffer_confirm_cmds = 0;
    uint64_t m_write_buffer_ops = 0;
    uint64_t m_write_buffer_bytes = 0;
    uint64_t m_word_program_cmds = 0;
    uint64_t m_block_erase_cmds = 0;
    uint64_t m_block_erase_ack_cmds = 0;
    uint64_t m_lock_unlock_cmds = 0;
    uint64_t m_lock_block_cmds = 0;
    uint64_t m_unknown_cmds = 0;
    uint64_t m_program_ops = 0;
    uint64_t m_program_bytes = 0;
    uint64_t m_program_changed_bytes = 0;
    uint64_t m_program_noop_bytes = 0;
    uint64_t m_compat_ff_sector_erase_ops = 0;
    uint64_t m_sector_erase_ops = 0;
    uint64_t m_sector_erase_bytes = 0;
    uint64_t m_backing_write_ops = 0;
    uint64_t m_backing_write_bytes = 0;
    uint64_t m_backing_deferred_ranges = 0;
    uint64_t m_backing_deferred_bytes = 0;
    uint64_t m_backing_flush_ops = 0;
    uint64_t m_backing_flush_bytes = 0;
    uint64_t m_last_stats_write_access = 0;
    uint64_t m_last_stats_dmi_request = 0;
    uint64_t m_write_buffer_base = 0;
    std::vector<uint8_t> m_write_buffer;
    std::vector<bool> m_write_buffer_written;
#ifdef _WIN32
    std::fstream m_backing_stream;
#else
    int m_backing_fd = -1;
    uint8_t* m_backing_map = nullptr;
    uint64_t m_backing_size = 0;
#endif

    void ensure_storage()
    {
        if (!m_data.empty()) {
            return;
        }

        m_data.assign(p_size.get_value(), 0xff);
    }

    bool in_range(uint64_t offset, unsigned int len)
    {
        ensure_storage();

        return offset <= m_data.size() && len <= m_data.size() - offset;
    }

    static std::string remove_spaces(std::string value)
    {
        value.erase(std::remove_if(value.begin(), value.end(),
                                   [](unsigned char ch) {
                                       return std::isspace(ch) != 0;
                                   }),
                    value.end());
        return value;
    }

    static bool parse_u64(const std::string& text, uint64_t& value)
    {
        if (text.empty()) {
            return false;
        }

        try {
            size_t pos = 0;
            value = std::stoull(text, &pos, 0);
            return pos == text.size();
        } catch (...) {
            return false;
        }
    }

    bool parse_dmi_range(const std::string& token, dmi_range& range)
    {
        const size_t size_separator = token.find(':');
        const size_t end_separator = token.find('-');
        uint64_t start = 0;
        uint64_t value = 0;

        if (size_separator != std::string::npos &&
            end_separator != std::string::npos) {
            return false;
        }

        if (size_separator != std::string::npos) {
            if (!parse_u64(token.substr(0, size_separator), start) ||
                !parse_u64(token.substr(size_separator + 1), value) ||
                value == 0 ||
                start > std::numeric_limits<uint64_t>::max() - (value - 1)) {
                return false;
            }
            range = {start, start + value - 1};
            return true;
        }

        if (end_separator != std::string::npos) {
            if (!parse_u64(token.substr(0, end_separator), start) ||
                !parse_u64(token.substr(end_separator + 1), value) ||
                value < start) {
                return false;
            }
            range = {start, value};
            return true;
        }

        return false;
    }

    void report_dmi_range_error_once(const std::string& spec)
    {
        if (m_dmi_ranges_error_reported) {
            return;
        }

        std::cerr << name() << " invalid dmi_ranges=" << spec
                  << "; expected comma-separated start:size or start-end"
                  << std::endl;
        m_dmi_ranges_error_reported = true;
    }

    void refresh_dmi_ranges()
    {
        ensure_storage();

        const std::string spec = remove_spaces(p_dmi_ranges.get_value());
        if (spec == m_open_dmi_ranges) {
            return;
        }

        m_open_dmi_ranges = spec;
        m_dmi_ranges.clear();
        m_dmi_ranges_valid = true;
        m_dmi_ranges_error_reported = false;

        if (spec.empty()) {
            return;
        }

        size_t token_start = 0;
        while (token_start <= spec.size()) {
            const size_t token_end = spec.find(',', token_start);
            const std::string token = spec.substr(
                token_start,
                token_end == std::string::npos ? std::string::npos :
                                                  token_end - token_start);
            dmi_range range {};

            if (token.empty() || !parse_dmi_range(token, range) ||
                range.start >= m_data.size()) {
                m_dmi_ranges.clear();
                m_dmi_ranges_valid = false;
                report_dmi_range_error_once(spec);
                return;
            }

            range.end = std::min<uint64_t>(range.end, m_data.size() - 1);
            m_dmi_ranges.push_back(range);

            if (token_end == std::string::npos) {
                break;
            }
            token_start = token_end + 1;
        }
    }

    bool find_dmi_range(uint64_t offset, unsigned int len, dmi_range& range)
    {
        ensure_storage();

        if (m_data.empty() ||
            offset >= m_data.size() ||
            (len != 0 && len > m_data.size() - offset)) {
            return false;
        }

        refresh_dmi_ranges();
        if (!m_dmi_ranges_valid) {
            return false;
        }

        const uint64_t last = len == 0 ? offset : offset + len - 1;
        if (m_open_dmi_ranges.empty()) {
            range = {0, m_data.size() - 1};
            return true;
        }

        for (const auto& candidate : m_dmi_ranges) {
            if (offset >= candidate.start && last <= candidate.end) {
                range = candidate;
                return true;
            }
        }

        return false;
    }

    bool backing_handle_open() const
    {
#ifdef _WIN32
        return m_backing_stream.is_open();
#else
        return m_backing_fd >= 0;
#endif
    }

    void close_backing_file()
    {
        flush_deferred_backing();
#ifdef _WIN32
        if (m_backing_stream.is_open()) {
            m_backing_stream.close();
        }
#else
        if (m_backing_map != nullptr) {
            ::munmap(m_backing_map, static_cast<size_t>(m_backing_size));
            m_backing_map = nullptr;
            m_backing_size = 0;
        }
        if (m_backing_fd >= 0) {
            ::close(m_backing_fd);
            m_backing_fd = -1;
        }
#endif
        m_open_backing_file.clear();
    }

    void report_backing_error_once(const std::string& action, const std::string& path)
    {
        if (m_backing_error_reported) {
            return;
        }

        std::cerr << name() << " unable to " << action
                  << " backing_file=" << path
                  << " error=" << std::strerror(errno) << std::endl;
        m_backing_error_reported = true;
    }

    bool collect_stats()
    {
        if (!m_stats_collecting_known) {
            m_stats_collecting =
                p_stats_interval.get_value() != 0 ||
                !p_stats_file.get_value().empty();
            m_stats_collecting_known = true;
        }
        return m_stats_collecting;
    }

    void invalidate_stats_collecting()
    {
        m_stats_collecting_known = false;
    }

    void refresh_hot_params()
    {
        m_trace_enabled = p_trace.get_value();
        m_trace_limit_value = p_trace_limit.get_value();
        m_enable_dmi_value = p_enable_dmi.get_value();
        m_program_ff_sets_bits_value = p_program_ff_sets_bits.get_value();
        m_program_ff_erases_sector_value = p_program_ff_erases_sector.get_value();
        m_defer_backing_write_value = p_defer_backing_write.get_value();
        m_sector_size_value = p_sector_size.get_value();
        m_backing_file_value = p_backing_file.get_value();
        invalidate_stats_collecting();
    }

    void register_hot_param_callbacks()
    {
        p_trace.register_post_write_callback(
            [this](const auto& ev) { m_trace_enabled = ev.new_value; });
        p_trace_limit.register_post_write_callback(
            [this](const auto& ev) { m_trace_limit_value = ev.new_value; });
        p_enable_dmi.register_post_write_callback(
            [this](const auto& ev) { m_enable_dmi_value = ev.new_value; });
        p_program_ff_sets_bits.register_post_write_callback(
            [this](const auto& ev) {
                m_program_ff_sets_bits_value = ev.new_value;
            });
        p_program_ff_erases_sector.register_post_write_callback(
            [this](const auto& ev) {
                m_program_ff_erases_sector_value = ev.new_value;
            });
        p_defer_backing_write.register_post_write_callback(
            [this](const auto& ev) {
                flush_deferred_backing();
                m_defer_backing_write_value = ev.new_value;
            });
        p_sector_size.register_post_write_callback(
            [this](const auto& ev) { m_sector_size_value = ev.new_value; });
        p_backing_file.register_post_write_callback(
            [this](const auto& ev) {
                flush_deferred_backing();
                m_backing_file_value = ev.new_value;
            });
        p_stats_file.register_post_write_callback(
            [this](const auto&) { invalidate_stats_collecting(); });
        p_stats_interval.register_post_write_callback(
            [this](const auto&) { invalidate_stats_collecting(); });
    }

    void count_stat(uint64_t& counter, uint64_t delta = 1)
    {
        if (collect_stats()) {
            counter += delta;
        }
    }

    bool ensure_backing_file()
    {
        const std::string& path = m_backing_file_value;

        if (path.empty()) {
            close_backing_file();
            return false;
        }

        if (backing_handle_open() && path == m_open_backing_file) {
            return true;
        }

        close_backing_file();
        m_backing_error_reported = false;
#ifdef _WIN32
        m_backing_stream.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!m_backing_stream) {
            report_backing_error_once("open", path);
            return false;
        }
#else
        m_backing_fd = ::open(path.c_str(), O_RDWR);
        if (m_backing_fd < 0) {
            report_backing_error_once("open", path);
            return false;
        }
        struct stat backing_stat {};
        if (::fstat(m_backing_fd, &backing_stat) != 0 || backing_stat.st_size <= 0) {
            report_backing_error_once("stat", path);
            close_backing_file();
            return false;
        }
        m_backing_size = static_cast<uint64_t>(backing_stat.st_size);
        void* map = ::mmap(nullptr,
                           static_cast<size_t>(m_backing_size),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           m_backing_fd,
                           0);
        if (map == MAP_FAILED) {
            report_backing_error_once("map", path);
            close_backing_file();
            return false;
        }
        m_backing_map = static_cast<uint8_t*>(map);
#endif
        m_open_backing_file = path;
        return true;
    }

    void mark_backing_dirty(uint64_t offset, uint64_t len)
    {
        if (len == 0) {
            return;
        }

        const uint64_t end = offset + len - 1;
        if (!m_backing_dirty) {
            m_backing_dirty_start = offset;
            m_backing_dirty_end = end;
            m_backing_dirty = true;
            return;
        }

        m_backing_dirty_start = std::min(m_backing_dirty_start, offset);
        m_backing_dirty_end = std::max(m_backing_dirty_end, end);
    }

    void flush_deferred_backing()
    {
        if (!m_backing_dirty) {
            return;
        }

        const uint64_t offset = m_backing_dirty_start;
        const uint64_t len = m_backing_dirty_end - m_backing_dirty_start + 1;
        m_backing_dirty = false;

        if (m_backing_file_value.empty() || !write_backing_range_now(offset, len)) {
            return;
        }

        count_stat(m_backing_flush_ops);
        count_stat(m_backing_flush_bytes, len);
    }

    void write_backing_range(uint64_t offset, uint64_t len)
    {
        if (len == 0 || m_backing_file_value.empty()) {
            return;
        }

        if (offset > m_data.size() || len > m_data.size() - offset) {
            return;
        }

        count_stat(m_backing_write_ops);
        count_stat(m_backing_write_bytes, len);

        if (m_defer_backing_write_value) {
            count_stat(m_backing_deferred_ranges);
            count_stat(m_backing_deferred_bytes, len);
            mark_backing_dirty(offset, len);
            return;
        }

        write_backing_range_now(offset, len);
    }

    bool write_backing_range_now(uint64_t offset, uint64_t len)
    {
        if (len == 0 || !ensure_backing_file()) {
            return false;
        }

        if (offset > m_data.size() || len > m_data.size() - offset) {
            return false;
        }

        const std::string path = p_backing_file.get_value();
#ifdef _WIN32
        m_backing_stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!m_backing_stream) {
            report_backing_error_once("seek", path);
            return false;
        }

        m_backing_stream.write(reinterpret_cast<const char*>(m_data.data() + offset),
                               static_cast<std::streamsize>(len));
        m_backing_stream.flush();
        if (!m_backing_stream) {
            report_backing_error_once("write", path);
            return false;
        }
#else
        if (m_backing_map == nullptr ||
            offset > m_backing_size ||
            len > m_backing_size - offset) {
            report_backing_error_once("range", path);
            return false;
        }

        std::memcpy(m_backing_map + offset, m_data.data() + offset, len);
#endif
        return true;
    }

    uint8_t read_byte(uint64_t offset)
    {
        switch (m_mode) {
        case mode::read_array:
            return m_data[offset];
        case mode::read_id:
            if (offset == 0) {
                return MANUFACTURER_ID;
            }
            if (offset == 4) {
                return DEVICE_CODE;
            }
            return 0;
        case mode::read_query:
            if (offset == 0x10) {
                return 'Q';
            }
            if (offset == 0x11) {
                return 'R';
            }
            if (offset == 0x12) {
                return 'Y';
            }
            return 0;
        case mode::read_status:
            return m_status;
        }

        return 0xff;
    }

    void program(uint64_t offset, const uint8_t* data, unsigned int len)
    {
        count_stat(m_program_ops);
        count_stat(m_program_bytes, len);

        if (is_sector_aligned_program_ff_erase(offset, data, len)) {
            count_stat(m_compat_ff_sector_erase_ops);
            erase_sector(offset);
            return;
        }

        uint64_t first_changed = len;
        uint64_t last_changed = 0;
        uint64_t changed = 0;
        for (unsigned int i = 0; i < len; ++i) {
            const uint8_t old_value = m_data[offset + i];
            uint8_t new_value = old_value & data[i];

            if (m_program_ff_sets_bits_value && data[i] == 0xff) {
                new_value = 0xff;
            }

            if (new_value == old_value) {
                continue;
            }

            m_data[offset + i] = new_value;
            ++changed;
            first_changed = std::min<uint64_t>(first_changed, i);
            last_changed = i;
        }

        count_stat(m_program_changed_bytes, changed);
        count_stat(m_program_noop_bytes, len - changed);
        if (first_changed != len) {
            write_backing_range(offset + first_changed,
                                last_changed - first_changed + 1);
        }
        m_status = STATUS_READY;
        m_mode = mode::read_status;
        m_pending = pending_command::none;
    }

    bool is_sector_aligned_program_ff_erase(uint64_t offset,
                                            const uint8_t* data,
                                            unsigned int len) const
    {
        const uint64_t sector_size = m_sector_size_value;

        return m_program_ff_erases_sector_value &&
               len == 1 &&
               data[0] == 0xff &&
               sector_size != 0 &&
               (offset % sector_size) == 0;
    }

    void erase_sector(uint64_t offset)
    {
        const uint64_t sector_size = m_sector_size_value;
        const uint64_t start = (offset / sector_size) * sector_size;
        const uint64_t end = std::min<uint64_t>(start + sector_size, m_data.size());

        count_stat(m_sector_erase_ops);
        count_stat(m_sector_erase_bytes, end - start);
        std::fill(m_data.begin() + start, m_data.begin() + end, 0xff);
        write_backing_range(start, end - start);
        m_status = STATUS_READY;
        m_mode = mode::read_status;
        m_pending = pending_command::none;
    }

    void invalidate_dmi()
    {
        if (!m_dmi_granted ||
            !m_enable_dmi_value ||
            m_data.empty() ||
            target_socket.size() == 0) {
            return;
        }

        target_socket->invalidate_direct_mem_ptr(0, m_data.size() - 1);
        count_stat(m_dmi_invalidations);
        m_dmi_granted = false;
    }

    void clear_write_buffer()
    {
        m_write_buffer.clear();
        m_write_buffer_written.clear();
        m_write_buffer_base = 0;
    }

    unsigned int count_value(const uint8_t* data, unsigned int len) const
    {
        if (len == 0) {
            return 0;
        }

        return static_cast<unsigned int>(data[0]) + 1;
    }

    bool write_buffer_complete() const
    {
        return !m_write_buffer_written.empty() &&
               std::all_of(m_write_buffer_written.begin(),
                           m_write_buffer_written.end(),
                           [](bool value) { return value; });
    }

    void begin_write_buffer(uint64_t offset, const uint8_t* data, unsigned int len)
    {
        const unsigned int count = count_value(data, len);

        count_stat(m_write_buffer_count_writes);
        if (count == 0 || count > WRITE_BUFFER_MAX ||
            offset > m_data.size() || count > m_data.size() - offset) {
            clear_write_buffer();
            m_status = STATUS_READY;
            m_mode = mode::read_status;
            m_pending = pending_command::none;
            return;
        }

        m_write_buffer_base = offset;
        m_write_buffer.assign(count, 0xff);
        m_write_buffer_written.assign(count, false);
        m_status = STATUS_READY;
        m_mode = mode::read_status;
        m_pending = pending_command::write_buffer_data;
    }

    void collect_write_buffer_data(uint64_t offset, const uint8_t* data,
                                   unsigned int len)
    {
        if (m_write_buffer.empty() ||
            offset < m_write_buffer_base ||
            offset > m_write_buffer_base + m_write_buffer.size()) {
            clear_write_buffer();
            m_pending = pending_command::none;
            return;
        }

        const uint64_t relative = offset - m_write_buffer_base;
        if (relative >= m_write_buffer.size()) {
            clear_write_buffer();
            m_pending = pending_command::none;
            return;
        }

        const unsigned int copy_len = std::min<uint64_t>(
            len,
            m_write_buffer.size() - relative);
        for (unsigned int i = 0; i < copy_len; ++i) {
            m_write_buffer[relative + i] = data[i];
            m_write_buffer_written[relative + i] = true;
        }
        count_stat(m_write_buffer_data_writes);
        m_status = STATUS_READY;
        m_mode = mode::read_status;
    }

    void confirm_write_buffer()
    {
        count_stat(m_write_buffer_confirm_cmds);
        if (write_buffer_complete()) {
            count_stat(m_write_buffer_ops);
            count_stat(m_write_buffer_bytes, m_write_buffer.size());
            program(m_write_buffer_base,
                    m_write_buffer.data(),
                    static_cast<unsigned int>(m_write_buffer.size()));
        } else {
            m_status = STATUS_READY;
            m_mode = mode::read_status;
            m_pending = pending_command::none;
        }
        clear_write_buffer();
    }

    void record_command(uint8_t command)
    {
        count_stat(m_command_writes);
        switch (command) {
        case CMD_READ_ARRAY:
            count_stat(m_read_array_cmds);
            break;
        case CMD_READ_ID_CODE:
            count_stat(m_read_id_cmds);
            break;
        case CMD_READ_QUERY:
            count_stat(m_read_query_cmds);
            break;
        case CMD_READ_STATUS_REG:
            count_stat(m_read_status_cmds);
            break;
        case CMD_CLEAR_STATUS_REG:
            count_stat(m_clear_status_cmds);
            break;
        case CMD_WRITE_TO_BUFFER:
            count_stat(m_write_buffer_cmds);
            break;
        case CMD_WORD_PROGRAM:
            count_stat(m_word_program_cmds);
            break;
        case CMD_BLOCK_ERASE:
            count_stat(m_block_erase_cmds);
            break;
        case CMD_BLOCK_ERASE_ACK:
            count_stat(m_block_erase_ack_cmds);
            break;
        case CMD_LOCK_UNLOCK:
            count_stat(m_lock_unlock_cmds);
            break;
        case CMD_LOCK_BLOCK:
            count_stat(m_lock_block_cmds);
            break;
        default:
            count_stat(m_unknown_cmds);
            break;
        }
    }

    void handle_command(uint64_t offset, uint8_t command)
    {
        record_command(command);

        switch (m_pending) {
        case pending_command::program:
            break;
        case pending_command::write_buffer_count:
        case pending_command::write_buffer_data:
            clear_write_buffer();
            m_pending = pending_command::none;
            break;
        case pending_command::erase:
            if (command == CMD_BLOCK_ERASE_ACK) {
                erase_sector(offset);
                return;
            }
            m_pending = pending_command::none;
            break;
        case pending_command::lock:
            if (command == CMD_LOCK_BLOCK || command == CMD_BLOCK_ERASE_ACK) {
                m_status = STATUS_READY;
                m_mode = mode::read_status;
                m_pending = pending_command::none;
                return;
            }
            m_pending = pending_command::none;
            break;
        case pending_command::none:
            break;
        }

        switch (command) {
        case CMD_READ_ARRAY:
            m_mode = mode::read_array;
            break;
        case CMD_READ_ID_CODE:
            m_mode = mode::read_id;
            break;
        case CMD_READ_QUERY:
            m_mode = mode::read_query;
            break;
        case CMD_READ_STATUS_REG:
            m_status = STATUS_READY;
            m_mode = mode::read_status;
            break;
        case CMD_CLEAR_STATUS_REG:
            m_status = STATUS_READY;
            m_mode = mode::read_status;
            break;
        case CMD_WRITE_TO_BUFFER:
            clear_write_buffer();
            m_status = STATUS_READY;
            m_mode = mode::read_status;
            m_pending = pending_command::write_buffer_count;
            break;
        case CMD_WORD_PROGRAM:
            m_pending = pending_command::program;
            break;
        case CMD_BLOCK_ERASE:
            m_pending = pending_command::erase;
            break;
        case CMD_LOCK_UNLOCK:
            m_pending = pending_command::lock;
            break;
        default:
            m_mode = mode::read_array;
            m_pending = pending_command::none;
            break;
        }
    }

    void write(uint64_t offset, const uint8_t* data, unsigned int len)
    {
        if (m_pending == pending_command::program) {
            program(offset, data, len);
            return;
        }
        if (m_pending == pending_command::write_buffer_count) {
            begin_write_buffer(offset, data, len);
            return;
        }
        if (m_pending == pending_command::write_buffer_data) {
            if (write_buffer_complete() &&
                len == 1 &&
                data[0] == CMD_BLOCK_ERASE_ACK) {
                confirm_write_buffer();
                return;
            }
            collect_write_buffer_data(offset, data, len);
            return;
        }

        invalidate_dmi();
        handle_command(offset, data[0]);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || len == 0 || !in_range(offset, len)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        trans.set_dmi_allowed(false);
        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            count_stat(m_read_accesses);
            for (unsigned int i = 0; i < len; ++i) {
                data[i] = read_byte(offset + i);
            }
            dmi_range range {};
            const bool dmi_allowed =
                m_enable_dmi_value &&
                m_mode == mode::read_array &&
                m_pending == pending_command::none &&
                find_dmi_range(offset, len, range);
            if (dmi_allowed) {
                count_stat(m_dmi_read_hints);
            }
            trans.set_dmi_allowed(dmi_allowed);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            count_stat(m_write_accesses);
            write(offset, data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);
        if (collect_stats()) {
            maybe_write_stats();
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void maybe_write_stats()
    {
        const unsigned int interval = p_stats_interval.get_value();

        if (interval == 0 ||
            p_stats_file.get_value().empty() ||
            m_write_accesses == 0 ||
            m_write_accesses == m_last_stats_write_access ||
            (m_write_accesses % interval) != 0) {
            return;
        }

        m_last_stats_write_access = m_write_accesses;
        write_stats_file();
    }

    void maybe_write_dmi_stats(bool force)
    {
        const unsigned int interval = p_stats_interval.get_value();

        if (interval == 0 ||
            p_stats_file.get_value().empty() ||
            m_dmi_requests == 0 ||
            (!force &&
             (m_dmi_requests == m_last_stats_dmi_request ||
              (m_dmi_requests % interval) != 0))) {
            return;
        }

        m_last_stats_dmi_request = m_dmi_requests;
        write_stats_file();
    }

    void report_stats_error_once(const std::string& action,
                                 const std::string& path)
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

        out << "{\n"
            << "  \"module\": \"" << name() << "\",\n"
            << "  \"read_accesses\": " << m_read_accesses << ",\n"
            << "  \"write_accesses\": " << m_write_accesses << ",\n"
            << "  \"dmi_read_hints\": " << m_dmi_read_hints << ",\n"
            << "  \"dmi_requests\": " << m_dmi_requests << ",\n"
            << "  \"dmi_grants\": " << m_dmi_grants << ",\n"
            << "  \"dmi_reject_disabled\": " << m_dmi_reject_disabled << ",\n"
            << "  \"dmi_reject_state\": " << m_dmi_reject_state << ",\n"
            << "  \"dmi_reject_command\": " << m_dmi_reject_command << ",\n"
            << "  \"dmi_reject_range\": " << m_dmi_reject_range << ",\n"
            << "  \"dmi_invalidations\": " << m_dmi_invalidations << ",\n"
            << "  \"command_writes\": " << m_command_writes << ",\n"
            << "  \"read_array_cmds\": " << m_read_array_cmds << ",\n"
            << "  \"read_id_cmds\": " << m_read_id_cmds << ",\n"
            << "  \"read_query_cmds\": " << m_read_query_cmds << ",\n"
            << "  \"read_status_cmds\": " << m_read_status_cmds << ",\n"
            << "  \"clear_status_cmds\": " << m_clear_status_cmds << ",\n"
            << "  \"write_buffer_cmds\": " << m_write_buffer_cmds << ",\n"
            << "  \"write_buffer_count_writes\": "
            << m_write_buffer_count_writes << ",\n"
            << "  \"write_buffer_data_writes\": "
            << m_write_buffer_data_writes << ",\n"
            << "  \"write_buffer_confirm_cmds\": "
            << m_write_buffer_confirm_cmds << ",\n"
            << "  \"write_buffer_ops\": " << m_write_buffer_ops << ",\n"
            << "  \"write_buffer_bytes\": " << m_write_buffer_bytes << ",\n"
            << "  \"word_program_cmds\": " << m_word_program_cmds << ",\n"
            << "  \"block_erase_cmds\": " << m_block_erase_cmds << ",\n"
            << "  \"block_erase_ack_cmds\": " << m_block_erase_ack_cmds << ",\n"
            << "  \"lock_unlock_cmds\": " << m_lock_unlock_cmds << ",\n"
            << "  \"lock_block_cmds\": " << m_lock_block_cmds << ",\n"
            << "  \"unknown_cmds\": " << m_unknown_cmds << ",\n"
            << "  \"program_ops\": " << m_program_ops << ",\n"
            << "  \"program_bytes\": " << m_program_bytes << ",\n"
            << "  \"program_changed_bytes\": " << m_program_changed_bytes << ",\n"
            << "  \"program_noop_bytes\": " << m_program_noop_bytes << ",\n"
            << "  \"compat_ff_sector_erase_ops\": "
            << m_compat_ff_sector_erase_ops << ",\n"
            << "  \"sector_erase_ops\": " << m_sector_erase_ops << ",\n"
            << "  \"sector_erase_bytes\": " << m_sector_erase_bytes << ",\n"
            << "  \"backing_write_ops\": " << m_backing_write_ops << ",\n"
            << "  \"backing_write_bytes\": " << m_backing_write_bytes << ",\n"
            << "  \"backing_deferred_ranges\": "
            << m_backing_deferred_ranges << ",\n"
            << "  \"backing_deferred_bytes\": "
            << m_backing_deferred_bytes << ",\n"
            << "  \"backing_flush_ops\": " << m_backing_flush_ops << ",\n"
            << "  \"backing_flush_bytes\": " << m_backing_flush_bytes << "\n"
            << "}\n";
        if (!out) {
            report_stats_error_once("write", path);
        }
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!m_trace_enabled || m_trace_count >= m_trace_limit_value) {
            return;
        }

        ++m_trace_count;
        uint32_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << " mode=" << mode_name()
                  << " pending=" << pending_name()
                  << std::dec << std::endl;
    }

    const char* mode_name() const
    {
        switch (m_mode) {
        case mode::read_array:
            return "array";
        case mode::read_id:
            return "id";
        case mode::read_query:
            return "query";
        case mode::read_status:
            return "status";
        }

        return "unknown";
    }

    const char* pending_name() const
    {
        switch (m_pending) {
        case pending_command::none:
            return "none";
        case pending_command::program:
            return "program";
        case pending_command::write_buffer_count:
            return "write_buffer_count";
        case pending_command::write_buffer_data:
            return "write_buffer_data";
        case pending_command::erase:
            return "erase";
        case pending_command::lock:
            return "lock";
        }

        return "unknown";
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<bool> p_enable_dmi;
    cci::cci_param<bool> p_program_ff_sets_bits;
    cci::cci_param<bool> p_program_ff_erases_sector;
    cci::cci_param<bool> p_defer_backing_write;
    cci::cci_param<uint64_t> p_size;
    cci::cci_param<uint64_t> p_sector_size;
    cci::cci_param<std::string> p_dmi_ranges;
    cci::cci_param<std::string> p_backing_file;
    cci::cci_param<std::string> p_stats_file;
    cci::cci_param<unsigned int> p_stats_interval;
    tlm_utils::simple_target_socket<strata_flash_j3, DEFAULT_TLM_BUSWIDTH> target_socket;
    gs::loader<> load;

    void load_image(const uint8_t* data, uint64_t offset, uint64_t len)
    {
        ensure_storage();
        if (offset > m_data.size() || len > m_data.size() - offset) {
            std::cerr << name()
                      << " load out of range offset=0x" << std::hex << offset
                      << " len=0x" << len << std::dec << std::endl;
            return;
        }
        std::memcpy(m_data.data() + offset, data, len);
    }

    explicit strata_flash_j3(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_enable_dmi("enable_dmi", false)
        , p_program_ff_sets_bits("program_ff_sets_bits", false)
        , p_program_ff_erases_sector("program_ff_erases_sector", false)
        , p_defer_backing_write("defer_backing_write", false)
        , p_size("size", 0x04000000)
        , p_sector_size("sector_size", 0x1000)
        , p_dmi_ranges("dmi_ranges", "")
        , p_backing_file("backing_file", "")
        , p_stats_file("stats_file", "")
        , p_stats_interval("stats_interval", 0)
        , target_socket("target_socket")
        , load("load", [&](const uint8_t* data, uint64_t offset, uint64_t len) -> void {
            load_image(data, offset, len);
        })
    {
        target_socket.register_b_transport(this, &strata_flash_j3::b_transport);
        target_socket.register_transport_dbg(this, &strata_flash_j3::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &strata_flash_j3::get_direct_mem_ptr);
        refresh_hot_params();
        register_hot_param_callbacks();
    }

    ~strata_flash_j3() override
    {
        flush_deferred_backing();
        write_stats_file();
        close_backing_file();
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        access(trans, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true) ? trans.get_data_length() : 0;
    }

    bool get_direct_mem_ptr(tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        dmi_range range {};

        ensure_storage();
        count_stat(m_dmi_requests);
        if (!m_enable_dmi_value) {
            count_stat(m_dmi_reject_disabled);
            if (collect_stats()) {
                maybe_write_dmi_stats(false);
            }
            return false;
        }
        if (m_mode != mode::read_array ||
            m_pending != pending_command::none) {
            count_stat(m_dmi_reject_state);
            if (collect_stats()) {
                maybe_write_dmi_stats(false);
            }
            return false;
        }
        if (trans.get_command() != tlm::TLM_READ_COMMAND &&
            trans.get_command() != tlm::TLM_IGNORE_COMMAND) {
            count_stat(m_dmi_reject_command);
            if (collect_stats()) {
                maybe_write_dmi_stats(false);
            }
            return false;
        }
        if (!find_dmi_range(offset, len, range)) {
            count_stat(m_dmi_reject_range);
            if (collect_stats()) {
                maybe_write_dmi_stats(false);
            }
            return false;
        }

        dmi_data.set_dmi_ptr(m_data.data() + range.start);
        dmi_data.set_start_address(range.start);
        dmi_data.set_end_address(range.end);
        dmi_data.set_read_latency(sc_core::SC_ZERO_TIME);
        dmi_data.set_write_latency(sc_core::SC_ZERO_TIME);
        dmi_data.allow_read();
        count_stat(m_dmi_grants);
        if (collect_stats()) {
            maybe_write_dmi_stats(m_dmi_grants == 1);
        }
        m_dmi_granted = true;
        return true;
    }
};

extern "C" void module_register();
