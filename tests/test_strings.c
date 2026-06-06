/*
 * test_strings.c — TimuiStr helpers (T1.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_str_from_cstr){
    TimuiStr s = timui_str_from_cstr("hello");
    TIMUI_CHECK(s.len == 5);
    TIMUI_CHECK(timui_str_eq(s, TIMUI_STR_LIT("hello")));

    TimuiStr e = timui_str_from_cstr("");
    TIMUI_CHECK(e.len == 0 && timui_str_empty(e));

    TimuiStr n = timui_str_from_cstr(NULL);
    TIMUI_CHECK(n.len == 0);
}

TIMUI_TEST(test_str_copy_bounded){
    char buf[10];
    /* fits with room to spare */
    TIMUI_CHECK(timui_str_copy(buf, sizeof buf, TIMUI_STR_LIT("hello")) == 5);
    TIMUI_CHECK(strcmp(buf, "hello") == 0);

    /* truncation: reports the full needed length, writes cap-1 bytes + NUL */
    char small[3];
    TIMUI_CHECK(timui_str_copy(small, sizeof small, TIMUI_STR_LIT("hello")) == 5);
    TIMUI_CHECK(strcmp(small, "he") == 0);

    /* cap 0 / NULL dst: writes nothing, reports needed */
    TIMUI_CHECK(timui_str_copy(NULL, 0, TIMUI_STR_LIT("hello")) == 5);
}

TIMUI_TEST(test_str_slice){
    TimuiStr s = TIMUI_STR_LIT("hello world");          /* len 11 */
    TimuiStr w = timui_str_slice(s, 6, 5);
    TIMUI_CHECK(timui_str_eq(w, TIMUI_STR_LIT("world")));

    /* len beyond remaining clamps to remaining */
    TimuiStr tail = timui_str_slice(s, 6, 100);
    TIMUI_CHECK(timui_str_eq(tail, TIMUI_STR_LIT("world")));

    /* start past end -> empty */
    TimuiStr over = timui_str_slice(s, 100, 5);
    TIMUI_CHECK(over.len == 0);
}

TIMUI_TEST(test_str_eq_cstr){
    TIMUI_CHECK(timui_str_eq_cstr(TIMUI_STR_LIT("hi"), "hi"));
    TIMUI_CHECK(!timui_str_eq_cstr(TIMUI_STR_LIT("hi"), "ho"));
    TIMUI_CHECK(!timui_str_eq_cstr(TIMUI_STR_LIT("hi"), NULL));
}

TIMUI_TEST(test_str_invalid_utf8_passthrough){
    /* TimuiStr is byte-oriented: invalid UTF-8 must survive a copy unchanged. */
    static const unsigned char bytes[] = {0xff, 0xfe, 'a', 0x80};
    TimuiStr a = { (const char *)bytes, sizeof bytes };
    TIMUI_CHECK(a.len == 4);

    char buf[8];
    timui_str_copy(buf, sizeof buf, a);
    TimuiStr b = timui_str_from_cstr(buf);   /* strlen stops at the appended NUL */
    TIMUI_CHECK(b.len == 4);
    TIMUI_CHECK(timui_str_eq(a, b));
}
