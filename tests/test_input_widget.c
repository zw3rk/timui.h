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

/* F1.5: input_field — in-line cursor editing (single line). */
TIMUI_TEST(test_input_field_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);
    bool submitted = false;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define IF_FRAME() do{ timui_begin(ui,&f); submitted = timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); IF_FRAME();       /* click to focus */
    SETIN(&fake, "\x1b[<0;2;1m"); IF_FRAME();
    SETIN(&fake, "abc"); IF_FRAME();
    TIMUI_CHECK(strcmp(text, "abc") == 0 && is.cursor == 3);
    SETIN(&fake, "\x1b[D"); IF_FRAME();             /* LEFT */
    TIMUI_CHECK(is.cursor == 2);
    SETIN(&fake, "Z"); IF_FRAME();                  /* insert mid-string */
    TIMUI_CHECK(strcmp(text, "abZc") == 0 && is.cursor == 3);
    SETIN(&fake, "\x1b[H"); IF_FRAME();             /* HOME */
    TIMUI_CHECK(is.cursor == 0);
    SETIN(&fake, "\x1b[3~"); IF_FRAME();            /* DELETE 'a' -> "bZc" */
    TIMUI_CHECK(strcmp(text, "bZc") == 0 && is.cursor == 0);
    SETIN(&fake, "\x1b[F"); IF_FRAME();             /* END */
    TIMUI_CHECK(is.cursor == 3);
    SETIN(&fake, "\x7f"); IF_FRAME();               /* backspace -> "bZ" */
    TIMUI_CHECK(strcmp(text, "bZ") == 0 && is.cursor == 2);
    SETIN(&fake, "\r"); IF_FRAME();                 /* Enter submits */
    TIMUI_CHECK(submitted);
#undef IF_FRAME
    timui_close(ui);
}

/* F1.5: horizontal scroll keeps the cursor visible; Home scrolls back. */
TIMUI_TEST(test_input_field_scroll){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 5, 1);           /* width 5 */
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define IF_FRAME() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); IF_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); IF_FRAME();
    SETIN(&fake, "abcdefg"); IF_FRAME();            /* 7 cols into a width-5 field */
    TIMUI_CHECK(is.cursor == 7);
    TIMUI_CHECK(is.scroll_x == 7 - 5 + 1);          /* cursor pinned at the right edge */
    SETIN(&fake, "\x1b[H"); IF_FRAME();             /* HOME -> scroll back to 0 */
    TIMUI_CHECK(is.cursor == 0 && is.scroll_x == 0);
#undef IF_FRAME
    timui_close(ui);
}

/* F1.5: NULL / degenerate guards. */
TIMUI_TEST(test_input_field_guards){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[4] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiInputState nul = { NULL, 4, 0, 0 };
    TIMUI_CHECK(!timui_input_field(NULL, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), &is));  /* NULL frame */
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 3, &al);
    timui_begin(ui, &f);
    TIMUI_CHECK(!timui_input_field(f, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), NULL));    /* NULL state */
    TIMUI_CHECK(!timui_input_field(f, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), &nul));    /* NULL text */
    timui_end(f);
    timui_close(ui);
}

/* F1.4: a focused input requests the hardware cursor at its edit cell; a frame
 * with no focused input hides it (once). */
static int out_contains(const TimuiFakeTransport *fake, const char *needle){
    TimuiStr o = timui_fake_output(fake);
    size_t nl = strlen(needle), i;
    if(o.len < nl) return 0;
    for(i = 0; i + nl <= o.len; i++)
        if(memcmp(o.ptr + i, needle, nl) == 0) return 1;
    return 0;
}
TIMUI_TEST(test_focused_input_cursor){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    SETIN(&fake, "ab");
    timui_fake_clear_output(&fake);
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    TIMUI_CHECK(is.cursor == 2);
    TIMUI_CHECK(out_contains(&fake, "\x1b[?25h"));       /* cursor shown */
    TIMUI_CHECK(out_contains(&fake, "\x1b[1;3H"));       /* CUP to screen (2,0), 1-based */
    /* a frame that renders no input -> the request is absent -> hide emitted */
    timui_fake_clear_output(&fake);
    timui_begin(ui,&f); timui_end(f);
    TIMUI_CHECK(out_contains(&fake, "\x1b[?25l"));       /* cursor hidden on focus loss */
    timui_close(ui);
}
