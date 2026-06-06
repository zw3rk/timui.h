/*
 * test_input.c — legacy/CSI input parser (T2.6).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

typedef struct { TimuiEvent ev[16]; size_t n; } Sink;

static void sink_cb(void *ctx, const TimuiEvent *ev){
    Sink *s = (Sink *)ctx;
    if(s->n < sizeof(s->ev) / sizeof(s->ev[0])) s->ev[s->n++] = *ev;
}

static TimuiKey parse_one_key(const char *bytes){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, bytes, strlen(bytes), sink_cb, &s);
    return s.n ? s.ev[0].as.key.key : TIMUI_KEY_UNKNOWN;
}

TIMUI_TEST(test_input_arrows){
    TIMUI_CHECK(parse_one_key("\x1b[A") == TIMUI_KEY_UP);
    TIMUI_CHECK(parse_one_key("\x1b[B") == TIMUI_KEY_DOWN);
    TIMUI_CHECK(parse_one_key("\x1b[C") == TIMUI_KEY_RIGHT);
    TIMUI_CHECK(parse_one_key("\x1b[D") == TIMUI_KEY_LEFT);
    TIMUI_CHECK(parse_one_key("\x1b[H") == TIMUI_KEY_HOME);
    TIMUI_CHECK(parse_one_key("\x1b[F") == TIMUI_KEY_END);
}

TIMUI_TEST(test_input_function_keys){
    TIMUI_CHECK(parse_one_key("\x1bOP") == TIMUI_KEY_F1);
    TIMUI_CHECK(parse_one_key("\x1bOQ") == TIMUI_KEY_F2);
    TIMUI_CHECK(parse_one_key("\x1bOR") == TIMUI_KEY_F3);
    TIMUI_CHECK(parse_one_key("\x1bOS") == TIMUI_KEY_F4);
    TIMUI_CHECK(parse_one_key("\x1b[15~") == TIMUI_KEY_F5);
    TIMUI_CHECK(parse_one_key("\x1b[21~") == TIMUI_KEY_F10);
    TIMUI_CHECK(parse_one_key("\x1b[24~") == TIMUI_KEY_F12);
}

TIMUI_TEST(test_input_tilde_edit_keys){
    TIMUI_CHECK(parse_one_key("\x1b[3~") == TIMUI_KEY_DELETE);
    TIMUI_CHECK(parse_one_key("\x1b[2~") == TIMUI_KEY_INSERT);
    TIMUI_CHECK(parse_one_key("\x1b[5~") == TIMUI_KEY_PAGE_UP);
    TIMUI_CHECK(parse_one_key("\x1b[6~") == TIMUI_KEY_PAGE_DOWN);
}

TIMUI_TEST(test_input_control_chars){
    TIMUI_CHECK(parse_one_key("\r") == TIMUI_KEY_ENTER);
    TIMUI_CHECK(parse_one_key("\n") == TIMUI_KEY_ENTER);
    TIMUI_CHECK(parse_one_key("\t") == TIMUI_KEY_TAB);
    TIMUI_CHECK(parse_one_key("\x7f") == TIMUI_KEY_BACKSPACE);
}

TIMUI_TEST(test_input_text_and_alt){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "a", 1, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 'a');

    s.n = 0;
    timui_input_feed(&p, "\x1b" "b", 2, sink_cb, &s);   /* ESC <printable> -> Alt+<c> */
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_KEY);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_ALT);
    TIMUI_CHECK(s.ev[0].as.key.codepoint == 'b');
}

TIMUI_TEST(test_input_partial_then_complete){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    TIMUI_CHECK(timui_input_feed(&p, "\x1b", 1, sink_cb, &s) == 0);  /* partial: nothing yet */
    TIMUI_CHECK(s.n == 0);
    TIMUI_CHECK(timui_input_feed(&p, "[A", 2, sink_cb, &s) == 1);    /* completes to Up */
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.key.key == TIMUI_KEY_UP);
}

TIMUI_TEST(test_input_utf8){
    TimuiInputParser p;
    Sink s;
    static const unsigned char e_acute[] = { 0xC3, 0xA9 };   /* U+00E9 */
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, e_acute, sizeof e_acute, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT);
    TIMUI_CHECK(s.ev[0].as.text.codepoint == 0xE9);
    TIMUI_CHECK(s.ev[0].as.text.len == 2);
}

TIMUI_TEST(test_input_invalid_safe){
    TimuiInputParser p;
    Sink s;
    /* stray continuation byte + unknown CSI final + invalid lead: no crash,
       each invalid byte becomes one U+FFFD replacement (2 total). */
    static const unsigned char junk[] = { 0x80, 0x1b, '[', 'Z', 0xff };
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, junk, sizeof junk, sink_cb, &s);
    TIMUI_CHECK(s.n == 2);
    TIMUI_CHECK(s.ev[0].as.text.codepoint == 0xFFFD);
}
