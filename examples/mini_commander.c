/*
 * mini_commander.c — two-pane file-manager skeleton (T7.4).
 *
 * Shows the DOS / Midnight-Commander aesthetic: a menu strip, two bordered
 * panes with scrollable lists, a command line, and a function-key bar.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

static const char *LITEMS[] = { "..", "src", "include", "README.md", "main.c", "build" };
static const char *RITEMS[] = { "..", "bin", "lib", "share", "notes.txt" };
static const char *pick(void *ud, int i){ const char **a = (const char **)ud; return a[i]; }

int main(void){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    TimuiListState lst = {0, 0}, rst = {0, 0};
    char cmd[128] = {0};

    cfg.title     = "timui.h mini commander";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_BRACKETED_PASTE
                  | TIMUI_FLAG_TRUECOLOR | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, menu, keys, cmdbar, left, right, lbody, rbody;
        if(!timui_begin(ui, &f)) break;
        if(timui_key_pressed(f, TIMUI_KEY_F10) || timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);

        root  = timui_root(f);
        menu  = timui_cut_top(&root, 1);
        keys  = timui_cut_bottom(&root, 1);
        cmdbar = timui_cut_bottom(&root, 1);
        timui_split_cols(root, 0.5f, &left, &right);

        timui_function_bar(f, menu, TIMUI_STR_LIT(" File  Edit  View  Help"));
        lbody = timui_panel_begin(f, TIMUI_ID("lp"), left,  TIMUI_STR_LIT("/left"),  TIMUI_BORDER_DOUBLE);
        timui_listbox_mut(f, TIMUI_ID("ll"), lbody, &lst, 6, pick, LITEMS);
        timui_panel_end(f);
        rbody = timui_panel_begin(f, TIMUI_ID("rp"), right, TIMUI_STR_LIT("/right"), TIMUI_BORDER_DOUBLE);
        timui_listbox_mut(f, TIMUI_ID("rl"), rbody, &rst, 5, pick, RITEMS);
        timui_panel_end(f);
        timui_input_line_buf(f, TIMUI_ID("cmd"), cmdbar, cmd, sizeof cmd);
        timui_function_bar(f, keys,
            TIMUI_STR_LIT("1 Help  2 Menu  3 View  4 Edit  5 Copy  6 Move  7 Mkdir  8 Delete  10 Quit"));

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
