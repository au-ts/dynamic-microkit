/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

#define CH_SECONDARY ((microkit_channel)0)

// As per cap_sharing.system
#define CAP_SECONDARY_SC  (microkit_cspace_root_slot_to_cptr(1))
#define CAP_SECONDARY_TCB (microkit_cspace_root_slot_to_cptr(2))
#define CAP_MY_SC         (microkit_cspace_root_slot_to_cptr(3))
#define CAP_MY_TCB        (microkit_cspace_root_slot_to_cptr(4))
#define CAP_SHARING_CND   (microkit_cspace_root_slot_to_cptr(5))
#define CAP_SECONDARY_CSP (microkit_cspace_root_slot_to_cptr(6))

static void halt(void)
{
    seL4_Error error = seL4_TCB_Suspend(CAP_MY_TCB);
    if (error != seL4_NoError) {
        microkit_dbg_puts("|primary  | error suspending TCB\n");
    }

    microkit_dbg_puts("|primary  | error: should not reach this point! we should have suspended ourself!\n");
    while (1) { }
}

void init(void)
{
    seL4_Error err;

    microkit_dbg_puts("|primary  | hello, world\n");

    /* Notify the secondary. This will print output from secondary as it is
       higher priority. */
    microkit_dbg_puts("|primary  | notifying secondary\n");
    microkit_notify(CH_SECONDARY);

    microkit_dbg_puts("|primary  | suspending secondary\n");
    err = seL4_TCB_Suspend(CAP_SECONDARY_TCB);
    if (err != seL4_NoError) {
        microkit_dbg_puts("|primary  | error suspending TCB\n");
        halt();
    }

    /* Notify the secondary. It is suspended so it will not print. */
    microkit_dbg_puts("|primary  | notifying secondary (it should not print)\n");
    microkit_notify(CH_SECONDARY);

    microkit_dbg_puts("|primary  | resuming secondary (it should then print)\n");
    err = seL4_TCB_Resume(CAP_SECONDARY_TCB);
    if (err != seL4_NoError) {
        microkit_dbg_puts("|primary  | error resuming TCB\n");
        halt();
    }

    microkit_dbg_puts("|primary  | begin test for cap sharing...\n");
    /* try to send signal to the secondary PD... */
    microkit_notify(CH_SECONDARY);
#if 0
    microkit_dbg_puts("|primary  | halting (success)...\n");
    halt();
#endif
}

void notified(microkit_channel ch)
{
    microkit_dbg_puts("=> primary$ notified by secondary!\n");

    microkit_dbg_puts("    |primary  | ready to stop secondary from sending signals\n");
    microkit_dbg_puts("    |primary  | secondary should prints null cap invocation\n");
    /* once receive a signal back, stop the secondary PD from sending signals */
    seL4_Error err = seL4_CNode_Move(
        CAP_SHARING_CND,
        3, /* some random available slot in the sharing CNode */
        (64 - PD_ROOT_CAP_BITS),
        CAP_SECONDARY_CSP,
        BASE_OUTPUT_NOTIFICATION_CAP + CH_SECONDARY,
        (64 - PD_ROOT_CAP_BITS)
    );
    if (err != seL4_NoError) {
        microkit_dbg_puts("|primary | error moving secondary's channel 0 cap to sharing CNode\n");
        halt();
    }
    /* we can still send signals,
     * but secondary will never be able to signal back. */
    for (int i = 0; i < 4; ++i) {
        /* primary has lower priority, so it will not block the secondary. */
        microkit_notify(CH_SECONDARY);
    }

    microkit_dbg_puts("    |primary  | ready to move back the ntfn cap\n");

    /* move back the ntfn cap for the secondary PD to notify back again */
    err = seL4_CNode_Move(
        CAP_SECONDARY_CSP,
        BASE_OUTPUT_NOTIFICATION_CAP + CH_SECONDARY,
        (64 - PD_ROOT_CAP_BITS),
        CAP_SHARING_CND,
        3, /* some random available slot in the sharing CNode */
        (64 - PD_ROOT_CAP_BITS)
    );
    if (err != seL4_NoError) {
        microkit_dbg_puts("|primary | error moving secondary's channel 0 cap back from sharing CNode\n");
        halt();
    }

    microkit_dbg_puts("    |primary  | at this point, primary can hear back from secondary again!\n");
    microkit_notify(CH_SECONDARY);
}
