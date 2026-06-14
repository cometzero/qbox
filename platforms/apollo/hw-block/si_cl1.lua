local si_cl1 = {}

function si_cl1.enable(ctx, platform)
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

    local si_cl1_image = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_IMAGE",
        ctx.apollo_root.."build/local-apollo-fvp/deploy/firmware/zephyr-demos-cl1.bin")
    local si_cl1_log = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_LOG",
        ctx.apollo_root.."build/qbox-apollo-fvp/full-live-cl1/qbox-safety-island-cl1.log")
    local si_cl1_uart_read_file = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_UART_READ_FILE",
        "/dev/null")
    local si_cl1_uart_poll_read = si_cl1_uart_read_file ~= "/dev/null"
    local si_cl1_qemu_args = ctx.getenv_or("QBOX_APOLLO_FULL_SI_CL1_QEMU_ARGS", "")
    local mhu_trace = ctx.getenv_bool_or("QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE", false)
    local mhu_trace_file = ctx.getenv_or(
        "QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE_FILE",
        ctx.apollo_root.."build/qbox-apollo-fvp/full-live-cl1/si-cl1-mhuv3-trace.log")
    local mhu_trace_limit =
        ctx.getenv_number_or("QBOX_APOLLO_FULL_SI_CL1_MHU_TRACE_LIMIT", "4096")
    -- The first live CL1 integration still uses the RD-Aspen host_router as a
    -- temporary merged bus. CL1 local addresses overlap broad AP regions in
    -- that flattened view, so lower only those broad AP windows and let the
    -- narrow CL1 targets win the overlapping slices.
    if platform.host_ap_flash ~= nil then
        ctx.lower_decode_priority(platform.host_ap_flash.target_socket, 10)
    end
    if platform.ap_gpex_0 ~= nil then
        ctx.lower_decode_priority(platform.ap_gpex_0.ecam_iface, 10)
    end
    if platform.host_ap_dram1 ~= nil then
        ctx.lower_decode_priority(platform.host_ap_dram1.target_socket, 10)
    end

    if platform.host_ap_bl2_header_sram ~= nil then
        local target = platform.host_ap_bl2_header_sram.target_socket
        target.aliases = target.aliases or {}
        target.aliases.si_cl1_hipc_local_view = {
            address = ctx.APOLLO_SI_CL1_HIPC_SHARED_BASE;
            size = ctx.APOLLO_SI_CL1_HIPC_SHARED_SIZE;
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
        accel = ctx.getenv_or("QBOX_APOLLO_FULL_SI_CL1_ACCEL", "tcg");
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
            start_powered_off = ctx.apollo_live_cl0;
            start_in_reset = ctx.apollo_live_cl0;
            reset_power_on = ctx.apollo_live_cl0;
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

    if ctx.apollo_live_cl0 and platform.host_rse_si_mhu_pbx ~= nil then
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


return si_cl1
