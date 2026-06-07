/*
 * test_interact.c — focus/hot/active interaction core (T4.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_interact_click){
    TimuiInteract ia;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;

    timui_interact_init(&ia);
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
}

TIMUI_TEST(test_interact_hover_only){
    TimuiInteract ia;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;
    timui_interact_init(&ia);
    timui_interact_set_mouse(&ia, 5, 1, 0);   /* hovering, no press */
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.hovered && !res.pressed && !res.clicked);
    timui_interact_end(&ia);
}

TIMUI_TEST(test_interact_tab_cycles){
    TimuiInteract ia;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    timui_interact_init(&ia);
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
}

TIMUI_TEST(test_interact_keyboard_activate){
    TimuiInteract ia;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    TimuiInteractResult res;
    timui_interact_init(&ia);
    /* focus 'b' via tab from 'a' */
    ia.focus = TIMUI_ID("b");
    timui_interact_set_keys(&ia, 0, 1);        /* Enter/Space */
    timui_interact_begin(&ia);
    res = timui_interact_button(&ia, TIMUI_ID("b"), r);
    TIMUI_CHECK(res.focused && res.clicked);   /* keyboard activates focused */
    timui_interact_end(&ia);
}
