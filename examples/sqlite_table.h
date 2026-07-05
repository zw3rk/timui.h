/* sqlite_table.h — PURE table-layout helpers for the timui.h SQLite TUI example
 * (examples/sqlite_tui.c). Header-only, pure C99, no allocation, no globals, no
 * I/O. Split out from the app so the deterministic, hand-computable units are
 * unit-tested directly (tests/test_sqlite_table.c).
 *
 * Two families, mirroring the two things a scrollable results grid must decide:
 *   - column-width fitting : sqltbl_disp_width (display columns of a UTF-8
 *     string), sqltbl_col_width (a column's width = the widest cell, floored at a
 *     minimum and capped at a maximum), and sqltbl_fit_cell (truncate a cell to a
 *     column width with a single-column "…" ellipsis, never splitting a wide
 *     glyph).
 *   - paging / scroll : sqltbl_page (the visible [first,count) row slice for a
 *     viewport + scroll offset, clamped at both ends) and sqltbl_scroll_to (the
 *     minimal offset that keeps a selected row inside the viewport).
 *
 * Depends on timui.h for timui_utf8_decode / timui_utf8_width, so — like
 * chat_text.h — timui.h MUST be included first (the app and the test build timui
 * as one TU with TIMUI_IMPLEMENTATION; a second include would re-emit it).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */
#ifndef SQLITE_TABLE_H
#define SQLITE_TABLE_H

#ifndef TIMUI_H
#error "include timui.h before sqlite_table.h"
#endif
#include <stddef.h>
#include <string.h>
#include <stdint.h>

/* The truncation marker: U+2026 HORIZONTAL ELLIPSIS — one display column. */
#define SQLTBL_ELLIPSIS "\xE2\x80\xA6"

/* A visible row range: rows [first, first+count) of the result set. */
typedef struct { int first; int count; } SqlSlice;

/* ----------------------------------------------------------------------- */
/* Column-width fitting.                                                      */
/* ----------------------------------------------------------------------- */

/* Display width (terminal columns) of a UTF-8 string: sums timui_utf8_width over
 * each code point (CJK/emoji count 2, control/combining 0). NULL -> 0. */
static int sqltbl_disp_width(const char *s)
{
    size_t i, len;
    int w = 0;
    if (s == NULL) return 0;
    len = strlen(s);
    for (i = 0; i < len;) {
        uint32_t cp;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        if (adv <= 0) adv = 1;                 /* never stall on a bad byte */
        w += timui_utf8_width(cp);
        i += (size_t)adv;
    }
    return w;
}

/* A column's fitted width from the display widths of its cells (header + data):
 * the widest cell, but floored at `minw` (so a short column still has room for an
 * ellipsis) and capped at `maxw`. `minw` is a hard floor — it wins even when
 * `maxw < minw`. Negative cell widths are ignored. n == 0 (or cells == NULL)
 * yields `minw`. */
static int sqltbl_col_width(const int *cellw, int n, int maxw, int minw)
{
    int i, m = 0, w;
    if (cellw != NULL)
        for (i = 0; i < n; i++)
            if (cellw[i] > m) m = cellw[i];
    w = m;
    if (w > maxw) w = maxw;                     /* cap */
    if (w < minw) w = minw;                     /* floor (also handles maxw<minw) */
    return w;
}

/* Truncate `s` to at most `width` display columns, writing a NUL-terminated
 * result into `out` (cap bytes). Returns the number of columns actually used
 * (<= width). Sets *ellipsis to 1 if the string was truncated (a trailing "…"
 * was appended), else 0.
 *
 * If `s` already fits, it is copied whole. Otherwise a prefix that fits in
 * (width - 1) columns is copied and the ellipsis occupies the final column — a
 * wide (2-column) glyph is never split, so when one straddles the budget the
 * ellipsis lands early and the returned column count is < width (the caller pads
 * the slack). width <= 0 yields an empty string (ellipsis flagged iff content
 * was dropped). */
static int sqltbl_fit_cell(const char *s, int width, char *out, size_t cap,
                           int *ellipsis)
{
    size_t i, len, o = 0;
    int used = 0, full;
    int budget;

    if (ellipsis) *ellipsis = 0;
    if (out == NULL || cap == 0) return 0;
    out[0] = '\0';
    if (s == NULL) s = "";
    len = strlen(s);
    full = sqltbl_disp_width(s);

    if (width <= 0) {
        if (ellipsis) *ellipsis = (full > 0);
        return 0;
    }
    if (full <= width) {                        /* fits whole — copy verbatim */
        size_t n = len < cap - 1 ? len : cap - 1;
        memcpy(out, s, n);
        out[n] = '\0';
        return full;
    }

    /* Truncate: reserve the last column for the ellipsis. */
    budget = width - 1;
    for (i = 0; i < len;) {
        uint32_t cp;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        int gw;
        if (adv <= 0) adv = 1;
        gw = timui_utf8_width(cp);
        if (used + gw > budget) break;          /* next glyph would overflow */
        if (o + (size_t)adv >= cap - 4) break;  /* keep room for "…" + NUL */
        memcpy(out + o, s + i, (size_t)adv);
        o += (size_t)adv;
        used += gw;
        i += (size_t)adv;
    }
    /* Append the single-column ellipsis. */
    if (o + 3 < cap) { memcpy(out + o, SQLTBL_ELLIPSIS, 3); o += 3; }
    out[o] = '\0';
    if (ellipsis) *ellipsis = 1;
    return used + 1;                            /* content columns + ellipsis */
}

/* ----------------------------------------------------------------------- */
/* Paging / scroll.                                                          */
/* ----------------------------------------------------------------------- */

/* The visible row slice for a viewport of `viewport` rows over `total` rows,
 * scrolled to `offset`. The offset is clamped to [0, max(0, total-viewport)] so
 * the view never scrolls past either end, and the returned count fills the
 * viewport whenever enough rows remain. Degenerate inputs (total<=0,
 * viewport<=0) yield a zero-length slice. */
static SqlSlice sqltbl_page(int total, int viewport, int offset)
{
    SqlSlice s;
    int maxoff;
    s.first = 0;
    s.count = 0;
    if (total <= 0) return s;
    maxoff = total - viewport;
    if (maxoff < 0) maxoff = 0;
    if (offset < 0) offset = 0;
    if (offset > maxoff) offset = maxoff;
    s.first = offset;
    if (viewport <= 0) { s.count = 0; return s; }
    s.count = total - offset;
    if (s.count > viewport) s.count = viewport;
    return s;
}

/* The minimal scroll offset that keeps row `sel` visible inside a `viewport`-row
 * window currently at `offset`: scroll up to `sel` if it is above the window,
 * down just enough if it is below, else leave `offset` unchanged. The result is
 * clamped to >= 0. `total` bounds the sensible range (unused beyond the clamp,
 * but kept in the signature so the app can pass it). A non-positive viewport is a
 * no-op. */
static int sqltbl_scroll_to(int sel, int offset, int viewport, int total)
{
    (void)total;
    if (viewport <= 0) return offset;
    if (sel < offset) offset = sel;
    else if (sel >= offset + viewport) offset = sel - viewport + 1;
    if (offset < 0) offset = 0;
    return offset;
}

#endif /* SQLITE_TABLE_H */
