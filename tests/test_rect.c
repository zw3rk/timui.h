/*
 * test_rect.c — rect layout + id/string leaf helpers (first TDD slice).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>

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

TIMUI_TEST(test_rect_cut_extreme_origin_safe){
    TimuiRect r;
    TimuiRect out;

    r = TIMUI_RECT(INT_MAX - 2, 0, 10, 1);
    out = timui_cut_right(&r, 5);
    TIMUI_CHECK(out.x == INT_MAX && out.w == 5);
    TIMUI_CHECK(r.x == INT_MAX - 2 && r.w == 5);

    r = TIMUI_RECT(0, INT_MAX - 2, 1, 10);
    out = timui_cut_bottom(&r, 5);
    TIMUI_CHECK(out.y == INT_MAX && out.h == 5);
    TIMUI_CHECK(r.y == INT_MAX - 2 && r.h == 5);
}

TIMUI_TEST(test_rect_cut_mutated_origin_safe){
    TimuiRect r;
    TimuiRect out;

    r = TIMUI_RECT(INT_MAX - 2, 0, 10, 1);
    out = timui_cut_left(&r, 5);
    TIMUI_CHECK(out.x == INT_MAX - 2 && out.w == 5);
    TIMUI_CHECK(r.x == INT_MAX && r.w == 5);

    r = TIMUI_RECT(0, INT_MAX - 2, 1, 10);
    out = timui_cut_top(&r, 5);
    TIMUI_CHECK(out.y == INT_MAX - 2 && out.h == 5);
    TIMUI_CHECK(r.y == INT_MAX && r.h == 5);
}

TIMUI_TEST(test_rect_split){
    TimuiRect a, b;
    timui_split_cols(TIMUI_RECT(0, 0, 100, 10), 0.25f, &a, &b);

    TIMUI_CHECK(a.w == 25 && b.w == 75);
    TIMUI_CHECK(a.x == 0 && b.x == 25 && a.h == 10 && b.h == 10);
}

TIMUI_TEST(test_rect_split_extreme_origin_safe){
    TimuiRect a, b;
    timui_split_cols(TIMUI_RECT(INT_MAX - 2, 7, 10, 4), 0.5f, &a, &b);
    TIMUI_CHECK(a.x == INT_MAX - 2 && a.w == 5);
    TIMUI_CHECK(b.x == INT_MAX && b.w == 5);

    timui_split_rows(TIMUI_RECT(3, INT_MAX - 2, 10, 4), 0.5f, &a, &b);
    TIMUI_CHECK(a.y == INT_MAX - 2 && a.h == 2);
    TIMUI_CHECK(b.y == INT_MAX && b.h == 2);
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

TIMUI_TEST(test_rect_inset_pad_extreme_safe){
    TimuiRect r;
    r = timui_inset(TIMUI_RECT(INT_MAX - 2, INT_MAX - 2, INT_MAX, INT_MAX), INT_MAX);
    TIMUI_CHECK(r.x == INT_MAX && r.y == INT_MAX && r.w == 0 && r.h == 0);

    r = timui_pad(TIMUI_RECT(INT_MAX - 2, INT_MAX - 2, INT_MAX, INT_MAX),
                  INT_MAX, INT_MAX, INT_MAX, INT_MAX);
    TIMUI_CHECK(r.x == INT_MAX && r.y == INT_MAX && r.w == 0 && r.h == 0);
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
