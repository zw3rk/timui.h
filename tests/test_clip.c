/*
 * test_clip.c — clip stack (T4.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>

TIMUI_TEST(test_clip_restricts_drawing){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    timui_push_clip(f, TIMUI_RECT(2, 2, 4, 2));   /* x in [2,6), y in [2,4) */
    timui_draw_fill(buf, TIMUI_RECT(0, 0, 20, 10), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    timui_pop_clip(f);

    TIMUI_CHECK(timui_cells_get(buf, 3, 3)->codepoint != 0);   /* inside -> filled */
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);   /* outside -> empty */
    TIMUI_CHECK(timui_cells_get(buf, 6, 3)->codepoint == 0);   /* x >= 6 -> outside */

    timui_end(f);
    timui_close(ui);
}

/* V/S7: nested clips intersect, so a panel inside a panel only draws in the
 * overlap. */
TIMUI_TEST(test_clip_nested_intersect){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_push_clip(f, TIMUI_RECT(2, 2, 8, 4));    /* x[2,10) y[2,6) */
    timui_push_clip(f, TIMUI_RECT(4, 4, 8, 4));    /* intersect -> x[4,10) y[4,6) */
    timui_draw_fill(buf, TIMUI_RECT(0, 0, 20, 10), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    timui_pop_clip(f);
    timui_pop_clip(f);
    TIMUI_CHECK(timui_cells_get(buf, 5, 5)->codepoint != 0);   /* in intersection */
    TIMUI_CHECK(timui_cells_get(buf, 3, 3)->codepoint == 0);   /* in outer only -> clipped */
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_clip_stack_overflow_preserves_pop_symmetry){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
    int i;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    for(i = 0; i < 9; i++) timui_push_clip(f, TIMUI_RECT(i, 0, 20 - i, 3));
    timui_pop_clip(f);

    timui_draw_text(buf, 6, 0, TIMUI_STR_LIT("X"), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    timui_draw_text(buf, 7, 0, TIMUI_STR_LIT("Y"), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));

    TIMUI_CHECK(timui_cells_get(buf, 6, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(buf, 7, 0)->codepoint == 'Y');

    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_clip_wide_glyph_requires_full_width){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 4, 1, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    timui_push_clip(f, TIMUI_RECT(0, 0, 1, 1));
    timui_draw_text(buf, 0, 0, TIMUI_STR_LIT("\xE4\xB8\xAD"),
                    timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    timui_pop_clip(f);

    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(buf, 1, 0)->codepoint == 0);

    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_clip_extreme_rect_does_not_overflow){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 4, 1, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    timui_push_clip(f, TIMUI_RECT(INT_MAX, 0, INT_MAX, 1));
    timui_draw_fill(buf, TIMUI_RECT(0, 0, 4, 1), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    timui_pop_clip(f);

    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(buf, 3, 0)->codepoint == 0);

    timui_end(f);
    timui_close(ui);
}

/* pop with an empty stack must be a no-op, not a crash. */
TIMUI_TEST(test_clip_pop_underflow_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 10, &al);
    timui_begin(ui, &f);
    timui_pop_clip(f);   /* no prior push */
    timui_pop_clip(f);
    timui_end(f);
    timui_close(ui);
}
