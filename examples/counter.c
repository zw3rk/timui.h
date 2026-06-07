/*
 * counter.c — functional controlled counter (T7.2).
 *
 * Demonstrates the functional/controlled style: the app owns the immutable
 * model (count); buttons report clicks; the model changes only here.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    int count = 0;

    cfg.title     = "timui.h counter";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, row, dec, inc;
        char buf[32];
        if(!timui_begin(ui, &f)) break;
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);

        root = timui_root(f);
        snprintf(buf, sizeof buf, "Count: %d", count);
        timui_label(f, root.x + 2, root.y + 1, timui_str_from_cstr(buf),
                    timui_style_make(0xFFFFFF, 0x0000AA, 0));

        row = timui_cut_top(&root, 3);
        row = timui_inset(row, 2);
        dec = timui_cut_left(&row, 12);
        inc = timui_cut_left(&row, 12);
        if(timui_button(f, TIMUI_ID("-"), dec, TIMUI_STR_LIT("- Dec")).clicked) count--;
        if(timui_button(f, TIMUI_ID("+"), inc, TIMUI_STR_LIT("+ Inc")).clicked) count++;

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
