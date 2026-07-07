/*
 * test_screen.c — screen-mode setup/teardown escape emission (T2.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

static int screen_contains(const char *haystack, size_t haystack_len, const char *needle){
    size_t needle_len = strlen(needle);
    size_t i;
    if(needle_len == 0) return 1;
    if(haystack_len < needle_len) return 0;
    for(i = 0; i <= haystack_len - needle_len; i++){
        if(memcmp(haystack + i, needle, needle_len) == 0) return 1;
    }
    return 0;
}

TIMUI_TEST(test_screen_enter_emits_modes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiScreenMode m;
    TimuiStr out;
    /* title + alt screen + mouse(SGR) + bracketed paste, in enter order */
    static const char expected[] =
        "\x1b]0;T\x07"             /* OSC 0 ; title BEL */
        "\x1b[?7l"                 /* auto-wrap off (cell renderer) */
        "\x1b[?1049h"              /* alt screen on  */
        "\x1b[?1000h" "\x1b[?1006h"/* mouse + SGR encoding */
        "\x1b[?2004h";             /* bracketed paste on */

    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    timui_screen_enter(&t, &m,
        TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_BRACKETED_PASTE,
        TIMUI_STR_LIT("T"));
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_screen_exit_reverses){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiScreenMode m;
    TimuiStr out;
    /* teardown reverses enter order, each mode set -> 'l'; cursor show is
     * unconditional because focused inputs may hide it during the session. */
    static const char expected[] =
        "\x1b[?2004l"              /* bracketed paste off */
        "\x1b[?1006l" "\x1b[?1000l"/* SGR + mouse off */
        "\x1b[?25h"                /* cursor shown */
        "\x1b[?1049l"              /* alt screen off */
        "\x1b[?7h";                /* auto-wrap restored */

    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    timui_screen_enter(&t, &m,
        TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_BRACKETED_PASTE,
        TIMUI_STR_LIT(""));     /* empty title -> no OSC emitted */
    timui_fake_clear_output(&f);
    timui_screen_exit(&t, &m);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_kitty_keyboard_mode_enter_exit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiScreenMode m;
    TimuiStr out;

    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    timui_screen_enter(&t, &m, TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_KITTY_KEYBOARD,
                       TIMUI_STR_LIT(""));
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(screen_contains(out.ptr, out.len, "\x1b[>1u"));

    timui_fake_clear_output(&f);
    timui_screen_exit(&t, &m);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(screen_contains(out.ptr, out.len, "\x1b[<u"));

    timui_fake_destroy(&f);
}

/* V9/W4: the title sanitizer filters at the codepoint level. Ü (U+00DC = C3 9C)
 * must SURVIVE — a byte-level reject of 0x80-0x9f would strip its 0x9C
 * continuation byte (the pass-1 V9 regression). U+009C (the C1 String
 * Terminator, UTF-8 C2 9C) and BEL must be dropped. */
TIMUI_TEST(test_title_rejects_controls){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiScreenMode m;
    TimuiStr out;
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);

    /* flags=0 so only the title OSC + the unconditional auto-wrap-off follow. */
    timui_screen_enter(&t, &m, 0, TIMUI_STR_LIT("\xC3\x9C"));   /* Ü */
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 12);
    TIMUI_CHECK(memcmp(out.ptr, "\x1b]0;\xC3\x9C\x07" "\x1b[?7l", 12) == 0);

    timui_fake_clear_output(&f);
    timui_screen_enter(&t, &m, 0, TIMUI_STR_LIT("a\xC2\x9C\x07" "b"));  /* a, U+009C, BEL, b */
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 12);
    TIMUI_CHECK(memcmp(out.ptr, "\x1b]0;ab\x07" "\x1b[?7l", 12) == 0);  /* U+009C + BEL dropped */

    timui_fake_destroy(&f);
}
