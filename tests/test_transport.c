/*
 * test_transport.c — transport abstraction + fake backend (T2.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_fake_capture_output){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    TIMUI_CHECK(t.write(&t, "abc", 3) == 3);
    TIMUI_CHECK(t.write(&t, "de", 2) == 2);
    TIMUI_CHECK(t.flush(&t) == 0);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 5 && memcmp(out.ptr, "abcde", 5) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_fake_inject_input){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    char buf[8];
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    timui_fake_set_input(&f, "\x1b[A", 3);          /* Up-arrow escape */
    TIMUI_CHECK(t.read(&t, buf, sizeof buf) == 3);
    TIMUI_CHECK(memcmp(buf, "\x1b[A", 3) == 0);
    TIMUI_CHECK(t.read(&t, buf, sizeof buf) == 0);  /* drained */
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_fake_grows){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    char big[256];
    int i;
    for(i = 0; i < 256; i++) big[i] = (char)('A' + (i % 26));
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    TIMUI_CHECK(t.write(&t, big, sizeof big) == 256);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 256 && memcmp(out.ptr, big, 256) == 0);
    timui_fake_destroy(&f);
}
