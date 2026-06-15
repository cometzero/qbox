-- Apollo FVP primary-compute Linux direct-boot configuration for QBox.

function top()
    local str = debug.getinfo(2, "S").source:sub(2)
    if str:match("(.*/)")
    then
        return str:match("(.*/)")
    else
        return "./"
    end
end

print("Apollo FVP QBox config running...")

INITIAL_DDR_SPACE = 0x80000000

_KERNEL64_LOAD_ADDR = 0x80080000
_DTB_LOAD_ADDR      = 0x8fc00000
_INITRAMFS_LOAD_ADDR = 0x94000000

dofile(top().."../../ubuntu/fw/arm64_bootloader.lua")

local function getenv_or(name, default)
    local value = os.getenv(name)
    if value == nil or value == "" then
        return default
    end
    return value
end

local root = top().."../../../../../"
local kernel_image = getenv_or(
    "QBOX_APOLLO_KERNEL",
    root.."build/tmp_baremetal/deploy/images/apollo-fvp/Image")
local dtb_image = getenv_or(
    "QBOX_APOLLO_DTB",
    root.."build/qbox-apollo-fvp/apollo-fvp-primary-compute.dtb")
local initramfs_image = os.getenv("QBOX_APOLLO_INITRAMFS")
local disk_image = os.getenv("QBOX_APOLLO_ROOTFS")
local extra_disk_images = {
    getenv_or("QBOX_APOLLO_EXTRA_BLK1", root.."build/qbox-apollo-fvp/apollo-extra-blk1.raw"),
    getenv_or("QBOX_APOLLO_EXTRA_BLK2", root.."build/qbox-apollo-fvp/apollo-extra-blk2.raw"),
    getenv_or("QBOX_APOLLO_EXTRA_BLK3", root.."build/qbox-apollo-fvp/apollo-extra-blk3.raw"),
}
local netdev = getenv_or("QBOX_APOLLO_NETDEV", "type=user,hostfwd=tcp::2222-:22")

if ACCEL == nil then
    ACCEL = getenv_or("QBOX_APOLLO_ACCEL", "tcg")
end

local ARM_NUM_CPUS = 4
local ARCH_TIMER_VIRT_IRQ = 16 + 11
local ARCH_TIMER_S_EL1_IRQ = 16 + 13
local ARCH_TIMER_NS_EL1_IRQ = 16 + 14
local ARCH_TIMER_NS_EL2_IRQ = 16 + 10

platform = {

    moduletype="Container";

    quantum_ns = 10000000;

    router = {
        moduletype="router";
        log_level=0;
    },

    keep_alive_0 = {
        moduletype = "keep_alive";
    },

    ram_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = INITIAL_DDR_SPACE;
            size = 0x7f000000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    ram_1 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x880000000;
            size = 0x80000000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    sram_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x00180000;
            size = 0x00001000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    si_cl1_rproc_rsctbl_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x00100000;
            size = 0x00020000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    si_cl1_vdev0vring0_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x00120000;
            size = 0x00020000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    si_cl1_vdev0vring1_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x00140000;
            size = 0x00020000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    si_cl1_vdev0buffer_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x00160000;
            size = 0x00020000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    ras_buffer_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0xffa00000;
            size = 0x00100000;
            bind = "&router.initiator_socket"
        };
        log_level=0,
    };

    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    },

    qemu_inst = {
        moduletype="QemuInstance";
        args = {"&platform.qemu_inst_mgr", "AARCH64"};
        accel = ACCEL,
        tcg_mode = "MULTI",
        sync_policy = "multithread-unconstrained"
    },

    gic_0 = {
        moduletype = "arm_gicv3",
        args = {"&platform.qemu_inst"},
        dist_iface = {
            address = 0x20800000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        };
        redist_iface_0 = {
            address = 0x20880000,
            size = 0x40000,
            bind = "&router.initiator_socket"
        };
        redist_iface_1 = {
            address = 0x208c0000,
            size = 0x40000,
            bind = "&router.initiator_socket"
        };
        redist_iface_2 = {
            address = 0x20900000,
            size = 0x40000,
            bind = "&router.initiator_socket"
        };
        redist_iface_3 = {
            address = 0x20940000,
            size = 0x40000,
            bind = "&router.initiator_socket"
        };
        num_cpus = ARM_NUM_CPUS,
        redist_region = {1, 1, 1, 1};
        has_lpi = true;
        num_spi = 512
    };

    its_0 = {
        moduletype = "arm_gicv3_its",
        args = {"&platform.qemu_inst", "&platform.gic_0"};
        mem = {
            address = 0x20840000,
            size = 0x40000,
            bind = "&router.initiator_socket"
        };
    };

    virtioblk_1 = {
        moduletype = "virtio_mmio_blk",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30030000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_258"},
        blkdev_str = "file="..extra_disk_images[1]..",format=raw,if=none,cache=writeback"
    };

    virtioblk_2 = {
        moduletype = "virtio_mmio_blk",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30040000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_259"},
        blkdev_str = "file="..extra_disk_images[2]..",format=raw,if=none,cache=writeback"
    };

    virtioblk_3 = {
        moduletype = "virtio_mmio_blk",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30050000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_260"},
        blkdev_str = "file="..extra_disk_images[3]..",format=raw,if=none,cache=writeback"
    };

    virtionet0_0 = {
        moduletype = "virtio_mmio_net",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30060000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_261"},
        netdev_str = netdev
    };

    virtiorng_0 = {
        moduletype = "virtio_mmio_rng",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30080000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_263"}
    };

    rtc_0 = {
        moduletype = "pl031",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x300d0000,
            size = 0x1000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_268"}
    };

    watchdog_0 = {
        moduletype = "sbsa_gwdt",
        args = {"&platform.qemu_inst"};
        refresh_mem = {
            address = 0x1a420000,
            size = 0x1000,
            bind = "&router.initiator_socket"
        },
        control_mem = {
            address = 0x1a430000,
            size = 0x1000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_50"}
    };

    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio";
        read_write = true;
    };

    pl011_uart_0 = {
        moduletype = "Pl011",
        dylib_path = "uart-pl011",
        target_socket = {
            address = 0x1a400000,
            size = 0x1000,
            bind = "&router.initiator_socket"
        },
        irq = {bind = "&gic_0.spi_in_52"},
        backend_socket = {bind = "&charbackend_stdio_0.biflow_socket"},
    };

    global_peripheral_initiator_arm_0 = {
        moduletype = "global_peripheral_initiator",
        args = {"&platform.qemu_inst", "&platform.cpu_0"},
        global_initiator = {bind = "&router.target_socket"},
    };

    fallback_0 = {
        moduletype="gs_memory";
        target_socket = {
            address = 0x0;
            size = 0x800000000,
            bind = "&router.initiator_socket",
            priority = 1
        },
        dmi_allow = false,
        log_level = 0,
    };

    load = {
        moduletype = "loader",
        initiator_socket = {bind = "&router.target_socket"};
        { bin_file = kernel_image, address = _KERNEL64_LOAD_ADDR };
        { bin_file = dtb_image, address = _DTB_LOAD_ADDR };
        { data = _bootloader_aarch64, address = INITIAL_DDR_SPACE };
    };
};

if initramfs_image ~= nil and initramfs_image ~= "" then
    table.insert(platform.load, {
        bin_file = initramfs_image,
        address = _INITRAMFS_LOAD_ADDR
    })
end

if disk_image ~= nil and disk_image ~= "" then
    platform.virtioblk_0 = {
        moduletype = "virtio_mmio_blk",
        args = {"&platform.qemu_inst"};
        mem = {
            address = 0x30020000,
            size = 0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {bind = "&gic_0.spi_in_257"},
        blkdev_str = "file="..disk_image..",format=raw,if=none,cache=writeback"
    };
end

print("kernel is loaded at: 0x"..string.format("%x", _KERNEL64_LOAD_ADDR));
print("dtb is loaded at:    0x"..string.format("%x", _DTB_LOAD_ADDR));
if initramfs_image ~= nil and initramfs_image ~= "" then
    print("initramfs is loaded at: 0x"..string.format("%x", _INITRAMFS_LOAD_ADDR));
end
print("kernel image: "..kernel_image);
print("dtb image:    "..dtb_image);
if initramfs_image ~= nil and initramfs_image ~= "" then
    print("initramfs:    "..initramfs_image);
end
if disk_image ~= nil and disk_image ~= "" then
    print("disk image:   "..disk_image);
end
for i=1,#extra_disk_images do
    print("extra disk "..i..": "..extra_disk_images[i]);
end
print("accel:        "..ACCEL);

local psci_conduit = "smc";
if ACCEL == "kvm" then
    psci_conduit = "hvc";
end
print("PSCI conduit: "..psci_conduit)

for i=0,(ARM_NUM_CPUS-1) do
    local cpu = {
        moduletype = "cpu_arm_cortexA720AE";
        args = {"&platform.qemu_inst"};
        mem = {bind = "&router.target_socket"};
        has_el3 = false;
        has_el2 = false;
        irq_timer_phys_out = {
            bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL1_IRQ
        },
        irq_timer_virt_out = {
            bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_VIRT_IRQ
        },
        irq_timer_hyp_out = {
            bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL2_IRQ
        },
        irq_timer_sec_out = {
            bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_S_EL1_IRQ
        },
        gicv3_maintenance_interrupt = {
            bind = "&gic_0.ppi_in_cpu_"..i.."_25"
        },
        pmu_interrupt = {bind = "&gic_0.ppi_in_cpu_"..i.."_23"},
        psci_conduit = psci_conduit,
        mp_affinity = i * 0x100;
        start_powered_off = true;
        rvbar = INITIAL_DDR_SPACE;
    };
    if i == 0 then
        cpu["start_powered_off"] = false;
    end
    platform["cpu_"..tostring(i)] = cpu;

    platform["gic_0"]["irq_out_"..i] = {bind = "&cpu_"..i..".irq_in"}
    platform["gic_0"]["fiq_out_"..i] = {bind = "&cpu_"..i..".fiq_in"}
    platform["gic_0"]["virq_out_"..i] = {bind = "&cpu_"..i..".virq_in"}
    platform["gic_0"]["vfiq_out_"..i] = {bind = "&cpu_"..i..".vfiq_in"}
end
