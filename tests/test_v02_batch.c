/*
 * test_v02_batch.c — theme light, clipboard, keymaps (v0.2 batch).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

static int bytes_contain(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

/* ---- theme light (#51) ---- */
TIMUI_TEST(test_theme_modern_light){
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_MODERN_LIGHT);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].bg == 0xFFFFFF);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_TEXT].fg == 0x2E2E2E);
    TIMUI_CHECK(th.slots[TIMUI_SLOT_BUTTON_FOCUSED].bg == 0x0066CC);
}

/* ---- clipboard OSC 52 (#45) ---- */
TIMUI_TEST(test_clipboard_osc52){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    TimuiStr out;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_clipboard_set(&t, TIMUI_STR_LIT("hi"));
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]52;c;"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "aGk="));
    timui_fake_destroy(&fake);
}

/* V22: a payload so large its base64 length would overflow size_t must be
 * rejected up front, not allocated undersized and encoded into. */
TIMUI_TEST(test_clipboard_huge_len_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    TimuiStr out;
    TimuiStr huge = { "x", (SIZE_MAX / 4) + 5 };
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_clipboard_set(&t, huge);             /* guard fires; no crash, no emit */
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len == 0);
    timui_fake_destroy(&fake);
}

/* ---- keymaps (#52) ---- */
TIMUI_TEST(test_keymap_bind){
    TimuiKeymap km;
    km.count = 0;
    timui_keymap_bind(&km, TIMUI_KEY_F1, 0, 100);
    timui_keymap_bind(&km, TIMUI_KEY_ENTER, TIMUI_MOD_CTRL, 101);
    TIMUI_CHECK(km.count == 2);
    TIMUI_CHECK(km.bindings[0].action == 100);
    TIMUI_CHECK(km.bindings[0].key == TIMUI_KEY_F1);
    TIMUI_CHECK(km.bindings[1].key == TIMUI_KEY_ENTER);
    TIMUI_CHECK(km.bindings[1].mods == TIMUI_MOD_CTRL);
}

/* S6: bindings[] is a fixed [32] array; binding past the cap must stop, not
 * overrun. */
TIMUI_TEST(test_keymap_bind_overflow){
    TimuiKeymap km;
    int i;
    km.count = 0;
    for(i = 0; i < 40; i++) timui_keymap_bind(&km, TIMUI_KEY_F1, 0, i);
    TIMUI_CHECK(km.count == 32);   /* capped at the array size, no overrun */
}

/* Y4: an action bound to several keys must match on ANY of them, not
 * short-circuit on the first binding. */
TIMUI_TEST(test_keymap_hit_multi_binding){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiKeymap km;
    int hit;
    km.count = 0;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_keymap_bind(&km, TIMUI_KEY_F1, 0, 100);     /* action 100, 1st binding */
    timui_keymap_bind(&km, TIMUI_KEY_ENTER, 0, 100);  /* action 100, 2nd binding */
    timui_fake_set_input(&fake, "\r", 1);             /* press Enter (the 2nd binding) */
    timui_begin(ui, &f);
    hit = timui_keymap_hit(f, &km, 100);
    timui_end(f);
    TIMUI_CHECK(hit);   /* the 2nd binding is reachable (old code returned 0 on F1) */
    timui_close(ui);
}
