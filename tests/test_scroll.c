/*
 * test_scroll.c — scroll view clipping (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_scroll_view_clips){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    int i;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    {
        TimuiRect vp = TIMUI_RECT(0, 0, 10, 3);
        TimuiRect content = timui_scroll_begin(f, vp, 2);  /* skip first 2 rows */
        for(i = 0; i < 5; i++){
            char label[2] = { (char)('A' + i), '\0' };
            timui_label(f, content.x, content.y + i, timui_str_from_cstr(label),
                        timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
        }
        timui_scroll_end(f);
    }

    /* viewport rows 0-2 show items 2,3,4 (C,D,E) */
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'C');
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->codepoint == 'D');
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->codepoint == 'E');
    /* row 3 is below the viewport -> clipped, empty */
    TIMUI_CHECK(timui_cells_get(buf, 0, 3)->codepoint == 0);

    timui_end(f);
    timui_close(ui);
}
