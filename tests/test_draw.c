/*
 * test_draw.c — drawing primitives into the cell buffer (T3.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_draw_text){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);
    TimuiCell *g;
    timui_cells_init(&b, 20, 5, &al);
    timui_draw_text(&b, 2, 1, TIMUI_STR_LIT("Hi"), s);
    g = timui_cells_get(&b, 2, 1);
    TIMUI_CHECK(g && g->codepoint == 'H' && g->fg == 0xffffff);
    g = timui_cells_get(&b, 3, 1);
    TIMUI_CHECK(g && g->codepoint == 'i');
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_fill){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, 0x0000ff, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_fill(&b, TIMUI_RECT(1, 1, 4, 3), s);
    TIMUI_CHECK(timui_cells_get(&b, 1, 1)->bg == 0x0000ff);
    TIMUI_CHECK(timui_cells_get(&b, 4, 3)->bg == 0x0000ff);   /* inclusive corner */
    TIMUI_CHECK(timui_cells_get(&b, 5, 3)->codepoint == 0);   /* outside the fill */
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_box_single){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_box(&b, TIMUI_RECT(0, 0, 4, 3), TIMUI_BORDER_SINGLE, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0x250C);  /* TL */
    TIMUI_CHECK(timui_cells_get(&b, 3, 0)->codepoint == 0x2510);  /* TR */
    TIMUI_CHECK(timui_cells_get(&b, 0, 2)->codepoint == 0x2514);  /* BL */
    TIMUI_CHECK(timui_cells_get(&b, 3, 2)->codepoint == 0x2518);  /* BR */
    TIMUI_CHECK(timui_cells_get(&b, 1, 0)->codepoint == 0x2500);  /* top edge */
    TIMUI_CHECK(timui_cells_get(&b, 0, 1)->codepoint == 0x2502);  /* left edge */
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_box_ascii){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_box(&b, TIMUI_RECT(0, 0, 3, 3), TIMUI_BORDER_ASCII, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == '+');
    TIMUI_CHECK(timui_cells_get(&b, 1, 0)->codepoint == '-');
    TIMUI_CHECK(timui_cells_get(&b, 0, 1)->codepoint == '|');
    timui_cells_destroy(&b);
}
