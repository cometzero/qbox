/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qemu_cc3xx.h>

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_cc3xx, sc_core::sc_object*);
}
