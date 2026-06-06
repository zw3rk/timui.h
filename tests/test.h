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
void test_fake_capture_output(void);
void test_fake_inject_input(void);
void test_fake_grows(void);
void test_screen_enter_emits_modes(void);
void test_screen_exit_reverses(void);
void test_input_arrows(void);
void test_input_function_keys(void);
void test_input_tilde_edit_keys(void);
void test_input_control_chars(void);
void test_input_text_and_alt(void);
void test_input_partial_then_complete(void);
void test_input_utf8(void);
void test_input_invalid_safe(void);

#endif /* TIMUI_TEST_H */
