/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qemu_pcie_root_port.h>

void module_register() { GSC_MODULE_REGISTER_C(qemu_pcie_root_port, sc_core::sc_object*, sc_core::sc_object*); }
