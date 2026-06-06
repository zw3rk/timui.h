/*
 * hello.c — minimal timui.h single-header smoke test (Phase 0).
 *
 * Exercises the pure layout/id helpers; no real terminal is required, so it
 * runs anywhere. The terminal backend lands in Phase 2.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>

int main(void){
    printf("timui.h %s\n", timui_version_string());

    /* Rect-split layout — pure, no terminal required. */
    TimuiRect root = TIMUI_RECT(0, 0, 80, 24);
    TimuiRect top  = timui_cut_top(&root, 1);
    TimuiRect left, right;
    timui_split_cols(root, 0.5f, &left, &right);

    printf("top   = %d,%d  %dx%d\n", top.x, top.y, top.w, top.h);
    printf("left  = %d,%d  %dx%d\n", left.x, left.y, left.w, left.h);
    printf("right = %d,%d  %dx%d\n", right.x, right.y, right.w, right.h);

    /* IDs are stable across frames (FNV-1a, non-cryptographic). */
    printf("id(\"save\") = 0x%016llx\n",
           (unsigned long long)timui_id_from_cstr("save"));

    /* Terminal backend is Phase 2; open() reports unsupported for now. */
    Timui *ui = NULL;
    TimuiResult r = timui_open(&(TimuiConfig){0}, &ui);
    printf("timui_open -> %d (%s)\n", (int)r, timui_error_string(r));
    return 0;
}
