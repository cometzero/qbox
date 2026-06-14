local si_cl0 = {}

function si_cl0.enable(ctx, platform)
    print("Apollo FVP live SI CL0 block enabled...")

    local SI_CL0_SRAM_BASE = 0x120000000
    local SI_CL0_SRAM_SIZE = 0x00800000
    local SI_CL0_ENTRY = 0x120000000
    local SI_CL0_RSE_SHARED_SRAM_BASE = 0x40000000
    local SI_CL0_RSE_SHARED_SRAM_SIZE = 0x00800000
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
    local SI_CL0_FMU_CRITICAL_IRQ = 128
    local SI_CL0_FMU_NON_CRITICAL_IRQ = 129
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
        ctx.APOLLO_SI_CL1_HIPC_SHARED_SIZE
    local SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_TAIL_SIZE =
        SI_CL0_ATW7_AP_PERIPHERAL_NS_SRAM_SIZE -
        ctx.APOLLO_SI_CL1_HIPC_SHARED_SIZE
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

    local si_cl0_image = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_IMAGE",
        ctx.apollo_root.."build/local-apollo-fvp/deploy/firmware/si0_ramfw.bin")
    local si_cl0_log = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_LOG",
        ctx.getenv_or(
            "QBOX_RDASPEN_SCP_LOG",
            ctx.apollo_root.."build/qbox-apollo-fvp/full-live-cl0-cl1/qbox-safety-island-cl0.log"))
    local si_cl0_uart_read_file = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL0_UART_READ_FILE",
        "/dev/null")
    local si_cl0_uart_poll_read = si_cl0_uart_read_file ~= "/dev/null"
    local si_cl0_qemu_args = ctx.getenv_or("QBOX_APOLLO_FULL_SI_CL0_QEMU_ARGS", "")
    local si_gic_trace = ctx.getenv_bool_or("QBOX_APOLLO_FULL_SI_GIC_MULTIVIEW_TRACE", false)
    local si_gic_trace_limit =
        ctx.getenv_number_or("QBOX_APOLLO_FULL_SI_GIC_MULTIVIEW_TRACE_LIMIT", "256")
    local si_cmn_trace = ctx.getenv_bool_or("QBOX_APOLLO_FULL_SI_CMN_TRACE", false)
    local si_cmn_trace_limit =
        ctx.getenv_number_or("QBOX_APOLLO_FULL_SI_CMN_TRACE_LIMIT", "512")

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
        accel = ctx.getenv_or("QBOX_APOLLO_FULL_SI_CL0_ACCEL", "tcg");
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
        moduletype = "zena_ssu";
        target_socket = {
            address = SI_CL0_SSU_BASE;
            size = SI_CL0_SSU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        log_level = 0;
    }

    platform.si_cl0_fmu = {
        moduletype = "zena_fmu";
        bank_count = 5;
        record_count = 384;
        target_socket = {
            address = SI_CL0_FMU_BASE;
            size = SI_CL0_FMU_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 0;
        };
        critical_irq = {bind = "&si_cl0_gic.spi_in_"..SI_CL0_FMU_CRITICAL_IRQ};
        non_critical_irq = {bind = "&si_cl0_gic.spi_in_"..SI_CL0_FMU_NON_CRITICAL_IRQ};
        critical_ssu = {bind = "&si_cl0_ssu.critical_in"};
        non_critical_ssu = {bind = "&si_cl0_ssu.non_critical_in"};
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
        trace_pc = ctx.getenv_bool_or("QBOX_APOLLO_FULL_SI_CL0_PC_TRACE", false);
        trace_exception_state = ctx.getenv_bool_or(
            "QBOX_APOLLO_FULL_SI_CL0_EXCEPTION_TRACE",
            false);
        trace_pc_file = ctx.getenv_or(
            "QBOX_APOLLO_FULL_SI_CL0_PC_TRACE_FILE",
            ctx.apollo_root.."build/qbox-apollo-fvp/full-live-cl0-cl1/si-cl0-pc-trace.log");
        trace_pc_interval = ctx.getenv_number_or(
            "QBOX_APOLLO_FULL_SI_CL0_PC_TRACE_INTERVAL",
            "1");
        trace_pc_limit = ctx.getenv_number_or(
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


return si_cl0
