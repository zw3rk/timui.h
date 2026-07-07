/*
 * test_arena.c — default allocator + bump arena (T1.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdint.h>
#include <stdlib.h>

/* Counting allocator: proves the arena routes through a custom allocator. */
static void *counting_alloc(void *ud, size_t sz){
    (*(int *)ud) += 1;
    return malloc(sz);
}
static void *counting_realloc(void *ud, void *p, size_t os, size_t ns){
    (void)ud; (void)os; (void)ns; return realloc(p, ns);
}
static void counting_free(void *ud, void *p, size_t sz){
    (void)ud; (void)sz; free(p);
}

typedef struct {
    void *raw;
} MisalignAlloc;

static void *misalign_alloc(void *ud, size_t sz){
    MisalignAlloc *ma = (MisalignAlloc *)ud;
    unsigned char *raw = (unsigned char *)malloc(sz + 64);
    int off;
    if(!raw) return NULL;
    ma->raw = raw;
    for(off = 0; off < 64; off += 16){
        unsigned char *p = raw + off;
        if(((uintptr_t)p % 64) != 0) return p;
    }
    return raw + 16;
}
static void *misalign_realloc(void *ud, void *p, size_t os, size_t ns){
    (void)ud; (void)p; (void)os; (void)ns; return NULL;
}
static void misalign_free(void *ud, void *p, size_t sz){
    MisalignAlloc *ma = (MisalignAlloc *)ud;
    (void)p; (void)sz;
    free(ma->raw);
    ma->raw = NULL;
}

TIMUI_TEST(test_default_allocator){
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(a.alloc != NULL && a.realloc != NULL && a.free != NULL);

    void *p = a.alloc(a.userdata, 64);
    TIMUI_CHECK(p != NULL);
    a.free(a.userdata, p, 64);
}

TIMUI_TEST(test_arena_alloc_reset){
    TimuiArena ar;
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(timui_arena_init(&ar, &a, 256) == TIMUI_OK);

    unsigned char *p1 = timui_arena_alloc(&ar, 10, 8);
    TIMUI_CHECK(p1 != NULL && p1 == ar.base);   /* first alloc lands at base */

    unsigned char *p2 = timui_arena_alloc(&ar, 10, 8);
    TIMUI_CHECK(p2 != NULL && p2 >= p1 + 10);   /* second advances */

    timui_arena_reset(&ar);
    unsigned char *p3 = timui_arena_alloc(&ar, 10, 8);
    TIMUI_CHECK(p3 == p1);                       /* reset reuses from base */

    timui_arena_free(&ar);
}

TIMUI_TEST(test_arena_oom){
    TimuiArena ar;
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(timui_arena_init(&ar, &a, 16) == TIMUI_OK);
    TIMUI_CHECK(timui_arena_alloc(&ar, 10, 1) != NULL);  /* fits */
    TIMUI_CHECK(timui_arena_alloc(&ar, 10, 1) == NULL);  /* over cap -> NULL */
    timui_arena_free(&ar);
}

TIMUI_TEST(test_arena_alignment){
    TimuiArena ar;
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(timui_arena_init(&ar, &a, 64) == TIMUI_OK);
    timui_arena_alloc(&ar, 1, 1);                          /* off = 1 */
    unsigned char *p = timui_arena_alloc(&ar, 4, 16);
    TIMUI_CHECK(p != NULL && ((uintptr_t)p % 16) == 0);
    timui_arena_free(&ar);
}

TIMUI_TEST(test_arena_alignment_uses_base_address){
    TimuiArena ar;
    MisalignAlloc ma = {0};
    TimuiAllocator a = {0};
    unsigned char *p;

    a.userdata = &ma;
    a.alloc = misalign_alloc;
    a.realloc = misalign_realloc;
    a.free = misalign_free;

    TIMUI_CHECK(timui_arena_init(&ar, &a, 128) == TIMUI_OK);
    TIMUI_CHECK(((uintptr_t)ar.base % 64) != 0);
    p = timui_arena_alloc(&ar, 1, 64);
    TIMUI_CHECK(p != NULL && ((uintptr_t)p % 64) == 0);
    timui_arena_free(&ar);
}

TIMUI_TEST(test_arena_custom_allocator){
    TimuiArena ar;
    int count = 0;
    TimuiAllocator a = {0};
    a.userdata = &count;
    a.alloc = counting_alloc; a.realloc = counting_realloc; a.free = counting_free;

    TIMUI_CHECK(timui_arena_init(&ar, &a, 32) == TIMUI_OK);
    TIMUI_CHECK(count == 1);   /* backing buffer allocated through our allocator */
    timui_arena_free(&ar);
}

TIMUI_TEST(test_partial_allocator_rejected){
    int count = 0;
    TimuiAllocator partial = {0};
    TimuiArena ar;
    TimuiCellBuffer cells;
    TimuiMsgQueue msgq;
    TimuiMpsc mpsc;
    TimuiIdStack ids;
    TimuiFakeTransport fake;
    TimuiTransport transport = {0};
    Timui *ui = NULL;

    partial.userdata = &count;
    partial.alloc = counting_alloc;
    partial.free = counting_free;

    TIMUI_CHECK(timui_arena_init(&ar, &partial, 32) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_cells_init(&cells, 2, 2, &partial) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_msgq_init(&msgq, &partial, 64) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_mpsc_init(&mpsc, &partial) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_id_stack_init(&ids, &partial, 4) == TIMUI_ERR_INVALID_ARGUMENT);

    TIMUI_CHECK(timui_fake_init(&fake, &partial) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_open_for_test(&ui, transport, 2, 2, &partial) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);
}

TIMUI_TEST(test_arena_invalid_args){
    TimuiArena ar;
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(timui_arena_init(NULL, &a, 64)    == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_arena_init(&ar, NULL, 64)   == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_arena_init(&ar, &a, 0)      == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_arena_alloc(NULL, 4, 8)     == NULL);
    TIMUI_CHECK(timui_arena_alloc(&ar, 4, 0)      == NULL);  /* align 0 invalid */
}

/* V20: a non-power-of-two alignment silently misaligns (mask = align-1 is
 * wrong); reject it. */
TIMUI_TEST(test_arena_non_pow2_align){
    TimuiArena ar;
    TimuiAllocator a = timui_default_allocator();
    TIMUI_CHECK(timui_arena_init(&ar, &a, 64) == TIMUI_OK);
    TIMUI_CHECK(timui_arena_alloc(&ar, 4, 3) == NULL);   /* 3 is not a power of two */
    TIMUI_CHECK(timui_arena_alloc(&ar, 4, 4) != NULL);   /* 4 is */
    timui_arena_free(&ar);
}
