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
    /* a non-kitty modern outer under a multiplexer: SYNC is stripped, no Kitty
     * caps (WezTerm is modern but not kitty-family, so passthrough isn't assumed). */
    timui_caps_detect(&c, "tmux-256color", "WezTerm", "truecolor");
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_SYNC_OUTPUT));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_SGR_MOUSE));   /* mouse still passes through */
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_256_COLOR));
}

TIMUI_TEST(test_caps_multiplexer_kitty_passthrough){
    TimuiCaps c;
    /* W12: under a multiplexer, a kitty-family OUTER (TERM_PROGRAM inherited
     * into the session) implies passthrough is intended -> KEEP Kitty caps. */
    timui_caps_detect(&c, "tmux-256color", "kitty", "truecolor");
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_KITTY_GRAPHICS));
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_KITTY_KEYBOARD));
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
