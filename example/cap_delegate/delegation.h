/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

// Size of microkit/delegation CNode
#define MK_CAP_BITS 9
// Size of root CNode
#define ROOT_CAP_BITS 6

#define LOOKUP_DEPTH__MK (MK_CAP_BITS)
#define LOOKUP_DEPTH__RT (ROOT_CAP_BITS)
#define LOOKUP_DEPTH__DL (seL4_WordBits - ROOT_CAP_BITS - MK_CAP_BITS)

// Request types provided by the delegatee...
#define PPC_DELEGATION_GRANT   0
#define PPC_DELEGATION_RELEASE 1

// Delegatee PD will put the cap of delegation CNode to this slot
// at the root CNode of the delegator PD temporarily, in response
// to the "PPC_DELEGATION_GRANT" request...
#define GRANT_SLOT 15

// Delegation CNode layout.
#define DELEGATION_SLOT_MICROKIT_CNODE 1
#define DELEGATION_SLOT_ROOT_CNODE     2
#define DELEGATION_SLOT_GRANT_CAP      3
#define DELEGATION_SLOT_VSPACE         4

// Must match DLG_MR_CAP in builder.rs.
#define DELEGATION_BASE_MR_CAP (10 + 64 + 64)

#define DGT_CPTR__DGTR_VSPACE(CPTR_DGT_CND) \
    ((CPTR_DGT_CND) | DELEGATION_SLOT_VSPACE)

#define DGT_CPTR__MR_FRAME(CPTR_DGT_CND, IDX) \
    ((CPTR_DGT_CND) | (DELEGATION_BASE_MR_CAP + (IDX)))

// Delegator PD's Microkit CNode, accessible through the delegation CNode.
#define DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND) \
    (CPTR_DGT_CND | DELEGATION_SLOT_MICROKIT_CNODE)

// Delegator PD's root CNode, accessible through the delegation CNode.
#define DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND) \
    (CPTR_DGT_CND | DELEGATION_SLOT_ROOT_CNODE)

static inline char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

/* stolen from monitor/src/util.c */
static inline void puthex64(seL4_Uint64 val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}
