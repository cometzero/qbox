/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu_pl061.h"

void module_register() { GSC_MODULE_REGISTER_C(qemu_pl061, sc_core::sc_object*); }
