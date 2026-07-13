/*
 * test_frame.c — frame lifecycle integration (T4.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

static int frame_contains(const char *haystack, size_t haystack_len, const char *needle){
    size_t needle_len = strlen(needle);
    size_t i;
    if(needle_len == 0) return 1;
    if(!haystack || needle_len > haystack_len) return 0;
    for(i = 0; i + needle_len <= haystack_len; i++)
        if(memcmp(haystack + i, needle, needle_len) == 0) return 1;
    return 0;
}

typedef struct { int read_result; } BeginResultTransport;

static int begin_result_write(TimuiTransport *t, const void *data, size_t len){
    (void)t; (void)data;
    return (int)len;
}
static int begin_result_read(TimuiTransport *t, void *buf, size_t cap){
    BeginResultTransport *br = (BeginResultTransport *)t->ctx;
    (void)buf; (void)cap;
    return br ? br->read_result : -1;
}
static int begin_result_flush(TimuiTransport *t){ (void)t; return 0; }
static void begin_result_close(TimuiTransport *t){ (void)t; }
static TimuiTransport begin_result_transport(BeginResultTransport *br){
    TimuiTransport t;
    t.write = begin_result_write;
    t.read = begin_result_read;
    t.flush = begin_result_flush;
    t.close = begin_result_close;
    t.ctx = br;
    return t;
}

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

TIMUI_TEST(test_begin_result_statuses){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = (TimuiFrame *)1;

    TIMUI_CHECK(timui_begin_result(NULL, &f) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(f == NULL);
    TIMUI_CHECK(timui_begin_result(NULL, NULL) == TIMUI_ERR_INVALID_ARGUMENT);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_begin_result(ui, &f) == TIMUI_OK);
    TIMUI_CHECK(f != NULL);
    timui_end(f);

    timui_quit(ui);
    f = (TimuiFrame *)1;
    TIMUI_CHECK(timui_begin_result(ui, &f) == TIMUI_ERR_CLOSED);
    TIMUI_CHECK(f == NULL);
    TIMUI_CHECK(!timui_begin(ui, &f));
    TIMUI_CHECK(f == NULL);

    timui_close(ui);
}

TIMUI_TEST(test_begin_result_transport_error){
    TimuiAllocator al = timui_default_allocator();
    BeginResultTransport br;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = (TimuiFrame *)1;

    br.read_result = -1;
    t = begin_result_transport(&br);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_begin_result(ui, &f) == TIMUI_ERR_IO);
    TIMUI_CHECK(f == NULL);
    TIMUI_CHECK(!timui_begin(ui, &f));
    TIMUI_CHECK(f == NULL);
    timui_close(ui);

    br.read_result = -2;
    t = begin_result_transport(&br);
    f = (TimuiFrame *)1;
    ui = NULL;
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_begin_result(ui, &f) == TIMUI_ERR_EOF);
    TIMUI_CHECK(f == NULL);
    timui_close(ui);
}

TIMUI_TEST(test_invalidate_resets_terminal_state_cache){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiStr out;
    TimuiStyle green = timui_style_make(0x59ee3f, TIMUI_COLOR_DEFAULT, 0);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);

    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("A"), green);
    timui_end(f);
    timui_fake_clear_output(&fake);

    timui_invalidate(ui);
    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("AB"), green);
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(frame_contains(out.ptr, out.len, "\x1b[0m"));   /* SGR cache reset */
    TIMUI_CHECK(frame_contains(out.ptr, out.len, "B"));
    TIMUI_CHECK(!frame_contains(out.ptr, out.len, "AB"));       /* not a full redraw */

    timui_close(ui);
}

TIMUI_TEST(test_full_redraw_repaints_unchanged_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiStr out;
    TimuiStyle st = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 10, 5, &al) == TIMUI_OK);

    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"), st);
    timui_end(f);

    timui_fake_clear_output(&fake);
    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"), st);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len == 0);

    timui_full_redraw(ui);
    TIMUI_CHECK(timui_begin(ui, &f));
    timui_draw_text(timui_frame_buffer(f), 0, 0, TIMUI_STR_LIT("Hi"), st);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(frame_contains(out.ptr, out.len, "Hi"));

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
