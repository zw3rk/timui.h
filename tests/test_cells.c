/*
 * test_cells.c — cell buffer (T3.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

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
