/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "delegation.h"

#define CH_DELEGATEE ((microkit_channel)1)

// Delegation CNode in the delegatee CSpace.
#define CPTR_DGT_CND \
    (microkit_cspace_root_slot_to_cptr(48))

void init(void)
{
    microkit_dbg_puts("[delegatee] init\n");
}

void notified(microkit_channel ch)
{
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    seL4_Error err;
    seL4_Word label = microkit_msginfo_get_label(msginfo);

    switch (label) {
    case PPC_DELEGATION_GRANT:
        microkit_dbg_puts("[delegatee] grant delegation CNode\n");

        // Copy the delegation CNode cap into delegator.root[15].
        // slot 15 must be (reserved) free...
#ifdef ANALYSIS_FOR_ARGS
        err = seL4_CNode_Copy(
// CPTR_DGT_CNG = 48 << (64 - 6) | 0
// DELEGATION_SLOT_ROOT_CNODE = 2
//
// DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND)
//   = (CPTR_DGT_CND) | DELEGATION_SLOT_ROOT_CNODE
//   = (CPTR_DGT_CND) | 2
//
// => slot 2 of delegation CNode points to
//   + the cap of the root CNode of the delegator PD
//   + with guard_size = 0
// => during a CNode capability lookup process
//    the root cap will consume the first 6 bits of the CPtr
//    given that the CPtr is (48 << (64 - 6) | 2)
//    the higher 6 bits is then 110000b = 48
//    now we will get slot 48 of the root CNode of current PD
//    which points to the cap of the delegation CNode of the first delegator
//      (valid range: 48 ~ 63, for delegators 0 ~ 15)
//    with guard_size = (64-6) - 9 (set by microkit tool)
//      = milestone => reached 'delegation CNode' of the first delegator
//    if we continue and use the remaining (64-6) bits to minus guard_size
//    we will still have 9 bits left for CNode cap lookup
//    currently the remaining 9 bits is 000000010b (from the CPtr)
//    which points to slot 2 of the delegation CNode
//    and now all lookup address bits are used
//    so, we will get the cap of the slot 2 of the delegation CNode
//    which is the cap of the root CNode of the target delegator PD
//      => final => reached delegator PD's 'root CNode'
// => Once this CNode cap lookup is finished,
//    we can use GRANT_SLOT to perform the actual lookup
//    for the destination slot in the target CNode
//    i.e., slot 15
//
//
// LOOKUP_DEPTH__RT = 6
// (target CNode => delegator PD's 'root CNode' with guard_size = 0)
//
// => the root CNode cap has guard_size = 0, so all
//    LOOKUP_DEPTH__RT = 6 bits are used as radix bits.
//    so 'slot' directly selects root CNode[slot].
// => we will finally get the cap in the 'slot' of 'root CNode'
//
            DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND),  GRANT_SLOT, LOOKUP_DEPTH__RT,
// Similarly, slot 3 of delegation CNode of the first delegator PD
// points to the cap of the delegation CNode itself (i.e., a self-pointing cap)
// (for lookup analysis, check delegator.c for details...)
            CPTR_DGT_CND,            DELEGATION_SLOT_GRANT_CAP, LOOKUP_DEPTH__DL,
            seL4_AllRights
        );
#else
        err = seL4_CNode_Copy(
            DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND),  GRANT_SLOT, LOOKUP_DEPTH__RT,
            CPTR_DGT_CND,            DELEGATION_SLOT_GRANT_CAP, LOOKUP_DEPTH__DL,
            seL4_AllRights
        );
#endif
        if (err != seL4_NoError) {
            microkit_dbg_puts("[delegatee] failed to grant delegation CNode\n");
            for (;;);
        }
        break;

    case PPC_DELEGATION_RELEASE:
        microkit_dbg_puts("[delegatee] release delegation CNode\n");

        // Remove the delegation CNode cap from delegator.root[15].
        err = seL4_CNode_Delete(
            DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND), GRANT_SLOT, LOOKUP_DEPTH__RT
        );

        if (err != seL4_NoError) {
            microkit_dbg_puts("[delegatee] failed to remove delegation CNode\n");
            for (;;);
        }

        microkit_dbg_puts("[delegatee] notify delegator\n");
        microkit_notify(CH_DELEGATEE);
        break;

    default:
        microkit_dbg_puts("[delegatee] invalid delegation request\n");
        break;
    }

    return microkit_msginfo_new(0, 0);
}
