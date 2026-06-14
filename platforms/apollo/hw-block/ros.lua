local ros = {}

ros.peripherals = {
    system_registers = {base = 0x30000000, size = 0x10000, modeled = false};
    virtio_p9 = {base = 0x30010000, size = 0x10000, irq = 288, modeled = false};
    virtio_block = {
        {name = "ap_virtioblk_0", base = 0x30020000, size = 0x10000, irq = 289, modeled = true};
        {name = "ap_virtioblk_1", base = 0x30030000, size = 0x10000, irq = 290, modeled = true};
        {name = "ap_virtioblk_2", base = 0x30040000, size = 0x10000, irq = 291, modeled = true};
        {name = "ap_virtioblk_3", base = 0x30050000, size = 0x10000, irq = 292, modeled = true};
    };
    virtio_net = {name = "ap_virtionet_0", base = 0x30060000, size = 0x10000, irq = 293, modeled = true};
    virtio_rng = {name = "ap_virtiorng_0", base = 0x30080000, size = 0x10000, irq = 295, modeled = true};
    vsi = {
        {base = 0x30090000, size = 0x10000, irq = 296, modeled = false};
        {base = 0x300a0000, size = 0x10000, irq = 297, modeled = false};
    };
    rtc = {name = "ap_rtc_0", base = 0x300d0000, size = 0x10000, irq = 300, modeled = true};
    uart = {
        {base = 0x300e0000, size = 0x10000, irq = 301, modeled = false};
        {base = 0x300f0000, size = 0x10000, irq = 302, modeled = false};
    };
}

local function bind_target(target, bind_ap_target)
    if target ~= nil then
        bind_ap_target(target)
    end
end

local function lower_target(target, lower_decode_priority, priority)
    if target ~= nil then
        lower_decode_priority(target, priority)
    end
end

function ros.bind_ap_view_targets(platform, bind_ap_target)
    for i=0,3 do
        local virtio = platform["ap_virtioblk_"..i]
        if virtio ~= nil then
            bind_target(virtio.mem, bind_ap_target)
        end
    end

    if platform.ap_virtionet_0 ~= nil then
        bind_target(platform.ap_virtionet_0.mem, bind_ap_target)
    end
    if platform.ap_virtiorng_0 ~= nil then
        bind_target(platform.ap_virtiorng_0.mem, bind_ap_target)
    end
    if platform.ap_rtc_0 ~= nil then
        bind_target(platform.ap_rtc_0.mem, bind_ap_target)
    end
end

function ros.lower_decode_priorities(platform, lower_decode_priority, priority)
    for i=0,3 do
        local virtio = platform["ap_virtioblk_"..i]
        if virtio ~= nil then
            lower_target(virtio.mem, lower_decode_priority, priority)
        end
    end

    if platform.ap_virtionet_0 ~= nil then
        lower_target(platform.ap_virtionet_0.mem, lower_decode_priority, priority)
    end
    if platform.ap_virtiorng_0 ~= nil then
        lower_target(platform.ap_virtiorng_0.mem, lower_decode_priority, priority)
    end
    if platform.ap_rtc_0 ~= nil then
        lower_target(platform.ap_rtc_0.mem, lower_decode_priority, priority)
    end
end

return ros
