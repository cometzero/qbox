/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qemu_pcie_test_ep.h>

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_pcie_test_ep, sc_core::sc_object*,
                          sc_core::sc_object*, sc_core::sc_object*);
}
