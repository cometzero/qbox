-- Apollo FVP full-system entrypoint.
--
-- This file intentionally reuses the existing RD-Aspen RSE-first topology as
-- the service-model baseline. The Apollo full runner supplies Apollo local
-- build artifacts through QBOX_RDASPEN_* environment variables until the
-- Apollo-specific live SI CL0/CL1 wiring replaces the service model.

print("Apollo FVP full-system QBox config running...")

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

local apollo_root = apollo_top().."../../../../"
local apollo_si_mode = getenv_or("QBOX_APOLLO_FULL_SI_MODE", "service-model")
local apollo_live_cl1 =
    getenv_bool_or("QBOX_APOLLO_FULL_LIVE_CL1", false) or
    apollo_si_mode == "live-cl1" or apollo_si_mode == "live-cl0-cl1"
local apollo_live_cl0 =
    getenv_bool_or("QBOX_APOLLO_FULL_LIVE_CL0", false) or
    apollo_si_mode == "live-cl0-cl1"
local APOLLO_SI_CL1_HIPC_SHARED_BASE = 0xe0130000
local APOLLO_SI_CL1_HIPC_SHARED_SIZE = 0x00080000

dofile(apollo_top().."../fvp-rd-aspen-rse/conf.lua")

local function enable_apollo_ap_view_router()
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
    for i=0,3 do
        local virtio = platform["ap_virtioblk_"..i]
        if virtio ~= nil then
            bind_ap_target(virtio.mem)
        end
    end
    if platform.ap_virtionet_0 ~= nil then
        bind_ap_target(platform.ap_virtionet_0.mem)
    end
    if platform.ap_virtiorng_0 ~= nil then
        bind_ap_target(platform.ap_virtiorng_0.mem)
    end
    bind_ap_socket(platform.ap_rtc_0, "mem")
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

local function enable_apollo_live_cl0()
    print("Apollo FVP live SI CL0 block enabled...")

    local SI_CL0_SRAM_BASE = 0x120000000
    local SI_CL0_SRAM_SIZE = 0x00800000
    local SI_CL0_ENTRY = 0x120000000
    local SI_CL0_RSE_SHARED_SRAM_BASE = 0x40000000
    local SI_CL0_RSE_SHARED_SRAM_SIZE = 0x00800000
    local AP_RSE_SECURE_MHU_PBX_LOGICAL_BASE = 0x40680000
    local AP_RSE_SECURE_MHU_MBX_LOGICAL_BASE = 0x406B0000
    local AP_LOGICAL_MHU_FRAME_SIZE = 0x00030000
    local SI_CL0_GICD_VIEW0_BASE = 0x30000000
    local SI_CL0_GICR_VIEW0_BASES = {
        0x30040000;
        0x30060000;
        0x30080000;
        0x300a0000;
        0x300c0000;
    }
    local SI_CL0_GICD_VIEW1_BASE = 0x30100000
    local SI_CL0_GICR_VIEW1_BASE = 0x30140000
    local SI_CL0_GICR_SIZE = 0x00020000
    local SI_CL1_CLUSTER_UTILITY_BUS_BASE = 0x28800000
    local SI_CL1_CLUSTER_PPU_BASE = SI_CL1_CLUSTER_UTILITY_BUS_BASE + 0x00010000
    local SI_CL1_PPU_AE_BASE = SI_CL1_CLUSTER_UTILITY_BUS_BASE + 0x00080000
    local SI_CL1_CORE_PPU0_BASE = SI_CL1_CLUSTER_UTILITY_BUS_BASE + 0x00040000
    local SI_CL1_CORE_PPU_STRIDE = 0x00100000
    local SI_CL1_CORE_PPU_COUNT = 4
    local SI_CL_PPU_SIZE = 0x00001000
    local SI_CL0_UART_BASE = 0x2a400000
    local SI_CL0_UART_IRQ = 40
    local SI_CL0_SCR_BASE = 0x2a6b0000
    local SI_CL0_SCR_SIZE = 0x00010000
    local SI_CL0_TIMER_CNTCTL_BASE = 0x2a6f0000
    local SI_CL0_TIMER_CNTCTL_SIZE = 0x00010000
    local SI_CL0_TIMER_CNT_BASE = 0x2a720000
    local SI_CL0_TIMER_CNT_SIZE = 0x00010000
    local SI_CL0_SSU_BASE = 0x2a500000
    local SI_CL0_SSU_SIZE = 0x00001000
    local SI_CL0_FMU_BASE = 0x2a510000
    local SI_CL0_FMU_SIZE = 0x00050000
    local SI_CL0_NI710AE_PRIMARY_NCI_BASE = 0x2a000000
    local SI_CL0_NI710AE_SECONDARY_NCI_BASE = 0x2a200000
    local SI_CL0_NI710AE_MHU_NCI_BASE = 0x2a300000
    local SI_CL0_NI710AE_NCI_SIZE = 0x00010000
    local SI_CL0_ATW0_CMN_BASE = 0x80000000
    local SI_CL0_ATW0_CMN_SIZE = 0x40000000
    local SI_CL0_ATW1_CLUSTER_UTILITY_BASE = 0xc0000000
    local SI_CL0_CLUSTER_UTILITY_STRIDE = 0x04000000
    local SI_CL0_AP_CLUSTER_COUNT = 4
    local SI_CL0_AP_CORE_PER_CLUSTER_COUNT = 4
    local SI_CL0_AP_CLUSTER_PPU_OFFSET = 0x01030000
    local SI_CL0_AP_CLUSTER_AE_OFFSET = 0x01050000
    local SI_CL0_AP_CORE_PPU0_OFFSET = 0x01080000
    local SI_CL0_AP_CORE_PPU_STRIDE = 0x00100000
    local SI_CL0_AP_CLUSTER_CONTROL_OFFSET = 0x02000000
    local SI_CL0_AP_CLUSTER_CONTROL_SIZE = 0x00010000
    local SI_CL0_ATW2_SMD_EXPANSION_BASE = 0xd0000000
    local SI_CL0_ATW2_SMD_EXPANSION_SIZE = 0x00020000
    local SI_CL0_PLL_BASE = SI_CL0_ATW2_SMD_EXPANSION_BASE
    local SI_CL0_PLL_SIZE = 0x00001000
    local SI_CL0_ATW3_SYSTOP_PIK_BASE = 0xd0020000
    local SI_CL0_ATW3_SYSTOP_PIK_SIZE = 0x00010000
    local SI_CL0_SYS0_PPU_BASE = 0xd0021000
    local SI_CL0_SYS0_PPU_SIZE = 0x00001000
    local SI_CL0_ATW4_SYSTEM_ID_BASE = 0xd0030000
    local SI_CL0_ATW4_SYSTEM_ID_SIZE = 0x00010000
    local SI_CL0_ATW5_CSS_COUNTERS_TIMERS_BASE = 0xd0040000
    local SI_CL0_ATW5_CSS_COUNTERS_TIMERS_SIZE = 0x00030000
    local SI_CL0_REFCLK_CNTCONTROL_BASE = SI_CL0_ATW5_CSS_COUNTERS_TIMERS_BASE
    local SI_CL0_REFCLK_CNTCONTROL_SIZE = 0x00010000
    local SI_CL0_ATW6_AP_GIC_BASE = 0xd0770000
    local SI_CL0_ATW6_AP_GICD_MULTIVIEW_SIZE = 0x00010000
    local SI_CL0_ATW6_AP_GICR_BASE = SI_CL0_ATW6_AP_GIC_BASE + 0x00080000
    local SI_CL0_ATW6_AP_GICR_STRIDE = 0x00040000
    local SI_CL0_ATW6_AP_GICR_SIZE = 0x00020000
    local SI_CL0_ATW6_AP_GICR_COUNT = 16
    local SI_CL0_CLUSTER_UTILITY_MGI0_BASE = 0xc0200000
    local SI_CL0_CLUSTER_UTILITY_MGI_STRIDE = 0x04000000
    local SI_CL0_CLUSTER_UTILITY_MGI_SIZE = 0x00010000
    local SI_CL0_ATW16_SMCF_SMD_MGI_BASE = 0xe0230000
    local SI_CL0_ATW16_SMCF_SMD_MGI_SIZE = 0x00010000
    local SI_CL0_ATW6_AP_PERIPHERAL_SRAM_BASE = 0xe0030000
    local SI_CL0_ATW6_AP_PERIPHERAL_SRAM_SIZE = 0x00100000
    local SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_BASE = 0xe0130000
    local SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_SIZE = 0x00100000
    local SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_TAIL_BASE =
        SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_BASE +
        APOLLO_SI_CL1_HIPC_SHARED_SIZE
    local SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_TAIL_SIZE =
        SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_SIZE -
        APOLLO_SI_CL1_HIPC_SHARED_SIZE
    local SI_CL0_ATW17_SMD_SRAM_BASE = 0xe0240000
    local SI_CL0_ATW17_SMD_SRAM_SIZE = 0x00100000
    local SI_CL0_ATW18_SMCF_SMDEXP_SRAM_BASE = 0xe0340000
    local SI_CL0_ATW18_SMCF_SMDEXP_SRAM_SIZE = 0x00002000
    local SI_CL0_ATU_CHECK_WINDOWS = {
        { name = "cmn"; base = 0x80000000; size = 0x00010000 };
        { name = "cluster_utility"; base = 0xc1000000; size = 0x00800000 };
        { name = "smd_expansion"; base = 0xd0000000; size = 0x00020000 };
        { name = "systop_pik"; base = 0xd0020000; size = 0x00002000 };
        { name = "system_id"; base = 0xd0030000; size = 0x00010000 };
        { name = "css_counters_timers"; base = 0xd0040000; size = 0x00030000 };
        { name = "ni710ae_cluster0_fmu"; base = 0xd0070000; size = 0x00010000 };
        { name = "ni710ae_cluster1_fmu"; base = 0xd0170000; size = 0x00010000 };
        { name = "ni710ae_cluster2_fmu"; base = 0xd0270000; size = 0x00010000 };
        { name = "ni710ae_cluster3_fmu"; base = 0xd0370000; size = 0x00010000 };
        { name = "ni710ae_sys_ctrl"; base = 0xd0470000; size = 0x00010000 };
        { name = "ni710ae_smd"; base = 0xd0670000; size = 0x00010000 };
        { name = "ap_gic"; base = 0xd0770000; size = 0x00080000 };
        { name = "shared_sram"; base = 0xe0030000; size = 0x00100000 };
        { name = "shared_sram_ns"; base = 0xe0130000; size = 0x00100000 };
        { name = "smd_smcf_mgi"; base = 0xe0230000; size = 0x00010000 };
        { name = "smd_sram"; base = 0xe0240000; size = 0x00100000 };
    }
    local ARCH_TIMER_SEC_PPI = 16 + 13
    local ARCH_TIMER_PHYS_PPI = 16 + 4
    local ARCH_TIMER_VIRT_PPI = 16 + 11
    local ARCH_TIMER_HYP_PPI = 16 + 3

    local si_cl0_image = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_IMAGE",
        apollo_root.."build/local-apollo-fvp/deploy/firmware/si0_ramfw.bin")
    local si_cl0_log = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_LOG",
        getenv_or(
            "QBOX_RDASPEN_SCP_LOG",
            apollo_root.."build/qbox-apollo-fvp/full-live-cl0-cl1/qbox-safety-island-cl0.log"))
    local si_cl0_uart_read_file = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_UART_READ_FILE",
        "/dev/null")
    local si_cl0_uart_poll_read = si_cl0_uart_read_file ~= "/dev/null"
    local si_cl0_qemu_args = getenv_or("QBOX_APOLLO_FULL_SI_CL0_QEMU_ARGS", "")
    local si_gic_trace = getenv_bool_or("QBOX_APOLLO_FULL_SI_GIC_MULTIVIEW_TRACE", false)
    local si_gic_trace_limit =
        getenv_number_or("QBOX_APOLLO_FULL_SI_GIC_MULTIVIEW_TRACE_LIMIT", "256")
    local si_cmn_trace = getenv_bool_or("QBOX_APOLLO_FULL_SI_CMN_TRACE", false)
    local si_cmn_trace_limit =
        getenv_number_or("QBOX_APOLLO_FULL_SI_CMN_TRACE_LIMIT", "512")

    -- The flattened RSE/AP/SI bus still contains broad AP and host-side
    -- service-model ranges. Lower the broad windows so the CL0 local view wins
    -- for narrow local SRAM and device accesses.
    if platform.host_ap_flash ~= nil then
        lower_decode_priority(platform.host_ap_flash.target_socket, 10)
    end
    if platform.ap_gpex_0 ~= nil then
        lower_decode_priority(platform.ap_gpex_0.ecam_iface, 10)
    end
    if platform.host_ap_dram1 ~= nil then
        lower_decode_priority(platform.host_ap_dram1.target_socket, 10)
    end
    for index = 0,3 do
        local virtio = platform["ap_virtioblk_"..index]
        if virtio ~= nil and virtio.mem ~= nil then
            lower_decode_priority(virtio.mem, 10)
        end
    end
    if platform.ap_virtionet_0 ~= nil and platform.ap_virtionet_0.mem ~= nil then
        lower_decode_priority(platform.ap_virtionet_0.mem, 10)
    end
    if platform.ap_virtiorng_0 ~= nil and platform.ap_virtiorng_0.mem ~= nil then
        lower_decode_priority(platform.ap_virtiorng_0.mem, 10)
    end

    enable_apollo_ap_view_router()

    -- Live CL0 adds a broad local RSE-shared-SRAM window at 0x40000000.
    -- AP uses that same address range as an AP ATU aperture. The AP-view
    -- router above keeps source-specific AP accesses on the AP ATU path
    -- instead of adding AP-SI aliases directly to the shared host router.
    if platform.host_ap_rse_mhu_pbx ~= nil then
        local target = platform.host_ap_rse_mhu_pbx.target_socket
        target.priority = 0
        target.aliases = target.aliases or {}
        target.aliases.ap_logical_pbx = {
            address = AP_RSE_SECURE_MHU_PBX_LOGICAL_BASE;
            size = AP_LOGICAL_MHU_FRAME_SIZE;
        }
    end
    if platform.host_ap_rse_mhu_mbx ~= nil then
        local target = platform.host_ap_rse_mhu_mbx.target_socket
        target.priority = 0
        target.aliases = target.aliases or {}
        target.aliases.ap_logical_mbx = {
            address = AP_RSE_SECURE_MHU_MBX_LOGICAL_BASE;
            size = AP_LOGICAL_MHU_FRAME_SIZE;
        }
    end

    platform.si_cl0_cmn_cyprus = {
        moduletype = "host_cmn_cyprus";
        trace = si_cmn_trace;
        trace_limit = si_cmn_trace_limit;
        target_socket = {
            address = SI_CL0_ATW0_CMN_BASE;
            size = SI_CL0_ATW0_CMN_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_gic_multiview = {
        moduletype = "gicx00_multiview";
        trace = si_gic_trace;
        trace_limit = si_gic_trace_limit;
        view0_dist = {
            address = SI_CL0_GICD_VIEW0_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
    }

    for i=0,4 do
        platform.si_gic_multiview["view0_redist_"..i] = {
            address = SI_CL0_GICR_VIEW0_BASES[i + 1];
            size = SI_CL0_GICR_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        }
    end

    platform.ap_gic_multiview = {
        moduletype = "gicx00_multiview";
        trace = si_gic_trace;
        trace_limit = si_gic_trace_limit;
        view0_dist = {
            address = SI_CL0_ATW6_AP_GIC_BASE;
            size = SI_CL0_ATW6_AP_GICD_MULTIVIEW_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
    }

    for i=0,(SI_CL0_ATW6_AP_GICR_COUNT - 1) do
        platform.ap_gic_multiview["view0_redist_"..i] = {
            address = SI_CL0_ATW6_AP_GICR_BASE +
                (i * SI_CL0_ATW6_AP_GICR_STRIDE);
            size = SI_CL0_ATW6_AP_GICR_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        }
    end

    platform.si_cl0_qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    }

    platform.si_cl0_qemu_inst = {
        moduletype = "QemuInstance";
        args = {"&platform.si_cl0_qemu_inst_mgr", "AARCH64"};
        accel = getenv_or("QBOX_APOLLO_FULL_SI_CL0_ACCEL", "tcg");
        tcg_mode = "MULTI";
        sync_policy = "multithread-unconstrained";
        qemu_args = si_cl0_qemu_args;
    }

    platform.si_cl0_sram = {
        moduletype = "gs_memory";
        dmi = true;
        target_socket = {
            address = SI_CL0_SRAM_BASE;
            size = SI_CL0_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    }

    for _, window in ipairs(SI_CL0_ATU_CHECK_WINDOWS) do
        platform["si_cl0_atu_check_"..window.name] = {
            moduletype = "gs_memory";
            dmi = false;
            target_socket = {
                address = window.base;
                size = window.size;
                bind = "&host_router.initiator_socket";
                priority = 20;
            };
            init_mem = true;
            log_level = 0;
        }
    end

    platform.si_cl0_rse_shared_sram = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_RSE_SHARED_SRAM_BASE;
            size = SI_CL0_RSE_SHARED_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
            -- Keep this broad merged-view window below narrow AP logical
            -- slices such as AP-RSE MHU at 0x40680000.
            priority = 5;
        };
        init_mem = true;
        log_level = 0;
    }

    platform.si_cl0_scr = {
        moduletype = "host_scr";
        cl1_present = true;
        cl0_config_0 = 0x01201717;
        cl0_config_1 = 0x01100002;
        cl0_config_2 = 0x00000034;
        cl0_c0_config_0 = 0x01000001;
        cl0_c0_config_1 = 0x01001000;
        cl0_c0_config_2 = 0x01200000;
        cl0_c0_config_3 = 0x00000000;
        target_socket = {
            address = SI_CL0_SCR_BASE;
            size = SI_CL0_SCR_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_timer_cntctl = {
        moduletype = "host_gtimer";
        target_socket = {
            address = SI_CL0_TIMER_CNTCTL_BASE;
            size = SI_CL0_TIMER_CNTCTL_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_timer_cntbase = {
        moduletype = "host_gtimer";
        counter_base = true;
        frequency = 125000000;
        counter_increment = 4096;
        target_socket = {
            address = SI_CL0_TIMER_CNT_BASE;
            size = SI_CL0_TIMER_CNT_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_ssu = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_SSU_BASE;
            size = SI_CL0_SSU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        init_mem = true;
        log_level = 0;
    }

    platform.si_cl0_fmu = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_FMU_BASE;
            size = SI_CL0_FMU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        init_mem = true;
        log_level = 0;
    }

    platform.si_cl0_ni710ae_primary_nci = {
        moduletype = "host_ni710ae_nci";
        topology = 4;
        target_socket = {
            address = SI_CL0_NI710AE_PRIMARY_NCI_BASE;
            size = SI_CL0_NI710AE_NCI_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_ni710ae_secondary_nci = {
        moduletype = "host_ni710ae_nci";
        topology = 2;
        target_socket = {
            address = SI_CL0_NI710AE_SECONDARY_NCI_BASE;
            size = SI_CL0_NI710AE_NCI_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_ni710ae_mhu_nci = {
        moduletype = "host_ni710ae_nci";
        topology = 1;
        target_socket = {
            address = SI_CL0_NI710AE_MHU_NCI_BASE;
            size = SI_CL0_NI710AE_NCI_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_smd_expansion_window = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW2_SMD_EXPANSION_BASE;
            size = SI_CL0_ATW2_SMD_EXPANSION_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    }

    platform.si_cl0_pll = {
        moduletype = "host_system_pll";
        lock_mask = 0x00000001;
        target_socket = {
            address = SI_CL0_PLL_BASE;
            size = SI_CL0_PLL_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_css_counters_timers_window = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW5_CSS_COUNTERS_TIMERS_BASE;
            size = SI_CL0_ATW5_CSS_COUNTERS_TIMERS_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    }

    platform.si_cl0_refclk_cntcontrol = {
        moduletype = "host_gtimer";
        target_socket = {
            address = SI_CL0_REFCLK_CNTCONTROL_BASE;
            size = SI_CL0_REFCLK_CNTCONTROL_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_smcf_smd_mgi = {
        moduletype = "host_smcf_mgi";
        group_id = 0x00000000;
        monitor_count = 1;
        data_values_per_monitor = 12;
        data_width_bits = 32;
        target_socket = {
            address = SI_CL0_ATW16_SMCF_SMD_MGI_BASE;
            size = SI_CL0_ATW16_SMCF_SMD_MGI_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_ap_peripheral_secure_sram = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW6_AP_PERIPHERAL_SRAM_BASE;
            size = SI_CL0_ATW6_AP_PERIPHERAL_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_ap_peripheral_ns_sram = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_TAIL_BASE;
            size = SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_TAIL_SIZE;
            bind = "&host_router.initiator_socket";
            -- The front 512 KiB is the CL1/AP HIPC resource-table, vring,
            -- and buffer window. Keep this catch-all model on the tail only
            -- so CL1 and AP share the same backing target without overlap.
            priority = 5;
        };
        log_level = 0;
    }

    for i=0,3 do
        platform["si_cl0_smcf_ap_cluster_mgi_"..i] = {
            moduletype = "host_smcf_mgi";
            group_id = i + 1;
            monitor_count = 1;
            data_values_per_monitor = 12;
            data_width_bits = 32;
            target_socket = {
                address = SI_CL0_CLUSTER_UTILITY_MGI0_BASE +
                    (i * SI_CL0_CLUSTER_UTILITY_MGI_STRIDE);
                size = SI_CL0_CLUSTER_UTILITY_MGI_SIZE;
                bind = "&host_router.initiator_socket";
                priority = 0;
            };
            log_level = 0;
        }
    end

    platform.si_cl0_smd_shared_sram = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW17_SMD_SRAM_BASE;
            size = SI_CL0_ATW17_SMD_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_smd_exp_mgi_sram = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW18_SMCF_SMDEXP_SRAM_BASE;
            size = SI_CL0_ATW18_SMCF_SMDEXP_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_systop_pik_window = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL0_ATW3_SYSTOP_PIK_BASE;
            size = SI_CL0_ATW3_SYSTOP_PIK_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    }

    platform.si_cl0_system_id = {
        moduletype = "host_scr";
        system_id = 0x0000073c;
        soc_id = 0x0000073c;
        pidr4 = 0x00000004;
        pidr0 = 0x0000003c;
        pidr1 = 0x000000b7;
        pidr2 = 0x0000000b;
        pidr3 = 0x00000000;
        cidr0 = 0x0000000d;
        cidr1 = 0x000000f0;
        cidr2 = 0x00000005;
        cidr3 = 0x000000b1;
        target_socket = {
            address = SI_CL0_ATW4_SYSTEM_ID_BASE;
            size = SI_CL0_ATW4_SYSTEM_ID_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_sys0_ppu = {
        moduletype = "host_ppu";
        target_socket = {
            address = SI_CL0_SYS0_PPU_BASE;
            size = SI_CL0_SYS0_PPU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl1_cluster_ppu = {
        moduletype = "host_ppu";
        initial_power_status = 0x8;
        target_socket = {
            address = SI_CL1_CLUSTER_PPU_BASE;
            size = SI_CL_PPU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    for i=0,(SI_CL1_CORE_PPU_COUNT - 1) do
        platform["si_cl1_core"..i.."_ppu"] = {
            moduletype = "host_ppu";
            initial_power_status = 0x8;
            target_socket = {
                address = SI_CL1_CORE_PPU0_BASE +
                    (i * SI_CL1_CORE_PPU_STRIDE);
                size = SI_CL_PPU_SIZE;
                bind = "&host_router.initiator_socket";
                priority = 0;
            };
            log_level = 0;
        }
    end

    platform.si_cl1_ppu_ae = {
        moduletype = "gs_memory";
        dmi = false;
        target_socket = {
            address = SI_CL1_PPU_AE_BASE;
            size = SI_CL_PPU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        init_mem = true;
        log_level = 0;
    }

    for cluster=0,(SI_CL0_AP_CLUSTER_COUNT - 1) do
        local cluster_base = SI_CL0_ATW1_CLUSTER_UTILITY_BASE +
            (cluster * SI_CL0_CLUSTER_UTILITY_STRIDE)
        platform["si_cl0_ap_cluster"..cluster.."_ppu"] = {
            moduletype = "host_ppu";
            initial_power_status = 0x8;
            target_socket = {
                address = cluster_base + SI_CL0_AP_CLUSTER_PPU_OFFSET;
                size = SI_CL_PPU_SIZE;
                bind = "&host_router.initiator_socket";
                priority = 0;
            };
            log_level = 0;
        }
        platform["si_cl0_ap_cluster"..cluster.."_ae"] = {
            moduletype = "gs_memory";
            dmi = false;
            target_socket = {
                address = cluster_base + SI_CL0_AP_CLUSTER_AE_OFFSET;
                size = SI_CL_PPU_SIZE;
                bind = "&host_router.initiator_socket";
                priority = 0;
            };
            init_mem = true;
            log_level = 0;
        }

        platform["si_cl0_ap_cluster"..cluster.."_control"] = {
            moduletype = "gs_memory";
            dmi = false;
            target_socket = {
                address = cluster_base + SI_CL0_AP_CLUSTER_CONTROL_OFFSET;
                size = SI_CL0_AP_CLUSTER_CONTROL_SIZE;
                bind = "&host_router.initiator_socket";
                priority = 0;
            };
            init_mem = true;
            log_level = 0;
        }

        for core=0,(SI_CL0_AP_CORE_PER_CLUSTER_COUNT - 1) do
            platform["si_cl0_ap_cluster"..cluster.."_core"..core.."_ppu"] = {
                moduletype = "host_ppu";
                initial_power_status = 0x8;
                target_socket = {
                    address = cluster_base + SI_CL0_AP_CORE_PPU0_OFFSET +
                        (core * SI_CL0_AP_CORE_PPU_STRIDE);
                    size = SI_CL_PPU_SIZE;
                    bind = "&host_router.initiator_socket";
                    priority = 0;
                };
                log_level = 0;
            }
        end
    end

    platform.si_cl0_gic = {
        moduletype = "arm_gicv3";
        args = {"&platform.si_cl0_qemu_inst"};
        dist_iface = {
            address = SI_CL0_GICD_VIEW1_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        redist_region = {1};
        redist_iface_0 = {
            address = SI_CL0_GICR_VIEW1_BASE;
            size = SI_CL0_GICR_SIZE;
            bind = "&host_router.initiator_socket";
        };
        num_cpus = 1;
        num_spi = 384;
    }

    platform.si_cl0_console_file = {
        moduletype = "char_backend_file";
        read_file = si_cl0_uart_read_file;
        write_file = si_cl0_log;
        poll_read = si_cl0_uart_poll_read;
        poll_interval_ms = 100;
        baudrate = 0;
    }

    platform.si_cl0_uart = {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = SI_CL0_UART_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq = {bind = "&si_cl0_gic.spi_in_"..SI_CL0_UART_IRQ};
        backend_socket = {bind = "&si_cl0_console_file.biflow_socket"};
    }

    platform.si_cl0_loader = {
        moduletype = "loader";
        initiator_socket = {bind = "&host_router.target_socket"};
        { bin_file = si_cl0_image, address = SI_CL0_SRAM_BASE };
    }

    platform.si_cl0_cpu_0 = {
        moduletype = "cpu_arm_cortexR82";
        args = {"&platform.si_cl0_qemu_inst"};
        mem = {bind = "&host_router.target_socket"};
        has_el2 = true;
        psci_conduit = "smc";
        start_powered_off = false;
        rvbar = SI_CL0_ENTRY;
        mp_affinity = 0x0;
        trace_pc = getenv_bool_or("QBOX_APOLLO_FULL_SI_CL0_PC_TRACE", false);
        trace_exception_state = getenv_bool_or(
            "QBOX_APOLLO_FULL_SI_CL0_EXCEPTION_TRACE",
            false);
        trace_pc_file = getenv_or(
            "QBOX_APOLLO_FULL_SI_CL0_PC_TRACE_FILE",
            apollo_root.."build/qbox-apollo-fvp/full-live-cl0-cl1/si-cl0-pc-trace.log");
        trace_pc_interval = getenv_number_or(
            "QBOX_APOLLO_FULL_SI_CL0_PC_TRACE_INTERVAL",
            "1");
        trace_pc_limit = getenv_number_or(
            "QBOX_APOLLO_FULL_SI_CL0_PC_TRACE_LIMIT",
            "4096");
        irq_timer_sec_out = {
            bind = "&si_cl0_gic.ppi_in_cpu_0_"..ARCH_TIMER_SEC_PPI;
        };
        irq_timer_phys_out = {
            bind = "&si_cl0_gic.ppi_in_cpu_0_"..ARCH_TIMER_PHYS_PPI;
        };
        irq_timer_virt_out = {
            bind = "&si_cl0_gic.ppi_in_cpu_0_"..ARCH_TIMER_VIRT_PPI;
        };
        irq_timer_hyp_out = {
            bind = "&si_cl0_gic.ppi_in_cpu_0_"..ARCH_TIMER_HYP_PPI;
        };
    }

    platform.si_cl0_gic.irq_out_0 = {bind = "&si_cl0_cpu_0.irq_in"}
    platform.si_cl0_gic.fiq_out_0 = {bind = "&si_cl0_cpu_0.fiq_in"}
    platform.si_cl0_gic.virq_out_0 = {bind = "&si_cl0_cpu_0.virq_in"}
    platform.si_cl0_gic.vfiq_out_0 = {bind = "&si_cl0_cpu_0.vfiq_in"}

    print("si-cl0 image: "..si_cl0_image)
    print("si-cl0 log:   "..si_cl0_log)
    print("si-cl0 entry: 0x"..string.format("%x", SI_CL0_ENTRY))
end

local function enable_apollo_live_cl1()
    print("Apollo FVP live SI CL1 block enabled...")

    local SI_CL1_CPU_COUNT = 4
    local SI_CL1_SRAM_BASE = 0x140000000
    local SI_CL1_SRAM_SIZE = 0x00800000
    local SI_CL1_ENTRY = 0x14000647c
    local SI_CL1_GICD_BASE = 0x30200000
    local SI_CL1_GICR0_BASE = 0x30260000
    local SI_CL1_GICR_STRIDE = 0x00020000
    local SI_CL1_GICR_SIZE = 0x00020000
    local SI_CL1_UART_BASE = 0x2a410000
    local SI_CL1_UART_IRQ = 7
    local SI_CL1_SCMI_SHMEM_BASE = 0x48000000
    local SI_CL1_SCMI_SHMEM_SIZE = 0x00001000
    local SI_CL1_HIPC_PBX_BASE = 0x39000000
    local SI_CL1_HIPC_MBX_BASE = 0x39040000
    local SI_CL1_PFDI_PBX_BASE = 0x39200000
    local SI_CL1_HIPC_MHU_SIZE = 0x00030000
    local SI_CL1_PFDI_MHU_SIZE = 0x00020000
    local SI_CL1_MHU_CHANNELS = 32
    local SI_CL1_SCMI_MSG_SIZE_PER_CORE = 40
    local SI_CL1_PFDI_MHU_CHANNEL_BASE = 2
    local ARCH_TIMER_SEC_PPI = 16 + 13
    local ARCH_TIMER_PHYS_PPI = 16 + 4
    local ARCH_TIMER_VIRT_PPI = 16 + 11
    local ARCH_TIMER_HYP_PPI = 16 + 3

    local si_cl1_image = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_IMAGE",
        apollo_root.."build/local-apollo-fvp/deploy/firmware/zephyr-demos-cl1.bin")
    local si_cl1_log = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_LOG",
        apollo_root.."build/qbox-apollo-fvp/full-live-cl1/qbox-safety-island-cl1.log")
    local si_cl1_uart_read_file = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_UART_READ_FILE",
        "/dev/null")
    local si_cl1_uart_poll_read = si_cl1_uart_read_file ~= "/dev/null"
    local si_cl1_qemu_args = getenv_or("QBOX_APOLLO_FULL_SI_CL1_QEMU_ARGS", "")
    local mhu_trace = getenv_bool_or("QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE", false)
    local mhu_trace_file = getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE_FILE",
        apollo_root.."build/qbox-apollo-fvp/full-live-cl1/si-cl1-mhuv3-trace.log")
    local mhu_trace_limit =
        getenv_number_or("QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE_LIMIT", "4096")
    -- The first live CL1 integration still uses the RD-Aspen host_router as a
    -- temporary merged bus. CL1 local addresses overlap broad AP regions in
    -- that flattened view, so lower only those broad AP windows and let the
    -- narrow CL1 targets win the overlapping slices.
    if platform.host_ap_flash ~= nil then
        lower_decode_priority(platform.host_ap_flash.target_socket, 10)
    end
    if platform.ap_gpex_0 ~= nil then
        lower_decode_priority(platform.ap_gpex_0.ecam_iface, 10)
    end
    if platform.host_ap_dram1 ~= nil then
        lower_decode_priority(platform.host_ap_dram1.target_socket, 10)
    end

    if platform.host_ap_bl2_header_sram ~= nil then
        local target = platform.host_ap_bl2_header_sram.target_socket
        target.aliases = target.aliases or {}
        target.aliases.si_cl1_hipc_local_view = {
            address = APOLLO_SI_CL1_HIPC_SHARED_BASE;
            size = APOLLO_SI_CL1_HIPC_SHARED_SIZE;
        }
    end

    if platform.host_ap_si_cl1_mhu_pbx ~= nil then
        platform.host_ap_si_cl1_mhu_pbx.pair = "apollo_ap_to_si_cl1"
        platform.host_ap_si_cl1_mhu_pbx.protocol = "doorbell-bridge"
        platform.host_ap_si_cl1_mhu_pbx.channel_count = SI_CL1_MHU_CHANNELS
        platform.host_ap_si_cl1_mhu_pbx.doorbell_ack_trigger_value = 0
        platform.host_ap_si_cl1_mhu_pbx.rpmsg_ns_enable = false
    end
    if platform.host_ap_si_cl1_mhu_mbx ~= nil then
        platform.host_ap_si_cl1_mhu_mbx.pair = "apollo_si_cl1_to_ap"
        platform.host_ap_si_cl1_mhu_mbx.protocol = "doorbell-bridge"
        platform.host_ap_si_cl1_mhu_mbx.channel_count = SI_CL1_MHU_CHANNELS
    end

    platform.si_cl1_qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    }

    platform.si_cl1_qemu_inst = {
        moduletype = "QemuInstance";
        args = {"&platform.si_cl1_qemu_inst_mgr", "AARCH64"};
        accel = getenv_or("QBOX_APOLLO_FULL_SI_CL1_ACCEL", "tcg");
        tcg_mode = "MULTI";
        sync_policy = "multithread-unconstrained";
        qemu_args = si_cl1_qemu_args;
    }

    platform.si_cl1_sram = {
        moduletype = "gs_memory";
        dmi = true;
        target_socket = {
            address = SI_CL1_SRAM_BASE;
            size = SI_CL1_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    }

    platform.si_cl1_scmi_shmem = {
        moduletype = "gs_memory";
        target_socket = {
            address = SI_CL1_SCMI_SHMEM_BASE;
            size = SI_CL1_SCMI_SHMEM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    }

    platform.si_cl1_gic = {
        moduletype = "arm_gicv3";
        args = {"&platform.si_cl1_qemu_inst"};
        dist_iface = {
            address = SI_CL1_GICD_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        redist_region = {1, 1, 1, 1};
        num_cpus = SI_CL1_CPU_COUNT;
        num_spi = 128;
    }

    platform.si_cl1_console_file = {
        moduletype = "char_backend_file";
        read_file = si_cl1_uart_read_file;
        write_file = si_cl1_log;
        poll_read = si_cl1_uart_poll_read;
        poll_interval_ms = 100;
        baudrate = 0;
    }

    platform.si_cl1_uart = {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = SI_CL1_UART_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq = {bind = "&si_cl1_gic.spi_in_"..SI_CL1_UART_IRQ};
        backend_socket = {bind = "&si_cl1_console_file.biflow_socket"};
    }

    platform.si_cl1_hipc_mhu_pbx = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "apollo_si_cl1_to_ap";
        protocol = "doorbell-bridge";
        channel_count = SI_CL1_MHU_CHANNELS;
        trace = mhu_trace;
        trace_file = mhu_trace_file;
        trace_limit = mhu_trace_limit;
        target_socket = {
            address = SI_CL1_HIPC_PBX_BASE;
            size = SI_CL1_HIPC_MHU_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_40"};
        log_level = 0;
    }

    platform.si_cl1_hipc_mhu_mbx = {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "apollo_ap_to_si_cl1";
        protocol = "doorbell-bridge";
        channel_count = SI_CL1_MHU_CHANNELS;
        trace = mhu_trace;
        trace_file = mhu_trace_file;
        trace_limit = mhu_trace_limit;
        target_socket = {
            address = SI_CL1_HIPC_MBX_BASE;
            size = SI_CL1_HIPC_MHU_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_41"};
        log_level = 0;
    }

    platform.si_cl1_pfdi_mhu_pbx = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "apollo_si_cl1_pfdi";
        protocol = "scmi";
        scmi_transport = "pfdi-monitor";
        channel_count = SI_CL1_MHU_CHANNELS;
        tx_shmem = SI_CL1_SCMI_SHMEM_BASE;
        rx_shmem = SI_CL1_SCMI_SHMEM_BASE;
        scmi_channel_stride = SI_CL1_SCMI_MSG_SIZE_PER_CORE;
        scmi_channel_base_index = SI_CL1_PFDI_MHU_CHANNEL_BASE;
        scmi_channel_count = 4;
        init_shmem = true;
        trace = mhu_trace;
        trace_file = mhu_trace_file;
        trace_limit = mhu_trace_limit;
        target_socket = {
            address = SI_CL1_PFDI_PBX_BASE;
            size = SI_CL1_PFDI_MHU_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_50"};
        log_level = 0;
    }

    platform.si_cl1_loader = {
        moduletype = "loader";
        initiator_socket = {bind = "&host_router.target_socket"};
        { bin_file = si_cl1_image, address = SI_CL1_SRAM_BASE };
    }

    for i=0,(SI_CL1_CPU_COUNT-1) do
        local cpu = {
            moduletype = "cpu_arm_cortexR82";
            args = {"&platform.si_cl1_qemu_inst"};
            mem = {bind = "&host_router.target_socket"};
            has_el2 = true;
            psci_conduit = "smc";
            start_powered_off = apollo_live_cl0;
            start_in_reset = apollo_live_cl0;
            reset_power_on = apollo_live_cl0;
            rvbar = SI_CL1_ENTRY;
            mp_affinity = 0x10000 + (i * 0x100);
            irq_timer_sec_out = {
                bind = "&si_cl1_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_SEC_PPI;
            };
            irq_timer_phys_out = {
                bind = "&si_cl1_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_PHYS_PPI;
            };
            irq_timer_virt_out = {
                bind = "&si_cl1_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_VIRT_PPI;
            };
            irq_timer_hyp_out = {
                bind = "&si_cl1_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_HYP_PPI;
            };
        }
        platform["si_cl1_cpu_"..tostring(i)] = cpu
        platform["si_cl1_gic"]["redist_iface_"..i] = {
            address = SI_CL1_GICR0_BASE + (i * SI_CL1_GICR_STRIDE);
            size = SI_CL1_GICR_SIZE;
            bind = "&host_router.initiator_socket";
        }
        platform["si_cl1_gic"]["irq_out_"..i] = {
            bind = "&si_cl1_cpu_"..i..".irq_in";
        }
        platform["si_cl1_gic"]["fiq_out_"..i] = {
            bind = "&si_cl1_cpu_"..i..".fiq_in";
        }
        platform["si_cl1_gic"]["virq_out_"..i] = {
            bind = "&si_cl1_cpu_"..i..".virq_in";
        }
        platform["si_cl1_gic"]["vfiq_out_"..i] = {
            bind = "&si_cl1_cpu_"..i..".vfiq_in";
        }
    end

    if apollo_live_cl0 and platform.host_rse_si_mhu_pbx ~= nil then
        local reset_targets = {"&ap_cpu_0.reset"}
        for i=0,(SI_CL1_CPU_COUNT-1) do
            reset_targets[#reset_targets + 1] =
                "&si_cl1_cpu_"..tostring(i)..".reset"
        end
        platform.apollo_si_cl1_reset_fanout = {
            moduletype = "reset_fanout";
            reset_out = {bind = table.concat(reset_targets, ";")};
            log_level = 0;
        }
        platform.host_rse_si_mhu_pbx.power_on_reset = {
            bind = "&apollo_si_cl1_reset_fanout.reset_in";
        }
    end

    print("si-cl1 image: "..si_cl1_image)
    print("si-cl1 log:   "..si_cl1_log)
    print("si-cl1 entry: 0x"..string.format("%x", SI_CL1_ENTRY))
end

if apollo_live_cl0 then
    enable_apollo_live_cl0()
end

if apollo_live_cl1 then
    enable_apollo_live_cl1()
end
