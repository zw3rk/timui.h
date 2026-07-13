/*
 * counter_runner.c — functional counter via timui_run (T6.1).
 *
 * The model is immutable to the view; buttons emit messages; update is the
 * only place the model changes.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>

typedef struct { int count; } Model;
enum { MSG_INC, MSG_DEC };

static void view(TimuiFrame *f, void *m){
    Model *mdl = (Model *)m;
    TimuiRect root = timui_root(f);
    TimuiRect row, dec, inc;
    char buf[32];
    snprintf(buf, sizeof buf, "Count: %d", mdl->count);
    timui_label(f, root.x + 2, root.y + 1, timui_str_from_cstr(buf),
                timui_style_make(0xFFFFFF, 0x0000AA, 0));
    row = timui_cut_top(&root, 3);
    row = timui_inset(row, 2);
    dec = timui_cut_left(&row, 12);
    inc = timui_cut_left(&row, 12);
    if(timui_button(f, TIMUI_ID("-"), dec, TIMUI_STR_LIT("- Dec")).clicked){
        uint32_t t = MSG_DEC; timui_emit(f, MSG_DEC, &t, sizeof t);
    }
    if(timui_button(f, TIMUI_ID("+"), inc, TIMUI_STR_LIT("+ Inc")).clicked){
        uint32_t t = MSG_INC; timui_emit(f, MSG_INC, &t, sizeof t);
    }
    if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_frame_quit(f);
}

static void update(void *m, uint32_t type, const void *msg, size_t sz){
    Model *mdl = (Model *)m;
    (void)msg; (void)sz;
    if(type == MSG_INC) mdl->count++;
    else if(type == MSG_DEC) mdl->count--;
}

int main(void){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Model mdl = {0};
    TimuiApp app = { &mdl, view, update };
    cfg.title     = "timui.h counter (runner)";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;
    return timui_run(&cfg, &app);
}
