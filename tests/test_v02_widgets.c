/*
 * test_v02_widgets.c — table, tree, command palette (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdio.h>
#include <string.h>

/* ---- table (#47) ---- */
static const char *tbl_cell(void *ud, int row, int col){
    static char buf[2][8];
    int r = row % 2;
    (void)ud;
    if(col == 0) snprintf(buf[r], sizeof buf[r], "r%d", row);
    else         snprintf(buf[r], sizeof buf[r], "c%d", col);
    return buf[r];
}
TIMUI_TEST(test_table_renders){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTableState ts = {0, 0};
    TimuiStr hdrs[2] = { TIMUI_STR_LIT("Name"), TIMUI_STR_LIT("Val") };
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_table_mut(f, TIMUI_ID("t"), TIMUI_RECT(0, 0, 20, 5), hdrs, 2, 3, tbl_cell, 0, &ts);
    /* check BEFORE end (end swaps curr/prev) */
    TIMUI_CHECK(timui_cells_get(buf, 1, 0)->codepoint == 'N');  /* header "Name" */
    TIMUI_CHECK(timui_cells_get(buf, 1, 1)->codepoint == 'r');  /* data "r0" */
    timui_end(f);
    timui_close(ui);
}

/* ---- tree (#48) ---- */
TIMUI_TEST(test_tree_renders){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTreeNode nodes[] = {
        {0, "root", 1, 1}, {1, "child", 0, 0}
    };
    int sel = 0;
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_tree_mut(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 20, 5), nodes, 2, &sel);
    /* root at row 0: expand marker '-' then "root" */
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '-');
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'r');
    /* child at row 1: depth-1 indent (4 chars) then "child" at x=4 */
    TIMUI_CHECK(timui_cells_get(buf, 4, 1)->codepoint == 'c');
    timui_end(f);
    timui_close(ui);
}

/* Pass-3: a deeply-nested node (depth 20) must not overflow the prefix
 * buffer; the label still renders. */
TIMUI_TEST(test_tree_deep_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
    TimuiTreeNode nodes[1] = { { 20, "deep", 0, 0 } };
    int sel = 0, x, found = 0;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 80, 5, &al);
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_tree_mut(f, TIMUI_ID("td"), TIMUI_RECT(0, 0, 80, 5), nodes, 1, &sel);
    for(x = 0; x < 80; x++) if(timui_cells_get(buf, x, 0)->codepoint == 'd') found = 1;
    TIMUI_CHECK(found);                 /* label rendered, no stack overflow */
    timui_end(f);
    timui_close(ui);
}

/* ---- command palette (#50) ---- */
TIMUI_TEST(test_cmd_palette_filter){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr cmds[3] = { TIMUI_STR_LIT("Save"), TIMUI_STR_LIT("Open"), TIMUI_STR_LIT("Quit") };
    TimuiCmdPaletteState cps = {0};
    int r;
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    strcpy(cps.filter, "op");
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    r = timui_command_palette_mut(f, TIMUI_ID("cp"), TIMUI_RECT(0, 0, 20, 6), cmds, 3, &cps).activated;
    TIMUI_CHECK(r == -1);             /* not activated yet (no Enter) */
    /* "Open" visible in the list at row 2 (inside the panel body) */
    TIMUI_CHECK(timui_cells_get(buf, 2, 2)->codepoint == 'O');
    timui_end(f);
    timui_close(ui);
}

/* Z26: the controlled (value) form never touches caller memory, and the _mut
 * twin writes back only on a real change — a pure out-of-range clamp is NOT
 * written back (fixes the old unconditional-write-back aliasing surprise). */
TIMUI_TEST(test_tree_controlled_no_write_without_change){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTreeNode nodes[] = { {0, "a", 0, 0}, {0, "b", 0, 0} };
    int sel = 99;                         /* out of range, no interaction this frame */
    TimuiTreeResult res, vres;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

    timui_begin(ui, &f);
    res = timui_tree_mut(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 10, 3), nodes, 2, &sel);
    timui_end(f);
    TIMUI_CHECK(sel == 99);              /* pure clamp NOT written back — no aliasing */
    TIMUI_CHECK(res.selected == 1 && res.state_changed == 0);  /* result still reports clamp */

    timui_begin(ui, &f);
    vres = timui_tree(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 10, 3), nodes, 2, 5);  /* by value */
    timui_end(f);
    TIMUI_CHECK(vres.selected == 1);     /* clamped in the result, caller int impossible to touch */
    timui_close(ui);
}
