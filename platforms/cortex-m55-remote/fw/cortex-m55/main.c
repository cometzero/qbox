/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "nvic.h"

#define SYST_CSR_S (*(volatile unsigned int*)0xe000e010)
#define SYST_RVR_S (*(volatile unsigned int*)0xe000e014)
#define SYST_CVR_S (*(volatile unsigned int*)0xe000e018)
#define SYST_RVR_NS (*(volatile unsigned int*)0xe002e014)

#define SYST_CSR_ENABLE (1u << 0)
#define SYST_CSR_TICKINT (1u << 1)
#define SYST_CSR_CLKSOURCE (1u << 2)
#define SYST_CSR_COUNTFLAG (1u << 16)

void uart_driver_init(void) { _enable_irq(); }

static void uart_puts(const char* str)
{
    while (*str) {
        *(volatile unsigned int*)0xc0000000 = *str++;
    }
}

void __attribute__((interrupt)) invalid_excp(void)
{
    // We never get here, unless the deliberate unhandled exception in c_entry() is uncommented.
    uart_puts("invalid exception happened\r\n");
}

void __attribute__((interrupt)) _handle_irq(void)
{
    uart_puts("IRQ 17 happened\r\n");
    *(volatile unsigned int*)0xc0001000 = 1;
}

void __attribute__((interrupt)) _handle_nmi(void)
{
    uart_puts("NMI happened\r\n");
    *(volatile unsigned int*)0xc0001004 = 1;

    SYST_RVR_S = 99999;
    SYST_CVR_S = 0;
    SYST_CSR_S = SYST_CSR_ENABLE | SYST_CSR_TICKINT |
                 SYST_CSR_CLKSOURCE;
}

void __attribute__((interrupt)) _handle_systick(void)
{
    unsigned int control = SYST_CSR_S;

    SYST_CSR_S = 0;
    if (control & SYST_CSR_COUNTFLAG) {
        uart_puts("SysTick COUNTFLAG set\r\n");
    }
    uart_puts("SysTick happened\r\n");
}

void c_entry(void)
{
    nvic_enable_irq(0);
    nvic_enable_irq(17);

    SYST_RVR_S = 99999;
    SYST_RVR_NS = 12345;
    if (SYST_RVR_S == 99999 && SYST_RVR_NS == 12345) {
        uart_puts("SysTick banks isolated\r\n");
    }

    uart_puts("Test program is running. Listening for interrupts.\r\n");
    *(volatile unsigned int*)0xc000100c = 1; // start IRQs generation

    while (1) {
        asm volatile("wfi");
    }
}
