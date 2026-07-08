/*
 * test_rect.c — rect layout + id/string leaf helpers (first TDD slice).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

TIMUI_TEST(test_rect_cuts){
    TimuiRect r   = TIMUI_RECT(0, 0, 10, 10);
    TimuiRect top = timui_cut_top(&r, 2);

    TIMUI_CHECK(top.x == 0 && top.y == 0 && top.w == 10 && top.h == 2);
    TIMUI_CHECK(r.x == 0 && r.y == 2 && r.w == 10 && r.h == 8);

    /* Over-cut clamps to the remaining height; never goes negative. */
    TimuiRect bot = timui_cut_bottom(&r, 100);
    TIMUI_CHECK(bot.h == 8);
    TIMUI_CHECK(r.h == 0);
}

TIMUI_TEST(test_rect_cut_null_safe){
    TimuiRect r;
    r = timui_cut_top(NULL, 1);
    TIMUI_CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
    r = timui_cut_bottom(NULL, 1);
    TIMUI_CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
    r = timui_cut_left(NULL, 1);
    TIMUI_CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
    r = timui_cut_right(NULL, 1);
    TIMUI_CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
}

TIMUI_TEST(test_rect_split){
    TimuiRect a, b;
    timui_split_cols(TIMUI_RECT(0, 0, 100, 10), 0.25f, &a, &b);

    TIMUI_CHECK(a.w == 25 && b.w == 75);
    TIMUI_CHECK(a.x == 0 && b.x == 25 && a.h == 10 && b.h == 10);
}

TIMUI_TEST(test_rect_inset_clamp){
    TimuiRect r = timui_inset(TIMUI_RECT(0, 0, 10, 10), 2);
    TIMUI_CHECK(r.x == 2 && r.y == 2 && r.w == 6 && r.h == 6);

    /* Over-inset clamps to zero, never negative. */
    TimuiRect z = timui_inset(TIMUI_RECT(0, 0, 3, 3), 10);
    TIMUI_CHECK(z.w == 0 && z.h == 0);

    /* Negative requested size is treated as zero. */
    TimuiRect neg = timui_cut_top(&(TimuiRect){0, 0, 5, 5}, -3);
    TIMUI_CHECK(neg.h == 0);
}

TIMUI_TEST(test_ids_stable){
    TimuiId a = timui_id_from_cstr("save");
    TimuiId b = timui_id_from_cstr("save");
    TimuiId c = timui_id_from_cstr("load");

    TIMUI_CHECK(a == b);
    TIMUI_CHECK(a != c);
    TIMUI_CHECK(timui_id_from_cstr(NULL) == 0);

    TIMUI_CHECK(timui_str_eq(TIMUI_STR_LIT("hi"), TIMUI_STR_LIT("hi")));
    TIMUI_CHECK(!timui_str_eq(TIMUI_STR_LIT("hi"), TIMUI_STR_LIT("ho")));
}
