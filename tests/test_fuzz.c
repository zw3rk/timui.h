/*
 * test_fuzz.c — input-parser fuzz (T8.5): arbitrary bytes must not crash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

static void fuzz_sink(void *ctx, const TimuiEvent *ev){ (void)ev; (*(int *)ctx)++; }

/* A deterministic pseudo-random byte stream fed in one shot. */
TIMUI_TEST(test_fuzz_parser_random_stream){
    TimuiInputParser p;
    unsigned char buf[1024];
    unsigned int seed = 12345u;   /* fixed -> deterministic */
    int count = 0;
    size_t i;
    timui_input_init(&p);
    for(i = 0; i < sizeof buf; i++){
        seed = seed * 1103515245u + 12345u;     /* LCG */
        buf[i] = (unsigned char)(seed >> 16);
    }
    timui_input_feed(&p, buf, sizeof buf, fuzz_sink, &count);
    TIMUI_CHECK(count > 0);      /* L10: parser produced events (not just "no crash") */
}

/* Adversarial hand-crafted bytes: broken escapes, invalid UTF-8, nested ESC,
 * huge CSI params, empty SGR-mouse params. */
TIMUI_TEST(test_fuzz_parser_adversarial){
    TimuiInputParser p;
    static const unsigned char adv[] = {
        0x1b, '[', 0xff, 0xfe, 0x80, 0x1b, 'O', 0x1b, '[', '[', '[', 0x1b,
        0x1b, '[', '6', '5', ';', '9', '9', '9', 'u', 0x1b, '[', '<', ';', ';', 'M',
        0xc3, 0x28, 0xe0, 0x80, 0xf8, 0x88, 0x88, 0x00
    };
    int count = 0;
    timui_input_init(&p);
    timui_input_feed(&p, adv, sizeof adv, fuzz_sink, &count);
    TIMUI_CHECK(count > 0);      /* L10: produced events; the stream ends in NUL */
    TIMUI_CHECK(p.state == 0);   /* and resynced back to ground */
}
