/*
 * test_esc_timeout.c — lone-Esc resolution (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

typedef struct { TimuiEvent ev[8]; size_t n; } Sink;
static void sink(void *ctx, const TimuiEvent *ev){
    Sink *s = (Sink *)ctx;
    if(s->n < 8) s->ev[s->n++] = *ev;
}

TIMUI_TEST(test_esc_timeout_flushes){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_set_now(&p, 1000);
    timui_input_feed(&p, "\x1b", 1, sink, &s);   /* enters ESC state at t=1000 */
    TIMUI_CHECK(s.n == 0);
    timui_input_flush_esc(&p, 1010, sink, &s);    /* 10ms < 50ms -> still pending */
    TIMUI_CHECK(s.n == 0);
    timui_input_flush_esc(&p, 1100, sink, &s);    /* 100ms >= 50ms -> emit Esc */
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.key.key == TIMUI_KEY_ESCAPE);
}

TIMUI_TEST(test_esc_arrow_not_delayed){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_set_now(&p, 5000);
    timui_input_feed(&p, "\x1b[A", 3, sink, &s);  /* Up arrow, completes immediately */
    TIMUI_CHECK(s.n == 1 && s.ev[0].as.key.key == TIMUI_KEY_UP);
}
