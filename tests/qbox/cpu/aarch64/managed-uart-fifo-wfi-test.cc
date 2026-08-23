/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "arm_gicv3.h"
#include "char_backend_file.h"
#include "cortex-a53.h"
#include "qemu-instance.h"
#include "uart-pl011.h"

namespace {

std::string fifo_path;
int fifo_fd = -1;

bool write_after_idle_requested(int argc, char* argv[])
{
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--async-host-input-after-wfi") == 0) {
            return true;
        }
    }
    return false;
}

bool close_after_idle_requested(int argc, char* argv[])
{
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--closed-writer-after-wfi") == 0) {
            return true;
        }
    }
    return false;
}

bool cancel_eintr_test_requested(int argc, char* argv[])
{
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cancel-eintr-self-test") == 0) {
            return true;
        }
    }
    return false;
}

unsigned int cancel_write_attempts = 0;

ssize_t interrupt_cancel_write_once(int fd, const void* data, size_t size)
{
    ++cancel_write_attempts;
    if (cancel_write_attempts == 1) {
        errno = EINTR;
        return -1;
    }
    return write(fd, data, size);
}

ssize_t fail_cancel_write(int, const void*, size_t)
{
    errno = EIO;
    return -1;
}

void test_cancel_eintr_retry()
{
    int cancel_pipe[2];
    TEST_ASSERT(pipe(cancel_pipe) == 0);
    TEST_ASSERT(char_backend_file::write_cancel_byte(
        cancel_pipe[1], interrupt_cancel_write_once));
    char cancel = 0;
    TEST_ASSERT(read(cancel_pipe[0], &cancel, sizeof(cancel)) == 1);
    TEST_ASSERT(cancel == 1);
    TEST_ASSERT(cancel_write_attempts == 2);
    close(cancel_pipe[0]);
    close(cancel_pipe[1]);

    TEST_ASSERT(pipe(cancel_pipe) == 0);
    std::atomic_bool poll_released{ false };
    std::thread poll_thread([&] {
        struct pollfd monitor = { cancel_pipe[0], POLLIN, 0 };
        const int result = poll(&monitor, 1, -1);
        poll_released = result > 0 &&
                        char_backend_file::cancellation_requested(
                            monitor.revents);
    });
    TEST_ASSERT(!char_backend_file::signal_cancel(cancel_pipe[1],
                                                  fail_cancel_write));
    poll_thread.join();
    TEST_ASSERT(cancel_pipe[1] == -1);
    TEST_ASSERT(poll_released);
    close(cancel_pipe[0]);
}

void create_fifo(bool seed)
{
    char path[] = "/tmp/qbox-char-backend-fifo-XXXXXX";
    const int file = mkstemp(path);
    TEST_ASSERT(file >= 0);
    close(file);
    unlink(path);
    TEST_ASSERT(mkfifo(path, 0600) == 0);
    fifo_path = path;
    fifo_fd = open(path, O_RDWR | O_NONBLOCK);
    TEST_ASSERT(fifo_fd >= 0);
    if (seed) {
        const char byte = 'P';
        TEST_ASSERT(write(fifo_fd, &byte, 1) == 1);
    }
}

void cleanup_fifo()
{
    if (fifo_fd >= 0) {
        close(fifo_fd);
        fifo_fd = -1;
    }
    if (!fifo_path.empty()) {
        unlink(fifo_path.c_str());
    }
}

}

class CpuArmManagedUartFifoWfiTest : public CpuTestBenchBase
{
    enum class Outcome {
        pending,
        resumed,
        unexpected,
    };

    static constexpr uint64_t GICD_BASE = 0x08000000;
    static constexpr uint64_t GICD_SIZE = 0x00010000;
    static constexpr uint64_t GICR_BASE = 0x080a0000;
    static constexpr uint64_t GICR_SIZE = 0x00020000;
    static constexpr uint64_t UART_BASE = 0x09000000;
    static constexpr uint64_t UART_SIZE = 0x00001000;
    static constexpr unsigned int UART_SPI = 52;
    static constexpr unsigned int UART_INTID = 32 + UART_SPI;

    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x10, =0x%08)" PRIx64 R"(
            ldr x11, =0x%08)" PRIx64 R"(
            ldr x12, =0x%08)" PRIx64 R"(
            ldr x13, =0x%08)" PRIx64 R"(

            msr spsel, #1
            adr x0, vectors
            msr vbar_el1, x0

            mov x0, #1
            msr icc_sre_el1, x0
            isb
            mov x0, #0xff
            msr icc_pmr_el1, x0
            mov x0, #1
            msr icc_igrpen1_el1, x0

            mov x0, #2
            str w0, [x11]
            mov x0, #(1 << 20)
            str w0, [x11, #0x88]
            str w0, [x11, #0x108]
            mov x0, #0
            str x0, [x11, #0x62a0]

            mov x0, #0x10
            str w0, [x13, #0x2c]
            str w0, [x13, #0x38]

            mov x0, #1
            str x0, [x10]
            msr daifclr, #2
        idle:
            wfi
            b idle

        .balign 2048
        vectors:
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b irq_handler
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected

        irq_handler:
            mrs x0, icc_iar1_el1
            cmp x0, #%u
            b.ne unexpected
            mov x1, #4
            str x1, [x10]
            ldr w1, [x13]
            cmp w1, #%u
            b.ne unexpected
            mov x1, #5
            str x1, [x10]
            mov x1, #2
            str x1, [x10]
            msr icc_eoir1_el1, x0
            eret

        unexpected:
            mov x0, #3
            str x0, [x10]
            b unexpected
    )";

    cci::cci_param<bool> p_write_after_idle;
    cci::cci_param<bool> p_close_after_idle;
    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    cpu_arm_cortexA53 m_cpu;
    arm_gicv3 m_gic;
    Pl011 m_uart;
    char_backend_file m_backend;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    sc_core::sc_out<bool> m_reset;
    gs::async_event m_entered_event;
    gs::async_event m_terminal_event;
    std::atomic<Outcome> m_outcome{ Outcome::pending };
    std::atomic_bool m_writer_ok{ false };
    std::atomic_bool m_cpu_nonrunnable{ false };
    std::atomic_bool m_watchdog_expired{ false };
    std::mutex m_cancel_mutex;
    std::condition_variable m_cancel;
    bool m_cancelled = false;
    std::thread m_writer;
    std::thread m_watchdog;
    sc_core::sc_time m_write_time{ sc_core::SC_ZERO_TIME };
    sc_core::sc_time m_async_checkpoint_time{ sc_core::SC_ZERO_TIME };
    sc_core::sc_time m_next_poll_deadline{ sc_core::SC_ZERO_TIME };
    bool m_poll_deadline_reached = false;
    bool m_async_before_poll = false;
    bool m_fifo_dequeued = false;
    bool m_pl011_rx_seen = false;
    bool m_uart_irq_seen = false;
    bool m_gic_spi_seen = false;
    bool m_parent_irq_seen = false;
    bool m_iar84_seen = false;
    bool m_uartdr_seen = false;
    unsigned int m_entered = 0;
    unsigned int m_resumed = 0;
    unsigned int m_unexpected = 0;

public:
    explicit CpuArmManagedUartFifoWfiTest(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, qemu::Target::AARCH64)
        , p_write_after_idle("write_after_idle", false)
        , p_close_after_idle("close_after_idle", false)
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_gic("gic", m_inst, 1)
        , m_uart("uart")
        , m_backend("backend")
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_reset("reset")
        , m_entered_event("entered")
        , m_terminal_event("terminal")
    {
        char firmware[8192];

        TEST_ASSERT(m_inst.manages_start_in_reset_release());
        TEST_ASSERT(m_gic.redist_iface.size() == 1);
        m_cpu.p_mp_affinity = 0;
        m_cpu.p_has_el3 = false;
        m_cpu.p_has_el2 = false;
        m_cpu.p_start_in_reset = true;
        m_cpu.p_reset_power_on = true;
        m_reset.bind(m_cpu.reset);

        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        m_router.add_target(m_gic.dist_iface, GICD_BASE, GICD_SIZE);
        m_router.add_target(m_gic.redist_iface[0], GICR_BASE, GICR_SIZE);
        m_router.add_target(m_uart.socket, UART_BASE, UART_SIZE);

        m_uart.backend_socket.bind(m_backend.socket);
        m_uart.irq.bind(m_gic.spi_in[UART_SPI]);
        m_gic.irq_out[0].bind(m_cpu.irq_in);
        m_gic.fiq_out[0].bind(m_cpu.fiq_in);
        m_gic.virq_out[0].bind(m_cpu.virq_in);
        m_gic.vfiq_out[0].bind(m_cpu.vfiq_in);

        std::snprintf(firmware, sizeof(firmware), FIRMWARE,
                      CpuTesterMmio::MMIO_ADDR, GICD_BASE,
                      GICR_BASE + 0x10000, UART_BASE, UART_INTID,
                      p_write_after_idle.get_value() ? 'W' : 'P');
        set_firmware(firmware);

        SC_THREAD(observe);
        SC_METHOD(trace_uart_irq);
        sensitive << m_gic.spi_in[UART_SPI]->default_event();
        dont_initialize();
        SC_METHOD(trace_parent_irq);
        sensitive << m_cpu.irq_in->default_event();
        dont_initialize();
        m_cpu.reset_cb(false);
    }

    ~CpuArmManagedUartFifoWfiTest() override
    {
        cancel_threads();
    }

    void start_threads()
    {
        if (p_write_after_idle.get_value()) {
            m_writer = std::thread([this] {
                const char byte = 'W';
                m_writer_ok.store(write(fifo_fd, &byte, 1) == 1);
            });
        } else if (p_close_after_idle.get_value()) {
            close(fifo_fd);
            fifo_fd = -1;
        }
        m_watchdog = std::thread([this] {
            std::unique_lock<std::mutex> lock(m_cancel_mutex);
            if (!m_cancel.wait_for(lock, std::chrono::seconds(5),
                                   [this] { return m_cancelled; })) {
                m_watchdog_expired.store(true);
                std::fprintf(stderr,
                             "QBOX_UART_FIFO_WFI_WATCHDOG diagnostic-only "
                             "resumed=%u iar84=%d uartdr=%d unexpected=%u\n",
                             m_resumed, m_iar84_seen, m_uartdr_seen,
                             m_unexpected);
            }
        });
    }

    void cancel_threads()
    {
        {
            std::lock_guard<std::mutex> lock(m_cancel_mutex);
            m_cancelled = true;
        }
        m_cancel.notify_one();
        if (m_writer.joinable()) {
            m_writer.join();
        }
        if (m_watchdog.joinable()) {
            m_watchdog.join();
        }
    }

    void observe()
    {
        wait(m_entered_event);
        const bool delayed = p_write_after_idle.get_value() ||
                             p_close_after_idle.get_value();
        if (delayed) {
            for (unsigned int attempt = 0;
                 attempt < 1000 && m_cpu.can_run(); ++attempt) {
                wait(10, sc_core::SC_US);
            }
            m_cpu_nonrunnable.store(!m_cpu.can_run());
            TEST_ASSERT(m_cpu_nonrunnable.load());
            m_parent_irq_seen = false;
        }
        start_threads();
        if (m_writer.joinable()) {
            m_writer.join();
        }

        if (delayed) {
            m_write_time = sc_core::sc_time_stamp();
            const sc_core::sc_time poll_interval(1, sc_core::SC_MS);
            const uint64_t next_tick =
                (m_write_time.value() / poll_interval.value() + 1) *
                poll_interval.value();
            m_next_poll_deadline = sc_core::sc_time::from_value(next_tick);

            if (p_write_after_idle.get_value()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                wait(sc_core::SC_ZERO_TIME);
                wait(sc_core::SC_ZERO_TIME);
                m_async_checkpoint_time = sc_core::sc_time_stamp();
                int available = -1;
                TEST_ASSERT(ioctl(fifo_fd, FIONREAD, &available) == 0);
                m_async_before_poll = available == 0 ||
                                      m_pl011_rx_seen || m_uart_irq_seen;
            }

            if (!fifo_path.empty()) {
                unlink(fifo_path.c_str());
                fifo_path.clear();
            }
            wait(m_next_poll_deadline - sc_core::sc_time_stamp());
            wait(sc_core::SC_ZERO_TIME);
            m_poll_deadline_reached =
                sc_core::sc_time_stamp() >= m_next_poll_deadline;
            int available = -1;
            if (fifo_fd >= 0) {
                TEST_ASSERT(ioctl(fifo_fd, FIONREAD, &available) == 0);
                m_fifo_dequeued = available == 0;
            } else {
                m_fifo_dequeued = true;
            }

            if (p_close_after_idle.get_value()) {
                wait(1, sc_core::SC_MS);
            } else {
                while (m_outcome.load() == Outcome::pending) {
                    wait(m_terminal_event);
                }
            }
        } else {
            while (m_outcome.load() == Outcome::pending) {
                wait(m_terminal_event);
            }
        }
        cancel_threads();

        m_cpu.halt_cb(true);
        std::cerr << "QBOX_UART_FIFO_WFI_STATE mode="
                  << (p_write_after_idle.get_value() ? "quiescent" :
                      p_close_after_idle.get_value() ? "closed-writer" :
                                                       "active")
                  << " entered=" << m_entered
                  << " cpu_nonrunnable=" << m_cpu_nonrunnable.load()
                  << " byte_written=" << m_writer_ok.load()
                  << " write_time=" << m_write_time
                  << " async_checkpoint=" << m_async_checkpoint_time
                  << " next_poll=" << m_next_poll_deadline
                  << " observed_time=" << sc_core::sc_time_stamp()
                  << " poll_reached=" << m_poll_deadline_reached
                  << " async_before_poll=" << m_async_before_poll
                  << " fifo_dequeued=" << m_fifo_dequeued
                  << " pl011_rx=" << m_pl011_rx_seen
                  << " uart_irq=" << m_uart_irq_seen
                  << " gic_spi52=" << m_gic_spi_seen
                  << " parent_irq=" << m_parent_irq_seen
                  << " iar84=" << m_iar84_seen
                  << " uartdr=" << m_uartdr_seen
                  << " resumed=" << m_resumed
                  << " unexpected=" << m_unexpected
                  << " watchdog=" << m_watchdog_expired.load() << std::endl;
        TEST_ASSERT(m_entered == 1);
        if (delayed) {
            TEST_ASSERT(m_cpu_nonrunnable.load());
            TEST_ASSERT(m_poll_deadline_reached);
            TEST_ASSERT(m_fifo_dequeued);
        }
        if (p_write_after_idle.get_value()) {
            TEST_ASSERT(m_writer_ok.load());
            TEST_ASSERT(m_pl011_rx_seen);
            TEST_ASSERT(m_uart_irq_seen);
            TEST_ASSERT(m_gic_spi_seen);
            TEST_ASSERT(m_parent_irq_seen);
            TEST_ASSERT(m_iar84_seen);
            TEST_ASSERT(m_uartdr_seen);
            TEST_ASSERT(m_async_checkpoint_time == m_write_time);
            TEST_ASSERT(m_async_checkpoint_time < m_next_poll_deadline);
            if (!m_async_before_poll) {
                std::cerr << "QBOX_UART_FIFO_WFI_FIRST_MISSING "
                          << "stage=async-backend-dequeue-before-poll"
                          << std::endl;
            }
            TEST_ASSERT(m_async_before_poll);
        }
        if (p_close_after_idle.get_value()) {
            TEST_ASSERT(!m_writer_ok.load());
            TEST_ASSERT(!m_pl011_rx_seen);
            TEST_ASSERT(!m_uart_irq_seen);
            TEST_ASSERT(!m_gic_spi_seen);
            TEST_ASSERT(!m_parent_irq_seen);
            TEST_ASSERT(!m_iar84_seen);
            TEST_ASSERT(!m_uartdr_seen);
            TEST_ASSERT(m_resumed == 0);
            TEST_ASSERT(m_unexpected == 0);
            TEST_ASSERT(m_outcome.load() == Outcome::pending);
            sc_core::sc_stop();
            return;
        }
        TEST_ASSERT(m_resumed == 1);
        TEST_ASSERT(m_unexpected == 0);
        TEST_ASSERT(m_outcome.load() == Outcome::resumed);
        std::cerr << "QBOX_UART_FIFO_WFI_RESUME mode="
                  << (p_write_after_idle.get_value() ? "quiescent" : "active")
                  << " spi=52 intid=84 byte="
                  << (p_write_after_idle.get_value() ? "W" : "P")
                  << " entered=1 resumed=1" << std::endl;
        sc_core::sc_stop();
    }

    void trace_uart_irq()
    {
        if (m_gic.spi_in[UART_SPI].read()) {
            m_uart_irq_seen = true;
            m_gic_spi_seen = true;
            m_pl011_rx_seen = m_pl011_rx_seen || m_uart.s->read_count > 0;
        }
    }

    void trace_parent_irq()
    {
        m_parent_irq_seen = true;
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    void mmio_write(int, uint64_t, uint64_t data, size_t) override
    {
        if (data == 1) {
            ++m_entered;
            m_entered_event.async_notify();
        } else if (data == 4) {
            m_iar84_seen = true;
        } else if (data == 5) {
            m_uartdr_seen = true;
        } else if (data == 2) {
            ++m_resumed;
            m_outcome.store(Outcome::resumed);
            m_terminal_event.async_notify();
        } else {
            ++m_unexpected;
            m_outcome.store(Outcome::unexpected);
            m_terminal_event.async_notify();
        }
    }

    uint64_t mmio_read(int, uint64_t, size_t) override
    {
        TEST_FAIL("Unexpected CPU read");
    }

    bool dmi_request(int, uint64_t, size_t, tlm::tlm_dmi&) override
    {
        TEST_FAIL("Unexpected DMI request");
    }
};

constexpr const char* CpuArmManagedUartFifoWfiTest::FIRMWARE;

int sc_main(int argc, char* argv[])
{
    if (cancel_eintr_test_requested(argc, argv)) {
        test_cancel_eintr_retry();
        std::cerr << "QBOX_UART_FIFO_CANCEL_EINTR attempts=2 delivered=1 "
                     "failure_fallback=pollhup released=1"
                  << std::endl;
        return 0;
    }
    const bool write_after_idle = write_after_idle_requested(argc, argv);
    const bool close_after_idle = close_after_idle_requested(argc, argv);
    TEST_ASSERT(!(write_after_idle && close_after_idle));
    create_fifo(!(write_after_idle || close_after_idle));
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<size_t>(argc) + 8);
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--async-host-input-after-wfi") != 0 &&
            std::strcmp(argv[i], "--closed-writer-after-wfi") != 0) {
            arguments.emplace_back(argv[i]);
        }
    }
    arguments.insert(arguments.end(), {
        "-p", "test-bench.backend.read_file=" + fifo_path,
        "-p", "test-bench.backend.poll_read=true",
        "-p", "test-bench.backend.poll_interval_ms=1",
        "-p", std::string("test-bench.write_after_idle=") +
                  (write_after_idle ? "true" : "false"),
        "-p", std::string("test-bench.close_after_idle=") +
                  (close_after_idle ? "true" : "false"),
    });
    std::vector<char*> argument_ptrs;
    argument_ptrs.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argument_ptrs.push_back(&argument[0]);
    }
    const int result = run_testbench<CpuArmManagedUartFifoWfiTest>(
        static_cast<int>(argument_ptrs.size()), argument_ptrs.data());
    cleanup_fifo();
    return result;
}
