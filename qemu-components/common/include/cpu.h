/*
 * This file is part of libqbox
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_CPU_H
#define _LIBQBOX_COMPONENTS_CPU_CPU_H

#include <sstream>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <functional>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cci_configuration>

#include <libgssync.h>
#include <libqemu-cxx/target/aarch64.h>

#include "cpu-pc-entry-observer.h"
#include "cpu-semantic-context.h"
#include "device.h"
#include "ports/initiator.h"
#include "tlm-extensions/qemu-cpu-hint.h"
#include "ports/qemu-target-signal-socket.h"

class QemuCpu : public QemuDevice, public QemuInitiatorIface, public QemuCpuSemanticContext
{
protected:
    /*
     * We have a unique copy per CPU of this extension, which is not dynamically allocated.
     * We really don't want the default implementation to call delete on it...
     */
    class QemuCpuHintTlmExtension : public ::QemuCpuHintTlmExtension
    {
    public:
        void free() override { /* leave my extension alone, TLM */ }
    };

    gs::runonsysc m_on_sysc;
    std::shared_ptr<qemu::Timer> m_deadline_timer;
    bool m_coroutines;

    qemu::Cpu m_cpu;

    gs::async_event m_qemu_kick_ev;
    sc_core::sc_event_or_list m_external_ev;
    sc_core::sc_process_handle m_sc_thread; // used for co-routines

    std::atomic<bool> m_signaled;
    std::mutex m_signaled_lock;
    std::condition_variable m_signaled_cond;

    std::shared_ptr<gs::tlm_quantumkeeper_extended> m_qk;
    std::atomic<bool> m_finished = false;
    std::atomic<bool> m_started = false;
    enum { none, start_reset, hold_reset, finish_reset } m_resetting = none;
    gs::async_event m_start_reset_done_ev;

    std::mutex m_can_delete;
    QemuCpuHintTlmExtension m_cpu_hint_ext;

    uint64_t m_quantum_ns; // For convenience

    /*
     * Outstanding async work tracking.
     *
     * When a job is queued via async_run or async_safe_run we increment this
     * counter.  The wrapper decrements it (and signals the condvar) once the
     * job has finished executing.  The destructor waits for the counter to
     * reach zero before tearing down the object so that any in-flight job
     * cannot call back into a destroyed member (e.g. m_start_reset_done_ev).
     *
     * A timeout is used as a safety valve: if the simulation is exiting
     * because the async job itself faulted and will never complete, we do not
     * want to hang forever.
     */
    std::atomic<int> m_async_work_outstanding{ 0 };
    std::mutex m_async_work_mutex;
    std::condition_variable m_async_work_cv;
    static constexpr int ASYNC_WORK_TIMEOUT_MS = 500;

    uint64_t m_pc_trace_seen = 0;
    uint64_t m_pc_trace_emitted = 0;
    std::ofstream m_pc_trace_stream;
    std::mutex m_trace_lock;
    std::vector<QemuCpuPcEntryObserver*> m_pc_entry_observers;

    const char* semantic_cpu_name() const override { return name(); }

    uint64_t get_pc() const override { return m_cpu.get_pc(); }

    uint64_t get_v7m_state(qemu::CpuArm::V7MStateField field) const override
    {
        return qemu::CpuArm(m_cpu).get_v7m_state(field);
    }

    uint64_t get_aarch64_state(qemu::CpuArm::Aarch64StateField field) const override
    {
        return qemu::CpuArm(m_cpu).get_aarch64_state(field);
    }

    bool set_v7m_state(qemu::CpuArm::V7MStateField field, uint64_t value) override
    {
        return qemu::CpuArm(m_cpu).set_v7m_state(field, value);
    }

    /*
     * Wrap @job so that m_async_work_outstanding is incremented before the
     * job is queued and decremented (with a condvar notification) after it
     * returns.  Must only be called when !m_finished so that the counter
     * cannot be incremented after the destructor has started waiting.
     */
    qemu::Cpu::AsyncJobFn make_tracked_async_job(qemu::Cpu::AsyncJobFn job)
    {
        {
            std::lock_guard<std::mutex> lock(m_async_work_mutex);
            m_async_work_outstanding++;
        }
        if (m_finished) return {}; // already shutting down
        return [this, job = std::move(job)]() mutable {
            /*
             * If the CPU is already shutting down (m_finished set by
             * end_of_simulation()), skip the job body entirely.  The job
             * will never be able to complete safely anyway (the CPU is being
             * halted/unplugged), and skipping lets the destructor's wait_for
             * exit immediately rather than burning the full timeout per CPU.
             */
            if (!m_finished) {
                job();
            }
            {
                std::lock_guard<std::mutex> lock(m_async_work_mutex);
                m_async_work_outstanding--;
            }
            m_async_work_cv.notify_all();
        };
    }

    /*
     * Request quantum keeper from instance
     */
    void create_quantum_keeper()
    {
        m_qk = m_inst.create_quantum_keeper();

        if (!m_qk) {
            SCP_FATAL(()) << "qbox : Sync policy unknown";
        }

        m_qk->reset();
    }

    /*
     * Given the quantum keeper nature (synchronous or asynchronous) and the
     * p_icount parameter, we can configure the QEMU instance accordingly.
     */
    void set_coroutine_mode()
    {
        switch (m_qk->get_thread_type()) {
        case gs::SyncPolicy::SYSTEMC_THREAD:
            m_coroutines = true;
            break;

        case gs::SyncPolicy::OS_THREAD:
            m_coroutines = false;
            break;
        }
    }

    /*
     * ---- CPU loop related methods ----
     */

    /*
     * Called by watch_external_ev and kick_cb in MTTCG mode. This keeps track
     * of an external event in case the CPU thread just released the iothread
     * and is going to call wait_for_work. This is needed to avoid missing an
     * event and going to sleep while we should effectively wake-up.
     *
     * The coroutine mode does not use this method and use the SystemC kernel
     * as a mean of synchronization. If an asynchronous event is triggered
     * while the CPU thread go to sleep, the fact that the CPU thread is also
     * the SystemC thread will ensure correct ordering of the events.
     */
    void set_signaled()
    {
        assert(!m_coroutines);
        if (m_inst.get_tcg_mode() != QemuInstance::TCG_SINGLE) {
            std::lock_guard<std::mutex> lock(m_signaled_lock);
            m_signaled = true;
            m_signaled_cond.notify_all();
        } else {
            std::lock_guard<std::mutex> lock(m_inst.g_signaled_lock);
            m_inst.g_signaled = true;
            m_inst.g_signaled_cond.notify_all();
        }
    }

    /*
     * SystemC thread watching the m_external_ev event list. Only used in MTTCG
     * mode.
     */
    void watch_external_ev()
    {
        for (;;) {
            wait(m_external_ev);
            set_signaled();
        }
    }

    /*
     * Called when the CPU is kicked. We notify the corresponding async event
     * to wake the CPU up if it was sleeping waiting for work.
     */
    void kick_cb()
    {
        SCP_TRACE(())("QEMU deadline KICK callback");
        if (m_coroutines) {
            if (!m_finished) m_qemu_kick_ev.async_notify();
        } else {
            set_signaled();
        }
    }

    /*
     * Called by the QEMU iothread when the deadline timer expires. We kick the
     * CPU out of its execution loop for it to call the end_of_loop_cb callback.
     * However, we should also handle the case that qemu is currently in 'sync'
     *  - by setting the time here, we will nudge the sync thread.
     */
    void deadline_timer_cb()
    {
        SCP_TRACE(())("QEMU deadline timer callback");
        // All syncing will be done in end_of_loop_cb
        m_cpu.kick();
        // Rearm timer for next time ....
        if (!m_finished) {
            rearm_deadline_timer();

            /* Take this opportunity to set the time */
            int64_t now = m_inst.get().get_virtual_clock();
            sc_core::sc_time sc_t = sc_core::sc_time_stamp();
            if (sc_core::sc_time(now, sc_core::SC_NS) > sc_t) {
                m_qk->set(sc_core::sc_time(now, sc_core::SC_NS) - sc_t);
            }
        }
    }

    /*
     * The CPU does not have work anymore. Pause the CPU thread until we have
     * some work to do.
     *
     * - In coroutine mode, this method runs a wait on the SystemC kernel,
     *   waiting for the m_external_ev list.
     * - In MTTCG mode, we wait on the m_signaled_cond condition, signaled when
     *   set_signaled is called.
     */
    void wait_for_work()
    {
        SCP_TRACE(())("Wait for work");
        m_qk->stop();
        if (m_finished) return;

        if (m_coroutines) {
            m_on_sysc.run_on_sysc([this]() { wait(m_external_ev); });
        } else {
            if (m_inst.get_tcg_mode() != QemuInstance::TCG_SINGLE) {
                std::unique_lock<std::mutex> lock(m_signaled_lock);
                m_signaled_cond.wait(lock, [this] { return m_signaled || m_finished; });
                m_signaled = false;
            } else {
                std::unique_lock<std::mutex> lock(m_inst.g_signaled_lock);
                m_inst.g_signaled_cond.wait(lock, [this] { return m_inst.g_signaled || m_finished; });
                m_inst.g_signaled = false;
            }
        }
        if (m_finished) return;
        SCP_TRACE(())("Have work, running CPU");
        m_qk->start();
    }

    /*
     * Set the deadline timer to trigger at the end of the time budget
     */
    void rearm_deadline_timer()
    {
        // This is a simple "every quantum" tick. Whether the QK makes use of it or not
        // is down to the sync policy
        m_deadline_timer->mod(m_inst.get().get_virtual_clock() + m_quantum_ns);
    }

    /*
     * Called before running the CPU. Lock the BQL and set the deadline timer
     * to not run beyond the time budget.
     */
    void prepare_run_cpu()
    {
        /*
         * The QEMU CPU loop expect us to enter it with the iothread mutex locked.
         * It is then unlocked when we come back from the CPU loop, in
         * sync_with_kernel().
         */

        SCP_TRACE(())("Prepare run");
        if (m_inst.get_tcg_mode() == QemuInstance::TCG_SINGLE) {
            while (!m_inst.can_run() && !m_finished) {
                wait_for_work();
            }
        } else {
            while (!m_cpu.can_run() && !m_finished) {
                if (!m_coroutines && !m_inst.is_tcg_enabled()) {
                    // For hardware accelerators (KVM/HVF), break back into the
                    // QEMU loop so the vCPU thread can handle signals delivered
                    // via ioctl. TCG does not need this.
                    SCP_TRACE(())("Stopping QK (accelerator)");
                    m_qk->stop();
                    break;
                }
                wait_for_work();
            }
        }

        if (m_started && m_resetting == none) {
            m_cpu.set_soft_stopped(false);
        }
        /*
         * The QEMU CPU loop expect us to enter it with the iothread mutex locked.
         * It is then unlocked when we come back from the CPU loop, in
         * sync_with_kernel().
         */
        m_inst.get().lock_iothread();
    }

    /*
     * Run the CPU loop. Only used in coroutine mode.
     */
    void run_cpu_loop()
    {
        auto last_vclock = m_inst.get().get_virtual_clock();
        m_cpu.loop();
        /*
         * Workaround in icount mode: sometimes, the CPU does not execute
         * on the first call of run_loop(). Give it a second chance.
         */
        for (int i = 0; i < m_inst.number_devices(); i++) {
            if ((m_inst.get().get_virtual_clock() == last_vclock) && (m_cpu.can_run())) {
                m_cpu.loop();
            } else
                break;
        }
    }

    bool guest_dmi_ptr(uint64_t address, uint64_t size, bool need_read,
                       bool need_write, uint8_t*& ptr) override
    {
        ptr = nullptr;
        if (size == 0 || size > std::numeric_limits<unsigned int>::max()) {
            return false;
        }
        if (address > std::numeric_limits<uint64_t>::max() - (size - 1)) {
            return false;
        }

        uint8_t dummy = 0;
        tlm::tlm_generic_payload trans;
        tlm::tlm_dmi dmi;
        const auto command = need_write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND;

        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(&dummy);
        trans.set_data_length(static_cast<unsigned int>(size));
        trans.set_streaming_width(static_cast<unsigned int>(size));
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        initiator_customize_tlm_payload(trans);

        if (!socket->get_direct_mem_ptr(trans, dmi) || dmi.is_none_allowed()) {
            initiator_tidy_tlm_payload(trans);
            return false;
        }
        initiator_tidy_tlm_payload(trans);

        const uint64_t end = address + size - 1;
        if (address < dmi.get_start_address() || end > dmi.get_end_address()) {
            return false;
        }
        if (need_read && !dmi.is_read_allowed()) {
            return false;
        }
        if (need_write && !dmi.is_write_allowed()) {
            return false;
        }

        ptr = dmi.get_dmi_ptr() + (address - dmi.get_start_address());
        return ptr != nullptr;
    }

    bool guest_tlm_access(uint64_t address, uint8_t* data, uint64_t size,
                          tlm::tlm_command command, bool allow_tlm_fallback)
    {
        if (!allow_tlm_fallback || size > std::numeric_limits<unsigned int>::max() ||
            (data == nullptr && size != 0)) {
            return false;
        }
        if (size == 0) {
            return true;
        }

        TlmPayload trans;
        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(data);
        trans.set_data_length(static_cast<unsigned int>(size));
        trans.set_streaming_width(static_cast<unsigned int>(size));
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        initiator_customize_tlm_payload(trans);

        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(trans, delay);
        initiator_tidy_tlm_payload(trans);

        return trans.is_response_ok();
    }

    bool guest_read_bytes_tlm(uint64_t address, uint64_t size,
                              std::vector<uint8_t>& out, bool allow_tlm_fallback)
    {
        out.clear();
        if (!allow_tlm_fallback || size > std::numeric_limits<unsigned int>::max()) {
            return false;
        }
        if (size == 0) {
            return true;
        }

        out.resize(static_cast<size_t>(size));
        static constexpr uint64_t CHUNK_SIZE = 4096;
        uint64_t offset = 0;
        while (offset < size) {
            const uint64_t chunk_addr = address + offset;
            const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
            const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
            TlmPayload trans;
            trans.set_command(tlm::TLM_READ_COMMAND);
            trans.set_address(chunk_addr);
            trans.set_data_ptr(out.data() + offset);
            trans.set_data_length(static_cast<unsigned int>(chunk_size));
            trans.set_streaming_width(static_cast<unsigned int>(chunk_size));
            trans.set_byte_enable_ptr(nullptr);
            trans.set_byte_enable_length(0);
            trans.set_dmi_allowed(false);
            trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            initiator_customize_tlm_payload(trans);

            const unsigned int read = socket->transport_dbg(trans);
            initiator_tidy_tlm_payload(trans);
            if (read != chunk_size) {
                out.clear();
                return false;
            }
            offset += chunk_size;
        }
        return true;
    }

    bool guest_write_bytes_tlm(uint64_t address, const uint8_t* data,
                               uint64_t size, bool allow_tlm_fallback)
    {
        if (!allow_tlm_fallback) {
            return false;
        }
        static constexpr uint64_t CHUNK_SIZE = 4096;
        uint64_t offset = 0;
        while (offset < size) {
            const uint64_t chunk_addr = address + offset;
            const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
            const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
            if (!guest_tlm_access(chunk_addr, const_cast<uint8_t*>(data) + offset,
                                  chunk_size, tlm::TLM_WRITE_COMMAND,
                                  allow_tlm_fallback)) {
                return false;
            }
            offset += chunk_size;
        }
        if (size != 0) {
            m_inst.get().tb_invalidate_phys_range(address, address + size - 1);
        }
        return true;
    }

    bool guest_read_u32(uint64_t address, uint32_t& value,
                        bool allow_tlm_fallback) override
    {
        uint8_t* ptr = nullptr;
        if (guest_dmi_ptr(address, sizeof(value), true, false, ptr)) {
            std::memcpy(&value, ptr, sizeof(value));
            return true;
        }
        std::vector<uint8_t> bytes;
        if (!guest_read_bytes(address, sizeof(value), bytes, allow_tlm_fallback)) {
            return false;
        }
        std::memcpy(&value, bytes.data(), sizeof(value));
        return true;
    }

    bool guest_read_u8(uint64_t address, uint8_t& value,
                       bool allow_tlm_fallback) override
    {
        uint8_t* ptr = nullptr;
        if (guest_dmi_ptr(address, sizeof(value), true, false, ptr)) {
            value = *ptr;
            return true;
        }
        std::vector<uint8_t> bytes;
        if (!guest_read_bytes(address, sizeof(value), bytes, allow_tlm_fallback)) {
            return false;
        }
        value = bytes[0];
        return true;
    }

    bool guest_read_bytes(uint64_t address, uint64_t size,
                          std::vector<uint8_t>& out,
                          bool allow_tlm_fallback) override
    {
        out.clear();
        if (size > std::numeric_limits<unsigned int>::max()) {
            return false;
        }

        uint8_t* ptr = nullptr;
        if (!guest_dmi_ptr(address, size, true, false, ptr)) {
            static constexpr uint64_t CHUNK_SIZE = 4096;
            out.resize(static_cast<size_t>(size));
            uint64_t offset = 0;
            while (offset < size) {
                const uint64_t chunk_addr = address + offset;
                const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
                uint8_t* chunk_ptr = nullptr;
                if (!guest_dmi_ptr(chunk_addr, chunk_size, true, false, chunk_ptr)) {
                    return guest_read_bytes_tlm(address, size, out, allow_tlm_fallback);
                }
                std::memcpy(out.data() + offset, chunk_ptr,
                            static_cast<size_t>(chunk_size));
                offset += chunk_size;
            }
            return true;
        }

        out.resize(static_cast<size_t>(size));
        if (size != 0) {
            std::memcpy(out.data(), ptr, static_cast<size_t>(size));
        }
        return true;
    }

    bool guest_read_bytes_or_alias(uint64_t address, uint64_t size,
                                   std::vector<uint8_t>& out,
                                   bool& direct_file_alias,
                                   bool allow_tlm_fallback) override
    {
        out.clear();
        direct_file_alias = false;
        if (size > std::numeric_limits<unsigned int>::max()) {
            return false;
        }
        if (size == 0) {
            return true;
        }

        uint8_t* ptr = nullptr;
        if (!guest_dmi_ptr(address, size, true, false, ptr)) {
            if (!socket.direct_file_alias_ptr(address, size, false, ptr)) {
                static constexpr uint64_t CHUNK_SIZE = 4096;
                out.resize(static_cast<size_t>(size));
                uint64_t offset = 0;
                while (offset < size) {
                    const uint64_t chunk_addr = address + offset;
                    const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                    const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
                    uint8_t* chunk_ptr = nullptr;
                    if (!guest_dmi_ptr(chunk_addr, chunk_size, true, false, chunk_ptr)) {
                        if (!socket.direct_file_alias_ptr(chunk_addr, chunk_size,
                                                          false, chunk_ptr)) {
                            return guest_read_bytes_tlm(address, size, out,
                                                        allow_tlm_fallback);
                        }
                        direct_file_alias = true;
                    }
                    std::memcpy(out.data() + offset, chunk_ptr,
                                static_cast<size_t>(chunk_size));
                    offset += chunk_size;
                }
                return true;
            }
            direct_file_alias = true;
        }

        out.resize(static_cast<size_t>(size));
        std::memcpy(out.data(), ptr, static_cast<size_t>(size));
        return true;
    }

    bool guest_write_bytes(uint64_t address, const uint8_t* data,
                           uint64_t size, bool allow_tlm_fallback) override
    {
        if (size > std::numeric_limits<unsigned int>::max() ||
            (data == nullptr && size != 0)) {
            return false;
        }

        uint8_t* ptr = nullptr;
        if (!guest_dmi_ptr(address, size, false, true, ptr)) {
            static constexpr uint64_t CHUNK_SIZE = 4096;
            uint64_t offset = 0;
            while (offset < size) {
                const uint64_t chunk_addr = address + offset;
                const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
                uint8_t* chunk_ptr = nullptr;
                if (!guest_dmi_ptr(chunk_addr, chunk_size, false, true, chunk_ptr)) {
                    return guest_write_bytes_tlm(address, data, size,
                                                 allow_tlm_fallback);
                }
                std::memcpy(chunk_ptr, data + offset, static_cast<size_t>(chunk_size));
                offset += chunk_size;
            }
            if (size != 0) {
                m_inst.get().tb_invalidate_phys_range(address, address + size - 1);
            }
            return true;
        }
        if (size != 0) {
            std::memcpy(ptr, data, static_cast<size_t>(size));
            m_inst.get().tb_invalidate_phys_range(address, address + size - 1);
        }
        return true;
    }

    bool guest_write_bytes_or_alias(uint64_t address, const uint8_t* data,
                                    uint64_t size, bool& direct_file_alias,
                                    bool allow_tlm_fallback) override
    {
        direct_file_alias = false;
        if (size > std::numeric_limits<unsigned int>::max() ||
            (data == nullptr && size != 0)) {
            return false;
        }
        if (size == 0) {
            return true;
        }

        uint8_t* ptr = nullptr;
        if (!guest_dmi_ptr(address, size, false, true, ptr)) {
            if (!socket.direct_file_alias_ptr(address, size, true, ptr)) {
                static constexpr uint64_t CHUNK_SIZE = 4096;
                uint64_t offset = 0;
                while (offset < size) {
                    const uint64_t chunk_addr = address + offset;
                    const uint64_t page_remaining = CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                    const uint64_t chunk_size = std::min<uint64_t>(size - offset, page_remaining);
                    uint8_t* chunk_ptr = nullptr;
                    if (!guest_dmi_ptr(chunk_addr, chunk_size, false, true, chunk_ptr)) {
                        if (!socket.direct_file_alias_ptr(chunk_addr, chunk_size,
                                                          true, chunk_ptr)) {
                            return guest_write_bytes_tlm(address, data, size,
                                                         allow_tlm_fallback);
                        }
                        direct_file_alias = true;
                    }
                    std::memcpy(chunk_ptr, data + offset,
                                static_cast<size_t>(chunk_size));
                    offset += chunk_size;
                }
                return true;
            }
            direct_file_alias = true;
        }

        std::memcpy(ptr, data, static_cast<size_t>(size));
        return true;
    }

    bool guest_write_u32(uint64_t address, uint32_t value,
                         bool allow_tlm_fallback) override
    {
        return guest_write_bytes(address, reinterpret_cast<const uint8_t*>(&value),
                                 sizeof(value), allow_tlm_fallback);
    }

    bool direct_file_alias_ptr(uint64_t address, uint64_t size,
                               bool need_write, uint8_t*& ptr) override
    {
        return socket.direct_file_alias_ptr(address, size, need_write, ptr);
    }

    void install_direct_file_aliases(const std::string& aliases) override
    {
        socket.install_direct_file_aliases(aliases);
    }

    void install_mmio_read_fastpath(const std::string& spec) override
    {
        socket.install_mmio_read_fastpath(spec);
    }

    void install_mmio_direct_fastpath_ranges(const std::string& spec) override
    {
        socket.install_mmio_direct_fastpath_ranges(spec);
    }

    void invalidate_guest_range(uint64_t start, uint64_t end) override
    {
        m_inst.get().tb_invalidate_phys_range(start, end);
    }

    void set_vcpu_dirty(bool dirty) override { m_cpu.set_vcpu_dirty(dirty); }

    void kick_cpu() override { m_cpu.kick(); }

    uint64_t pc_entry_watch_count() const override { return m_cpu.pc_entry_watch_count(); }

    uint64_t pc_entry_watch_add_calls() const override { return m_cpu.pc_entry_watch_add_calls(); }

    uint64_t pc_entry_watch_clear_calls() const override { return m_cpu.pc_entry_watch_clear_calls(); }

    uint64_t pc_entry_watch_match_queries() const override { return m_cpu.pc_entry_watch_match_queries(); }

    uint64_t pc_entry_watch_match_hits() const override { return m_cpu.pc_entry_watch_match_hits(); }

    uintptr_t pc_entry_watch_last_pc() const override { return m_cpu.pc_entry_watch_last_pc(); }

    uintptr_t pc_entry_watch_last_watch_pc() const override { return m_cpu.pc_entry_watch_last_watch_pc(); }

    bool dispatch_pc_entry(uintptr_t pc)
    {
        bool handled = false;
        for (auto* observer : m_pc_entry_observers) {
            if (observer != nullptr && observer->enabled()) {
                handled = observer->on_pc_entry(pc) || handled;
            }
        }
        return handled;
    }

    void notify_observers_cpu_sync()
    {
        for (auto* observer : m_pc_entry_observers) {
            if (observer != nullptr && observer->enabled()) {
                observer->on_cpu_sync();
            }
        }
    }

    /*
     * Called after a CPU loop run. It synchronizes with the kernel.
     */
    void sync_with_kernel()
    {
        int64_t now = m_inst.get().get_virtual_clock();

        m_cpu.set_soft_stopped(true);
        notify_observers_cpu_sync();
        trace_pc_sample(now);

        m_inst.get().unlock_iothread();
        if (m_finished) return;
        if (!m_coroutines) {
            m_qk->start(); // we may have switched the QK off, so switch it on before setting
        }
        sc_core::sc_time sc_t = sc_core::sc_time_stamp();
        if (sc_core::sc_time(now, sc_core::SC_NS) > sc_t) {
            m_qk->set(sc_core::sc_time(now, sc_core::SC_NS) - sc_t);
        }
        // Important to allow QK to notify itself if it's waiting.
        m_qk->sync();
    }

    /*
     * Callback called when the CPU exits its execution loop. In coroutine
     * mode, we yield here to come back to run_cpu_loop(). In TCG thread mode,
     * we use this hook to synchronize with the kernel.
     */
    void end_of_loop_cb()
    {
        SCP_TRACE(())("End of loop");
        if (m_finished) return;
        /*
         * In MTTCG mode, vCPU threads are created during elaboration and can
         * call this callback before start_of_simulation() has completed.
         * Skip sync_with_kernel/prepare_run_cpu until fully initialized.
         */
        if (!m_started) return;

        if (m_coroutines) {
            m_inst.get().coroutine_yield();
        } else {
            std::lock_guard<std::mutex> lock(m_can_delete);
            sync_with_kernel();
            prepare_run_cpu();
        }
    }

    /*
     * SystemC thread entry when running in coroutine mode.
     */
    void mainloop_thread_coroutine()
    {
        m_cpu.register_thread();

        for (; !m_finished;) {
            prepare_run_cpu();
            run_cpu_loop();
            sync_with_kernel();
        }
    }

    void trace_pc_sample(int64_t vclock_now)
    {
        if (!p_trace_pc.get_value()) {
            return;
        }

        const uint64_t interval = std::max<uint64_t>(1, p_trace_pc_interval.get_value());
        ++m_pc_trace_seen;
        if ((m_pc_trace_seen % interval) != 0) {
            return;
        }

        const uint64_t limit = p_trace_pc_limit.get_value();
        if (limit != 0 && m_pc_trace_emitted >= limit) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_trace_lock);
        std::ostream* out = &std::cerr;
        const std::string trace_file = p_trace_pc_file.get_value();
        if (!trace_file.empty()) {
            if (!m_pc_trace_stream.is_open()) {
                m_pc_trace_stream.open(trace_file, std::ios::out | std::ios::app);
                if (!m_pc_trace_stream) {
                    std::cerr << name() << " pc_trace_error file=" << trace_file << std::endl;
                    return;
                }
            }
            out = &m_pc_trace_stream;
        }

        ++m_pc_trace_emitted;
        *out << name()
             << " pc_trace sample=" << m_pc_trace_emitted
             << " seen=" << m_pc_trace_seen
             << " sc_time=" << sc_core::sc_time_stamp()
             << " vclock_ns=" << vclock_now
             << " pc=0x" << std::hex << m_cpu.get_pc()
             << " mem_io_pc=0x" << m_cpu.get_mem_io_pc()
             << " run_state=0x" << m_cpu.get_run_state()
             << " power_state=" << std::dec << qemu::CpuArm(m_cpu).get_power_state();
        if (p_trace_exception_state.get_value()) {
            trace_exception_state(*out);
        }
        *out << std::endl;
    }

    const char* reset_state_name() const
    {
        switch (m_resetting) {
        case none:
            return "none";
        case start_reset:
            return "start_reset";
        case hold_reset:
            return "hold_reset";
        case finish_reset:
            return "finish_reset";
        }

        return "unknown";
    }

    void trace_reset_event(const char* event, bool value)
    {
        if (!p_trace_pc.get_value()) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_trace_lock);
        std::ostream* out = &std::cerr;
        const std::string trace_file = p_trace_pc_file.get_value();
        if (!trace_file.empty()) {
            if (!m_pc_trace_stream.is_open()) {
                m_pc_trace_stream.open(trace_file, std::ios::out | std::ios::app);
                if (!m_pc_trace_stream) {
                    std::cerr << name() << " reset_trace_error file=" << trace_file << std::endl;
                    return;
                }
            }
            out = &m_pc_trace_stream;
        }

        *out << name()
             << " reset_trace event=" << event
             << " value=" << (value ? 1 : 0)
             << " state=" << reset_state_name()
             << " started=" << (m_started ? 1 : 0)
             << " finished=" << (m_finished ? 1 : 0)
             << " sc_time=" << sc_core::sc_time_stamp()
             << " vclock_ns=" << m_inst.get().get_virtual_clock()
             << " run_state=0x" << std::hex << m_cpu.get_run_state()
             << " power_state=" << std::dec << qemu::CpuArm(m_cpu).get_power_state()
             << std::endl;
    }

    void trace_reset_code(const char* event, int code)
    {
        if (!p_trace_pc.get_value()) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_trace_lock);
        std::ostream* out = &std::cerr;
        const std::string trace_file = p_trace_pc_file.get_value();
        if (!trace_file.empty()) {
            if (!m_pc_trace_stream.is_open()) {
                m_pc_trace_stream.open(trace_file, std::ios::out | std::ios::app);
                if (!m_pc_trace_stream) {
                    std::cerr << name() << " reset_trace_error file=" << trace_file << std::endl;
                    return;
                }
            }
            out = &m_pc_trace_stream;
        }

        *out << name()
             << " reset_trace event=" << event
             << " code=" << code
             << " state=" << reset_state_name()
             << " started=" << (m_started ? 1 : 0)
             << " finished=" << (m_finished ? 1 : 0)
             << " sc_time=" << sc_core::sc_time_stamp()
             << " vclock_ns=" << m_inst.get().get_virtual_clock()
             << " run_state=0x" << std::hex << m_cpu.get_run_state()
             << " power_state=" << std::dec << qemu::CpuArm(m_cpu).get_power_state()
             << std::endl;
    }

    void trace_exception_state(std::ostream& out)
    {
        using A64Field = qemu::CpuArm::Aarch64StateField;
        if (get_aarch64_state(A64Field::IS_A64)) {
            const auto a64_hex = [this, &out](const char* key, A64Field field) {
                out << " " << key << "=0x" << std::hex << get_aarch64_state(field) << std::dec;
            };
            const auto a64_dec = [this, &out](const char* key, A64Field field) {
                out << " " << key << "=" << std::dec << get_aarch64_state(field);
            };

            a64_dec("aarch64", A64Field::IS_A64);
            a64_dec("el", A64Field::CURRENT_EL);
            a64_dec("cpu_exception_index", A64Field::CPU_EXCEPTION_INDEX);
            a64_hex("pstate", A64Field::PSTATE);
            a64_hex("sp", A64Field::SP);
            a64_hex("sp_el0", A64Field::SP_EL0);
            a64_hex("sp_el3", A64Field::SP_EL3);
            a64_hex("lr", A64Field::LR);
            a64_hex("x29", A64Field::X29);
            a64_hex("syndrome", A64Field::EXCEPTION_SYNDROME);
            a64_hex("vaddr", A64Field::EXCEPTION_VADDRESS);
            a64_hex("esr_el3", A64Field::ESR_EL3);
            a64_hex("far_el3", A64Field::FAR_EL3);
            a64_hex("elr_el3", A64Field::ELR_EL3);
            a64_hex("x0", A64Field::X0);
            a64_hex("x1", A64Field::X1);
            a64_hex("x2", A64Field::X2);
            a64_hex("x3", A64Field::X3);
            a64_hex("x4", A64Field::X4);
            a64_hex("x5", A64Field::X5);
            a64_hex("x6", A64Field::X6);
            a64_hex("x7", A64Field::X7);
            return;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const auto hex_field = [this, &out](const char* key, Field field) {
            out << " " << key << "=0x" << std::hex << get_v7m_state(field) << std::dec;
        };
        const auto dec_field = [this, &out](const char* key, Field field) {
            out << " " << key << "=" << std::dec << get_v7m_state(field);
        };

        hex_field("xpsr", Field::XPSR);
        dec_field("exception", Field::EXCEPTION);
        dec_field("cpu_exception_index", Field::CPU_EXCEPTION_INDEX);
        dec_field("secure", Field::SECURE);
        hex_field("sp", Field::SP);
        hex_field("lr", Field::LR);
        hex_field("r0", Field::R0);
        hex_field("r1", Field::R1);
        hex_field("r2", Field::R2);
        hex_field("r3", Field::R3);
        hex_field("r4", Field::R4);
        hex_field("r5", Field::R5);
        hex_field("r6", Field::R6);
        hex_field("r7", Field::R7);
        hex_field("r8", Field::R8);
        hex_field("r9", Field::R9);
        hex_field("r10", Field::R10);
        hex_field("r11", Field::R11);
        hex_field("r12", Field::R12);
        hex_field("cfsr_ns", Field::CFSR_NS);
        hex_field("cfsr_s", Field::CFSR_S);
        hex_field("hfsr", Field::HFSR);
        hex_field("dfsr", Field::DFSR);
        hex_field("sfsr", Field::SFSR);
        hex_field("mmfar_ns", Field::MMFAR_NS);
        hex_field("mmfar_s", Field::MMFAR_S);
        hex_field("bfar", Field::BFAR);
        hex_field("sfar", Field::SFAR);
        hex_field("aircr", Field::AIRCR);
        hex_field("vtor_ns", Field::VTOR_NS);
        hex_field("vtor_s", Field::VTOR_S);
        hex_field("control_ns", Field::CONTROL_NS);
        hex_field("control_s", Field::CONTROL_S);
        hex_field("primask_ns", Field::PRIMASK_NS);
        hex_field("primask_s", Field::PRIMASK_S);
        hex_field("faultmask_ns", Field::FAULTMASK_NS);
        hex_field("faultmask_s", Field::FAULTMASK_S);
        hex_field("basepri_ns", Field::BASEPRI_NS);
        hex_field("basepri_s", Field::BASEPRI_S);
        hex_field("other_sp", Field::OTHER_SP);
        hex_field("other_ss_msp", Field::OTHER_SS_MSP);
        hex_field("other_ss_psp", Field::OTHER_SS_PSP);
        hex_field("msplim_ns", Field::MSPLIM_NS);
        hex_field("msplim_s", Field::MSPLIM_S);
        hex_field("psplim_ns", Field::PSPLIM_NS);
        hex_field("psplim_s", Field::PSPLIM_S);
    }

public:
    cci::cci_param<unsigned int> p_gdb_port;
    cci::cci_param<bool> p_trace_pc;
    cci::cci_param<bool> p_trace_exception_state;
    cci::cci_param<uint64_t> p_trace_pc_interval;
    cci::cci_param<uint64_t> p_trace_pc_limit;
    cci::cci_param<std::string> p_trace_pc_file;
    cci::cci_param<bool> p_start_in_reset;
    cci::cci_param<bool> p_reset_power_on;

    /* The default memory socket. Mapped to the default CPU address space in QEMU */
    QemuInitiatorSocket<> socket;
    TargetSignalSocket<bool> halt;
    TargetSignalSocket<bool> reset;

    QemuCpu(const sc_core::sc_module_name& name, QemuInstance& inst, const std::string& type_name)
        : QemuDevice(name, inst, (type_name + "-cpu").c_str())
        , halt("halt")
        , reset("reset")
        , m_qemu_kick_ev(false)
        , m_signaled(false)
        , p_gdb_port("gdb_port", 0, "Wait for gdb connection on TCP port <gdb_port>")
        , p_trace_pc("trace_pc", false, "Emit lightweight PC samples at CPU loop sync points")
        , p_trace_exception_state("trace_exception_state", false,
                                  "Include Arm M-profile exception state in PC samples")
        , p_trace_pc_interval("trace_pc_interval", 1, "Emit one PC sample every N CPU loop sync points")
        , p_trace_pc_limit("trace_pc_limit", 0, "Maximum PC samples to emit; 0 means unlimited")
        , p_trace_pc_file("trace_pc_file", "", "Optional file path for PC samples")
        , p_start_in_reset("start_in_reset", false, "Hold the CPU in reset when simulation starts")
        , p_reset_power_on("reset_power_on", false, "Set Arm PSCI power state to ON when reset is released")
        , socket("mem", *this, inst)
    {
        using namespace std::placeholders;

        m_external_ev |= m_qemu_kick_ev;

        auto haltcb = std::bind(&QemuCpu::halt_cb, this, _1);
        halt.register_value_changed_cb(haltcb);
        auto resetcb = std::bind(&QemuCpu::reset_cb, this, _1);
        reset.register_value_changed_cb(resetcb);

        create_quantum_keeper();
        set_coroutine_mode();

        if (!m_coroutines) {
            SC_THREAD(watch_external_ev);
        }

        m_inst.add_dev(this);

        m_start_reset_done_ev.async_detach_suspending();
    }


    void register_pc_entry_observer(QemuCpuPcEntryObserver& observer)
    {
        if (std::find(m_pc_entry_observers.begin(), m_pc_entry_observers.end(),
                      &observer) == m_pc_entry_observers.end()) {
            m_pc_entry_observers.push_back(&observer);
        }
    }

    void unregister_pc_entry_observer(QemuCpuPcEntryObserver& observer)
    {
        m_pc_entry_observers.erase(
            std::remove(m_pc_entry_observers.begin(), m_pc_entry_observers.end(),
                        &observer),
            m_pc_entry_observers.end());
    }

    virtual ~QemuCpu()
    {
        end_of_simulation(); // catch the case we exited abnormally

        /*
         * Wait for any jobs that were already queued via async_run /
         * async_safe_run to finish executing before we destroy the object.
         * Those jobs might hold captured references.
         */
        {
            std::unique_lock<std::mutex> lock(m_async_work_mutex);
            if (!m_async_work_cv.wait_for(lock, std::chrono::milliseconds(ASYNC_WORK_TIMEOUT_MS),
                                          [this] { return m_async_work_outstanding == 0; })) {
                SCP_WARN(()) << "Timeout waiting for " << m_async_work_outstanding.load()
                             << " outstanding async work(s) to complete during destruction";
                // We may arrive here if the QEMU thread never actually started, there was queue'd work waiting for it,
                // but the simulation has been terminated.
            }
        }

        while (!m_can_delete.try_lock()) {
            m_qk->stop();
        }
        m_inst.del_dev(this);
    }

    // Process shutting down the CPU's at end of simulation, check this was done on destruction.
    // This gives time for QEMU to exit etc.
    void end_of_simulation() override
    {
        for (auto* observer : m_pc_entry_observers) {
            if (observer != nullptr) {
                observer->end_of_simulation();
            }
        }
        if (m_finished) return;
        m_finished = true; // assert before taking lock (for co-routines too)

        if (!m_cpu.valid()) {
            /* CPU hasn't been created yet */
            return;
        }

        if (!m_realized) {
            return;
        }

        /*
         * If start_of_simulation() was never called (e.g. the simulation
         * aborted during elaboration) then finish_qemu_init() was never
         * called either, and the QEMU iothread is still in its startup
         * wait.  Calling lock_iothread() in that state blocks forever
         * inside wait_for_iothread_startup.  Nothing useful can be done
         * without a running iothread, so bail out early.
         */
        if (!m_started) {
            return;
        }

        m_inst.get().lock_iothread();
        /* Make sure QEMU won't call us anymore */
        m_cpu.clear_callbacks();

        if (m_coroutines) {
            // can't join or wait for sc_event
            m_inst.get().unlock_iothread();
            return;
        }

        /* Unblock it if it's waiting for run budget */
        m_qk->stop();

        /* Unblock the CPU thread if it's sleeping */
        set_signaled();

        /* Wait for QEMU to terminate the CPU thread */
        /*
         * Theoretically we should m_cpu.remove_sync(); here, however if QEMU is in the process of an io operation or an
         * exclusive cpu region, it will end up waiting for the io operation to finish (effectively waiting for the
         * SystemC thread, or potentially another CPU that wont get the chance to exit)
         */
        m_cpu.halt(true);

        m_inst.get().unlock_iothread();
        m_cpu.kick(); // Just in case the CPU is currently in the big lock waiting
        m_cpu.set_unplug(true);
    }

    /* NB this is usd to determin if this cpu can run in SINGLE mode
     * for the m_inst.can_run calculation
     */
    bool can_run() override { return m_cpu.can_run(); }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        m_cpu = qemu::Cpu(m_dev);

        if (m_coroutines) {
            m_sc_thread = sc_core::sc_spawn(std::bind(&QemuCpu::mainloop_thread_coroutine, this));
        }

        socket.init(m_dev, "memory");

        m_cpu.set_soft_stopped(true);

        m_cpu.set_end_of_loop_callback(std::bind(&QemuCpu::end_of_loop_cb, this));
        m_cpu.set_kick_callback(std::bind(&QemuCpu::kick_cb, this));
        bool needs_pc_entry_callback = false;
        for (auto* observer : m_pc_entry_observers) {
            if (observer != nullptr && observer->enabled()) {
                observer->configure_pc_watches(m_cpu);
                needs_pc_entry_callback =
                    observer->needs_pc_entry_callback() || needs_pc_entry_callback;
            }
        }
        if (needs_pc_entry_callback) {
            m_cpu.set_pc_entry_callback(
                std::bind(&QemuCpu::dispatch_pc_entry, this, std::placeholders::_1));
        }

        m_deadline_timer = m_inst.get().timer_new();
        m_deadline_timer->set_callback(std::bind(&QemuCpu::deadline_timer_cb, this));

        m_cpu_hint_ext.set_cpu(m_cpu);
    }

    void halt_cb(const bool& val)
    {
        SCP_TRACE(())("Halt : {}", val);
        if (!m_finished) {
            if (val) {
                m_deadline_timer->del();
                m_qk->stop();
            } else {
                m_qk->start();
                rearm_deadline_timer();
            }
            m_inst.get().lock_iothread();
            m_cpu.halt(val);
            m_inst.get().unlock_iothread();
            m_qemu_kick_ev.async_notify(); // notify the other thread so that the CPU is allowed to continue
        }
    }

    /* NB _MUST_ be called from an SC_THREAD */
    void reset_cb(const bool& val)
    {
        /* Assume this is on the SystemC thread, so no race condition issues */
        if (m_finished) return;
        trace_reset_event("reset-cb-enter", val);

        if (val) {
            if (m_resetting != none) {
                trace_reset_event("reset-cb-assert-ignored", val);
                return; // dont double reset!
            }
            SCP_WARN(())("Start reset");
            m_resetting = start_reset;
            trace_reset_event("reset-cb-assert-start", val);
            m_cpu.async_safe_run(make_tracked_async_job([this] {
                m_cpu.reset(true);
                m_resetting = hold_reset;
                m_start_reset_done_ev.async_notify();
            })); // start the reset (which will pause the CPU)
        } else {
            if (m_resetting == none) {
                trace_reset_event("reset-cb-release-ignored", val);
                return; // dont finish a finished reset!
            }
            while (m_resetting == start_reset) {
                SCP_WARN(())("Hold reset");
                trace_reset_event("reset-cb-release-wait-start", val);
                sc_core::wait(m_start_reset_done_ev);
                trace_reset_event("reset-cb-release-wait-done", val);
            }
            m_inst.get().lock_iothread();
            socket.reset(); // remove DMI's (needs BQL for memory region updates)
            m_inst.get().unlock_iothread();
            m_resetting = finish_reset;
            trace_reset_event("reset-cb-release-start", val);
            if (p_start_in_reset.get_value()) {
                const bool reset_power_on = p_reset_power_on.get_value();

                m_qk->start();
                m_qk->reset();
                m_resetting = none;
                m_cpu.async_safe_run(make_tracked_async_job([this, reset_power_on] {
                    trace_reset_event("reset-cb-release-async-start", false);
                    if (reset_power_on) {
                        qemu::CpuArm(m_cpu).set_power_state(true);
                        trace_reset_event("reset-cb-release-async-set-power-on-before-reset", false);
                    }
                    m_cpu.reset(false);
                    trace_reset_event("reset-cb-release-async-after-reset-false", false);
                    if (reset_power_on) {
                        qemu::CpuArm(m_cpu).set_power_state(true);
                        trace_reset_event("reset-cb-release-async-after-set-power-on", false);
                    }
                    m_cpu.set_soft_stopped(false);
                    rearm_deadline_timer();
                    m_cpu.kick();
                    m_qemu_kick_ev.async_notify();
                    trace_reset_event("reset-cb-release-async-after-kick", false);
                }));
                m_qemu_kick_ev.async_notify();
                trace_reset_event("reset-cb-release-queued", val);
                return;
            }
            if (p_reset_power_on.get_value()) {
                m_inst.get().lock_iothread();
                const int power_rc = qemu::CpuArm(m_cpu).power_on_and_reset();
                m_inst.get().unlock_iothread();
                trace_reset_code("reset-cb-power-on-and-reset", power_rc);
            }
            m_cpu.reset(false); // call the end-of-reset (which will unpause the CPU)
            trace_reset_event("reset-cb-after-reset-false", val);
            if (p_reset_power_on.get_value()) {
                m_inst.get().lock_iothread();
                qemu::CpuArm(m_cpu).set_power_state(true);
                m_inst.get().unlock_iothread();
                trace_reset_event("reset-cb-after-set-power-on", val);
            }
            m_qk->start();      // restart the QK if it's stopped
            m_qk->reset();
            m_resetting = none;
            m_inst.get().lock_iothread();
            m_cpu.set_soft_stopped(false);
            rearm_deadline_timer();
            m_cpu.kick();
            m_inst.get().unlock_iothread();
            trace_reset_event("reset-cb-after-kick", val);
            m_qemu_kick_ev.notify(sc_core::SC_ZERO_TIME);
            trace_reset_event("reset-cb-after-kick-notify", val);
            SCP_WARN(())("Finished reset");
            trace_reset_event("reset-cb-release-done", val);
        }
        m_qemu_kick_ev.notify(sc_core::SC_ZERO_TIME);
        trace_reset_event("reset-cb-after-final-notify", val);
    }
    virtual void end_of_elaboration() override
    {
        QemuDevice::end_of_elaboration();

        if (!p_gdb_port.is_default_value()) {
            std::stringstream ss;
            SCP_INFO(()) << "Starting gdb server on TCP port " << p_gdb_port;
            ss << "tcp::" << p_gdb_port;
            m_inst.get().start_gdb_server(ss.str());
        }
    }

    virtual void start_of_simulation() override
    {
        m_quantum_ns = int64_t(tlm_utils::tlm_quantumkeeper::get_global_quantum().to_seconds() * 1e9);

        QemuDevice::start_of_simulation();
        const bool start_in_reset = p_start_in_reset.get_value();
        if (start_in_reset) {
            m_cpu.reset(true);
            m_resetting = hold_reset;
            trace_reset_event("start-of-simulation-hold-reset", true);
        }

        if (m_inst.get_tcg_mode() == QemuInstance::TCG_SINGLE) {
            if (m_inst.can_run()) {
                m_qk->start();
            }
        } else if (!m_coroutines) {
            /*
             * In MTTCG mode, start the QK to register a suspending channel
             * with the SystemC kernel. Without this, async_suspend() returns
             * true (exit) whenever there are no pending events, which can
             * happen in the gap between MMIO transactions processed by
             * run_on_sysc(). The QK will be stopped later in wait_for_work()
             * when the CPU halts (e.g. WFI), allowing normal starvation exit.
             */
            m_qk->start();
        }

        m_started = true;
        if (!m_coroutines) {
            /*
             * Start the quantum keeper before kicking the CPU to ensure
             * its tick event is attached as suspending. Without this, a
             * fast CPU could complete and stop its QK before a slow CPU
             * ever calls sync_with_kernel() (where start() was previously
             * first called), leaving no suspending events and causing
             * premature simulation exit due to starvation.
             */
            m_qk->start();

            if (!start_in_reset) {
                trace_reset_event("start-of-simulation-release", false);
                /* Prepare the CPU for its first run and release it
                 * Hold BQL to synchronize with the vCPU thread's idle-wait loop
                 * in qemu_process_cpu_events(). That loop checks cpu_thread_is_idle()
                 * (which reads soft_stopped) under BQL, then enters
                 * qemu_cond_wait(halt_cond, &bql) which atomically releases BQL.
                 * Without BQL here, the kick (broadcast on halt_cond) can be lost
                 * if the vCPU thread is between the idle check and the cond_wait.
                 */
                m_inst.get().lock_iothread();
                m_cpu.set_soft_stopped(false);
                rearm_deadline_timer();
                m_cpu.kick();
                m_inst.get().unlock_iothread();
            }
        }

        // Have not managed to figure out the root cause of the issue, but the
        // PC is not properly set before running KVM, or it is possibly reset to
        // 0 by some routine. By setting the vcpu as dirty, we trigger pushing
        // registers to KVM just before running it.
        m_cpu.set_vcpu_dirty(true);
    }

    /* QemuInitiatorIface  */
    virtual void initiator_customize_tlm_payload(TlmPayload& payload) override
    {
        /* Signal the other end we are a CPU */
        payload.set_extension(&m_cpu_hint_ext);
    }

    virtual void initiator_tidy_tlm_payload(TlmPayload& payload) override { payload.clear_extension(&m_cpu_hint_ext); }

    /*
     * Called by the initiator socket just before a memory transaction.
     * We update our current view of the local time and return it.
     */
    virtual sc_core::sc_time initiator_get_local_time() override
    {
        using sc_core::sc_time;
        using sc_core::SC_NS;

        int64_t vclock_now;

        if (m_finished) return sc_core::SC_ZERO_TIME;

        vclock_now = m_inst.get().get_virtual_clock();
        sc_core::sc_time sc_t = sc_core::sc_time_stamp();
        if (sc_time(vclock_now, SC_NS) > sc_t) {
            m_qk->set(sc_time(vclock_now, SC_NS) - sc_t);
            return m_qk->get_local_time();
        } else {
            return sc_core::SC_ZERO_TIME;
        }
    }

    /*
     * Called after the transaction. We must update our local time view to
     * match t.
     */
    virtual void initiator_set_local_time(const sc_core::sc_time& t) override
    {
        if (m_finished) return;
        m_qk->set(t);

        if (m_qk->need_sync()) {
            /*
             * Kick the CPU out of its execution loop so that we can sync with
             * the kernel.
             */
            m_cpu.kick();
        }
    }

    /* expose async run interface for DMI invalidation */
    virtual void initiator_async_run(qemu::Cpu::AsyncJobFn job) override
    {
        if (!m_finished) m_cpu.async_run(make_tracked_async_job(std::move(job)));
    }
};

#endif
