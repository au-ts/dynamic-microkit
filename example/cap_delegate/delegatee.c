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

#define CHILD_DELEGATOR ((microkit_child)0)

#define DELEGATED_MR_VADDR 0xC00000
#define DELEGATED_MR_SIZE  0x1000

static seL4_Error map_delegated_page(seL4_CPtr frame, seL4_CPtr vspace, seL4_Word vaddr)
{
#if defined(CONFIG_ARCH_AARCH64)
    return seL4_ARM_Page_Map(
        frame,
        vspace,
        vaddr,
        seL4_ReadWrite,
        seL4_ARM_Default_VMAttributes
    );
#elif defined(CONFIG_ARCH_RISCV)
    return seL4_RISCV_Page_Map(
        frame,
        vspace,
        vaddr,
        seL4_ReadWrite,
        seL4_RISCV_Default_VMAttributes
    );
#elif defined(CONFIG_ARCH_X86_64)
    return seL4_X86_Page_Map(
        frame,
        vspace,
        vaddr,
        seL4_ReadWrite,
        seL4_X86_Default_VMAttributes
    );
#else
#error "Unsupported architecture"
#endif
}


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

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);

    microkit_dbg_puts("[delegatee] fault from child ");
    microkit_dbg_put32(child);
    microkit_dbg_puts("\n");

    if (child != CHILD_DELEGATOR) {
        microkit_dbg_puts("[delegatee] unexpected child\n");
        return seL4_False;
    }

    if (label != seL4_Fault_VMFault) {
        microkit_dbg_puts("[delegatee] unexpected fault type\n");
        return seL4_False;
    }

    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);

    microkit_dbg_puts("[delegatee] VM fault address: ");
    puthex64(fault_addr);
    microkit_dbg_puts("\n");

    if (fault_addr < DELEGATED_MR_VADDR ||
        fault_addr >= DELEGATED_MR_VADDR + DELEGATED_MR_SIZE) {
        microkit_dbg_puts("[delegatee] fault outside delegated MR\n");
        return seL4_False;
    }

    // delegation CNode[4] -> delegator VSpace
    seL4_CPtr vspace =
        DGT_CPTR__DGTR_VSPACE(CPTR_DGT_CND);

    // delegation CNode[138] -> first frame of delegated MR
    seL4_CPtr frame =
        DGT_CPTR__MR_FRAME(CPTR_DGT_CND, 0);

    microkit_dbg_puts("[delegatee] map delegated frame\n");

    seL4_Error err = map_delegated_page(
        frame,
        vspace,
        DELEGATED_MR_VADDR
    );

    if (err != seL4_NoError) {
        microkit_dbg_puts("[delegatee] Page_Map failed: ");
        microkit_dbg_put32(err);
        microkit_dbg_puts("\n");
        return seL4_False;
    }

    microkit_dbg_puts("[delegatee] delegated MR mapped\n");

    *reply_msginfo = microkit_msginfo_new(0, 0);

    // Reply to the fault. The delegator resumes from the fault restart PC,
    // so the faulting memory access is retried.
    return seL4_True;
}
