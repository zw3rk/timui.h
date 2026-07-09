/*
 * async_scan_state.h — pure state helper for examples/async_scan.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#ifndef TIMUI_ASYNC_SCAN_STATE_H
#define TIMUI_ASYNC_SCAN_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { int progress; int done; } AsyncScanModel;
enum { ASYNC_SCAN_MSG_PROGRESS = 1, ASYNC_SCAN_MSG_DONE };

static int async_scan_clamp_progress_(int progress){
    if(progress < 0) return 0;
    if(progress > 100) return 100;
    return progress;
}

static int async_scan_update(AsyncScanModel *m, uint32_t type, const void *msg, size_t sz){
    int progress;
    if(!m) return 0;
    if(type == ASYNC_SCAN_MSG_PROGRESS){
        if(!msg || sz != sizeof progress) return 0;
        memcpy(&progress, msg, sizeof progress);
        m->progress = async_scan_clamp_progress_(progress);
        return 1;
    }
    if(type == ASYNC_SCAN_MSG_DONE){
        if(msg || sz != 0) return 0;
        m->progress = 100;
        m->done = 1;
        return 1;
    }
    return 0;
}

#endif /* TIMUI_ASYNC_SCAN_STATE_H */
