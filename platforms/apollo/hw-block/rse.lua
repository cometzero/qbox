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

function getenv_number_or(name, default)
    local value = tonumber(getenv_or(name, default))
    assert(value ~= nil, name.." must be numeric")
    return value
end

local function repeat_value(value, count)
    local values = {}
    for _=1,count do
        values[#values + 1] = value
    end
    return values
end

local function mp_affinity(cpu_index)
    local cluster = math.floor(cpu_index / 4)
    local core = cpu_index % 4
    return (cluster * 0x10000) + (core * 0x100)
end

function ap_cpu_reset_bind_targets(count)
    local targets = {}
    for i=0,(count-1) do
        targets[#targets + 1] = "&ap_cpu_"..i..".reset"
    end
    return table.concat(targets, ";")
end

function ap_system_reset_bind_targets()
    local targets = {
        "&ap_bl2_reset_loader.reset";
        "&host_ap_bl2_header_sram.reset";
        "&host_ap_mhu_ns_shared_sram.reset";
        "&ap_reset_gpio.reset_in";
    }

    return table.concat(targets, ";")
end

print("RD-Aspen RSE QBox skeleton config running...")

local root = top().."../../../../../"
local deploy = root.."build/tmp_baremetal/deploy/images/fvp-rd-aspen/"

local rse_rom = getenv_or("QBOX_RDASPEN_RSE_ROM", deploy.."rse-rom-image.img")
local rse_flash = getenv_or("QBOX_RDASPEN_RSE_FLASH", deploy.."rse-flash-image.img")
local rse_otp = getenv_or("QBOX_RDASPEN_RSE_OTP", deploy.."rse-otp-image.img")
local ap_flash = getenv_or("QBOX_RDASPEN_AP_FLASH", deploy.."ap-flash-image.img")
flash_writeback = getenv_or("QBOX_RDASPEN_FLASH_WRITEBACK", "false") == "true"
flash_defer_backing_flush_interval =
    getenv_number_or("QBOX_RDASPEN_FLASH_DEFER_FLUSH_INTERVAL", 1024)
AP_BL2_ELF = getenv_or(
    "QBOX_RDASPEN_AP_BL2_ELF",
    root.."build/tmp_baremetal/work/fvp_rd_aspen-poky-linux/trusted-firmware-a/2.14.0+git/build/rdaspen/debug/bl2/bl2.elf")
local ap_virtio = {
    disk_image = getenv_or("QBOX_RDASPEN_ROOTFS", deploy.."baremetal-image-fvp-rd-aspen.wic");
    extra_disk_images = {
        getenv_or("QBOX_RDASPEN_EXTRA_BLK1", deploy.."efi-capsule-update-disk-image-fvp-rd-aspen.img");
        getenv_or("QBOX_RDASPEN_EXTRA_BLK2", root.."build/qbox-fvp-rd-aspen/rd-aspen-extra-blk2.raw");
        getenv_or("QBOX_RDASPEN_EXTRA_BLK3", root.."build/qbox-fvp-rd-aspen/rd-aspen-extra-blk3.raw");
    };
    netdev = getenv_or("QBOX_RDASPEN_NETDEV", "type=user,hostfwd=tcp::2222-:22");
    mmio_size = 0x00010000;
    block_base = {0x30020000; 0x30030000; 0x30040000; 0x30050000};
    block_irq = {257; 258; 259; 260};
    net_base = 0x30060000;
    net_irq = 261;
    rng_base = 0x30080000;
    rng_irq = 263;
    trace = getenv_or("QBOX_RDASPEN_VIRTIO_TRACE", "false") == "true";
    trace_file = getenv_or("QBOX_RDASPEN_VIRTIO_TRACE_FILE", "");
    trace_limit = tonumber(getenv_or("QBOX_RDASPEN_VIRTIO_TRACE_LIMIT", "4096"));
    trace_filter = getenv_or("QBOX_RDASPEN_VIRTIO_TRACE_FILTER", "control");
}
local host_si_cl0_sram_map_file = getenv_or(
    "QBOX_RDASPEN_HOST_SI_CL0_SRAM_MAP_FILE",
    "")
host_si_cl1_sram_map_file = getenv_or(
    "QBOX_RDASPEN_HOST_SI_CL1_SRAM_MAP_FILE",
    "")
local host_ap_shared_sram_map_file = getenv_or(
    "QBOX_RDASPEN_HOST_AP_SHARED_SRAM_MAP_FILE",
    "")
local host_ap_bl2_header_sram_map_file = getenv_or(
    "QBOX_RDASPEN_HOST_AP_BL2_HEADER_SRAM_MAP_FILE",
    "")
local provisioning_bundle = getenv_or(
    "QBOX_RDASPEN_PROVISIONING_BUNDLE",
    deploy.."combined_provisioning_message.bin")
local rse_log = getenv_or(
    "QBOX_RDASPEN_RSE_LOG",
    root.."build/qbox-fvp-rd-aspen/qbox-rse.log")
local secure_console_log = getenv_or(
    "QBOX_RDASPEN_SECURE_CONSOLE_LOG",
    root.."build/qbox-fvp-rd-aspen/qbox-secure-console.log")
local primary_console_log = getenv_or(
    "QBOX_RDASPEN_PRIMARY_CONSOLE_LOG",
    root.."build/qbox-fvp-rd-aspen/qbox-primary-console.log")
local rse_uart_read_file = getenv_or("QBOX_RDASPEN_UART_READ_FILE", "/dev/null")
local primary_uart_read_file = getenv_or(
    "QBOX_RDASPEN_PRIMARY_UART_READ_FILE",
    "/dev/null")
local primary_uart_poll_read = primary_uart_read_file ~= "/dev/null"
local qemu_args = getenv_or("QBOX_RDASPEN_RSE_QEMU_ARGS", "")
local ap_qemu_args = getenv_or("QBOX_RDASPEN_AP_QEMU_ARGS", "")
local ap_pc_trace = getenv_or("QBOX_RDASPEN_AP_PC_TRACE", "false") == "true"
local ap_pc_trace_file = getenv_or(
    "QBOX_RDASPEN_AP_PC_TRACE_FILE",
    root.."build/qbox-fvp-rd-aspen/ap-pc-trace.log")
local ap_pc_trace_interval = tonumber(getenv_or("QBOX_RDASPEN_AP_PC_TRACE_INTERVAL", "1"))
local ap_pc_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_AP_PC_TRACE_LIMIT", "4096"))
local ap_exception_trace = getenv_or("QBOX_RDASPEN_AP_EXCEPTION_TRACE", "false") == "true"
local enable_ap_cpus = getenv_or("QBOX_RDASPEN_ENABLE_AP_CPUS", "false") == "true"
local rse_pc_trace = getenv_or("QBOX_RDASPEN_RSE_PC_TRACE", "false") == "true"
local rse_pc_trace_file = getenv_or(
    "QBOX_RDASPEN_RSE_PC_TRACE_FILE",
    root.."build/qbox-fvp-rd-aspen/rse-pc-trace.log")
local rse_pc_trace_interval = tonumber(getenv_or("QBOX_RDASPEN_RSE_PC_TRACE_INTERVAL", "1"))
local rse_pc_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_RSE_PC_TRACE_LIMIT", "4096"))
local rse_exception_trace = getenv_or("QBOX_RDASPEN_RSE_EXCEPTION_TRACE", "false") == "true"
rse_hotpath_accel = getenv_or("QBOX_RDASPEN_RSE_HOTPATH_ACCEL", "false") == "true"
rse_hotpath_memcpy_addr = tonumber(getenv_or("QBOX_RDASPEN_RSE_HOTPATH_MEMCPY_ADDR", "0x11000488"))
rse_hotpath_memset_addr = tonumber(getenv_or("QBOX_RDASPEN_RSE_HOTPATH_MEMSET_ADDR", "0x11000448"))
rse_hotpath_max_bytes = tonumber(getenv_or("QBOX_RDASPEN_RSE_HOTPATH_MAX_BYTES", tostring(16 * 1024 * 1024)))
rse_hotpath_profile_file = getenv_or("QBOX_RDASPEN_RSE_HOTPATH_PROFILE_FILE", "")
rse_hotpath_profile_interval = tonumber(getenv_or("QBOX_RDASPEN_RSE_HOTPATH_PROFILE_INTERVAL", "1024"))
rse_lms_accel = getenv_or("QBOX_RDASPEN_RSE_LMS_ACCEL", "false") == "true"
rse_lms_verify_addr = tonumber(getenv_or("QBOX_RDASPEN_RSE_LMS_VERIFY_ADDR", "0x11009bad"))
rse_lms_max_data_bytes = tonumber(getenv_or("QBOX_RDASPEN_RSE_LMS_MAX_DATA_BYTES", tostring(16 * 1024 * 1024)))
rse_bl2_load_profile =
    getenv_or("QBOX_RDASPEN_RSE_BL2_LOAD_PROFILE", "false") == "true"
rse_bl2_boot_go_for_image_id_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_GO_FOR_IMAGE_ID_ADDR", "0x3101e288"))
rse_bl2_boot_load_image_to_sram_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_LOAD_IMAGE_TO_SRAM_ADDR", "0x3101e758"))
rse_bl2_boot_enc_load_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_LOAD_ADDR", "0x3101eeb6"))
rse_bl2_boot_enc_set_key_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_SET_KEY_ADDR", "0x3101ef52"))
rse_bl2_boot_enc_decrypt_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_DECRYPT_ADDR", "0x3101ef8c"))
rse_bl2_bootutil_img_validate_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOTUTIL_IMG_VALIDATE_ADDR", "0x3101f010"))
rse_bl2_bootutil_img_hash_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOTUTIL_IMG_HASH_ADDR", "0x3101f3aa"))
rse_bl2_bootutil_verify_sig_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOTUTIL_VERIFY_SIG_ADDR", "0x3101f5bc"))
rse_bl2_boot_image_count =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_IMAGE_COUNT", "5"))
rse_bl2_boot_state_curr_img_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_CURR_IMG_OFFSET", "0x10c8"))
rse_bl2_boot_state_imgs_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_IMGS_OFFSET", "0x0"))
rse_bl2_boot_state_image_stride =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_IMAGE_STRIDE", "88"))
rse_bl2_boot_state_slot_stride =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_SLOT_STRIDE", "44"))
rse_bl2_boot_state_slot_usage_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_SLOT_USAGE_OFFSET", "0x10d0"))
rse_bl2_boot_state_slot_usage_stride =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATE_SLOT_USAGE_STRIDE", "16"))
rse_bl2_boot_slot_usage_img_dst_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_SLOT_USAGE_IMG_DST_OFFSET", "8"))
rse_bl2_boot_slot_usage_img_sz_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_SLOT_USAGE_IMG_SZ_OFFSET", "12"))
rse_bl2_load_accel =
    getenv_or("QBOX_RDASPEN_RSE_BL2_LOAD_ACCEL", "false") == "true"
rse_bl2_load_accel_max_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_LOAD_ACCEL_MAX_BYTES", tostring(16 * 1024 * 1024)))
rse_bl2_boot_enc_accel =
    getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_ACCEL", "false") == "true"
rse_bl2_img_hash_accel =
    getenv_or("QBOX_RDASPEN_RSE_BL2_IMG_HASH_ACCEL", "false") == "true"
rse_bl2_img_hash_max_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_IMG_HASH_MAX_BYTES", tostring(16 * 1024 * 1024)))
rse_bl2_img_hash_max_seed_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_IMG_HASH_MAX_SEED_BYTES", "4096"))
rse_bl2_verify_sig_accel =
    getenv_or("QBOX_RDASPEN_RSE_BL2_VERIFY_SIG_ACCEL", "false") == "true"
rse_bl2_verify_sig_skip =
    getenv_or("QBOX_RDASPEN_RSE_BL2_VERIFY_SIG_SKIP", "false") == "true"
rse_bl2_bootutil_keys_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOTUTIL_KEYS_ADDR", "0x31000454"))
rse_bl2_bootutil_key_cnt_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOTUTIL_KEY_CNT_ADDR", "0x3102b424"))
rse_bl2_fih_success_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_FIH_SUCCESS_ADDR", "0x310027dc"))
rse_bl2_verify_sig_max_key_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_VERIFY_SIG_MAX_KEY_BYTES", "512"))
rse_bl2_verify_sig_max_sig_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_VERIFY_SIG_MAX_SIG_BYTES", "128"))
rse_bl2_delay_accel =
    getenv_or("QBOX_RDASPEN_RSE_BL2_DELAY_ACCEL", "false") == "true"
rse_bl2_delay_cycles_addr =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_DELAY_CYCLES_ADDR", "0x31021aca"))
rse_bl2_delay_max_cycles =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_DELAY_MAX_CYCLES", "50000000"))
rse_bl2_delay_expected_hits =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_DELAY_EXPECTED_HITS", "3"))
rse_bl2_boot_status_enckey_offset =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_STATUS_ENCKEY_OFFSET", "0x0c"))
rse_bl2_boot_enc_key_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_KEY_BYTES", "16"))
rse_bl2_boot_enc_key_stride =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_KEY_STRIDE", "16"))
rse_bl2_boot_enc_slots =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_SLOTS", "2"))
rse_bl2_boot_enc_max_bytes =
    tonumber(getenv_or("QBOX_RDASPEN_RSE_BL2_BOOT_ENC_MAX_BYTES", "4096"))
rse_direct_si_sram_alias =
    getenv_or("QBOX_RDASPEN_RSE_DIRECT_SI_SRAM_ALIAS", "false") == "true"
rse_direct_file_aliases =
    getenv_or("QBOX_RDASPEN_RSE_DIRECT_FILE_ALIASES", "")
rse_direct_si_sram_code_alias_size = getenv_number_or(
    "QBOX_RDASPEN_RSE_DIRECT_SI_SRAM_CODE_ALIAS_SIZE",
    "0x00100000")
local cc3xx_trace = getenv_or("QBOX_RDASPEN_CC3XX_TRACE", "false") == "true"
local cc3xx_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_CC3XX_TRACE_LIMIT", "64"))
local cc3xx_trace_filter = getenv_or("QBOX_RDASPEN_CC3XX_TRACE_FILTER", "all")
local cc3xx_trace_address_min = tonumber(getenv_or("QBOX_RDASPEN_CC3XX_TRACE_ADDRESS_MIN", "0"))
cc3xx_stats_file = getenv_or("QBOX_RDASPEN_CC3XX_STATS_FILE", "")
cc3xx_stats_interval = tonumber(getenv_or("QBOX_RDASPEN_CC3XX_STATS_INTERVAL", "0"))
cc3xx_backend = getenv_or("QBOX_RDASPEN_CC3XX_BACKEND", "systemc")
assert(cc3xx_backend == "systemc" or cc3xx_backend == "qemu-native",
       "QBOX_RDASPEN_CC3XX_BACKEND must be systemc or qemu-native")
smmu_backend = getenv_or("QBOX_RDASPEN_SMMU_BACKEND", "systemc-mmu720ae")
assert(smmu_backend == "qemu-arm-smmuv3" or smmu_backend == "systemc-mmu720ae",
       "QBOX_RDASPEN_SMMU_BACKEND must be qemu-arm-smmuv3 or systemc-mmu720ae")
local dma350_trace = getenv_or("QBOX_RDASPEN_DMA350_TRACE", "false") == "true"
local dma350_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_DMA350_TRACE_LIMIT", "64"))
local dma350_trace_filter = getenv_or("QBOX_RDASPEN_DMA350_TRACE_FILTER", "all")
local dma350_trace_address_min = tonumber(getenv_or("QBOX_RDASPEN_DMA350_TRACE_ADDRESS_MIN", "0"))
local sysctrl_trace = getenv_or("QBOX_RDASPEN_SYSCTRL_TRACE", "false") == "true"
local sysctrl_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_SYSCTRL_TRACE_LIMIT", "64"))
local lcm_trace = getenv_or("QBOX_RDASPEN_LCM_TRACE", "false") == "true"
local lcm_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_LCM_TRACE_LIMIT", "64"))
rse_lcm_lcs = getenv_number_or("QBOX_RDASPEN_RSE_LCM_LCS", "4008617381")
rse_lcm_tp_mode = getenv_number_or("QBOX_RDASPEN_RSE_LCM_TP_MODE", "286348714")
rse_lcm_sp_enable = getenv_number_or("QBOX_RDASPEN_RSE_LCM_SP_ENABLE", "0")
local boot_flash_trace = getenv_or("QBOX_RDASPEN_BOOT_FLASH_TRACE", "false") == "true"
local boot_flash_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_BOOT_FLASH_TRACE_LIMIT", "64"))
local boot_flash_dmi = getenv_or("QBOX_RDASPEN_BOOT_FLASH_DMI", "false") == "true"
boot_flash_dmi_ranges = getenv_or("QBOX_RDASPEN_BOOT_FLASH_DMI_RANGES", "")
local host_memory_dmi = getenv_or("QBOX_RDASPEN_HOST_MEMORY_DMI", "false") == "true"
host_si_sram_dmi = getenv_or("QBOX_RDASPEN_HOST_SI_SRAM_DMI", "false") == "true"
ap_flash_dmi_ranges = getenv_or("QBOX_RDASPEN_AP_FLASH_DMI_RANGES", "")
rse_boot_flash_stats_file = getenv_or(
    "QBOX_RDASPEN_RSE_BOOT_FLASH_STATS_FILE",
    "")
ap_flash_stats_file = getenv_or("QBOX_RDASPEN_AP_FLASH_STATS_FILE", "")
flash_stats_interval = tonumber(getenv_or("QBOX_RDASPEN_FLASH_STATS_INTERVAL", "0"))
local atu_trace = getenv_or("QBOX_RDASPEN_ATU_TRACE", "false") == "true"
local atu_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_ATU_TRACE_LIMIT", "64"))
local atu_trace_filter = getenv_or("QBOX_RDASPEN_ATU_TRACE_FILTER", "all")
local atu_trace_address_min = tonumber(getenv_or("QBOX_RDASPEN_ATU_TRACE_ADDRESS_MIN", "0"))
local atu_trace_address_max = tonumber(getenv_or("QBOX_RDASPEN_ATU_TRACE_ADDRESS_MAX", "0"))
local atu_dmi = getenv_or("QBOX_RDASPEN_ATU_DMI", "false") == "true"
local kmu_trace = getenv_or("QBOX_RDASPEN_KMU_TRACE", "false") == "true"
local kmu_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_KMU_TRACE_LIMIT", "64"))
local kmu_trace_filter = getenv_or("QBOX_RDASPEN_KMU_TRACE_FILTER", "all")
local integrity_checker_trace = getenv_or("QBOX_RDASPEN_INTEGRITY_CHECKER_TRACE", "false") == "true"
local integrity_checker_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_INTEGRITY_CHECKER_TRACE_LIMIT", "64"))
local sam_trace = getenv_or("QBOX_RDASPEN_SAM_TRACE", "false") == "true"
local sam_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_SAM_TRACE_LIMIT", "64"))
local host_ppu_trace = getenv_or("QBOX_RDASPEN_HOST_PPU_TRACE", "false") == "true"
local host_ppu_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_HOST_PPU_TRACE_LIMIT", "64"))
local mhu_trace = getenv_or("QBOX_RDASPEN_MHU_TRACE", "false") == "true"
local mhu_trace_limit = tonumber(getenv_or("QBOX_RDASPEN_MHU_TRACE_LIMIT", "256"))
local mhu_trace_file = getenv_or(
    "QBOX_RDASPEN_MHU_TRACE_FILE",
    root.."build/qbox-fvp-rd-aspen/mhuv3-trace.log")
remotepass_dmi_cache =
    getenv_or("QBOX_RDASPEN_REMOTEPASS_DMI_CACHE", "false") == "true"
local ap_power_domain_reset_delay_ns = tonumber(
    getenv_or("QBOX_RDASPEN_AP_POWER_DOMAIN_RESET_DELAY_NS", "1"))
rse_local_crypto = getenv_or("QBOX_RDASPEN_RSE_LOCAL_CRYPTO", "true") == "true"
rse_local_boot_flash = getenv_or("QBOX_RDASPEN_RSE_LOCAL_BOOT_FLASH", "true") == "true"
rse_local_peripherals = rse_local_crypto or rse_local_boot_flash
rse_split_cpu0_dtcm_alias = getenv_or(
    "QBOX_RDASPEN_RSE_SPLIT_CPU0_DTCM_ALIAS",
    "false") == "true"
rse_split_cpu0_itcm_alias = getenv_or(
    "QBOX_RDASPEN_RSE_SPLIT_CPU0_ITCM_ALIAS",
    "false") == "true"
rse_itcm_dmi = getenv_or("QBOX_RDASPEN_RSE_ITCM_DMI", "true") == "true"
rse_dtcm_dmi = getenv_or("QBOX_RDASPEN_RSE_DTCM_DMI", "true") == "true"
rse_vm_dmi = getenv_or("QBOX_RDASPEN_RSE_VM_DMI", "true") == "true"
rse_vmaddrwidth = getenv_number_or(
    "QBOX_RDASPEN_RSE_VMADDRWIDTH",
    "18")
assert(rse_vmaddrwidth >= 18 and rse_vmaddrwidth <= 31,
       "QBOX_RDASPEN_RSE_VMADDRWIDTH must be in range 18..31")
rse_reset_syndrome = getenv_number_or(
    "QBOX_RDASPEN_RSE_RESET_SYNDROME",
    "0x80000000")
rse_cpuwait = getenv_number_or(
    "QBOX_RDASPEN_RSE_CPUWAIT",
    "0x0000000F")
rse_dma_boot_en = getenv_number_or(
    "QBOX_RDASPEN_RSE_DMA_BOOT_EN",
    "0x00000001")
rse_dma_boot_addr = getenv_number_or(
    "QBOX_RDASPEN_RSE_DMA_BOOT_ADDR",
    "0x00000000")
local remote_cpu_exec = getenv_or(
    "QBOX_REMOTE_CPU_EXEC",
    root.."tools/qbox/build/remote_cpu")

local RSE_ROM_BASE_S = 0x11000000
local RSE_ROM_SIZE = 0x00020000
local RSE_ITCM_BASE_NS = 0x00000000
local RSE_ITCM_CPU0_BASE_NS = 0x0A000000
local RSE_ITCM_BASE_S = 0x10000000
local RSE_ITCM_CPU0_BASE_S = 0x1A000000
local RSE_ITCM_SIZE = 0x00008000
local RSE_DTCM_BASE_NS = 0x20000000
local RSE_DTCM_CPU0_BASE_NS = 0x24000000
local RSE_DTCM_BASE_S = 0x30000000
local RSE_DTCM_CPU0_BASE_S = 0x34000000
local RSE_DTCM_SIZE = 0x00008000
local RSE_VM0_BASE_S = 0x31000000
local RSE_VM_SIZE = 2 ^ rse_vmaddrwidth
local RSE_VM1_BASE_S = RSE_VM0_BASE_S + RSE_VM_SIZE
local RSE_PROVISIONING_OFFSET = 0x00020000
local RSE_BOOT_FLASH_BASE_S = 0xB0000000
local RSE_BOOT_FLASH_SIZE = 0x04000000
local RSE_HOST_ACCESS_BASE_NS = 0x60000000
local RSE_HOST_ACCESS_BASE_S = 0x70000000
local RSE_HOST_ACCESS_SIZE = 0x10000000
HOST_SI_CL0_IMG_HDR_LOGICAL_BASE = 0x70083C00
HOST_SI_CL0_IMG_CODE_LOGICAL_BASE = 0x70084000
HOST_SI_CL0_HEADER_FILE_OFFSET = 0x000FFC00
HOST_SI_CL0_CODE_FILE_OFFSET = 0x00000000
HOST_SI_CL1_IMG_HDR_LOGICAL_BASE = 0x70185C00
HOST_SI_CL1_IMG_CODE_LOGICAL_BASE = 0x70186000
HOST_SI_CL1_HEADER_FILE_OFFSET = 0x000FFC00
HOST_SI_CL1_CODE_FILE_OFFSET = 0x00000000
HOST_SI_IMG_HEADER_ALIAS_SIZE = 0x00000400
local RSE_HOST_UART0_BASE_NS = RSE_HOST_ACCESS_BASE_NS + 0x0FF00000
local RSE_HOST_UART0_BASE_S = RSE_HOST_ACCESS_BASE_S + 0x0FF00000
local RSE_NSACFG_BASE_NS = 0x40080000
local RSE_DMA350_BASE_S = 0x50002000
local RSE_SACFG_BASE_S = 0x50080000
local RSE_KMU_BASE_S = 0x5009E000
local RSE_SAM_BASE_S = 0x5009F000
local RSE_ATU_BASE_S = 0x50150000
local RSE_CC3XX_BASE_S = 0x50154000
local RSE_SYSCNTR_CNTRL_BASE_S = 0x5015A000
local RSE_SYSCNTR_READ_BASE_S = 0x5015B000
local RSE_INTEGRITY_CHECKER_BASE_S = 0x5015C000
local RSE_TRAM_BASE_S = 0x5015D000
RSE_MHU0_SENDER_BASE_S = 0x50160000
RSE_MHU0_RECEIVER_BASE_S = 0x50170000
RSE_MHU2_SENDER_BASE_S = 0x501A0000
RSE_MHU2_RECEIVER_BASE_S = 0x501B0000

function rse_cc3xx_component(target_bind, initiator_bind)
    local component = {
        moduletype = cc3xx_backend == "qemu-native" and "qemu_cc3xx" or "cc3xx";
        trace = cc3xx_trace;
        trace_limit = cc3xx_trace_limit;
        trace_skip = tonumber(getenv_or("QBOX_RDASPEN_CC3XX_TRACE_SKIP", "0"));
        trace_filter = cc3xx_trace_filter;
        trace_address_min = cc3xx_trace_address_min;
        stats_file = cc3xx_stats_file;
        stats_interval = cc3xx_stats_interval;
        target_socket = {
            address = RSE_CC3XX_BASE_S;
            size = 0x00002000;
            bind = target_bind;
        };
        initiator_socket = {bind = initiator_bind};
        log_level = 0;
    }

    if cc3xx_backend == "qemu-native" then
        component.args = {"&qemu_inst"}
        component.size = 0x00002000
    end

    return component
end
RSE_LOCAL_MHU_FRAME_SIZE = 0x00010000
local RSE_LCM_BASE_S = 0x500A0000
local RSE_LCM_SIZE = 0x00011000
local RSE_SYSCTRL_BASE_S = 0x58021000
local RSE_INTEG_LAYER_BASE_S = 0x58100000
local RSE_MPC_VM0_BASE_S = 0x50083000
local RSE_MPC_VM1_BASE_S = 0x50084000
local RSE_OTP_WRAPPER_BASE_S = 0x58111000
local RSE_NVIC_BASE = 0xE000E000
local RSE_NVIC_SIZE = 0x00010000
RSE_NVIC_NUM_IRQ = 160
RSE_REMOTE_SIGNAL_COUNT = RSE_NVIC_NUM_IRQ
RSE_IRQ_CMU_MHU0_RECEIVER = 41
RSE_IRQ_CMU_MHU2_RECEIVER = 45
RSE_IRQ_SI_CL0_RSE_CMU_MHU_RECEIVER = 139
local HOST_AP_SHARED_SRAM_PHYS_BASE = 0x00000000
local HOST_AP_SHARED_SRAM_SIZE = 0x00100000
local HOST_AP_SDS_MEM_SIZE = 0x00000DC0
HOST_AP_SDS_RESET_SYNDROME_PHYS_BASE = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x50
SDS_RESET_SYNDROME_SYS_RESET_REQ = 0x00000008
local HOST_AP_SCMI_PAYLOAD_BASE = HOST_AP_SHARED_SRAM_PHYS_BASE + HOST_AP_SDS_MEM_SIZE
local HOST_AP_BL2_PHYS_BASE = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x00082000
AP_BL2_RESET = {
    data_phys_base = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x00098000;
    stacks_phys_base = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x00098F40;
    bss_phys_base = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x0009A000;
    xlat_phys_base = HOST_AP_SHARED_SRAM_PHYS_BASE + 0x000A2000;
    data_elf_offset = 0x00017000;
    data_size = 0x00000F35;
    stacks_size = 0x00001000;
    bss_size = 0x00008000;
    xlat_size = 0x0000E000;
}
local HOST_AP_BL2_HEADER_SRAM_PHYS_BASE = 0x00100000
local HOST_AP_BL2_HEADER_SRAM_SIZE = 0x00080000
local HOST_AP_FLASH_PHY_BASE = 0x38000000
local HOST_AP_FLASH_IMAGE_SIZE = 0x08000000
local HOST_AP_TRUSTED_NVCTR_BASE = 0x32030000
local HOST_AP_TRUSTED_NVCTR_SIZE = 0x00010000
local HOST_AP_DRAM1_BASE = 0x80000000
local HOST_AP_DRAM1_SIZE = 0x7F000000
local HOST_AP_SPMC_BASE = 0xFFC00000
local HOST_AP_SPMC_SIZE = 0x003FC000
local HOST_AP_DRAM2_BASE = 0x20000000000
local HOST_AP_DRAM2_SIZE = 0x80000000
local HOST_AP_ATU_LOGICAL_BASE = 0x40000000
local HOST_AP_ATU_LOGICAL_SIZE = 0x00800000
local AP_NUM_CPUS = enable_ap_cpus and 4 or 0
local AP_GIC_NUM_CPUS = enable_ap_cpus and AP_NUM_CPUS or 1
local ARCH_TIMER_VIRT_IRQ = 16 + 11
local ARCH_TIMER_S_EL1_IRQ = 16 + 13
local ARCH_TIMER_NS_EL1_IRQ = 16 + 14
local ARCH_TIMER_NS_EL2_IRQ = 16 + 10
local AP_SECURE_UART_BASE = 0x1A410000
local AP_PRIMARY_UART_BASE = 0x1A400000
local AP_SECURE_WDOG_BASE = 0x1A460000
local AP_SECURE_WDOG_SIZE = 0x00010000
local AP_SYS_TIMCTL_BASE = 0x1A810000
local AP_SYS_CNT_BASE_NS = 0x1A830000
local AP_SYS_TIMER_SIZE = 0x00010000
local AP_SYS_TIMER_IRQ = 49
local AP_PRIMARY_UART_IRQ = 52
local AP_SECURE_UART_IRQ = 53
local AP_SI_SCMI_MHU_PBX_IRQ = 112
local AP_SI_SCMI_MHU_MBX_IRQ = 113
local AP_GIC_DIST_BASE = 0x20800000
local AP_GIC_REDIST_BASE = 0x20880000
local AP_GIC_REDIST_SIZE = 0x00040000
local AP_GIC_REDIST_REGIONS = 16
local AP_GIC_ACTIVE_REDIST_REGIONS = AP_GIC_NUM_CPUS
local AP_GIC_LEGACY_DIST_BASE = 0x20000000
local AP_GIC_LEGACY_REDIST_BASE = 0x200C0000
local AP_GIC_LEGACY_REDIST_SIZE = 0x00020000
local HOST_SI_CL0_CL_UTIL_BASE = 0x4000028000000
local HOST_SI_CL1_CL_UTIL_BASE = 0x4000028800000
local HOST_SI_CL_UTIL_SIZE = 0x00800000
local HOST_SI_CLUS_PPU_OFFSET = 0x00010000
local HOST_SI_CORE0_PPU_OFFSET = 0x00040000
local HOST_SI_PIK_PHYS_BASE = 0x400002A600000
local HOST_SI_SCR_PHYS_BASE = 0x400002A6B0000
local HOST_SI_ATU_PHYS_BASE = 0x4000031000000
local HOST_RSE_SI_MHU_PHYS_BASE = 0x400003C000000
local HOST_RSE_SI_MHU_SIZE = 0x01000000
local RSE_MHU_FRAME_SIZE = 0x00020000
local HOST_RSE_SI_SSRAM_PHYS_BASE = 0x4000040000000
local HOST_RSE_SI_SSRAM_SIZE = 0x00040000
local HOST_SI_CL0_SRAM_PHYS_BASE = 0x4000120000000
local HOST_SI_CL1_SRAM_PHYS_BASE = 0x4000140000000
local HOST_SI_SRAM_WINDOW_SIZE = 0x01000000
local HOST_SI_CONTROL_WINDOW_SIZE = 0x00010000
function direct_file_alias_spec(address, size, file_offset, access, path)
    assert(path ~= "", "direct file alias requires a map file")
    return string.format("0x%x:0x%x:0x%x:%s:%s",
                         address, size, file_offset, access, path)
end

if rse_direct_si_sram_alias and rse_direct_file_aliases == "" then
    rse_direct_file_aliases = table.concat({
        direct_file_alias_spec(
            HOST_SI_CL0_IMG_HDR_LOGICAL_BASE,
            HOST_SI_IMG_HEADER_ALIAS_SIZE,
            HOST_SI_CL0_HEADER_FILE_OFFSET,
            "rw",
            host_si_cl0_sram_map_file);
        direct_file_alias_spec(
            HOST_SI_CL0_IMG_CODE_LOGICAL_BASE,
            rse_direct_si_sram_code_alias_size,
            HOST_SI_CL0_CODE_FILE_OFFSET,
            "rw",
            host_si_cl0_sram_map_file);
        direct_file_alias_spec(
            HOST_SI_CL1_IMG_HDR_LOGICAL_BASE,
            HOST_SI_IMG_HEADER_ALIAS_SIZE,
            HOST_SI_CL1_HEADER_FILE_OFFSET,
            "rw",
            host_si_cl1_sram_map_file);
        direct_file_alias_spec(
            HOST_SI_CL1_IMG_CODE_LOGICAL_BASE,
            rse_direct_si_sram_code_alias_size,
            HOST_SI_CL1_CODE_FILE_OFFSET,
            "rw",
            host_si_cl1_sram_map_file);
    }, ";")
end
local HOST_AP_SI_SCMI_MHU_PBX_PHYS_BASE = 0x400003B080000
local HOST_AP_SI_SCMI_MHU_MBX_PHYS_BASE = 0x400003B0C0000
local HOST_AP_SI_MHU_FRAME_SIZE = 0x00030000
local HOST_AP_SI_PFDI_MONITOR_MHU_PBX_PHYS_BASE = 0x400003B380000
local HOST_AP_SCMI_PFDI_MONITOR_BASE = HOST_AP_SCMI_PAYLOAD_BASE + 0x00000100
local HOST_AP_SCMI_PFDI_MONITOR_STRIDE = 40
local HOST_AP_SCMI_PFDI_MONITOR_CHANNELS = 16
local HOST_AP_ATU_PHYS_BASE = 0x20000D0080000
local HOST_SMDEXP2SMD_ATU_PHYS_BASE = 0x20000D0070000
local HOST_CSS_COUNTERS_TIMERS_PHYS_BASE = 0x20000D0100000
local HOST_CSS_COUNTERS_TIMERS_SIZE = 0x00030000
local HOST_SYSTOP_PIK_PHYS_BASE = 0x20000D0200000
local HOST_SMCF_SRAM_PHYS_BASE = 0x2000060000000
local HOST_SMCF_SRAM_SIZE = 0x00002000
local HOST_AP_RSE_MHU_PHYS_BASE = 0x300001B600000
local HOST_AP_RSE_MHU_SIZE = 0x00060000
local MHU_V3_FRAME_SIZE = 0x00030000
HOST_AP_MHU_POINTER_ACCESS_PHYS_BASE = 0x0FFFE0000
HOST_AP_MHU_POINTER_ACCESS_SIZE = 0x00020000
local HOST_AP_RSE_MAILBOX_PHYS_BASE = 0xFFFFC000
local HOST_AP_RSE_MAILBOX_SIZE = 0x00004000

-- TF-A RD-Aspen BL2 expects the SCP-created SDS region at ARM_SHARED_RAM_BASE.
-- Seed the FVP-observed region descriptor and reset-syndrome structure so
-- measured boot follows the same cold-reset path as the Arm FVP logs.
local HOST_AP_SDS_REGION_DATA = {
    0x1007AA7A, HOST_AP_SDS_MEM_SIZE, -- signature, 7 structures, schema 1.0
    0x01000001, 0x00000011, 0x00000000, 0x00000000, -- AP CPU info
    0x01000002, 0x00000011, 0x00000000, 0x00000000, -- ROM version
    0x01000003, 0x00000011, 0x00000000, 0x00000000, -- RAM version
    0x01000004, 0x00000011, 0x20000000, 0x00000003, -- cfg2, FVP
    0x01000005, 0x00000011, 0x00000000, 0x00000000, -- reset syndrome
    0x01000006, 0x00000011, 0x00000007, 0x00000000, -- SCP/DMC/msg ready
    0x01000009, 0x00000021, 0x00000000, 0x00000000, -- SCP image metadata
    0x00000000, 0x00000000,
}

-- TF-A RD-Aspen certificates in the AP FIP carry trusted/non-trusted NV
-- counter values 31 and 223 respectively.
-- The platform implementation treats the backing counters as read-only, so
-- matching the FVP-visible values is required before authenticated loading.
local HOST_AP_TRUSTED_NVCTR_DATA = {
    0x0000001F, -- TFW_NVCTR_BASE, TFW_NVCTR_VAL
    0x000000DF, -- NTFW_CTR_BASE, NTFW_NVCTR_VAL
}

function rse_tcm_aliases(split_cpu0_alias, ns_address, cpu0_s_address, cpu0_ns_address, size)
    local aliases = {
        ns = {
            address = ns_address;
            size = size;
        };
    }

    if not split_cpu0_alias then
        aliases.cpu0_s = {
            address = cpu0_s_address;
            size = size;
        }
        aliases.cpu0_ns = {
            address = cpu0_ns_address;
            size = size;
        }
    end

    return aliases
end

function ap_smmu_component()
    if smmu_backend == "systemc-mmu720ae" then
        return {
            moduletype = "mmu720ae";
            mem = {
                address = 0x1C0000000;
                size = 0x08000000;
                bind = "&host_router.initiator_socket";
            };
            downstream_socket = {bind = "&host_router.target_socket"};
            ptw_socket = {bind = "&host_router.target_socket"};
            irq_combined = {bind = "&ap_gic.spi_in_65"};
            stage = "1";
            profile = "zena-css-cfg2";
        }
    end

    return {
        moduletype = "arm_smmuv3";
        args = {"&platform.ap_qemu_inst", "&platform.ap_gpex_0"};
        mem = {
            address = 0x1C0000000;
            size = 0x08000000;
            bind = "&host_router.initiator_socket";
        };
        irq_out_0 = {bind = "&ap_gic.spi_in_65"};
        stage = "1";
    }
end

platform = {
    moduletype = "Container";
    quantum_ns = 10000000;

    rse_router = {
        moduletype = "router";
        log_level = 0;
    },

    host_router = {
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

    ap_qemu_inst_mgr = enable_ap_cpus and {
        moduletype = "QemuInstanceManager";
    } or nil,

    ap_qemu_inst = enable_ap_cpus and {
        moduletype = "QemuInstance";
        args = {"&platform.ap_qemu_inst_mgr", "AARCH64"};
        accel = "tcg";
        tcg_mode = "MULTI";
        sync_policy = "multithread-freerunning";
        qemu_args = ap_qemu_args;
    } or nil,

    ap_reset_gpio = enable_ap_cpus and {
        moduletype = "reset_gpio";
        args = {"&platform.ap_qemu_inst"};
        reset_out = {bind = ap_cpu_reset_bind_targets(AP_NUM_CPUS)};
        log_level = 0;
    } or nil,

    ap_global_peripheral_initiator = enable_ap_cpus and {
        moduletype = "global_peripheral_initiator";
        args = {"&platform.ap_qemu_inst", "&platform.ap_cpu_0"};
        global_initiator = {bind = "&host_router.target_socket"};
    } or nil,

    ap_gpex_0 = enable_ap_cpus and {
        moduletype = "qemu_gpex";
        args = {"&platform.ap_qemu_inst"};
        bus_master = {bind = "&host_router.target_socket"};
        pio_iface = {
            address = 0x60200000;
            size = 0x00100000;
            bind = "&host_router.initiator_socket";
        };
        mmio_iface = {
            address = 0x60300000;
            size = 0x1FD00000;
            bind = "&host_router.initiator_socket";
        };
        ecam_iface = {
            address = 0x43B50000;
            size = 0x10000000;
            bind = "&host_router.initiator_socket";
        };
        mmio_iface_high = {
            address = 0x400000000;
            size = 0x200000000;
            bind = "&host_router.initiator_socket";
        };
        irq_out_0 = {bind = "&ap_gic.spi_in_300"};
        irq_out_1 = {bind = "&ap_gic.spi_in_301"};
        irq_out_2 = {bind = "&ap_gic.spi_in_302"};
        irq_out_3 = {bind = "&ap_gic.spi_in_303"};
    } or nil,

    rse_rom = {
        moduletype = "gs_memory";
        read_only = true;
        shared_memory = true;
        shared_memory_prefix = "rse_rom_";
        target_socket = {
            address = RSE_ROM_BASE_S;
            size = RSE_ROM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        load = {bin_file = rse_rom, offset = 0};
        log_level = 0;
    },

    rse_itcm = {
        moduletype = "gs_memory";
        shared_memory = true;
        shared_memory_prefix = "rse_itcm_";
        dmi_allow = rse_itcm_dmi;
        target_socket = {
            address = RSE_ITCM_BASE_S;
            size = RSE_ITCM_SIZE;
            bind = "&rse_router.initiator_socket";
            aliases = rse_tcm_aliases(
                rse_split_cpu0_itcm_alias,
                RSE_ITCM_BASE_NS,
                RSE_ITCM_CPU0_BASE_S,
                RSE_ITCM_CPU0_BASE_NS,
                RSE_ITCM_SIZE);
        };
        init_mem = true;
        log_level = 0;
    },

    rse_itcm_cpu0 = rse_split_cpu0_itcm_alias and {
        moduletype = "gs_memory";
        shared_memory = true;
        shared_memory_prefix = "rse_itcm_cpu0_";
        dmi_allow = rse_itcm_dmi;
        target_socket = {
            address = RSE_ITCM_CPU0_BASE_S;
            size = RSE_ITCM_SIZE;
            bind = "&rse_router.initiator_socket";
            aliases = {
                cpu0_ns = {
                    address = RSE_ITCM_CPU0_BASE_NS;
                    size = RSE_ITCM_SIZE;
                };
            };
        };
        init_mem = true;
        log_level = 0;
    } or nil,

    rse_dtcm = {
        moduletype = "gs_memory";
        shared_memory = true;
        shared_memory_prefix = "rse_dtcm_";
        dmi_allow = rse_dtcm_dmi;
        target_socket = {
            address = RSE_DTCM_BASE_S;
            size = RSE_DTCM_SIZE;
            bind = "&rse_router.initiator_socket";
            aliases = rse_tcm_aliases(
                rse_split_cpu0_dtcm_alias,
                RSE_DTCM_BASE_NS,
                RSE_DTCM_CPU0_BASE_S,
                RSE_DTCM_CPU0_BASE_NS,
                RSE_DTCM_SIZE);
        };
        init_mem = true;
        log_level = 0;
    },

    rse_dtcm_cpu0 = rse_split_cpu0_dtcm_alias and {
        moduletype = "gs_memory";
        shared_memory = true;
        shared_memory_prefix = "rse_dtcm_cpu0_";
        dmi_allow = rse_dtcm_dmi;
        target_socket = {
            address = RSE_DTCM_CPU0_BASE_S;
            size = RSE_DTCM_SIZE;
            bind = "&rse_router.initiator_socket";
            aliases = {
                cpu0_ns = {
                    address = RSE_DTCM_CPU0_BASE_NS;
                    size = RSE_DTCM_SIZE;
                };
            };
        };
        init_mem = true;
        log_level = 0;
    } or nil,

    rse_vm0 = {
        moduletype = "gs_memory";
        shared_memory = true;
        shared_memory_prefix = "rse_vm0_";
        dmi_allow = rse_vm_dmi;
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
        shared_memory = true;
        shared_memory_prefix = "rse_vm1_";
        dmi_allow = rse_vm_dmi;
        target_socket = {
            address = RSE_VM1_BASE_S;
            size = RSE_VM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        init_mem = true;
        load = {bin_file = provisioning_bundle, offset = RSE_PROVISIONING_OFFSET};
        log_level = 0;
    },

    rse_boot_flash = (not rse_local_boot_flash) and {
        moduletype = "strata_flash_j3";
        trace = boot_flash_trace;
        trace_limit = boot_flash_trace_limit;
        enable_dmi = boot_flash_dmi;
        dmi_ranges = boot_flash_dmi_ranges;
        program_ff_sets_bits = true;
        program_ff_erases_sector = true;
        size = RSE_BOOT_FLASH_SIZE;
        sector_size = 0x1000;
        backing_file = flash_writeback and rse_flash or "";
        defer_backing_write = true;
        defer_backing_flush_interval = flash_defer_backing_flush_interval;
        stats_file = rse_boot_flash_stats_file;
        stats_interval = flash_stats_interval;
        target_socket = {
            address = RSE_BOOT_FLASH_BASE_S;
            size = RSE_BOOT_FLASH_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        load = {bin_file = rse_flash, offset = 0};
        log_level = 0;
    } or nil,

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

    rse_cpu0_secctrl_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x50011000;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    rse_cpu0_pwrctrl_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x50012000;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
            aliases = {
                ns = {
                    address = 0x40012000;
                    size = 0x00001000;
                };
            };
        };
        init_mem = true;
        log_level = 0;
    },

    rse_cpu0_identity_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x5001F000;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
            aliases = {
                ns = {
                    address = 0x4001F000;
                    size = 0x00001000;
                };
            };
        };
        init_mem = true;
        log_level = 0;
    },

    rse_nsacfg_regs = {
        moduletype = "rse_protection_ctrl";
        target_socket = {
            address = RSE_NSACFG_BASE_NS;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_dma350 = {
        moduletype = "dma350";
        trace = dma350_trace;
        trace_limit = dma350_trace_limit;
        trace_filter = dma350_trace_filter;
        trace_address_min = dma350_trace_address_min;
        target_socket = {
            address = RSE_DMA350_BASE_S;
            size = 0x00002000;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&rse_router.target_socket"};
        log_level = 0;
    },

    rse_sacfg_regs = {
        moduletype = "rse_protection_ctrl";
        target_socket = {
            address = RSE_SACFG_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_kmu_regs = (not rse_local_crypto) and {
        moduletype = "rse_kmu";
        trace = kmu_trace;
        trace_limit = kmu_trace_limit;
        trace_filter = kmu_trace_filter;
        otp_image = rse_otp;
        build_config = 0x003D0005;
        hw_slot_config = 0x00D60100;
        hw_slot_export_address = 0x50154400;
        target_socket = {
            address = RSE_KMU_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&rse_router.target_socket"};
        log_level = 0;
    } or nil,

    rse_lcm_regs = {
        moduletype = "rse_lcm";
        trace = lcm_trace;
        trace_limit = lcm_trace_limit;
        otp_image = rse_otp;
        lcs = rse_lcm_lcs;
        tp_mode = rse_lcm_tp_mode;
        sp_enable = rse_lcm_sp_enable;
        otp_size = 0x00010000;
        otp_writeback = getenv_or("QBOX_RDASPEN_RSE_OTP_WRITEBACK", "false") == "true";
        otp_lock_after_provision =
            getenv_or("QBOX_RDASPEN_RSE_OTP_LOCK_AFTER_PROVISION", "true") == "true";
        target_socket = {
            address = RSE_LCM_BASE_S;
            size = RSE_LCM_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_sam_regs = {
        moduletype = "rse_sam";
        trace = sam_trace;
        trace_limit = sam_trace_limit;
        build_config = 0x00000700;
        target_socket = {
            address = RSE_SAM_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_mpc_vm0_regs = {
        moduletype = "rse_protection_ctrl";
        profile = 1;
        blk_max = 1;
        blk_cfg = 0x00000007;
        target_socket = {
            address = RSE_MPC_VM0_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_mpc_vm1_regs = {
        moduletype = "rse_protection_ctrl";
        profile = 1;
        blk_max = 1;
        blk_cfg = 0x00000007;
        target_socket = {
            address = RSE_MPC_VM1_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_atu_regs = {
        moduletype = "rse_atu";
        trace = atu_trace;
        trace_limit = atu_trace_limit;
        trace_filter = atu_trace_filter;
        trace_address_min = atu_trace_address_min;
        trace_address_max = atu_trace_address_max;
        enable_dmi = atu_dmi;
        build_config = 0x000000C5;
        target_socket = {
            address = RSE_ATU_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        translation_socket = {
            address = RSE_HOST_ACCESS_BASE_NS;
            size = RSE_HOST_ACCESS_SIZE;
            bind = "&rse_router.initiator_socket";
            relative_addresses = false;
            priority = 10;
            aliases = {
                secure = {
                    address = RSE_HOST_ACCESS_BASE_S;
                    size = RSE_HOST_ACCESS_SIZE;
                };
            };
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    },

    rse_sic_regs = {
        moduletype = "rse_protection_ctrl";
        target_socket = {
            address = 0x50140000;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_mpc_sic_regs = {
        moduletype = "rse_protection_ctrl";
        profile = 1;
        blk_max = 127;
        blk_cfg = 0x00000007;
        target_socket = {
            address = 0x50151000;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_cc3xx = (not rse_local_crypto) and
        rse_cc3xx_component("&rse_router.initiator_socket",
                            "&rse_router.target_socket") or nil,

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
        moduletype = "rse_integrity_checker";
        trace = integrity_checker_trace;
        trace_limit = integrity_checker_trace_limit;
        build_config = 0x00000109;
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

    host_ap_shared_sram = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_AP_SHARED_SRAM_PHYS_BASE;
            size = HOST_AP_SHARED_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        map_file = host_ap_shared_sram_map_file;
        init_mem = host_ap_shared_sram_map_file == "";
        load = {data = HOST_AP_SDS_REGION_DATA, offset = 0};
        log_level = 0;
    },

    ap_bl2_reset_loader = enable_ap_cpus and {
        moduletype = "loader";
        load_at_elaboration = false;
        initiator_socket = {bind = "&host_router.target_socket"};
        {
            bin_file = AP_BL2_ELF;
            address = AP_BL2_RESET.data_phys_base;
            bin_file_offset = AP_BL2_RESET.data_elf_offset;
            bin_file_size = AP_BL2_RESET.data_size;
        };
        {
            bin_file = "/dev/zero";
            address = AP_BL2_RESET.stacks_phys_base;
            bin_file_offset = 0;
            bin_file_size = AP_BL2_RESET.stacks_size;
        };
        {
            bin_file = "/dev/zero";
            address = AP_BL2_RESET.bss_phys_base;
            bin_file_offset = 0;
            bin_file_size = AP_BL2_RESET.bss_size;
        };
        {
            bin_file = "/dev/zero";
            address = AP_BL2_RESET.xlat_phys_base;
            bin_file_offset = 0;
            bin_file_size = AP_BL2_RESET.xlat_size;
        };
        {
            address = HOST_AP_SDS_RESET_SYNDROME_PHYS_BASE;
            data = {SDS_RESET_SYNDROME_SYS_RESET_REQ};
        };
        log_level = 0;
    } or nil,

    host_ap_mhu_ns_shared_sram = {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = 0x00180000;
            size = 0x00001000;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    host_ap_bl2_header_sram = {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = HOST_AP_BL2_HEADER_SRAM_PHYS_BASE;
            size = HOST_AP_BL2_HEADER_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        map_file = host_ap_bl2_header_sram_map_file;
        init_mem = host_ap_bl2_header_sram_map_file == "";
        log_level = 0;
    },

    host_ap_flash = {
        moduletype = "strata_flash_j3";
        trace = boot_flash_trace;
        trace_limit = boot_flash_trace_limit;
        enable_dmi = host_memory_dmi;
        dmi_ranges = ap_flash_dmi_ranges;
        program_ff_sets_bits = true;
        program_ff_erases_sector = true;
        size = HOST_AP_FLASH_IMAGE_SIZE;
        sector_size = 0x1000;
        backing_file = flash_writeback and ap_flash or "";
        defer_backing_write = true;
        defer_backing_flush_interval = flash_defer_backing_flush_interval;
        stats_file = ap_flash_stats_file;
        stats_interval = flash_stats_interval;
        target_socket = {
            address = HOST_AP_FLASH_PHY_BASE;
            size = HOST_AP_FLASH_IMAGE_SIZE;
            bind = "&host_router.initiator_socket";
        };
        load = {bin_file = ap_flash, offset = 0};
        log_level = 0;
    },

    host_ap_trusted_nvctr = {
        moduletype = "gs_memory";
        read_only = true;
        target_socket = {
            address = HOST_AP_TRUSTED_NVCTR_BASE;
            size = HOST_AP_TRUSTED_NVCTR_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        load = {data = HOST_AP_TRUSTED_NVCTR_DATA, offset = 0};
        log_level = 0;
    },

    host_ap_dram1 = enable_ap_cpus and {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = HOST_AP_DRAM1_BASE;
            size = HOST_AP_DRAM1_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    } or nil,

    host_ap_ffa_mm_comm_buffer = enable_ap_cpus and {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = 0xFFBF0000;
            size = 0x00002000;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    } or nil,

    host_ap_spmc_sdram = enable_ap_cpus and {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = HOST_AP_SPMC_BASE;
            size = HOST_AP_SPMC_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    } or nil,

    host_ap_dram2 = enable_ap_cpus and {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = HOST_AP_DRAM2_BASE;
            size = HOST_AP_DRAM2_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    } or nil,

    ap_gic = enable_ap_cpus and {
        moduletype = "arm_gicv3";
        args = {"&platform.ap_qemu_inst"};
        dist_iface = {
            address = AP_GIC_DIST_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
            aliases = {
                optee_secure_view = {
                    address = AP_GIC_LEGACY_DIST_BASE;
                    size = 0x00010000;
                };
            };
        };
        num_cpus = AP_GIC_NUM_CPUS;
        redist_region = repeat_value(1, AP_GIC_ACTIVE_REDIST_REGIONS);
        has_security_extensions = true;
        has_lpi = true;
        num_spi = 512;
    } or nil,

    ap_gic_its = enable_ap_cpus and {
        moduletype = "arm_gicv3_its";
        args = {"&platform.ap_qemu_inst", "&platform.ap_gic"};
        mem = {
            address = 0x20840000;
            size = 0x00040000;
            bind = "&host_router.initiator_socket";
        };
    } or nil,

    ap_smmu_0 = enable_ap_cpus and ap_smmu_component() or nil,

    ap_virtioblk_0 = enable_ap_cpus and {
        moduletype = "virtio_mmio_blk";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.block_base[1];
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.block_irq[1]};
        blkdev_str = "file="..ap_virtio.disk_image..",format=raw,if=none,cache=writeback";
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_virtioblk_1 = enable_ap_cpus and {
        moduletype = "virtio_mmio_blk";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.block_base[2];
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.block_irq[2]};
        blkdev_str = "file="..ap_virtio.extra_disk_images[1]..",format=raw,if=none,cache=writeback";
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_virtioblk_2 = enable_ap_cpus and {
        moduletype = "virtio_mmio_blk";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.block_base[3];
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.block_irq[3]};
        blkdev_str = "file="..ap_virtio.extra_disk_images[2]..",format=raw,if=none,cache=writeback";
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_virtioblk_3 = enable_ap_cpus and {
        moduletype = "virtio_mmio_blk";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.block_base[4];
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.block_irq[4]};
        blkdev_str = "file="..ap_virtio.extra_disk_images[3]..",format=raw,if=none,cache=writeback";
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_virtionet_0 = enable_ap_cpus and {
        moduletype = "virtio_mmio_net";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.net_base;
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.net_irq};
        netdev_str = ap_virtio.netdev;
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_virtiorng_0 = enable_ap_cpus and {
        moduletype = "virtio_mmio_rng";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = ap_virtio.rng_base;
            size = ap_virtio.mmio_size;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_"..ap_virtio.rng_irq};
        trace = ap_virtio.trace;
        trace_file = ap_virtio.trace_file;
        trace_limit = ap_virtio.trace_limit;
        trace_filter = ap_virtio.trace_filter;
    } or nil,

    ap_rtc_0 = enable_ap_cpus and {
        moduletype = "pl031";
        args = {"&platform.ap_qemu_inst"};
        mem = {
            address = 0x300D0000;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_268"};
    } or nil,

    ap_watchdog_0 = enable_ap_cpus and {
        moduletype = "sbsa_gwdt";
        args = {"&platform.ap_qemu_inst"};
        refresh_mem = {
            address = 0x1A420000;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        control_mem = {
            address = 0x1A430000;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq_out = {bind = "&ap_gic.spi_in_50"};
    } or nil,

    ap_secure_console_file = enable_ap_cpus and {
        moduletype = "char_backend_file";
        read_file = "/dev/null";
        write_file = secure_console_log;
        baudrate = 0;
    } or nil,

    ap_primary_console_file = enable_ap_cpus and {
        moduletype = "char_backend_file";
        read_file = primary_uart_read_file;
        write_file = primary_console_log;
        poll_read = primary_uart_poll_read;
        poll_interval_ms = tonumber(getenv_or(
            "QBOX_RDASPEN_PRIMARY_UART_POLL_INTERVAL_MS",
            "100"));
        baudrate = 0;
    } or nil,

    ap_secure_uart = enable_ap_cpus and {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = AP_SECURE_UART_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq = {bind = "&ap_gic.spi_in_"..AP_SECURE_UART_IRQ};
        backend_socket = {bind = "&ap_secure_console_file.biflow_socket"};
    } or nil,

    ap_primary_uart = enable_ap_cpus and {
        moduletype = "Pl011";
        dylib_path = "uart-pl011";
        target_socket = {
            address = AP_PRIMARY_UART_BASE;
            size = 0x00010000;
            bind = "&host_router.initiator_socket";
        };
        irq = {bind = "&ap_gic.spi_in_"..AP_PRIMARY_UART_IRQ};
        backend_socket = {bind = "&ap_primary_console_file.biflow_socket"};
    } or nil,

    ap_timer_mem = enable_ap_cpus and {
        moduletype = "qemu_hexagon_qtimer";
        args = {"&platform.ap_qemu_inst"};
        nr_frames = 1;
        nr_views = 1;
        cnttid = 0x1;
        mem = {
            address = AP_SYS_TIMCTL_BASE;
            size = AP_SYS_TIMER_SIZE;
            bind = "&host_router.initiator_socket";
        };
        mem_view = {
            address = AP_SYS_CNT_BASE_NS;
            size = AP_SYS_TIMER_SIZE;
            bind = "&host_router.initiator_socket";
        };
        irq = {
            {bind = "&ap_gic.spi_in_"..AP_SYS_TIMER_IRQ};
        };
    } or nil,

    -- RD-Aspen AP BL2 refreshes the secure SBSA watchdog in panic/error paths.
    -- Keep the window mapped so watchdog access does not hide the original
    -- secure-world failure while a fuller watchdog model is still pending.
    ap_secure_wdog = enable_ap_cpus and {
        moduletype = "gs_memory";
        target_socket = {
            address = AP_SECURE_WDOG_BASE;
            size = AP_SECURE_WDOG_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    } or nil,

    host_si_cl0_sram = {
        moduletype = "gs_memory";
        dmi_allow = host_si_sram_dmi;
        target_socket = {
            address = HOST_SI_CL0_SRAM_PHYS_BASE;
            size = HOST_SI_SRAM_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        map_file = host_si_cl0_sram_map_file;
        init_mem = host_si_cl0_sram_map_file == "";
        log_level = 0;
    },

    host_si_cl1_sram = {
        moduletype = "gs_memory";
        dmi_allow = host_si_sram_dmi;
        target_socket = {
            address = HOST_SI_CL1_SRAM_PHYS_BASE;
            size = HOST_SI_SRAM_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        map_file = host_si_cl1_sram_map_file;
        init_mem = host_si_cl1_sram_map_file == "";
        log_level = 0;
    },

    host_si_cl0_cub = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_SI_CL0_CL_UTIL_BASE;
            size = HOST_SI_CL_UTIL_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 20;
        };
        init_mem = true;
        log_level = 0;
    },

    host_si_cl1_cub = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_SI_CL1_CL_UTIL_BASE;
            size = HOST_SI_CL_UTIL_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 20;
        };
        init_mem = true;
        log_level = 0;
    },

    host_si_pik = {
        moduletype = "host_ppu";
        trace = host_ppu_trace;
        trace_limit = host_ppu_trace_limit;
        target_socket = {
            address = HOST_SI_PIK_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    },

    host_si_scr = {
        moduletype = "host_scr";
        cl1_present = true;
        target_socket = {
            address = HOST_SI_SCR_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    },

    host_si_atu = {
        moduletype = "rse_atu";
        trace = atu_trace;
        trace_limit = atu_trace_limit;
        trace_filter = atu_trace_filter;
        trace_address_min = atu_trace_address_min;
        trace_address_max = atu_trace_address_max;
        build_config = 0x000000C5;
        target_socket = {
            address = HOST_SI_ATU_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    },

    host_si_cl0_clus_ppu = {
        moduletype = "host_ppu";
        trace = host_ppu_trace;
        trace_limit = host_ppu_trace_limit;
        target_socket = {
            address = HOST_SI_CL0_CL_UTIL_BASE + HOST_SI_CLUS_PPU_OFFSET;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    },

    host_si_cl0_core0_ppu = {
        moduletype = "host_ppu";
        trace = host_ppu_trace;
        trace_limit = host_ppu_trace_limit;
        target_socket = {
            address = HOST_SI_CL0_CL_UTIL_BASE + HOST_SI_CORE0_PPU_OFFSET;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    },

    host_si_cl1_clus_ppu = {
        moduletype = "host_ppu";
        trace = host_ppu_trace;
        trace_limit = host_ppu_trace_limit;
        target_socket = {
            address = HOST_SI_CL1_CL_UTIL_BASE + HOST_SI_CLUS_PPU_OFFSET;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
            priority = 10;
        };
        log_level = 0;
    },

    host_rse_si_mhu_pbx = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "rse_si_cl0";
        protocol = "scmi";
        scmi_transport = "rse-bl2";
        tx_shmem = HOST_RSE_SI_SSRAM_PHYS_BASE;
        rx_shmem = HOST_RSE_SI_SSRAM_PHYS_BASE;
        init_shmem = false;
        ack_bit = 1;
        assert_power_on_reset = true;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_RSE_SI_MHU_PHYS_BASE;
            size = RSE_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        power_on_reset = enable_ap_cpus and {bind = "&ap_cpu_0.reset"} or nil;
        log_level = 0;
    },

    host_rse_si_mhu_mbx = {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "rse_si_cl0";
        protocol = "scmi";
        scmi_transport = "rse-bl2";
        tx_shmem = HOST_RSE_SI_SSRAM_PHYS_BASE;
        rx_shmem = HOST_RSE_SI_SSRAM_PHYS_BASE;
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_RSE_SI_MHU_PHYS_BASE + RSE_MHU_FRAME_SIZE;
            size = RSE_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&rse_cpu_pass.target_signal_socket_"..
            RSE_IRQ_SI_CL0_RSE_CMU_MHU_RECEIVER};
        log_level = 0;
    },

    host_rse_si_ssram = {
        moduletype = "gs_memory";
        dmi_allow = host_memory_dmi;
        target_socket = {
            address = HOST_RSE_SI_SSRAM_PHYS_BASE;
            size = HOST_RSE_SI_SSRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    host_ap_atu = {
        moduletype = "rse_atu";
        trace = atu_trace;
        trace_limit = atu_trace_limit;
        trace_filter = atu_trace_filter;
        trace_address_min = atu_trace_address_min;
        trace_address_max = atu_trace_address_max;
        enable_dmi = enable_ap_cpus and atu_dmi;
        build_config = 0x000000C5;
        target_socket = {
            address = HOST_AP_ATU_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        translation_socket = enable_ap_cpus and {
            address = HOST_AP_ATU_LOGICAL_BASE;
            size = HOST_AP_ATU_LOGICAL_SIZE;
            bind = "&host_router.initiator_socket";
            relative_addresses = false;
            priority = 10;
        } or nil;
        initiator_socket = enable_ap_cpus and {bind = "&host_router.target_socket"} or nil;
        log_level = 0;
    },

    host_ap_si_ns_scmi_mhu_pbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "ap_si_ns_scmi";
        protocol = "scmi";
        tx_shmem = 0x00180000;
        rx_shmem = 0x00180100;
        init_shmem = true;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = 0x400003B000000;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_"..AP_SI_SCMI_MHU_PBX_IRQ};
        log_level = 0;
    } or nil,

    host_ap_si_ns_scmi_mhu_mbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "ap_si_ns_scmi";
        protocol = "scmi";
        tx_shmem = 0x00180000;
        rx_shmem = 0x00180100;
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = 0x400003B040000;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_"..AP_SI_SCMI_MHU_MBX_IRQ};
        log_level = 0;
    } or nil,

    host_ap_si_scmi_mhu_pbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "ap_si_scmi";
        protocol = "scmi";
        tx_shmem = HOST_AP_SCMI_PAYLOAD_BASE;
        rx_shmem = HOST_AP_SCMI_PAYLOAD_BASE;
        init_shmem = true;
        power_domain_reset_count = AP_NUM_CPUS;
        power_domain_reset_delay_ns = ap_power_domain_reset_delay_ns;
        power_domain_reset_assert_on_power_off = false;
        power_domain_reset_pulse_on_power_on = true;
        system_power_reset_delay_ns = 1;
        system_power_reset_pulse_width_ns = 1;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_AP_SI_SCMI_MHU_PBX_PHYS_BASE;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_"..AP_SI_SCMI_MHU_PBX_IRQ};
        system_reset = {bind = ap_system_reset_bind_targets()};
        power_domain_reset_1 = {bind = "&ap_cpu_1.reset"};
        power_domain_reset_2 = {bind = "&ap_cpu_2.reset"};
        power_domain_reset_3 = {bind = "&ap_cpu_3.reset"};
        log_level = 0;
    } or nil,

    host_ap_si_scmi_mhu_mbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "ap_si_scmi";
        protocol = "scmi";
        tx_shmem = HOST_AP_SCMI_PAYLOAD_BASE;
        rx_shmem = HOST_AP_SCMI_PAYLOAD_BASE;
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_AP_SI_SCMI_MHU_MBX_PHYS_BASE;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_"..AP_SI_SCMI_MHU_MBX_IRQ};
        log_level = 0;
    } or nil,

    host_ap_si_cl1_mhu_pbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "ap_si_cl1";
        protocol = "doorbell";
        doorbell_ack_trigger_channel = 0;
        doorbell_ack_trigger_value = 0x8;
        doorbell_ack_channel = 0;
        doorbell_ack_value = 0x4;
        -- si_cl1_rproc_rsctbl@0x00100000 from the AP DTS HIPC layout.
        doorbell_ack_seed_address = 0x00100000;
        doorbell_ack_seed_words = {
            0x00000001, 0x00000001, 0x00000000, 0x00000000,
            0x00000014, 0x00000003, 0x00000007, 0x00000000,
            0x00000001, 0x00000000, 0x00000000, 0x00000200,
            0xffffffff, 0x00000010, 0x00000020, 0x00000000,
            0x00000000, 0xffffffff, 0x00000010, 0x00000020,
            0x00000001, 0x00000000,
        };
        rpmsg_ns_enable = true;
        rpmsg_ns_name = "ethsi1";
        rpmsg_ns_remote_addr = 0x400;
        -- si_cl1_vdev0vring0@0x00120000 from the AP DTS HIPC layout.
        rpmsg_ns_vring_address = 0x00120000;
        rpmsg_ns_vring_num = 0x20;
        rpmsg_ns_vring_align = 0x10;
        rpmsg_ns_signal_channel = 0;
        rpmsg_ns_signal_value = 0x1;
        rpmsg_ns_signal_delay_ns = 1000000;
        rpmsg_ns_poll_period_ns = 100000;
        rpmsg_ns_max_polls = 10000;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = 0x400003B100000;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_120"};
        log_level = 0;
    } or nil,

    host_ap_si_cl1_mhu_mbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "ap_si_cl1";
        protocol = "doorbell";
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = 0x400003B140000;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&ap_gic.spi_in_121"};
        log_level = 0;
    } or nil,

    host_ap_si_pfdi_monitor_mhu_pbx = enable_ap_cpus and {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "ap_si_pfdi_monitor";
        protocol = "scmi";
        scmi_transport = "pfdi-monitor";
        tx_shmem = HOST_AP_SCMI_PFDI_MONITOR_BASE;
        rx_shmem = HOST_AP_SCMI_PFDI_MONITOR_BASE;
        scmi_channel_stride = HOST_AP_SCMI_PFDI_MONITOR_STRIDE;
        scmi_channel_count = HOST_AP_SCMI_PFDI_MONITOR_CHANNELS;
        init_shmem = true;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_AP_SI_PFDI_MONITOR_MHU_PBX_PHYS_BASE;
            size = HOST_AP_SI_MHU_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    } or nil,

    host_smdexp2smd_atu = {
        moduletype = "rse_atu";
        trace = atu_trace;
        trace_limit = atu_trace_limit;
        trace_filter = atu_trace_filter;
        trace_address_min = atu_trace_address_min;
        trace_address_max = atu_trace_address_max;
        build_config = 0x000000C5;
        target_socket = {
            address = HOST_SMDEXP2SMD_ATU_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        log_level = 0;
    },

    host_systop_pik = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_SYSTOP_PIK_PHYS_BASE;
            size = HOST_SI_CONTROL_WINDOW_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    host_css_counters_timers = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_CSS_COUNTERS_TIMERS_PHYS_BASE;
            size = HOST_CSS_COUNTERS_TIMERS_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    host_smcf_sram = {
        moduletype = "gs_memory";
        target_socket = {
            address = HOST_SMCF_SRAM_PHYS_BASE;
            size = HOST_SMCF_SRAM_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    host_ap_rse_mhu_pbx = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "ap_s_to_rse";
        protocol = "doorbell-bridge";
        tx_shmem = HOST_AP_RSE_MAILBOX_PHYS_BASE;
        rx_shmem = HOST_AP_RSE_MAILBOX_PHYS_BASE;
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_AP_RSE_MHU_PHYS_BASE;
            size = MHU_V3_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    },

    host_ap_rse_mhu_mbx = {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "rse_to_ap_s";
        protocol = "doorbell-bridge";
        tx_shmem = HOST_AP_RSE_MAILBOX_PHYS_BASE;
        rx_shmem = HOST_AP_RSE_MAILBOX_PHYS_BASE;
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = HOST_AP_RSE_MHU_PHYS_BASE + MHU_V3_FRAME_SIZE;
            size = MHU_V3_FRAME_SIZE;
            bind = "&host_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    },

    host_ap_rse_mailbox = {
        moduletype = "gs_memory";
        target_socket = {
            -- The MHU outband mailbox at 0xffffc000 sits inside the larger
            -- AP MHU pointer-access window used by TF-M SFCP requests.
            address = HOST_AP_MHU_POINTER_ACCESS_PHYS_BASE;
            size = HOST_AP_MHU_POINTER_ACCESS_SIZE;
            bind = "&host_router.initiator_socket";
        };
        init_mem = true;
        log_level = 0;
    },

    rse_mhu0_sender_s = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "rse_ap_monitor_local";
        protocol = "doorbell";
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = RSE_MHU0_SENDER_BASE_S;
            size = RSE_LOCAL_MHU_FRAME_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    },

    rse_mhu0_receiver_s = {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "rse_ap_monitor_local";
        protocol = "doorbell";
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = RSE_MHU0_RECEIVER_BASE_S;
            size = RSE_LOCAL_MHU_FRAME_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&rse_cpu_pass.target_signal_socket_"..
            RSE_IRQ_CMU_MHU0_RECEIVER};
        log_level = 0;
    },

    rse_mhu2_sender_s = {
        moduletype = "mhu320ae";
        frame = "pbx";
        pair = "rse_to_ap_s";
        protocol = "doorbell-bridge";
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = RSE_MHU2_SENDER_BASE_S;
            size = RSE_LOCAL_MHU_FRAME_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        log_level = 0;
    },

    rse_mhu2_receiver_s = {
        moduletype = "mhu320ae";
        frame = "mbx";
        pair = "ap_s_to_rse";
        protocol = "doorbell-bridge";
        init_shmem = false;
        trace = mhu_trace;
        trace_limit = mhu_trace_limit;
        trace_file = mhu_trace_file;
        target_socket = {
            address = RSE_MHU2_RECEIVER_BASE_S;
            size = RSE_LOCAL_MHU_FRAME_SIZE;
            bind = "&rse_router.initiator_socket";
        };
        initiator_socket = {bind = "&host_router.target_socket"};
        irq = {bind = "&rse_cpu_pass.target_signal_socket_"..
            RSE_IRQ_CMU_MHU2_RECEIVER};
        log_level = 0;
    },

    rse_sysctrl = {
        moduletype = "rse_sysctrl";
        trace = sysctrl_trace;
        trace_limit = sysctrl_trace_limit;
        reset_syndrome = rse_reset_syndrome;
        cpuwait = rse_cpuwait;
        dma_boot_en = rse_dma_boot_en;
        dma_boot_addr = rse_dma_boot_addr;
        target_socket = {
            address = RSE_SYSCTRL_BASE_S;
            size = 0x00001000;
            bind = "&rse_router.initiator_socket";
        };
        log_level = 0;
    },

    rse_integ_layer_regs = {
        moduletype = "gs_memory";
        target_socket = {
            address = RSE_INTEG_LAYER_BASE_S;
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
            aliases = {
                ns_atu_logical = {
                    address = RSE_HOST_UART0_BASE_NS;
                    size = 0x00010000;
                };
            };
        };
        irq = {bind = "&rse_cpu_pass.target_signal_socket_0"};
        backend_socket = {bind = "&rse_uart_file.biflow_socket"};
    },

    rse_cpu_pass = {
        moduletype = "RemotePass";
        exec_path = remote_cpu_exec;
        remote_argv = {
            "--param",
            "log_level=0",
            "--param",
            "remote_platform.quantum_ns="..
                tonumber(getenv_or("QBOX_RDASPEN_RSE_REMOTE_QUANTUM_NS", "1000000")),
        };
        tlm_initiator_ports_num = 2;
        tlm_target_ports_num = 0;
        target_signals_num = RSE_REMOTE_SIGNAL_COUNT;
        initiator_signals_num = 0;
        dmi_cache = remotepass_dmi_cache;
        initiator_socket_0 = {bind = "&rse_router.target_socket"};
        initiator_socket_1 = {bind = "&rse_router.target_socket"};

        remote_main_router = rse_local_peripherals and {
            moduletype = "router";
            target_socket = {
                address = 0x00000000;
                size = RSE_NVIC_BASE;
                bind = "&cpu_0.router.initiator_socket";
                relative_addresses = false;
                priority = 100;
                aliases = {
                    post_nvic = {
                        address = RSE_NVIC_BASE + RSE_NVIC_SIZE;
                        size = 0x00100000;
                        priority = 100;
                    };
                };
            };
            log_level = 0;
        } or nil,

        remote_crypto_router = rse_local_crypto and {
            moduletype = "router";
            target_socket = {
                address = RSE_CC3XX_BASE_S;
                size = 0x00002000;
                bind = "&cpu_0.router.initiator_socket";
                relative_addresses = false;
            };
            log_level = 0;
        } or nil,

        plugin_pass = {
            moduletype = "RemotePass";
            tlm_initiator_ports_num = 0;
            tlm_target_ports_num = 2;
            target_signals_num = 0;
            initiator_signals_num = RSE_REMOTE_SIGNAL_COUNT;
            dmi_cache = remotepass_dmi_cache;
            target_socket_0 = {
                address = 0x00000000;
                size = RSE_NVIC_BASE;
                bind = rse_local_peripherals and
                    "&remote_main_router.initiator_socket" or
                    "&cpu_0.router.initiator_socket";
            };
            target_socket_1 = {
                address = RSE_NVIC_BASE + RSE_NVIC_SIZE;
                size = 0x00100000;
                bind = rse_local_peripherals and
                    "&remote_main_router.initiator_socket" or
                    "&cpu_0.router.initiator_socket";
            };
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

        rse_boot_flash = rse_local_boot_flash and {
            moduletype = "strata_flash_j3";
            trace = boot_flash_trace;
            trace_limit = boot_flash_trace_limit;
            enable_dmi = boot_flash_dmi;
            dmi_ranges = boot_flash_dmi_ranges;
            program_ff_sets_bits = true;
            program_ff_erases_sector = true;
            size = RSE_BOOT_FLASH_SIZE;
            sector_size = 0x1000;
            backing_file = flash_writeback and rse_flash or "";
            defer_backing_write = true;
            defer_backing_flush_interval = flash_defer_backing_flush_interval;
            stats_file = rse_boot_flash_stats_file;
            stats_interval = flash_stats_interval;
            target_socket = {
                address = RSE_BOOT_FLASH_BASE_S;
                size = RSE_BOOT_FLASH_SIZE;
                bind = "&cpu_0.router.initiator_socket";
            };
            load = {bin_file = rse_flash, offset = 0};
            log_level = 0;
        } or nil,

        rse_kmu_regs = rse_local_crypto and {
            moduletype = "rse_kmu";
            trace = kmu_trace;
            trace_limit = kmu_trace_limit;
            trace_filter = kmu_trace_filter;
            otp_image = rse_otp;
            build_config = 0x003D0005;
            hw_slot_config = 0x00D60100;
            hw_slot_export_address = 0x50154400;
            target_socket = {
                address = RSE_KMU_BASE_S;
                size = 0x00001000;
                bind = "&cpu_0.router.initiator_socket";
            };
            initiator_socket = {bind = "&remote_crypto_router.target_socket"};
            log_level = 0;
        } or nil,

        rse_cc3xx = rse_local_crypto and
            rse_cc3xx_component("&remote_crypto_router.initiator_socket",
                                "&remote_main_router.target_socket") or nil,

        cpu_0 = {
            moduletype = "RemoteCPU";
            args = {"&qemu_inst"};
            cpu = {
                init_svtor = RSE_ROM_BASE_S;
                init_nsvtor = RSE_ROM_BASE_S;
                start_powered_off = false;
                trace_pc = rse_pc_trace;
                trace_exception_state = rse_exception_trace;
                trace_pc_file = rse_pc_trace_file;
                trace_pc_interval = rse_pc_trace_interval;
                trace_pc_limit = rse_pc_trace_limit;
                hotpath_accel = rse_hotpath_accel;
                hotpath_memcpy_addr = rse_hotpath_memcpy_addr;
                hotpath_memset_addr = rse_hotpath_memset_addr;
                hotpath_max_bytes = rse_hotpath_max_bytes;
                hotpath_profile_file = rse_hotpath_profile_file;
                hotpath_profile_interval = rse_hotpath_profile_interval;
                lms_accel = rse_lms_accel;
                lms_verify_addr = rse_lms_verify_addr;
                lms_max_data_bytes = rse_lms_max_data_bytes;
                bl2_load_profile = rse_bl2_load_profile;
                bl2_boot_go_for_image_id_addr = rse_bl2_boot_go_for_image_id_addr;
                bl2_boot_load_image_to_sram_addr = rse_bl2_boot_load_image_to_sram_addr;
                bl2_boot_enc_load_addr = rse_bl2_boot_enc_load_addr;
                bl2_boot_enc_set_key_addr = rse_bl2_boot_enc_set_key_addr;
                bl2_boot_enc_decrypt_addr = rse_bl2_boot_enc_decrypt_addr;
                bl2_bootutil_img_validate_addr = rse_bl2_bootutil_img_validate_addr;
                bl2_bootutil_img_hash_addr = rse_bl2_bootutil_img_hash_addr;
                bl2_bootutil_verify_sig_addr = rse_bl2_bootutil_verify_sig_addr;
                bl2_boot_image_count = rse_bl2_boot_image_count;
                bl2_boot_state_curr_img_offset = rse_bl2_boot_state_curr_img_offset;
                bl2_boot_state_imgs_offset = rse_bl2_boot_state_imgs_offset;
                bl2_boot_state_image_stride = rse_bl2_boot_state_image_stride;
                bl2_boot_state_slot_stride = rse_bl2_boot_state_slot_stride;
                bl2_boot_state_slot_usage_offset = rse_bl2_boot_state_slot_usage_offset;
                bl2_boot_state_slot_usage_stride = rse_bl2_boot_state_slot_usage_stride;
                bl2_boot_slot_usage_img_dst_offset = rse_bl2_boot_slot_usage_img_dst_offset;
                bl2_boot_slot_usage_img_sz_offset = rse_bl2_boot_slot_usage_img_sz_offset;
                bl2_load_accel = rse_bl2_load_accel;
                bl2_load_accel_max_bytes = rse_bl2_load_accel_max_bytes;
                bl2_boot_enc_accel = rse_bl2_boot_enc_accel;
                bl2_boot_status_enckey_offset = rse_bl2_boot_status_enckey_offset;
                bl2_boot_enc_key_bytes = rse_bl2_boot_enc_key_bytes;
                bl2_boot_enc_key_stride = rse_bl2_boot_enc_key_stride;
                bl2_boot_enc_slots = rse_bl2_boot_enc_slots;
                bl2_boot_enc_max_bytes = rse_bl2_boot_enc_max_bytes;
                bl2_img_hash_accel = rse_bl2_img_hash_accel;
                bl2_img_hash_max_bytes = rse_bl2_img_hash_max_bytes;
                bl2_img_hash_max_seed_bytes = rse_bl2_img_hash_max_seed_bytes;
                bl2_verify_sig_accel = rse_bl2_verify_sig_accel;
                bl2_verify_sig_skip = rse_bl2_verify_sig_skip;
                bl2_bootutil_keys_addr = rse_bl2_bootutil_keys_addr;
                bl2_bootutil_key_cnt_addr = rse_bl2_bootutil_key_cnt_addr;
                bl2_fih_success_addr = rse_bl2_fih_success_addr;
                bl2_verify_sig_max_key_bytes = rse_bl2_verify_sig_max_key_bytes;
                bl2_verify_sig_max_sig_bytes = rse_bl2_verify_sig_max_sig_bytes;
                bl2_delay_accel = rse_bl2_delay_accel;
                bl2_delay_cycles_addr = rse_bl2_delay_cycles_addr;
                bl2_delay_max_cycles = rse_bl2_delay_max_cycles;
                bl2_delay_expected_hits = rse_bl2_delay_expected_hits;
                direct_file_aliases = rse_direct_file_aliases;
                nvic = {
                    mem = {
                        address = RSE_NVIC_BASE;
                        size = RSE_NVIC_SIZE;
                    };
                    num_irq = RSE_NVIC_NUM_IRQ;
                };
            };
        };
    },
}

print("rse rom:      "..rse_rom)
print("rse flash:    "..rse_flash)
print("rse otp:      "..rse_otp)
print("ap flash:     "..ap_flash)
print("ap bl2 elf:   "..AP_BL2_ELF)
print("provisioning: "..provisioning_bundle)
print("rse log:      "..rse_log)
print("secure log:   "..secure_console_log)
print("primary log:  "..primary_console_log)
print("remote cpu:   "..remote_cpu_exec)
print("ap cpus:      "..tostring(AP_NUM_CPUS))
print("rse rom base: 0x"..string.format("%x", RSE_ROM_BASE_S))
print("rse vmaddrwidth: "..tostring(rse_vmaddrwidth))
print("rse vm size:  0x"..string.format("%x", RSE_VM_SIZE))

for irq=0,(RSE_NVIC_NUM_IRQ-1) do
    platform.rse_cpu_pass.plugin_pass["initiator_signal_socket_"..irq] = {
        bind = "&cpu_0.cpu.nvic.irq_in_"..irq;
    }
end

if enable_ap_cpus then
    for i=0,(AP_GIC_ACTIVE_REDIST_REGIONS-1) do
        platform["ap_gic"]["redist_iface_"..i] = {
            address = AP_GIC_REDIST_BASE + (i * AP_GIC_REDIST_SIZE);
            size = AP_GIC_REDIST_SIZE;
            bind = "&host_router.initiator_socket";
            aliases = {
                optee_secure_view = {
                    address = AP_GIC_LEGACY_REDIST_BASE + (i * AP_GIC_LEGACY_REDIST_SIZE);
                    size = AP_GIC_LEGACY_REDIST_SIZE;
                };
            };
        }
    end

    for i=AP_GIC_ACTIVE_REDIST_REGIONS,(AP_GIC_REDIST_REGIONS-1) do
        platform["ap_gicr_reserved_"..i] = {
            moduletype = "gs_memory";
            target_socket = {
                address = AP_GIC_REDIST_BASE + (i * AP_GIC_REDIST_SIZE);
                size = AP_GIC_REDIST_SIZE;
                bind = "&host_router.initiator_socket";
            };
            dmi_allow = false;
            log_level = 0;
        }
    end

    for i=0,(AP_NUM_CPUS-1) do
        local cpu = {
            moduletype = "cpu_arm_cortexA720AE";
            args = {"&platform.ap_qemu_inst"};
            mem = {bind = "&host_router.target_socket"};
            has_el3 = true;
            has_el2 = true;
            irq_timer_phys_out = {
                bind = "&ap_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL1_IRQ;
            };
            irq_timer_virt_out = {
                bind = "&ap_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_VIRT_IRQ;
            };
            irq_timer_hyp_out = {
                bind = "&ap_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL2_IRQ;
            };
            irq_timer_sec_out = {
                bind = "&ap_gic.ppi_in_cpu_"..i.."_"..ARCH_TIMER_S_EL1_IRQ;
            };
            gicv3_maintenance_interrupt = {
                bind = "&ap_gic.ppi_in_cpu_"..i.."_25";
            };
            pmu_interrupt = {bind = "&ap_gic.ppi_in_cpu_"..i.."_23"};
            psci_conduit = getenv_or("QBOX_RDASPEN_AP_PSCI_CONDUIT", "disabled");
            mp_affinity = mp_affinity(i);
            start_powered_off = i ~= 0;
            start_in_reset = true;
            reset_power_on = true;
            rvbar = HOST_AP_BL2_PHYS_BASE;
            trace_pc = ap_pc_trace;
            trace_pc_file = ap_pc_trace_file;
            trace_pc_interval = ap_pc_trace_interval;
            trace_pc_limit = ap_pc_trace_limit;
            trace_exception_state = ap_exception_trace;
        }
        platform["ap_cpu_"..tostring(i)] = cpu

        platform["ap_gic"]["irq_out_"..i] = {bind = "&ap_cpu_"..i..".irq_in"}
        platform["ap_gic"]["fiq_out_"..i] = {bind = "&ap_cpu_"..i..".fiq_in"}
        platform["ap_gic"]["virq_out_"..i] = {bind = "&ap_cpu_"..i..".virq_in"}
        platform["ap_gic"]["vfiq_out_"..i] = {bind = "&ap_cpu_"..i..".vfiq_in"}
    end
end

return {}
