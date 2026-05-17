/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sbsa_gwdt.h"

void module_register() { GSC_MODULE_REGISTER_C(sbsa_gwdt, sc_core::sc_object*); }
