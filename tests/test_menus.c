/*
 * test_menus.c — menu bar + popups (T5.7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

/* Z27: the caller owns the TimuiMenuBar and threads it through every call; its
 * `open` field persists across frames. */
static void menu_frame(Timui *ui, TimuiMenuBar *bar, int *item){
    TimuiFrame *f = NULL;
    timui_begin(ui, &f);
    timui_menu_bar_begin(f, bar, TIMUI_RECT(0, 0, 40, 1));
    if(timui_menu_begin(f, bar, TIMUI_ID("file"), TIMUI_STR_LIT("File"))){
        if(timui_menu_item(f, bar, TIMUI_ID("open"), TIMUI_STR_LIT("Open"))) *item = 1;
    }
    timui_menu_end(f);
    timui_menu_bar_end(f, bar);
    timui_end(f);
}

TIMUI_TEST(test_menu_open_and_select){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiMenuBar bar = {0};
    int item = 0;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    /* click the "File" header (cell 1,0): press then release opens it */
    SETIN(&fake, "\x1b[<0;2;1M"); menu_frame(ui, &bar, &item);
    SETIN(&fake, "\x1b[<0;2;1m"); menu_frame(ui, &bar, &item);
    TIMUI_CHECK(bar.open == TIMUI_ID("file"));   /* Z27: open state is caller-observable */

    /* click the "Open" item (cell 1,1): press then release selects it */
    SETIN(&fake, "\x1b[<0;2;2M"); menu_frame(ui, &bar, &item);
    SETIN(&fake, "\x1b[<0;2;2m"); menu_frame(ui, &bar, &item);

    TIMUI_CHECK(item);             /* the item reported a click */
    TIMUI_CHECK(bar.open == 0);    /* Z27: selecting an item closed the menu */
    timui_close(ui);
}

TIMUI_TEST(test_menu_outside_click_closes){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiMenuBar bar = {0};
    int item = 0;
    int opened_after_close = 0;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    /* open the menu */
    SETIN(&fake, "\x1b[<0;2;1M"); menu_frame(ui, &bar, &item);
    SETIN(&fake, "\x1b[<0;2;1m"); menu_frame(ui, &bar, &item);

    /* click far outside (cell 30,10) closes it */
    SETIN(&fake, "\x1b[<0;31;11M"); menu_frame(ui, &bar, &item);
    SETIN(&fake, "\x1b[<0;31;11m"); menu_frame(ui, &bar, &item);

    /* next frame: menu should be closed (menu_begin returns 0) */
    {
        TimuiFrame *f = NULL;
        timui_begin(ui, &f);
        timui_menu_bar_begin(f, &bar, TIMUI_RECT(0, 0, 40, 1));
        opened_after_close = timui_menu_begin(f, &bar, TIMUI_ID("file"), TIMUI_STR_LIT("File"));
        timui_menu_bar_end(f, &bar);
        timui_end(f);
    }
    TIMUI_CHECK(!opened_after_close);
    TIMUI_CHECK(bar.open == 0);    /* Z27: outside click cleared the observable state */
    timui_close(ui);
}
