/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "delegation.h"

#define CH_SERVER    ((microkit_channel)0)
#define CH_DELEGATEE ((microkit_channel)1)

// Delegation CNode temporarily installed in this PD's root CNode.
#define CPTR_DGT_CND \
    (microkit_cspace_root_slot_to_cptr(GRANT_SLOT))

typedef void (*entry_t)(void);

#define DELEGATED_MR_VADDR 0xC00000

static void delegated_mr_test(void)
{
    volatile seL4_Word *mr = (volatile seL4_Word *)DELEGATED_MR_VADDR;

    microkit_dbg_puts("<delegator> access delegated MR\n");

    // This store should fault the first time.
    //
    // The delegatee maps the delegated frame and replies to the fault.
    // The instruction is then restarted and this store succeeds.
    *mr = 0x12345678;

    microkit_dbg_puts("<delegator> delegated MR mapped, value: ");
    puthex64(*mr);
    microkit_dbg_puts("\n");
}

static void delegation_restore_cap(seL4_Word slot)
{
    microkit_dbg_puts("<delegator> restore cap: ");
    microkit_dbg_put32(slot);
    microkit_dbg_puts("\n");

    // to => 'microkit' CNode (of delegator PD)
    // from <= 'delegation' CNode
    //
    seL4_Error err =
#ifdef ANALYSIS_FOR_ARGS
        seL4_CNode_Copy( /* DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND), slot, LOOKUP_DEPTH__MK, */
// ANALYSIS for below:
//
// CPTR_DGT_CND = 15 << (64 - 6) | 0
// DELEGATION_SLOT_MICROKIT_CNODE = 1
//
// DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND)
//   = (CPTR_DGT_CND) | DELEGATION_SLOT_MICROKIT_CNODE
//   = (CPTR_DGT_CND) | 1
//
// => slot 1 of delegation CNode points to
//   + the cap of the microkit CNode of the delegator PD
//   + with guard_size = 0
// => during a CNode capability lookup process
//    the root cap will consume the first 6 bits of the CPtr
//    given that the CPtr is (15 << (64 - 6) | 1)
//    the higher 6 bits is then 001111b = 15
//    now we will get slot 15 of the root CNode of current PD
//    which points to the cap of the delegation CNode
//    with guard_size = (64-6) - 9 (set by microkit tool)
//      = milestone => reached 'delegation CNode'
//    if we continue and use the remaining (64-6) bits to minus guard_size
//    we will still have 9 bits left for CNode cap lookup
//    currently the remaining 9 bits is 000000001b (from the CPtr)
//    which points to slot 1 of the delegation CNode
//    and now all lookup address bits are used
//    so, we will get the cap of the slot 1 of the delegation CNode
//    which is the cap of the microkit CNode of the delegator PD
//      => final => reached delegator PD's 'microkit CNode'
// => Once this CNode cap lookup is finished,
//    we can use the 'slot' arg to perform the actual lookup
//    for the destination slot in the target CNode
//
// LOOKUP_DEPTH__MK = 9
// (target CNode => delegator PD's 'microkit CNode' with guard_size = 0)
//
// => The Microkit CNode cap has guard_size = 0, so all
//    LOOKUP_DEPTH__MK = 9 bits are used as radix bits.
//    so 'slot' directly selects Microkit CNode[slot].
// => we will finally get the cap in the 'slot' of 'microkit CNode'
//
            DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND),    slot, LOOKUP_DEPTH__MK,

// ANALYSIS for below:
//
// CPTR_DGT_CND = 15 << (64 - 6) | 0
//
// => slot 0 of delegation CNode is a self-pointing cap
// => during a CNode capability lookup process
//    the root CNode will consume the first 6 bit
//    and 15 points to the cap of delegation CNode cap
//    which is initialised with guard_size = 64 - 6 - 9
//    the current remaining lookup bits are 58
//    once the guard size is consumed, the final bits are 9
//    9 equals to the radix of delegation CNode
//    and the remanining 9 bit is 000000000b
//    which points to slot 0, i.e., the self-pointing cap
//    so CPTR_DGT_CND will give you the cap of the delegation CNode
//
// => Once this CNode cap lookup is finished,
//    we can use the 'slot' arg to perform the actual lookup
//    for the source slot in the target CNode
//
// LOOKUP_DEPTH__DL = 64 - 6 - 9 = 49
// (target CNode => delegator PD's 'delegation CNode' with guard_size = 40)
//   (i.e., self-pointing cap has guard size = (64 - 6 - 9 - 9) = 40)
//
// => during the final cap lookup process, we will use 40 bits out of LOOKUP_DEPTH__DL
//    for parsing the guard size of target CNode (i.e., the 'delegation CNode')
//    Since 'slot' only occupies the low 9 bits, the upper 40 bits
//    of the 49-bit source index are zero and match the self-cap's
//    zero-valued 40-bit guard. The remaining 9 bits of the lookup depth
//    matches with the radix of the 'delegation CNode'
// => we will finally get the cap in the 'slot' of 'delegation CNode'
//
            CPTR_DGT_CND, /* access via delegtor */ slot, LOOKUP_DEPTH__DL,
            seL4_AllRights
        );
#else
        seL4_CNode_Copy(
            DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND),    slot, LOOKUP_DEPTH__MK,
            CPTR_DGT_CND, /* access via delegtor */ slot, LOOKUP_DEPTH__DL,
            seL4_AllRights
        );
#endif
    if (err != seL4_NoError) {
        microkit_dbg_puts("<delegator> failed to restore cap: ");
        microkit_dbg_put32(slot);
        microkit_dbg_puts("\n");
        for (;;);
    }
}

static void delegation_remove_cap(seL4_Word slot)
{
    microkit_dbg_puts("<delegator> remove cap: ");
    microkit_dbg_put32(slot);
    microkit_dbg_puts("\n");

    seL4_Error err =
        seL4_CNode_Delete(
// Delete the slot cap from the microkit CNode of the delegator PD
// (when the cap of the microkit CNode is accessed via the delegation CNode)
            DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND), slot, LOOKUP_DEPTH__MK
        );

    if (err != seL4_NoError) {
        microkit_dbg_puts("<delegator> failed to remove cap: ");
        microkit_dbg_put32(slot);
        microkit_dbg_puts("\n");
        for (;;);
    }
}

static void request_delegation_cnode(void)
{
    microkit_dbg_puts("<delegator> request delegation CNode\n");
    microkit_ppcall(
        CH_DELEGATEE,
        microkit_msginfo_new(PPC_DELEGATION_GRANT, 0)
    );
}

static void release_delegation_cnode(void)
{
    microkit_dbg_puts("<delegator> release delegation CNode\n");
    microkit_ppcall(
        CH_DELEGATEE,
        microkit_msginfo_new(PPC_DELEGATION_RELEASE, 0)
    );
}

//
// This function uses the 'test_func' to test the validity of 'slot'
// The slot must be a slot of delegated capability
//
static void delegator_self_mng_test(seL4_Word slot, entry_t test_func)
{
    // This should fail,
    // as the channel capability is delegated.
    int loop = 3;
    do {
        test_func();
    } while (--loop);

    // Restore the delegated channel cap to its normal Microkit slot.
    delegation_restore_cap(slot);

    // This will succeed,
    // as the channel capability is now valid.
    loop = 3;
    do {
        test_func();
    } while (--loop);

    // Remove the restored capability before releasing delegation access.
    delegation_remove_cap(slot);
}

static void delegator_self_mng_test_wrapper(seL4_Word slot, entry_t test_func)
{
    // Ask the delegatee for temporary access to the delegation CNode.
    request_delegation_cnode();

    // Perform test...
    delegator_self_mng_test(slot, test_func);

    release_delegation_cnode();
}

static void signal_test(void)
{
    microkit_dbg_puts("<delegator> notify server from channel: ");
    microkit_dbg_put32(CH_SERVER + BASE_OUTPUT_NOTIFICATION_CAP);
    microkit_dbg_puts("\n");

    microkit_notify(CH_SERVER);
}

void init(void)
{
    microkit_dbg_puts("<delegator> hello world\n");

    delegator_self_mng_test_wrapper(CH_SERVER + BASE_OUTPUT_NOTIFICATION_CAP, signal_test);
}

void notified(microkit_channel ch)
{
    microkit_dbg_puts("<delegator>::notified: received signal from delegatee\n");
    microkit_dbg_puts("<delegator>::notified: try notifying server\n");
    microkit_notify(CH_SERVER);

    delegated_mr_test();
}
