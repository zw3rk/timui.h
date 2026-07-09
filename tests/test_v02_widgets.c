/*
 * test_v02_widgets.c — table, tree, command palette (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdio.h>
#include <string.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

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
    TimuiTableState ts = {0, 0, 0};
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

TIMUI_TEST(test_table_draws_keyboard_selection_same_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTableState ts = {0, 0, 0};
    TimuiTableResult res;
    TimuiStr hdrs[2] = { TIMUI_STR_LIT("Name"), TIMUI_STR_LIT("Val") };
    TimuiCellBuffer *buf;
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    uint32_t sel_bg = timui_theme_style(&th, TIMUI_SLOT_SELECTION).bg;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    SETIN(&fake, "\x1b[B");
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_set_focus(f, TIMUI_ID("t"));
    res = timui_table_mut(f, TIMUI_ID("t"), TIMUI_RECT(0, 0, 20, 5), hdrs, 2, 3, tbl_cell, 0, &ts);
    TIMUI_CHECK(res.state.selected == 1 && ts.selected == 1);
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->bg != sel_bg);
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->bg == sel_bg);
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

TIMUI_TEST(test_tree_draws_keyboard_selection_same_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiTreeNode nodes[] = {
        {0, "root", 1, 1}, {1, "child", 0, 0}, {1, "peer", 0, 0}
    };
    TimuiTreeResult res;
    TimuiCellBuffer *buf;
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    uint32_t sel_bg = timui_theme_style(&th, TIMUI_SLOT_SELECTION).bg;
    int sel = 0;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    SETIN(&fake, "\x1b[B");
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_set_focus(f, TIMUI_ID("tr"));
    res = timui_tree_mut(f, TIMUI_ID("tr"), TIMUI_RECT(0, 0, 20, 5), nodes, 3, &sel);
    TIMUI_CHECK(res.selected == 1 && sel == 1);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->bg != sel_bg);
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->bg == sel_bg);
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

TIMUI_TEST(test_cmd_palette_draws_keyboard_selection_same_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr cmds[3] = { TIMUI_STR_LIT("Save"), TIMUI_STR_LIT("Open"), TIMUI_STR_LIT("Quit") };
    TimuiCmdPaletteState cps = {0};
    TimuiCmdPaletteResult res;
    TimuiCellBuffer *buf;
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    uint32_t sel_bg = timui_theme_style(&th, TIMUI_SLOT_SELECTION).bg;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    SETIN(&fake, "\x1b[B");
    timui_begin(ui, &f); buf = timui_frame_buffer(f);
    timui_set_focus(f, TIMUI_ID("cp") + 1);
    res = timui_command_palette_mut(f, TIMUI_ID("cp"), TIMUI_RECT(0, 0, 20, 6), cmds, 3, &cps);
    TIMUI_CHECK(res.state.selected == 1 && cps.selected == 1);
    TIMUI_CHECK(timui_cells_get(buf, 1, 2)->bg != sel_bg);
    TIMUI_CHECK(timui_cells_get(buf, 1, 3)->bg == sel_bg);
    timui_end(f);
    timui_close(ui);
}

/* ---- combobox / autocomplete (Phase 1.5) ---- */
TIMUI_TEST(test_combobox_filter_select_activate){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[3] = { TIMUI_STR_LIT("Apple"), TIMUI_STR_LIT("Apricot"), TIMUI_STR_LIT("Banana") };
    char query[32] = {0};
    TimuiComboboxState st = { query, sizeof query, 0, 0, 0, 0, 0 };
    TimuiComboboxResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 4);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define CB_FRAME() do{ timui_begin(ui,&f); res = timui_combobox_mut(f, TIMUI_ID("cb"), r, opts, 3, &st); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); CB_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); CB_FRAME();
    SETIN(&fake, "ap"); CB_FRAME();
    TIMUI_CHECK(res.state_changed && st.open && strcmp(query, "ap") == 0 && st.selected == 0);
    TIMUI_CHECK(res.match_count == 2 && res.selected == 0);
    SETIN(&fake, "\x1b[B"); CB_FRAME();
    TIMUI_CHECK(st.selected == 1 && res.selected == 1);
    SETIN(&fake, "\r"); CB_FRAME();
    TIMUI_CHECK(res.activated == 1 && !st.open);
    TIMUI_CHECK(strcmp(query, "Apricot") == 0 && st.cursor == strlen(query));
#undef CB_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_combobox_no_match_and_clamp){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[2] = { TIMUI_STR_LIT("Alpha"), TIMUI_STR_LIT("Beta") };
    char query[8] = "zz";
    TimuiComboboxState st = { query, sizeof query, 2, 0, 1, 7, 3 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("empty"));
    res = timui_combobox_mut(f, TIMUI_ID("empty"), TIMUI_RECT(0, 0, 20, 4), opts, 2, &st);
    timui_end(f);
    TIMUI_CHECK(res.selected == -1 && res.activated == -1);
    TIMUI_CHECK(res.state.selected == 0 && res.state.scroll == 0 && strcmp(query, "zz") == 0);
    TIMUI_CHECK(st.selected == 7 && st.scroll == 3);
    timui_close(ui);
}

TIMUI_TEST(test_combobox_mouse_accept_duplicate){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[3] = { TIMUI_STR_LIT("Cat"), TIMUI_STR_LIT("Cat"), TIMUI_STR_LIT("Car") };
    char query[16] = "cat";
    TimuiComboboxState st = { query, sizeof query, 3, 0, 1, 0, 0 };
    TimuiComboboxResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 4);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define CM_FRAME() do{ timui_begin(ui,&f); res = timui_combobox_mut(f, TIMUI_ID("dup"), r, opts, 3, &st); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;3M"); CM_FRAME();   /* second popup row: 0-based y=2 */
    SETIN(&fake, "\x1b[<0;2;3m"); CM_FRAME();
    TIMUI_CHECK(res.activated == 1);
    TIMUI_CHECK(strcmp(query, "Cat") == 0 && !st.open);
#undef CM_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_combobox_query_cap_utf8_no_split){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[1] = { TIMUI_STR_LIT("ab") };
    char query[4] = {0};
    TimuiComboboxState st = { query, sizeof query, 0, 0, 0, 0, 0 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "ab\xC3\xA9");
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("utf8"));
    res = timui_combobox_mut(f, TIMUI_ID("utf8"), TIMUI_RECT(0, 0, 20, 3), opts, 1, &st);
    timui_end(f);
    TIMUI_CHECK(res.query_changed && strcmp(query, "ab") == 0 && st.cursor == 2);
    timui_close(ui);
}

TIMUI_TEST(test_combobox_escape_closes_without_clearing_query){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[2] = { TIMUI_STR_LIT("Alpha"), TIMUI_STR_LIT("Alpine") };
    char query[16] = "al";
    TimuiComboboxState st = { query, sizeof query, 2, 0, 1, 0, 0 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "\x1b[27u");
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("esc"));
    res = timui_combobox_mut(f, TIMUI_ID("esc"), TIMUI_RECT(0, 0, 20, 3), opts, 2, &st);
    timui_end(f);
    TIMUI_CHECK(res.state_changed && !st.open && res.activated == -1);
    TIMUI_CHECK(strcmp(query, "al") == 0);
    timui_close(ui);
}

TIMUI_TEST(test_combobox_cursor_movement_updates_state){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[1] = { TIMUI_STR_LIT("abc") };
    char query[8] = "abc";
    TimuiComboboxState st = { query, sizeof query, 3, 0, 0, 0, 0 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "\x1b[D");
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("move"));
    res = timui_combobox_mut(f, TIMUI_ID("move"), TIMUI_RECT(0, 0, 20, 3), opts, 1, &st);
    timui_end(f);
    TIMUI_CHECK(res.state_changed && !res.query_changed && st.cursor == 2);
    timui_close(ui);
}

TIMUI_TEST(test_combobox_clamps_cursor_to_query_len_before_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[1] = { TIMUI_STR_LIT("abcQ") };
    char query[8] = { 'a', 'b', 'c', '\0', 'X', 'Y', 'Z', '\0' };
    TimuiComboboxState st = { query, sizeof query, 6, 0, 0, 0, 0 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "Q");
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("stale"));
    res = timui_combobox_mut(f, TIMUI_ID("stale"), TIMUI_RECT(0, 0, 20, 3), opts, 1, &st);
    timui_end(f);
    TIMUI_CHECK(res.query_changed && strcmp(query, "abcQ") == 0);
    TIMUI_CHECK(st.cursor == 4);
    timui_close(ui);
}

TIMUI_TEST(test_combobox_guards_empty_options){
    TimuiComboboxState st;
    TimuiComboboxResult res;
    char query[8] = "x";
    memset(&st, 0, sizeof st);
    st.query = query;
    st.cap = sizeof query;
    res = timui_combobox_mut(NULL, TIMUI_ID("g"), TIMUI_RECT(0, 0, 10, 3), NULL, 0, &st);
    TIMUI_CHECK(res.activated == -1 && res.selected == -1 && !res.state_changed);
    res = timui_combobox(NULL, TIMUI_ID("g"), TIMUI_RECT(0, 0, 10, 3), NULL, -1, st);
    TIMUI_CHECK(res.activated == -1 && res.selected == -1);
    st.query = NULL;
    res = timui_combobox(NULL, TIMUI_ID("g"), TIMUI_RECT(0, 0, 10, 3), NULL, 0, st);
    TIMUI_CHECK(res.activated == -1 && res.selected == -1);
}

TIMUI_TEST(test_combobox_accept_cap_limited){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr opts[1] = { TIMUI_STR_LIT("LongOption") };
    char query[5] = {0};
    TimuiComboboxState st = { query, sizeof query, 0, 0, 1, 0, 0 };
    TimuiComboboxResult res;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "\r");
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("cap"));
    res = timui_combobox_mut(f, TIMUI_ID("cap"), TIMUI_RECT(0, 0, 20, 3), opts, 1, &st);
    timui_end(f);
    TIMUI_CHECK(res.activated == 0);
    TIMUI_CHECK(strcmp(query, "Long") == 0 && st.cursor == strlen(query));
    timui_close(ui);
}

/* ---- toast / notification (Phase 1.5) ---- */
TIMUI_TEST(test_toast_order_timeout_dismiss){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiToast toasts[3];
    TimuiToastResult res;
    TimuiCellBuffer *buf;
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    TimuiStyle success = timui_theme_style(&th, TIMUI_SLOT_SUCCESS);
    TimuiStyle error = timui_theme_style(&th, TIMUI_SLOT_ERROR);
    memset(toasts, 0, sizeof toasts);
    toasts[0].title = TIMUI_STR_LIT("Saved");
    toasts[0].message = TIMUI_STR_LIT("File written");
    toasts[0].severity = TIMUI_TOAST_SUCCESS;
    toasts[0].created_ms = 100;
    toasts[0].ttl_ms = 1000;
    toasts[1].title = TIMUI_STR_LIT("Old");
    toasts[1].message = TIMUI_STR_LIT("Expired");
    toasts[1].severity = TIMUI_TOAST_WARNING;
    toasts[1].created_ms = 0;
    toasts[1].ttl_ms = 10;
    toasts[2].title = TIMUI_STR_LIT("Network");
    toasts[2].message = TIMUI_STR_LIT("Offline");
    toasts[2].severity = TIMUI_TOAST_ERROR;
    toasts[2].created_ms = 100;
    toasts[2].ttl_ms = 0;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 30, 8), toasts, 3, 200);
    TIMUI_CHECK(res.visible_count == 2 && res.dismissed == -1);
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'S');
    TIMUI_CHECK(timui_cells_get(buf, 2, 3)->codepoint == 'N');
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->fg == success.fg);
    TIMUI_CHECK(timui_cells_get(buf, 2, 3)->fg == error.fg);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;4M");  /* click second visible toast, original index 2 */
    timui_begin(ui, &f); res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 30, 8), toasts, 3, 200); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;4m");
    timui_begin(ui, &f); res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 30, 8), toasts, 3, 200); timui_end(f);
    TIMUI_CHECK(res.dismissed == 2);
    timui_close(ui);
}

TIMUI_TEST(test_toast_clips_stack){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiToast toasts[3];
    TimuiToastResult res;
    TimuiCellBuffer *buf;
    memset(toasts, 0, sizeof toasts);
    toasts[0].title = TIMUI_STR_LIT("OneVeryLongTitle"); toasts[0].message = TIMUI_STR_LIT("MessageLongerThanBox");
    toasts[1].title = TIMUI_STR_LIT("Two");   toasts[1].message = TIMUI_STR_LIT("B");
    toasts[2].title = TIMUI_STR_LIT("Three"); toasts[2].message = TIMUI_STR_LIT("C");
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 8, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(2, 0, 10, 4), toasts, 3, 1);
    TIMUI_CHECK(res.visible_count == 1);
    TIMUI_CHECK(timui_cells_get(buf, 4, 0)->codepoint == 'O');
    TIMUI_CHECK(timui_cells_get(buf, 4, 3)->codepoint != 'T');
    TIMUI_CHECK(timui_cells_get(buf, 12, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(buf, 12, 1)->codepoint == 0);
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_toast_guards){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiToast toasts[3];
    TimuiToastResult res;
    res = timui_toasts(NULL, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 10, 3), NULL, 0, 0);
    TIMUI_CHECK(res.visible_count == 0 && res.dismissed == -1);
    res = timui_toasts(NULL, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 10, 3), NULL, -1, 0);
    TIMUI_CHECK(res.visible_count == 0 && res.dismissed == -1);
    memset(toasts, 0, sizeof toasts);
    toasts[0].title = TIMUI_STR_LIT("Gone");
    toasts[0].message = TIMUI_STR_LIT("dismissed");
    toasts[0].ttl_ms = 0;
    toasts[0].dismissed = 1;
    toasts[1].title = TIMUI_STR_LIT("Exact");
    toasts[1].message = TIMUI_STR_LIT("expired at equality");
    toasts[1].created_ms = 90;
    toasts[1].ttl_ms = 10;
    toasts[2].title = TIMUI_STR_LIT("Future");
    toasts[2].message = TIMUI_STR_LIT("clock skew");
    toasts[2].created_ms = 200;
    toasts[2].ttl_ms = 10;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 6, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 20, 6), NULL, 1, 100);
    TIMUI_CHECK(res.visible_count == 0 && res.dismissed == -1);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 0, 6), toasts, 3, 100);
    TIMUI_CHECK(res.visible_count == 0 && res.dismissed == -1);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 20, 6), toasts, 0, 100);
    TIMUI_CHECK(res.visible_count == 0 && res.dismissed == -1);
    res = timui_toasts(f, TIMUI_ID("toast"), TIMUI_RECT(0, 0, 20, 6), toasts, 3, 100);
    TIMUI_CHECK(res.visible_count == 1 && res.dismissed == -1);
    TIMUI_CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'F');
    TIMUI_CHECK(timui_cells_get(buf, 2, 3)->codepoint == 0);
    timui_end(f);
    timui_close(ui);
}

/* ---- split / resizable panes (Phase 1.5) ---- */
TIMUI_TEST(test_split_pane_horizontal_drag_clamps_minmax){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiSplitPaneState st = { 0.5f, 10, 20 };
    TimuiSplitPaneResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 100, 5);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 120, 8, &al);

    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(!res.changed && !res.dragging);
    TIMUI_CHECK(res.first.w == 50 && res.divider.x == 50 && res.second.w == 49);
    timui_end(f);

    SETIN(&fake, "\x1b[<0;51;3M");      /* press the divider at x=50, y=2 */
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(res.dragging && !res.changed);
    timui_end(f);

    SETIN(&fake, "\x1b[<32;86;3M");     /* drag to x=85, clamped by min_second=20 */
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(res.changed && res.dragging);
    TIMUI_CHECK(res.first.w == 79 && res.divider.x == 79 && res.second.w == 20);
    TIMUI_CHECK(st.ratio > 0.79f && st.ratio < 0.81f);
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_split_pane_vertical_geometry_and_guards){
    TimuiSplitPaneState st = { 0.25f, 2, 3 };
    TimuiSplitPaneResult res;
    res = timui_split_pane(NULL, TIMUI_ID("split"), TIMUI_RECT(0, 0, 10, 20),
                           TIMUI_AXIS_V, st);
    TIMUI_CHECK(!res.changed && !res.dragging);
    TIMUI_CHECK(res.first.h == 5 && res.divider.y == 5 && res.second.h == 14);
    TIMUI_CHECK(res.first.w == 10 && res.divider.w == 10 && res.second.w == 10);

    st.ratio = -10.0f; st.min_first = -1; st.min_second = -2;
    res = timui_split_pane(NULL, TIMUI_ID("split"), TIMUI_RECT(0, 0, 8, 4),
                           TIMUI_AXIS_H, st);
    TIMUI_CHECK(res.first.w == 0 && res.divider.w == 1 && res.second.w == 7);

    st.ratio = 0.5f; st.min_first = 10; st.min_second = 10;
    res = timui_split_pane(NULL, TIMUI_ID("split"), TIMUI_RECT(0, 0, 5, 4),
                           TIMUI_AXIS_H, st);
    TIMUI_CHECK(res.first.w >= 0 && res.second.w >= 0 && res.divider.w == 1);
    TIMUI_CHECK(res.first.w + res.divider.w + res.second.w == 5);
}

TIMUI_TEST(test_split_pane_mut_writeback_only_on_drag){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiSplitPaneState st = { 0.25f, 0, 0 };
    TimuiSplitPaneResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 40, 4);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 60, 6, &al);

    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(!res.changed && st.ratio == 0.25f);
    timui_end(f);

    SETIN(&fake, "\x1b[<0;11;2M");
    timui_begin(ui, &f);
    (void)timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    timui_end(f);

    SETIN(&fake, "\x1b[<32;21;2M");
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(res.changed && st.ratio > 0.49f && st.ratio < 0.53f);
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_split_pane_drag_requires_divider_press){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiSplitPaneState st = { 0.25f, 0, 0 };
    TimuiSplitPaneResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 40, 4);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 60, 6, &al);

    SETIN(&fake, "\x1b[<0;2;2M");       /* press inside first pane, not divider */
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(!res.dragging && !res.changed && st.ratio == 0.25f);
    timui_end(f);

    SETIN(&fake, "\x1b[<32;11;2M");     /* move over divider while still down */
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(!res.dragging && !res.changed && st.ratio == 0.25f);
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_split_pane_wheel_does_not_drag){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiSplitPaneState st = { 0.5f, 0, 0 };
    TimuiSplitPaneResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 40, 4);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 60, 6, &al);

    SETIN(&fake, "\x1b[<64;20;2M");
    timui_begin(ui, &f);
    res = timui_split_pane_mut(f, TIMUI_ID("split"), r, TIMUI_AXIS_H, &st);
    TIMUI_CHECK(!res.dragging && !res.changed && st.ratio == 0.5f);
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
