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
#include "async_scan_state.h"

#include <pthread.h>
#include <time.h>      /* nanosleep */

static void *worker(void *arg){
    Timui *ui = (Timui *)arg;
    int i;
    for(i = 0; i <= 10; i++){
        int p = i * 10;
        timui_post(ui, ASYNC_SCAN_MSG_PROGRESS, &p, sizeof p);
        { struct timespec ts = {0, 100 * 1000 * 1000}; nanosleep(&ts, NULL); }   /* 100ms */
    }
    timui_post(ui, ASYNC_SCAN_MSG_DONE, NULL, 0);
    return NULL;
}

static void view(TimuiFrame *f, void *m){
    AsyncScanModel *mdl = (AsyncScanModel *)m;
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
    (void)async_scan_update((AsyncScanModel *)m, type, msg, sz);
}

int main(void){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    AsyncScanModel mdl = {0};
    pthread_t th;
    int thread_started = 0;
    cfg.title     = "timui.h async scan";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    if(pthread_create(&th, NULL, worker, ui) == 0) thread_started = 1;
    else mdl.done = 1;
    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        uint32_t type = 0;
        int val = 0;
        if(!timui_begin(ui, &f)) break;
        for(;;){
            size_t sz = sizeof val;
            if(!timui_recv(ui, &type, &val, &sz)) break;
            update(&mdl, type, sz == 0 ? NULL : &val, sz);
        }
        if(mdl.done) timui_quit(ui);          /* W2: terminate on completion */
        view(f, &mdl);
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);
        timui_end(f);
    }
    if(thread_started) pthread_join(th, NULL);
    timui_close(ui);
    return 0;
}
