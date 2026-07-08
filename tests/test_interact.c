/*
 * test_interact.c — focus/hot/active interaction core (T4.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>

TIMUI_TEST(test_interact_click){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;

    timui_interact_init(&ia, &al);
    /* press inside the widget */
    timui_interact_set_mouse(&ia, 5, 1, 1);
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.pressed && !res.clicked);
    TIMUI_CHECK(res.focused);                /* press claims focus */
    timui_interact_end(&ia);

    /* release inside -> click */
    timui_interact_set_mouse(&ia, 5, 1, 0);
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.clicked);
    timui_interact_end(&ia);
    timui_interact_destroy(&ia);
}

TIMUI_TEST(test_interact_hover_only){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;
    timui_interact_init(&ia, &al);
    timui_interact_set_mouse(&ia, 5, 1, 0);   /* hovering, no press */
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.hovered && !res.pressed && !res.clicked);
    timui_interact_end(&ia);
    timui_interact_destroy(&ia);
}

TIMUI_TEST(test_interact_release_outside_does_not_click){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;

    timui_interact_init(&ia, &al);
    timui_interact_set_mouse(&ia, 5, 1, 1);
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.pressed && !res.clicked);
    timui_interact_end(&ia);

    timui_interact_set_mouse(&ia, 30, 8, 0);
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(!res.hovered && !res.clicked);
    TIMUI_CHECK(ia.active == 0);
    timui_interact_end(&ia);
    timui_interact_destroy(&ia);
}

TIMUI_TEST(test_interact_extreme_rect_hit_test){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiInteractResult res;

    timui_interact_init(&ia, &al);
    timui_interact_set_mouse(&ia, INT_MAX - 1, 0, 0);
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("edge"), TIMUI_RECT(INT_MAX - 1, 0, 10, 1));
    TIMUI_CHECK(res.hovered);
    timui_interact_end(&ia);
    timui_interact_destroy(&ia);
}

TIMUI_TEST(test_interact_tab_cycles){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    timui_interact_init(&ia, &al);
    /* focus the first button by clicking it */
    timui_interact_set_mouse(&ia, 0, 0, 1);
    timui_interact_begin(&ia);
    timui_interact_button(&ia, TIMUI_ID("a"), r);
    timui_interact_end(&ia);
    timui_interact_set_mouse(&ia, 0, 0, 0);
    timui_interact_begin(&ia);
    timui_interact_button(&ia, TIMUI_ID("a"), r);
    timui_interact_end(&ia);

    /* Tab advances focus a -> b (takes effect at end(), visible next frame) */
    timui_interact_set_keys(&ia, 1, 0);
    timui_interact_begin(&ia);
    (void)timui_interact_button(&ia, TIMUI_ID("a"), r);
    (void)timui_interact_button(&ia, TIMUI_ID("b"), r);
    (void)timui_interact_button(&ia, TIMUI_ID("c"), r);
    timui_interact_end(&ia);
    TIMUI_CHECK(ia.focus == timui_id_from_cstr("b"));
    timui_interact_destroy(&ia);
}

TIMUI_TEST(test_interact_keyboard_activate){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;
    timui_interact_init(&ia, &al);
    /* focus 'b' via tab from 'a' */
    ia.focus = TIMUI_ID("b");
    timui_interact_set_keys(&ia, 0, 1);        /* Enter/Space */
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.focused && res.clicked);   /* keyboard activates focused */
    timui_interact_end(&ia);
    timui_interact_destroy(&ia);
}

/* V24: >64 focusable widgets must ALL be Tab-reachable (no fixed cap). */
TIMUI_TEST(test_interact_tab_many_widgets){
    TimuiInteract ia;
    TimuiAllocator al = timui_default_allocator();
    TimuiRect r = TIMUI_RECT(0, 0, 4, 1);
    int i;
    timui_interact_init(&ia, &al);
    ia.focus = TIMUI_ID("w00");
    timui_interact_set_keys(&ia, 1, 0);
    timui_interact_begin(&ia);
    for(i = 0; i < 100; i++){
        char name[8];
        name[0] = 'w'; name[1] = (char)('0' + (i / 10)); name[2] = (char)('0' + (i % 10)); name[3] = '\0';
        (void)timui_interact_button(&ia, timui_id_from_cstr(name), r);
    }
    timui_interact_end(&ia);
    /* 100 widgets registered (no 64-cap truncation); Tab from w00 -> w01. */
    TIMUI_CHECK(ia.tab_count == 100);
    TIMUI_CHECK(ia.focus == timui_id_from_cstr("w01"));
    timui_interact_destroy(&ia);
}
