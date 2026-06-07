/*
 * test_caps.c — capability detection (T2.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_caps_modern_kitty_family){
    TimuiCaps c;
    timui_caps_detect(&c, "xterm-ghostty", "ghostty", "truecolor");
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_TRUECOLOR));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_SGR_MOUSE));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_BRACKETED_PASTE));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_SYNC_OUTPUT));
    TIMUI_CHECK(c.colors == 16777216);
}

TIMUI_TEST(test_caps_multiplexer_reduces){
    TimuiCaps c;
    timui_caps_detect(&c, "tmux-256color", "ghostty", "truecolor");
    /* a multiplexer drops keyboard/sync/graphics even under a kitty-family TERM_PROGRAM */
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_SYNC_OUTPUT));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_SGR_MOUSE));   /* mouse still passes through */
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_256_COLOR));
}

TIMUI_TEST(test_caps_unknown_fallback){
    TimuiCaps c;
    timui_caps_detect(&c, "dumb", NULL, NULL);
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_TRUECOLOR));
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
    TIMUI_CHECK(c.colors == 16);
}

TIMUI_TEST(test_caps_force_masks){
    TimuiCaps c;
    timui_caps_detect(&c, "xterm-ghostty", "ghostty", "truecolor");
    timui_caps_apply_force(&c, 0, TIMUI_CAP_KITTY_KEYBOARD);   /* force-disable */
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
    timui_caps_apply_force(&c, TIMUI_CAP_KITTY_KEYBOARD, 0);   /* force-enable */
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
}
