/*
 * test_dialog.c — message box (T5.8).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

/* A click well outside the centered box hits no button -> -1. */
TIMUI_TEST(test_message_box_miss){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiRect parent = TIMUI_RECT(0, 0, 40, 12);
    const TimuiStr btns[2] = { TIMUI_STR_LIT("OK"), TIMUI_STR_LIT("Cancel") };
    int r;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 12, &al);

    SETIN(&fake, "\x1b[<0;1;1M");              /* click at cell (0,0), outside the box */
    timui_begin(ui, &f); r = timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("T"), TIMUI_STR_LIT("Hi"), btns, 2); timui_end(f);
    SETIN(&fake, "\x1b[<0;1;1m");
    timui_begin(ui, &f); r = timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("T"), TIMUI_STR_LIT("Hi"), btns, 2); timui_end(f);
    TIMUI_CHECK(r == -1);

    timui_close(ui);
}

/* W3: a button label too long for the (clamped) box must still render button 0
 * (clamped to fit), so the modal stays dismissible instead of permanently
 * trapping input. */
TIMUI_TEST(test_message_box_button_clamped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiRect parent = TIMUI_RECT(0, 0, 14, 7);   /* narrow vs the long label */
    const TimuiStr btns[1] = { TIMUI_STR_LIT("VeryLongButtonLabel") };
    int r;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 14, 7, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    r = timui_message_box(f, TIMUI_ID("m"), parent, TIMUI_STR_LIT("T"), TIMUI_STR_LIT("Hi"), btns, 1);
    TIMUI_CHECK(timui_cells_get(buf, 4, 4)->codepoint == 'V');   /* button 0 rendered (label at r.x+1) */
    timui_end(f);
    TIMUI_CHECK(r == -1);
    timui_close(ui);
}
