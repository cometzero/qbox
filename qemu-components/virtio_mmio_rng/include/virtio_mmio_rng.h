/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <module_factory_registery.h>
#include <qemu-instance.h>
#include <virtio/virtio-mmio.h>

class virtio_mmio_rng : public QemuVirtioMMIO
{
public:
    virtio_mmio_rng(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : virtio_mmio_rng(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    virtio_mmio_rng(sc_core::sc_module_name name, QemuInstance& inst)
        : QemuVirtioMMIO(name, inst, "virtio-rng-device")
    {
    }
};

extern "C" void module_register();
