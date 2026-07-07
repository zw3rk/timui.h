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
                    timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0));
    timui_fake_clear_output(&fake);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len > 0);              /* something was rendered */

    /* frame 2: identical content -> stable, no output */
    timui_fake_clear_output(&fake);
    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"),
                    timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0));
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

/* G7: the event queue holds a WHOLE read (512 slots > the 256-byte read buffer),
 * so a burst of typed text — e.g. a Finder drag-drop, which the terminal inserts
 * as plain text, one event per char — is delivered in full, not dropped (the
 * 16-slot queue truncated a dropped path to its first 16 chars). The
 * events_dropped counter remains as a safety net and still resets on read. */
TIMUI_TEST(test_events_dropped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    static char burst[120];
    int k;
    for(k = 0; k < (int)sizeof burst; k++) burst[k] = (char)('a' + (k % 26));
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_fake_set_input(&fake, burst, sizeof burst);   /* 120 text events, one read */
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_events_dropped(ui) == 0);   /* all fit — nothing dropped */
    TIMUI_CHECK(timui_events_dropped(ui) == 0);   /* still zero (and reset) after read */
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_begin_preserves_focus_events){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiEvent ev;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_fake_set_input(&fake, "\x1b[I", sizeof("\x1b[I") - 1);
    timui_begin(ui, &f);

    TIMUI_CHECK(timui_poll_event(ui, &ev));
    TIMUI_CHECK(ev.kind == TIMUI_EVENT_FOCUS && ev.as.focus.focused);

    timui_end(f);
    timui_close(ui);
}
