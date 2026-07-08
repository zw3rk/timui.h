/*
 * test_grid.c — standalone unit tests for the PURE data-grid math that backs the
 * enhanced table (timui_table_ex) and scrollable tree (timui_tree_scroll):
 *
 *   (a) column-width fitting  : timui_display_width / timui_col_fit_width /
 *       timui_fit_cell — per-column width from cell display widths + a cap, and
 *       ellipsis truncation that never splits a wide glyph.
 *   (b) paging / scroll        : timui_page_slice (visible [first,count) slice,
 *       clamped both ends — reused for VERTICAL rows AND HORIZONTAL cells) and
 *       timui_scroll_to (minimal offset that keeps a selection visible).
 *   (c) tree flatten           : timui_tree_flatten (DFS node list + expanded
 *       flags -> the indices of the VISIBLE nodes; a collapsed node hides its
 *       whole subtree).
 *
 * These test ONLY the deterministic, hand-computable units — no TUI, no frame.
 * The math is LIFTED from examples/sqlite_table.h into the library so both the
 * widgets and this test drive the exact same production code path.
 *
 * Builds timui as a single TU (for the TIMUI_API helpers), and drives them with
 * its own main() + CHECK macro. Positive tests assert exact values; adversarial
 * tests assert bounds, no crash, and no overrun on empty/oversized/degenerate
 * input.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

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
        int _c = timui_fit_cell((s), (width), _out, sizeof _out, &_e);      \
        CHECK(_c == (exp_cols));                                            \
        CHECK(_e == (exp_ellip));                                           \
        CHECK(strcmp(_out, (exp_str)) == 0);                               \
    } while (0)

/* Some UTF-8 test strings. */
#define CJK_ZHONG "\xE4\xB8\xAD"     /* 中 (width 2) */
#define CJK_WEN   "\xE6\x96\x87"     /* 文 (width 2) */
#define WAVE      "\xF0\x9F\x91\x8B" /* 👋 (width 2) */
#define ELL       "\xE2\x80\xA6"     /* … (width 1) */
#define ACUTE     "\xCC\x81"         /* U+0301 combining acute accent */
#define SKIN_MED  "\xF0\x9F\x8F\xBD" /* U+1F3FD skin tone modifier */
#define RI_US     "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8" /* 🇺🇸 */
#define HEART_VS  "\xE2\x9D\xA4\xEF\xB8\x8F"         /* ❤️ */
#define FAMILY_ZWJ "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D" \
                   "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6"            /* family */

/* ----------------------------------------------------------------------- */
/* (a) column-width fitting — display width                                  */
/* ----------------------------------------------------------------------- */

static void test_display_width(void)
{
    CHECK(timui_display_width("abc") == 3);
    CHECK(timui_display_width("") == 0);
    CHECK(timui_display_width(CJK_ZHONG CJK_WEN) == 4);   /* 中文 = 2+2 */
    CHECK(timui_display_width(WAVE) == 2);                /* emoji = 2 */
    CHECK(timui_display_width(CJK_ZHONG "x") == 3);       /* 中 + x */
    CHECK(timui_display_width("e" ACUTE) == 1);           /* combining cluster */
    CHECK(timui_display_width(WAVE SKIN_MED) == 2);        /* emoji + skin tone */
    CHECK(timui_display_width(RI_US) == 2);                /* regional-indicator pair */
    CHECK(timui_display_width(HEART_VS) == 2);             /* VS16 emoji presentation */
    CHECK(timui_display_width(FAMILY_ZWJ) == 2);           /* ZWJ emoji sequence */
    CHECK(timui_display_width("\r\nx") == 1);             /* CRLF is one zero-width cluster */
    CHECK(timui_display_width(NULL) == 0);                /* adversarial: NULL */
}

/* ----------------------------------------------------------------------- */
/* (a) column-width fitting — per-column width                               */
/* ----------------------------------------------------------------------- */

static void test_col_fit_width(void)
{
    /* max cell = 10, but capped at 8 */
    { int w[] = { 3, 10, 5 }; CHECK(timui_col_fit_width(w, 3, 8, 3) == 8); }
    /* max cell = 5, under cap -> 5 */
    { int w[] = { 3, 4, 5 };  CHECK(timui_col_fit_width(w, 3, 20, 3) == 5); }
    /* max cell = 2, below the minimum -> minw */
    { int w[] = { 1, 2 };     CHECK(timui_col_fit_width(w, 2, 20, 4) == 4); }
    /* no cells -> minw */
    CHECK(timui_col_fit_width(NULL, 0, 20, 3) == 3);
    /* adversarial: cap below minw -> minw wins (never below the floor) */
    { int w[] = { 10 };       CHECK(timui_col_fit_width(w, 1, 2, 5) == 5); }
    /* adversarial: negative widths ignored, floor still applies */
    { int w[] = { -3, 1 };    CHECK(timui_col_fit_width(w, 2, 20, 3) == 3); }
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

    /* grapheme clusters are never split across the truncation boundary */
    CHECK_FIT("e" ACUTE "fg", 2, 2, 1, "e" ACUTE ELL);
    CHECK_FIT(WAVE SKIN_MED "x", 2, 1, 1, ELL);
    CHECK_FIT(WAVE SKIN_MED "x", 3, 3, 0, WAVE SKIN_MED "x");
    CHECK_FIT(RI_US "x", 2, 1, 1, ELL);
    CHECK_FIT(RI_US "x", 3, 3, 0, RI_US "x");
    CHECK_FIT(FAMILY_ZWJ "x", 2, 1, 1, ELL);
    CHECK_FIT(HEART_VS "x", 2, 1, 1, ELL);

    /* adversarial: width 0 / negative -> empty, ellipsis flagged if content lost */
    CHECK_FIT("abc", 0, 0, 1, "");
    CHECK_FIT("", 0, 0, 0, "");
    /* adversarial: NULL string -> empty, no ellipsis */
    CHECK_FIT(NULL, 5, 0, 0, "");

    /* adversarial: tiny output buffers must remain NUL-terminated in bounds.
     * In the truncating path, cap<4 used to underflow the ellipsis-room check
     * and write the terminator past out[0]. */
    { char out[2] = { 'X', 'G' }; int e = 0;
      int c = timui_fit_cell("abcdef", 2, out, 1, &e);
      CHECK(c == 1);
      CHECK(e == 1);
      CHECK(out[0] == '\0');
      CHECK(out[1] == 'G'); }
    { char out[3] = { 'X', 'Y', 'G' }; int e = 0;
      int c = timui_fit_cell("abcdef", 2, out, 2, &e);
      CHECK(c == 1);
      CHECK(e == 1);
      CHECK(out[0] == '\0' && out[1] == 'Y');
      CHECK(out[2] == 'G'); }
}

/* ----------------------------------------------------------------------- */
/* (b) paging / scroll — visible slice (rows AND cells share this)           */
/* ----------------------------------------------------------------------- */

static void test_page_slice(void)
{
    TimuiSlice s;

    /* --- vertical: N rows, viewport height, offset --- */
    s = timui_page_slice(100, 10, 0);   CHECK(s.first == 0  && s.count == 10);
    s = timui_page_slice(100, 10, 5);   CHECK(s.first == 5  && s.count == 10);
    /* clamp at the bottom: last full page starts at total-viewport */
    s = timui_page_slice(100, 10, 95);  CHECK(s.first == 90 && s.count == 10);
    s = timui_page_slice(100, 10, 200); CHECK(s.first == 90 && s.count == 10);
    /* viewport larger than total -> show everything from row 0 */
    s = timui_page_slice(5, 10, 0);     CHECK(s.first == 0  && s.count == 5);
    s = timui_page_slice(5, 10, 3);     CHECK(s.first == 0  && s.count == 5);

    /* --- horizontal: exactly the same math over cell columns --- */
    /* a 50-cell-wide grid, 20-cell viewport, scrolled way past the right edge
     * clamps to first=30 (50-20), count 20 cells visible. */
    s = timui_page_slice(50, 20, 100);  CHECK(s.first == 30 && s.count == 20);
    s = timui_page_slice(50, 20, -3);   CHECK(s.first == 0  && s.count == 20);
    s = timui_page_slice(12, 40, 0);    CHECK(s.first == 0  && s.count == 12);

    /* adversarial: empty, zero viewport, negative offset */
    s = timui_page_slice(0, 10, 0);     CHECK(s.first == 0  && s.count == 0);
    s = timui_page_slice(100, 0, 5);    CHECK(s.count == 0);
    s = timui_page_slice(100, 10, -5);  CHECK(s.first == 0  && s.count == 10);
}

/* ----------------------------------------------------------------------- */
/* (b) paging / scroll — keep the selection visible                          */
/* ----------------------------------------------------------------------- */

static void test_scroll_to(void)
{
    /* already visible -> unchanged */
    CHECK(timui_scroll_to(0, 0, 10, 100) == 0);
    CHECK(timui_scroll_to(5, 5, 10, 100) == 5);
    CHECK(timui_scroll_to(14, 5, 10, 100) == 5);   /* 14 in [5,15) */

    /* selection below the viewport -> scroll down just enough */
    CHECK(timui_scroll_to(15, 0, 10, 100) == 6);   /* 15-10+1 */
    /* selection above the viewport -> scroll up to it */
    CHECK(timui_scroll_to(3, 6, 10, 100) == 3);

    /* adversarial: never goes negative; zero viewport is a no-op */
    CHECK(timui_scroll_to(0, 0, 10, 1) == 0);
    CHECK(timui_scroll_to(2, 5, 0, 100) == 5);
}

/* ----------------------------------------------------------------------- */
/* (c) tree flatten — visible node indices                                   */
/* ----------------------------------------------------------------------- */

/* Assert the flattened visible index list matches want[0..nwant). */
static void check_flat(const TimuiTreeNode *nodes, int count,
                       const int *want, int nwant)
{
    int out[16];
    int n = timui_tree_flatten(nodes, count, out, 16);
    int i, ok = (n == nwant);
    for (i = 0; ok && i < nwant; i++) ok = (out[i] == want[i]);
    CHECK(ok);
    if (!ok) {
        printf("      flatten -> %d [", n);
        for (i = 0; i < n && i < 16; i++) printf("%s%d", i ? "," : "", out[i]);
        printf("], wanted %d [", nwant);
        for (i = 0; i < nwant; i++) printf("%s%d", i ? "," : "", want[i]);
        printf("]\n");
    }
}

static void test_tree_flatten(void)
{
    /* all leaves at depth 0 -> all visible */
    { TimuiTreeNode ns[] = {
          {0, "a", 0, 0}, {0, "b", 0, 0}, {0, "c", 0, 0} };
      int want[] = { 0, 1, 2 }; check_flat(ns, 3, want, 3); }

    /* an EXPANDED parent shows its children */
    { TimuiTreeNode ns[] = {
          {0, "root", 1, 1}, {1, "childA", 0, 0}, {1, "childB", 0, 0} };
      int want[] = { 0, 1, 2 }; check_flat(ns, 3, want, 3); }

    /* a COLLAPSED parent hides its whole subtree; a same-depth sibling reappears */
    { TimuiTreeNode ns[] = {
          {0, "root", 1, 0}, {1, "childA", 0, 0}, {1, "childB", 0, 0},
          {0, "sib",  0, 0} };
      int want[] = { 0, 3 }; check_flat(ns, 4, want, 2); }

    /* NESTED: expanded root, collapsed A hides its deeper kids, B1 (back at
     * depth 1) reappears */
    { TimuiTreeNode ns[] = {
          {0, "root", 1, 1},   /* 0 visible                */
          {1, "A",    1, 0},   /* 1 visible, collapsed      */
          {2, "A1a",  0, 0},   /* 2 hidden (2 > 1)          */
          {2, "A1b",  0, 0},   /* 3 hidden                  */
          {1, "B1",   0, 0} }; /* 4 visible (1 <= hidden 1) */
      int want[] = { 0, 1, 4 }; check_flat(ns, 5, want, 3); }

    /* cap: real count returned even when out[] can't hold it all */
    { TimuiTreeNode ns[] = {
          {0, "a", 0, 0}, {0, "b", 0, 0}, {0, "c", 0, 0} };
      int out[2]; int n = timui_tree_flatten(ns, 3, out, 2);
      CHECK(n == 3); CHECK(out[0] == 0); CHECK(out[1] == 1); }

    /* adversarial: empty / NULL -> zero, no crash */
    { int out[4]; CHECK(timui_tree_flatten(NULL, 3, out, 4) == 0);
      CHECK(timui_tree_flatten((TimuiTreeNode *)0, 0, out, 4) == 0); }
}

int main(void)
{
    run("display_width", test_display_width);
    run("col_fit_width", test_col_fit_width);
    run("fit_cell",      test_fit_cell);
    run("page_slice",    test_page_slice);
    run("scroll_to",     test_scroll_to);
    run("tree_flatten",  test_tree_flatten);

    if (g_fail != 0) {
        printf("\n%d/%d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d checks passed\n", g_checks);
    return 0;
}
