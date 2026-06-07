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
        test_fake_capture_output,
        test_fake_inject_input,
        test_fake_grows,
        test_screen_enter_emits_modes,
        test_screen_exit_reverses,
        test_input_arrows,
        test_input_function_keys,
        test_input_tilde_edit_keys,
        test_input_control_chars,
        test_input_text_and_alt,
        test_input_partial_then_complete,
        test_input_utf8,
        test_input_invalid_safe,
        test_mouse_press_release,
        test_mouse_wheel_and_motion,
        test_focus_events,
        test_bracketed_paste,
        test_termios_raw_and_restore,
        test_term_size_query,
        test_term_size_not_a_tty,
        test_caps_modern_kitty_family,
        test_caps_multiplexer_reduces,
        test_caps_unknown_fallback,
        test_caps_force_masks,
        test_kitty_csi_u_plain,
        test_kitty_csi_u_with_mods,
        test_kitty_special_codes,
        test_kitty_mods_on_arrows,
        test_sync_begin_end_bytes,
        test_cursor_hide_show_bytes,
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
