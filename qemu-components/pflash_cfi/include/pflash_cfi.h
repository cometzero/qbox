/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_PFLASH_CFI_H
#define _LIBQBOX_COMPONENTS_PFLASH_CFI_H

#include <limits>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <qemu-instance.h>
#include <cpu.h>
#include <device.h>
#include <ports/target.h>

class pflash_cfi : public QemuDevice
{
public:
    QemuTargetSocket<> socket;

private:
    uint8_t m_pflash_type;
    std::string blkdev_id;
    cci::cci_param<std::string> blkdev_str;
    cci::cci_param<uint32_t> p_num_blocks;
    cci::cci_param<uint32_t> p_sector_length;
    cci::cci_param<uint32_t> p_num_blocks0;
    cci::cci_param<uint32_t> p_sector_length0;
    cci::cci_param<uint32_t> p_num_blocks1;
    cci::cci_param<uint32_t> p_sector_length1;
    cci::cci_param<uint32_t> p_num_blocks2;
    cci::cci_param<uint32_t> p_sector_length2;
    cci::cci_param<uint32_t> p_num_blocks3;
    cci::cci_param<uint32_t> p_sector_length3;
    cci::cci_param<uint8_t> p_width;
    cci::cci_param<uint8_t> p_device_width;
    cci::cci_param<uint8_t> p_max_device_width;
    cci::cci_param<uint8_t> p_secure;
    cci::cci_param<bool> p_old_multiple_chip_handling;
    cci::cci_param<uint8_t> p_mappings;
    cci::cci_param<uint8_t> p_big_endian;
    cci::cci_param<uint16_t> p_id0;
    cci::cci_param<uint16_t> p_id1;
    cci::cci_param<uint16_t> p_id2;
    cci::cci_param<uint16_t> p_id3;
    cci::cci_param<uint16_t> p_unlock_addr0;
    cci::cci_param<uint16_t> p_unlock_addr1;
    cci::cci_param<std::string> p_name;
    cci::cci_param<uint64_t> p_local_address;
    cci::cci_param<int> p_local_priority;
    cci::cci_param<std::string> p_local_cpu;
    cci::cci_param<bool> p_program_ff_erases_sector;
    cci::cci_param<bool> p_io_mode_only;
    cci::cci_param<bool> p_defer_backing_write;
    cci::cci_param<uint64_t> p_defer_backing_flush_interval;
    cci::cci_param<uint64_t> p_defer_backing_flush_delay_ms;

public:
    pflash_cfi(const sc_core::sc_module_name& name, sc_core::sc_object* o, uint8_t pflash_type)
        : pflash_cfi(name, *(dynamic_cast<QemuInstance*>(o)), pflash_type)
    {
    }
    pflash_cfi(const sc_core::sc_module_name& n, QemuInstance& inst, uint8_t pflash_type)
        : QemuDevice(n, inst, (pflash_type == 1) ? "cfi.pflash01" : "cfi.pflash02")
        , m_pflash_type(pflash_type)
        , socket("target_socket", inst)
        , blkdev_id(std::string(name()) + "-id")
        , blkdev_str("blkdev_str", "", "blkdev string for QEMU (do not specify ID)")
        , p_num_blocks("num_blocks", 0,
                       "number of blocks actually visible to the guest, i.e. the total size of the device divided by "
                       "the sector length")
        , p_sector_length("sector_length", 0, "sector length")
        , p_num_blocks0("num_blocks0", 0, "number of blocks 0")
        , p_sector_length0("sector_length0", 0, "sector length for 0")
        , p_num_blocks1("num_blocks1", 0, "number of blocks 1")
        , p_sector_length1("sector_length1", 0, "sector length for 1")
        , p_num_blocks2("num_blocks2", 0, "number of blocks 2")
        , p_sector_length2("sector_length2", 0, "sector length for 2")
        , p_num_blocks3("num_blocks3", 0, "number of blocks 3")
        , p_sector_length3("sector_length3", 0, "sector length for 3")
        , p_width("width", 0, "the overall width of this QEMU device in bytes.")
        , p_device_width("device_width", 0, "the width of each flash device")
        , p_max_device_width("max_device_width", 0, "the maximum width of each flash device")
        , p_secure("secure", 0, "secure bit (uint8_t)")
        , p_old_multiple_chip_handling("old_multiple_chip_handling", false, "old multiple chip handling (bool)")
        , p_mappings("mappings", 0, "flash mapping")
        , p_big_endian("big_endian", 0, "is big endian (uint8_t)")
        , p_id0("id0", 0, "id of 0")
        , p_id1("id1", 0, "id of 1")
        , p_id2("id2", 0, "id of 2")
        , p_id3("id3", 0, "id of 3")
        , p_unlock_addr0("unlock_addr0", 0, "unlock address 0")
        , p_unlock_addr1("unlock_addr1", 0, "unlock address 1")
        , p_name("name", "", "name of the device")
        , p_local_address("local_address", std::numeric_limits<uint64_t>::max(),
                          "optional address in this QEMU instance's system memory")
        , p_local_priority("local_priority", 1,
                           "overlap priority for the optional QEMU-local mapping")
        , p_local_cpu("local_cpu", "",
                      "QemuCpu SystemC path owning the optional local address space")
        , p_program_ff_erases_sector("program_ff_erases_sector", false,
                                     "treat an aligned one-byte 0xff program as a sector erase")
        , p_io_mode_only("io_mode_only", false,
                         "keep the QEMU CFI device in callback I/O mode")
        , p_defer_backing_write("defer_backing_write", false,
                                "coalesce block-backend writes by dirty sector")
        , p_defer_backing_flush_interval(
              "defer_backing_flush_interval", 0,
              "QEMU CFI updates between deferred backing flushes")
        , p_defer_backing_flush_delay_ms(
              "defer_backing_flush_delay_ms", 0,
              "real-time delay before flushing pending backing sectors")
    {
        if (!blkdev_str.get_value().empty()) {
            std::stringstream opts;
            opts << blkdev_str.get_value();
            opts << ",id=" << blkdev_id;

            m_inst.add_arg("-drive");
            m_inst.add_arg(opts.str().c_str());
        }
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        if (!p_name.get_value().empty()) m_dev.set_prop_str("name", p_name.get_value().c_str());
        if (!p_num_blocks.is_default_value()) m_dev.set_prop_uint("num-blocks", p_num_blocks.get_value());
        if (!p_sector_length.is_default_value()) m_dev.set_prop_uint("sector-length", p_sector_length.get_value());
        if (m_pflash_type == 2) {
            if (!p_num_blocks0.is_default_value()) m_dev.set_prop_uint("num-blocks0", p_num_blocks0.get_value());
            if (!p_sector_length0.is_default_value())
                m_dev.set_prop_uint("sector-length0", p_sector_length0.get_value());
            if (!p_num_blocks1.is_default_value()) m_dev.set_prop_uint("num-blocks1", p_num_blocks1.get_value());
            if (!p_sector_length1.is_default_value())
                m_dev.set_prop_uint("sector-length1", p_sector_length1.get_value());
            if (!p_num_blocks2.is_default_value()) m_dev.set_prop_uint("num-blocks2", p_num_blocks2.get_value());
            if (!p_sector_length2.is_default_value())
                m_dev.set_prop_uint("sector-length2", p_sector_length2.get_value());
            if (!p_num_blocks3.is_default_value()) m_dev.set_prop_uint("num-blocks3", p_num_blocks3.get_value());
            if (!p_sector_length3.is_default_value())
                m_dev.set_prop_uint("sector-length3", p_sector_length3.get_value());
        }
        if (!p_width.is_default_value()) m_dev.set_prop_uint("width", p_width.get_value());
        if (m_pflash_type == 1) {
            if (!p_device_width.is_default_value()) m_dev.set_prop_uint("device-width", p_device_width.get_value());
            if (!p_max_device_width.is_default_value())
                m_dev.set_prop_uint("max-device-width", p_max_device_width.get_value());
            if (!p_secure.is_default_value()) m_dev.set_prop_uint("secure", p_secure.get_value());
            if (!p_old_multiple_chip_handling.is_default_value())
                m_dev.set_prop_bool("old-multiple-chip-handling", p_old_multiple_chip_handling.get_value());
            if (!p_program_ff_erases_sector.is_default_value())
                m_dev.set_prop_bool("program-ff-erases-sector", p_program_ff_erases_sector.get_value());
            if (!p_io_mode_only.is_default_value())
                m_dev.set_prop_bool("io-mode-only", p_io_mode_only.get_value());
            if (!p_defer_backing_write.is_default_value())
                m_dev.set_prop_bool("defer-backing-write", p_defer_backing_write.get_value());
            if (!p_defer_backing_flush_interval.is_default_value())
                m_dev.set_prop_uint("defer-backing-flush-interval",
                                    p_defer_backing_flush_interval.get_value());
            if (!p_defer_backing_flush_delay_ms.is_default_value())
                m_dev.set_prop_uint("defer-backing-flush-delay-ms",
                                    p_defer_backing_flush_delay_ms.get_value());
        }
        if (!p_mappings.is_default_value()) m_dev.set_prop_uint("mappings", p_mappings.get_value());
        if (!p_big_endian.is_default_value()) m_dev.set_prop_uint("big-endian", p_big_endian.get_value());
        if (!p_id0.is_default_value()) m_dev.set_prop_uint("id0", p_id0.get_value());
        if (!p_id1.is_default_value()) m_dev.set_prop_uint("id1", p_id1.get_value());
        if (!p_id2.is_default_value()) m_dev.set_prop_uint("id2", p_id2.get_value());
        if (!p_id3.is_default_value()) m_dev.set_prop_uint("id3", p_id3.get_value());
        if (!p_unlock_addr0.is_default_value()) m_dev.set_prop_uint("unlock-addr0", p_unlock_addr0.get_value());
        if (!p_unlock_addr1.is_default_value()) m_dev.set_prop_uint("unlock-addr1", p_unlock_addr1.get_value());
        if (!blkdev_str.get_value().empty()) {
            m_dev.set_prop_parse("drive", blkdev_id.c_str());
        }
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);

        socket.init(sbd, 0);
        if (p_local_address.get_value() != std::numeric_limits<uint64_t>::max()) {
            qemu::MemoryRegion region = sbd.mmio_get_region(0);
            sc_core::sc_object* object =
                sc_core::sc_find_object(p_local_cpu.get_value().c_str());
            QemuCpu* cpu = dynamic_cast<QemuCpu*>(object);
            if (cpu == nullptr) {
                SCP_FATAL(()) << "local_cpu is not a QemuCpu: "
                              << p_local_cpu.get_value();
            }
            cpu->socket.map_local_region(
                region, p_local_address.get_value(),
                p_local_priority.get_value());
        }
    }
};

extern "C" void module_register();

#endif
