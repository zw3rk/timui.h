/*
 * test_cursor.c — cursor placement after a frame (T3.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>
#include <string.h>

TIMUI_TEST(test_render_cursor_visible){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    static const char expected[] = "\x1b[4;6H" "\x1b[?25h";   /* CUP(y+1;x+1) + show */
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_render_cursor(&t, 5, 3, 1);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_render_cursor_hidden){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    static const char expected[] = "\x1b[?25l";               /* hide only */
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_render_cursor(&t, 0, 0, 0);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_render_cursor_extreme_coords_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    static const char expected[] = "\x1b[?25h";   /* show only; skip impossible CUP */
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);

    timui_render_cursor(&t, INT_MAX, 0, 1);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);

    timui_fake_clear_output(&f);
    timui_render_cursor(&t, 0, INT_MAX, 1);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_fake_destroy(&f);
}
