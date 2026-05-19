/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <arm-smmuv3.h>

void module_register()
{
    GSC_MODULE_REGISTER_C(arm_smmuv3, sc_core::sc_object*, sc_core::sc_object*);
}
