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
    memset(&b, 0xA5, sizeof b);
    TIMUI_CHECK(timui_cells_init(&b, 0x7fffffff, 0x7fffffff, &al) == TIMUI_ERR_OUT_OF_MEMORY);
    TIMUI_CHECK(b.cells == NULL && b.links == NULL && b.w == 0 && b.h == 0);
}

typedef struct { size_t owner; } OwnerHdr;
typedef struct { size_t id; int wrong_realloc; int wrong_free; } OwnerCtx;
static void *owner_alloc(void *ud, size_t sz){
    OwnerCtx *ctx = (OwnerCtx *)ud;
    OwnerHdr *h = (OwnerHdr *)malloc(sizeof(*h) + sz);
    if(!h) return NULL;
    h->owner = ctx->id;
    return h + 1;
}
static void *owner_realloc(void *ud, void *p, size_t os, size_t ns){
    OwnerCtx *ctx = (OwnerCtx *)ud;
    OwnerHdr *h;
    (void)os;
    if(!p) return owner_alloc(ud, ns);
    h = ((OwnerHdr *)p) - 1;
    if(h->owner != ctx->id){ ctx->wrong_realloc++; return NULL; }
    h = (OwnerHdr *)realloc(h, sizeof(*h) + ns);
    if(!h) return NULL;
    h->owner = ctx->id;
    return h + 1;
}
static void owner_free(void *ud, void *p, size_t sz){
    OwnerCtx *ctx = (OwnerCtx *)ud;
    OwnerHdr *h;
    (void)sz;
    if(!p) return;
    h = ((OwnerHdr *)p) - 1;
    if(h->owner != ctx->id) ctx->wrong_free++;
    free(h);
}

TIMUI_TEST(test_cells_resize_keeps_original_allocator){
    OwnerCtx c1 = {1, 0, 0}, c2 = {2, 0, 0};
    TimuiAllocator a1 = {&c1, owner_alloc, owner_realloc, owner_free};
    TimuiAllocator a2 = {&c2, owner_alloc, owner_realloc, owner_free};
    TimuiCellBuffer b;

    TIMUI_CHECK(timui_cells_init(&b, 4, 4, &a1) == TIMUI_OK);
    TIMUI_CHECK(timui_cells_resize(&b, 8, 8, &a2) == TIMUI_OK);
    TIMUI_CHECK(c1.wrong_realloc == 0 && c2.wrong_realloc == 0);
    timui_cells_destroy(&b);
    TIMUI_CHECK(c1.wrong_free == 0 && c2.wrong_free == 0);
}

/* V10: a failed ui_resize must leave curr/prev/ui dimensions identical. The
 * failure injector counts armed alloc/realloc calls so this covers both the old
 * rollback path and the transactional replacement-buffer path. */
typedef struct { int armed; int fail_on; int fail_on2; int n; } FailCtx;
static int fc_should_fail(FailCtx *fc){
    if(!fc->armed) return 0;
    fc->n++;
    return fc->n == fc->fail_on || fc->n == fc->fail_on2;
}
static void *fc_alloc(void *ud, size_t sz){
    FailCtx *fc = (FailCtx *)ud;
    if(fc_should_fail(fc)) return NULL;
    return malloc(sz);
}
static void *fc_realloc(void *ud, void *p, size_t os, size_t ns){
    FailCtx *fc = (FailCtx *)ud; (void)os;
    if(fc_should_fail(fc)) return NULL;
    return realloc(p, ns);
}
static void fc_free(void *ud, void *p, size_t sz){ (void)ud; (void)sz; free(p); }

TIMUI_TEST(test_resize_oom_keeps_dims){
    TimuiAllocator al;
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    FailCtx fc = {0, 0, 0, 0};
    al.userdata = &fc;
    al.alloc = fc_alloc; al.realloc = fc_realloc; al.free = fc_free;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    fc.armed = 1; fc.fail_on = 2;             /* fail curr/2nd replacement buffer */
    /* Z12: the OOM is now reported to the caller (was void). */
    TIMUI_CHECK(timui_ui_resize(ui, 40, 12) == TIMUI_ERR_OUT_OF_MEMORY);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_width(f) == 30 && timui_height(f) == 10);          /* ui->w/h unchanged */
    TIMUI_CHECK(timui_frame_buffer(f)->w == 30);                         /* curr not diverged */
    timui_end(f);
    /* Z12: a subsequent unarmed resize succeeds and reports TIMUI_OK; NULL/0 args
     * report TIMUI_ERR_INVALID_ARGUMENT. */
    fc.armed = 0;
    TIMUI_CHECK(timui_ui_resize(ui, 40, 12) == TIMUI_OK);
    TIMUI_CHECK(timui_ui_resize(NULL, 40, 12) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_ui_resize(ui, 0, 12) == TIMUI_ERR_INVALID_ARGUMENT);
    timui_close(ui);
}

TIMUI_TEST(test_resize_oom_rollback_failure_keeps_dims){
    TimuiAllocator al;
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    FailCtx fc = {0, 0, 0, 0};
    al.userdata = &fc;
    al.alloc = fc_alloc; al.realloc = fc_realloc; al.free = fc_free;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    fc.armed = 1;
    fc.fail_on = 2;                              /* fail curr/2nd replacement buffer */
    fc.fail_on2 = 3;                             /* fail the old rollback path too */

    TIMUI_CHECK(timui_ui_resize(ui, 40, 12) == TIMUI_ERR_OUT_OF_MEMORY);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_width(f) == 30 && timui_height(f) == 10);
    TIMUI_CHECK(timui_frame_buffer(f)->w == 30);
    timui_end(f);                                /* swaps prev/curr */

    timui_begin(ui, &f);
    TIMUI_CHECK(timui_width(f) == 30 && timui_height(f) == 10);
    TIMUI_CHECK(timui_frame_buffer(f)->w == 30);
    timui_end(f);

    timui_close(ui);
    timui_fake_destroy(&fake);
}
