/*
 * test_theme.c — style/theme system (T4.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_theme_dos_blue){
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].bg == 0x0000AA);    /* blue background */
    TIMUI_CHECK(th.slots[TIMUI_SLOT_PANEL].bg == 0x0000AA);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].fg == 0xFFFFFF);    /* white text */
    TIMUI_CHECK(th.slots[TIMUI_SLOT_BUTTON].bg == 0xAAAAAA);  /* gray button */
}

TIMUI_TEST(test_theme_mono_default){
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_MONO);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].fg == 0xFFFFFF);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].bg == 0x000000);
}

TIMUI_TEST(test_theme_style_lookup){
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    TimuiStyle s = timui_theme_style(&th, TIMUI_SLOT_BORDER);
    TIMUI_CHECK(s.bg == th.slots[TIMUI_SLOT_BORDER].bg);
    TIMUI_CHECK(s.fg == th.slots[TIMUI_SLOT_BORDER].fg);
}
