local system_mgmt = {}

system_mgmt.ownership = {
    reset = {
        "reset_gpio";
        "reset_fanout";
        "host_ppu";
        "mhu320ae";
    };
    shared_memory = {
        "host_rse_si_ssram";
        "host_smcf_sram";
        "si_cl0_smd_shared_sram";
    };
    messaging = {
        "host_ap_rse_mhu_pbx";
        "host_ap_rse_mhu_mbx";
        "host_rse_si_mhu_pbx";
        "host_rse_si_mhu_mbx";
        "host_ap_si_pfdi_monitor_mhu_pbx";
        "si_cl1_pfdi_mhu_pbx";
    };
    translation = {
        "rse_atu_regs";
        "host_si_atu";
        "host_ap_atu";
        "host_smdexp2smd_atu";
    };
    safety_control = {
        "host_smcf_mgi";
        "si_cl0_ssu";
        "si_cl0_fmu";
        "host_scr";
        "host_system_pll";
        "host_gtimer";
    };
}

function system_mgmt.add_ap_logical_mhu_aliases(platform)
    local AP_RSE_SECURE_MHU_PBX_LOGICAL_BASE = 0x40680000
    local AP_RSE_SECURE_MHU_MBX_LOGICAL_BASE = 0x406B0000
    local AP_LOGICAL_MHU_FRAME_SIZE = 0x00030000

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
end

function system_mgmt.prepare_live_cl0_integration(ctx, platform)
    if platform.host_ap_flash ~= nil then
        ctx.lower_decode_priority(platform.host_ap_flash.target_socket, 10)
    end
    if platform.ap_gpex_0 ~= nil then
        ctx.lower_decode_priority(platform.ap_gpex_0.ecam_iface, 10)
    end
    if platform.host_ap_dram1 ~= nil then
        ctx.lower_decode_priority(platform.host_ap_dram1.target_socket, 10)
    end
    ctx.ros.lower_decode_priorities(platform, ctx.lower_decode_priority, 10)

    ctx.ap_compute.enable_ap_view_router(ctx, platform)

    system_mgmt.add_ap_logical_mhu_aliases(platform)
end

return system_mgmt
