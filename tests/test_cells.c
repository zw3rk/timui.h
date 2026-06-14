/*
 * test_cells.c — cell buffer (T3.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>
#include <stdlib.h>

TIMUI_TEST(test_cells_init_clear){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TIMUI_CHECK(timui_cells_init(&b, 10, 5, &al) == TIMUI_OK);
    TIMUI_CHECK(b.w == 10 && b.h == 5 && b.cells != NULL);
    timui_cells_clear(&b);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->flags == TIMUI_CELL_EMPTY);
    timui_cells_destroy(&b);
    TIMUI_CHECK(b.cells == NULL);
}

TIMUI_TEST(test_cells_put_get_roundtrip){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiCell c;
    TimuiCell *g;
    timui_cells_init(&b, 8, 8, &al);
    memset(&c, 0, sizeof c);
    c.codepoint = 'A';
    c.fg = 0xffffff;
    c.flags = TIMUI_CELL_DIRTY;
    TIMUI_CHECK(timui_cells_put(&b, 3, 4, &c) == 1);
    g = timui_cells_get(&b, 3, 4);
    TIMUI_CHECK(g != NULL && g->codepoint == 'A' && g->fg == 0xffffff && g->flags == TIMUI_CELL_DIRTY);

    /* bounds checking */
    TIMUI_CHECK(timui_cells_get(&b, 8, 0) == NULL);
    TIMUI_CHECK(timui_cells_put(&b, 0, 8, &c) == 0);

    timui_cells_destroy(&b);
}

TIMUI_TEST(test_cells_resize){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    timui_cells_init(&b, 4, 4, &al);
    TIMUI_CHECK(timui_cells_resize(&b, 20, 10, &al) == TIMUI_OK);
    TIMUI_CHECK(b.w == 20 && b.h == 10);
    TIMUI_CHECK(timui_cells_get(&b, 19, 9) != NULL);   /* valid in new size */
    timui_cells_destroy(&b);
}

/* V19: dimensions whose product (× sizeof cell) overflow size_t must return
 * OOM, not allocate a tiny buffer and memset past its end. */
TIMUI_TEST(test_cells_init_overflow_guard){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TIMUI_CHECK(timui_cells_init(&b, 0x7fffffff, 0x7fffffff, &al) == TIMUI_ERR_OUT_OF_MEMORY);
    TIMUI_CHECK(b.cells == NULL);
}

/* V10: if curr's resize fails after prev's succeeded, ui_resize must roll back
 * so curr/prev/ui dimensions never diverge. We arm a failing realloc to fail
 * the 2nd realloc after arming (curr's), then assert the frame width AND the
 * frame buffer width both stay at the original. */
typedef struct { int armed; int fail_on; int n; } FailCtx;
static void *fc_alloc(void *ud, size_t sz){ (void)ud; return malloc(sz); }
static void *fc_realloc(void *ud, void *p, size_t os, size_t ns){
    FailCtx *fc = (FailCtx *)ud; (void)os;
    if(fc->armed){ fc->n++; if(fc->n == fc->fail_on) return NULL; }
    return realloc(p, ns);
}
static void fc_free(void *ud, void *p, size_t sz){ (void)ud; (void)sz; free(p); }

TIMUI_TEST(test_resize_oom_keeps_dims){
    TimuiAllocator al;
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    FailCtx fc = {0, 0, 0};
    al.userdata = &fc;
    al.alloc = fc_alloc; al.realloc = fc_realloc; al.free = fc_free;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    fc.armed = 1; fc.fail_on = 2;             /* fail curr's resize (2nd armed realloc) */
    timui_ui_resize(ui, 40, 12);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_width(f) == 30 && timui_height(f) == 10);          /* ui->w/h unchanged */
    TIMUI_CHECK(timui_frame_buffer(f)->w == 30);                         /* curr not diverged */
    timui_end(f);
    timui_close(ui);
}
