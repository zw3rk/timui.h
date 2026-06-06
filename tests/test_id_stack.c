/*
 * test_id_stack.c — ID composition / stack (T1.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

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
