/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_RUNTIME_ACTION_SERVICE_H
#define _LIBQBOX_RUNTIME_ACTION_SERVICE_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gs {

enum class RuntimeResourceType { INTERRUPT, EVENT, CONTROL, GPIO_PIN };

enum class RuntimeActionState { ACCEPTED, SCHEDULED, ACTIVE, COMPLETED, CANCELLED, FAILED };

class RuntimeActionValue
{
public:
    enum class Type { BOOLEAN, UNSIGNED_INTEGER, STRING };

private:
    Type m_type = Type::STRING;
    bool m_boolean = false;
    uint64_t m_unsigned_integer = 0;
    std::string m_string;

public:
    RuntimeActionValue() = default;
    explicit RuntimeActionValue(bool value): m_type(Type::BOOLEAN), m_boolean(value) {}
    explicit RuntimeActionValue(uint64_t value): m_type(Type::UNSIGNED_INTEGER), m_unsigned_integer(value) {}
    explicit RuntimeActionValue(std::string value): m_type(Type::STRING), m_string(std::move(value)) {}

    Type type() const { return m_type; }
    bool boolean() const { return m_boolean; }
    uint64_t unsigned_integer() const { return m_unsigned_integer; }
    const std::string& string() const { return m_string; }
};

struct RuntimeActionSchema {
    std::string name;
    std::vector<std::string> required_parameters;
};

struct RuntimeTargetCapability {
    std::string target;
    RuntimeResourceType resource_type = RuntimeResourceType::CONTROL;
    std::vector<RuntimeActionSchema> actions;
    std::map<std::string, RuntimeActionValue> attributes;
};

struct RuntimeActionTrigger {
    enum class Type { IMMEDIATE, RELATIVE_SIMULATION_TIME };

    Type type = Type::IMMEDIATE;
    uint64_t delay_ns = 0;
};

struct RuntimeActionRequest {
    uint32_t schema_version = 1;
    std::string target;
    std::string action;
    RuntimeActionTrigger trigger;
    std::map<std::string, RuntimeActionValue> parameters;
    bool clear_on_reset = true;
};

struct RuntimeTargetSnapshot {
    std::string target;
    RuntimeResourceType resource_type = RuntimeResourceType::CONTROL;
    std::map<std::string, RuntimeActionValue> values;
};

struct RuntimeActionStatus {
    uint64_t id = 0;
    RuntimeActionState state = RuntimeActionState::FAILED;
    std::string target;
    std::string action;
    bool has_requested_sim_time_ns = false;
    uint64_t requested_sim_time_ns = 0;
    bool has_applied_sim_time_ns = false;
    uint64_t applied_sim_time_ns = 0;
    bool has_cleared_sim_time_ns = false;
    uint64_t cleared_sim_time_ns = 0;
    uint64_t generation = 0;
    std::string result;
};

template <typename T>
struct RuntimeActionReply {
    int http_status = 200;
    std::string error_code;
    std::string error_message;
    T value;

    bool ok() const { return error_code.empty(); }
};

using RuntimeTargetSnapshotReply = RuntimeActionReply<RuntimeTargetSnapshot>;
using RuntimeActionStatusReply = RuntimeActionReply<RuntimeActionStatus>;

class RuntimeActionService
{
public:
    virtual ~RuntimeActionService() = default;

    virtual std::vector<RuntimeTargetCapability> capabilities() const = 0;
    virtual RuntimeTargetSnapshotReply target_snapshot(const std::string& target) const = 0;
    virtual RuntimeActionStatusReply submit(const RuntimeActionRequest& request) = 0;
    virtual std::vector<RuntimeActionStatus> list() const = 0;
    virtual RuntimeActionStatusReply status(uint64_t id) const = 0;
    virtual RuntimeActionStatusReply cancel(uint64_t id) = 0;
};

}

#endif
