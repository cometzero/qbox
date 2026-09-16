/* SPDX-License-Identifier: BSD-3-Clause */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"

static bool irq_wake_passed = false;
static std::atomic<bool> suppress_one_resume{false};

class IrqWakeCpu : public cpu_arm_cortexA53
{
public:
    using cpu_arm_cortexA53::cpu_arm_cortexA53;
    cci::cci_param<bool> p_disable_reservation{"disable_reservation", false};
    cci::cci_param<bool> p_disable_exec_entry{"disable_exec_entry", false};
    void before_end_of_elaboration() override
    {
        cpu_arm_cortexA53::before_end_of_elaboration();
        if (p_disable_exec_entry) m_cpu.set_exec_entry_callback([] {});
        if (p_disable_reservation) {
            // Diagnostic old wake path, without reverting production files.
            m_cpu.set_kick_callback([this] { request_standby(false); });
        }
    }
    bool is_halted()
    {
        m_inst.get().lock_iothread();
        const auto state = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
        m_inst.get().unlock_iothread();
        return state & (1u << 3);
    }
    void kick_without_work() { m_cpu.kick(); }
    void delay_cpu_work(gs::async_event& started, gs::async_event& done)
    {
        m_cpu.async_safe_run([&started, &done] {
            started.async_notify();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            done.async_notify();
        });
    }
    void queued_idle_irq()
    {
        m_inst.get().lock_iothread();
        m_cpu.async_run([this] {
            // Queued work runs after QEMU's sleep/resume notification point.
            // Report the still-halted CPU idle, then make it runnable in this
            // same job. A self-directed IRQ needs no native thread kick, so
            // the asynchronous wake-budget hook cannot supply this transition.
            const auto state = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
            sc_assert(state & (1u << 3));
            auto& plugin = m_inst.get_mcips_plugin();
            plugin.vcpu_idle(m_cpu.get_index());
            irq_in.get_gpio().set(true);
            std::fprintf(stderr, "MCIPS_EXEC_ENTRY queued idle-to-runnable work completed\n");
        });
        m_inst.get().unlock_iothread();
    }
    void dump_state()
    {
        m_inst.get().lock_iothread();
        const auto state = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
        std::fprintf(stderr, "MCIPS_RESET_WAKE native_state=%#llx pc=%#llx\n",
                     static_cast<unsigned long long>(state),
                     static_cast<unsigned long long>(m_cpu.get_pc()));
        m_inst.get().unlock_iothread();
    }
};

class McipsIrqWakeTest : public CpuTestBenchBase
{
    QemuInstanceManager m_manager;
    QemuInstance m_inst;
    IrqWakeCpu m_cpu;
    std::unique_ptr<IrqWakeCpu> m_peer;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    sc_core::sc_out<bool> m_reset;
    sc_core::sc_out<bool> m_peer_reset;
    sc_core::sc_signal<bool> m_unused_peer_reset;
    cci::cci_param<bool> p_peer_start_in_reset{"peer_start_in_reset", false};
    cci::cci_param<unsigned> p_reset_rounds{"reset_rounds", 0};
    cci::cci_param<bool> p_suppress_resume{"suppress_resume", false};
    cci::cci_param<bool> p_queued_idle{"queued_idle", false};
    std::thread m_irq_thread;
    gs::async_event m_release_event{false};
    gs::async_event m_work_started{false};
    gs::async_event m_timeout_event{false};
    std::thread m_watchdog;
    std::mutex m_watchdog_mutex;
    std::condition_variable m_watchdog_cv;
    bool m_cancel_watchdog = false;
    unsigned m_stage = 0;
    bool m_peer_done = false;

    void ticks()
    {
        while (true) wait(25, sc_core::SC_US);
    }

    void timeout()
    {
        std::fprintf(stderr, "MCIPS_MISSED_RESUME TIMEOUT stage=%u sc=%s\n", m_stage,
                     sc_core::sc_time_stamp().to_string().c_str());
        m_cpu.dump_state();
        std::fprintf(stderr, "%s\n", m_inst.get_mcips_plugin().get_mcips_status_json().c_str());
        irq_wake_passed = false;
        sc_core::sc_stop();
    }

    void suppress_resume_notification()
    {
        auto& plugin = m_inst.get_mcips_plugin();
        const auto id = gs::cci_get<uint64_t>(cci::cci_get_broker(), std::string(plugin.name()) + ".id");
        m_inst.get().lock_iothread();
        suppress_one_resume.store(true);
        m_inst.get().plugin_api().qemu_plugin_register_vcpu_resume_cb(
            id, [](unsigned int index, void* userdata) {
                if (suppress_one_resume.exchange(false)) {
                    std::fprintf(stderr, "MCIPS_MISSED_RESUME suppressed native notification\n");
                    return;
                }
                LibQemuPlugin::dispatch_userdata(userdata, [index](LibQemuPlugin* plugin) {
                    static_cast<McipsPlugin*>(plugin)->vcpu_resume(index);
                });
            }, plugin.handle_as_userdata());
        m_inst.get().unlock_iothread();
    }

    void start_watchdog()
    {
        m_watchdog = std::thread([this] {
            std::unique_lock<std::mutex> lock(m_watchdog_mutex);
            if (!m_watchdog_cv.wait_for(lock, std::chrono::seconds(1),
                                       [this] { return m_cancel_watchdog; }))
                m_timeout_event.async_notify();
        });
    }

    bool delayed_wake(bool real_irq)
    {
        // Raise the IRQ at a fixed SystemC timestamp. Keep BQL owned by
        // this same OS thread while the SystemC process waits, so the host
        // vCPU cannot acknowledge the kick for 20ms. Other device processes
        // remain runnable; an async event releases BQL even at the barrier.
        m_inst.get().lock_iothread();
        const auto raised = sc_core::sc_time_stamp();
        if (real_irq) m_cpu.irq_in.get_gpio().set(true);
        else m_cpu.kick_without_work();
        m_irq_thread = std::thread([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            m_release_event.async_notify();
        });
        wait(m_release_event);
        const auto elapsed = sc_core::sc_time_stamp() - raised;
        const bool bounded = elapsed <= tlm_utils::tlm_quantumkeeper::get_global_quantum();
        std::fprintf(stderr, "MCIPS_IRQ_WAKE %s_sc=%s bounded=%d\n",
                     real_irq ? "delayed_resume" : "spurious_kick",
                     elapsed.to_string().c_str(), bounded);
        m_inst.get().unlock_iothread();
        m_irq_thread.join();
        return bounded;
    }

    bool await_stage(unsigned stage)
    {
        const auto deadline = sc_core::sc_time_stamp() + sc_core::sc_time(20, sc_core::SC_MS);
        while (m_stage != stage || !m_cpu.is_halted()) {
            if (sc_core::sc_time_stamp() >= deadline) {
                std::fprintf(stderr, "MCIPS_RESET_WAKE stage_timeout wanted=%u actual=%u sc=%s\n",
                             stage, m_stage, sc_core::sc_time_stamp().to_string().c_str());
                m_cpu.dump_state();
                std::fprintf(stderr, "%s\n", m_inst.get_mcips_plugin().get_mcips_status_json().c_str());
                irq_wake_passed = false;
                sc_core::sc_stop();
                return false;
            }
            wait(1, sc_core::SC_US);
        }
        return true;
    }

    void exercise()
    {
        irq_wake_passed = true;
        if (!m_cpu.p_gdb_port.is_default_value()) {
            // A debugger can attach after autostart and pause the guest.
            // Do not start an SC-time firmware deadline during that pause.
            m_irq_thread = std::thread([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                m_release_event.async_notify();
            });
            wait(m_release_event);
            m_irq_thread.join();
            while (!m_inst.get().vm_is_running()) wait(1, sc_core::SC_US);
        }
        if (p_peer_start_in_reset) {
            start_watchdog();
            wait(10, sc_core::SC_MS);
            TEST_ASSERT(!m_peer_done);
            std::fprintf(stderr, "MCIPS_HELD_PEER release sc=%s\n",
                         sc_core::sc_time_stamp().to_string().c_str());
            // Delay the actual CPU-side release behind an exclusive work
            // item that drops BQL. SC can still request reset release, but
            // must reserve time while its native worker is not yet available.
            m_peer->delay_cpu_work(m_work_started, m_release_event);
            wait(m_work_started);
            const auto requested = sc_core::sc_time_stamp();
            m_peer_reset.write(false);
            wait(m_release_event);
            const auto elapsed = sc_core::sc_time_stamp() - requested;
            std::fprintf(stderr, "MCIPS_HELD_PEER queued_release_delay_sc=%s\n",
                         elapsed.to_string().c_str());
            TEST_ASSERT(elapsed <= tlm_utils::tlm_quantumkeeper::get_global_quantum());
        }
        for (unsigned round = 0; round <= p_reset_rounds; ++round) {
            if (!await_stage(1)) return;
            wait(p_reset_rounds ? 10 : 1, sc_core::SC_MS);
            // The queued-work case isolates the observed lost-notification
            // contract as well: an incidental paired resume must not repair
            // the deliberately retained IDLE state before execution entry.
            if (p_suppress_resume || p_queued_idle) suppress_resume_notification();
            if (p_suppress_resume || p_queued_idle) start_watchdog();
            if (p_queued_idle) {
                m_cpu.queued_idle_irq();
                m_irq_thread = std::thread([this] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    m_release_event.async_notify();
                });
                wait(m_release_event);
                m_irq_thread.join();
                const auto status = m_inst.get_mcips_plugin().get_mcips_status_json();
                const bool accounted = status.find("\"cpu_execution_status\":0") == std::string::npos;
                std::fprintf(stderr, "MCIPS_EXEC_ENTRY accounted=%d %s\n", accounted, status.c_str());
                irq_wake_passed &= accounted;
            } else {
                irq_wake_passed &= delayed_wake(true);
            }
            if (p_suppress_resume || p_queued_idle) {
                // Guest polls DMI RAM without MMIO. Only real instruction
                // clock progress can advance SC to release this loop.
                wait(500, sc_core::SC_US);
                uint64_t done = 1;
                m_mem.load.ptr_load(reinterpret_cast<uint8_t*>(&done), 0x10000, sizeof(done));
            }
            if (!await_stage(2)) return;
            wait(p_reset_rounds ? 10 : 1, sc_core::SC_MS);
            irq_wake_passed &= delayed_wake(false);
            // A no-work kick must release its reservation without executing
            // another guest instruction, including after a long idle gap.
            wait(2, sc_core::SC_MS);
            TEST_ASSERT(m_stage == 2 && m_cpu.is_halted());
            if (p_reset_rounds) {
                std::fprintf(stderr, "MCIPS_RESET_WAKE round=%u complete sc=%s\n", round,
                             sc_core::sc_time_stamp().to_string().c_str());
            }
            if (round != p_reset_rounds) {
                m_reset.write(true);
                wait(10, sc_core::SC_MS);
                m_stage = 0;
                m_reset.write(false);
            }
        }
        while (m_peer && !m_peer_done) wait(25, sc_core::SC_US);
        while (m_peer && !m_peer->is_halted()) wait(1, sc_core::SC_US);
        std::fprintf(stderr, "MCIPS_IRQ_WAKE stage=%u PASS=%d\n", m_stage, irq_wake_passed);
        sc_core::sc_stop();
    }

public:
    McipsIrqWakeTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64)
        , m_manager("manager")
        , m_inst("inst", &m_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_reset("reset")
        , m_peer_reset("peer_reset")
    {
        m_cpu.p_has_el3 = false;
        m_cpu.p_has_el2 = false;
        m_cpu.p_mp_affinity = 0;
        m_reset.bind(m_cpu.reset);
        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        TEST_ASSERT(p_num_cpu == 1 || p_num_cpu == 2);
        if (p_num_cpu == 2) {
            m_peer.reset(new IrqWakeCpu("peer", m_inst));
            m_peer->p_has_el3 = false;
            m_peer->p_has_el2 = false;
            m_peer->p_mp_affinity = 1;
            m_peer_reset.bind(m_peer->reset);
            if (p_peer_start_in_reset) {
                m_peer->p_start_in_reset = true;
            }
            m_router.add_initiator(m_peer->socket);
        } else {
            m_peer_reset.bind(m_unused_peer_reset);
        }
        set_firmware(R"(
            ldr x1, =0x80000000
            mrs x2, mpidr_el1
            and x2, x2, #0xff
            cbnz x2, peer
            mov x0, #1
            str x0, [x1]
            wfi
            mov x0, #2
            str x0, [x1]
        idle:
            wfi
            b idle
        peer:
            ldr x2, =5000000
        peer_progress:
            subs x2, x2, #1
            b.ne peer_progress
            mov x0, #1
            str x0, [x1, #8]
            b idle
        )");
        if (p_reset_rounds) {
            TEST_ASSERT(p_num_cpu == 1);
            set_firmware(R"(
                ldr x1, =0x80000000
                ldr x2, =250000
            boot_progress:
                subs x2, x2, #1
                b.ne boot_progress
                mov x0, #1
                str x0, [x1]
                wfi
                ldr x2, =250000
            irq_progress:
                subs x2, x2, #1
                b.ne irq_progress
                mov x0, #2
                str x0, [x1]
            idle:
                wfi
                b idle
            )");
        }
        if (p_suppress_resume || p_queued_idle) {
            TEST_ASSERT(p_num_cpu == 1 && p_reset_rounds == 0);
            set_firmware(R"(
                ldr x1, =0x80000000
                mov x0, #1
                str x0, [x1]
                wfi
                ldr x2, =0x10000
            wait_for_device:
                ldr x0, [x2]
                cbz x0, wait_for_device
                mov x0, #2
                str x0, [x1]
            idle:
                wfi
                b idle
            )");
        }
        SC_THREAD(exercise);
        SC_THREAD(ticks);
        SC_METHOD(timeout);
        sensitive << m_timeout_event;
        dont_initialize();
    }

    ~McipsIrqWakeTest() override
    {
        {
            std::lock_guard<std::mutex> lock(m_watchdog_mutex);
            m_cancel_watchdog = true;
        }
        m_watchdog_cv.notify_one();
        if (m_watchdog.joinable()) m_watchdog.join();
        if (m_irq_thread.joinable()) m_irq_thread.join();
    }
    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}
    void mmio_write(int, uint64_t address, uint64_t value, size_t length) override
    {
        if (address == 8) {
            TEST_ASSERT(m_peer && length == 8 && value == 1 && !m_peer_done);
            m_peer_done = true;
            std::fprintf(stderr, "MCIPS_IRQ_WAKE peer_completed=%s\n",
                         sc_core::sc_time_stamp().to_string().c_str());
            return;
        }
        if (!(address == 0 && length == 8 && value == m_stage + 1)) {
            std::fprintf(stderr, "MCIPS_MMIO_FAILURE address=%llu length=%zu value=%llu stage=%u reset=%d\n",
                         static_cast<unsigned long long>(address), length,
                         static_cast<unsigned long long>(value), m_stage, m_reset.read());
        }
        TEST_ASSERT(address == 0 && length == 8 && value == m_stage + 1);
        m_stage = value;
        if (value == 2) {
            m_inst.get().lock_iothread();
            m_cpu.irq_in.get_gpio().set(false);
            m_inst.get().unlock_iothread();
        }
    }
};

int sc_main(int argc, char* argv[])
{
    const auto result = run_testbench<McipsIrqWakeTest>(argc, argv);
    return result ? result : !irq_wake_passed;
}
