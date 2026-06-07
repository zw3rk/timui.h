/*
 * test_utf8.c — UTF-8 decode + display width (T3.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_utf8_decode){
    uint32_t cp = 0;
    static const unsigned char e_acute[] = { 0xC3, 0xA9 };
    static const unsigned char half[]    = { 0xC3 };
    static const unsigned char bad[]     = { 0xff };

    TIMUI_CHECK(timui_utf8_decode("a", 1, &cp) == 1 && cp == 'a');
    TIMUI_CHECK(timui_utf8_decode((const char *)e_acute, 2, &cp) == 2 && cp == 0xE9);
    TIMUI_CHECK(timui_utf8_decode((const char *)half, 1, &cp) == 0);        /* incomplete */
    TIMUI_CHECK(timui_utf8_decode((const char *)bad, 1, &cp) == 1 && cp == 0xFFFD);  /* invalid */
}

TIMUI_TEST(test_utf8_width){
    TIMUI_CHECK(timui_utf8_width('A') == 1);
    TIMUI_CHECK(timui_utf8_width(0x2500) == 1);    /* box drawing */
    TIMUI_CHECK(timui_utf8_width(0x4E2D) == 2);    /* CJK 'middle' */
    TIMUI_CHECK(timui_utf8_width(0x0300) == 0);    /* combining mark */
    TIMUI_CHECK(timui_utf8_width(0xFFFD) == 1);    /* replacement */
    TIMUI_CHECK(timui_utf8_width(0x1b) == 0);      /* control */
}
