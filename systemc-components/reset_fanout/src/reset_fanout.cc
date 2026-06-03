/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <reset_fanout.h>

extern "C" void module_register()
{
    GSC_MODULE_REGISTER_C(reset_fanout);
}
