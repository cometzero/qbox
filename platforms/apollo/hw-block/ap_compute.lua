local ap_compute = {}

function ap_compute.enable_ap_view_router(ctx, platform)
    if platform.ap_cpu_0 == nil then
        return
    end

    print("Apollo FVP AP logical view router enabled...")

    platform.ap_view_router = {
        moduletype = "router";
        log_level = 0;
    }

    platform.ap_view_passthrough = {
        moduletype = "addrtr";
        mapped_base_addr = 0x0;
        target_socket = {
            address = 0x0;
            size = 0x1000000000000;
            bind = "&ap_view_router.initiator_socket";
            relative_addresses = false;
            priority = 100;
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    }

    local function bind_ap_target(target)
        if target ~= nil then
            target.bind = "&ap_view_router.initiator_socket"
            target.priority = 0
        end
    end

    local function bind_ap_socket(device, socket_name)
        if device ~= nil then
            bind_ap_target(device[socket_name])
        end
    end

    if platform.host_ap_atu ~= nil and
       platform.host_ap_atu.translation_socket ~= nil then
        platform.host_ap_atu.translation_socket.bind =
            "&ap_view_router.initiator_socket"
        platform.host_ap_atu.translation_socket.priority = 10
    end

    if platform.ap_global_peripheral_initiator ~= nil then
        platform.ap_global_peripheral_initiator.global_initiator = {
            bind = "&ap_view_router.target_socket";
        }
    end
    if platform.ap_gpex_0 ~= nil then
        platform.ap_gpex_0.bus_master = {
            bind = "&ap_view_router.target_socket";
        }
        bind_ap_target(platform.ap_gpex_0.pio_iface)
        bind_ap_target(platform.ap_gpex_0.mmio_iface)
        bind_ap_target(platform.ap_gpex_0.ecam_iface)
        bind_ap_target(platform.ap_gpex_0.mmio_iface_high)
    end
    bind_ap_socket(platform.host_ap_dram1, "target_socket")
    bind_ap_socket(platform.host_ap_dram2, "target_socket")
    bind_ap_socket(platform.host_ap_ffa_mm_comm_buffer, "target_socket")
    bind_ap_socket(platform.host_ap_spmc_sdram, "target_socket")
    bind_ap_socket(platform.ap_gic, "dist_iface")
    if platform.ap_gic ~= nil then
        for i=0,15 do
            bind_ap_socket(platform.ap_gic, "redist_iface_"..i)
        end
    end
    bind_ap_socket(platform.ap_gic_its, "mem")
    bind_ap_socket(platform.ap_smmu_0, "mem")
    ctx.ros.bind_ap_view_targets(platform, bind_ap_target)
    bind_ap_socket(platform.ap_watchdog_0, "refresh_mem")
    bind_ap_socket(platform.ap_watchdog_0, "control_mem")
    bind_ap_socket(platform.ap_secure_uart, "target_socket")
    bind_ap_socket(platform.ap_primary_uart, "target_socket")
    bind_ap_socket(platform.ap_timer_mem, "mem")
    bind_ap_socket(platform.ap_timer_mem, "mem_view")
    bind_ap_socket(platform.ap_secure_wdog, "target_socket")
    for i=0,15 do
        local cpu = platform["ap_cpu_"..i]
        if cpu ~= nil then
            cpu.mem = {bind = "&ap_view_router.target_socket"}
        end
    end
end


return ap_compute
