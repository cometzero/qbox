/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <virtio_mmio_rng.h>

void module_register() { GSC_MODULE_REGISTER_C(virtio_mmio_rng, sc_core::sc_object*); }
