/*
 * test_listbox.c — scrollable list (T5.5).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

static const char *LB_ITEMS[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
static const char *lb_label(void *ud, int i){ (void)ud; return LB_ITEMS[i]; }

TIMUI_TEST(test_listbox_down_key){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiListState st = {0, 0};
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 8, &al);

    /* focus by click */
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f); timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    TIMUI_CHECK(st.selected == 0);

    SETIN(&fake, "\x1b[B");                       /* Down -> 1 */
    timui_begin(ui, &f); timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    TIMUI_CHECK(st.selected == 1);

    SETIN(&fake, "\x1b[B");                       /* Down -> 2 */
    timui_begin(ui, &f); timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    TIMUI_CHECK(st.selected == 2);

    timui_close(ui);
}

TIMUI_TEST(test_listbox_click_selects){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiListState st = {0, 0};
    TimuiListResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 8, &al);

    /* click on row 1 (cell y=1) -> index 1, activated */
    SETIN(&fake, "\x1b[<0;1;2M");
    timui_begin(ui, &f); timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    SETIN(&fake, "\x1b[<0;1;2m");
    timui_begin(ui, &f); res = timui_listbox_mut(f, TIMUI_ID("L"), r, &st, 5, lb_label, 0); timui_end(f);
    TIMUI_CHECK(st.selected == 1);
    TIMUI_CHECK(res.activated);

    timui_close(ui);
}

/* Y3: an out-of-range selected (e.g. left stale after a shrink) must self-heal
 * into [0,count-1], like tree/table do. */
TIMUI_TEST(test_listbox_selected_clamped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiListState st = { 7, 0 };                 /* selected past a 3-item list */
    TimuiListResult res;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 8, &al);
    timui_begin(ui, &f);
    res = timui_listbox_mut(f, TIMUI_ID("L"), TIMUI_RECT(0, 0, 10, 3), &st, 3, lb_label, 0);
    timui_end(f);
    TIMUI_CHECK(res.selected == 2);               /* clamped to count-1 */
    timui_close(ui);
}

static const char *lb_long_label(void *ud, int i){
    (void)ud; (void)i;
    return "abcdefgh";
}

TIMUI_TEST(test_listbox_label_clipped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *cells;
    TimuiListState st = {0, 0};

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 3, &al);

    timui_begin(ui, &f);
    cells = timui_frame_buffer(f);
    (void)timui_listbox_mut(f, TIMUI_ID("L"), TIMUI_RECT(1, 1, 4, 1),
                            &st, 1, lb_long_label, 0);
    TIMUI_CHECK(timui_cells_get(cells, 5, 1)->codepoint == 0);
    timui_end(f);

    timui_close(ui);
}
