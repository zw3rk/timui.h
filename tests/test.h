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
void test_arena_non_pow2_align(void);
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
void test_msgq_null_data_rejected(void);
void test_mpsc_fifo_single(void);
void test_mpsc_multi_producer(void);
void test_mpsc_overflow_guard(void);
void test_fake_capture_output(void);
void test_fake_inject_input(void);
void test_fake_grows(void);
void test_screen_enter_emits_modes(void);
void test_screen_exit_reverses(void);
void test_title_rejects_controls(void);
void test_input_arrows(void);
void test_input_function_keys(void);
void test_input_tilde_edit_keys(void);
void test_input_control_chars(void);
void test_input_text_and_alt(void);
void test_input_partial_then_complete(void);
void test_input_utf8(void);
void test_input_invalid_safe(void);
void test_mouse_press_release(void);
void test_mouse_wheel_and_motion(void);
void test_focus_events(void);
void test_bracketed_paste(void);
void test_input_esc_resync_no_loss(void);
void test_input_utf8_resync_no_loss(void);
void test_mouse_wheel_with_mods(void);
void test_input_nul_ignored(void);
void test_paste_cross_feed_three_fragments(void);
void test_paste_empty_no_event(void);
void test_termios_raw_and_restore(void);
void test_term_size_query(void);
void test_term_size_not_a_tty(void);
void test_caps_modern_kitty_family(void);
void test_caps_multiplexer_reduces(void);
void test_caps_multiplexer_kitty_passthrough(void);
void test_caps_unknown_fallback(void);
void test_caps_force_masks(void);
void test_kitty_csi_u_plain(void);
void test_kitty_csi_u_with_mods(void);
void test_kitty_special_codes(void);
void test_kitty_mods_on_arrows(void);
void test_sync_begin_end_bytes(void);
void test_cursor_hide_show_bytes(void);
void test_cells_init_clear(void);
void test_cells_put_get_roundtrip(void);
void test_cells_resize(void);
void test_cells_init_overflow_guard(void);
void test_resize_oom_keeps_dims(void);
void test_utf8_decode(void);
void test_utf8_width(void);
void test_utf8_decode_above_max(void);
void test_draw_text(void);
void test_draw_fill(void);
void test_draw_box_single(void);
void test_draw_box_ascii(void);
void test_render_diff_exact(void);
void test_render_unchanged_emits_nothing(void);
void test_render_diff_narrow_to_wide(void);
void test_render_diff_wide_to_narrow(void);
void test_render_hyperlink_uri_change(void);
void test_render_black_vs_default(void);
void test_render_cursor_visible(void);
void test_render_cursor_hidden(void);
void test_frame_lifecycle(void);
void test_frame_quit_flag(void);
void test_interact_click(void);
void test_interact_hover_only(void);
void test_interact_tab_cycles(void);
void test_interact_keyboard_activate(void);
void test_interact_tab_many_widgets(void);
void test_theme_dos_blue(void);
void test_theme_mono_default(void);
void test_theme_style_lookup(void);
void test_button_click(void);
void test_button_outside_no_click(void);
void test_checkbox_toggles(void);
void test_radio_selects(void);
void test_panel_body_rect(void);
void test_panel_title_clipped(void);
void test_input_types_and_submits(void);
void test_input_line_utf8_no_split(void);
void test_input_line_utf8_backspace(void);
void test_listbox_down_key(void);
void test_listbox_click_selects(void);
void test_listbox_selected_clamped(void);
void test_message_box_miss(void);
void test_message_box_button_clamped(void);
void test_fuzz_parser_random_stream(void);
void test_fuzz_parser_adversarial(void);
void test_clip_restricts_drawing(void);
void test_clip_nested_intersect(void);
void test_clip_pop_underflow_safe(void);
void test_menu_open_and_select(void);
void test_menu_outside_click_closes(void);
void test_modal_traps_background(void);
void test_hyperlink_renders_osc8(void);
void test_esc_timeout_flushes(void);
void test_esc_arrow_not_delayed(void);
void test_scroll_view_clips(void);
void test_theme_modern_light(void);
void test_clipboard_osc52(void);
void test_clipboard_huge_len_safe(void);
void test_keymap_bind(void);
void test_keymap_bind_overflow(void);
void test_keymap_hit_multi_binding(void);
void test_table_renders(void);
void test_tree_renders(void);
void test_tree_deep_safe(void);
void test_cmd_palette_filter(void);
void test_snapshot_row_eq(void);
void test_snapshot_grid_full(void);
void test_snapshot_grid_eq_same(void);
void test_snapshot_grid_eq_diff(void);
void test_snapshot_grid_eq_dim(void);
void test_snapshot_goldens(void);
void test_snapshot_grid_returns_would_be_length(void);
void test_snapshot_grid_size_query(void);
/* libvterm round-trip tests (Tier A) — only linked when WITH_VTERM=1.
 * Registered in test_main.c under #if __has_include(<vterm.h>). */
void test_vt_plain_text(void);
void test_vt_rainbow(void);
void test_vt_attrs(void);
void test_vt_box_glyphs(void);
void test_vt_fill(void);
void test_vt_hyperlink(void);
void test_vt_partial_update(void);
void test_vt_wide_glyph(void);
void test_text_area_renders(void);
void test_text_area_utf8_no_split(void);
void test_text_area_utf8_backspace(void);
void test_text_area_zero_cap_safe(void);
void test_text_area_cursor_overcap_safe(void);
void test_conpty_unsupported(void);
void test_kitty_graphics_transmit(void);
void test_kitty_graphics_chunking(void);
void test_signal_restore(void);
void test_kitty_graphics_placeholder(void);
void test_pty_hello_exits_on_esc(void);
void test_paste_cross_feed_terminator(void);
void test_esc_timeout_zero_now(void);
void test_msgq_overflow_guard(void);

#endif /* TIMUI_TEST_H */
