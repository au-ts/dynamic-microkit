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

// Delegator PD's Microkit CNode, accessible through the delegation CNode.
#define DGT_CPTR__DGTR_MK_CND(CPTR_DGT_CND) \
    (CPTR_DGT_CND | DELEGATION_SLOT_MICROKIT_CNODE)

// Delegator PD's root CNode, accessible through the delegation CNode.
#define DGT_CPTR__DGTR_ROOT_CND(CPTR_DGT_CND) \
    (CPTR_DGT_CND | DELEGATION_SLOT_ROOT_CNODE)
