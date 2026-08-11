/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

void init(void)
{
    // test mktsymb patcher
    microkit_dbg_puts("microkit name: ");
    microkit_dbg_puts(microkit_name);
    microkit_dbg_puts(" starting\n");

    // test microkit_notifications bitmap
    microkit_notify(1);

    for (int i = 0; i < 300000; ++i) {
        __asm__ volatile("nop");
    }
    int *x = 0;
    /* crash here... */
    *x = 1;
}

void notified(microkit_channel ch)
{
}