-- Apollo FVP Safety Island CL1 isolated Zephyr platform.
--
-- This is an isolated bring-up target for QAP-FULL-020. It models the CL1
-- local view closely enough to boot the local Zephyr binary on Cortex-R82 CPUs
-- and capture file-backed UART evidence before full AP/SI integration exists.

function top()
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

print("Apollo FVP Safety Island CL1 isolated QBox config running...")

local root = top().."../../../../"
local si_cl1_image = getenv_or(
    "QBOX_APOLLO_SI_CL1_IMAGE",
    root.."build/local-apollo-fvp/deploy/firmware/zephyr-demos-cl1.bin")
local si_cl1_log = getenv_or(
    "QBOX_APOLLO_SI_CL1_LOG",
    root.."build/qbox-apollo-fvp/si-cl1-isolated/qbox-safety-island-cl1.log")
local si_cl1_uart_read_file = getenv_or(
    "QBOX_APOLLO_SI_CL1_UART_READ_FILE",
    "/dev/null")
local si_cl1_uart_poll_read = si_cl1_uart_read_file ~= "/dev/null"
local accel = getenv_or("QBOX_APOLLO_SI_CL1_ACCEL", "tcg")
local qemu_args = getenv_or("QBOX_APOLLO_SI_CL1_QEMU_ARGS", "")
local mhu_trace = getenv_or("QBOX_APOLLO_SI_CL1_MHU_TRACE", "false") == "true"
local mhu_trace_file = getenv_or(
    "QBOX_APOLLO_SI_CL1_MHU_TRACE_FILE",
    root.."build/qbox-apollo-fvp/si-cl1-isolated/mhuv3-trace.log")
local mhu_trace_limit = getenv_number_or("QBOX_APOLLO_SI_CL1_MHU_TRACE_LIMIT", "4096")

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
local SI_CL1_SHARED_BASE = 0xe0130000
local SI_CL1_SHARED_SIZE = 0x00080000
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

platform = {
    moduletype = "Container";
    quantum_ns = 10000000;

    router = {
        moduletype = "router";
        log_level = 0;
    };

    keep_alive_0 = {
        moduletype = "keep_alive";
    };

    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    };

    qemu_inst = {
        moduletype = "QemuInstance";
        args = {"&platform.qemu_inst_mgr", "AARCH64"};
        accel = accel;
        tcg_mode = "MULTI";
        sync_policy = "multithread-unconstrained";
        qemu_args = qemu_args;
    };

    si_cl1_sram = {
        moduletype = "gs_memory";
        dmi = true;
        target_socket = {
            address = SI_CL1_SRAM_BASE;
            size = SI_CL1_SRAM_SIZE;
            bind = "&router.initiator_socket";
        };
        log_level = 0;
    };

    si_cl1_shared_ram = {
        moduletype = "gs_memory";
        dmi = true;
        target_socket = {
            address = SI_CL1_SHARED_BASE;
            size = SI_CL1_SHARED_SIZE;
            bind = "&router.initiator_socket";
        };
        log_level = 0;
    };

    si_cl1_scmi_shmem = {
        moduletype = "gs_memory";
        target_socket = {
            address = SI_CL1_SCMI_SHMEM_BASE;
            size = SI_CL1_SCMI_SHMEM_SIZE;
            bind = "&router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    };

    si_cl1_gic = {
        moduletype = "arm_gicv3";
        args = {"&platform.qemu_inst"};
        dist_iface = {
            address = SI_CL1_GICD_BASE;
            size = 0x00010000;
            bind = "&router.initiator_socket";
        };
        redist_region = {1, 1, 1, 1};
        num_cpus = SI_CL1_CPU_COUNT;
        num_spi = 128;
    };

    si_cl1_console_file = {
        moduletype = "char_backend_file";
        read_file = si_cl1_uart_read_file;
        write_file = si_cl1_log;
        poll_read = si_cl1_uart_poll_read;
        poll_interval_ms = 100;
        baudrate = 0;
    };

    si_cl1_uart = {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = SI_CL1_UART_BASE;
            size = 0x00010000;
            bind = "&router.initiator_socket";
        };
        irq = {bind = "&si_cl1_gic.spi_in_"..SI_CL1_UART_IRQ};
        backend_socket = {bind = "&si_cl1_console_file.biflow_socket"};
    };

    si_cl1_hipc_mhu_pbx = {
        moduletype = "mhuv3_stub";
        frame = "pbx";
        pair = "apollo_si_cl1_hipc_isolated";
        protocol = "doorbell";
        channel_count = SI_CL1_MHU_CHANNELS;
        trace = mhu_trace;
        trace_file = mhu_trace_file;
        trace_limit = mhu_trace_limit;
        target_socket = {
            address = SI_CL1_HIPC_PBX_BASE;
            size = SI_CL1_HIPC_MHU_SIZE;
            bind = "&router.initiator_socket";
        };
        initiator_socket = {bind = "&router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_40"};
        log_level = 0;
    };

    si_cl1_hipc_mhu_mbx = {
        moduletype = "mhuv3_stub";
        frame = "mbx";
        pair = "apollo_si_cl1_hipc_isolated";
        protocol = "doorbell";
        channel_count = SI_CL1_MHU_CHANNELS;
        trace = mhu_trace;
        trace_file = mhu_trace_file;
        trace_limit = mhu_trace_limit;
        target_socket = {
            address = SI_CL1_HIPC_MBX_BASE;
            size = SI_CL1_HIPC_MHU_SIZE;
            bind = "&router.initiator_socket";
        };
        initiator_socket = {bind = "&router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_41"};
        log_level = 0;
    };

    si_cl1_pfdi_mhu_pbx = {
        moduletype = "mhuv3_stub";
        frame = "pbx";
        pair = "apollo_si_cl1_pfdi_isolated";
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
            bind = "&router.initiator_socket";
        };
        initiator_socket = {bind = "&router.target_socket"};
        irq = {bind = "&si_cl1_gic.spi_in_50"};
        log_level = 0;
    };

    fallback_0 = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x0;
            size = 0x200000000;
            bind = "&router.initiator_socket";
            priority = 1;
        };
        dmi_allow = false;
        log_level = 0;
    };

    load = {
        moduletype = "loader";
        initiator_socket = {bind = "&router.target_socket"};
        { bin_file = si_cl1_image, address = SI_CL1_SRAM_BASE };
    };
};

for i=0,(SI_CL1_CPU_COUNT-1) do
    local cpu = {
        moduletype = "cpu_arm_cortexR82";
        args = {"&platform.qemu_inst"};
        mem = {bind = "&router.target_socket"};
        has_el2 = true;
        psci_conduit = "smc";
        start_powered_off = false;
        rvbar = SI_CL1_ENTRY;
        mp_affinity = 0x10000 + (i << 8);
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
    };
    platform["si_cl1_cpu_"..tostring(i)] = cpu;
    platform["si_cl1_gic"]["redist_iface_"..i] = {
        address = SI_CL1_GICR0_BASE + (i * SI_CL1_GICR_STRIDE);
        size = SI_CL1_GICR_SIZE;
        bind = "&router.initiator_socket";
    };
    platform["si_cl1_gic"]["irq_out_"..i] = {
        bind = "&si_cl1_cpu_"..i..".irq_in";
    };
    platform["si_cl1_gic"]["fiq_out_"..i] = {
        bind = "&si_cl1_cpu_"..i..".fiq_in";
    };
    platform["si_cl1_gic"]["virq_out_"..i] = {
        bind = "&si_cl1_cpu_"..i..".virq_in";
    };
    platform["si_cl1_gic"]["vfiq_out_"..i] = {
        bind = "&si_cl1_cpu_"..i..".vfiq_in";
    };
end

print("si-cl1 image: "..si_cl1_image)
print("si-cl1 log:   "..si_cl1_log)
print("si-cl1 entry: 0x"..string.format("%x", SI_CL1_ENTRY))
print("accel:        "..accel)
