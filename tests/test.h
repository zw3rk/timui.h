/*
 * test.h — tiny unit-test harness for timui.h (test-only).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIMUI_TEST_H
#define TIMUI_TEST_H

#include <stddef.h>
#include <stdio.h>

/* Global failure counter (test-only mutable state — not production code). */
extern int timui_test_failures;

/* A test is a void function; failures bump the global counter. */
#define TIMUI_TEST(name) void name(void)
#define TIMUI_CHECK(cond)                                                   \
    do {                                                                    \
        if(!(cond)) {                                                       \
            ++timui_test_failures;                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                   \
    } while(0)

/* Registry of unit tests — add one line per new test function. */
void test_rect_cuts(void);
void test_rect_split(void);
void test_rect_inset_clamp(void);
void test_ids_stable(void);
void test_error_string(void);
void test_version(void);
void test_default_allocator(void);
void test_arena_alloc_reset(void);
void test_arena_oom(void);
void test_arena_alignment(void);
void test_arena_custom_allocator(void);
void test_arena_invalid_args(void);
void test_str_from_cstr(void);
void test_str_copy_bounded(void);
void test_str_slice(void);
void test_str_eq_cstr(void);
void test_str_invalid_utf8_passthrough(void);
void test_id_stack_stable(void);
void test_id_stack_nesting_order(void);
void test_id_stack_pop_restore(void);
void test_id_stack_empty_and_grow(void);
void test_msgq_order_and_copy(void);
void test_msgq_full_predictable(void);
void test_msgq_variable_sizes(void);
void test_mpsc_fifo_single(void);
void test_mpsc_multi_producer(void);

#endif /* TIMUI_TEST_H */
