/*
 * test_grid_widget.c — frame-driven tests for the enhanced data widgets:
 * timui_table_ex (virtual multi-column grid: sticky header, content-fit widths,
 * horizontal + vertical scroll, selection) and timui_tree_scroll (large-tree
 * flatten + viewport windowing). These exercise the ACTUAL render + interaction
 * path (a real Timui over a fake transport), complementing the pure-math units
 * in tests/test_grid.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include "test.h"
#include "timui.h"
#include <stdio.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

/* cell provider: "r<row>c<col>" (first char always 'r'). */
static const char *gx_cell(void *ud, int row, int col){
    static char buf[8][16];
    static int slot = 0;
    char *b;
    (void)ud;
    slot = (slot + 1) & 7;
    b = buf[slot];
    snprintf(b, sizeof buf[0], "r%dc%d", row, col);
    return b;
}

/* ---- timui_table_ex: sticky header + first data row render --------------- */
TIMUI_TEST(test_table_ex_renders){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiStr hdrs[3] = { TIMUI_STR_LIT("Name"), TIMUI_STR_LIT("Val"), TIMUI_STR_LIT("Z") };
    TimuiTableModel m = {0};
    TimuiTableState st = {0, 0, 0};
    m.headers = hdrs; m.ncols = 3; m.nrows = 4; m.cell_fn = gx_cell; m.ud = 0;

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_table_ex(f, TIMUI_ID("g"), TIMUI_RECT(0, 0, 30, 6), &m, st);
    /* check BEFORE end (end swaps curr/prev). cells_get is (buf, x, y); column 0
     * starts at x=0, the sticky header is y=0 and the first data row is y=1. */
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'N');  /* header "Name" @ (0,0) */
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->codepoint == 'r');  /* data row 0 "r0c0" @ (0,1) */
    timui_end(f);
    timui_close(ui);
}

/* ---- timui_table_ex: down-arrow moves the selection (when focused) -------- */
TIMUI_TEST(test_table_ex_down_key){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr hdrs[2] = { TIMUI_STR_LIT("A"), TIMUI_STR_LIT("B") };
    TimuiTableModel m = {0};
    TimuiTableState st = {0, 0, 0};
    TimuiRect r = TIMUI_RECT(0, 0, 20, 6);
    m.headers = hdrs; m.ncols = 2; m.nrows = 5; m.cell_fn = gx_cell;

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);

    /* focus by clicking a body row (press + release across two frames) */
    SETIN(&fake, "\x1b[<0;1;2M");
    timui_begin(ui, &f); timui_table_ex_mut(f, TIMUI_ID("g"), r, &m, &st); timui_end(f);
    SETIN(&fake, "\x1b[<0;1;2m");
    timui_begin(ui, &f); timui_table_ex_mut(f, TIMUI_ID("g"), r, &m, &st); timui_end(f);
    /* click landed on screen row 1 -> body row 0 selected */
    TIMUI_CHECK(st.selected == 0);

    SETIN(&fake, "\x1b[B");                       /* Down -> row 1 */
    timui_begin(ui, &f); timui_table_ex_mut(f, TIMUI_ID("g"), r, &m, &st); timui_end(f);
    TIMUI_CHECK(st.selected == 1);

    SETIN(&fake, "\x1b[B");                       /* Down -> row 2 */
    timui_begin(ui, &f); timui_table_ex_mut(f, TIMUI_ID("g"), r, &m, &st); timui_end(f);
    TIMUI_CHECK(st.selected == 2);
    timui_close(ui);
}

/* ---- timui_table_ex: horizontal scroll pushes the first column off-left --- */
TIMUI_TEST(test_table_ex_hscroll){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    /* 6 columns of width>=3 + gaps overflow a 10-cell-wide viewport, so an
     * hscroll of 4 must scroll the leftmost content out of view. */
    TimuiStr hdrs[6] = { TIMUI_STR_LIT("AA"), TIMUI_STR_LIT("BB"), TIMUI_STR_LIT("CC"),
                         TIMUI_STR_LIT("DD"), TIMUI_STR_LIT("EE"), TIMUI_STR_LIT("FF") };
    TimuiTableModel m = {0};
    TimuiTableState st0 = {0, 0, 0};
    TimuiTableState st4 = {0, 0, 4};
    TimuiRect r = TIMUI_RECT(0, 0, 10, 4);
    char c00_unscrolled, c00_scrolled;
    m.headers = hdrs; m.ncols = 6; m.nrows = 3; m.cell_fn = gx_cell;

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);

    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_table_ex(f, TIMUI_ID("g"), r, &m, st0);
    c00_unscrolled = (char)timui_cells_get(buf, 0, 0)->codepoint;   /* 'A' (header col 0) */
    timui_end(f);

    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_table_ex(f, TIMUI_ID("g"), r, &m, st4);
    c00_scrolled = (char)timui_cells_get(buf, 0, 0)->codepoint;     /* col 0 scrolled away */
    timui_end(f);

    TIMUI_CHECK(c00_unscrolled == 'A');
    TIMUI_CHECK(c00_scrolled != 'A');    /* the leftmost header shifted off-screen */
    timui_close(ui);
}

TIMUI_TEST(test_table_ex_wheel_only_when_hovered){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr hdrs[2] = { TIMUI_STR_LIT("A"), TIMUI_STR_LIT("B") };
    TimuiTableModel m = {0};
    TimuiTableState top = {0, 0, 0};
    TimuiTableState bottom = {0, 0, 0};
    m.headers = hdrs; m.ncols = 2; m.nrows = 20; m.cell_fn = gx_cell;

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 12, &al);
    SETIN(&fake, "\x1b[<65;2;7M\x1b[<32;2;2M");  /* wheel bottom, then motion top */
    timui_begin(ui, &f);
    timui_table_ex_mut(f, TIMUI_ID("top"), TIMUI_RECT(0, 0, 20, 4), &m, &top);
    timui_table_ex_mut(f, TIMUI_ID("bottom"), TIMUI_RECT(0, 5, 20, 4), &m, &bottom);
    timui_end(f);
    TIMUI_CHECK(top.scroll == 0);
    TIMUI_CHECK(bottom.scroll == 1);
    timui_close(ui);
}

/* ---- timui_tree_scroll: a collapsed node hides its subtree ---------------- */
TIMUI_TEST(test_tree_scroll_hides_collapsed){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    /* root(exp) > A(collapsed) > A1(hidden); B(sibling) reappears. Visible rows:
     * 0 root, 1 A, 2 B — the collapsed A1 must NOT be drawn. */
    TimuiTreeNode nodes[] = {
        {0, "root", 1, 1}, {1, "A", 1, 0}, {2, "A1", 0, 0}, {1, "B", 0, 0}
    };
    TimuiTreeState st = {0, 0};

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_tree_scroll(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 20, 5), nodes, 4, st);
    /* cells_get is (buf, x, y). root label at x=2,y=0 ("- root"); A at x=4,y=1
     * ("  + A"); y=2 is B (x=4), NOT the hidden A1. */
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'r');   /* root @ (2,0) */
    TIMUI_CHECK(timui_cells_get(buf, 4, 1)->codepoint == 'A');   /* A    @ (4,1) */
    TIMUI_CHECK(timui_cells_get(buf, 4, 2)->codepoint == 'B');   /* B    @ (4,2) — A1 hidden */
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_tree_scroll_wheel_only_when_hovered){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTreeNode nodes[] = {
        {0, "n0", 0, 0}, {0, "n1", 0, 0}, {0, "n2", 0, 0}, {0, "n3", 0, 0},
        {0, "n4", 0, 0}, {0, "n5", 0, 0}, {0, "n6", 0, 0}, {0, "n7", 0, 0}
    };
    TimuiTreeState top = {0, 0};
    TimuiTreeState bottom = {0, 0};

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 12, &al);
    SETIN(&fake, "\x1b[<65;2;7M\x1b[<32;2;2M");  /* wheel bottom, then motion top */
    timui_begin(ui, &f);
    timui_tree_scroll_mut(f, TIMUI_ID("toptr"), TIMUI_RECT(0, 0, 20, 4), nodes, 8, &top);
    timui_tree_scroll_mut(f, TIMUI_ID("bottr"), TIMUI_RECT(0, 5, 20, 4), nodes, 8, &bottom);
    timui_end(f);
    TIMUI_CHECK(top.scroll == 0);
    TIMUI_CHECK(bottom.scroll == 1);
    timui_close(ui);
}

/* ---- timui_tree_scroll: viewport windowing drops off-screen rows ---------- */
TIMUI_TEST(test_tree_scroll_windows){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    /* 6 flat leaf nodes, viewport of 3 rows. selected=4, scroll=2 keeps the
     * selection visible, so the window starts at visible position 2 (node "n2"). */
    TimuiTreeNode nodes[] = {
        {0, "n0", 0, 0}, {0, "n1", 0, 0}, {0, "n2", 0, 0},
        {0, "n3", 0, 0}, {0, "n4", 0, 0}, {0, "n5", 0, 0}
    };
    TimuiTreeState st = {4, 2};

    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_tree_scroll(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 20, 3), nodes, 6, st);
    /* leaf prefix is "  " (2 cols), so labels start at x=2. Top row (y=0) = "n2". */
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'n');
    TIMUI_CHECK(timui_cells_get(buf, 3, 0)->codepoint == '2');
    timui_end(f);
    timui_close(ui);
}
