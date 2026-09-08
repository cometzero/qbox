/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include "runtime-action-api.h"

#include <limits>
#include <map>
#include <utility>
#include <vector>

#define RUNTIME_ACTION_MAX_BODY_SIZE (1024ULL * 16ULL)

namespace gs {
namespace runtime_action_api {
namespace {

const char* resource_type_name(RuntimeResourceType type)
{
    switch (type) {
    case RuntimeResourceType::INTERRUPT:
        return "interrupt";
    case RuntimeResourceType::EVENT:
        return "event";
    case RuntimeResourceType::CONTROL:
        return "control";
    case RuntimeResourceType::GPIO_PIN:
        return "gpio-pin";
    }
    return "control";
}

const char* action_state_name(RuntimeActionState state)
{
    switch (state) {
    case RuntimeActionState::ACCEPTED:
        return "accepted";
    case RuntimeActionState::SCHEDULED:
        return "scheduled";
    case RuntimeActionState::ACTIVE:
        return "active";
    case RuntimeActionState::COMPLETED:
        return "completed";
    case RuntimeActionState::CANCELLED:
        return "cancelled";
    case RuntimeActionState::FAILED:
        return "failed";
    }
    return "failed";
}

crow::json::wvalue action_value_json(const RuntimeActionValue& value)
{
    switch (value.type()) {
    case RuntimeActionValue::Type::BOOLEAN:
        return crow::json::wvalue(value.boolean());
    case RuntimeActionValue::Type::UNSIGNED_INTEGER:
        return crow::json::wvalue(value.unsigned_integer());
    case RuntimeActionValue::Type::STRING:
        return crow::json::wvalue(value.string());
    }
    return crow::json::wvalue();
}

crow::json::wvalue action_values_json(const std::map<std::string, RuntimeActionValue>& values)
{
    crow::json::wvalue json = crow::json::wvalue::empty_object();
    for (const auto& value : values) {
        json[value.first] = action_value_json(value.second);
    }
    return json;
}

bool parse_unsigned(const crow::json::rvalue& json, uint64_t& value)
{
    if (json.t() != crow::json::type::Number) {
        return false;
    }
    if (json.nt() == crow::json::num_type::Floating_point ||
        json.nt() == crow::json::num_type::Double_precision_floating_point) {
        return false;
    }
    if (json.nt() == crow::json::num_type::Signed_integer && json.i() < 0) {
        return false;
    }
    value = json.u();
    return true;
}

bool parse_action_value(const crow::json::rvalue& json, RuntimeActionValue& value)
{
    if (json.t() == crow::json::type::True || json.t() == crow::json::type::False) {
        value = RuntimeActionValue(json.b());
        return true;
    }
    if (json.t() == crow::json::type::String) {
        value = RuntimeActionValue(static_cast<std::string>(json));
        return true;
    }
    uint64_t unsigned_value = 0;
    if (parse_unsigned(json, unsigned_value)) {
        value = RuntimeActionValue(unsigned_value);
        return true;
    }
    return false;
}

}

crow::json::wvalue capability_json(const RuntimeTargetCapability& capability)
{
    crow::json::wvalue json;
    json["target"] = capability.target;
    json["resource_type"] = resource_type_name(capability.resource_type);

    std::vector<crow::json::wvalue> actions;
    for (const auto& action : capability.actions) {
        crow::json::wvalue action_json;
        action_json["name"] = action.name;
        action_json["required"] = action.required_parameters;
        actions.push_back(std::move(action_json));
    }
    json["actions"] = std::move(actions);
    json["attributes"] = action_values_json(capability.attributes);
    return json;
}

crow::json::wvalue snapshot_json(const RuntimeTargetSnapshot& snapshot)
{
    crow::json::wvalue json;
    json["target"] = snapshot.target;
    json["resource_type"] = resource_type_name(snapshot.resource_type);
    json["values"] = action_values_json(snapshot.values);
    return json;
}

crow::json::wvalue status_json(const RuntimeActionStatus& status)
{
    crow::json::wvalue json;
    json["id"] = status.id;
    json["state"] = action_state_name(status.state);
    json["target"] = status.target;
    json["action"] = status.action;
    json["generation"] = status.generation;
    json["result"] = status.result;
    if (status.has_requested_sim_time_ns) {
        json["requested_sim_time_ns"] = status.requested_sim_time_ns;
    }
    if (status.has_applied_sim_time_ns) {
        json["applied_sim_time_ns"] = status.applied_sim_time_ns;
    }
    if (status.has_cleared_sim_time_ns) {
        json["cleared_sim_time_ns"] = status.cleared_sim_time_ns;
    }
    return json;
}

crow::response json_response(int status, crow::json::wvalue body) { return crow::response(status, std::move(body)); }

crow::response error_response(int status, const std::string& code, const std::string& message)
{
    crow::json::wvalue error;
    error["code"] = code;
    error["message"] = message;
    crow::json::wvalue body;
    body["error"] = std::move(error);
    return json_response(status, std::move(body));
}

crow::response status_reply_response(const RuntimeActionStatusReply& reply)
{
    if (!reply.ok()) {
        return error_response(reply.http_status, reply.error_code, reply.error_message);
    }
    return json_response(reply.http_status, status_json(reply.value));
}

bool parse_action_request(const std::string& body, RuntimeActionRequest& request, std::string& error)
{
    if (body.empty() || body.size() > RUNTIME_ACTION_MAX_BODY_SIZE) {
        error = "request body must be between 1 and 16384 bytes";
        return false;
    }

    crow::json::rvalue json = crow::json::load(body);
    if (!json || json.t() != crow::json::type::Object) {
        error = "request body must be a JSON object";
        return false;
    }
    if (!json.has("schema_version") || !json.has("target") || !json.has("action")) {
        error = "schema_version, target, and action are required";
        return false;
    }
    if (json.has("persistent")) {
        error = "persistent requests are not supported";
        return false;
    }
    if (json.has("duration") || json.has("duration_ns")) {
        error = "duration policies are not supported";
        return false;
    }

    uint64_t schema_version = 0;
    if (!parse_unsigned(json["schema_version"], schema_version) || schema_version != 1 ||
        schema_version > std::numeric_limits<uint32_t>::max()) {
        error = "schema_version must be 1";
        return false;
    }
    if (json["target"].t() != crow::json::type::String || json["action"].t() != crow::json::type::String) {
        error = "target and action must be strings";
        return false;
    }

    request.schema_version = static_cast<uint32_t>(schema_version);
    request.target = static_cast<std::string>(json["target"]);
    request.action = static_cast<std::string>(json["action"]);
    if (request.target.empty() || request.action.empty()) {
        error = "target and action must not be empty";
        return false;
    }

    if (json.has("reset_domain")) {
        if (json["reset_domain"].t() != crow::json::type::String) {
            error = "reset_domain must be a string";
            return false;
        }
        request.reset_domain = static_cast<std::string>(json["reset_domain"]);
    }

    if (json.has("expected_generation")) {
        if (!parse_unsigned(json["expected_generation"], request.expected_generation)) {
            error = "expected_generation must be an unsigned integer";
            return false;
        }
        request.has_expected_generation = true;
    }

    if (json.has("trigger")) {
        const auto& trigger = json["trigger"];
        if (trigger.t() != crow::json::type::Object || !trigger.has("type") ||
            trigger["type"].t() != crow::json::type::String) {
            error = "trigger must contain a string type";
            return false;
        }
        std::string type = static_cast<std::string>(trigger["type"]);
        if (type == "immediate") {
            request.trigger.type = RuntimeActionTrigger::Type::IMMEDIATE;
        } else if (type == "absolute-simulation-time") {
            request.trigger.type = RuntimeActionTrigger::Type::ABSOLUTE_SIMULATION_TIME;
            if (!trigger.has("time_ns") || !parse_unsigned(trigger["time_ns"], request.trigger.time_ns)) {
                error = "absolute-simulation-time trigger requires unsigned time_ns";
                return false;
            }
        } else if (type == "relative-simulation-time") {
            request.trigger.type = RuntimeActionTrigger::Type::RELATIVE_SIMULATION_TIME;
            if (!trigger.has("delay_ns") || !parse_unsigned(trigger["delay_ns"], request.trigger.delay_ns)) {
                error = "relative-simulation-time trigger requires unsigned delay_ns";
                return false;
            }
        } else {
            error = "unsupported trigger type";
            return false;
        }
    }

    if (json.has("parameters")) {
        const auto& parameters = json["parameters"];
        if (parameters.t() != crow::json::type::Object) {
            error = "parameters must be an object";
            return false;
        }
        for (const auto& parameter : parameters) {
            RuntimeActionValue value;
            if (!parse_action_value(parameter, value)) {
                error = "parameters accept only booleans, unsigned integers, and strings";
                return false;
            }
            request.parameters[parameter.key()] = std::move(value);
        }
    }

    if (json.has("clear_on_reset")) {
        const auto& clear_on_reset = json["clear_on_reset"];
        if (clear_on_reset.t() != crow::json::type::True && clear_on_reset.t() != crow::json::type::False) {
            error = "clear_on_reset must be a boolean";
            return false;
        }
        request.clear_on_reset = clear_on_reset.b();
    }
    return true;
}

}
}
