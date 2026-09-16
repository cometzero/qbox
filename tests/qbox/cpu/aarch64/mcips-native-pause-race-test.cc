/* SPDX-License-Identifier: BSD-3-Clause */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"

// Standalone diagnostic: exercise the native plugin API, not MCIPS bookkeeping.
static bool native_race_observed;

class NativePauseCpu : public cpu_arm_cortexA53
{
public:
    using cpu_arm_cortexA53::cpu_arm_cortexA53;
    cci::cci_param<bool> serialize_native{"serialize_native", false};
    void pause()
    {
        if (serialize_native) m_inst.get().lock_iothread();
        m_inst.get().plugin_api().qemu_plugin_cpu_request_pause(m_cpu.get_index());
        if (serialize_native) m_inst.get().unlock_iothread();
    }
    void resume()
    {
        if (serialize_native) m_inst.get().lock_iothread();
        m_inst.get().plugin_api().qemu_plugin_cpu_resume(m_cpu.get_index());
        if (serialize_native) m_inst.get().unlock_iothread();
    }
    uint64_t state()
    {
        m_inst.get().lock_iothread();
        const auto value = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
        m_inst.get().unlock_iothread();
        return value;
    }
};

class NativePauseTest : public CpuTestBenchBase
{
    QemuInstanceManager m_manager;
    QemuInstance m_inst;
    NativePauseCpu m_cpu;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    gs::async_event m_done;
    std::thread m_stress;
    bool m_started = false;

    void finish() { m_done.async_detach_suspending(); sc_core::sc_stop(); }

public:
    NativePauseTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64), m_manager("manager"),
          m_inst("inst", &m_manager, qemu::Target::AARCH64), m_cpu("cpu", m_inst),
          m_tester("tester", *this), m_gpi("gpi", m_inst, m_cpu), m_done("done")
    {
        m_cpu.p_has_el3 = false;
        m_cpu.p_has_el2 = false;
        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        set_firmware(R"(
            ldr x1, =0x80000000
            mov x0, #1
            str x0, [x1]
        loop:
            add x2, x2, #1
            b loop
        )");
        m_done.async_attach_suspending();
        SC_METHOD(finish);
        sensitive << m_done;
        dont_initialize();
    }
    ~NativePauseTest() override { if (m_stress.joinable()) m_stress.join(); }
    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}
    void mmio_write(int, uint64_t, uint64_t, size_t) override
    {
        TEST_ASSERT(!m_started);
        m_started = true;
        m_stress = std::thread([this] {
            const auto start = std::chrono::steady_clock::now();
            unsigned rounds = 0;
            uint64_t flags = 0;
            for (; rounds < 200000; ++rounds) {
                m_cpu.pause();
                std::this_thread::yield();
                m_cpu.resume();
                if ((rounds & 31) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    flags = m_cpu.state();
                    // No other producer pauses this QK CPU. A stopped CPU
                    // following the last resume violates native API ordering.
                    if ((flags & (3 | 8 | 16)) == 2) {
                        native_race_observed = true;
                        break;
                    }
                    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
                }
            }
            // An asynchronous implementation may still have ordered requests
            // queued after the final resume. Do not mistake this for a loss.
            const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!native_race_observed) {
                flags = m_cpu.state();
                if ((flags & (3 | 8 | 16)) == 2) {
                    native_race_observed = true;
                    break;
                }
                if (!(flags & (3 | 16))) break;
                if (std::chrono::steady_clock::now() >= drain_deadline) {
                    std::fprintf(stderr, "NATIVE_PAUSE_RACE drain_timeout flags=%#llx\n",
                                 static_cast<unsigned long long>(flags));
                    native_race_observed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::fprintf(stderr, "NATIVE_PAUSE_RACE observed=%d rounds=%u flags=%#llx wall_us=%lld\n",
                         native_race_observed, rounds, static_cast<unsigned long long>(flags),
                         static_cast<long long>(elapsed));
            m_cpu.resume();
            m_done.async_notify();
        });
    }
    uint64_t mmio_read(int, uint64_t, size_t) override { return 0; }
    bool dmi_request(int, uint64_t, size_t, tlm::tlm_dmi&) override { return false; }
};

int sc_main(int argc, char** argv)
{
    const int result = run_testbench<NativePauseTest>(argc, argv);
    return result ? result : (native_race_observed ? 2 : 0);
}
