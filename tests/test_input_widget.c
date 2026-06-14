/*
 * test_input_widget.c — single-line input (T5.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

TIMUI_TEST(test_input_types_and_submits){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[16] = {0};
    bool submitted;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);

    /* click to focus the input */
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);

    /* type "hi" */
    SETIN(&fake, "hi");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "hi") == 0);

    /* backspace -> "h" */
    SETIN(&fake, "\x7f");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "h") == 0);

    /* enter -> submit */
    SETIN(&fake, "\r");
    timui_begin(ui, &f);
    submitted = timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(submitted);

    timui_close(ui);
}

/* V7: a multibyte codepoint must not be split at the cap boundary, and
 * backspace must remove a whole codepoint. */
TIMUI_TEST(test_input_line_utf8_no_split){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[4] = {0};   /* fits "ab" + NUL, but not "ab" + é (2 more bytes) */
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");                 /* press to focus */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");                 /* release */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);

    SETIN(&fake, "ab\xC3\xA9");                   /* "ab" + é: é must not be split in */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "ab") == 0);          /* é skipped whole, not half-written */
    timui_close(ui);
}

TIMUI_TEST(test_input_line_utf8_backspace){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[8] = {0};
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);

    SETIN(&fake, "\xC3\xA9");                     /* é */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "\xC3\xA9") == 0);    /* valid UTF-8 é */
    SETIN(&fake, "\x7f");                          /* backspace -> whole é removed */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "") == 0);            /* no dangling lead byte */
    timui_close(ui);
}
