/*
 * test_sqlite_table.c — standalone unit tests for examples/sqlite_table.h, the
 * PURE table-layout helpers used by the SQLite TUI example (examples/sqlite_tui.c).
 *
 * These test ONLY the deterministic, hand-computable units — NOT SQLite:
 *   (a) column-width fitting  : sqltbl_disp_width / sqltbl_col_width / sqltbl_fit_cell
 *       (per-column width from cell widths + a cap, and ellipsis truncation that
 *        never splits a wide glyph).
 *   (b) table paging / scroll : sqltbl_page (visible slice, clamped both ends) and
 *       sqltbl_scroll_to (minimal offset that keeps a selected row visible).
 *
 * Builds timui as a single TU (for timui_utf8_width/decode) plus the header, and
 * drives the helpers with its own main() + CHECK macro (no tests/test.h, no
 * SQLite). Positive tests assert exact values; adversarial tests assert bounds,
 * no crash, and no overrun on empty / oversized / degenerate inputs.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include "sqlite_table.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_fail;                                                       \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                   \
    } while (0)

typedef void (*test_fn)(void);
static void run(const char *name, test_fn fn)
{
    int before = g_fail;
    fn();
    printf("%s %s\n", (g_fail == before) ? "PASS" : "FAIL", name);
}

/* Assert a fitted cell: return value (cols), the *ellipsis flag, and the exact
 * bytes written into out. */
#define CHECK_FIT(s, width, exp_cols, exp_ellip, exp_str)                   \
    do {                                                                    \
        char _out[64]; int _e = -1;                                        \
        int _c = sqltbl_fit_cell((s), (width), _out, sizeof _out, &_e);    \
        CHECK(_c == (exp_cols));                                            \
        CHECK(_e == (exp_ellip));                                           \
        CHECK(strcmp(_out, (exp_str)) == 0);                               \
    } while (0)

/* Some UTF-8 test strings. */
#define CJK_ZHONG "\xE4\xB8\xAD"   /* 中 (width 2) */
#define CJK_WEN   "\xE6\x96\x87"   /* 文 (width 2) */
#define WAVE      "\xF0\x9F\x91\x8B" /* 👋 (width 2) */
#define ELL       "\xE2\x80\xA6"   /* … (width 1) */

/* ----------------------------------------------------------------------- */
/* (a) column-width fitting — display width                                  */
/* ----------------------------------------------------------------------- */

static void test_disp_width(void)
{
    CHECK(sqltbl_disp_width("abc") == 3);
    CHECK(sqltbl_disp_width("") == 0);
    CHECK(sqltbl_disp_width(CJK_ZHONG CJK_WEN) == 4);   /* 中文 = 2+2 */
    CHECK(sqltbl_disp_width(WAVE) == 2);                /* emoji = 2 */
    CHECK(sqltbl_disp_width(CJK_ZHONG "x") == 3);       /* 中 + x */
    CHECK(sqltbl_disp_width(NULL) == 0);                /* adversarial: NULL */
}

/* ----------------------------------------------------------------------- */
/* (a) column-width fitting — per-column width                               */
/* ----------------------------------------------------------------------- */

static void test_col_width(void)
{
    /* max cell = 10, but capped at 8 */
    { int w[] = { 3, 10, 5 }; CHECK(sqltbl_col_width(w, 3, 8, 3) == 8); }
    /* max cell = 5, under cap -> 5 */
    { int w[] = { 3, 4, 5 };  CHECK(sqltbl_col_width(w, 3, 20, 3) == 5); }
    /* max cell = 2, below the minimum -> minw */
    { int w[] = { 1, 2 };     CHECK(sqltbl_col_width(w, 2, 20, 4) == 4); }
    /* no cells -> minw */
    CHECK(sqltbl_col_width(NULL, 0, 20, 3) == 3);
    /* adversarial: cap below minw -> minw wins (never below the floor) */
    { int w[] = { 10 };       CHECK(sqltbl_col_width(w, 1, 2, 5) == 5); }
    /* adversarial: negative widths ignored, floor still applies */
    { int w[] = { -3, 1 };    CHECK(sqltbl_col_width(w, 2, 20, 3) == 3); }
}

/* ----------------------------------------------------------------------- */
/* (a) column-width fitting — ellipsis truncation                            */
/* ----------------------------------------------------------------------- */

static void test_fit_cell(void)
{
    /* fits exactly / with room to spare -> copied whole, no ellipsis */
    CHECK_FIT("abc", 5, 3, 0, "abc");
    CHECK_FIT("abc", 3, 3, 0, "abc");
    CHECK_FIT("", 5, 0, 0, "");

    /* truncation: keep width-1 columns of content, append "…" */
    CHECK_FIT("abcdef", 4, 4, 1, "abc" ELL);
    CHECK_FIT("abcdef", 1, 1, 1, ELL);        /* width 1 -> just the ellipsis */

    /* wide glyphs are never split across the truncation boundary */
    CHECK_FIT(CJK_ZHONG CJK_WEN "X", 3, 3, 1, CJK_ZHONG ELL); /* 中文X @3 -> 中… */
    /* budget leaves a wide glyph unable to fit -> ellipsis lands early (cols<width) */
    CHECK_FIT(CJK_ZHONG CJK_WEN "X", 4, 3, 1, CJK_ZHONG ELL); /* 中(2)+… = 3, 4th padded */
    /* wide pair fits exactly, no ellipsis */
    CHECK_FIT(CJK_ZHONG CJK_WEN, 4, 4, 0, CJK_ZHONG CJK_WEN);

    /* adversarial: width 0 / negative -> empty, ellipsis flagged if content lost */
    CHECK_FIT("abc", 0, 0, 1, "");
    CHECK_FIT("", 0, 0, 0, "");
}

/* ----------------------------------------------------------------------- */
/* (b) paging / scroll — visible slice                                       */
/* ----------------------------------------------------------------------- */

static void test_page(void)
{
    SqlSlice s;

    s = sqltbl_page(100, 10, 0);  CHECK(s.first == 0  && s.count == 10);
    s = sqltbl_page(100, 10, 5);  CHECK(s.first == 5  && s.count == 10);
    /* clamp at the bottom: last full page starts at total-viewport */
    s = sqltbl_page(100, 10, 95); CHECK(s.first == 90 && s.count == 10);
    s = sqltbl_page(100, 10, 200);CHECK(s.first == 90 && s.count == 10);
    /* viewport larger than total -> show everything from row 0 */
    s = sqltbl_page(5, 10, 0);    CHECK(s.first == 0  && s.count == 5);
    s = sqltbl_page(5, 10, 3);    CHECK(s.first == 0  && s.count == 5);

    /* adversarial: empty table, zero viewport, negative offset */
    s = sqltbl_page(0, 10, 0);    CHECK(s.first == 0  && s.count == 0);
    s = sqltbl_page(100, 0, 5);   CHECK(s.count == 0);
    s = sqltbl_page(100, 10, -5); CHECK(s.first == 0  && s.count == 10);
}

/* ----------------------------------------------------------------------- */
/* (b) paging / scroll — keep the selection visible                          */
/* ----------------------------------------------------------------------- */

static void test_scroll_to(void)
{
    /* already visible -> unchanged */
    CHECK(sqltbl_scroll_to(0, 0, 10, 100) == 0);
    CHECK(sqltbl_scroll_to(5, 5, 10, 100) == 5);
    CHECK(sqltbl_scroll_to(14, 5, 10, 100) == 5);   /* 14 in [5,15) */

    /* selection below the viewport -> scroll down just enough */
    CHECK(sqltbl_scroll_to(15, 0, 10, 100) == 6);   /* 15-10+1 */
    /* selection above the viewport -> scroll up to it */
    CHECK(sqltbl_scroll_to(3, 6, 10, 100) == 3);

    /* adversarial: never goes negative; zero viewport is a no-op */
    CHECK(sqltbl_scroll_to(0, 0, 10, 1) == 0);
    CHECK(sqltbl_scroll_to(2, 5, 0, 100) == 5);
}

int main(void)
{
    run("disp_width", test_disp_width);
    run("col_width",  test_col_width);
    run("fit_cell",   test_fit_cell);
    run("page",       test_page);
    run("scroll_to",  test_scroll_to);

    if (g_fail != 0) {
        printf("\n%d/%d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d checks passed\n", g_checks);
    return 0;
}
