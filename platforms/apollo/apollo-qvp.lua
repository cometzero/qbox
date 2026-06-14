-- Apollo QVP full-system entrypoint.
--
-- This file intentionally reuses the existing RD-Aspen RSE-first topology as
-- the service-model baseline. The Apollo full runner supplies Apollo local
-- build artifacts through QBOX_RDASPEN_* environment variables until the
-- Apollo-specific live SI CL0/CL1 wiring replaces the service model.

print("Apollo QVP full-system QBox config running...")

local function apollo_top()
    local str = debug.getinfo(2, "S").source:sub(2)
    if str:match("(.*/)")
    then
        return str:match("(.*/)")
    else
        return "./"
    end
end

local function getenv_or(name, default)
    local value = os.getenv(name)
    if value == nil or value == "" then
        return default
    end
    return value
end

local function getenv_number_or(name, default)
    local value = tonumber(getenv_or(name, default))
    assert(value ~= nil, name.." must be numeric")
    return value
end

local function getenv_bool_or(name, default)
    local value = getenv_or(name, default and "true" or "false")
    return value == "true" or value == "1" or value == "yes"
end

local function lower_decode_priority(target, priority)
    if target ~= nil then
        target.priority = priority
    end
end

local apollo_dir = apollo_top()
local ros = dofile(apollo_dir.."hw-block/ros.lua")
local rse = dofile(apollo_dir.."hw-block/rse.lua")
local ap_compute = dofile(apollo_dir.."hw-block/ap_compute.lua")
local si_cl0 = dofile(apollo_dir.."hw-block/si_cl0.lua")
local si_cl1 = dofile(apollo_dir.."hw-block/si_cl1.lua")
local apollo_root = apollo_dir.."../../../../"
local apollo_si_mode = getenv_or("QBOX_APOLLO_FULL_SI_MODE", "service-model")
local apollo_live_cl1 =
    getenv_bool_or("QBOX_APOLLO_FULL_LIVE_CL1", false) or
    apollo_si_mode == "live-cl1" or apollo_si_mode == "live-cl0-cl1"
local apollo_live_cl0 =
    getenv_bool_or("QBOX_APOLLO_FULL_LIVE_CL0", false) or
    apollo_si_mode == "live-cl0-cl1"
local APOLLO_SI_CL1_HIPC_SHARED_BASE = 0xe0130000
local APOLLO_SI_CL1_HIPC_SHARED_SIZE = 0x00080000

local ctx = {
    apollo_dir = apollo_dir;
    apollo_root = apollo_root;
    apollo_live_cl0 = apollo_live_cl0;
    apollo_live_cl1 = apollo_live_cl1;
    APOLLO_SI_CL1_HIPC_SHARED_BASE = APOLLO_SI_CL1_HIPC_SHARED_BASE;
    APOLLO_SI_CL1_HIPC_SHARED_SIZE = APOLLO_SI_CL1_HIPC_SHARED_SIZE;
    getenv_or = getenv_or;
    getenv_number_or = getenv_number_or;
    getenv_bool_or = getenv_bool_or;
    lower_decode_priority = lower_decode_priority;
    ros = ros;
    rse = rse;
    ap_compute = ap_compute;
}

if apollo_live_cl0 then
    si_cl0.enable(ctx, platform)
end

if apollo_live_cl1 then
    si_cl1.enable(ctx, platform)
end
