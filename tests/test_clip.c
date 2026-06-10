/*
 * test_clip.c — clip stack (T4.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

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
    timui_draw_fill(buf, TIMUI_RECT(0, 0, 20, 10), timui_style_make(0xFFFFFF, 0, 0));
    timui_pop_clip(f);

    TIMUI_CHECK(timui_cells_get(buf, 3, 3)->codepoint != 0);   /* inside -> filled */
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);   /* outside -> empty */
    TIMUI_CHECK(timui_cells_get(buf, 6, 3)->codepoint == 0);   /* x >= 6 -> outside */

    timui_end(f);
    timui_close(ui);
}
