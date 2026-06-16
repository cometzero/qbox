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

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cci_configuration>

#include <libgssync.h>
#include <libqemu-cxx/target/aarch64.h>

#include <cc3xx_core.h>

#include "device.h"
#include "ports/initiator.h"
#include "rse_lms_accel.h"
#include "rse_mcuboot_image.h"
#include "rse_p256_ecdsa.h"
#include "tlm-extensions/qemu-cpu-hint.h"
#include "ports/qemu-target-signal-socket.h"

class QemuCpu : public QemuDevice, public QemuInitiatorIface
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
    std::atomic<uint64_t> m_hotpath_memcpy_hits{ 0 };
    std::atomic<uint64_t> m_hotpath_memset_hits{ 0 };
    std::atomic<uint64_t> m_hotpath_fallbacks{ 0 };
    std::atomic<uint64_t> m_hotpath_tlm_fallbacks{ 0 };
    std::atomic<uint64_t> m_hotpath_pc_misses{ 0 };
    std::atomic<uint64_t> m_hotpath_too_large_fails{ 0 };
    std::atomic<uint64_t> m_hotpath_dmi_fails{ 0 };
    std::atomic<uint64_t> m_hotpath_stack_fails{ 0 };
    std::atomic<uint64_t> m_hotpath_state_fails{ 0 };
    std::atomic<uint64_t> m_lms_accel_hits{ 0 };
    std::atomic<uint64_t> m_lms_accel_pc_misses{ 0 };
    std::atomic<uint64_t> m_lms_accel_dmi_fails{ 0 };
    std::atomic<uint64_t> m_lms_accel_verify_fails{ 0 };
    std::atomic<uint64_t> m_lms_accel_state_fails{ 0 };
    std::atomic<uint64_t> m_lms_accel_unsupported{ 0 };
    std::atomic<bool> m_lms_accel_done{ false };
    std::atomic<uint32_t> m_lms_last_key_addr{ 0 };
    std::atomic<uint32_t> m_lms_last_key_size{ 0 };
    std::atomic<uint32_t> m_lms_last_data_addr{ 0 };
    std::atomic<uint32_t> m_lms_last_data_size{ 0 };
    std::atomic<uint32_t> m_lms_last_sig_addr{ 0 };
    std::atomic<uint32_t> m_lms_last_sig_size{ 0 };
    std::atomic<uint32_t> m_lms_last_unsupported_mask{ 0 };

    struct Bl2ProfileSample {
        std::atomic<uint32_t> pc{ 0 };
        std::atomic<uint32_t> r0{ 0 };
        std::atomic<uint32_t> r1{ 0 };
        std::atomic<uint32_t> r2{ 0 };
        std::atomic<uint32_t> r3{ 0 };
        std::atomic<uint32_t> sp{ 0 };
        std::atomic<uint32_t> lr{ 0 };
        std::atomic<uint32_t> stack0{ 0 };
        std::atomic<uint32_t> stack1{ 0 };
        std::atomic<uint32_t> stack2{ 0 };
        std::atomic<uint32_t> stack3{ 0 };
        std::atomic<uint32_t> stack_words{ 0 };
    };

    struct Bl2ProfileSite {
        std::atomic<uint64_t> hits{ 0 };
        Bl2ProfileSample last;
    };

    std::atomic<uint64_t> m_bl2_load_profile_pc_misses{ 0 };
    std::atomic<uint64_t> m_bl2_load_profile_stack_fails{ 0 };
    Bl2ProfileSite m_bl2_boot_go_for_image_id;
    Bl2ProfileSite m_bl2_boot_load_image_to_sram;
    Bl2ProfileSite m_bl2_boot_enc_load;
    Bl2ProfileSite m_bl2_boot_enc_decrypt;
    Bl2ProfileSite m_bl2_bootutil_img_validate;
    Bl2ProfileSite m_bl2_bootutil_img_hash;
    Bl2ProfileSite m_bl2_bootutil_verify_sig;

    struct Bl2RamLoadSnapshot {
        std::atomic<uint64_t> hits{ 0 };
        std::atomic<uint64_t> dmi_failures{ 0 };
        std::atomic<uint64_t> unsupported{ 0 };
        std::atomic<uint32_t> last_state{ 0 };
        std::atomic<uint32_t> last_curr_img{ 0 };
        std::atomic<uint32_t> last_active_slot{ 0 };
        std::atomic<uint32_t> last_hdr_addr{ 0 };
        std::atomic<uint32_t> last_hdr_magic{ 0 };
        std::atomic<uint32_t> last_load_addr{ 0 };
        std::atomic<uint32_t> last_hdr_size{ 0 };
        std::atomic<uint32_t> last_protect_tlv_size{ 0 };
        std::atomic<uint32_t> last_img_size{ 0 };
        std::atomic<uint32_t> last_flags{ 0 };
        std::atomic<uint32_t> last_hash_region_size{ 0 };
        std::atomic<uint32_t> last_slot_img_dst{ 0 };
        std::atomic<uint32_t> last_slot_img_sz{ 0 };
        std::atomic<uint32_t> last_unsupported_mask{ 0 };
    };

    static constexpr size_t BL2_RAM_LOAD_SNAPSHOT_IMAGE_SLOTS = 64;

    Bl2RamLoadSnapshot m_bl2_ram_load_snapshot;
    std::array<Bl2RamLoadSnapshot, BL2_RAM_LOAD_SNAPSHOT_IMAGE_SLOTS>
        m_bl2_ram_load_snapshot_by_image;

    std::array<std::atomic<bool>, BL2_RAM_LOAD_SNAPSHOT_IMAGE_SLOTS>
        m_bl2_load_accel_image_decrypted;
    std::atomic<uint64_t> m_bl2_load_accel_hits{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_skip_hits{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_bytes{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_key_misses{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_dmi_failures{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_direct_file_alias_hits{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_state_failures{ 0 };
    std::atomic<uint64_t> m_bl2_load_accel_unsupported{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_curr_img{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_slot{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_off{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_size{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_blk_off{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_buf{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_img_size{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_flags{ 0 };
    std::atomic<uint32_t> m_bl2_load_accel_last_unsupported_mask{ 0 };

    struct Bl2EncKeyCacheEntry {
        bool valid = false;
        uint32_t key_size = 0;
        std::array<uint8_t, 32> key{};
    };

    std::mutex m_bl2_boot_enc_lock;
    std::map<uint64_t, Bl2EncKeyCacheEntry> m_bl2_boot_enc_keys;
    std::atomic<uint64_t> m_bl2_boot_enc_key_captures{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_key_capture_failures{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_hits{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_bytes{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_key_misses{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_dmi_failures{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_direct_file_alias_hits{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_state_failures{ 0 };
    std::atomic<uint64_t> m_bl2_boot_enc_decrypt_unsupported{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_enc_state{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_slot{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_off{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_size{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_blk_off{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_buf{ 0 };
    std::atomic<uint32_t> m_bl2_boot_enc_last_unsupported_mask{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_hits{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_bytes{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_dmi_failures{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_direct_file_alias_hits{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_state_failures{ 0 };
    std::atomic<uint64_t> m_bl2_img_hash_unsupported{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_hdr{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_load_addr{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_hash_result{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_hash_size{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_seed{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_seed_len{ 0 };
    std::atomic<uint32_t> m_bl2_img_hash_last_unsupported_mask{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_matches{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_cache_hits{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_cache_misses{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_skip_hits{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_dmi_failures{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_verify_failures{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_state_failures{ 0 };
    std::atomic<uint64_t> m_bl2_verify_sig_unsupported{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_hash{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_hlen{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_sig{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_slen{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_key_id{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_key_ptr{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_key_len{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_fih_success{ 0 };
    std::atomic<uint32_t> m_bl2_verify_sig_last_unsupported_mask{ 0 };
    std::atomic<uint64_t> m_bl2_delay_hits{ 0 };
    std::atomic<uint64_t> m_bl2_delay_cycles{ 0 };
    std::atomic<uint64_t> m_bl2_delay_state_failures{ 0 };
    std::atomic<uint64_t> m_bl2_delay_unsupported{ 0 };
    std::atomic<uint32_t> m_bl2_delay_last_cycles{ 0 };
    std::atomic<uint32_t> m_bl2_delay_last_lr{ 0 };
    std::atomic<uint32_t> m_bl2_delay_last_unsupported_mask{ 0 };
    std::atomic<bool> m_bl2_delay_watch_active{ false };

    struct Bl2VerifySigCacheEntry {
        std::vector<uint8_t> public_key;
        std::vector<uint8_t> hash;
        std::vector<uint8_t> signature;
        bool verified = false;
    };

    std::mutex m_bl2_verify_sig_cache_lock;
    std::vector<Bl2VerifySigCacheEntry> m_bl2_verify_sig_cache;
    std::mutex m_hotpath_profile_lock;
    bool m_lms_pc_entry_registered = false;

    enum class HotpathFailReason {
        PcMismatch,
        TooLarge,
        Dmi,
        Stack,
        State,
    };

    uint64_t get_v7m_state(qemu::CpuArm::V7MStateField field) const
    {
        return qemu::CpuArm(m_cpu).get_v7m_state(field);
    }

    uint64_t get_aarch64_state(qemu::CpuArm::Aarch64StateField field) const
    {
        return qemu::CpuArm(m_cpu).get_aarch64_state(field);
    }

    bool set_v7m_state(qemu::CpuArm::V7MStateField field, uint64_t value)
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

    bool hotpath_dmi_ptr(uint64_t address, uint64_t size, bool need_read,
                         bool need_write, uint8_t*& ptr)
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

    bool hotpath_read_u32(uint64_t address, uint32_t& value)
    {
        uint8_t* ptr = nullptr;
        if (!hotpath_dmi_ptr(address, sizeof(value), true, false, ptr)) {
            return false;
        }
        std::memcpy(&value, ptr, sizeof(value));
        return true;
    }

    bool hotpath_read_u8(uint64_t address, uint8_t& value)
    {
        uint8_t* ptr = nullptr;
        if (!hotpath_dmi_ptr(address, sizeof(value), true, false, ptr)) {
            return false;
        }
        value = *ptr;
        return true;
    }

    bool hotpath_tlm_access(uint64_t address, uint8_t* data, uint64_t size,
                            tlm::tlm_command command)
    {
        if (!p_hotpath_tlm_fallback.get_value() ||
            size > std::numeric_limits<unsigned int>::max() ||
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

        if (trans.is_response_ok()) {
            m_hotpath_tlm_fallbacks.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    bool hotpath_read_bytes_tlm(uint64_t address, uint64_t size,
                                std::vector<uint8_t>& out)
    {
        out.clear();
        if (!p_hotpath_tlm_fallback.get_value() ||
            size > std::numeric_limits<unsigned int>::max()) {
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
            const uint64_t page_remaining =
                CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
            const uint64_t chunk_size =
                std::min<uint64_t>(size - offset, page_remaining);
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
        m_hotpath_tlm_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool hotpath_write_bytes_tlm(uint64_t address, const uint8_t* data,
                                 uint64_t size)
    {
        if (!p_hotpath_tlm_fallback.get_value()) {
            return false;
        }
        static constexpr uint64_t CHUNK_SIZE = 4096;
        uint64_t offset = 0;
        while (offset < size) {
            const uint64_t chunk_addr = address + offset;
            const uint64_t page_remaining =
                CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
            const uint64_t chunk_size =
                std::min<uint64_t>(size - offset, page_remaining);
            if (!hotpath_tlm_access(chunk_addr, const_cast<uint8_t*>(data) + offset,
                                    chunk_size, tlm::TLM_WRITE_COMMAND)) {
                return false;
            }
            offset += chunk_size;
        }
        if (size != 0) {
            m_inst.get().tb_invalidate_phys_range(address, address + size - 1);
        }
        return true;
    }

    bool hotpath_read_bytes(uint64_t address, uint64_t size,
                            std::vector<uint8_t>& out)
    {
        out.clear();
        if (size > std::numeric_limits<unsigned int>::max()) {
            return false;
        }

        uint8_t* ptr = nullptr;
        if (!hotpath_dmi_ptr(address, size, true, false, ptr)) {
            static constexpr uint64_t CHUNK_SIZE = 4096;
            out.resize(static_cast<size_t>(size));
            uint64_t offset = 0;
            while (offset < size) {
                const uint64_t chunk_addr = address + offset;
                const uint64_t page_remaining =
                    CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                const uint64_t chunk_size =
                    std::min<uint64_t>(size - offset, page_remaining);
                uint8_t* chunk_ptr = nullptr;
                if (!hotpath_dmi_ptr(chunk_addr, chunk_size, true, false,
                                     chunk_ptr)) {
                    return hotpath_read_bytes_tlm(address, size, out);
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

    bool hotpath_read_bytes_or_alias(uint64_t address, uint64_t size,
                                     std::vector<uint8_t>& out,
                                     bool& direct_file_alias)
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
        if (!hotpath_dmi_ptr(address, size, true, false, ptr)) {
            if (!socket.direct_file_alias_ptr(address, size, false, ptr)) {
                const uint64_t chunk_limit = 4096;
                out.resize(static_cast<size_t>(size));
                uint64_t offset = 0;
                while (offset < size) {
                    const uint64_t chunk_addr = address + offset;
                    const uint64_t page_remaining =
                        chunk_limit - (chunk_addr & (chunk_limit - 1));
                    const uint64_t chunk_size =
                        std::min<uint64_t>(size - offset, page_remaining);
                    uint8_t* chunk_ptr = nullptr;
                    if (!hotpath_dmi_ptr(chunk_addr, chunk_size, true, false,
                                         chunk_ptr)) {
                        if (!socket.direct_file_alias_ptr(
                                chunk_addr, chunk_size, false, chunk_ptr)) {
                            return hotpath_read_bytes_tlm(address, size, out);
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
        if (size != 0) {
            std::memcpy(out.data(), ptr, static_cast<size_t>(size));
        }
        return true;
    }

    bool hotpath_write_bytes(uint64_t address, const uint8_t* data, uint64_t size)
    {
        if (size > std::numeric_limits<unsigned int>::max() ||
            (data == nullptr && size != 0)) {
            return false;
        }

        uint8_t* ptr = nullptr;
        if (!hotpath_dmi_ptr(address, size, false, true, ptr)) {
            static constexpr uint64_t CHUNK_SIZE = 4096;
            uint64_t offset = 0;
            while (offset < size) {
                const uint64_t chunk_addr = address + offset;
                const uint64_t page_remaining =
                    CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                const uint64_t chunk_size =
                    std::min<uint64_t>(size - offset, page_remaining);
                uint8_t* chunk_ptr = nullptr;
                if (!hotpath_dmi_ptr(chunk_addr, chunk_size, false, true,
                                     chunk_ptr)) {
                    return hotpath_write_bytes_tlm(address, data, size);
                }
                std::memcpy(chunk_ptr, data + offset,
                            static_cast<size_t>(chunk_size));
                offset += chunk_size;
            }
            if (size != 0) {
                m_inst.get().tb_invalidate_phys_range(
                    address, static_cast<uint64_t>(address) + size - 1);
            }
            return true;
        }
        if (size != 0) {
            std::memcpy(ptr, data, static_cast<size_t>(size));
            m_inst.get().tb_invalidate_phys_range(
                address, static_cast<uint64_t>(address) + size - 1);
        }
        return true;
    }

    bool hotpath_write_bytes_or_alias(uint64_t address, const uint8_t* data,
                                      uint64_t size, bool& direct_file_alias)
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
        if (!hotpath_dmi_ptr(address, size, false, true, ptr)) {
            if (!socket.direct_file_alias_ptr(address, size, true, ptr)) {
                static constexpr uint64_t CHUNK_SIZE = 4096;
                uint64_t offset = 0;
                while (offset < size) {
                    const uint64_t chunk_addr = address + offset;
                    const uint64_t page_remaining =
                        CHUNK_SIZE - (chunk_addr & (CHUNK_SIZE - 1));
                    const uint64_t chunk_size =
                        std::min<uint64_t>(size - offset, page_remaining);
                    uint8_t* chunk_ptr = nullptr;
                    if (!hotpath_dmi_ptr(chunk_addr, chunk_size, false, true,
                                         chunk_ptr)) {
                        if (!socket.direct_file_alias_ptr(
                                chunk_addr, chunk_size, true, chunk_ptr)) {
                            return hotpath_write_bytes_tlm(address, data, size);
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

    bool hotpath_write_u32(uint64_t address, uint32_t value)
    {
        return hotpath_write_bytes(address, reinterpret_cast<const uint8_t*>(&value),
                                   sizeof(value));
    }

    static std::string profile_hex_string(uint64_t value)
    {
        std::ostringstream ss;
        ss << "0x" << std::hex << value;
        return ss.str();
    }

    bool hotpath_memcpy_safe_pc(uint32_t pc, uint32_t base) const
    {
        return pc == base ||
               pc == base + 0x14 ||
               pc == base + 0x36 ||
               pc == base + 0x4a;
    }

    bool hotpath_memset_safe_pc(uint32_t pc, uint32_t base) const
    {
        return pc == base ||
               pc == base + 0x24 ||
               pc == base + 0x36;
    }

    uint64_t bl2_load_profile_events() const
    {
        return m_bl2_boot_go_for_image_id.hits.load(std::memory_order_relaxed) +
               m_bl2_boot_load_image_to_sram.hits.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_load.hits.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt.hits.load(std::memory_order_relaxed) +
               m_bl2_bootutil_img_validate.hits.load(std::memory_order_relaxed) +
               m_bl2_bootutil_img_hash.hits.load(std::memory_order_relaxed) +
               m_bl2_bootutil_verify_sig.hits.load(std::memory_order_relaxed) +
               m_bl2_load_profile_pc_misses.load(std::memory_order_relaxed) +
               m_bl2_load_profile_stack_fails.load(std::memory_order_relaxed);
    }

    uint64_t bl2_boot_enc_accel_events() const
    {
        return m_bl2_boot_enc_key_captures.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_key_capture_failures.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt_hits.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt_key_misses.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt_dmi_failures.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt_state_failures.load(std::memory_order_relaxed) +
               m_bl2_boot_enc_decrypt_unsupported.load(std::memory_order_relaxed);
    }

    uint64_t bl2_load_accel_events() const
    {
        return m_bl2_load_accel_hits.load(std::memory_order_relaxed) +
               m_bl2_load_accel_skip_hits.load(std::memory_order_relaxed) +
               m_bl2_load_accel_key_misses.load(std::memory_order_relaxed) +
               m_bl2_load_accel_dmi_failures.load(std::memory_order_relaxed) +
               m_bl2_load_accel_state_failures.load(std::memory_order_relaxed) +
               m_bl2_load_accel_unsupported.load(std::memory_order_relaxed);
    }

    uint64_t bl2_img_hash_accel_events() const
    {
        return m_bl2_img_hash_hits.load(std::memory_order_relaxed) +
               m_bl2_img_hash_dmi_failures.load(std::memory_order_relaxed) +
               m_bl2_img_hash_state_failures.load(std::memory_order_relaxed) +
               m_bl2_img_hash_unsupported.load(std::memory_order_relaxed);
    }

    uint64_t bl2_verify_sig_accel_events() const
    {
        return m_bl2_verify_sig_matches.load(std::memory_order_relaxed) +
               m_bl2_verify_sig_skip_hits.load(std::memory_order_relaxed) +
               m_bl2_verify_sig_dmi_failures.load(std::memory_order_relaxed) +
               m_bl2_verify_sig_verify_failures.load(std::memory_order_relaxed) +
               m_bl2_verify_sig_state_failures.load(std::memory_order_relaxed) +
               m_bl2_verify_sig_unsupported.load(std::memory_order_relaxed);
    }

    uint64_t bl2_delay_accel_events() const
    {
        return m_bl2_delay_hits.load(std::memory_order_relaxed) +
               m_bl2_delay_state_failures.load(std::memory_order_relaxed) +
               m_bl2_delay_unsupported.load(std::memory_order_relaxed);
    }

    void capture_bl2_profile_sample(Bl2ProfileSite& site, uint32_t pc)
    {
        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));

        site.hits.fetch_add(1, std::memory_order_relaxed);
        site.last.pc.store(pc, std::memory_order_relaxed);
        site.last.r0.store(static_cast<uint32_t>(get_v7m_state(Field::R0)),
                           std::memory_order_relaxed);
        site.last.r1.store(static_cast<uint32_t>(get_v7m_state(Field::R1)),
                           std::memory_order_relaxed);
        site.last.r2.store(static_cast<uint32_t>(get_v7m_state(Field::R2)),
                           std::memory_order_relaxed);
        site.last.r3.store(static_cast<uint32_t>(get_v7m_state(Field::R3)),
                           std::memory_order_relaxed);
        site.last.sp.store(sp, std::memory_order_relaxed);
        site.last.lr.store(static_cast<uint32_t>(get_v7m_state(Field::LR)),
                           std::memory_order_relaxed);

        std::atomic<uint32_t>* stack_words[] = {
            &site.last.stack0,
            &site.last.stack1,
            &site.last.stack2,
            &site.last.stack3,
        };
        uint32_t captured = 0;
        for (uint32_t index = 0; index < 4; ++index) {
            uint32_t value = 0;
            if (!hotpath_read_u32(sp + index * sizeof(value), value)) {
                m_bl2_load_profile_stack_fails.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            stack_words[index]->store(value, std::memory_order_relaxed);
            ++captured;
        }
        site.last.stack_words.store(captured, std::memory_order_relaxed);
    }

    bool record_bl2_profile_site(uint32_t pc, uint64_t addr, Bl2ProfileSite& site)
    {
        const uint32_t entry = static_cast<uint32_t>(addr & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        capture_bl2_profile_sample(site, pc);
        return true;
    }

    static void store_bl2_ram_load_snapshot(
        Bl2RamLoadSnapshot& snapshot, uint32_t state, uint32_t curr_img,
        uint32_t active_slot, uint32_t hdr_addr, uint32_t hdr_magic,
        uint32_t load_addr, uint32_t hdr_size, uint32_t protect_tlv_size,
        uint32_t img_size, uint32_t flags, uint32_t hash_region_size,
        uint32_t slot_img_dst, uint32_t slot_img_sz, uint32_t unsupported_mask)
    {
        snapshot.last_state.store(state, std::memory_order_relaxed);
        snapshot.last_curr_img.store(curr_img, std::memory_order_relaxed);
        snapshot.last_active_slot.store(active_slot, std::memory_order_relaxed);
        snapshot.last_hdr_addr.store(hdr_addr, std::memory_order_relaxed);
        snapshot.last_hdr_magic.store(hdr_magic, std::memory_order_relaxed);
        snapshot.last_load_addr.store(load_addr, std::memory_order_relaxed);
        snapshot.last_hdr_size.store(hdr_size, std::memory_order_relaxed);
        snapshot.last_protect_tlv_size.store(
            protect_tlv_size, std::memory_order_relaxed);
        snapshot.last_img_size.store(img_size, std::memory_order_relaxed);
        snapshot.last_flags.store(flags, std::memory_order_relaxed);
        snapshot.last_hash_region_size.store(
            hash_region_size, std::memory_order_relaxed);
        snapshot.last_slot_img_dst.store(slot_img_dst, std::memory_order_relaxed);
        snapshot.last_slot_img_sz.store(slot_img_sz, std::memory_order_relaxed);
        snapshot.last_unsupported_mask.store(
            unsupported_mask, std::memory_order_relaxed);
    }

    void record_bl2_ram_load_snapshot(
        uint32_t state, uint32_t curr_img, uint32_t active_slot,
        uint32_t hdr_addr, uint32_t hdr_magic, uint32_t load_addr,
        uint32_t hdr_size, uint32_t protect_tlv_size, uint32_t img_size,
        uint32_t flags, uint32_t hash_region_size, uint32_t slot_img_dst,
        uint32_t slot_img_sz, uint32_t unsupported_mask)
    {
        store_bl2_ram_load_snapshot(
            m_bl2_ram_load_snapshot, state, curr_img, active_slot, hdr_addr,
            hdr_magic, load_addr, hdr_size, protect_tlv_size, img_size, flags,
            hash_region_size, slot_img_dst, slot_img_sz, unsupported_mask);
        if (curr_img < m_bl2_ram_load_snapshot_by_image.size()) {
            store_bl2_ram_load_snapshot(
                m_bl2_ram_load_snapshot_by_image[curr_img], state, curr_img,
                active_slot, hdr_addr, hdr_magic, load_addr, hdr_size,
                protect_tlv_size, img_size, flags, hash_region_size,
                slot_img_dst, slot_img_sz, unsupported_mask);
        }
    }

    void account_bl2_ram_load_snapshot(uint32_t curr_img,
                                       uint32_t unsupported_mask)
    {
        if (unsupported_mask != 0) {
            m_bl2_ram_load_snapshot.unsupported.fetch_add(
                1, std::memory_order_relaxed);
            if (curr_img < m_bl2_ram_load_snapshot_by_image.size()) {
                m_bl2_ram_load_snapshot_by_image[curr_img].unsupported.fetch_add(
                    1, std::memory_order_relaxed);
            }
        } else {
            m_bl2_ram_load_snapshot.hits.fetch_add(1, std::memory_order_relaxed);
            if (curr_img < m_bl2_ram_load_snapshot_by_image.size()) {
                m_bl2_ram_load_snapshot_by_image[curr_img].hits.fetch_add(
                    1, std::memory_order_relaxed);
                m_bl2_load_accel_image_decrypted[curr_img].store(
                    false, std::memory_order_relaxed);
            }
        }
    }

    bool capture_bl2_ram_load_snapshot()
    {
        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t state = static_cast<uint32_t>(get_v7m_state(Field::R0));
        m_bl2_ram_load_snapshot.last_state.store(state, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        if (state == 0) {
            unsupported_mask |= 1u;
        }

        const uint64_t image_count = p_bl2_boot_image_count.get_value();
        if (image_count == 0 || image_count > 64) {
            unsupported_mask |= 2u;
        }

        uint8_t curr_img_u8 = 0;
        uint32_t active_slot = 0;
        if (unsupported_mask == 0 &&
            !hotpath_read_u8(
                static_cast<uint64_t>(state) +
                    p_bl2_boot_state_curr_img_offset.get_value(),
                curr_img_u8)) {
            m_bl2_ram_load_snapshot.dmi_failures.fetch_add(
                1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        const uint32_t curr_img = curr_img_u8;
        m_bl2_ram_load_snapshot.last_curr_img.store(
            curr_img, std::memory_order_relaxed);
        if (unsupported_mask == 0 && curr_img >= image_count) {
            unsupported_mask |= 4u;
        }

        const uint64_t slot_usage_addr =
            static_cast<uint64_t>(state) +
            p_bl2_boot_state_slot_usage_offset.get_value() +
            static_cast<uint64_t>(curr_img) *
                p_bl2_boot_state_slot_usage_stride.get_value();
        if (unsupported_mask == 0 &&
            !hotpath_read_u32(slot_usage_addr, active_slot)) {
            m_bl2_ram_load_snapshot.dmi_failures.fetch_add(
                1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_ram_load_snapshot.last_active_slot.store(
            active_slot, std::memory_order_relaxed);
        if (unsupported_mask == 0 && active_slot > 1) {
            unsupported_mask |= 8u;
        }

        const uint64_t hdr_addr =
            static_cast<uint64_t>(state) +
            p_bl2_boot_state_imgs_offset.get_value() +
            static_cast<uint64_t>(curr_img) *
                p_bl2_boot_state_image_stride.get_value() +
            static_cast<uint64_t>(active_slot) *
                p_bl2_boot_state_slot_stride.get_value();
        m_bl2_ram_load_snapshot.last_hdr_addr.store(
            static_cast<uint32_t>(hdr_addr), std::memory_order_relaxed);

        std::vector<uint8_t> hdr_bytes;
        if (unsupported_mask == 0 &&
            !hotpath_read_bytes(hdr_addr,
                                qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE,
                                hdr_bytes)) {
            m_bl2_ram_load_snapshot.dmi_failures.fetch_add(
                1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        qbox::rse_mcuboot_image::ImageHeader image_header;
        size_t hash_region_size = 0;
        if (unsupported_mask == 0 &&
            !qbox::rse_mcuboot_image::parse_header(
                hdr_bytes.data(), hdr_bytes.size(), image_header)) {
            unsupported_mask |= 16u;
        }
        if (unsupported_mask == 0 &&
            !qbox::rse_mcuboot_image::hash_region_size(
                image_header, hash_region_size)) {
            unsupported_mask |= 32u;
        }
        if (unsupported_mask == 0 &&
            hash_region_size > std::numeric_limits<uint32_t>::max()) {
            unsupported_mask |= 64u;
        }

        uint32_t slot_img_dst = 0;
        uint32_t slot_img_sz = 0;
        if (unsupported_mask == 0 &&
            (!hotpath_read_u32(
                 slot_usage_addr +
                     p_bl2_boot_slot_usage_img_dst_offset.get_value(),
                 slot_img_dst) ||
             !hotpath_read_u32(
                 slot_usage_addr +
                     p_bl2_boot_slot_usage_img_sz_offset.get_value(),
                 slot_img_sz))) {
            m_bl2_ram_load_snapshot.dmi_failures.fetch_add(
                1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        record_bl2_ram_load_snapshot(
            state, curr_img, active_slot, static_cast<uint32_t>(hdr_addr),
            image_header.magic, image_header.load_addr, image_header.hdr_size,
            image_header.protect_tlv_size, image_header.img_size,
            image_header.flags, static_cast<uint32_t>(hash_region_size),
            slot_img_dst, slot_img_sz, unsupported_mask);
        account_bl2_ram_load_snapshot(curr_img, unsupported_mask);

        if (unsupported_mask != 0) {
            write_hotpath_profile_file();
            return false;
        }

        return true;
    }

    bool try_bl2_load_profile_at_pc(uint32_t pc)
    {
        if (!p_bl2_load_profile.get_value() && !p_bl2_load_accel.get_value()) {
            return false;
        }

        bool matched = false;
        if (p_bl2_load_profile.get_value()) {
            matched |= record_bl2_profile_site(
                pc, p_bl2_boot_go_for_image_id_addr.get_value(),
                m_bl2_boot_go_for_image_id);
        }

        const uint32_t load_to_sram_entry =
            static_cast<uint32_t>(p_bl2_boot_load_image_to_sram_addr.get_value() & ~1ull);
        const bool load_to_sram_matched =
            load_to_sram_entry != 0 && pc == load_to_sram_entry;
        if (p_bl2_load_profile.get_value() && load_to_sram_matched) {
            capture_bl2_profile_sample(m_bl2_boot_load_image_to_sram, pc);
            m_bl2_boot_load_image_to_sram.hits.fetch_add(
                1, std::memory_order_relaxed);
        }
        matched |= load_to_sram_matched;

        if (p_bl2_load_profile.get_value()) {
            matched |= record_bl2_profile_site(
                pc, p_bl2_boot_enc_load_addr.get_value(),
                m_bl2_boot_enc_load);
            matched |= record_bl2_profile_site(
                pc, p_bl2_boot_enc_decrypt_addr.get_value(),
                m_bl2_boot_enc_decrypt);
            matched |= record_bl2_profile_site(
                pc, p_bl2_bootutil_img_validate_addr.get_value(),
                m_bl2_bootutil_img_validate);
            matched |= record_bl2_profile_site(
                pc, p_bl2_bootutil_img_hash_addr.get_value(),
                m_bl2_bootutil_img_hash);
            matched |= record_bl2_profile_site(
                pc, p_bl2_bootutil_verify_sig_addr.get_value(),
                m_bl2_bootutil_verify_sig);
        }

        if (matched) {
            if (load_to_sram_matched) {
                capture_bl2_ram_load_snapshot();
            }
            maybe_write_hotpath_profile_file();
        }
        return matched;
    }

    static bool bl2_boot_enc_valid_key_size(uint32_t key_size)
    {
        return key_size == 16 || key_size == 24 || key_size == 32;
    }

    static uint64_t bl2_boot_enc_cache_key(uint32_t enc_state, uint32_t slot)
    {
        return (static_cast<uint64_t>(enc_state) << 8) | (slot & 0xffu);
    }

    bool try_bl2_boot_enc_capture_key_at_pc(uint32_t pc)
    {
        if (!p_bl2_boot_enc_accel.get_value() && !p_bl2_load_accel.get_value()) {
            return false;
        }

        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_boot_enc_set_key_addr.get_value() & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t enc_state = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t slot = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t boot_status = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t slots = static_cast<uint32_t>(p_bl2_boot_enc_slots.get_value());
        const uint32_t key_size =
            static_cast<uint32_t>(p_bl2_boot_enc_key_bytes.get_value());
        const uint32_t key_stride =
            static_cast<uint32_t>(p_bl2_boot_enc_key_stride.get_value());
        const uint32_t enckey_offset =
            static_cast<uint32_t>(p_bl2_boot_status_enckey_offset.get_value());

        if (slots == 0 || slot >= slots ||
            !bl2_boot_enc_valid_key_size(key_size) || key_stride < key_size) {
            m_bl2_boot_enc_key_capture_failures.fetch_add(1, std::memory_order_relaxed);
            maybe_write_hotpath_profile_file();
            return false;
        }

        std::vector<uint8_t> key;
        const uint64_t key_addr =
            static_cast<uint64_t>(boot_status) + enckey_offset +
            static_cast<uint64_t>(slot) * key_stride;
        if (!hotpath_read_bytes(key_addr, key_size, key)) {
            m_bl2_boot_enc_key_capture_failures.fetch_add(1, std::memory_order_relaxed);
            maybe_write_hotpath_profile_file();
            return false;
        }

        Bl2EncKeyCacheEntry entry_data;
        entry_data.valid = true;
        entry_data.key_size = key_size;
        std::copy(key.begin(), key.end(), entry_data.key.begin());

        {
            std::lock_guard<std::mutex> lock(m_bl2_boot_enc_lock);
            m_bl2_boot_enc_keys[bl2_boot_enc_cache_key(enc_state, slot)] = entry_data;
        }

        m_bl2_boot_enc_key_captures.fetch_add(1, std::memory_order_relaxed);
        maybe_write_hotpath_profile_file();
        return false;
    }

    bool find_bl2_load_snapshot_for_decrypt(uint32_t off, uint32_t buf,
                                            uint32_t& curr_img,
                                            uint32_t& load_addr,
                                            uint32_t& hdr_size,
                                            uint32_t& img_size,
                                            uint32_t& flags)
    {
        const uint64_t image_count = std::min<uint64_t>(
            p_bl2_boot_image_count.get_value(),
            m_bl2_ram_load_snapshot_by_image.size());
        for (uint64_t image = 0; image < image_count; ++image) {
            const Bl2RamLoadSnapshot& snapshot =
                m_bl2_ram_load_snapshot_by_image[image];
            if (snapshot.hits.load(std::memory_order_relaxed) == 0 ||
                snapshot.last_unsupported_mask.load(std::memory_order_relaxed) != 0) {
                continue;
            }

            const uint32_t candidate_load_addr =
                snapshot.last_load_addr.load(std::memory_order_relaxed);
            const uint32_t candidate_hdr_size =
                snapshot.last_hdr_size.load(std::memory_order_relaxed);
            const uint32_t candidate_img_size =
                snapshot.last_img_size.load(std::memory_order_relaxed);
            const uint32_t candidate_flags =
                snapshot.last_flags.load(std::memory_order_relaxed);
            if (candidate_hdr_size == 0 || candidate_img_size == 0 ||
                off >= candidate_img_size) {
                continue;
            }
            const uint64_t expected_buf =
                static_cast<uint64_t>(candidate_load_addr) + candidate_hdr_size + off;
            if (expected_buf != buf) {
                continue;
            }

            curr_img = static_cast<uint32_t>(image);
            load_addr = candidate_load_addr;
            hdr_size = candidate_hdr_size;
            img_size = candidate_img_size;
            flags = candidate_flags;
            return true;
        }

        return false;
    }

    bool try_bl2_load_decrypt_accel_at_pc(uint32_t pc)
    {
        if (!p_bl2_load_accel.get_value()) {
            return false;
        }

        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_boot_enc_decrypt_addr.get_value() & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t enc_state = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t slot = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t off = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t size = static_cast<uint32_t>(get_v7m_state(Field::R3));
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t blk_off = 0;
        uint32_t buf = 0;

        if (!hotpath_read_u32(sp, blk_off) ||
            !hotpath_read_u32(sp + sizeof(blk_off), buf)) {
            m_bl2_load_accel_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_load_accel_last_slot.store(slot, std::memory_order_relaxed);
        m_bl2_load_accel_last_off.store(off, std::memory_order_relaxed);
        m_bl2_load_accel_last_size.store(size, std::memory_order_relaxed);
        m_bl2_load_accel_last_blk_off.store(blk_off, std::memory_order_relaxed);
        m_bl2_load_accel_last_buf.store(buf, std::memory_order_relaxed);

        uint32_t curr_img = 0;
        uint32_t load_addr = 0;
        uint32_t hdr_size = 0;
        uint32_t img_size = 0;
        uint32_t flags = 0;
        uint32_t unsupported_mask = 0;
        if (!find_bl2_load_snapshot_for_decrypt(
                off, buf, curr_img, load_addr, hdr_size, img_size, flags)) {
            unsupported_mask |= 1u;
        }
        m_bl2_load_accel_last_curr_img.store(curr_img, std::memory_order_relaxed);
        m_bl2_load_accel_last_img_size.store(img_size, std::memory_order_relaxed);
        m_bl2_load_accel_last_flags.store(flags, std::memory_order_relaxed);

        const uint32_t slots = static_cast<uint32_t>(p_bl2_boot_enc_slots.get_value());
        if (slots == 0 || slot >= slots) {
            unsupported_mask |= 2u;
        }
        if (size > p_bl2_boot_enc_max_bytes.get_value()) {
            unsupported_mask |= 4u;
        }
        if (blk_off >= 16) {
            unsupported_mask |= 8u;
        }
        if (buf == 0 && size != 0) {
            unsupported_mask |= 16u;
        }
        if (img_size > p_bl2_load_accel_max_bytes.get_value()) {
            unsupported_mask |= 32u;
        }
        if (flags != (qbox::rse_mcuboot_image::IMAGE_F_RAM_LOAD |
                      qbox::rse_mcuboot_image::IMAGE_F_ENCRYPTED_AES128)) {
            unsupported_mask |= 128u;
        }

        if (unsupported_mask == 0 &&
            curr_img < m_bl2_load_accel_image_decrypted.size() &&
            m_bl2_load_accel_image_decrypted[curr_img].load(
                std::memory_order_relaxed)) {
            const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
            if (!set_v7m_state(Field::PC, return_pc)) {
                m_bl2_load_accel_state_failures.fetch_add(1, std::memory_order_relaxed);
                write_hotpath_profile_file();
                return false;
            }
            m_bl2_load_accel_skip_hits.fetch_add(1, std::memory_order_relaxed);
            m_bl2_load_accel_last_unsupported_mask.store(
                0, std::memory_order_relaxed);
            m_cpu.set_vcpu_dirty(true);
            maybe_write_hotpath_profile_file();
            return true;
        }

        if (off != 0 || blk_off != 0) {
            unsupported_mask |= 64u;
        }
        m_bl2_load_accel_last_unsupported_mask.store(
            unsupported_mask, std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_bl2_load_accel_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        Bl2EncKeyCacheEntry key;
        {
            std::lock_guard<std::mutex> lock(m_bl2_boot_enc_lock);
            const auto it = m_bl2_boot_enc_keys.find(
                bl2_boot_enc_cache_key(enc_state, slot));
            if (it != m_bl2_boot_enc_keys.end()) {
                key = it->second;
            }
        }
        if (!key.valid || !bl2_boot_enc_valid_key_size(key.key_size)) {
            m_bl2_load_accel_key_misses.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        const uint64_t payload_addr = static_cast<uint64_t>(load_addr) + hdr_size;
        std::vector<uint8_t> input;
        std::vector<uint8_t> output(static_cast<size_t>(img_size));
        bool direct_file_alias = false;
        if (!hotpath_read_bytes_or_alias(payload_addr, img_size, input,
                                         direct_file_alias)) {
            m_bl2_load_accel_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::array<uint8_t, 16> counter{};
        if (!qbox::cc3xx::core::aes_ctr_xcrypt_buffer(
                key.key.data(), key.key_size, counter.data(), input.data(),
                img_size, 0, output.data())) {
            m_bl2_load_accel_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        bool write_direct_file_alias = false;
        if (!hotpath_write_bytes_or_alias(payload_addr, output.data(), img_size,
                                          write_direct_file_alias)) {
            m_bl2_load_accel_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }
        if (direct_file_alias || write_direct_file_alias) {
            m_bl2_load_accel_direct_file_alias_hits.fetch_add(
                1, std::memory_order_relaxed);
        }
        m_inst.get().tb_invalidate_phys_range(
            payload_addr, payload_addr + img_size - 1);
        m_bl2_load_accel_image_decrypted[curr_img].store(
            true, std::memory_order_relaxed);

        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (!set_v7m_state(Field::PC, return_pc)) {
            m_bl2_load_accel_state_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_load_accel_hits.fetch_add(1, std::memory_order_relaxed);
        m_bl2_load_accel_bytes.fetch_add(img_size, std::memory_order_relaxed);
        m_cpu.set_vcpu_dirty(true);
        maybe_write_hotpath_profile_file();
        return true;
    }

    bool try_bl2_delay_accel_at_pc(uint32_t pc)
    {
        if (!p_bl2_delay_accel.get_value()) {
            return false;
        }

        const uint32_t norm_pc = pc & ~1u;
        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_delay_cycles_addr.get_value() & ~1ull);
        if (entry == 0 || norm_pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t cycles = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        m_bl2_delay_last_cycles.store(cycles, std::memory_order_relaxed);
        m_bl2_delay_last_lr.store(return_pc, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        if (cycles > p_bl2_delay_max_cycles.get_value()) {
            unsupported_mask |= 1u;
        }
        if (return_pc == 0) {
            unsupported_mask |= 2u;
        }
        m_bl2_delay_last_unsupported_mask.store(
            unsupported_mask, std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_bl2_delay_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        if (!set_v7m_state(Field::R0, 0) ||
            !set_v7m_state(Field::PC, return_pc)) {
            m_bl2_delay_state_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        const uint64_t hits =
            m_bl2_delay_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        m_bl2_delay_cycles.fetch_add(cycles, std::memory_order_relaxed);
        const uint64_t expected_hits = p_bl2_delay_expected_hits.get_value();
        if (expected_hits != 0 && hits >= expected_hits &&
            m_bl2_delay_watch_active.exchange(false, std::memory_order_relaxed)) {
            m_cpu.clear_pc_entry_watches();
        }
        m_cpu.set_vcpu_dirty(true);
        maybe_write_hotpath_profile_file();
        return true;
    }

    void try_bl2_delay_accel()
    {
        if (!p_bl2_delay_accel.get_value()) {
            return;
        }

        if (try_bl2_delay_accel_at_pc(static_cast<uint32_t>(m_cpu.get_pc()))) {
            m_cpu.set_vcpu_dirty(true);
            m_cpu.kick();
        }
    }

    bool try_bl2_boot_enc_decrypt_accel_at_pc(uint32_t pc)
    {
        if (!p_bl2_boot_enc_accel.get_value()) {
            return false;
        }

        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_boot_enc_decrypt_addr.get_value() & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t enc_state = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t slot = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t off = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t size = static_cast<uint32_t>(get_v7m_state(Field::R3));
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t blk_off = 0;
        uint32_t buf = 0;

        if (!hotpath_read_u32(sp, blk_off) ||
            !hotpath_read_u32(sp + sizeof(blk_off), buf)) {
            m_bl2_boot_enc_decrypt_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_boot_enc_last_enc_state.store(enc_state, std::memory_order_relaxed);
        m_bl2_boot_enc_last_slot.store(slot, std::memory_order_relaxed);
        m_bl2_boot_enc_last_off.store(off, std::memory_order_relaxed);
        m_bl2_boot_enc_last_size.store(size, std::memory_order_relaxed);
        m_bl2_boot_enc_last_blk_off.store(blk_off, std::memory_order_relaxed);
        m_bl2_boot_enc_last_buf.store(buf, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        const uint32_t slots = static_cast<uint32_t>(p_bl2_boot_enc_slots.get_value());
        if (slots == 0 || slot >= slots) {
            unsupported_mask |= 1u;
        }
        if (size > p_bl2_boot_enc_max_bytes.get_value()) {
            unsupported_mask |= 2u;
        }
        if (blk_off >= 16) {
            unsupported_mask |= 4u;
        }
        if (buf == 0 && size != 0) {
            unsupported_mask |= 8u;
        }
        m_bl2_boot_enc_last_unsupported_mask.store(unsupported_mask,
                                                   std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_bl2_boot_enc_decrypt_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (size == 0) {
            if (!set_v7m_state(Field::PC, return_pc)) {
                m_bl2_boot_enc_decrypt_state_failures.fetch_add(1, std::memory_order_relaxed);
                write_hotpath_profile_file();
                return false;
            }
            m_bl2_boot_enc_decrypt_hits.fetch_add(1, std::memory_order_relaxed);
            m_cpu.set_vcpu_dirty(true);
            maybe_write_hotpath_profile_file();
            return true;
        }

        Bl2EncKeyCacheEntry key;
        {
            std::lock_guard<std::mutex> lock(m_bl2_boot_enc_lock);
            const auto it = m_bl2_boot_enc_keys.find(bl2_boot_enc_cache_key(enc_state, slot));
            if (it != m_bl2_boot_enc_keys.end()) {
                key = it->second;
            }
        }
        if (!key.valid || !bl2_boot_enc_valid_key_size(key.key_size)) {
            m_bl2_boot_enc_decrypt_key_misses.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::vector<uint8_t> input;
        std::vector<uint8_t> output(static_cast<size_t>(size));
        bool direct_file_alias = false;
        if (!hotpath_read_bytes_or_alias(buf, size, input, direct_file_alias)) {
            m_bl2_boot_enc_decrypt_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::array<uint8_t, 16> counter{};
        const uint32_t block = off >> 4;
        counter[12] = static_cast<uint8_t>(block >> 24);
        counter[13] = static_cast<uint8_t>(block >> 16);
        counter[14] = static_cast<uint8_t>(block >> 8);
        counter[15] = static_cast<uint8_t>(block);

        if (!qbox::cc3xx::core::aes_ctr_xcrypt_buffer(
                key.key.data(), key.key_size, counter.data(), input.data(), size,
                blk_off, output.data())) {
            m_bl2_boot_enc_decrypt_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        bool write_direct_file_alias = false;
        if (!hotpath_write_bytes_or_alias(buf, output.data(), size,
                                          write_direct_file_alias)) {
            m_bl2_boot_enc_decrypt_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }
        if (direct_file_alias || write_direct_file_alias) {
            m_bl2_boot_enc_decrypt_direct_file_alias_hits.fetch_add(
                1, std::memory_order_relaxed);
        }

        m_inst.get().tb_invalidate_phys_range(buf, static_cast<uint64_t>(buf) + size - 1);
        if (!set_v7m_state(Field::PC, return_pc)) {
            m_bl2_boot_enc_decrypt_state_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_boot_enc_decrypt_hits.fetch_add(1, std::memory_order_relaxed);
        m_bl2_boot_enc_decrypt_bytes.fetch_add(size, std::memory_order_relaxed);
        m_cpu.set_vcpu_dirty(true);
        maybe_write_hotpath_profile_file();
        return true;
    }

    bool try_bl2_img_hash_accel_at_pc(uint32_t pc)
    {
        if (!p_bl2_img_hash_accel.get_value()) {
            return false;
        }

        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_bootutil_img_hash_addr.get_value() & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t hdr = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t hash_result = 0;
        uint32_t seed = 0;
        uint32_t seed_len = 0;
        if (!hotpath_read_u32(sp + sizeof(uint32_t), hash_result) ||
            !hotpath_read_u32(sp + 2 * sizeof(uint32_t), seed) ||
            !hotpath_read_u32(sp + 3 * sizeof(uint32_t), seed_len)) {
            m_bl2_img_hash_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_img_hash_last_hdr.store(hdr, std::memory_order_relaxed);
        m_bl2_img_hash_last_hash_result.store(hash_result, std::memory_order_relaxed);
        m_bl2_img_hash_last_seed.store(seed, std::memory_order_relaxed);
        m_bl2_img_hash_last_seed_len.store(seed_len, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        if (hdr == 0) {
            unsupported_mask |= 1u;
        }
        if (hash_result == 0) {
            unsupported_mask |= 2u;
        }
        if (seed_len > p_bl2_img_hash_max_seed_bytes.get_value()) {
            unsupported_mask |= 4u;
        }
        if (seed == 0 && seed_len != 0) {
            unsupported_mask |= 8u;
        }

        std::vector<uint8_t> hdr_bytes;
        if (unsupported_mask == 0 &&
            !hotpath_read_bytes(hdr, qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE,
                                hdr_bytes)) {
            m_bl2_img_hash_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        qbox::rse_mcuboot_image::ImageHeader image_header;
        size_t hash_size = 0;
        if (unsupported_mask == 0 &&
            !qbox::rse_mcuboot_image::parse_header(
                hdr_bytes.data(), hdr_bytes.size(), image_header)) {
            unsupported_mask |= 16u;
        }
        if (unsupported_mask == 0 &&
            !qbox::rse_mcuboot_image::hash_region_size(image_header, hash_size)) {
            unsupported_mask |= 32u;
        }
        if (unsupported_mask == 0 &&
            hash_size > p_bl2_img_hash_max_bytes.get_value()) {
            unsupported_mask |= 64u;
        }

        m_bl2_img_hash_last_load_addr.store(image_header.load_addr,
                                            std::memory_order_relaxed);
        m_bl2_img_hash_last_hash_size.store(static_cast<uint32_t>(hash_size),
                                            std::memory_order_relaxed);
        m_bl2_img_hash_last_unsupported_mask.store(
            unsupported_mask, std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_bl2_img_hash_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::vector<uint8_t> image;
        bool direct_file_alias = false;
        if (!hotpath_read_bytes_or_alias(image_header.load_addr, hash_size,
                                         image, direct_file_alias)) {
            m_bl2_img_hash_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }
        if (direct_file_alias) {
            m_bl2_img_hash_direct_file_alias_hits.fetch_add(
                1, std::memory_order_relaxed);
        }

        std::vector<uint8_t> seed_bytes;
        if (seed_len != 0) {
            bool seed_direct_file_alias = false;
            if (!hotpath_read_bytes_or_alias(seed, seed_len, seed_bytes,
                                             seed_direct_file_alias)) {
                m_bl2_img_hash_dmi_failures.fetch_add(1, std::memory_order_relaxed);
                write_hotpath_profile_file();
                return false;
            }
            if (seed_direct_file_alias) {
                m_bl2_img_hash_direct_file_alias_hits.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }

        const auto digest = qbox::rse_mcuboot_image::sha256(
            seed_len == 0 ? nullptr : seed_bytes.data(), seed_bytes.size(),
            image.data(), image.size());
        if (!hotpath_write_bytes(hash_result, digest.data(), digest.size())) {
            m_bl2_img_hash_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (!set_v7m_state(Field::R0, 0) ||
            !set_v7m_state(Field::PC, return_pc)) {
            m_bl2_img_hash_state_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_img_hash_hits.fetch_add(1, std::memory_order_relaxed);
        m_bl2_img_hash_bytes.fetch_add(hash_size, std::memory_order_relaxed);
        m_cpu.set_vcpu_dirty(true);
        maybe_write_hotpath_profile_file();
        return true;
    }

    bool try_bl2_verify_sig_accel_at_pc(uint32_t pc)
    {
        if (!p_bl2_verify_sig_accel.get_value()) {
            return false;
        }

        const uint32_t entry =
            static_cast<uint32_t>(p_bl2_bootutil_verify_sig_addr.get_value() & ~1ull);
        if (entry == 0 || pc != entry) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t hash = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t hlen = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t sig = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t slen = static_cast<uint32_t>(get_v7m_state(Field::R3));
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t key_id = 0;
        if (!hotpath_read_u32(sp, key_id)) {
            m_bl2_verify_sig_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_verify_sig_last_hash.store(hash, std::memory_order_relaxed);
        m_bl2_verify_sig_last_hlen.store(hlen, std::memory_order_relaxed);
        m_bl2_verify_sig_last_sig.store(sig, std::memory_order_relaxed);
        m_bl2_verify_sig_last_slen.store(slen, std::memory_order_relaxed);
        m_bl2_verify_sig_last_key_id.store(key_id, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        if (hlen != 32) {
            unsupported_mask |= 1u;
        }
        if (slen == 0 || slen > p_bl2_verify_sig_max_sig_bytes.get_value()) {
            unsupported_mask |= 2u;
        }
        if (hash == 0 || sig == 0) {
            unsupported_mask |= 4u;
        }
        if (key_id > 0xffu) {
            unsupported_mask |= 8u;
        }
        const uint32_t keys_addr =
            static_cast<uint32_t>(p_bl2_bootutil_keys_addr.get_value());
        if (keys_addr == 0) {
            unsupported_mask |= 16u;
        }

        const uint32_t key_cnt_addr =
            static_cast<uint32_t>(p_bl2_bootutil_key_cnt_addr.get_value());
        if (key_cnt_addr != 0) {
            uint32_t key_cnt = 0;
            if (!hotpath_read_u32(key_cnt_addr, key_cnt)) {
                m_bl2_verify_sig_dmi_failures.fetch_add(1, std::memory_order_relaxed);
                write_hotpath_profile_file();
                return false;
            }
            if (key_id >= key_cnt) {
                unsupported_mask |= 32u;
            }
        }

        m_bl2_verify_sig_last_unsupported_mask.store(
            unsupported_mask, std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_bl2_verify_sig_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        uint32_t key_ptr = 0;
        uint32_t key_len_ptr = 0;
        uint32_t key_len = 0;
        const uint32_t key_entry_addr = keys_addr + key_id * 8u;
        if (!hotpath_read_u32(key_entry_addr, key_ptr) ||
            !hotpath_read_u32(key_entry_addr + sizeof(key_ptr), key_len_ptr) ||
            !hotpath_read_u32(key_len_ptr, key_len)) {
            m_bl2_verify_sig_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_verify_sig_last_key_ptr.store(key_ptr, std::memory_order_relaxed);
        m_bl2_verify_sig_last_key_len.store(key_len, std::memory_order_relaxed);
        if (key_ptr == 0 || key_len == 0 ||
            key_len > p_bl2_verify_sig_max_key_bytes.get_value()) {
            m_bl2_verify_sig_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::vector<uint8_t> public_key;
        std::vector<uint8_t> hash_bytes;
        std::vector<uint8_t> signature;
        if (!hotpath_read_bytes(key_ptr, key_len, public_key) ||
            !hotpath_read_bytes(hash, hlen, hash_bytes) ||
            !hotpath_read_bytes(sig, slen, signature)) {
            m_bl2_verify_sig_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        bool verified = false;
        bool cached = false;
        {
            std::lock_guard<std::mutex> lock(m_bl2_verify_sig_cache_lock);
            for (const auto& entry : m_bl2_verify_sig_cache) {
                if (entry.public_key == public_key && entry.hash == hash_bytes &&
                    entry.signature == signature) {
                    verified = entry.verified;
                    cached = true;
                    break;
                }
            }
        }

        if (cached) {
            m_bl2_verify_sig_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_bl2_verify_sig_cache_misses.fetch_add(1, std::memory_order_relaxed);
            verified = qbox::rse_p256_ecdsa::verify(public_key, hash_bytes, signature);

            Bl2VerifySigCacheEntry entry;
            entry.public_key = public_key;
            entry.hash = hash_bytes;
            entry.signature = signature;
            entry.verified = verified;
            {
                std::lock_guard<std::mutex> lock(m_bl2_verify_sig_cache_lock);
                static constexpr size_t MAX_VERIFY_SIG_CACHE_ENTRIES = 16;
                if (m_bl2_verify_sig_cache.size() >= MAX_VERIFY_SIG_CACHE_ENTRIES) {
                    m_bl2_verify_sig_cache.erase(m_bl2_verify_sig_cache.begin());
                }
                m_bl2_verify_sig_cache.push_back(entry);
            }
        }

        if (!verified) {
            m_bl2_verify_sig_verify_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_verify_sig_matches.fetch_add(1, std::memory_order_relaxed);
        if (!p_bl2_verify_sig_skip.get_value()) {
            maybe_write_hotpath_profile_file();
            return false;
        }

        uint32_t fih_success = 0;
        const uint32_t fih_success_addr =
            static_cast<uint32_t>(p_bl2_fih_success_addr.get_value());
        if (fih_success_addr == 0 ||
            !hotpath_read_u32(fih_success_addr, fih_success)) {
            m_bl2_verify_sig_dmi_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }
        m_bl2_verify_sig_last_fih_success.store(
            fih_success, std::memory_order_relaxed);

        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (!set_v7m_state(Field::R0, fih_success) ||
            !set_v7m_state(Field::PC, return_pc)) {
            m_bl2_verify_sig_state_failures.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_bl2_verify_sig_skip_hits.fetch_add(1, std::memory_order_relaxed);
        m_cpu.set_vcpu_dirty(true);
        maybe_write_hotpath_profile_file();
        return true;
    }

    void write_bl2_profile_site(std::ostream& out, const char* json_name,
                                uint64_t addr, const Bl2ProfileSite& site,
                                bool trailing_comma) const
    {
        const Bl2ProfileSample& last = site.last;
        out << "      \"" << json_name << "\": {\n"
            << "        \"addr\": \"" << profile_hex_string(addr) << "\",\n"
            << "        \"hits\": " << site.hits.load(std::memory_order_relaxed) << ",\n"
            << "        \"last\": {\n"
            << "          \"pc\": \"" << profile_hex_string(last.pc.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"r0\": \"" << profile_hex_string(last.r0.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"r1\": \"" << profile_hex_string(last.r1.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"r2\": \"" << profile_hex_string(last.r2.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"r3\": \"" << profile_hex_string(last.r3.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"sp\": \"" << profile_hex_string(last.sp.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"lr\": \"" << profile_hex_string(last.lr.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"stack_words\": " << last.stack_words.load(std::memory_order_relaxed) << ",\n"
            << "          \"stack0\": \"" << profile_hex_string(last.stack0.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"stack1\": \"" << profile_hex_string(last.stack1.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"stack2\": \"" << profile_hex_string(last.stack2.load(std::memory_order_relaxed)) << "\",\n"
            << "          \"stack3\": \"" << profile_hex_string(last.stack3.load(std::memory_order_relaxed)) << "\"\n"
            << "        }\n"
            << "      }" << (trailing_comma ? "," : "") << "\n";
    }

    static void write_bl2_ram_load_snapshot_values(
        std::ostream& out, const Bl2RamLoadSnapshot& snapshot,
        const char* indent)
    {
        out << indent << "\"state\": \""
            << profile_hex_string(snapshot.last_state.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"curr_img\": "
            << snapshot.last_curr_img.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"active_slot\": "
            << snapshot.last_active_slot.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"hdr_addr\": \""
            << profile_hex_string(snapshot.last_hdr_addr.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"hdr_magic\": \""
            << profile_hex_string(snapshot.last_hdr_magic.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"load_addr\": \""
            << profile_hex_string(snapshot.last_load_addr.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"hdr_size\": "
            << snapshot.last_hdr_size.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"protect_tlv_size\": "
            << snapshot.last_protect_tlv_size.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"img_size\": "
            << snapshot.last_img_size.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"flags\": \""
            << profile_hex_string(snapshot.last_flags.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"hash_region_size\": "
            << snapshot.last_hash_region_size.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"slot_img_dst\": \""
            << profile_hex_string(snapshot.last_slot_img_dst.load(std::memory_order_relaxed)) << "\",\n"
            << indent << "\"slot_img_sz\": "
            << snapshot.last_slot_img_sz.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"unsupported_mask\": "
            << snapshot.last_unsupported_mask.load(std::memory_order_relaxed) << "\n";
    }

    static void write_bl2_ram_load_snapshot_object(
        std::ostream& out, const Bl2RamLoadSnapshot& snapshot,
        const char* indent, const char* value_indent)
    {
        out << indent << "\"hits\": "
            << snapshot.hits.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"dmi_failures\": "
            << snapshot.dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"unsupported\": "
            << snapshot.unsupported.load(std::memory_order_relaxed) << ",\n"
            << indent << "\"last\": {\n";
        write_bl2_ram_load_snapshot_values(out, snapshot, value_indent);
        out << indent << "}\n";
    }

    void write_hotpath_profile_file()
    {
        const std::string profile_file = p_hotpath_profile_file.get_value();
        if (profile_file.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_hotpath_profile_lock);
        std::ofstream out(profile_file, std::ios::out | std::ios::trunc);
        if (!out) {
            std::cerr << name() << " hotpath_profile_error file="
                      << profile_file << std::endl;
            return;
        }

        const auto hex_string = [](uint64_t value) {
            return profile_hex_string(value);
        };

        out << "{\n"
            << "  \"name\": \"" << name() << "\",\n"
            << "  \"enabled\": " << (p_hotpath_accel.get_value() ? "true" : "false") << ",\n"
            << "  \"memcpy_addr\": \"" << hex_string(p_hotpath_memcpy_addr.get_value()) << "\",\n"
            << "  \"memset_addr\": \"" << hex_string(p_hotpath_memset_addr.get_value()) << "\",\n"
            << "  \"max_bytes\": " << p_hotpath_max_bytes.get_value() << ",\n"
            << "  \"memcpy_hits\": " << m_hotpath_memcpy_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"memset_hits\": " << m_hotpath_memset_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"fallbacks\": " << m_hotpath_fallbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"tlm_fallback_enabled\": " << (p_hotpath_tlm_fallback.get_value() ? "true" : "false") << ",\n"
            << "  \"tlm_fallbacks\": " << m_hotpath_tlm_fallbacks.load(std::memory_order_relaxed) << ",\n"
            << "  \"pc_misses\": " << m_hotpath_pc_misses.load(std::memory_order_relaxed) << ",\n"
            << "  \"too_large_failures\": " << m_hotpath_too_large_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"dmi_failures\": " << m_hotpath_dmi_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"stack_failures\": " << m_hotpath_stack_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"state_failures\": " << m_hotpath_state_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_accel_enabled\": " << (p_lms_accel.get_value() ? "true" : "false") << ",\n"
            << "  \"lms_verify_addr\": \"" << hex_string(p_lms_verify_addr.get_value()) << "\",\n"
            << "  \"lms_hits\": " << m_lms_accel_hits.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_pc_misses\": " << m_lms_accel_pc_misses.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_dmi_failures\": " << m_lms_accel_dmi_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_verify_failures\": " << m_lms_accel_verify_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_state_failures\": " << m_lms_accel_state_fails.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_unsupported\": " << m_lms_accel_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_last_key_addr\": \"" << hex_string(m_lms_last_key_addr.load(std::memory_order_relaxed)) << "\",\n"
            << "  \"lms_last_key_size\": " << m_lms_last_key_size.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_last_data_addr\": \"" << hex_string(m_lms_last_data_addr.load(std::memory_order_relaxed)) << "\",\n"
            << "  \"lms_last_data_size\": " << m_lms_last_data_size.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_last_sig_addr\": \"" << hex_string(m_lms_last_sig_addr.load(std::memory_order_relaxed)) << "\",\n"
            << "  \"lms_last_sig_size\": " << m_lms_last_sig_size.load(std::memory_order_relaxed) << ",\n"
            << "  \"lms_last_unsupported_mask\": " << m_lms_last_unsupported_mask.load(std::memory_order_relaxed) << ",\n"
            << "  \"bl2_delay_accel\": {\n"
            << "    \"enabled\": " << (p_bl2_delay_accel.get_value() ? "true" : "false") << ",\n"
            << "    \"delay_cycles_addr\": \"" << hex_string(p_bl2_delay_cycles_addr.get_value()) << "\",\n"
            << "    \"max_cycles\": " << p_bl2_delay_max_cycles.get_value() << ",\n"
            << "    \"watch_count\": " << m_cpu.pc_entry_watch_count() << ",\n"
            << "    \"watch_add_calls\": " << m_cpu.pc_entry_watch_add_calls() << ",\n"
            << "    \"watch_clear_calls\": " << m_cpu.pc_entry_watch_clear_calls() << ",\n"
            << "    \"watch_match_queries\": " << m_cpu.pc_entry_watch_match_queries() << ",\n"
            << "    \"watch_match_hits\": " << m_cpu.pc_entry_watch_match_hits() << ",\n"
            << "    \"watch_last_pc\": \"" << hex_string(m_cpu.pc_entry_watch_last_pc()) << "\",\n"
            << "    \"watch_last_watch_pc\": \"" << hex_string(m_cpu.pc_entry_watch_last_watch_pc()) << "\",\n"
            << "    \"hits\": " << m_bl2_delay_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"cycles\": " << m_bl2_delay_cycles.load(std::memory_order_relaxed) << ",\n"
            << "    \"expected_hits\": " << p_bl2_delay_expected_hits.get_value() << ",\n"
            << "    \"state_failures\": " << m_bl2_delay_state_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"unsupported\": " << m_bl2_delay_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_cycles\": " << m_bl2_delay_last_cycles.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_lr\": \"" << hex_string(m_bl2_delay_last_lr.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_unsupported_mask\": " << m_bl2_delay_last_unsupported_mask.load(std::memory_order_relaxed) << "\n"
            << "  },\n"
            << "  \"bl2_load_accel\": {\n"
            << "    \"enabled\": " << (p_bl2_load_accel.get_value() ? "true" : "false") << ",\n"
            << "    \"decrypt_addr\": \"" << hex_string(p_bl2_boot_enc_decrypt_addr.get_value()) << "\",\n"
            << "    \"max_bytes\": " << p_bl2_load_accel_max_bytes.get_value() << ",\n"
            << "    \"hits\": " << m_bl2_load_accel_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"skip_hits\": " << m_bl2_load_accel_skip_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"bytes\": " << m_bl2_load_accel_bytes.load(std::memory_order_relaxed) << ",\n"
            << "    \"key_misses\": " << m_bl2_load_accel_key_misses.load(std::memory_order_relaxed) << ",\n"
            << "    \"dmi_failures\": " << m_bl2_load_accel_dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"direct_file_alias_hits\": " << m_bl2_load_accel_direct_file_alias_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"state_failures\": " << m_bl2_load_accel_state_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"unsupported\": " << m_bl2_load_accel_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_curr_img\": " << m_bl2_load_accel_last_curr_img.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_slot\": " << m_bl2_load_accel_last_slot.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_off\": \"" << hex_string(m_bl2_load_accel_last_off.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_size\": " << m_bl2_load_accel_last_size.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_blk_off\": " << m_bl2_load_accel_last_blk_off.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_buf\": \"" << hex_string(m_bl2_load_accel_last_buf.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_img_size\": " << m_bl2_load_accel_last_img_size.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_flags\": \"" << hex_string(m_bl2_load_accel_last_flags.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_unsupported_mask\": " << m_bl2_load_accel_last_unsupported_mask.load(std::memory_order_relaxed) << "\n"
            << "  },\n"
            << "  \"bl2_boot_enc_accel\": {\n"
            << "    \"enabled\": " << (p_bl2_boot_enc_accel.get_value() ? "true" : "false") << ",\n"
            << "    \"set_key_addr\": \"" << hex_string(p_bl2_boot_enc_set_key_addr.get_value()) << "\",\n"
            << "    \"decrypt_addr\": \"" << hex_string(p_bl2_boot_enc_decrypt_addr.get_value()) << "\",\n"
            << "    \"boot_status_enckey_offset\": \"" << hex_string(p_bl2_boot_status_enckey_offset.get_value()) << "\",\n"
            << "    \"key_bytes\": " << p_bl2_boot_enc_key_bytes.get_value() << ",\n"
            << "    \"key_stride\": " << p_bl2_boot_enc_key_stride.get_value() << ",\n"
            << "    \"slots\": " << p_bl2_boot_enc_slots.get_value() << ",\n"
            << "    \"max_bytes\": " << p_bl2_boot_enc_max_bytes.get_value() << ",\n"
            << "    \"key_captures\": " << m_bl2_boot_enc_key_captures.load(std::memory_order_relaxed) << ",\n"
            << "    \"key_capture_failures\": " << m_bl2_boot_enc_key_capture_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_hits\": " << m_bl2_boot_enc_decrypt_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_bytes\": " << m_bl2_boot_enc_decrypt_bytes.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_key_misses\": " << m_bl2_boot_enc_decrypt_key_misses.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_dmi_failures\": " << m_bl2_boot_enc_decrypt_dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_direct_file_alias_hits\": " << m_bl2_boot_enc_decrypt_direct_file_alias_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_state_failures\": " << m_bl2_boot_enc_decrypt_state_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"decrypt_unsupported\": " << m_bl2_boot_enc_decrypt_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_enc_state\": \"" << hex_string(m_bl2_boot_enc_last_enc_state.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_slot\": " << m_bl2_boot_enc_last_slot.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_off\": \"" << hex_string(m_bl2_boot_enc_last_off.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_size\": " << m_bl2_boot_enc_last_size.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_blk_off\": " << m_bl2_boot_enc_last_blk_off.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_buf\": \"" << hex_string(m_bl2_boot_enc_last_buf.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_unsupported_mask\": " << m_bl2_boot_enc_last_unsupported_mask.load(std::memory_order_relaxed) << "\n"
            << "  },\n"
            << "  \"bl2_img_hash_accel\": {\n"
            << "    \"enabled\": " << (p_bl2_img_hash_accel.get_value() ? "true" : "false") << ",\n"
            << "    \"img_hash_addr\": \"" << hex_string(p_bl2_bootutil_img_hash_addr.get_value()) << "\",\n"
            << "    \"max_bytes\": " << p_bl2_img_hash_max_bytes.get_value() << ",\n"
            << "    \"max_seed_bytes\": " << p_bl2_img_hash_max_seed_bytes.get_value() << ",\n"
            << "    \"hits\": " << m_bl2_img_hash_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"bytes\": " << m_bl2_img_hash_bytes.load(std::memory_order_relaxed) << ",\n"
            << "    \"dmi_failures\": " << m_bl2_img_hash_dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"direct_file_alias_hits\": " << m_bl2_img_hash_direct_file_alias_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"state_failures\": " << m_bl2_img_hash_state_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"unsupported\": " << m_bl2_img_hash_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_hdr\": \"" << hex_string(m_bl2_img_hash_last_hdr.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_load_addr\": \"" << hex_string(m_bl2_img_hash_last_load_addr.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_hash_result\": \"" << hex_string(m_bl2_img_hash_last_hash_result.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_hash_size\": " << m_bl2_img_hash_last_hash_size.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_seed\": \"" << hex_string(m_bl2_img_hash_last_seed.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_seed_len\": " << m_bl2_img_hash_last_seed_len.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_unsupported_mask\": " << m_bl2_img_hash_last_unsupported_mask.load(std::memory_order_relaxed) << "\n"
            << "  },\n"
            << "  \"bl2_verify_sig_accel\": {\n"
            << "    \"enabled\": " << (p_bl2_verify_sig_accel.get_value() ? "true" : "false") << ",\n"
            << "    \"verify_sig_addr\": \"" << hex_string(p_bl2_bootutil_verify_sig_addr.get_value()) << "\",\n"
            << "    \"bootutil_keys_addr\": \"" << hex_string(p_bl2_bootutil_keys_addr.get_value()) << "\",\n"
            << "    \"bootutil_key_cnt_addr\": \"" << hex_string(p_bl2_bootutil_key_cnt_addr.get_value()) << "\",\n"
            << "    \"max_key_bytes\": " << p_bl2_verify_sig_max_key_bytes.get_value() << ",\n"
            << "    \"max_sig_bytes\": " << p_bl2_verify_sig_max_sig_bytes.get_value() << ",\n"
            << "    \"skip_enabled\": " << (p_bl2_verify_sig_skip.get_value() ? "true" : "false") << ",\n"
            << "    \"verify_matches\": " << m_bl2_verify_sig_matches.load(std::memory_order_relaxed) << ",\n"
            << "    \"cache_hits\": " << m_bl2_verify_sig_cache_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"cache_misses\": " << m_bl2_verify_sig_cache_misses.load(std::memory_order_relaxed) << ",\n"
            << "    \"skip_hits\": " << m_bl2_verify_sig_skip_hits.load(std::memory_order_relaxed) << ",\n"
            << "    \"dmi_failures\": " << m_bl2_verify_sig_dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"verify_failures\": " << m_bl2_verify_sig_verify_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"state_failures\": " << m_bl2_verify_sig_state_failures.load(std::memory_order_relaxed) << ",\n"
            << "    \"unsupported\": " << m_bl2_verify_sig_unsupported.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_hash\": \"" << hex_string(m_bl2_verify_sig_last_hash.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_hlen\": " << m_bl2_verify_sig_last_hlen.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_sig\": \"" << hex_string(m_bl2_verify_sig_last_sig.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_slen\": " << m_bl2_verify_sig_last_slen.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_key_id\": " << m_bl2_verify_sig_last_key_id.load(std::memory_order_relaxed) << ",\n"
            << "    \"last_key_ptr\": \"" << hex_string(m_bl2_verify_sig_last_key_ptr.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_key_len\": " << m_bl2_verify_sig_last_key_len.load(std::memory_order_relaxed) << ",\n"
            << "    \"fih_success_addr\": \"" << hex_string(p_bl2_fih_success_addr.get_value()) << "\",\n"
            << "    \"last_fih_success\": \"" << hex_string(m_bl2_verify_sig_last_fih_success.load(std::memory_order_relaxed)) << "\",\n"
            << "    \"last_unsupported_mask\": " << m_bl2_verify_sig_last_unsupported_mask.load(std::memory_order_relaxed) << "\n"
            << "  },\n"
            << "  \"bl2_load_profile\": {\n"
            << "    \"enabled\": " << (p_bl2_load_profile.get_value() ? "true" : "false") << ",\n"
            << "    \"pc_misses\": " << m_bl2_load_profile_pc_misses.load(std::memory_order_relaxed) << ",\n"
            << "    \"stack_failures\": " << m_bl2_load_profile_stack_fails.load(std::memory_order_relaxed) << ",\n"
            << "    \"ram_load_snapshot\": {\n"
            << "      \"hits\": " << m_bl2_ram_load_snapshot.hits.load(std::memory_order_relaxed) << ",\n"
            << "      \"dmi_failures\": " << m_bl2_ram_load_snapshot.dmi_failures.load(std::memory_order_relaxed) << ",\n"
            << "      \"unsupported\": " << m_bl2_ram_load_snapshot.unsupported.load(std::memory_order_relaxed) << ",\n"
            << "      \"layout\": {\n"
            << "        \"image_count\": " << p_bl2_boot_image_count.get_value() << ",\n"
            << "        \"curr_img_offset\": \"" << hex_string(p_bl2_boot_state_curr_img_offset.get_value()) << "\",\n"
            << "        \"imgs_offset\": \"" << hex_string(p_bl2_boot_state_imgs_offset.get_value()) << "\",\n"
            << "        \"image_stride\": " << p_bl2_boot_state_image_stride.get_value() << ",\n"
            << "        \"slot_stride\": " << p_bl2_boot_state_slot_stride.get_value() << ",\n"
            << "        \"slot_usage_offset\": \"" << hex_string(p_bl2_boot_state_slot_usage_offset.get_value()) << "\",\n"
            << "        \"slot_usage_stride\": " << p_bl2_boot_state_slot_usage_stride.get_value() << ",\n"
            << "        \"slot_img_dst_offset\": \"" << hex_string(p_bl2_boot_slot_usage_img_dst_offset.get_value()) << "\",\n"
            << "        \"slot_img_sz_offset\": \"" << hex_string(p_bl2_boot_slot_usage_img_sz_offset.get_value()) << "\"\n"
            << "      },\n"
            << "      \"last\": {\n";
        write_bl2_ram_load_snapshot_values(out, m_bl2_ram_load_snapshot, "        ");
        out << "      },\n"
            << "      \"by_image\": {\n";
        const uint64_t snapshot_image_count = std::min<uint64_t>(
            p_bl2_boot_image_count.get_value(),
            m_bl2_ram_load_snapshot_by_image.size());
        bool first_snapshot = true;
        for (uint64_t image = 0; image < snapshot_image_count; ++image) {
            const Bl2RamLoadSnapshot& snapshot =
                m_bl2_ram_load_snapshot_by_image[image];
            const uint64_t events =
                snapshot.hits.load(std::memory_order_relaxed) +
                snapshot.dmi_failures.load(std::memory_order_relaxed) +
                snapshot.unsupported.load(std::memory_order_relaxed);
            if (events == 0) {
                continue;
            }
            if (!first_snapshot) {
                out << ",\n";
            }
            first_snapshot = false;
            out << "        \"" << image << "\": {\n";
            write_bl2_ram_load_snapshot_object(
                out, snapshot, "          ", "            ");
            out << "        }";
        }
        out << "\n"
            << "      }\n"
            << "    },\n"
            << "    \"sites\": {\n";
        write_bl2_profile_site(
            out, "boot_go_for_image_id", p_bl2_boot_go_for_image_id_addr.get_value(),
            m_bl2_boot_go_for_image_id, true);
        write_bl2_profile_site(
            out, "boot_load_image_to_sram", p_bl2_boot_load_image_to_sram_addr.get_value(),
            m_bl2_boot_load_image_to_sram, true);
        write_bl2_profile_site(
            out, "boot_enc_load", p_bl2_boot_enc_load_addr.get_value(),
            m_bl2_boot_enc_load, true);
        write_bl2_profile_site(
            out, "boot_enc_decrypt", p_bl2_boot_enc_decrypt_addr.get_value(),
            m_bl2_boot_enc_decrypt, true);
        write_bl2_profile_site(
            out, "bootutil_img_validate", p_bl2_bootutil_img_validate_addr.get_value(),
            m_bl2_bootutil_img_validate, true);
        write_bl2_profile_site(
            out, "bootutil_img_hash", p_bl2_bootutil_img_hash_addr.get_value(),
            m_bl2_bootutil_img_hash, true);
        write_bl2_profile_site(
            out, "bootutil_verify_sig", p_bl2_bootutil_verify_sig_addr.get_value(),
            m_bl2_bootutil_verify_sig, false);
        out << "    }\n"
            << "  }\n"
            << "}\n";
    }

    void maybe_write_hotpath_profile_file()
    {
        const uint64_t interval = p_hotpath_profile_interval.get_value();
        if (interval == 0) {
            return;
        }

        const uint64_t events =
            m_hotpath_memcpy_hits.load(std::memory_order_relaxed) +
            m_hotpath_memset_hits.load(std::memory_order_relaxed) +
            m_hotpath_fallbacks.load(std::memory_order_relaxed) +
            m_lms_accel_hits.load(std::memory_order_relaxed) +
            m_lms_accel_pc_misses.load(std::memory_order_relaxed) +
            m_lms_accel_dmi_fails.load(std::memory_order_relaxed) +
            m_lms_accel_verify_fails.load(std::memory_order_relaxed) +
            m_lms_accel_state_fails.load(std::memory_order_relaxed) +
            m_lms_accel_unsupported.load(std::memory_order_relaxed) +
            bl2_load_profile_events() +
            bl2_load_accel_events() +
            bl2_boot_enc_accel_events() +
            bl2_img_hash_accel_events() +
            bl2_verify_sig_accel_events() +
            bl2_delay_accel_events();
        if (events != 0 && (events % interval) == 0) {
            write_hotpath_profile_file();
        }
    }

    void record_hotpath_failure(HotpathFailReason reason)
    {
        m_hotpath_fallbacks.fetch_add(1, std::memory_order_relaxed);
        switch (reason) {
        case HotpathFailReason::PcMismatch:
            m_hotpath_pc_misses.fetch_add(1, std::memory_order_relaxed);
            break;
        case HotpathFailReason::TooLarge:
            m_hotpath_too_large_fails.fetch_add(1, std::memory_order_relaxed);
            break;
        case HotpathFailReason::Dmi:
            m_hotpath_dmi_fails.fetch_add(1, std::memory_order_relaxed);
            break;
        case HotpathFailReason::Stack:
            m_hotpath_stack_fails.fetch_add(1, std::memory_order_relaxed);
            break;
        case HotpathFailReason::State:
            m_hotpath_state_fails.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        maybe_write_hotpath_profile_file();
    }

    bool hotpath_finish_stacked_lr(uint32_t& return_pc,
                                   HotpathFailReason& reason)
    {
        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        if (!hotpath_read_u32(sp, return_pc)) {
            reason = HotpathFailReason::Stack;
            return false;
        }
        if (!set_v7m_state(Field::SP, sp + sizeof(return_pc)) ||
            !set_v7m_state(Field::PC, return_pc)) {
            reason = HotpathFailReason::State;
            return false;
        }
        return true;
    }

    bool hotpath_finish_memset(uint32_t return_pc,
                               HotpathFailReason& reason)
    {
        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t saved_r4 = 0;
        if (!hotpath_read_u32(sp, saved_r4)) {
            reason = HotpathFailReason::Stack;
            return false;
        }
        if (!set_v7m_state(Field::R4, saved_r4) ||
            !set_v7m_state(Field::SP, sp + sizeof(saved_r4)) ||
            !set_v7m_state(Field::PC, return_pc)) {
            reason = HotpathFailReason::State;
            return false;
        }
        return true;
    }

    bool hotpath_memcpy(uint32_t pc, uint32_t base, uint64_t max_bytes,
                        HotpathFailReason& reason)
    {
        if (!hotpath_memcpy_safe_pc(pc, base)) {
            reason = HotpathFailReason::PcMismatch;
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t dest = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t src = pc == base
                                 ? static_cast<uint32_t>(get_v7m_state(Field::R1))
                                 : static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t remaining = pc == base
                                       ? static_cast<uint32_t>(get_v7m_state(Field::R2))
                                       : static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t current_dest = pc == base
                                          ? dest
                                          : static_cast<uint32_t>(get_v7m_state(Field::R3));
        if (remaining == 0) {
            uint32_t return_pc = 0;
            if (pc == base) {
                return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
                if (!set_v7m_state(Field::PC, return_pc)) {
                    reason = HotpathFailReason::State;
                    return false;
                }
                return true;
            }
            return hotpath_finish_stacked_lr(return_pc, reason);
        }
        if (remaining > max_bytes) {
            reason = HotpathFailReason::TooLarge;
            return false;
        }

        uint8_t* src_ptr = nullptr;
        uint8_t* dst_ptr = nullptr;
        if (!hotpath_dmi_ptr(src, remaining, true, false, src_ptr) ||
            !hotpath_dmi_ptr(current_dest, remaining, false, true, dst_ptr)) {
            reason = HotpathFailReason::Dmi;
            return false;
        }

        std::memmove(dst_ptr, src_ptr, remaining);
        if (remaining != 0) {
            m_inst.get().tb_invalidate_phys_range(current_dest, current_dest + remaining - 1);
        }

        if (pc == base) {
            const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
            if (!set_v7m_state(Field::PC, return_pc)) {
                reason = HotpathFailReason::State;
                return false;
            }
        } else {
            uint32_t return_pc = 0;
            if (!hotpath_finish_stacked_lr(return_pc, reason)) {
                return false;
            }
        }
        m_hotpath_memcpy_hits.fetch_add(1, std::memory_order_relaxed);
        maybe_write_hotpath_profile_file();
        return true;
    }

    bool hotpath_memset(uint32_t pc, uint32_t base, uint64_t max_bytes,
                        HotpathFailReason& reason)
    {
        if (!hotpath_memset_safe_pc(pc, base)) {
            reason = HotpathFailReason::PcMismatch;
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t dest = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t current_dest = pc == base
                                          ? dest
                                          : static_cast<uint32_t>(get_v7m_state(Field::R3));
        const uint32_t remaining = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint8_t value = static_cast<uint8_t>(get_v7m_state(Field::R1));
        if (remaining > max_bytes) {
            reason = HotpathFailReason::TooLarge;
            return false;
        }

        uint8_t* dst_ptr = nullptr;
        if (remaining != 0 &&
            !hotpath_dmi_ptr(current_dest, remaining, false, true, dst_ptr)) {
            reason = HotpathFailReason::Dmi;
            return false;
        }
        if (remaining != 0) {
            std::memset(dst_ptr, value, remaining);
            m_inst.get().tb_invalidate_phys_range(current_dest, current_dest + remaining - 1);
        }

        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (pc == base) {
            if (!set_v7m_state(Field::PC, return_pc)) {
                reason = HotpathFailReason::State;
                return false;
            }
        } else if (!hotpath_finish_memset(return_pc, reason)) {
            return false;
        }
        m_hotpath_memset_hits.fetch_add(1, std::memory_order_relaxed);
        maybe_write_hotpath_profile_file();
        return true;
    }

    void try_hotpath_accel()
    {
        if (!p_hotpath_accel.get_value()) {
            return;
        }

        const uint64_t max_bytes = p_hotpath_max_bytes.get_value();
        if (max_bytes == 0) {
            return;
        }

        const uint32_t pc = static_cast<uint32_t>(m_cpu.get_pc());
        const uint32_t memcpy_base = static_cast<uint32_t>(p_hotpath_memcpy_addr.get_value() & ~1ull);
        const uint32_t memset_base = static_cast<uint32_t>(p_hotpath_memset_addr.get_value() & ~1ull);
        bool handled = false;
        HotpathFailReason fail_reason = HotpathFailReason::PcMismatch;

        if (memcpy_base != 0 && hotpath_memcpy_safe_pc(pc, memcpy_base)) {
            handled = hotpath_memcpy(pc, memcpy_base, max_bytes, fail_reason);
        } else if (memset_base != 0 && hotpath_memset_safe_pc(pc, memset_base)) {
            handled = hotpath_memset(pc, memset_base, max_bytes, fail_reason);
        }

        if (!handled) {
            record_hotpath_failure(fail_reason);
        } else {
            m_cpu.set_vcpu_dirty(true);
            m_cpu.kick();
        }
    }

    bool try_lms_accel_at_pc(uint32_t pc)
    {
        if (!p_lms_accel.get_value() ||
            m_lms_accel_done.load(std::memory_order_relaxed)) {
            return false;
        }

        using Field = qemu::CpuArm::V7MStateField;
        const uint32_t verify_addr =
            static_cast<uint32_t>(p_lms_verify_addr.get_value() & ~1ull);
        if (verify_addr == 0 || pc != verify_addr) {
            m_lms_accel_pc_misses.fetch_add(1, std::memory_order_relaxed);
            maybe_write_hotpath_profile_file();
            return false;
        }

        const uint32_t key_addr = static_cast<uint32_t>(get_v7m_state(Field::R0));
        const uint32_t key_size = static_cast<uint32_t>(get_v7m_state(Field::R1));
        const uint32_t data_addr = static_cast<uint32_t>(get_v7m_state(Field::R2));
        const uint32_t data_size = static_cast<uint32_t>(get_v7m_state(Field::R3));
        const uint32_t sp = static_cast<uint32_t>(get_v7m_state(Field::SP));
        uint32_t sig_addr = 0;
        uint32_t sig_size = 0;
        if (!hotpath_read_u32(sp, sig_addr) ||
            !hotpath_read_u32(sp + sizeof(sig_addr), sig_size)) {
            m_lms_accel_dmi_fails.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }
        m_lms_last_key_addr.store(key_addr, std::memory_order_relaxed);
        m_lms_last_key_size.store(key_size, std::memory_order_relaxed);
        m_lms_last_data_addr.store(data_addr, std::memory_order_relaxed);
        m_lms_last_data_size.store(data_size, std::memory_order_relaxed);
        m_lms_last_sig_addr.store(sig_addr, std::memory_order_relaxed);
        m_lms_last_sig_size.store(sig_size, std::memory_order_relaxed);

        uint32_t unsupported_mask = 0;
        if (key_size != qbox::rse_lms_accel::LMS_PUBLIC_KEY_LEN) {
            unsupported_mask |= 1u;
        }
        if (sig_size != qbox::rse_lms_accel::LMS_SIG_LEN) {
            unsupported_mask |= 2u;
        }
        if (data_size > p_lms_max_data_bytes.get_value()) {
            unsupported_mask |= 4u;
        }
        m_lms_last_unsupported_mask.store(unsupported_mask, std::memory_order_relaxed);
        if (unsupported_mask != 0) {
            m_lms_accel_unsupported.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        std::vector<uint8_t> key;
        std::vector<uint8_t> data;
        std::vector<uint8_t> sig;
        if (!hotpath_read_bytes(key_addr, key_size, key) ||
            !hotpath_read_bytes(data_addr, data_size, data) ||
            !hotpath_read_bytes(sig_addr, sig_size, sig)) {
            m_lms_accel_dmi_fails.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        if (!qbox::rse_lms_accel::verify(key, data, sig)) {
            m_lms_accel_verify_fails.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        static constexpr uint32_t FIH_SUCCESS_VALUE = 0x1aaa555au;
        const uint32_t return_pc = static_cast<uint32_t>(get_v7m_state(Field::LR));
        if (!set_v7m_state(Field::R0, FIH_SUCCESS_VALUE) ||
            !set_v7m_state(Field::PC, return_pc)) {
            m_lms_accel_state_fails.fetch_add(1, std::memory_order_relaxed);
            write_hotpath_profile_file();
            return false;
        }

        m_lms_accel_hits.fetch_add(1, std::memory_order_relaxed);
        m_lms_accel_done.store(true, std::memory_order_relaxed);
        if (m_lms_pc_entry_registered && !p_bl2_load_profile.get_value() &&
            !p_bl2_load_accel.get_value() &&
            !p_bl2_boot_enc_accel.get_value() &&
            !p_bl2_img_hash_accel.get_value() &&
            !p_bl2_verify_sig_accel.get_value() &&
            !p_bl2_delay_accel.get_value()) {
            m_cpu.clear_pc_entry_callback();
            m_cpu.clear_pc_entry_watches();
            m_lms_pc_entry_registered = false;
        }
        write_hotpath_profile_file();
        return true;
    }

    bool try_rse_pc_entry(uintptr_t pc)
    {
        const uint32_t pc32 = static_cast<uint32_t>(pc);
        try_bl2_load_profile_at_pc(pc32);
        try_bl2_boot_enc_capture_key_at_pc(pc32);
        if (try_bl2_delay_accel_at_pc(pc32)) {
            return true;
        }
        if (try_bl2_load_decrypt_accel_at_pc(pc32)) {
            return true;
        }
        if (try_bl2_boot_enc_decrypt_accel_at_pc(pc32)) {
            return true;
        }
        if (try_bl2_img_hash_accel_at_pc(pc32)) {
            return true;
        }
        if (try_bl2_verify_sig_accel_at_pc(pc32)) {
            return true;
        }
        return try_lms_accel_at_pc(pc32);
    }

    void try_lms_accel()
    {
        if (try_lms_accel_at_pc(static_cast<uint32_t>(m_cpu.get_pc()))) {
            m_cpu.set_vcpu_dirty(true);
            m_cpu.kick();
        }
    }

    /*
     * Called after a CPU loop run. It synchronizes with the kernel.
     */
    void sync_with_kernel()
    {
        int64_t now = m_inst.get().get_virtual_clock();

        m_cpu.set_soft_stopped(true);
        if (!m_lms_pc_entry_registered) {
            try_lms_accel();
        }
        try_bl2_delay_accel();
        try_hotpath_accel();
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
    cci::cci_param<bool> p_hotpath_accel;
    cci::cci_param<uint64_t> p_hotpath_memcpy_addr;
    cci::cci_param<uint64_t> p_hotpath_memset_addr;
    cci::cci_param<uint64_t> p_hotpath_max_bytes;
    cci::cci_param<bool> p_hotpath_tlm_fallback;
    cci::cci_param<std::string> p_hotpath_profile_file;
    cci::cci_param<uint64_t> p_hotpath_profile_interval;
    cci::cci_param<bool> p_lms_accel;
    cci::cci_param<uint64_t> p_lms_verify_addr;
    cci::cci_param<uint64_t> p_lms_max_data_bytes;
    cci::cci_param<bool> p_bl2_load_profile;
    cci::cci_param<uint64_t> p_bl2_boot_go_for_image_id_addr;
    cci::cci_param<uint64_t> p_bl2_boot_load_image_to_sram_addr;
    cci::cci_param<uint64_t> p_bl2_boot_enc_load_addr;
    cci::cci_param<uint64_t> p_bl2_boot_enc_decrypt_addr;
    cci::cci_param<uint64_t> p_bl2_bootutil_img_validate_addr;
    cci::cci_param<uint64_t> p_bl2_bootutil_img_hash_addr;
    cci::cci_param<uint64_t> p_bl2_bootutil_verify_sig_addr;
    cci::cci_param<uint64_t> p_bl2_boot_image_count;
    cci::cci_param<uint64_t> p_bl2_boot_state_curr_img_offset;
    cci::cci_param<uint64_t> p_bl2_boot_state_imgs_offset;
    cci::cci_param<uint64_t> p_bl2_boot_state_image_stride;
    cci::cci_param<uint64_t> p_bl2_boot_state_slot_stride;
    cci::cci_param<uint64_t> p_bl2_boot_state_slot_usage_offset;
    cci::cci_param<uint64_t> p_bl2_boot_state_slot_usage_stride;
    cci::cci_param<uint64_t> p_bl2_boot_slot_usage_img_dst_offset;
    cci::cci_param<uint64_t> p_bl2_boot_slot_usage_img_sz_offset;
    cci::cci_param<bool> p_bl2_load_accel;
    cci::cci_param<uint64_t> p_bl2_load_accel_max_bytes;
    cci::cci_param<bool> p_bl2_boot_enc_accel;
    cci::cci_param<uint64_t> p_bl2_boot_enc_set_key_addr;
    cci::cci_param<uint64_t> p_bl2_boot_status_enckey_offset;
    cci::cci_param<uint64_t> p_bl2_boot_enc_key_bytes;
    cci::cci_param<uint64_t> p_bl2_boot_enc_key_stride;
    cci::cci_param<uint64_t> p_bl2_boot_enc_slots;
    cci::cci_param<uint64_t> p_bl2_boot_enc_max_bytes;
    cci::cci_param<bool> p_bl2_img_hash_accel;
    cci::cci_param<uint64_t> p_bl2_img_hash_max_bytes;
    cci::cci_param<uint64_t> p_bl2_img_hash_max_seed_bytes;
    cci::cci_param<bool> p_bl2_verify_sig_accel;
    cci::cci_param<bool> p_bl2_verify_sig_skip;
    cci::cci_param<uint64_t> p_bl2_bootutil_keys_addr;
    cci::cci_param<uint64_t> p_bl2_bootutil_key_cnt_addr;
    cci::cci_param<uint64_t> p_bl2_fih_success_addr;
    cci::cci_param<uint64_t> p_bl2_verify_sig_max_key_bytes;
    cci::cci_param<uint64_t> p_bl2_verify_sig_max_sig_bytes;
    cci::cci_param<bool> p_bl2_delay_accel;
    cci::cci_param<uint64_t> p_bl2_delay_cycles_addr;
    cci::cci_param<uint64_t> p_bl2_delay_max_cycles;
    cci::cci_param<uint64_t> p_bl2_delay_expected_hits;
    cci::cci_param<std::string> p_direct_file_aliases;
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
        , p_hotpath_accel("hotpath_accel", false,
                          "Enable opt-in semantic acceleration for known firmware hot paths")
        , p_hotpath_memcpy_addr("hotpath_memcpy_addr", 0,
                                "Thumb entry address for an acceleratable memcpy implementation")
        , p_hotpath_memset_addr("hotpath_memset_addr", 0,
                                "Thumb entry address for an acceleratable memset implementation")
        , p_hotpath_max_bytes("hotpath_max_bytes", 16 * 1024 * 1024,
                              "Maximum byte count handled by semantic hotpath acceleration")
        , p_hotpath_tlm_fallback("hotpath_tlm_fallback", false,
                                 "Allow reported TLM fallback for hotpath byte transfers when DMI chunks are unavailable")
        , p_hotpath_profile_file("hotpath_profile_file", "",
                                 "Optional JSON profile file for hotpath acceleration counters")
        , p_hotpath_profile_interval("hotpath_profile_interval", 1024,
                                     "Hotpath events between profile JSON flushes; 0 disables periodic flush")
        , p_lms_accel("lms_accel", false,
                      "Enable opt-in RSE BL1_2 LMS verify semantic acceleration")
        , p_lms_verify_addr("lms_verify_addr", 0,
                            "Thumb entry address for pq_crypto_verify/fixed LMS accelerator")
        , p_lms_max_data_bytes("lms_max_data_bytes", 4096,
                               "Maximum message byte count accepted by LMS accelerator")
        , p_bl2_load_profile("bl2_load_profile", false,
                             "Record opt-in RSE BL2 image load/validate function entry samples")
        , p_bl2_boot_go_for_image_id_addr("bl2_boot_go_for_image_id_addr", 0,
                                          "Thumb entry address for boot_go_for_image_id")
        , p_bl2_boot_load_image_to_sram_addr("bl2_boot_load_image_to_sram_addr", 0,
                                             "Thumb entry address for boot_load_image_to_sram")
        , p_bl2_boot_enc_load_addr("bl2_boot_enc_load_addr", 0,
                                   "Thumb entry address for boot_enc_load")
        , p_bl2_boot_enc_decrypt_addr("bl2_boot_enc_decrypt_addr", 0,
                                      "Thumb entry address for boot_enc_decrypt")
        , p_bl2_bootutil_img_validate_addr("bl2_bootutil_img_validate_addr", 0,
                                           "Thumb entry address for bootutil_img_validate")
        , p_bl2_bootutil_img_hash_addr("bl2_bootutil_img_hash_addr", 0,
                                       "Thumb entry address for bootutil_img_hash")
        , p_bl2_bootutil_verify_sig_addr("bl2_bootutil_verify_sig_addr", 0,
                                         "Thumb entry address for bootutil_verify_sig")
        , p_bl2_boot_image_count("bl2_boot_image_count", 5,
                                 "MCUBoot BOOT_IMAGE_NUMBER for BL2 state snapshots")
        , p_bl2_boot_state_curr_img_offset("bl2_boot_state_curr_img_offset", 0x10c8,
                                           "Offset of boot_loader_state.curr_img_idx")
        , p_bl2_boot_state_imgs_offset("bl2_boot_state_imgs_offset", 0,
                                       "Offset of boot_loader_state.imgs")
        , p_bl2_boot_state_image_stride("bl2_boot_state_image_stride", 88,
                                        "Stride between boot_loader_state.imgs image entries")
        , p_bl2_boot_state_slot_stride("bl2_boot_state_slot_stride", 44,
                                       "Stride between boot_loader_state.imgs slot entries")
        , p_bl2_boot_state_slot_usage_offset("bl2_boot_state_slot_usage_offset", 0x10d0,
                                             "Offset of boot_loader_state.slot_usage")
        , p_bl2_boot_state_slot_usage_stride("bl2_boot_state_slot_usage_stride", 16,
                                             "Stride between boot_loader_state.slot_usage entries")
        , p_bl2_boot_slot_usage_img_dst_offset("bl2_boot_slot_usage_img_dst_offset", 8,
                                               "Offset of slot_usage_t.img_dst")
        , p_bl2_boot_slot_usage_img_sz_offset("bl2_boot_slot_usage_img_sz_offset", 12,
                                              "Offset of slot_usage_t.img_sz")
        , p_bl2_load_accel("bl2_load_accel", false,
                           "Enable opt-in RSE BL2 RAM-load payload semantic acceleration")
        , p_bl2_load_accel_max_bytes("bl2_load_accel_max_bytes", 16 * 1024 * 1024,
                                     "Maximum payload byte count accepted by BL2 RAM-load accelerator")
        , p_bl2_boot_enc_accel("bl2_boot_enc_accel", false,
                               "Enable opt-in RSE BL2 boot_enc_decrypt semantic acceleration")
        , p_bl2_boot_enc_set_key_addr("bl2_boot_enc_set_key_addr", 0,
                                      "Thumb entry address for boot_enc_set_key")
        , p_bl2_boot_status_enckey_offset("bl2_boot_status_enckey_offset", 0x0c,
                                          "Offset of struct boot_status.enckey in the active BL2 build")
        , p_bl2_boot_enc_key_bytes("bl2_boot_enc_key_bytes", 16,
                                   "AES key byte count for BL2 encrypted images")
        , p_bl2_boot_enc_key_stride("bl2_boot_enc_key_stride", 16,
                                    "Stride between boot_status.enckey slots")
        , p_bl2_boot_enc_slots("bl2_boot_enc_slots", 2,
                               "Number of boot_status.enckey slots")
        , p_bl2_boot_enc_max_bytes("bl2_boot_enc_max_bytes", 4096,
                                   "Maximum byte count accepted by BL2 boot_enc_decrypt accelerator")
        , p_bl2_img_hash_accel("bl2_img_hash_accel", false,
                               "Enable opt-in RSE BL2 bootutil_img_hash host-native SHA256 acceleration")
        , p_bl2_img_hash_max_bytes("bl2_img_hash_max_bytes", 16 * 1024 * 1024,
                                   "Maximum byte count accepted by BL2 image hash accelerator")
        , p_bl2_img_hash_max_seed_bytes("bl2_img_hash_max_seed_bytes", 4096,
                                        "Maximum seed byte count accepted by BL2 image hash accelerator")
        , p_bl2_verify_sig_accel("bl2_verify_sig_accel", false,
                                 "Enable opt-in RSE BL2 bootutil_verify_sig host-native ECDSA verification")
        , p_bl2_verify_sig_skip("bl2_verify_sig_skip", false,
                                "Skip guest bootutil_verify_sig after host-native verification succeeds")
        , p_bl2_bootutil_keys_addr("bl2_bootutil_keys_addr", 0,
                                   "Address of bootutil_keys in the active RSE BL2 build")
        , p_bl2_bootutil_key_cnt_addr("bl2_bootutil_key_cnt_addr", 0,
                                      "Address of bootutil_key_cnt in the active RSE BL2 build")
        , p_bl2_fih_success_addr("bl2_fih_success_addr", 0,
                                 "Address of FIH_SUCCESS in the active RSE BL2 build")
        , p_bl2_verify_sig_max_key_bytes("bl2_verify_sig_max_key_bytes", 512,
                                         "Maximum public-key byte count accepted by BL2 signature accelerator")
        , p_bl2_verify_sig_max_sig_bytes("bl2_verify_sig_max_sig_bytes", 128,
                                         "Maximum DER signature byte count accepted by BL2 signature accelerator")
        , p_bl2_delay_accel("bl2_delay_accel", false,
                            "Enable opt-in RSE BL2 delay_cycles spin-loop acceleration")
        , p_bl2_delay_cycles_addr("bl2_delay_cycles_addr", 0,
                                  "Effective hook PC inside RSE BL2 delay_cycles")
        , p_bl2_delay_max_cycles("bl2_delay_max_cycles", 50 * 1000 * 1000,
                                 "Maximum cycle count accepted by BL2 delay accelerator")
        , p_bl2_delay_expected_hits("bl2_delay_expected_hits", 3,
                                    "Clear BL2 delay PC watch after this many hits; 0 keeps it armed")
        , p_direct_file_aliases("direct_file_aliases", "",
                                "Semicolon-separated addr:size:file_offset:ro|rw:path direct file aliases")
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
        if (p_hotpath_accel.get_value() || p_lms_accel.get_value() ||
            p_bl2_load_profile.get_value() || p_bl2_boot_enc_accel.get_value() ||
            p_bl2_img_hash_accel.get_value() || p_bl2_verify_sig_accel.get_value() ||
            p_bl2_delay_accel.get_value()) {
            write_hotpath_profile_file();
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
        socket.install_direct_file_aliases(p_direct_file_aliases.get_value());

        m_cpu.set_soft_stopped(true);

        m_cpu.set_end_of_loop_callback(std::bind(&QemuCpu::end_of_loop_cb, this));
        m_cpu.set_kick_callback(std::bind(&QemuCpu::kick_cb, this));
        if (p_lms_accel.get_value() || p_bl2_load_profile.get_value() ||
            p_bl2_load_accel.get_value() || p_bl2_boot_enc_accel.get_value() ||
            p_bl2_img_hash_accel.get_value() ||
            p_bl2_verify_sig_accel.get_value() ||
            p_bl2_delay_accel.get_value()) {
            if (p_bl2_delay_accel.get_value() &&
                p_bl2_delay_cycles_addr.get_value() != 0) {
                m_cpu.add_pc_entry_watch(p_bl2_delay_cycles_addr.get_value() & ~1ull);
                m_bl2_delay_watch_active.store(true, std::memory_order_relaxed);
            }
            m_cpu.set_pc_entry_callback(std::bind(&QemuCpu::try_rse_pc_entry, this, std::placeholders::_1));
            m_lms_pc_entry_registered = true;
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
