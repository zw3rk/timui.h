/*
 * test_id_stack.c — ID composition / stack (T1.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdlib.h>
#include <string.h>

static TimuiId push_path(TimuiIdStack *s, const char *a, const char *b){
    timui_id_stack_push_cstr(s, a);
    timui_id_stack_push_cstr(s, b);
    return timui_id_stack_current(s);
}

TIMUI_TEST(test_id_stack_stable){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s1, s2;
    TimuiId c1, c2;
    TIMUI_CHECK(timui_id_stack_init(&s1, &al, 8) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_init(&s2, &al, 8) == TIMUI_OK);
    c1 = push_path(&s1, "left", "item");
    c2 = push_path(&s2, "left", "item");
    TIMUI_CHECK(c1 == c2);                  /* stable across instances/frames */
    timui_id_stack_destroy(&s1);
    timui_id_stack_destroy(&s2);
}

TIMUI_TEST(test_id_stack_nesting_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s;
    TimuiId ab, ba;
    TIMUI_CHECK(timui_id_stack_init(&s, &al, 8) == TIMUI_OK);
    ab = push_path(&s, "a", "b");
    timui_id_stack_pop(&s);
    timui_id_stack_pop(&s);
    ba = push_path(&s, "b", "a");
    TIMUI_CHECK(ab != ba);                  /* nested path order matters */
    timui_id_stack_destroy(&s);
}

TIMUI_TEST(test_id_stack_pop_restore){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s;
    TimuiId after_a, after_b, restored;
    TIMUI_CHECK(timui_id_stack_init(&s, &al, 8) == TIMUI_OK);
    timui_id_stack_push_cstr(&s, "a");
    after_a = timui_id_stack_current(&s);
    timui_id_stack_push_cstr(&s, "b");
    after_b = timui_id_stack_current(&s);
    TIMUI_CHECK(after_a != after_b);
    timui_id_stack_pop(&s);
    restored = timui_id_stack_current(&s);
    TIMUI_CHECK(restored == after_a);       /* pop restores the parent seed */
    timui_id_stack_destroy(&s);
}

TIMUI_TEST(test_id_stack_empty_and_grow){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s;
    TIMUI_CHECK(timui_id_stack_init(&s, &al, 2) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_current(&s) == s.root);   /* empty -> root */
    timui_id_stack_pop(&s);                              /* pop on empty: no-op */
    TIMUI_CHECK(s.count == 0);
    timui_id_stack_push_cstr(&s, "x");
    timui_id_stack_push_cstr(&s, "y");
    timui_id_stack_push_cstr(&s, "z");                   /* forces realloc growth */
    TIMUI_CHECK(s.count == 3);
    TIMUI_CHECK(timui_id_stack_current(&s) != s.root);
    timui_id_stack_destroy(&s);
}

/* G6: push returns TimuiResult — positive (grow succeeds) and negative (OOM). */
typedef struct { int fail_at; int n; } FailAlloc;
static void *fa_alloc(void *ud, size_t sz){ (void)ud; return malloc(sz); }
static void *fa_realloc(void *ud, void *p, size_t os, size_t ns){
    FailAlloc *fa = (FailAlloc *)ud; (void)os;
    fa->n++;
    if(fa->n == fa->fail_at) return NULL;          /* fail this realloc */
    return realloc(p, ns);
}
static void fa_free(void *ud, void *p, size_t sz){ (void)ud; (void)sz; free(p); }

TIMUI_TEST(test_id_stack_push_ok){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s;
    TIMUI_CHECK(timui_id_stack_init(&s, &al, 2) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("a")) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("b")) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("c")) == TIMUI_OK);   /* grow succeeds */
    TIMUI_CHECK(s.count == 3);
    timui_id_stack_destroy(&s);
}

TIMUI_TEST(test_id_stack_push_oom){
    FailAlloc fa = { 1, 0 };   /* fail the 1st realloc (the grow) */
    TimuiAllocator al = { &fa, fa_alloc, fa_realloc, fa_free };
    TimuiIdStack s;
    TIMUI_CHECK(timui_id_stack_init(&s, &al, 2) == TIMUI_OK);
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("a")) == TIMUI_OK);   /* fits */
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("b")) == TIMUI_OK);   /* fills cap 2 */
    TIMUI_CHECK(timui_id_stack_push(&s, TIMUI_ID("c")) == TIMUI_ERR_OUT_OF_MEMORY);  /* grow fails */
    TIMUI_CHECK(s.count == 2);   /* failed push didn't increment — no corruption */
    /* pop works correctly (pops 'b', not a stale seed) */
    timui_id_stack_pop(&s);
    TIMUI_CHECK(s.count == 1);
    timui_id_stack_destroy(&s);
}

TIMUI_TEST(test_id_stack_init_overflow_guard){
    TimuiAllocator al = timui_default_allocator();
    TimuiIdStack s;
    memset(&s, 0xA5, sizeof s);
    TIMUI_CHECK(timui_id_stack_init(&s, &al, SIZE_MAX / sizeof(TimuiId) + 1) == TIMUI_ERR_OUT_OF_MEMORY);
    TIMUI_CHECK(s.seeds == NULL && s.cap == 0 && s.count == 0);
}
