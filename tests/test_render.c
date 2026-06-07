/*
 * test_render.c — diff renderer (T3.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_render_diff_exact){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    /* CUP(0,0) + SGR reset + fg truecolor white + "Hi"; the adjacent 'i' needs
     * no repositioning and no SGR (same style). */
    static const char expected[] = "\x1b[1;1H\x1b[0m\x1b[38;2;255;255;255mHi";

    timui_cells_init(&prev, 10, 5, &al);
    timui_cells_init(&curr, 10, 5, &al);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Hi"), timui_style_make(0xffffff, 0, 0));
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_render_unchanged_emits_nothing){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    TimuiStyle s = timui_style_make(0xffffff, 0, 0);
    timui_cells_init(&prev, 10, 5, &al);
    timui_cells_init(&curr, 10, 5, &al);
    timui_draw_text(&prev, 0, 0, TIMUI_STR_LIT("Hi"), s);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Hi"), s);
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 0);    /* identical frames -> no output at all */
    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&f);
}
