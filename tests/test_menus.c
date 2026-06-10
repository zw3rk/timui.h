/*
 * test_menus.c — menu bar + popups (T5.7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

static void menu_frame(Timui *ui, int *item){
    TimuiFrame *f = NULL;
    timui_begin(ui, &f);
    timui_menu_bar_begin(f, TIMUI_RECT(0, 0, 40, 1));
    if(timui_menu_begin(f, TIMUI_ID("file"), TIMUI_STR_LIT("File"))){
        if(timui_menu_item(f, TIMUI_ID("open"), TIMUI_STR_LIT("Open"))) *item = 1;
    }
    timui_menu_end(f);
    timui_menu_bar_end(f);
    timui_end(f);
}

TIMUI_TEST(test_menu_open_and_select){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    int item = 0;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    /* click the "File" header (cell 1,0): press then release opens it */
    SETIN(&fake, "\x1b[<0;2;1M"); menu_frame(ui, &item);
    SETIN(&fake, "\x1b[<0;2;1m"); menu_frame(ui, &item);

    /* click the "Open" item (cell 1,1): press then release selects it */
    SETIN(&fake, "\x1b[<0;2;2M"); menu_frame(ui, &item);
    SETIN(&fake, "\x1b[<0;2;2m"); menu_frame(ui, &item);

    TIMUI_CHECK(item);             /* the item reported a click */
    timui_close(ui);
}

TIMUI_TEST(test_menu_outside_click_closes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    int item = 0;
    int opened_after_close = 0;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    /* open the menu */
    SETIN(&fake, "\x1b[<0;2;1M"); menu_frame(ui, &item);
    SETIN(&fake, "\x1b[<0;2;1m"); menu_frame(ui, &item);

    /* click far outside (cell 30,10) closes it */
    SETIN(&fake, "\x1b[<0;31;11M"); menu_frame(ui, &item);
    SETIN(&fake, "\x1b[<0;31;11m"); menu_frame(ui, &item);

    /* next frame: menu should be closed (menu_begin returns 0) */
    {
        TimuiFrame *f = NULL;
        timui_begin(ui, &f);
        timui_menu_bar_begin(f, TIMUI_RECT(0, 0, 40, 1));
        opened_after_close = timui_menu_begin(f, TIMUI_ID("file"), TIMUI_STR_LIT("File"));
        timui_menu_bar_end(f);
        timui_end(f);
    }
    TIMUI_CHECK(!opened_after_close);
    timui_close(ui);
}
