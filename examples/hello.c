/*
 * hello.c — minimal timui.h app: a centred "Hello!" (T7.1).
 *
 * Build:   cc -std=c99 -Wall -Wextra -Wpedantic -O2 -pthread \
 *              -Iinclude examples/hello.c -o hello
 * Run:     ./hello   (press Esc to quit)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;

    cfg.title     = "timui.h hello";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root;
        if(!timui_begin(ui, &f)) break;
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);
        root = timui_root(f);
        timui_label(f, root.w / 2 - 3, root.h / 2, TIMUI_STR_LIT("Hello!"),
                    timui_style_make(0xFFFFFF, 0x0000AA, 0));
        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
