/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qemu_pcie_epc.h>

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_pcie_epc, sc_core::sc_object*);
}
