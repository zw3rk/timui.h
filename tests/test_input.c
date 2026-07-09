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

TIMUI_TEST(test_input_timeout_before_new_bytes){
    TimuiInputParser p;
    Sink s;

    s.n = 0;
    timui_input_init(&p);
    timui_input_set_now(&p, 0);
    timui_input_feed(&p, "\x1b", 1, sink_cb, &s);
    TIMUI_CHECK(s.n == 0);

    timui_input_set_now(&p, 100);
    timui_input_feed(&p, "a", 1, sink_cb, &s);
    TIMUI_CHECK(s.n == 2);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_KEY && s.ev[0].as.key.key == TIMUI_KEY_ESCAPE);
    TIMUI_CHECK(s.ev[1].kind == TIMUI_EVENT_TEXT && s.ev[1].as.text.codepoint == 'a');

    s.n = 0;
    timui_input_init(&p);
    timui_input_set_now(&p, 0);
    timui_input_feed(&p, "\x1b[", 2, sink_cb, &s);
    timui_input_set_now(&p, 100);
    timui_input_feed(&p, "A", 1, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 'A');
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

/* V2: a byte at a feed boundary must not be lost when ESC/UTF-8 resyncs. */
TIMUI_TEST(test_input_esc_resync_no_loss){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1b", 1, sink_cb, &s);          /* ESC at end of feed */
    timui_input_feed(&p, "\xc3\xa9", 2, sink_cb, &s);       /* invalid ESC-2nd-byte lead */
    /* ESC key, then the bytes reprocessed in ground as é (U+00E9) — not dropped. */
    TIMUI_CHECK(s.n == 2);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_KEY && s.ev[0].as.key.key == TIMUI_KEY_ESCAPE);
    TIMUI_CHECK(s.ev[1].kind == TIMUI_EVENT_TEXT && s.ev[1].as.text.codepoint == 0xE9);
}

TIMUI_TEST(test_input_utf8_resync_no_loss){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\xc3", 1, sink_cb, &s);           /* 2-byte lead, partial */
    TIMUI_CHECK(s.n == 0);
    timui_input_feed(&p, "A", 1, sink_cb, &s);              /* invalid continuation */
    /* one U+FFFD for the abandoned \xc3, then 'A' reprocessed in ground (not
     * mis-emitted as U+FFFD, and not dropped). */
    TIMUI_CHECK(s.n == 2);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 0xFFFD);
    TIMUI_CHECK(s.ev[1].kind == TIMUI_EVENT_TEXT && s.ev[1].as.text.codepoint == 'A');
}

TIMUI_TEST(test_input_csi_ss3_non_ascii_resync_no_loss){
    TimuiInputParser p;
    Sink s;

    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1b[", 2, sink_cb, &s);
    timui_input_feed(&p, "\xC3\xA9", 2, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 0xE9);

    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1bO", 2, sink_cb, &s);
    timui_input_feed(&p, "\xC3\xA9", 2, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 0xE9);
}

/* V5: modifier-tagged mouse wheel must keep its direction. Shift+wheel-up is
 * SGR code 0x40|0x04 = 68; old exact-match (==64) zeroed the delta. */
TIMUI_TEST(test_mouse_wheel_with_mods){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1b[<68;10;5M", 11, sink_cb, &s);  /* Shift + wheel-up */
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_MOUSE);
    TIMUI_CHECK(s.ev[0].as.mouse.wheel_y == 1);
    TIMUI_CHECK(s.ev[0].as.mouse.mods == TIMUI_MOD_SHIFT);
}

/* V14: NUL must be ignored (no phantom Ctrl-@ key event with codepoint 0). */
TIMUI_TEST(test_input_nul_ignored){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x00", 1, sink_cb, &s);
    TIMUI_CHECK(s.n == 0);
}

/* paste sink: concatenates PASTE payloads so we can assert no terminator leak. */
typedef struct { char buf[64]; size_t len; int events; } PasteSink;
static void paste_cb(void *ctx, const TimuiEvent *ev){
    PasteSink *s = (PasteSink *)ctx;
    if(ev->kind != TIMUI_EVENT_PASTE) return;
    s->events++;
    if(ev->as.paste.len && s->len + ev->as.paste.len < sizeof s->buf){
        memcpy(s->buf + s->len, ev->as.paste.ptr, ev->as.paste.len);
        s->len += ev->as.paste.len;
    }
}

/* V12: a paste terminator split across 3+ feeds must not inject the literal
 * terminator bytes ("201...") into the paste content. */
TIMUI_TEST(test_paste_cross_feed_three_fragments){
    TimuiInputParser p;
    PasteSink s; s.len = 0; s.events = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1b[200~AB", 8, paste_cb, &s);   /* start + payload "AB" */
    timui_input_feed(&p, "\x1b[", 2, paste_cb, &s);          /* partial terminator (2/6) */
    timui_input_feed(&p, "2", 1, paste_cb, &s);              /* still a prefix (3/6) */
    timui_input_feed(&p, "01~", 3, paste_cb, &s);            /* completes terminator */
    TIMUI_CHECK(s.len == 2 && memcmp(s.buf, "AB", 2) == 0);  /* no terminator leakage */
}

/* V13: back-to-back paste START/END must not emit an empty paste event. */
TIMUI_TEST(test_paste_empty_no_event){
    TimuiInputParser p;
    PasteSink s; s.len = 0; s.events = 0;
    timui_input_init(&p);
    timui_input_feed(&p, "\x1b[200~\x1b[201~", 12, paste_cb, &s);
    TIMUI_CHECK(s.events == 0);                               /* no empty paste */
}

/* Z2: the input UTF-8 decoder must apply the same overlong / surrogate /
 * above-max rejection as the render decoder (timui_utf8_decode), substituting
 * U+FFFD. Otherwise `C0 80` decodes to codepoint 0 and injects a real NUL byte
 * into the app's text buffer — bypassing the V14 ground-state NUL guard. */
TIMUI_TEST(test_input_utf8_overlong_rejected){
    TimuiInputParser p;
    Sink s;
    static const unsigned char overlong_nul[] = { 0xC0, 0x80 };            /* overlong U+0000 */
    static const unsigned char overlong_slash[] = { 0xE0, 0x80, 0xAF };    /* overlong U+002F */
    static const unsigned char surrogate[]     = { 0xED, 0xA0, 0x80 };     /* U+D800 (half)   */
    static const unsigned char above_max[]     = { 0xF4, 0x90, 0x80, 0x80 };/* U+110000        */
    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, overlong_nul, sizeof overlong_nul, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_TEXT);
    TIMUI_CHECK(s.ev[0].as.text.codepoint == 0xFFFD);   /* must NOT be 0 */

    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, overlong_slash, sizeof overlong_slash, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.text.codepoint == 0xFFFD);   /* not '/' */

    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, surrogate, sizeof surrogate, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.text.codepoint == 0xFFFD);

    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, above_max, sizeof above_max, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.text.codepoint == 0xFFFD);

    /* a legitimate 2-byte rune (U+00E9) must still pass unchanged */
    { static const unsigned char e_acute[] = { 0xC3, 0xA9 };
      s.n = 0; timui_input_init(&p);
      timui_input_feed(&p, e_acute, sizeof e_acute, sink_cb, &s);
      TIMUI_CHECK(s.n == 1 && s.ev[0].as.text.codepoint == 0xE9); }
}

TIMUI_TEST(test_input_utf8_invalid_lead_immediate){
    TimuiInputParser p;
    Sink s;
    static const unsigned char bad_two_byte[] = { 0xC0 };
    static const unsigned char bad_four_byte[] = { 0xF5 };

    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, bad_two_byte, sizeof bad_two_byte, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 0xFFFD);

    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, bad_four_byte, sizeof bad_four_byte, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 0xFFFD);
}

/* Z3: an ESC arriving mid-sequence must abort the pending CSI/SS3 and begin a
 * fresh escape (ECMA-48), not resync to ground and leak the interrupted
 * sequence's tail as injected text. */
TIMUI_TEST(test_input_esc_mid_csi_restarts){
    TimuiInputParser p;
    Sink s;
    /* CSI case: "ESC[3" (truncated Delete) then "ESC[A" (Up). The Up must
     * survive; no literal "[A" text may be injected. */
    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1b[3\x1b[A", 6, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_KEY && s.ev[0].as.key.key == TIMUI_KEY_UP);

    /* SS3 case: "ESC O" (truncated) then "ESC[A" (Up) — same guarantee. */
    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1bO\x1b[A", 5, sink_cb, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_KEY && s.ev[0].as.key.key == TIMUI_KEY_UP);
}

TIMUI_TEST(test_input_truncated_csi_ss3_timeout_resyncs){
    TimuiInputParser p;
    Sink s;

    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1b[", 2, sink_cb, &s);
    timui_input_flush_esc(&p, 100, sink_cb, &s);
    timui_input_feed(&p, "123\r", 4, sink_cb, &s);
    TIMUI_CHECK(s.n == 4);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == '1');
    TIMUI_CHECK(s.ev[1].kind == TIMUI_EVENT_TEXT && s.ev[1].as.text.codepoint == '2');
    TIMUI_CHECK(s.ev[2].kind == TIMUI_EVENT_TEXT && s.ev[2].as.text.codepoint == '3');
    TIMUI_CHECK(s.ev[3].kind == TIMUI_EVENT_KEY && s.ev[3].as.key.key == TIMUI_KEY_ENTER);

    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1bO", 2, sink_cb, &s);
    timui_input_flush_esc(&p, 100, sink_cb, &s);
    timui_input_feed(&p, "abc", 3, sink_cb, &s);
    TIMUI_CHECK(s.n == 3);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_TEXT && s.ev[0].as.text.codepoint == 'a');
    TIMUI_CHECK(s.ev[1].kind == TIMUI_EVENT_TEXT && s.ev[1].as.text.codepoint == 'b');
    TIMUI_CHECK(s.ev[2].kind == TIMUI_EVENT_TEXT && s.ev[2].as.text.codepoint == 'c');
}

/* Z4: a CSI ':' sub-parameter (Kitty "report event types" / "report alternate
 * keys") is a legal ECMA-48 parameter-substring separator. It must not resync
 * the parser to ground: the base key survives and no sub-param tail leaks as
 * text. */
TIMUI_TEST(test_input_csi_subparam_ignored){
    TimuiInputParser p;
    Sink s;
    /* event-type form: "ESC[97;1:3u" — Kitty 'a' (code 97), mods from param 1
     * (none), event-type 3 in a sub-param that must be discarded. */
    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1b[97;1:3u", 9, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_KEY);
    TIMUI_CHECK(s.ev[0].as.key.codepoint == 97);

    /* alternate-key form: "ESC[97:65;2u" — base 'a' with a shifted-key sub-param
     * before the modifier; base survives, Shift (param 2) decoded. */
    s.n = 0; timui_input_init(&p);
    timui_input_feed(&p, "\x1b[97:65;2u", 10, sink_cb, &s);
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_KEY);
    TIMUI_CHECK(s.ev[0].as.key.codepoint == 97);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_SHIFT);
}
