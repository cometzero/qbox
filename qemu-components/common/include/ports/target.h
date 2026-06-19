/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_PORTS_TARGET_H
#define _LIBQBOX_PORTS_TARGET_H

#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <cci_configuration>
#include <tlm>

#include "qemu-instance.h"
#include "tlm-extensions/qemu-cpu-hint.h"
#include "tlm-extensions/qemu-memtx-attrs.h"
#include "tlm-extensions/qemu-mr-hint.h"
#include <tlm_sockets_buswidth.h>

class TlmTargetToQemuBridge : public tlm::tlm_fw_transport_if<>
{
public:
    using MemTxAttrs = qemu::MemoryRegion::MemTxAttrs;
    using MemTxResult = qemu::MemoryRegion::MemTxResult;
    using TlmPayload = tlm::tlm_generic_payload;

protected:
    qemu::MemoryRegion m_mr;
    std::shared_ptr<qemu::AddressSpace> m_as;
    bool m_trace_enabled = false;
    uint64_t m_trace_count = 0;
    uint64_t m_trace_limit = 0;
    std::string m_trace_name;
    std::string m_trace_file;
    std::string m_trace_filter;
    std::ofstream m_trace_stream;
    std::mutex m_trace_lock;
    bool m_mirror_4k_aperture = false;
    bool m_mirror_4k_writes = false;

    void init_as()
    {
        m_as = m_mr.get_inst().address_space_new();
        m_as->init(m_mr, "qemu-target-socket");
    }

    qemu::Cpu push_current_cpu(TlmPayload& trans)
    {
        qemu::Cpu ret;
        QemuCpuHintTlmExtension* ext = nullptr;

        trans.get_extension(ext);

        if (ext == nullptr) {
            /* return an invalid object */
            return ret;
        }

        qemu::Cpu initiator(ext->get_cpu());

        if (initiator.get_inst_id() != m_mr.get_inst_id()) {
            /* return an invalid object */
            return ret;
        }

        ret = initiator.set_as_current();

        return ret;
    }

    void pop_current_cpu(qemu::Cpu cpu)
    {
        if (!cpu.valid()) {
            return;
        }

        cpu.set_as_current();
    }

    static const char* memtx_result_str(MemTxResult res)
    {
        switch (res) {
        case qemu::MemoryRegionOps::MemTxOK:
            return "ok";
        case qemu::MemoryRegionOps::MemTxDecodeError:
            return "decode_error";
        case qemu::MemoryRegionOps::MemTxError:
            return "error";
        default:
            return "unknown";
        }
    }

    static const char* virtio_mmio_reg_name(uint64_t addr)
    {
        switch (addr) {
        case 0x000:
            return "MagicValue";
        case 0x004:
            return "Version";
        case 0x008:
            return "DeviceID";
        case 0x00c:
            return "VendorID";
        case 0x010:
            return "DeviceFeatures";
        case 0x014:
            return "DeviceFeaturesSel";
        case 0x020:
            return "DriverFeatures";
        case 0x024:
            return "DriverFeaturesSel";
        case 0x028:
            return "GuestPageSize";
        case 0x030:
            return "QueueSel";
        case 0x034:
            return "QueueNumMax";
        case 0x038:
            return "QueueNum";
        case 0x03c:
            return "QueueAlign";
        case 0x040:
            return "QueuePFN";
        case 0x044:
            return "QueueReady";
        case 0x050:
            return "QueueNotify";
        case 0x060:
            return "InterruptStatus";
        case 0x064:
            return "InterruptACK";
        case 0x070:
            return "Status";
        case 0x080:
            return "QueueDescLow";
        case 0x084:
            return "QueueDescHigh";
        case 0x090:
            return "QueueAvailLow";
        case 0x094:
            return "QueueAvailHigh";
        case 0x0a0:
            return "QueueUsedLow";
        case 0x0a4:
            return "QueueUsedHigh";
        case 0x0fc:
            return "ConfigGeneration";
        default:
            return addr >= 0x100 ? "Config" : "Unknown";
        }
    }

    bool trace_filter_matches(uint64_t addr) const
    {
        if (m_trace_filter.empty() || m_trace_filter == "all") {
            return true;
        }

        if (m_trace_filter == "control") {
            return addr < 0x200;
        }

        if (m_trace_filter == "identity") {
            return addr == 0x000 || addr == 0x004 || addr == 0x008 || addr == 0x00c;
        }

        if (m_trace_filter == "queue") {
            return (addr >= 0x030 && addr <= 0x050) || (addr >= 0x080 && addr <= 0x0a4);
        }

        return false;
    }

    std::string data_hex(const unsigned char* data, unsigned int size) const
    {
        std::ostringstream ss;
        ss << "0x";
        for (unsigned int i = 0; i < size; ++i) {
            const unsigned int idx = size - 1 - i;
            ss << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(data[idx]);
        }
        return ss.str();
    }

    void trace_access(TlmPayload& trans, uint64_t orig_addr, uint64_t qemu_addr,
                      unsigned int size, MemTxResult res, bool mirrored)
    {
        if (!m_trace_enabled || m_trace_count >= m_trace_limit ||
            !trace_filter_matches(qemu_addr)) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_trace_lock);
        if (m_trace_count >= m_trace_limit) {
            return;
        }

        std::ostream* out = &std::cerr;
        if (!m_trace_file.empty()) {
            if (!m_trace_stream.is_open()) {
                m_trace_stream.open(m_trace_file, std::ios::out | std::ios::app);
                if (!m_trace_stream) {
                    std::cerr << m_trace_name << " qemu_target_trace_error file="
                              << m_trace_file << std::endl;
                    return;
                }
            }
            out = &m_trace_stream;
        }

        ++m_trace_count;
        *out << m_trace_name
             << " qemu_target_trace command="
             << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
             << " offset=0x" << std::hex << qemu_addr
             << " original_offset=0x" << orig_addr
             << " reg=" << virtio_mmio_reg_name(qemu_addr)
             << " size=0x" << size
             << " result=" << memtx_result_str(res)
             << " data=" << data_hex(trans.get_data_ptr(), size)
             << " mirrored=" << (mirrored ? "true" : "false")
             << " sc_time=" << sc_core::sc_time_stamp()
             << std::dec << std::endl;
    }

public:
    void init(qemu::SysBusDevice sbd, int mmio_idx)
    {
        m_mr = sbd.mmio_get_region(mmio_idx);
        init_as();
    }

    void init_with_mr(qemu::MemoryRegion mr)
    {
        m_mr = mr;
        init_as();
    }

    void set_trace(const std::string& name, bool enabled, const std::string& file,
                   uint64_t limit, const std::string& filter)
    {
        m_trace_name = name;
        m_trace_enabled = enabled;
        m_trace_file = file;
        m_trace_limit = limit;
        m_trace_filter = filter;
    }

    void set_mirror_4k_aperture(bool enabled, bool mirror_writes)
    {
        m_mirror_4k_aperture = enabled;
        m_mirror_4k_writes = mirror_writes;
    }

    virtual void b_transport(TlmPayload& trans, sc_core::sc_time& t)
    {
        uint64_t addr = trans.get_address();
        uint64_t qemu_addr = addr;
        uint64_t* data = reinterpret_cast<uint64_t*>(trans.get_data_ptr());
        unsigned int size = trans.get_data_length();
        MemTxAttrs attrs;
        MemTxResult res;
        qemu::Cpu current_cpu_save;
        bool mirrored = false;
        QemuMemTxAttrsTlmExtension* attrs_ext = nullptr;

        if (trans.get_command() == tlm::TLM_IGNORE_COMMAND) {
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }

        trans.get_extension(attrs_ext);
        if (attrs_ext != nullptr) {
            attrs = attrs_ext->get_attrs();
        }

        current_cpu_save = push_current_cpu(trans);

        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            res = m_as->read(qemu_addr, data, size, attrs);
            if (m_mirror_4k_aperture &&
                res == qemu::MemoryRegionOps::MemTxDecodeError &&
                addr >= 0x1000) {
                qemu_addr = addr & 0xfff;
                mirrored = true;
                res = m_as->read(qemu_addr, data, size, attrs);
            }
            break;

        case tlm::TLM_WRITE_COMMAND:
            res = m_as->write(qemu_addr, data, size, attrs);
            if (m_mirror_4k_writes &&
                res == qemu::MemoryRegionOps::MemTxDecodeError &&
                addr >= 0x1000) {
                qemu_addr = addr & 0xfff;
                mirrored = true;
                res = m_as->write(qemu_addr, data, size, attrs);
            }
            break;

        default:
            /* TLM_IGNORE_COMMAND already handled above */
            assert(false);
            return;
        }

        trace_access(trans, addr, qemu_addr, size, res, mirrored);

        trans.set_extension(new QemuMrHintTlmExtension(m_mr, addr));

        switch (res) {
        case qemu::MemoryRegionOps::MemTxOK:
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            break;

        case qemu::MemoryRegionOps::MemTxDecodeError:
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            break;

        case qemu::MemoryRegionOps::MemTxError:
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            break;

        default:
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            break;
        }

        pop_current_cpu(current_cpu_save);
    }

    virtual tlm::tlm_sync_enum nb_transport_fw(TlmPayload& trans, tlm::tlm_phase& phase, sc_core::sc_time& t)
    {
        /* TODO: report an error */
        abort();
        return tlm::TLM_ACCEPTED;
    }

    virtual bool get_direct_mem_ptr(TlmPayload& trans, tlm::tlm_dmi& dmi_data) { return false; }

    virtual unsigned int transport_dbg(TlmPayload& trans)
    {
        unsigned int size = trans.get_data_length();
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(trans, delay);
        if (trans.get_response_status() == tlm::TLM_OK_RESPONSE)
            return size;
        else
            return 0;
    }
};

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class QemuTargetSocket
    : public tlm::tlm_target_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>
{
public:
    using TlmTargetSocket = tlm::tlm_target_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1,
                                                   sc_core::SC_ZERO_OR_MORE_BOUND>;
    using TlmPayload = tlm::tlm_generic_payload;

protected:
    TlmTargetToQemuBridge m_bridge;
    QemuInstance& m_inst;
    qemu::SysBusDevice m_sbd;
    cci::cci_param<bool> p_mirror_4k_aperture;
    cci::cci_param<bool> p_mirror_4k_writes;

public:
    QemuTargetSocket(const char* name, QemuInstance& inst)
        : TlmTargetSocket(name)
        , m_inst(inst)
        , p_mirror_4k_aperture(std::string(name) + ".mirror_4k_aperture",
                                false,
                                "Mirror high FVP-style apertures to the first 4 KiB for reads")
        , p_mirror_4k_writes(std::string(name) + ".mirror_4k_writes",
                             false,
                             "Allow 4 KiB aperture mirroring for writes")
    {
        TlmTargetSocket::bind(m_bridge);
    }

    void init(qemu::SysBusDevice sbd, int mmio_idx)
    {
        m_bridge.init(sbd, mmio_idx);
        apply_mirror_config();
    }

    void init_with_mr(qemu::MemoryRegion mr)
    {
        m_bridge.init_with_mr(mr);
        apply_mirror_config();
    }

    void set_trace(const std::string& name, bool enabled, const std::string& file,
                   uint64_t limit, const std::string& filter)
    {
        m_bridge.set_trace(name, enabled, file, limit, filter);
    }

    void set_mirror_4k_aperture(bool enabled, bool mirror_writes = false)
    {
        m_bridge.set_mirror_4k_aperture(enabled, mirror_writes);
    }

private:
    void apply_mirror_config()
    {
        m_bridge.set_mirror_4k_aperture(
            p_mirror_4k_aperture.get_value(), p_mirror_4k_writes.get_value());
    }
};

#endif
