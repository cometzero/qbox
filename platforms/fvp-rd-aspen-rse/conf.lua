-- RD-Aspen RSE-oriented boot skeleton for QBox.

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

print("RD-Aspen RSE QBox skeleton config running...")

local root = top().."../../../../"
local deploy = root.."build/tmp_baremetal/deploy/images/fvp-rd-aspen/"

local rse_rom = getenv_or("QBOX_RDASPEN_RSE_ROM", deploy.."rse-rom-image.img")
local rse_flash = getenv_or("QBOX_RDASPEN_RSE_FLASH", deploy.."rse-flash-image.img")
local rse_otp = getenv_or("QBOX_RDASPEN_RSE_OTP", deploy.."rse-otp-image.img")
local provisioning_bundle = getenv_or(
    "QBOX_RDASPEN_PROVISIONING_BUNDLE",
    deploy.."combined_provisioning_message.bin")
local rse_log = getenv_or(
    "QBOX_RDASPEN_RSE_LOG",
    root.."build/qbox-fvp-rd-aspen/qbox-rse.log")
local rse_uart_read_file = getenv_or("QBOX_RDASPEN_UART_READ_FILE", "/dev/null")
local qemu_args = getenv_or("QBOX_RDASPEN_RSE_QEMU_ARGS", "")
local cc3xx_trace = getenv_or("QBOX_RDASPEN_CC3XX_TRACE", "false") == "true"
local cc3xx_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_CC3XX_TRACE_LIMIT", "64"))
local dma350_trace = getenv_or("QBOX_RDASPEN_DMA350_TRACE", "false") == "true"
local dma350_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_DMA350_TRACE_LIMIT", "64"))
local remote_cpu_exec = getenv_or(
    "QBOX_REMOTE_CPU_EXEC",
    root.."tools/qbox/build/remote_cpu")

local RSE_ROM_BASE_S = 0x11000000
local RSE_ROM_SIZE = 0x00020000
local RSE_DTCM_BASE_NS = 0x20000000
local RSE_DTCM_CPU0_BASE_NS = 0x24000000
local RSE_DTCM_BASE_S = 0x30000000
local RSE_DTCM_CPU0_BASE_S = 0x34000000
local RSE_DTCM_SIZE = 0x00008000
local RSE_VM0_BASE_S = 0x31000000
local RSE_VM_SIZE = 0x00040000
local RSE_VM1_BASE_S = RSE_VM0_BASE_S + RSE_VM_SIZE
local RSE_PROVISIONING_OFFSET = 0x00020000
local RSE_BOOT_FLASH_BASE_S = 0xB0000000
local RSE_BOOT_FLASH_SIZE = 0x04000000
local RSE_HOST_UART0_BASE_S = 0x7FF00000
local RSE_DMA350_BASE_S = 0x50002000
local RSE_KMU_BASE_S = 0x5009E000
local RSE_ATU_BASE_S = 0x50150000
local RSE_CC3XX_BASE_S = 0x50154000
local RSE_SYSCNTR_CNTRL_BASE_S = 0x5015A000
local RSE_SYSCNTR_READ_BASE_S = 0x5015B000
local RSE_INTEGRITY_CHECKER_BASE_S = 0x5015C000
local RSE_TRAM_BASE_S = 0x5015D000
local RSE_LCM_BASE_S = 0x500A0000
local RSE_MPC_VM0_BASE_S = 0x50083000
local RSE_MPC_VM1_BASE_S = 0x50084000
local RSE_OTP_WRAPPER_BASE_S = 0x58111000
local RSE_NVIC_BASE = 0xE000E000
local RSE_NVIC_SIZE = 0x00010000

platform = {
    moduletype = "Container";
    quantum_ns = 10000000;

    rse_router = {
        moduletype = "router";
        log_level = 0;
    },

    keep_alive_0 = {
        moduletype = "keep_alive";
    },

    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    },

    qemu_inst = {
        moduletype = "QemuInstance";
        args = {"&platform.qemu_inst_mgr", "AARCH64"};
        sync_policy = "multithread-freerunning";
        qemu_args = qemu_args;
    },

    rse_rom = {
        moduletype = "gs_memory";
        read_only = true;
        target_socket = {
            address = RSE_ROM_BASE_S;
            size = RSE_ROM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        load = {bin_file = rse_rom, offset = 0};
        log_level = 0;
    },

    rse_dtcm = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_DTCM_BASE_S;
            size = RSE_DTCM_SIZE;
            bind = "&rse_router.initiator_socket";
            aliases = {
                cpu0_s = {
                    address = RSE_DTCM_CPU0_BASE_S;
                    size = RSE_DTCM_SIZE;
                };
                ns = {
                    address = RSE_DTCM_BASE_NS;
                    size = RSE_DTCM_SIZE;
                };
                cpu0_ns = {
                    address = RSE_DTCM_CPU0_BASE_NS;
                    size = RSE_DTCM_SIZE;
                };
            };
        };
        init_mem = true;
        log_level = 0;
    },

    rse_vm0 = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_VM0_BASE_S;
            size = RSE_VM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    rse_vm1 = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_VM1_BASE_S;
            size = RSE_VM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        init_mem = true;
        load = {bin_file = provisioning_bundle, offset = RSE_PROVISIONING_OFFSET};
        log_level = 0;
    },

    rse_boot_flash = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_BOOT_FLASH_BASE_S;
            size = RSE_BOOT_FLASH_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        load = {bin_file = rse_flash, offset = 0};
        log_level = 0;
    },

    rse_otp_wrapper = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_OTP_WRAPPER_BASE_S;
            size = 0x00010000;
            bind = "&rse_router.initiator_socket";
        };
        load = {bin_file = rse_otp, offset = 0};
        log_level = 0;
    },

    rse_dma350 = {
        moduletype = "dma350";
        trace = dma350_trace;
        trace_limit = dma350_trace_limit;
        target_socket = {
            address = RSE_DMA350_BASE_S;
            size = 0x00002000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_kmu_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_KMU_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_lcm_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_LCM_BASE_S;
            size = 0x00010000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_mpc_vm0_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_MPC_VM0_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_mpc_vm1_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_MPC_VM1_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_atu_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_ATU_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_cc3xx = {
        moduletype = "cc3xx";
        trace = cc3xx_trace;
        trace_limit = cc3xx_trace_limit;
        target_socket = {
            address = RSE_CC3XX_BASE_S;
            size = 0x00002000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_syscntr_cntrl_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_SYSCNTR_CNTRL_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_syscntr_read_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_SYSCNTR_READ_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_integrity_checker_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_INTEGRITY_CHECKER_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_tram = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_TRAM_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    rse_uart_file = {
        moduletype = "char_backend_file";
        read_file = rse_uart_read_file;
        write_file = rse_log;
        baudrate = 0;
    },

    rse_host_uart0_s = {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = RSE_HOST_UART0_BASE_S;
            size = 0x00010000;
            bind = "&rse_router.initiator_socket";
        };
        irq = {bind = "&rse_cpu_pass.target_signal_socket_0"};
        backend_socket = {bind = "&rse_uart_file.biflow_socket"};
    },

    rse_cpu_pass = {
        moduletype = "RemotePass";
        exec_path = remote_cpu_exec;
        remote_argv = {"--param", "log_level=0"};
        tlm_initiator_ports_num = 2;
        tlm_target_ports_num = 0;
        target_signals_num = 1;
        initiator_signals_num = 0;
        initiator_socket_0 = {bind = "&rse_router.target_socket"};
        initiator_socket_1 = {bind = "&rse_router.target_socket"};

        plugin_pass = {
            moduletype = "RemotePass";
            tlm_initiator_ports_num = 0;
            tlm_target_ports_num = 2;
            target_signals_num = 0;
            initiator_signals_num = 1;
            target_socket_0 = {
                address = 0x00000000;
                size = RSE_NVIC_BASE;
                bind = "&cpu_0.router.initiator_socket";
            };
            target_socket_1 = {
                address = RSE_NVIC_BASE + RSE_NVIC_SIZE;
                size = 0x00100000;
                bind = "&cpu_0.router.initiator_socket";
            };
            initiator_signal_socket_0 = {bind = "&cpu_0.cpu.nvic.irq_in_0"};
        },

        qemu_inst_mgr = {
            moduletype = "QemuInstanceManager";
        },

        qemu_inst = {
            moduletype = "QemuInstance";
            args = {"&qemu_inst_mgr", "AARCH64"};
            sync_policy = "multithread-freerunning";
            qemu_args = qemu_args;
        },

        cpu_0 = {
            moduletype = "RemoteCPU";
            args = {"&qemu_inst"};
            cpu = {
                init_svtor = RSE_ROM_BASE_S;
                init_nsvtor = RSE_ROM_BASE_S;
                start_powered_off = false;
                nvic = {
                    mem = {
                        address = RSE_NVIC_BASE;
                        size = RSE_NVIC_SIZE;
                    };
                    num_irq = 160;
                };
            };
        };
    },
}

print("rse rom:      "..rse_rom)
print("rse flash:    "..rse_flash)
print("rse otp:      "..rse_otp)
print("provisioning: "..provisioning_bundle)
print("rse log:      "..rse_log)
print("remote cpu:   "..remote_cpu_exec)
print("rse rom base: 0x"..string.format("%x", RSE_ROM_BASE_S))
