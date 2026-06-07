/*
 * test_widgets.c — panel/label, checkbox/radio, function bar (T5.1/T5.3/T5.6).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

TIMUI_TEST(test_checkbox_toggles){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    bool on = false;
    TimuiRect r = TIMUI_RECT(0, 0, 12, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);

    SETIN(&fake, "\x1b[<0;2;1M");              /* press at cell (1,0) */
    timui_begin(ui, &f);
    timui_checkbox_mut(f, TIMUI_ID("c"), r, TIMUI_STR_LIT("opt"), &on);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");              /* release -> toggle */
    timui_begin(ui, &f);
    timui_checkbox_mut(f, TIMUI_ID("c"), r, TIMUI_STR_LIT("opt"), &on);
    timui_end(f);
    TIMUI_CHECK(on);

    timui_close(ui);
}

TIMUI_TEST(test_radio_selects){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiBoolEdit be;
    TimuiRect r = TIMUI_RECT(0, 0, 12, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f);
    (void)timui_radio(f, TIMUI_ID("r"), r, TIMUI_STR_LIT("one"), false);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f);
    be = timui_radio(f, TIMUI_ID("r"), r, TIMUI_STR_LIT("one"), false);
    timui_end(f);
    TIMUI_CHECK(be.changed && be.value);       /* radio -> selected */

    timui_close(ui);
}

TIMUI_TEST(test_panel_body_rect){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiRect body;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 10, &al);
    timui_begin(ui, &f);
    body = timui_panel_begin(f, TIMUI_ID("p"), TIMUI_RECT(1, 1, 10, 5),
                             TIMUI_STR_LIT("Title"), TIMUI_BORDER_SINGLE);
    timui_panel_end(f);
    timui_end(f);
    TIMUI_CHECK(body.x == 2 && body.y == 2 && body.w == 8 && body.h == 3);
    timui_close(ui);
}
