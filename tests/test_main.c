/*
 * test_main.c — registers and runs all timui.h unit tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "test.h"

int timui_test_failures = 0;

int main(void){
    typedef void (*test_fn)(void);
    test_fn tests[] = {
        test_rect_cuts,
        test_rect_split,
        test_rect_inset_clamp,
        test_ids_stable,
        test_error_string,
        test_version,
        test_default_allocator,
        test_arena_alloc_reset,
        test_arena_oom,
        test_arena_alignment,
        test_arena_custom_allocator,
        test_arena_invalid_args,
        test_str_from_cstr,
        test_str_copy_bounded,
        test_str_slice,
        test_str_eq_cstr,
        test_str_invalid_utf8_passthrough,
        test_id_stack_stable,
        test_id_stack_nesting_order,
        test_id_stack_pop_restore,
        test_id_stack_empty_and_grow,
        test_msgq_order_and_copy,
        test_msgq_full_predictable,
        test_msgq_variable_sizes,
        test_mpsc_fifo_single,
        test_mpsc_multi_producer,
    };
    size_t i;
    size_t n = sizeof(tests) / sizeof(tests[0]);

    for(i = 0; i < n; ++i) tests[i]();

    if(timui_test_failures == 0)
        printf("all tests passed (%zu)\n", n);
    else
        printf("%d CHECK failure(s)\n", timui_test_failures);

    return timui_test_failures == 0 ? 0 : 1;
}
