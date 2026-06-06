/*
 * test_screen.c — screen-mode setup/teardown escape emission (T2.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_screen_enter_emits_modes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiScreenMode m;
    TimuiStr out;
    /* title + alt screen + mouse(SGR) + bracketed paste, in enter order */
    static const char expected[] =
        "\x1b]0;T\x07"             /* OSC 0 ; title BEL */
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
    /* teardown reverses enter order, each mode set -> 'l' */
    static const char expected[] =
        "\x1b[?2004l"              /* bracketed paste off */
        "\x1b[?1006l" "\x1b[?1000l"/* SGR + mouse off */
        "\x1b[?1049l";             /* alt screen off */

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
