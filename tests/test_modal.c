/*
 * test_modal.c — modal focus trapping (T4.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

/* A background button behind a displayed message box must NOT react. */
TIMUI_TEST(test_modal_traps_background){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    const TimuiStr ok[1] = { TIMUI_STR_LIT("OK") };
    int bg_clicked = 0;
    TimuiRect parent = TIMUI_RECT(0, 0, 40, 12);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    /* frame 1: show the modal (sets modal_active for subsequent frames) */
    timui_begin(ui, &f);
    if(timui_button(f, TIMUI_ID("bg"), TIMUI_RECT(0, 0, 8, 1), TIMUI_STR_LIT("BG")).clicked) bg_clicked = 1;
    timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("M"), TIMUI_STR_LIT("hi"), ok, 1);
    timui_end(f);

    /* frame 2: click the BG button at (0,0) -- behind the centered modal -> trapped */
    SETIN(&fake, "\x1b[<0;1;1M");
    timui_begin(ui, &f);
    if(timui_button(f, TIMUI_ID("bg"), TIMUI_RECT(0, 0, 8, 1), TIMUI_STR_LIT("BG")).clicked) bg_clicked = 1;
    timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("M"), TIMUI_STR_LIT("hi"), ok, 1);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;1;1m");
    timui_begin(ui, &f);
    if(timui_button(f, TIMUI_ID("bg"), TIMUI_RECT(0, 0, 8, 1), TIMUI_STR_LIT("BG")).clicked) bg_clicked = 1;
    timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("M"), TIMUI_STR_LIT("hi"), ok, 1);
    timui_end(f);

    TIMUI_CHECK(!bg_clicked);   /* the background button was trapped by the modal */
    timui_close(ui);
}
