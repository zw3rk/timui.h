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
    /* Under a multiplexer, a kitty-family OUTER (TERM_PROGRAM inherited into the
     * session) keeps keyboard/sync (passthrough likely). But GRAPHICS is ALWAYS
     * stripped: it needs explicit tmux allow-passthrough + graphics support we
     * can't assume, and dropped APC graphics leave a grey placeholder + stray
     * cursor moves. Run outside the multiplexer for real images. */
    timui_caps_detect(&c, "tmux-256color", "kitty", "truecolor");
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_GRAPHICS));   /* stripped under tmux */
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_NONE);
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

TIMUI_TEST(test_caps_image_protocol_selection){
    TimuiCaps c = {0};
    TIMUI_CHECK(timui_caps_image_protocol(NULL) == TIMUI_IMAGE_PROTOCOL_NONE);
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_NONE);

    timui_caps_detect(&c, "xterm-ghostty", "ghostty", "truecolor");
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_KITTY);

    c.flags = TIMUI_CAP_ITERM2_IMAGES;
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_ITERM2);

    c.flags = TIMUI_CAP_SIXEL_GRAPHICS;
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_SIXEL);

    c.flags = TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES;
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_SIXEL);

    c.flags = TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES;
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_KITTY);
}

TIMUI_TEST(test_caps_iterm2_detects_image_protocol){
    TimuiCaps c;

    timui_caps_detect(&c, "xterm-256color", "iTerm.app", "truecolor");
    TIMUI_CHECK(timui_caps_has(&c, TIMUI_CAP_ITERM2_IMAGES));
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_ITERM2);

    timui_caps_detect(&c, "tmux-256color", "iTerm.app", "truecolor");
    TIMUI_CHECK(!timui_caps_has(&c, TIMUI_CAP_ITERM2_IMAGES));
    TIMUI_CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_NONE);
}

TIMUI_TEST(test_caps_report_explains_multiplexer_reductions){
    TimuiCapsReport r;

    timui_caps_detect_report(&r, "tmux-256color", "kitty", "truecolor", NULL);
    TIMUI_CHECK((r.notes & TIMUI_CAPS_NOTE_MULTIPLEXER) != 0);
    TIMUI_CHECK((r.notes & TIMUI_CAPS_NOTE_KITTY_PASSTHROUGH) != 0);
    TIMUI_CHECK((r.disabled_by_multiplexer & TIMUI_CAP_KITTY_GRAPHICS) != 0);
    TIMUI_CHECK((r.disabled_by_multiplexer & TIMUI_CAP_KITTY_KEYBOARD) == 0);
    TIMUI_CHECK(timui_caps_has(&r.caps, TIMUI_CAP_KITTY_KEYBOARD));
    TIMUI_CHECK(timui_caps_image_protocol(&r.caps) == TIMUI_IMAGE_PROTOCOL_NONE);

    timui_caps_detect_report(&r, "tmux-256color", "WezTerm", "truecolor", NULL);
    TIMUI_CHECK((r.notes & TIMUI_CAPS_NOTE_MULTIPLEXER) != 0);
    TIMUI_CHECK((r.disabled_by_multiplexer & TIMUI_CAP_SYNC_OUTPUT) != 0);
    TIMUI_CHECK(!timui_caps_has(&r.caps, TIMUI_CAP_SYNC_OUTPUT));
}

TIMUI_TEST(test_caps_report_notes_ssh_and_safe_fallback){
    TimuiCapsReport r;

    timui_caps_detect_report(&r, "xterm-256color", NULL, NULL, "host 22 client 55555");
    TIMUI_CHECK((r.notes & TIMUI_CAPS_NOTE_SSH_SESSION) != 0);
    TIMUI_CHECK((r.enabled_by_env & TIMUI_CAP_256_COLOR) != 0);
    TIMUI_CHECK(timui_caps_has(&r.caps, TIMUI_CAP_256_COLOR));

    timui_caps_detect_report(&r, "dumb", NULL, NULL, NULL);
    TIMUI_CHECK((r.notes & TIMUI_CAPS_NOTE_SAFE_FALLBACK) != 0);
    TIMUI_CHECK(r.enabled_by_env == 0);
    TIMUI_CHECK(r.caps.colors == 16);
}
