/*
 * test_mouse.c — SGR mouse, focus, bracketed paste (T2.7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

typedef struct { TimuiEvent ev[16]; size_t n; } Sink;

static void mcb(void *ctx, const TimuiEvent *ev){
    Sink *s = (Sink *)ctx;
    if(s->n < 16) s->ev[s->n++] = *ev;
}

/* sizeof(lit)-1 so escape-sequence lengths can never be hand-miscounted. */
#define FEED(p, lit) timui_input_feed((p), (lit), sizeof(lit) - 1, mcb, &s)

TIMUI_TEST(test_mouse_press_release){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<0;10;20M");
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_MOUSE);
    TIMUI_CHECK(s.ev[0].as.mouse.x == 10 && s.ev[0].as.mouse.y == 20);
    TIMUI_CHECK(s.ev[0].as.mouse.button == 0 && s.ev[0].as.mouse.pressed);

    s.n = 0;
    FEED(&p, "\x1b[<2;3;4m");
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.mouse.button == 2 && s.ev[0].as.mouse.released);
    TIMUI_CHECK(s.ev[0].as.mouse.x == 3 && s.ev[0].as.mouse.y == 4);
}

TIMUI_TEST(test_mouse_sgr_cb3_is_release){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);

    FEED(&p, "\x1b[<3;5;6m");
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_MOUSE);
    TIMUI_CHECK(s.ev[0].as.mouse.x == 5 && s.ev[0].as.mouse.y == 6);
    TIMUI_CHECK(s.ev[0].as.mouse.button == -1);
    TIMUI_CHECK(!s.ev[0].as.mouse.pressed && s.ev[0].as.mouse.released);
}

TIMUI_TEST(test_mouse_malformed_sgr_ignored){
    TimuiInputParser p;
    Sink s;

    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<0;5M");
    TIMUI_CHECK(s.n == 0);

    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<;;M");
    TIMUI_CHECK(s.n == 0);

    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<0;0;1M");
    TIMUI_CHECK(s.n == 0);
}

TIMUI_TEST(test_mouse_wheel_and_motion){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<64;5;6M");
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.mouse.wheel_y == 1);
    TIMUI_CHECK(!s.ev[0].as.mouse.pressed && !s.ev[0].as.mouse.released);
    TIMUI_CHECK(s.ev[0].as.mouse.button == -1);
    s.n = 0;
    FEED(&p, "\x1b[<65;5;6M");
    TIMUI_CHECK(s.ev[0].as.mouse.wheel_y == -1);
    TIMUI_CHECK(!s.ev[0].as.mouse.pressed && !s.ev[0].as.mouse.released);
    TIMUI_CHECK(s.ev[0].as.mouse.button == -1);
    s.n = 0;
    FEED(&p, "\x1b[<32;7;8M");
    TIMUI_CHECK(s.ev[0].as.mouse.motion);
}

TIMUI_TEST(test_mouse_wheel_not_clicked){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    int x = -1, y = -1;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

    timui_fake_set_input(&fake, "\x1b[<64;5;3M", sizeof("\x1b[<64;5;3M") - 1);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_mouse_wheel(f) == 1);
    TIMUI_CHECK(!timui_mouse_clicked(f, &x, &y));
    timui_end(f);

    timui_close(ui);
}

TIMUI_TEST(test_mouse_motion_not_clicked){
    TimuiAllocator al = timui_default_allocator();
    TimuiInputParser p;
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    Sink s;
    int x = -1, y = -1;

    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[<35;5;6M");  /* no-button motion */
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_MOUSE);
    TIMUI_CHECK(s.ev[0].as.mouse.motion);
    TIMUI_CHECK(!s.ev[0].as.mouse.pressed);
    TIMUI_CHECK(!s.ev[0].as.mouse.released);
    TIMUI_CHECK(s.ev[0].as.mouse.button == -1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);
    timui_fake_set_input(&fake, "\x1b[<35;5;3M", sizeof("\x1b[<35;5;3M") - 1);
    timui_begin(ui, &f);
    TIMUI_CHECK(!timui_mouse_clicked(f, &x, &y));
    timui_end(f);

    timui_close(ui);
}

TIMUI_TEST(test_mouse_clicked_reports_press_cell){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    int x = -1, y = -1;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);

    timui_fake_set_input(&fake, "\x1b[<0;5;2M\x1b[<0;11;2m",
                         sizeof("\x1b[<0;5;2M\x1b[<0;11;2m") - 1);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_mouse_clicked(f, &x, &y));
    TIMUI_CHECK(x == 4 && y == 1);
    timui_end(f);

    timui_close(ui);
}

TIMUI_TEST(test_focus_events){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[I");
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_FOCUS && s.ev[0].as.focus.focused);
    s.n = 0;
    FEED(&p, "\x1b[O");
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_FOCUS && !s.ev[0].as.focus.focused);
}

TIMUI_TEST(test_bracketed_paste){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    FEED(&p, "\x1b[200~hello\x1b[201~");
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_PASTE);
    TIMUI_CHECK(s.ev[0].as.paste.len == 5);
    TIMUI_CHECK(s.ev[0].as.paste.ptr != NULL && memcmp(s.ev[0].as.paste.ptr, "hello", 5) == 0);
}
