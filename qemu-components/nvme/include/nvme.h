/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_PCI_NVME_H
#define _LIBQBOX_COMPONENTS_PCI_NVME_H

#include <cci_configuration>

#include <sstream>
#include <sys/stat.h>

#include <module_factory_registery.h>
#include <libgssync.h>

#include <qemu_gpex.h>
#include <qemu_pcie_root_port.h>

class nvme : public qemu_gpex::Device
{
    // TODO: use a real backend object
protected:
    cci::cci_param<std::string> p_serial;
    cci::cci_param<std::string> p_image_path;
    cci::cci_param<std::string> p_blob_file;
    cci::cci_param<std::string> p_format;
    cci::cci_param<std::string> p_addr;
    cci::cci_param<std::string> p_x_speed;
    cci::cci_param<std::string> p_x_width;
    cci::cci_param<uint32_t> max_ioqpairs;
    std::string m_drive_id;

public:
    nvme(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : nvme(name, *(dynamic_cast<QemuInstance*>(o)), t)
    {
    }
    nvme(const sc_core::sc_module_name& name, QemuInstance& inst, sc_core::sc_object* bus)
        : qemu_gpex::Device(name, inst, "nvme")
        , p_serial("serial", basename(), "Serial name of the nvme disk")
        , p_image_path("image_path", "", "Disk image used as NVMe namespace data storage")
        , p_blob_file("blob_file", "", "Deprecated alias for image_path")
        , p_format("format", "raw", "Format of the disk image (supported: raw, qcow2)")
        , p_addr("addr", "", "PCI slot on the parent bus")
        , p_x_speed("x_speed", "2_5", "Maximum PCIe link speed in GT/s")
        , p_x_width("x_width", "1", "Maximum PCIe link width")
        , max_ioqpairs("max_ioqpairs", 64, "Passed through to QEMU max_ioqpairs")
        , m_drive_id(basename())
    {
        std::string file = p_image_path.get_value();
        const std::string blob_file = p_blob_file.get_value();
        if (!file.empty() && !blob_file.empty() && file != blob_file) {
            SCP_FATAL(SCMOD) << "image_path and blob_file name different images";
        }
        if (file.empty()) file = blob_file;
        if (file.empty()) {
            SCP_FATAL(SCMOD) << "the nvme device needs image_path CCI parameter";
        }
        if (file.find_first_of(",\r\n") != std::string::npos ||
            file.find('\0') != std::string::npos) {
            SCP_FATAL(SCMOD) << "image_path must not contain commas, newlines, or NUL bytes";
        }
        struct stat image_stat;
        if (::stat(file.c_str(), &image_stat) != 0 || !S_ISREG(image_stat.st_mode)) {
            SCP_FATAL(SCMOD) << "image_path must name an existing regular file: " << file;
        }
        std::string format = p_format.get_value();
        if (format != "raw" && format != "qcow2") {
            SCP_FATAL(SCMOD) << "format parameter must be 'raw' or 'qcow2', got: " << format;
        }
        m_drive_id += "_drive";

        std::stringstream opts;
        opts << "if=none,id=" << m_drive_id << ",file=" << file << ",format=" << format;
        m_inst.add_arg("-drive");
        m_inst.add_arg(opts.str().c_str());

        if (auto* gpex = dynamic_cast<qemu_gpex*>(bus)) {
            gpex->add_device(*this);
        } else if (auto* root_port = dynamic_cast<qemu_pcie_root_port*>(bus)) {
            root_port->add_device(*this);
        } else {
            SCP_FATAL(SCMOD) << "nvme requires a GPEX or PCIe root port parent";
        }
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();

        std::string serial = p_serial;
        m_dev.set_prop_str("serial", serial.c_str());
        m_dev.set_prop_parse("drive", m_drive_id.c_str());
        m_dev.set_prop_int("max_ioqpairs", max_ioqpairs);
        if (!p_addr.get_value().empty()) m_dev.set_prop_str("addr", p_addr.get_value().c_str());
        m_dev.set_prop_parse("x-speed", p_x_speed.get_value().c_str());
        m_dev.set_prop_parse("x-width", p_x_width.get_value().c_str());
    }

    void gpex_realize(qemu::Bus& bus) override { qemu_gpex::Device::gpex_realize(bus); }
};

extern "C" void module_register();

#endif
