/*
 * test_button.c — button widget end-to-end (T5.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

TIMUI_TEST(test_button_click){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiButtonResult b;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);

    /* SGR press at col 3,row 2 -> cell (2,1), inside the button */
    SETIN(&fake, "\x1b[<0;3;2M");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    TIMUI_CHECK(b.hovered && b.pressed);

    /* release over the same button -> click */
    SETIN(&fake, "\x1b[<0;3;2m");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    TIMUI_CHECK(b.clicked);

    timui_close(ui);
}

TIMUI_TEST(test_button_click_press_release_same_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiButtonResult b;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);

    SETIN(&fake, "\x1b[<0;3;2M\x1b[<0;3;2m");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    TIMUI_CHECK(b.hovered && b.clicked);

    timui_close(ui);
}

TIMUI_TEST(test_button_same_frame_release_outside_no_click){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiButtonResult b;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);

    SETIN(&fake, "\x1b[<0;3;2M\x1b[<0;30;8m");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    TIMUI_CHECK(!b.clicked && !b.hovered);

    timui_close(ui);
}

TIMUI_TEST(test_button_outside_no_click){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiButtonResult b;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);

    /* press + release far outside the button */
    SETIN(&fake, "\x1b[<0;30;8M");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    SETIN(&fake, "\x1b[<0;30;8m");
    timui_begin(ui, &f);
    b = timui_button(f, TIMUI_ID("ok"), r, TIMUI_STR_LIT("OK"));
    timui_end(f);
    TIMUI_CHECK(!b.clicked && !b.hovered);

    timui_close(ui);
}

TIMUI_TEST(test_button_label_clipped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiRect r = TIMUI_RECT(1, 1, 5, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 12, 4, &al);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    (void)timui_button(f, TIMUI_ID("long"), r, TIMUI_STR_LIT("abcdefghi"));
    TIMUI_CHECK(timui_cells_get(buf, 6, 1)->codepoint == 0);
    timui_end(f);

    timui_close(ui);
}
