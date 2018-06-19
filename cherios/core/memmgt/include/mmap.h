/*-
 * Copyright (c) 2017 Lawrence Esswood
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CHERIOS_MMAP_H_H
#define CHERIOS_MMAP_H_H

#include "mman.h"

#define DUMP_INTERVAL   0 // 1000000000ULL

#define MAX_CLAIMERS 0x4
#define DESC_ALLOC_CHUNK_PAGES  (0x1000)
#define DESC_ALLOC_CHUNK_SIZE   (DESC_ALLOC_CHUNK_PAGES * PHY_PAGE_SIZE)
#define DESC_ALLOC_N_PER_POOL   (DESC_ALLOC_CHUNK_SIZE/ (sizeof(vpage_range_desc_table_t)))

#define CHECK_PASS (2)
#define VISIT_CONT (1)
#define VISIT_DONE (0)

typedef struct claimer_link_t{
    size_t start_page;
    size_t index;
} claimer_link_t;

enum mop_state {
    active = 0,
    reclaiming = 1,
    dead = 2
};

typedef struct mop_internal_t {
    // Doubly linked list of claims

    claimer_link_t first;
    claimer_link_t last;


    struct mop_internal_t* parent_mop;
    struct mop_internal_t* child_mop;

    struct mop_internal_t* next_sibling_mop;
    struct mop_internal_t* prev_sibling_mop;

    const char* debug_id;

    // Allocation tracking
    size_t allocated_pages;

    // TODO this is much more difficult with the current structure
    // size_t commited_pages;

    // Book-keeping complexity. Also adds cost so we may add a limit
    size_t allocated_ranges;

    enum mop_state state;
} mop_internal_t;

_Static_assert(MOP_REQUIRED_SPACE >= sizeof(mop_internal_t), "Increase the size of mop required size");

typedef struct claimer_t {
    struct mop_internal_t* owner;
    claimer_link_t next;
    claimer_link_t prev;
    size_t n_claims;
} claimer_t;

enum allocation_type_t {
    free_node = 0,          // Result of init
    open_node = 1,          // Has an open reservation
    allocation_node = 2,    // Result of allocation.
    internal_node = 4,      // Not a leaf
    tomb_node = 5           // Everything freed
};

#define FOLLOW(X) (hard_index((X).start_page)->claimers[X.index])
#define FOLLOW_DESC(X) (hard_index((X).start_page))
#define INDEX_FROM_LNK(X) ((X).index)
#define MAKE_CLAIM_LINK(link, desc, index) link.start_page = desc->start; link.index = index
#define LINK_IS_END(link) ((link).start_page == 0)
#define LINK_SET_END(link) (link).start_page = 0

#define FOREACH_CLAIMER(desc, index, claim)     \
size_t index;     \
claimer_t* claim; \
for(index = 0; claim = &desc->claimers[index], index != MAX_CLAIMERS; index++)

#define FOREACH_CLAIMED_RANGE(owner, desc, lnk)  \
claimer_link_t lnk;             \
vpage_range_desc_t* desc;       \
claimer_t* claim;                \
claimer_link_t lnk_tmp;         \
for(lnk = owner->first; lnk.start_page != 0 && (desc = FOLLOW_DESC(lnk), claim = &desc->claimers[INDEX_FROM_LNK(lnk)], lnk_tmp = claim->next, 1); lnk = lnk_tmp)

typedef struct vpage_range_desc_t {
    // An array of all claimers. Each part of a doubly linked list managed by a mop
    claimer_t claimers[MAX_CLAIMERS];

    // A pointer to the sub-table (if it exists, this node may summarise)
    struct vpage_range_desc_table_t* sub_table;

    // If this is an allocation node (of non zero allocation length) this is a taken reservation
    // If this is an open node this will be the open reservation to hand out
    res_t reservation;

    // This information is valid for length virtual pages. Its start vaddr is start.
    size_t start;
    size_t length;
    size_t prev;             // Only valid for leaf nodes
    size_t allocated_length; // Only valid in allocation node to track splits

    enum allocation_type_t allocation_type;

    uint8_t claims_used;

} vpage_range_desc_t;

typedef struct vpage_range_desc_table_t {
    // Our structure mirrors the actual vtables - when we can free a desc-table we can also free the corresponding vtable

    vpage_range_desc_t descs[PAGE_TABLE_ENT_PER_TABLE];

    // These are for tracking. We can use them to make allocation decisions or early free decisions
    size_t ranges_allocated;

    // TODO these are not updated yet
    size_t pages_allocated;
    size_t pages_commited;
    size_t pages_freed;

    // This is for our little table allocator
    struct vpage_range_desc_table_t* next_free;
} vpage_range_desc_table_t;

extern capability mop_sealing_cap;
extern vpage_range_desc_table_t desc_table_root;

static inline mop_internal_t* unseal_mop(mop_t mop) {

    mop_internal_t* unsealed = (mop_internal_t*)cheri_unseal_2(mop, mop_sealing_cap);
    if(unsealed == NULL) return NULL;

    enum mop_state result;

    ENUM_VMEM_SAFE_DEREFERENCE(&(unsealed->state), result, dead);

    if(result != active) return NULL;

    return unsealed;
}

static inline mop_t seal_mop(mop_internal_t* mop_internal) {
    return cheri_seal(mop_internal, mop_sealing_cap);
}

extern act_kt general_act;
extern act_kt commit_act;
extern act_kt revoke_act;
extern act_kt clean_act;

void revoke(void);
void revoke_finish(res_t res);
void __revoke(void);
void __revoke_finish(res_t res);

int __mem_claim(size_t base, size_t length, size_t times, mop_t mop_sealed);
int __mem_release(size_t base, size_t length, size_t times, mop_t mop_sealed);
ERROR_T(res_t) __mem_request(size_t base, size_t length, mem_request_flags flags, mop_t mop_sealed);
mop_t __init_mop(capability sealing_cap, res_t big_res);
ERROR_T(mop_t) __mem_makemop(res_t space, mop_t mop_sealed, const char* debug_id);
int __mem_reclaim_mop(mop_t mop_sealed);

void mmap_dump(void);

#endif //CHERIOS_MMAP_H_H
