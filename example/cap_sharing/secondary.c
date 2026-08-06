/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

static int recv_cnt;

void init(void)
{
    microkit_dbg_puts("|secondary| hello, world\n");
    recv_cnt = 9;
}

void notified(microkit_channel ch)
{
    microkit_dbg_puts("|secondary| notified, recv cnt: '");
    microkit_dbg_put32(recv_cnt);
    microkit_dbg_puts("'\n");

    recv_cnt--;

    microkit_notify(ch);

    if (!recv_cnt) {
        while (1);
    }
}
