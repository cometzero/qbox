/*
 * Copyright (c) 2026, Arm Limited and Contributors. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <cortex-r82.h>

void module_register() { GSC_MODULE_REGISTER_C(cpu_arm_cortexR82, sc_core::sc_object*); }
