/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

#include <cstddef>
#include <exception>

namespace qemu {

Timer::Timer(std::shared_ptr<LibQemuInternals> internals): m_int(internals) {}

Timer::~Timer()
{
    del();

    if (m_timer != nullptr) {
        m_int->exports().timer_free(m_timer);
    }
}

static void timer_generic_callback(void* opaque)
{
    Timer::TimerCallbackFn* cb = reinterpret_cast<Timer::TimerCallbackFn*>(opaque);

    (*cb)();
}

void Timer::set_callback(TimerCallbackFn cb)
{
    m_cb = cb;
    m_timer = m_int->exports().timer_new_virtual_ns(timer_generic_callback, reinterpret_cast<void*>(&m_cb));
}

void Timer::mod(int64_t deadline)
{
    if (m_timer != nullptr) {
        m_int->exports().timer_mod_ns(m_timer, deadline);
    }
}

void Timer::del()
{
    if (m_timer != nullptr) {
        m_int->exports().timer_del(m_timer);
    }
}

Clock::Clock(QemuClock* clock, std::shared_ptr<LibQemuInternals> internals)
    : m_clock(clock), m_int(std::move(internals))
{
}

bool Clock::update_hz(uint64_t frequency_hz)
{
    const size_t field_end = offsetof(LibQemuExports, clock_update_hz) +
                             sizeof(((LibQemuExports*)nullptr)->clock_update_hz);
    m_int->require_v2_export(field_end, "clock_update_hz");
    return m_int->exports().clock_update_hz(m_clock, frequency_hz);
}

void IOThreadJob::invoke(void* opaque)
{
    IOThreadJob* job = static_cast<IOThreadJob*>(opaque);
    try {
        job->m_callback();
    } catch (...) {
        std::lock_guard<std::mutex> lock(job->m_exception_mutex);
        job->m_exception = std::current_exception();
    }
}

IOThreadJob::IOThreadJob(std::shared_ptr<LibQemuInternals> internals,
                         std::function<void()> callback)
    : m_int(std::move(internals)), m_callback(std::move(callback))
{
    const size_t field_end = offsetof(LibQemuExports, iothread_job_stop) +
                             sizeof(((LibQemuExports*)nullptr)->iothread_job_stop);
    m_int->require_v2_export(field_end, "iothread job API");
    m_job = m_int->exports().iothread_job_new(&IOThreadJob::invoke, this);
    if (m_job == nullptr) {
        throw LibQemuException("failed to create libqemu iothread job");
    }
}

IOThreadJob::~IOThreadJob()
{
    if (m_job == nullptr) {
        return;
    }
    stop();
    drain();
    m_int->exports().iothread_job_free(m_job);
}

bool IOThreadJob::schedule()
{
    return m_int->exports().iothread_job_schedule(m_job);
}

void IOThreadJob::cancel()
{
    m_int->exports().iothread_job_cancel(m_job);
}

void IOThreadJob::stop()
{
    m_int->exports().iothread_job_stop(m_job);
}

void IOThreadJob::drain()
{
    m_int->exports().iothread_job_drain(m_job);
}

void IOThreadJob::rethrow_if_failed()
{
    std::exception_ptr exception;
    {
        std::lock_guard<std::mutex> lock(m_exception_mutex);
        exception = m_exception;
        m_exception = nullptr;
    }
    if (exception) {
        std::rethrow_exception(exception);
    }
}

} // namespace qemu
