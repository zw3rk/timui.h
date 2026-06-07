/*
 * test_sync.c — synchronized output + cursor hide/show (T2.9).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_sync_begin_end_bytes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_sync_begin(&t);
    timui_sync_end(&t);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 16);
    TIMUI_CHECK(memcmp(out.ptr, "\x1b[?2026h" "\x1b[?2026l", 16) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_cursor_hide_show_bytes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_hide_cursor(&t);
    timui_show_cursor(&t);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 12);
    TIMUI_CHECK(memcmp(out.ptr, "\x1b[?25l" "\x1b[?25h", 12) == 0);
    timui_fake_destroy(&f);
}
