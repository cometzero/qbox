/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file file.h
 * @brief file backend which support biflow socket
 */

#ifndef _GS_UART_BACKEND_FILE_H_
#define _GS_UART_BACKEND_FILE_H_

#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <async_event.h>
#include <uutils.h>
#include <ports/biflow-socket.h>
#include <module_factory_registery.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#include <scp/report.h>
class char_backend_file : public sc_core::sc_module
{
protected:
    cci::cci_param<std::string> p_read_file;
    cci::cci_param<std::string> p_write_file;
    cci::cci_param<unsigned int> p_baudrate;
    cci::cci_param<bool> p_poll_read;
    cci::cci_param<unsigned int> p_poll_interval_ms;

private:
    FILE* r_file = nullptr;
    FILE* w_file = nullptr;
    std::atomic_bool m_poll_running = false;
    std::thread m_poll_thread;
    double delay;
    SCP_LOGGER();

public:
    gs::biflow_socket<char_backend_file> socket;
    sc_core::sc_event update_event;

    /**
     * char_backend_file() - Construct the file-backend
     * @name: this backend's name
     * the paramters p_read_file, p_write_file and p_baudrate are CCI paramters
     */
    char_backend_file(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_read_file("read_file", "", "read file path")
        , p_write_file("write_file", "", "write file path")
        , p_baudrate("baudrate", 0, "number of bytes per second")
        , p_poll_read("poll_read", false, "poll read_file instead of sending EOF at initial end of file")
        , p_poll_interval_ms("poll_interval_ms", 1, "polling interval for poll_read in milliseconds")
        , socket("biflow_socket")
    {
        SCP_TRACE(()) << "constructor";

        if (p_write_file.get_value().empty() && p_read_file.get_value().empty()) {
            SCP_ERR(()) << "At least one of read_file or write_file must be specified.\n";
        }

        SC_THREAD(rcv_thread);
        sensitive << update_event;

        socket.register_b_transport(this, &char_backend_file::writefn);
    }

    bool poll_read_enabled()
    {
#ifndef _WIN32
        return p_poll_read.get_value();
#else
        return false;
#endif
    }

    void start_of_simulation()
    {
        if (!p_read_file.get_value().empty()) {
#ifndef _WIN32
            if (poll_read_enabled()) {
                int fd = open(p_read_file.get_value().c_str(), O_RDONLY | O_NONBLOCK);
                if (fd >= 0) {
                    r_file = fdopen(fd, "r");
                    if (r_file == NULL) {
                        close(fd);
                    }
                }
            } else
#endif
            {
                r_file = fopen(p_read_file.get_value().c_str(), "r");
            }

            if (r_file == NULL) SCP_ERR(()) << "Error opening the input file " << p_read_file.get_value() << ".\n";
            if (r_file != NULL && poll_read_enabled()) {
                set_nonblocking(r_file);
                m_poll_running = true;
                m_poll_thread = std::thread(&char_backend_file::poll_read_thread, this);
            }
            update_event.notify(sc_core::SC_ZERO_TIME);
        }

        if (!p_write_file.get_value().empty()) {
            w_file = fopen(p_write_file.get_value().c_str(), "w");

            if (w_file == NULL) SCP_ERR(()) << "Error opening the output file " << p_write_file.get_value() << ".\n";

            socket.can_receive_any();
        }
    }
    void end_of_elaboration() {}

    void set_nonblocking(FILE* file)
    {
#ifndef _WIN32
        int fd = fileno(file);
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
#else
        (void)file;
#endif
    }

    void rcv_thread()
    {
        if (p_baudrate.get_value() == 0)
            delay = 0;
        else
            delay = (1.0 / p_baudrate.get_value());
        if (poll_read_enabled()) {
            return;
        }
        char c;
        if (r_file == nullptr) sc_core::wait(update_event);
        if (r_file == nullptr) return;
        while (fread(&c, sizeof(char), 1, r_file) == 1) {
            socket.enqueue(c);
            sc_core::wait(delay, sc_core::SC_SEC);
        }
        socket.enqueue(EOF);
        fclose(r_file);
        r_file = nullptr;
    }

    void poll_read_thread()
    {
#ifndef _WIN32
        const unsigned int poll_ms = p_poll_interval_ms.get_value() == 0 ? 1 : p_poll_interval_ms.get_value();
        char buffer[256];
        while (m_poll_running) {
            if (r_file == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
                continue;
            }

            ssize_t count = read(fileno(r_file), buffer, sizeof(buffer));
            if (count > 0) {
                for (ssize_t i = 0; i < count; ++i) {
                    socket.enqueue(buffer[i]);
                }
                continue;
            }
            if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                SCP_ERR(()) << "Error reading the input file " << p_read_file.get_value() << ".\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
#endif
    }

    void writefn(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
    {
        uint8_t* data = txn.get_data_ptr();
        for (int i = 0; i < txn.get_streaming_width(); i++) {
            size_t ret = fwrite(&data[i], sizeof(uint8_t), 1, w_file);
            if (ret != 1) {
                SCP_ERR(()) << "Error writing to the file.\n";
            }
        }
        fflush(w_file);
    }

    void stop_poll_thread()
    {
        m_poll_running = false;
        if (m_poll_thread.joinable()) {
            m_poll_thread.join();
        }
    }

    void end_of_simulation() { stop_poll_thread(); }

    ~char_backend_file()
    {
        stop_poll_thread();

        if (w_file != NULL) {
            fclose(w_file);
        }

        if (r_file != NULL) {
            fclose(r_file);
        }
    }
};
// GSC_MODULE_REGISTER(char_backend_file);
extern "C" void module_register();
#endif
