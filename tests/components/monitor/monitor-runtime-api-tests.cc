/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <cci_configuration>
#include <systemc>

#include <async_event.h>
#include <monitor.h>
#include <runtime-action-service.h>

#include "test/test.h"

class DummyRuntimeActionService : public sc_core::sc_module, public gs::RuntimeActionService
{
    std::thread::id m_systemc_thread = std::this_thread::get_id();
    gs::RuntimeActionStatus m_status;

public:
    bool submit_on_systemc = false;
    unsigned submissions = 0;

    explicit DummyRuntimeActionService(const sc_core::sc_module_name& name): sc_core::sc_module(name)
    {
        m_status.id = 42;
        m_status.state = gs::RuntimeActionState::ACCEPTED;
        m_status.target = "dummy.event";
        m_status.action = "trigger";
        m_status.generation = 3;
        m_status.result = "ok";
    }

    std::vector<gs::RuntimeTargetCapability> capabilities() const override
    {
        gs::RuntimeTargetCapability capability;
        capability.target = "dummy.event";
        capability.resource_type = gs::RuntimeResourceType::EVENT;
        capability.actions.push_back({ "trigger", { "count" } });
        capability.attributes.emplace("enabled", gs::RuntimeActionValue(true));
        return { capability };
    }

    gs::RuntimeTargetSnapshotReply target_snapshot(const std::string& target) const override
    {
        gs::RuntimeTargetSnapshotReply reply;
        if (target != "dummy.event") {
            reply.http_status = 404;
            reply.error_code = "target-not-found";
            reply.error_message = "unknown target";
            return reply;
        }
        reply.value.target = target;
        reply.value.resource_type = gs::RuntimeResourceType::EVENT;
        reply.value.values.emplace("count", gs::RuntimeActionValue(uint64_t(0)));
        return reply;
    }

    gs::RuntimeActionStatusReply submit(const gs::RuntimeActionRequest& request) override
    {
        gs::RuntimeActionStatusReply reply;
        const bool relative = request.trigger.type == gs::RuntimeActionTrigger::Type::RELATIVE_SIMULATION_TIME &&
                              request.trigger.delay_ns == 10 && request.reset_domain.empty() &&
                              !request.has_expected_generation;
        const bool absolute = request.trigger.type == gs::RuntimeActionTrigger::Type::ABSOLUTE_SIMULATION_TIME &&
                              request.trigger.time_ns == 20 && request.reset_domain == "ap" &&
                              request.has_expected_generation && request.expected_generation == 3;
        if (request.target != "dummy.event" || request.action != "trigger" || (!relative && !absolute) ||
            !request.clear_on_reset ||
            request.parameters.at("count").unsigned_integer() != 2 || !request.parameters.at("enabled").boolean() ||
            request.parameters.at("label").string() != "test") {
            reply.http_status = 400;
            reply.error_code = "invalid-request";
            reply.error_message = "unexpected request";
            return reply;
        }
        submit_on_systemc = std::this_thread::get_id() == m_systemc_thread;
        ++submissions;
        reply.http_status = 202;
        reply.value = m_status;
        return reply;
    }

    std::vector<gs::RuntimeActionStatus> list() const override { return { m_status }; }

    gs::RuntimeActionStatusReply status(uint64_t id) const override
    {
        gs::RuntimeActionStatusReply reply;
        if (id != m_status.id) {
            reply.http_status = 404;
            reply.error_code = "request-not-found";
            reply.error_message = "unknown request";
            return reply;
        }
        reply.value = m_status;
        return reply;
    }

    gs::RuntimeActionStatusReply cancel(uint64_t id) override
    {
        gs::RuntimeActionStatusReply reply = status(id);
        if (reply.ok()) {
            m_status.state = gs::RuntimeActionState::CANCELLED;
            m_status.result = "cancelled-by-client";
            reply.value = m_status;
        }
        return reply;
    }
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

HttpResponse request(uint16_t port, const std::string& method, const std::string& path,
                     const std::string& body = std::string())
{
    boost::asio::io_context context;
    boost::asio::ip::tcp::socket socket(context);
    boost::system::error_code error;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        socket.connect({ boost::asio::ip::make_address("127.0.0.1"), port }, error);
        if (!error) {
            break;
        }
        socket.close();
        socket = boost::asio::ip::tcp::socket(context);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (error) {
        throw std::runtime_error("monitor server did not accept connections");
    }

    std::ostringstream request_stream;
    request_stream << method << " " << path << " HTTP/1.1\r\n"
                   << "Host: 127.0.0.1\r\n"
                   << "Connection: close\r\n";
    if (!body.empty()) {
        request_stream << "Content-Type: application/json\r\n"
                       << "Content-Length: " << body.size() << "\r\n";
    }
    request_stream << "\r\n" << body;
    boost::asio::write(socket, boost::asio::buffer(request_stream.str()));

    boost::asio::streambuf response_buffer;
    while (boost::asio::read(socket, response_buffer, boost::asio::transfer_at_least(1), error)) {
    }
    if (error != boost::asio::error::eof) {
        throw boost::system::system_error(error);
    }

    std::istream response_stream(&response_buffer);
    std::string http_version;
    HttpResponse response;
    response_stream >> http_version >> response.status;
    std::string ignored;
    std::getline(response_stream, ignored);
    std::string response_text((std::istreambuf_iterator<char>(response_stream)), std::istreambuf_iterator<char>());
    std::size_t body_offset = response_text.find("\r\n\r\n");
    if (body_offset == std::string::npos) {
        throw std::runtime_error("invalid HTTP response");
    }
    response.body = response_text.substr(body_offset + 4);
    return response;
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class MonitorRuntimeApiTest : public TestBench
{
    cci::cci_param<std::string> p_test_mode;
    DummyRuntimeActionService m_service;
    gs::monitor<32> m_monitor;
    gs::async_event m_done;
    std::thread m_client;
    std::string m_client_error;

    void client_test()
    {
        try {
            const uint16_t port = m_monitor.server_port();
            require(port != 0, "monitor did not publish its ephemeral port");
            HttpResponse simulation_time = request(port, "GET", "/sc_time");
            require(simulation_time.status == 200, "existing sc_time endpoint failed");

            const std::string mode = p_test_mode.get_value();
            HttpResponse capabilities = request(port, "GET", "/api/v1/injection/capabilities");
            HttpResponse capabilities_alias = request(port, "GET", "/api/v1/injection-capabilities");
            if (mode == "disabled") {
                require(capabilities.status == 403, "disabled mutation was not rejected");
                require(capabilities_alias.status == 403, "disabled capability alias was not rejected");
                require(capabilities.body.find("\"error\":{") != std::string::npos, "error payload is not nested");
                require(capabilities.body.find("mutation-disabled") != std::string::npos,
                        "disabled error code is missing");
                require(request(port, "POST", "/api/v1/injections", "{}").status == 403,
                        "disabled submission was not rejected");
            } else if (mode == "missing") {
                require(capabilities.status == 503, "missing service was not rejected");
                require(capabilities_alias.status == 503, "missing service alias was not rejected");
                require(capabilities.body.find("simulation-unavailable") != std::string::npos,
                        "missing service error code is missing");
            } else {
                require(capabilities.status == 200, "capability request failed");
                require(capabilities_alias.status == 200, "capability alias request failed");
                require(capabilities.body.find("dummy.event") != std::string::npos,
                        "capability target is missing");
                HttpResponse snapshot = request(port, "GET", "/api/v1/injection/targets/dummy.event");
                require(snapshot.status == 200, "target snapshot failed");

                HttpResponse invalid = request(port, "POST", "/api/v1/injections", "{}");
                require(invalid.status == 400, "invalid request was not rejected");
                require(invalid.body.find("invalid-request") != std::string::npos,
                        "invalid request error code is missing");

                const std::string action =
                    "{\"schema_version\":1,\"target\":\"dummy.event\",\"action\":\"trigger\","
                    "\"trigger\":{\"type\":\"relative-simulation-time\",\"delay_ns\":10},"
                    "\"parameters\":{\"count\":2,\"enabled\":true,\"label\":\"test\"},"
                    "\"clear_on_reset\":true}";
                HttpResponse submitted = request(port, "POST", "/api/v1/injections", action);
                require(submitted.status == 202, "valid request was not accepted");
                require(submitted.body.find("\"id\":42") != std::string::npos, "request id is missing");

                const std::string absolute_action =
                    "{\"schema_version\":1,\"target\":\"dummy.event\",\"action\":\"trigger\","
                    "\"reset_domain\":\"ap\",\"expected_generation\":3,"
                    "\"trigger\":{\"type\":\"absolute-simulation-time\",\"time_ns\":20},"
                    "\"parameters\":{\"count\":2,\"enabled\":true,\"label\":\"test\"}}";
                require(request(port, "POST", "/api/v1/injections", absolute_action).status == 202,
                        "absolute simulation-time request was not accepted");

                const std::string unsupported_prefix =
                    "{\"schema_version\":1,\"target\":\"dummy.event\",\"action\":\"trigger\",";
                HttpResponse persistent = request(port, "POST", "/api/v1/injections",
                                                  unsupported_prefix + "\"persistent\":true}");
                require(persistent.status == 400 &&
                            persistent.body.find("persistent requests are not supported") != std::string::npos,
                        "persistent policy was not rejected");
                HttpResponse duration = request(port, "POST", "/api/v1/injections",
                                                unsupported_prefix + "\"duration_ns\":10}");
                require(duration.status == 400 &&
                            duration.body.find("duration policies are not supported") != std::string::npos,
                        "duration policy was not rejected");
                require(request(port, "GET", "/api/v1/injections").status == 200, "request list failed");
                require(request(port, "GET", "/api/v1/injections/42").status == 200, "request status failed");
                HttpResponse cancelled = request(port, "DELETE", "/api/v1/injections/42");
                require(cancelled.status == 200, "request cancel failed");
                require(cancelled.body.find("cancelled") != std::string::npos, "cancel state is missing");
            }
        } catch (const std::exception& exception) {
            m_client_error = exception.what();
        }
        m_done.async_notify();
    }

    void run_test()
    {
        TEST_ASSERT(m_monitor.p_bind_address.get_value() == "127.0.0.1");
        TEST_ASSERT(gs::monitor<32>::is_runtime_configuration_safe(false, "0.0.0.0"));
        TEST_ASSERT(gs::monitor<32>::is_runtime_configuration_safe(true, "127.0.0.1"));
        TEST_ASSERT(gs::monitor<32>::is_runtime_configuration_safe(true, "::1"));
        TEST_ASSERT(!gs::monitor<32>::is_runtime_configuration_safe(true, "0.0.0.0"));
        wait(m_done);
        m_client.join();
        if (!m_client_error.empty()) {
            throw std::runtime_error(m_client_error);
        }
        if (p_test_mode.get_value() == "full") {
            TEST_ASSERT(m_service.submit_on_systemc);
            TEST_ASSERT(m_service.submissions == 2);
        }
        sc_core::sc_stop();
    }

public:
    explicit MonitorRuntimeApiTest(const sc_core::sc_module_name& name)
        : TestBench(name), p_test_mode("test_mode", "full"), m_service("service"), m_monitor("monitor"), m_done(true)
    {
        SC_THREAD(run_test);
    }

    void start_of_simulation() override
    {
        m_client = std::thread([this] { client_test(); });
    }

    ~MonitorRuntimeApiTest() override
    {
        if (m_client.joinable()) {
            m_client.join();
        }
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<MonitorRuntimeApiTest>(argc, argv); }
