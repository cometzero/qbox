/*
 * This file is part of libqbox
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_RESET_QMP_H
#define _LIBQBOX_COMPONENTS_RESET_QMP_H

#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/biflow-socket.h>
#include <device.h>
#include <qemu-instance.h>
#include <module_factory_registery.h>
#ifdef _WIN32
#include <winsock2.h>
#include <afunix.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdio>


#define QMP_SOCK_POLL_TIMEOUT 300
#define QMP_RECV_BUFFER_LEN   8192

#ifdef _WIN32
typedef SOCKET socket_t;
static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#define CLOSE_SOCKET closesocket
#define SOCK_POLL    WSAPoll
#else
typedef int socket_t;
static constexpr socket_t INVALID_SOCK = -1;
#define CLOSE_SOCKET close
#define SOCK_POLL    poll
#endif

class qmp : public sc_core::sc_module
{
    SCP_LOGGER();

    gs::biflow_socket_multi<qmp> qmp_socket;
    std::atomic<socket_t> m_sockfd{ INVALID_SOCK };

    std::string buffer = "";
    std::thread reader_thread;
    std::string socket_path;
    std::atomic_bool stop_running;

public:
    cci::cci_param<std::string> p_qmp_str;
    cci::cci_param<bool> p_monitor;
    cci::cci_param<unsigned> p_connect_timeout_ms;

    qmp(const sc_core::sc_module_name& name, sc_core::sc_object* o): qmp(name, *(dynamic_cast<QemuInstance*>(o))) {}
    qmp(const sc_core::sc_module_name& n, QemuInstance& inst)
        : sc_core::sc_module(n)
        , p_qmp_str("qmp_str", "", "qmp options string, i.e. unix:./qmp-sock,server,wait=off")
        , qmp_socket("qmp_socket")
        , p_monitor("monitor", true, "use the HMP monitor (true, default) - or QMP (false) ")
        , p_connect_timeout_ms("connect_timeout_ms", 5000, "optional QMP connection deadline (maximum 60000 ms)")
        , stop_running{ false }
    {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            SCP_FATAL(())("WSAStartup failed");
        }
#endif
        SCP_TRACE(())("qmp constructor");
        if (p_qmp_str.get_value().empty()) {
            SCP_FATAL(())("qmp options string is empty!");
        }
        if (p_qmp_str.get_value().find("unix") != std::string::npos) {
            auto first = p_qmp_str.get_value().find(":") + 1;
            auto last = p_qmp_str.get_value().find(",");
            socket_path = p_qmp_str.get_value().substr(first, last - first);
            //            unlink(socket_path.c_str());
        }
        // https://qemu-project.gitlab.io/qemu/interop/qemu-qmp-ref.html
        if (p_monitor) {
            inst.add_arg("-monitor");
        } else {
            inst.add_arg("-qmp");
        }
        inst.add_arg(p_qmp_str.get_value().c_str());

        qmp_socket.register_b_transport(this, &qmp::b_transport);
        qmp_socket.can_receive_any();
    }

    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        char* data = (char*)txn.get_data_ptr();
        int length = txn.get_data_length();
        if (length <= 0 || data == nullptr) return;

        /* collect the string in a buffer, till we see a newline, at which point, if it starts with a brace, send it as
         * is, otherwise wrap it as a human monitor command */
        buffer = buffer + std::string(data, length);
        if (data[length - 1] == '\n' || data[length - 1] == '\r') {
            if (!p_monitor) {
                buffer.erase(
                    std::remove_if(buffer.begin(), buffer.end(), [](char c) { return c == '\r' || c == '\n'; }),
                    buffer.end());
                if (buffer.empty()) return;
                if (buffer[0] != '{') {
                    SCP_WARN(())
                    ("Wrapping raw HMP command {} on QMP interface, consider selecting monitor mode", buffer);
                    buffer = R"("{ "execute": "human-monitor-command", "arguments": { "command-line": ")" + buffer +
                             R"(" } }")";
                }
            }
            if (m_sockfd != INVALID_SOCK) {
                send_message(buffer);
            }
            buffer = "";
        }
        /* echo for the user */
        if (p_monitor) {
            for (int i = 0; i < txn.get_data_length(); i++) {
                qmp_socket.enqueue(txn.get_data_ptr()[i]);
            }
        }
    }

    void start_of_simulation()
    {
        const unsigned timeout_ms = std::min(p_connect_timeout_ms.get_value(), 60000u);
        reader_thread = std::thread([this, timeout_ms]() {
            try {
                if (connect_to_qmp_usocket(timeout_ms)) {
                    struct pollfd qmp_poll;
                    qmp_poll.fd = m_sockfd;
                    qmp_poll.events = POLLIN;
                    while (!stop_running) {
                        const int ret = SOCK_POLL(&qmp_poll, 1, QMP_SOCK_POLL_TIMEOUT);
                        if ((ret == -1 && errno == EINTR) || ret == 0) {
                            continue;
                        } else if ((ret > 0) && (qmp_poll.revents & POLLIN)) {
                            if (!qmp_recv()) break;
                        } else {
                            break;
                        }
                    }
                }
            } catch (const std::exception& error) {
                // SC_REPORT actions may throw even for diagnostics. An optional
                // host reader must never let an exception escape std::thread.
                std::fprintf(stderr, "%s: optional QMP reader stopped: %s\n", name(), error.what());
            } catch (...) {
                std::fprintf(stderr, "%s: optional QMP reader stopped after an exception\n", name());
            }
            const auto fd = m_sockfd.exchange(INVALID_SOCK);
            if (fd != INVALID_SOCK) CLOSE_SOCKET(fd);
        });
    }

    bool qmp_recv()
    {
        char buffer[QMP_RECV_BUFFER_LEN];
        int l = recv(m_sockfd, buffer, QMP_RECV_BUFFER_LEN, 0);
        if (l <= 0) return false;
        for (int i = 0; i < l; i++) {
            qmp_socket.enqueue(buffer[i]);
        }
        return true;
    }

    bool connect_to_qmp_usocket(unsigned timeout_ms)
    {
        SCP_INFO(())("Connecting QMP socket to unix socket {}", socket_path);

        m_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_sockfd == INVALID_SOCK) {
            SCP_WARN(())("Optional QMP bridge unavailable: socket creation failed");
            return false;
        }
#ifndef _WIN32
        timeval send_timeout{2, 0};
        setsockopt(m_sockfd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
#endif

        struct sockaddr_un addr;
        if (socket_path.size() >= sizeof(addr.sun_path)) {
            SCP_WARN(())("Optional QMP bridge unavailable: unix socket path is too long");
            return false;
        }
        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        bool connected = false;
        // QemuInstances start independently. Bound startup retries and keep
        // destruction responsive even when one domain never creates its socket.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        do {
            if (stop_running) break;
            if (connect(m_sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == 0) {
                connected = true;
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (std::chrono::steady_clock::now() < deadline);
        if (!connected) {
            if (!stop_running)
                SCP_WARN(())("Optional QMP bridge unavailable after {} ms: {}", timeout_ms, socket_path);
            return false;
        }

        if (!p_monitor) {
            std::string msg = R"({ "execute": "qmp_capabilities", "arguments": { "enable": ["oob"] } })";
            if (!send_message(msg)) {
                SCP_WARN(())("Optional QMP bridge unavailable: capability negotiation send failed");
                return false;
            }
        }
        return true;
    }

    bool send_message(const std::string& message)
    {
        size_t sent = 0;
        while (sent < message.size() && !stop_running) {
#ifdef MSG_NOSIGNAL
            const int flags = MSG_NOSIGNAL;
#else
            const int flags = 0;
#endif
            int count = send(m_sockfd, message.data() + sent, (int)(message.size() - sent), flags);
            if (count <= 0) return false;
            sent += count;
        }
        return sent == message.size();
    }

    ~qmp()
    {
        stop_running = true;
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

extern "C" void module_register();
#endif //_LIBQBOX_COMPONENTS_QMP_H
