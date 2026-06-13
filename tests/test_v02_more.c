/*
 * test_v02_more.c — snapshot testing, text-area, ConPTY (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

/* ---- snapshot testing (#53) ---- */
TIMUI_TEST(test_snapshot_row_eq){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    timui_cells_init(&b, 10, 3, &al);
    timui_draw_text(&b, 0, 1, TIMUI_STR_LIT("Hello"), timui_style_make(0xFFFFFF, 0, 0));
    TIMUI_CHECK(timui_snapshot_row_eq(&b, 1, "Hello     "));  /* 10 cells: Hello + 5 spaces */
    TIMUI_CHECK(!timui_snapshot_row_eq(&b, 1, "World      "));
    timui_cells_destroy(&b);
}

/* ---- text-area (#49) ---- */
TIMUI_TEST(test_text_area_renders){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[64] = "Hello";
    TimuiTextAreaState tas = { text, sizeof text, 5, 0 };
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_text_area(f, TIMUI_ID("ta"), TIMUI_RECT(0, 0, 20, 3), &tas);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'H');
    timui_end(f);
    timui_close(ui);
}

/* ---- ConPTY (#55) ---- */
TIMUI_TEST(test_conpty_unsupported){
    TimuiTransport tr;
    int pid;
    TIMUI_CHECK(timui_conpty_open(&tr, &pid) == TIMUI_ERR_UNSUPPORTED);
}
