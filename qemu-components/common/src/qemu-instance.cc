/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <future>
#include <iostream>

#include <qemu-instance.h>

namespace {

std::mutex debug_instances_lock;
std::vector<QemuInstance*> registered_debug_instances;

}

void QemuInstance::register_debug_instance(QemuInstance* instance)
{
    std::lock_guard<std::mutex> lock(debug_instances_lock);
    registered_debug_instances.push_back(instance);
}

void QemuInstance::unregister_debug_instance(QemuInstance* instance)
{
    std::lock_guard<std::mutex> lock(debug_instances_lock);
    registered_debug_instances.erase(
        std::remove(registered_debug_instances.begin(),
                    registered_debug_instances.end(), instance),
        registered_debug_instances.end());
}

std::vector<QemuInstance*> QemuInstance::debug_instances()
{
    std::lock_guard<std::mutex> lock(debug_instances_lock);
    return registered_debug_instances;
}

void QemuInstance::set_debug_sync_hold_on_cpus(bool asserted)
{
    std::lock_guard<std::mutex> lock(m_lock);
    for (QemuDeviceBaseIF* device : devices) {
        device->set_debug_sync_hold(asserted);
    }
}

void QemuInstance::enable_global_gdb_pause()
{
    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        if (m_global_debug_enabled) {
            return;
        }
        m_global_debug_enabled = true;
    }

    sc_core::sc_spawn_options options;
    options.spawn_method();
    options.set_sensitivity(&m_global_debug_systemc_event);
    options.dont_initialize();
    sc_core::sc_spawn(
        sc_core::sc_bind(&QemuInstance::global_debug_systemc_handler, this),
        sc_core::sc_gen_unique_name("global_gdb_pause"), &options);

    m_global_debug_worker =
        std::thread(&QemuInstance::global_debug_worker_loop, this);
    m_inst.set_vm_state_callback(
        [this](bool running) { global_debug_vm_state_changed(running); });
}

void QemuInstance::disable_global_gdb_pause()
{
    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        if (!m_global_debug_enabled) {
            return;
        }
        m_global_debug_enabled = false;
        m_global_debug_worker_exit = true;
    }
    m_global_debug_cond.notify_all();
    m_inst.set_vm_state_callback(nullptr);
    if (m_global_debug_worker.joinable()) {
        m_global_debug_worker.join();
    }
}

void QemuInstance::request_global_gdb_pause(uint64_t entry_pc)
{
    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        if (!m_global_debug_enabled) {
            return;
        }
        m_global_debug_desired_paused = true;
        if (entry_pc != 0) {
            m_global_debug_entry_pc = entry_pc;
            m_global_debug_entry_marker_pending = true;
        }
    }
    m_global_debug_cond.notify_all();
}

void QemuInstance::request_global_gdb_resume()
{
    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        if (!m_global_debug_enabled) {
            return;
        }
        m_global_debug_desired_paused = false;
    }
    m_global_debug_cond.notify_all();
}

void QemuInstance::global_debug_vm_state_changed(bool running)
{
    if (running) {
        request_global_gdb_resume();
    } else {
        request_global_gdb_pause();
    }
}

bool QemuInstance::global_debug_run_systemc_action(
    std::unique_lock<std::mutex>& lock,
    GlobalDebugSystemcAction action)
{
    const uint64_t request = ++m_global_debug_systemc_request;
    m_global_debug_systemc_action = action;
    m_global_debug_systemc_event.async_notify();
    m_global_debug_cond.wait(lock, [this, request] {
        return m_global_debug_systemc_ack >= request ||
               m_global_debug_worker_exit;
    });
    return !m_global_debug_worker_exit;
}

void QemuInstance::global_debug_worker_loop()
{
    std::unique_lock<std::mutex> lock(m_global_debug_lock);

    while (!m_global_debug_worker_exit) {
        m_global_debug_cond.wait(lock, [this] {
            return m_global_debug_worker_exit ||
                   m_global_debug_desired_paused !=
                       m_global_debug_applied_paused;
        });
        if (m_global_debug_worker_exit) {
            break;
        }

        if (m_global_debug_desired_paused) {
            lock.unlock();
            std::vector<QemuInstance*> instances = debug_instances();
            std::vector<std::future<QemuInstance*>> stop_jobs;
            stop_jobs.reserve(instances.size());
            for (QemuInstance* instance : instances) {
                stop_jobs.push_back(std::async(
                    std::launch::async, [instance] {
                        if (!instance->get().vm_is_running()) {
                            return static_cast<QemuInstance*>(nullptr);
                        }
                        instance->get().vm_stop_paused();
                        return instance;
                    }));
            }
            std::vector<QemuInstance*> paused_instances;
            for (std::future<QemuInstance*>& job : stop_jobs) {
                QemuInstance* instance = job.get();
                if (instance != nullptr) {
                    paused_instances.push_back(instance);
                }
            }
            for (QemuInstance* instance : instances) {
                instance->set_debug_sync_hold_on_cpus(true);
            }
            lock.lock();

            m_global_debug_paused_instances =
                std::move(paused_instances);
            if (!global_debug_run_systemc_action(
                    lock, GlobalDebugSystemcAction::PAUSE)) {
                break;
            }
            m_global_debug_applied_paused = true;
        } else {
            std::vector<QemuInstance*> paused_instances =
                m_global_debug_paused_instances;
            if (!global_debug_run_systemc_action(
                    lock, GlobalDebugSystemcAction::RESUME)) {
                break;
            }

            lock.unlock();
            std::vector<std::future<void>> start_jobs;
            start_jobs.reserve(paused_instances.size());
            for (QemuInstance* instance : paused_instances) {
                start_jobs.push_back(std::async(
                    std::launch::async,
                    [instance] { instance->get().vm_start(); }));
            }
            for (std::future<void>& job : start_jobs) {
                job.get();
            }
            for (QemuInstance* instance : debug_instances()) {
                instance->set_debug_sync_hold_on_cpus(false);
            }
            lock.lock();

            m_global_debug_paused_instances.clear();
            m_global_debug_applied_paused = false;
        }
    }
}

void QemuInstance::global_debug_systemc_handler()
{
    GlobalDebugSystemcAction action;
    uint64_t request;
    uint64_t entry_pc = 0;
    bool announce_entry = false;

    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        action = m_global_debug_systemc_action;
        request = m_global_debug_systemc_request;
        m_global_debug_systemc_action =
            GlobalDebugSystemcAction::NONE;
        if (action == GlobalDebugSystemcAction::PAUSE &&
            m_global_debug_entry_marker_pending) {
            announce_entry = true;
            entry_pc = m_global_debug_entry_pc;
            m_global_debug_entry_marker_pending = false;
        }
    }

    if (action == GlobalDebugSystemcAction::PAUSE &&
        !m_global_debug_systemc_paused) {
        sc_core::sc_suspend_all();
        m_global_debug_systemc_paused = true;
    } else if (action == GlobalDebugSystemcAction::RESUME &&
               m_global_debug_systemc_paused) {
        sc_core::sc_unsuspend_all();
        m_global_debug_systemc_paused = false;
    }

    if (announce_entry) {
        std::cerr << "QBox GDB entry breakpoint reached: 0x"
                  << std::hex << entry_pc << std::dec << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(m_global_debug_lock);
        m_global_debug_systemc_ack =
            std::max(m_global_debug_systemc_ack, request);
    }
    m_global_debug_cond.notify_all();
}
