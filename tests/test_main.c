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
        test_cells_init_clear,
        test_cells_put_get_roundtrip,
        test_cells_resize,
        test_utf8_decode,
        test_utf8_width,
        test_draw_text,
        test_draw_fill,
        test_draw_box_single,
        test_draw_box_ascii,
        test_render_diff_exact,
        test_render_unchanged_emits_nothing,
        test_render_cursor_visible,
        test_render_cursor_hidden,
        test_frame_lifecycle,
        test_frame_quit_flag,
        test_interact_click,
        test_interact_hover_only,
        test_interact_tab_cycles,
        test_interact_keyboard_activate,
        test_theme_dos_blue,
        test_theme_mono_default,
        test_theme_style_lookup,
        test_button_click,
        test_button_outside_no_click,
        test_checkbox_toggles,
        test_radio_selects,
        test_panel_body_rect,
        test_input_types_and_submits,
        test_listbox_down_key,
        test_listbox_click_selects,
        test_message_box_miss,
        test_fuzz_parser_random_stream,
        test_fuzz_parser_adversarial,
        test_clip_restricts_drawing,
        test_menu_open_and_select,
        test_menu_outside_click_closes,
        test_modal_traps_background,
        test_hyperlink_renders_osc8,
        test_esc_timeout_flushes,
        test_esc_arrow_not_delayed,
        test_scroll_view_clips,
        test_theme_modern_light,
        test_clipboard_osc52,
        test_keymap_bind,
        test_table_renders,
        test_tree_renders,
        test_cmd_palette_filter,
        test_snapshot_row_eq,
        test_snapshot_grid_full,
        test_snapshot_grid_eq_same,
        test_snapshot_grid_eq_diff,
        test_snapshot_grid_eq_dim,
        test_snapshot_goldens,
#if __has_include(<vterm.h>)
        /* Tier A libvterm round-trip tests — only present when libvterm is on
         * the include path (WITH_VTERM=1), matching tests/test_vt_roundtrip.c. */
        test_vt_plain_text,
        test_vt_rainbow,
        test_vt_attrs,
        test_vt_box_glyphs,
        test_vt_fill,
        test_vt_hyperlink,
        test_vt_partial_update,
        test_vt_wide_glyph,
#endif
        test_text_area_renders,
        test_conpty_unsupported,
        test_kitty_graphics_transmit,
        test_kitty_graphics_placeholder,
        test_pty_hello_exits_on_esc,
        test_paste_cross_feed_terminator,
        test_esc_timeout_zero_now,
        test_msgq_overflow_guard,
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
