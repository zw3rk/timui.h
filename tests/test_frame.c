/*
 * test_frame.c — frame lifecycle integration (T4.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_frame_lifecycle){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiStr out;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);
    TIMUI_CHECK(ui != NULL);

    /* frame 1: draw + end renders the diff */
    TIMUI_CHECK(timui_begin(ui, &f));
    TIMUI_CHECK(timui_width(f) == 10 && timui_height(f) == 5);
    TIMUI_CHECK(timui_root(f).w == 10 && timui_root(f).h == 5);
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"),
                    timui_style_make(0xffffff, 0, 0));
    timui_fake_clear_output(&fake);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len > 0);              /* something was rendered */

    /* frame 2: identical content -> stable, no output */
    timui_fake_clear_output(&fake);
    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"),
                    timui_style_make(0xffffff, 0, 0));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len == 0);

    /* resize updates the root rect */
    timui_ui_resize(ui, 20, 10);
    TIMUI_CHECK(timui_begin(ui, &f));
    TIMUI_CHECK(timui_width(f) == 20 && timui_height(f) == 10);
    timui_end(f);

    timui_close(ui);
}

TIMUI_TEST(test_frame_quit_flag){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 5, &al);
    TIMUI_CHECK(!timui_should_quit(ui));
    timui_quit(ui);
    TIMUI_CHECK(timui_should_quit(ui));
    timui_close(ui);
}
