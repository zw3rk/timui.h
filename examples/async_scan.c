/*
 * async_scan.c — background worker posts progress; UI drains it (T6.3).
 *
 * A worker thread posts MSG_PROGRESS messages via the thread-safe timui_post;
 * the UI loop drains them with timui_recv and updates an immutable model.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <pthread.h>
#include <time.h>      /* nanosleep */

typedef struct { int progress; int done; } Model;
enum { MSG_PROGRESS = 1, MSG_DONE };

static void *worker(void *arg){
    Timui *ui = (Timui *)arg;
    int i;
    for(i = 0; i <= 10; i++){
        int p = i * 10;
        timui_post(ui, MSG_PROGRESS, &p, sizeof p);
        { struct timespec ts = {0, 100 * 1000 * 1000}; nanosleep(&ts, NULL); }   /* 100ms */
    }
    { int d = 1; timui_post(ui, MSG_DONE, &d, sizeof d); }
    return NULL;
}

static void view(TimuiFrame *f, void *m){
    Model *mdl = (Model *)m;
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    TimuiRect root = timui_root(f);
    TimuiRect bar;
    int i, filled;
    timui_label(f, root.x + 2, root.y + 1,
                TIMUI_STR_LIT(mdl->done ? "Done!" : "Scanning..."),
                timui_style_make(0xFFFFFF, 0x0000AA, 0));
    bar = TIMUI_RECT(root.x + 2, root.y + 3, 30, 1);
    timui_draw_fill(buf, bar, timui_style_make(0xFFFFFF, 0x000055, 0));
    filled = (bar.w * mdl->progress) / 100;
    for(i = 0; i < filled; i++)
        timui_draw_text(buf, bar.x + i, bar.y, TIMUI_STR_LIT("#"),
                        timui_style_make(0x55FF55, 0x000055, 0));
}

static void update(void *m, uint32_t type, const void *msg, size_t sz){
    Model *mdl = (Model *)m;
    (void)sz;
    if(type == MSG_PROGRESS) mdl->progress = *(const int *)msg;
    else if(type == MSG_DONE){ mdl->progress = 100; mdl->done = 1; }
}

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    Model mdl = {0};
    pthread_t th;
    cfg.title     = "timui.h async scan";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    pthread_create(&th, NULL, worker, ui);
    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        uint32_t type = 0;
        int val = 0;
        size_t sz = sizeof val;
        if(!timui_begin(ui, &f)) break;
        while(timui_recv(ui, &type, &val, &sz)) update(&mdl, type, &val, sz);
        view(f, &mdl);
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);
        sz = sizeof val;
        timui_end(f);
    }
    pthread_join(th, NULL);
    timui_close(ui);
    return 0;
}
