/*
 * test_review_critical.c — regression tests for CRITICAL review fixes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

typedef struct { TimuiEvent ev[16]; size_t n; } Sink;
static void sink(void *ctx, const TimuiEvent *ev){
    Sink *s = (Sink *)ctx;
    if(s->n < 16) s->ev[s->n++] = *ev;
}

/* C1: paste cross-feed — ESC[201~ split across two feeds must not inject raw ESC */
TIMUI_TEST(test_paste_cross_feed_terminator){
    TimuiInputParser p;
    Sink s;
    static const unsigned char f1[] = {0x1b,'[','2','0','0','~','A','B',0x1b,'[','2'};
    static const unsigned char f2[] = {'0','1','~'};
    s.n = 0;
    timui_input_init(&p);
    timui_input_feed(&p, f1, sizeof f1, sink, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].kind == TIMUI_EVENT_PASTE);
    TIMUI_CHECK(s.ev[0].as.paste.len == 2);
    timui_input_feed(&p, f2, sizeof f2, sink, &s);
    TIMUI_CHECK(s.n == 1);  /* no new events — terminator completed cleanly */
}

/* C3: esc_since_ms == 0 sentinel — Esc at now_ms=0 must still time out */
TIMUI_TEST(test_esc_timeout_zero_now){
    TimuiInputParser p;
    Sink s;
    s.n = 0;
    timui_input_init(&p);
    timui_input_set_now(&p, 0);
    timui_input_feed(&p, "\x1b", 1, sink, &s);
    TIMUI_CHECK(s.n == 0);
    timui_input_flush_esc(&p, 100, sink, &s);
    TIMUI_CHECK(s.n == 1);
    TIMUI_CHECK(s.ev[0].as.key.key == TIMUI_KEY_ESCAPE);
}

/* msgq overflow guard — near-SIZE_MAX size must be rejected */
TIMUI_TEST(test_msgq_overflow_guard){
    TimuiAllocator al = timui_default_allocator();
    TimuiMsgQueue q;
    timui_msgq_init(&q, &al, 4096);
    TIMUI_CHECK(timui_msgq_emit(&q, 1, "x", 1) == 1);
    TIMUI_CHECK(timui_msgq_emit(&q, 2, "x", (size_t)-1) == 0);
    timui_msgq_destroy(&q);
}
