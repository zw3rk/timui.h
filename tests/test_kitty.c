/*
 * test_kitty.c — Kitty keyboard protocol (CSI u + modifier decoding) (T2.8).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

typedef struct { TimuiEvent ev[16]; size_t n; } Sink;

static void kcb(void *ctx, const TimuiEvent *ev){
    Sink *s = (Sink *)ctx;
    if(s->n < 16) s->ev[s->n++] = *ev;
}

#define KFEED(p, lit) timui_input_feed((p), (lit), sizeof(lit) - 1, kcb, &s)

TIMUI_TEST(test_kitty_csi_u_plain){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    KFEED(&p, "\x1b[97u");              /* 'a' */
    TIMUI_CHECK(s.n == 1 && s.ev[0].kind == TIMUI_EVENT_KEY);
    TIMUI_CHECK(s.ev[0].as.key.codepoint == 97);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_NONE);
}

TIMUI_TEST(test_kitty_csi_u_with_mods){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    KFEED(&p, "\x1b[97;5u");            /* ctrl+a */
    TIMUI_CHECK(s.ev[0].as.key.codepoint == 97);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_CTRL);
    s.n = 0;
    KFEED(&p, "\x1b[97;2u");            /* shift+a */
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_SHIFT);
}

TIMUI_TEST(test_kitty_special_codes){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    KFEED(&p, "\x1b[27u");              /* Esc */
    TIMUI_CHECK(s.ev[0].as.key.key == TIMUI_KEY_ESCAPE);
    s.n = 0;
    KFEED(&p, "\x1b[13;2u");            /* shift+enter */
    TIMUI_CHECK(s.ev[0].as.key.key == TIMUI_KEY_ENTER);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_SHIFT);
}

TIMUI_TEST(test_kitty_mods_on_arrows){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    KFEED(&p, "\x1b[1;2A");             /* shift+up */
    TIMUI_CHECK(s.ev[0].as.key.key == TIMUI_KEY_UP);
    TIMUI_CHECK(s.ev[0].as.key.mods == TIMUI_MOD_SHIFT);
}
