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

/* Two styles are visually distinct if they differ in fg, bg, or attrs. */
static int style_distinct(TimuiStyle a, TimuiStyle b){
    return a.fg != b.fg || a.bg != b.bg || a.attrs != b.attrs;
}

/* G13: every builtin theme must give the interactive-STATE slots a style that
 * is visually distinct from their resting reference, so a widget's dim /
 * selected / focused / pressed state is never indistinguishable from its base.
 * Colour themes differ by fg/bg; MONO differs by attrs (reverse/bold/dim).
 * This is a STRUCTURAL invariant — it survives colour re-tuning and only fires
 * when a state slot silently collapses onto its base (the original G13 defect:
 * TEXT_DIM/BUTTON_ACTIVE/MENU_ACTIVE inheriting the plain fg-on-bg default in
 * the MODERN themes, and BUTTON_ACTIVE==BUTTON in DOS_GRAY). */
TIMUI_TEST(test_theme_slot_coverage){
    TimuiBuiltinTheme themes[5] = {
        TIMUI_THEME_DOS_BLUE, TIMUI_THEME_DOS_GRAY, TIMUI_THEME_MODERN_DARK,
        TIMUI_THEME_MONO, TIMUI_THEME_MODERN_LIGHT
    };
    int i;
    for(i = 0; i < 5; i++){
        TimuiTheme th     = timui_theme_builtin(themes[i]);
        TimuiStyle text   = th.slots[TIMUI_SLOT_TEXT];
        TimuiStyle button = th.slots[TIMUI_SLOT_BUTTON];
        TimuiStyle menu   = th.slots[TIMUI_SLOT_MENU];
        /* dim text must be distinguishable from normal text */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_TEXT_DIM], text));
        /* a selection must stand out from plain text */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_SELECTION], text));
        /* focus and press must each be visible from a resting button... */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_BUTTON_FOCUSED], button));
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_BUTTON_ACTIVE], button));
        /* ...a press must be visible even on an already-focused button... */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_BUTTON_ACTIVE],
                                   th.slots[TIMUI_SLOT_BUTTON_FOCUSED]));
        /* ...and a pressed button must never render as plain body text
         * (an unfocused mouse-press lands on BUTTON_ACTIVE alone) */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_BUTTON_ACTIVE], text));
        /* a highlighted menu item must stand out from the menu bar */
        TIMUI_CHECK(style_distinct(th.slots[TIMUI_SLOT_MENU_ACTIVE], menu));
    }
}

/* G13 negative: an out-of-range or NULL slot lookup must be bounds-safe and
 * return a deterministic zero style — never an OOB read into slots[]. */
TIMUI_TEST(test_theme_slot_out_of_range){
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    TimuiStyle a = timui_theme_style(&th, (TimuiStyleSlot)-1);
    TimuiStyle b = timui_theme_style(&th, TIMUI_SLOT_COUNT);
    TimuiStyle c = timui_theme_style(&th, (TimuiStyleSlot)(TIMUI_SLOT_COUNT + 100));
    TimuiStyle d = timui_theme_style(NULL, TIMUI_SLOT_TEXT);
    TIMUI_CHECK(a.fg == 0 && a.bg == 0 && a.attrs == 0);
    TIMUI_CHECK(b.fg == 0 && b.bg == 0 && b.attrs == 0);
    TIMUI_CHECK(c.fg == 0 && c.bg == 0 && c.attrs == 0);
    TIMUI_CHECK(d.fg == 0 && d.bg == 0 && d.attrs == 0);
}
