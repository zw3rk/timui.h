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
    TIMUI_CHECK(timui_utf8_width(0x200D) == 0);    /* zero-width joiner */
    TIMUI_CHECK(timui_utf8_width(0xFE0F) == 0);    /* variation selector */
    TIMUI_CHECK(timui_utf8_width(0x1F3FD) == 0);   /* emoji skin-tone modifier */
    TIMUI_CHECK(timui_utf8_width(0xFFFD) == 1);    /* replacement */
    TIMUI_CHECK(timui_utf8_width(0x1b) == 0);      /* control */
}

/* V16: a 4-byte sequence decoding above U+10FFFF must yield U+FFFD. */
TIMUI_TEST(test_utf8_decode_above_max){
    uint32_t cp = 0;
    static const unsigned char above[] = { 0xF4, 0x90, 0x80, 0x80 };  /* U+110000 */
    TIMUI_CHECK(timui_utf8_decode((const char *)above, sizeof above, &cp) == 1 && cp == 0xFFFD);
}

TIMUI_TEST(test_grapheme_next_prev){
    static const char combining[] = "e\xCC\x81X";  /* e + U+0301, then X */
    static const char wave_skin[] = "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD!"; /* 👋🏽! */
    static const char flag_us[]   = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8x"; /* 🇺🇸x */
    static const char family[]    =
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
        "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";                 /* family ZWJ */
    static const char crlf[] = "\r\nx";
    static const char bad[] = "\xFFx";

    TIMUI_CHECK(timui_grapheme_next(combining, sizeof combining - 1, 0) == 3);
    TIMUI_CHECK(timui_grapheme_next(combining, sizeof combining - 1, 3) == 4);
    TIMUI_CHECK(timui_grapheme_prev(combining, sizeof combining - 1, 4) == 3);
    TIMUI_CHECK(timui_grapheme_prev(combining, sizeof combining - 1, 3) == 0);

    TIMUI_CHECK(timui_grapheme_next(wave_skin, sizeof wave_skin - 1, 0) == 8);
    TIMUI_CHECK(timui_grapheme_next(flag_us, sizeof flag_us - 1, 0) == 8);
    TIMUI_CHECK(timui_grapheme_next(family, sizeof family - 1, 0) == 25);
    TIMUI_CHECK(timui_grapheme_next(crlf, sizeof crlf - 1, 0) == 2);
    TIMUI_CHECK(timui_grapheme_next(bad, sizeof bad - 1, 0) == 1);
}

TIMUI_TEST(test_grapheme_width){
    static const char combining[] = "e\xCC\x81";                         /* e + U+0301 */
    static const char wave_skin[] = "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD";   /* 👋🏽 */
    static const char flag_us[]   = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";   /* 🇺🇸 */
    static const char ri_u[]      = "\xF0\x9F\x87\xBA";                   /* 🇺 regional indicator */
    static const char heart_vs16[] = "\xE2\x9D\xA4\xEF\xB8\x8F";          /* ❤️ */
    static const char family[]    =
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
        "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";                  /* family ZWJ */
    static const char crlf[] = "\r\n";

    TIMUI_CHECK(timui_grapheme_width(combining, sizeof combining - 1) == 1);
    TIMUI_CHECK(timui_grapheme_width(wave_skin, sizeof wave_skin - 1) == 2);
    TIMUI_CHECK(timui_grapheme_width(flag_us, sizeof flag_us - 1) == 2);
    TIMUI_CHECK(timui_grapheme_width(ri_u, sizeof ri_u - 1) == 2);
    TIMUI_CHECK(timui_grapheme_width(heart_vs16, sizeof heart_vs16 - 1) == 2);
    TIMUI_CHECK(timui_grapheme_width(family, sizeof family - 1) == 2);
    TIMUI_CHECK(timui_grapheme_width(crlf, sizeof crlf - 1) == 0);
}

TIMUI_TEST(test_utf8_decode_impossible_leads){
    uint32_t cp = 0;
    static const unsigned char bad_two[] = { 0xC0 };
    static const unsigned char bad_four[] = { 0xF5 };

    TIMUI_CHECK(timui_utf8_decode((const char *)bad_two, sizeof bad_two, &cp) == 1 && cp == 0xFFFD);
    TIMUI_CHECK(timui_utf8_decode((const char *)bad_four, sizeof bad_four, &cp) == 1 && cp == 0xFFFD);
}
