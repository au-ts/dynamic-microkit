/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

#define CH_CLIENT ((microkit_channel) 0)

void init(void)
{
    microkit_dbg_puts(" (server)  init\n");
}

void notified(microkit_channel ch)
{
    microkit_dbg_puts(" (server)  received signal from delegator\n");
}
