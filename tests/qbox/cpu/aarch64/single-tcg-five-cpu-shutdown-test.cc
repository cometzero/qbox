/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <array>
#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <dirent.h>
#endif

#include <cci/utils/broker.h>
#include <libgsutils.h>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

class SingleTcgFiveCpuShutdownTest : public CpuTestBenchBase
{
    static constexpr int NUM_CPUS = 5;
    static constexpr int NUM_WRITES = 10;
    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            mrs x0, mpidr_el1
            and x0, x0, #0xff
            lsl x0, x0, #3
            add x1, x1, x0
            mov x0, #0
        loop:
            str x0, [x1]
            add x0, x0, #1
            cmp x0, #%d
            b.ne loop
        end:
            wfi
            b end
    )";

    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    sc_core::sc_vector<cpu_arm_cortexA53> m_cpus;
    CpuTesterMmio m_tester;
    gs::async_event m_keepalive;
    std::array<int, NUM_CPUS> m_writes{};

public:
    SingleTcgFiveCpuShutdownTest(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, qemu::Target::AARCH64)
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpus("cpu", NUM_CPUS,
                 [this](const char* cpu_name, size_t) {
                     return new cpu_arm_cortexA53(cpu_name, m_inst);
                 })
        , m_tester("tester", *this)
        , m_keepalive("keepalive")
    {
        char firmware[1024];

        m_keepalive.async_attach_suspending();
        std::snprintf(firmware, sizeof(firmware), FIRMWARE,
                      CpuTesterMmio::MMIO_ADDR, NUM_WRITES);
        set_firmware(firmware);

        for (int i = 0; i < NUM_CPUS; ++i) {
            m_cpus[i].p_mp_affinity = i;
            m_cpus[i].p_has_el3 = false;
            m_cpus[i].p_has_el2 = false;
            m_router.add_initiator(m_cpus[i].socket);
        }
    }

    void mmio_write(int, uint64_t addr, uint64_t data, size_t) override
    {
        const size_t cpu = addr >> 3;

        TEST_ASSERT(cpu < m_writes.size());
        TEST_ASSERT(data == static_cast<uint64_t>(m_writes[cpu]));
        ++m_writes[cpu];

        for (const int writes : m_writes) {
            if (writes != NUM_WRITES) {
                return;
            }
        }

        m_keepalive.async_detach_suspending();
        sc_core::sc_stop();
    }

    void map_irqs_to_cpus(
        sc_core::sc_vector<InitiatorSignalSocket<bool>>& irqs) override
    {
        for (int i = 0; i < NUM_CPUS; ++i) {
            irqs[i].bind(m_cpus[i].irq_in);
        }
    }

    void end_of_simulation() override
    {
        CpuTestBenchBase::end_of_simulation();
        for (const int writes : m_writes) {
            TEST_ASSERT(writes == NUM_WRITES);
        }
    }
};

constexpr const char* SingleTcgFiveCpuShutdownTest::FIRMWARE;

static int live_qemu_threads()
{
#ifdef __linux__
    int count = 0;
    DIR* tasks = opendir("/proc/self/task");
    TEST_ASSERT(tasks != nullptr);

    while (dirent* entry = readdir(tasks)) {
        char path[128];
        char name[32] = {};

        if (entry->d_name[0] == '.') {
            continue;
        }
        std::snprintf(path, sizeof(path), "/proc/self/task/%s/comm",
                      entry->d_name);
        FILE* comm = std::fopen(path, "r");
        if (comm == nullptr) {
            continue;
        }
        if (std::fgets(name, sizeof(name), comm) != nullptr &&
            (std::strncmp(name, "qemu-iothread", 13) == 0 ||
             std::strncmp(name, "ALL CPUs/TCG", 12) == 0)) {
            ++count;
        }
        std::fclose(comm);
    }
    closedir(tasks);
    return count;
#else
    return 0;
#endif
}

int sc_main(int argc, char* argv[])
{
    const int test_status =
        run_testbench<SingleTcgFiveCpuShutdownTest>(argc, argv);
    const int qemu_threads = live_qemu_threads();

    std::printf("single_tcg_shutdown test_status=%d live_qemu_threads=%d\n",
                test_status, qemu_threads);
    return test_status == 0 && qemu_threads == 0 ? 0 : 1;
}
