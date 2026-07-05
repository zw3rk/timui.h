/*
 * sqlite_tui.c — a terminal SQLite browser built on timui.h.
 *
 *   sqlite_tui <db> [--query "<sql>"] [--frames N | --exit-after] [--headless]
 *
 * Three panes:
 *   LEFT   a SCHEMA TREE — tables/views → their columns (from sqlite_master +
 *          PRAGMA table_info), expand/collapse with →/←/Enter.
 *   MAIN   a RESULTS TABLE — column headers, per-column widths fit to content
 *          (capped + ellipsised on overflow), horizontal + vertical scroll, and
 *          row selection/highlight.
 *   BOTTOM a QUERY EDITOR — the chat multi-line composer pattern (Enter runs,
 *          Shift+Enter inserts a newline) with SQL syntax highlighting via
 *          examples/chat_highlight.h. A status line reports the row count and
 *          query time, or the SQLite error message.
 *
 * Tab cycles focus (tree → results → editor); arrows navigate; Enter runs.
 *
 * The PURE layout units (column fitting, paging/scroll) live in
 * examples/sqlite_table.h and are unit-tested in tests/test_sqlite_table.c.
 * SQLite itself is the vendored public-domain amalgamation (tools/vendor/sqlite3)
 * and is NOT unit-tested here.
 *
 * Headless smoke: --frames N (or --exit-after) opens the db, optionally runs a
 * --query, and renders N frames through a fake transport (no tty needed), then
 * dumps the final frame's grid to stderr. Deterministic and CI-able.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include "sqlite3.h"          /* vendored amalgamation (tools/vendor/sqlite3) */
#include "sqlite_table.h"     /* pure column-fit + paging/scroll helpers */
#include "chat_highlight.h"   /* SQL syntax highlighter (the "sql" lexer) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- Tunables ---------------------------------------------------------- */
#define TREE_W        30      /* schema-tree pane width (columns) */
#define COL_MAX_W     40      /* per-column display cap in the results grid */
#define COL_MIN_W      3      /* per-column floor (room for the ellipsis) */
#define RS_MAXCOLS    64      /* max columns rendered from a result set */
#define RS_MAXROWS  5000      /* max rows collected (older rows past this drop) */
#define CELL_STORE   1024     /* max bytes stored per cell (display truncates) */
#define QUERY_MAX   4096      /* editor / query buffer size */
#define MAX_TABLES   512      /* schema-tree table capacity */
#define MAX_TCOLS    128      /* columns captured per table */

/* Focus panes, cycled by Tab. */
enum { PANE_TREE = 0, PANE_RESULTS, PANE_EDITOR, PANE_COUNT };

/* ---- Result set (one query's output) ----------------------------------- *
 * Cells are row-major char* (row*ncols + col), each an owned copy (SQL NULL is
 * stored as the literal "NULL"). colw[] holds the fitted display width per
 * column. Non-SELECT statements leave ncols==0 and fill `status`. */
typedef struct {
    int    ncols;
    char  *colname[RS_MAXCOLS];
    int    colw[RS_MAXCOLS];
    int    nrows;
    int    cap_rows;
    char **cells;
    int    truncated;               /* rows beyond RS_MAXROWS were dropped */
    int    has_error;
    char   error[512];
    char   status[256];             /* "N row(s) in X.X ms" / "OK (…)" */
    double ms;
} ResultSet;

/* ---- Schema tree ------------------------------------------------------- */
typedef struct {
    char name[128];
    char kind[8];                   /* "table" / "view" */
    int  expanded;
    int  ncols;
    char col[MAX_TCOLS][96];        /* "name  type" */
} SchemaTable;

typedef struct { SchemaTable tab[MAX_TABLES]; int ntab; } Schema;

/* A flattened visible tree node — rebuilt each frame from the expand flags so
 * up/down navigation and rendering share one linear index space. */
enum { NODE_TABLE = 0, NODE_COL };
typedef struct { int table; int col; unsigned char kind; } TreeNode;

/* ---- Application state ------------------------------------------------- */
typedef struct {
    sqlite3  *db;
    const char *db_path;
    Schema    schema;
    ResultSet rs;

    char            compose[QUERY_MAX];
    TimuiInputState ed;

    int focus;                      /* PANE_* */
    int tree_sel, tree_scroll;
    int res_selrow, res_voff, res_coloff;

    /* theme-derived styles */
    TimuiStyle panel, header, status_st, sel;
    uint32_t   text_fg, dim_fg, accent_fg, err_fg;
} App;

/* ===================================================================== *
 * Result-set lifecycle + query execution
 * ===================================================================== */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* Duplicate a cell value (bounded); SQL NULL (txt==NULL) becomes "NULL". */
static char *dup_cell(const unsigned char *txt)
{
    const char *s = txt ? (const char *)txt : "NULL";
    size_t n = strlen(s);
    char *out;
    if (n > CELL_STORE) n = CELL_STORE;
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void rs_clear(ResultSet *rs)
{
    int i, total = rs->nrows * rs->ncols;
    for (i = 0; i < rs->ncols; i++) { free(rs->colname[i]); rs->colname[i] = NULL; }
    for (i = 0; i < total; i++) free(rs->cells[i]);
    free(rs->cells);
    memset(rs, 0, sizeof *rs);
}

/* Ensure room for one more row of `ncols` cells; returns 0 on OOM. */
static int rs_reserve(ResultSet *rs)
{
    if (rs->nrows < rs->cap_rows) return 1;
    {
        int nc = rs->cap_rows ? rs->cap_rows * 2 : 64;
        char **p = (char **)realloc(rs->cells,
                                    (size_t)nc * (size_t)rs->ncols * sizeof(char *));
        if (!p) return 0;
        rs->cells = p;
        rs->cap_rows = nc;
    }
    return 1;
}

/* Run `sql` against `db`, replacing `rs`. Populates columns/rows for a SELECT,
 * or a status line for a statement, or the error message on failure. */
static void run_query(sqlite3 *db, const char *sql, ResultSet *rs)
{
    sqlite3_stmt *stmt = NULL;
    double t0 = now_ms();
    int rc, c, ncol;

    rs_clear(rs);
    if (!sql || !sql[0]) { snprintf(rs->status, sizeof rs->status, "(empty query)"); return; }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        rs->has_error = 1;
        snprintf(rs->error, sizeof rs->error, "%s", sqlite3_errmsg(db));
        return;
    }

    ncol = sqlite3_column_count(stmt);
    if (ncol == 0) {                         /* a statement: execute + report */
        rc = sqlite3_step(stmt);
        rs->ms = now_ms() - t0;
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            rs->has_error = 1;
            snprintf(rs->error, sizeof rs->error, "%s", sqlite3_errmsg(db));
        } else {
            snprintf(rs->status, sizeof rs->status, "OK — %d change(s) in %.1f ms",
                     sqlite3_changes(db), rs->ms);
        }
        sqlite3_finalize(stmt);
        return;
    }

    rs->ncols = ncol > RS_MAXCOLS ? RS_MAXCOLS : ncol;
    for (c = 0; c < rs->ncols; c++) {
        const char *nm = sqlite3_column_name(stmt, c);
        rs->colname[c] = dup_cell((const unsigned char *)(nm ? nm : ""));
        rs->colw[c] = sqltbl_disp_width(rs->colname[c]);   /* seed with header */
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (rs->nrows >= RS_MAXROWS) { rs->truncated = 1; break; }
        if (!rs_reserve(rs)) { rs->truncated = 1; break; }
        for (c = 0; c < rs->ncols; c++) {
            char *cell = dup_cell(sqlite3_column_text(stmt, c));
            int w;
            rs->cells[rs->nrows * rs->ncols + c] = cell;
            w = sqltbl_disp_width(cell);
            if (w > rs->colw[c]) rs->colw[c] = w;          /* track natural width */
        }
        rs->nrows++;
    }
    rs->ms = now_ms() - t0;

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {           /* mid-scan error */
        rs->has_error = 1;
        snprintf(rs->error, sizeof rs->error, "%s", sqlite3_errmsg(db));
    }
    /* clamp each column's natural width into the display range */
    for (c = 0; c < rs->ncols; c++)
        rs->colw[c] = sqltbl_col_width(&rs->colw[c], 1, COL_MAX_W, COL_MIN_W);

    snprintf(rs->status, sizeof rs->status, "%d row%s%s in %.1f ms",
             rs->nrows, rs->nrows == 1 ? "" : "s",
             rs->truncated ? " (truncated)" : "", rs->ms);
    sqlite3_finalize(stmt);
}

/* ===================================================================== *
 * Schema tree construction
 * ===================================================================== */

/* Populate `sc->tab[t].col[]` from PRAGMA table_info(name). */
static void load_columns(sqlite3 *db, SchemaTable *t)
{
    sqlite3_stmt *st = NULL;
    char sql[256];
    /* PRAGMA does not accept bound parameters for the table name; the name comes
     * from sqlite_master (already a valid identifier), and we quote it by
     * doubling any embedded quote. */
    char q[128]; size_t i, j = 0;
    for (i = 0; t->name[i] && j < sizeof q - 2; i++) {
        if (t->name[i] == '"') q[j++] = '"';
        q[j++] = t->name[i];
    }
    q[j] = '\0';
    snprintf(sql, sizeof sql, "PRAGMA table_info(\"%s\")", q);
    t->ncols = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW && t->ncols < MAX_TCOLS) {
        const char *nm = (const char *)sqlite3_column_text(st, 1);
        const char *ty = (const char *)sqlite3_column_text(st, 2);
        int pk = sqlite3_column_int(st, 5);
        snprintf(t->col[t->ncols], sizeof t->col[0], "%s%s%s%s",
                 nm ? nm : "?", (ty && ty[0]) ? " " : "", ty ? ty : "",
                 pk ? "  [pk]" : "");
        t->ncols++;
    }
    sqlite3_finalize(st);
}

static void build_schema(sqlite3 *db, Schema *sc)
{
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT name, type FROM sqlite_master "
        "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
        "ORDER BY type='view', name";
    sc->ntab = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW && sc->ntab < MAX_TABLES) {
        const char *nm = (const char *)sqlite3_column_text(st, 0);
        const char *ty = (const char *)sqlite3_column_text(st, 1);
        SchemaTable *t = &sc->tab[sc->ntab];
        snprintf(t->name, sizeof t->name, "%s", nm ? nm : "?");
        snprintf(t->kind, sizeof t->kind, "%s", ty ? ty : "table");
        t->expanded = 0;
        load_columns(db, t);
        sc->ntab++;
    }
    sqlite3_finalize(st);
}

/* Flatten the tree into visible nodes; returns the count (<= cap). */
static int flatten(const Schema *sc, TreeNode *out, int cap)
{
    int n = 0, t, c;
    for (t = 0; t < sc->ntab && n < cap; t++) {
        out[n].kind = NODE_TABLE; out[n].table = t; out[n].col = -1; n++;
        if (sc->tab[t].expanded)
            for (c = 0; c < sc->tab[t].ncols && n < cap; c++) {
                out[n].kind = NODE_COL; out[n].table = t; out[n].col = c; n++;
            }
    }
    return n;
}

/* ===================================================================== *
 * Rendering
 * ===================================================================== */

/* Map a highlighter token class to a colour (same palette as the chat demo). */
static uint32_t hl_color(HlClass c, uint32_t deflt)
{
    switch (c) {
        case HL_KEYWORD: return 0xC792EAu;   /* purple */
        case HL_TYPE:    return 0x82AAFFu;   /* blue   */
        case HL_STRING:  case HL_CHAR: return 0xC3E88Du;  /* green */
        case HL_COMMENT: return 0x7A88A0u;   /* muted  */
        case HL_NUMBER:  return 0xF78C6Cu;   /* orange */
        case HL_PUNCT:   return 0x89DDFFu;   /* cyan   */
        default:         return deflt;
    }
}

/* Draw a fitted, padded cell string at (x,y) spanning `w` columns over `bg`. */
static void draw_cell(TimuiFrame *f, int x, int y, int w, const char *s,
                      uint32_t fg, uint32_t bg, uint32_t attrs)
{
    char buf[CELL_STORE + 8];
    int ell = 0;
    if (w <= 0) return;
    (void)sqltbl_fit_cell(s ? s : "", w, buf, sizeof buf, &ell);
    timui_label(f, x, y, timui_str_from_cstr(buf), timui_style_make(fg, bg, attrs));
}

/* SCHEMA TREE pane. */
static void draw_tree(TimuiFrame *f, App *a, TimuiRect r, int focused)
{
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    TreeNode nodes[MAX_TABLES + MAX_TABLES * 4];
    int nvis = flatten(&a->schema, nodes, (int)(sizeof nodes / sizeof nodes[0]));
    int body_y = r.y + 1, viewport = r.h - 1, i;
    SqlSlice sl;

    timui_draw_fill(buf, r, a->panel);
    timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, r.w, 1), a->header);
    draw_cell(f, r.x, r.y, r.w, focused ? " SCHEMA \xE2\x97\x82" : " SCHEMA",
              a->header.fg, a->header.bg, TIMUI_ATTR_BOLD);

    if (nvis == 0) {
        timui_label(f, r.x + 1, body_y, TIMUI_STR_LIT("(no tables)"),
                    timui_style_make(a->dim_fg, a->panel.bg, 0));
        return;
    }
    sl = sqltbl_page(nvis, viewport, a->tree_scroll);
    for (i = 0; i < sl.count; i++) {
        int idx = sl.first + i, y = body_y + i;
        TreeNode *nd = &nodes[idx];
        int sel = focused && idx == a->tree_sel;
        uint32_t bg = sel ? a->sel.bg : a->panel.bg;
        char line[160];
        if (sel) timui_draw_fill(buf, TIMUI_RECT(r.x, y, r.w, 1), a->sel);
        if (nd->kind == NODE_TABLE) {
            const SchemaTable *t = &a->schema.tab[nd->table];
            snprintf(line, sizeof line, "%s %s",
                     t->expanded ? "\xE2\x96\xBE" : "\xE2\x96\xB8", t->name); /* ▾ / ▸ */
            draw_cell(f, r.x, y, r.w, line,
                      sel ? a->sel.fg : a->accent_fg, bg, TIMUI_ATTR_BOLD);
        } else {
            const SchemaTable *t = &a->schema.tab[nd->table];
            snprintf(line, sizeof line, "    %s", t->col[nd->col]);
            draw_cell(f, r.x, y, r.w, line, sel ? a->sel.fg : a->text_fg, bg, 0);
        }
    }
}

/* RESULTS TABLE pane. */
static void draw_results(TimuiFrame *f, App *a, TimuiRect r, int focused)
{
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    ResultSet *rs = &a->rs;
    int body_y = r.y + 1, viewport = r.h - 1;
    int c, x, vis;
    SqlSlice sl;

    timui_draw_fill(buf, r, a->panel);

    if (rs->has_error) {
        timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, r.w, 1), a->header);
        draw_cell(f, r.x, r.y, r.w, " ERROR", a->err_fg, a->header.bg, TIMUI_ATTR_BOLD);
        draw_cell(f, r.x + 1, body_y, r.w - 1, rs->error, a->err_fg, a->panel.bg, 0);
        return;
    }
    if (rs->ncols == 0) {                    /* statement result or nothing yet */
        timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, r.w, 1), a->header);
        draw_cell(f, r.x, r.y, r.w, focused ? " RESULTS \xE2\x97\x82" : " RESULTS",
                  a->header.fg, a->header.bg, TIMUI_ATTR_BOLD);
        draw_cell(f, r.x + 1, body_y, r.w - 1,
                  rs->status[0] ? rs->status
                                : "Type a query below and press Enter to run.",
                  a->dim_fg, a->panel.bg, 0);
        return;
    }

    /* header row: column names from res_coloff, each padded to colw + a │ sep */
    timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, r.w, 1), a->header);
    x = r.x;
    for (c = a->res_coloff; c < rs->ncols && x < r.x + r.w; c++) {
        int w = rs->colw[c];
        if (w > r.x + r.w - x) w = r.x + r.w - x;
        draw_cell(f, x, r.y, w, rs->colname[c], a->header.fg, a->header.bg, TIMUI_ATTR_BOLD);
        x += rs->colw[c];
        if (x < r.x + r.w) {
            timui_label(f, x, r.y, TIMUI_STR_LIT("\xE2\x94\x82"),  /* │ */
                        timui_style_make(a->dim_fg, a->header.bg, 0));
            x += 1;
        }
    }

    /* data rows */
    sl = sqltbl_page(rs->nrows, viewport, a->res_voff);
    for (vis = 0; vis < sl.count; vis++) {
        int row = sl.first + vis, y = body_y + vis;
        int sel = focused && row == a->res_selrow;
        TimuiStyle rowst = sel ? a->sel : a->panel;
        timui_draw_fill(buf, TIMUI_RECT(r.x, y, r.w, 1), rowst);
        x = r.x;
        for (c = a->res_coloff; c < rs->ncols && x < r.x + r.w; c++) {
            int w = rs->colw[c];
            const char *cell = rs->cells[row * rs->ncols + c];
            if (w > r.x + r.w - x) w = r.x + r.w - x;
            draw_cell(f, x, y, w, cell, rowst.fg, rowst.bg, 0);
            x += rs->colw[c];
            if (x < r.x + r.w) {
                timui_label(f, x, y, TIMUI_STR_LIT("\xE2\x94\x82"),
                            timui_style_make(a->dim_fg, rowst.bg, 0));
                x += 1;
            }
        }
    }
    /* a scroll hint when columns/rows are off-screen */
    if (a->res_coloff > 0 || sl.count < rs->nrows) {
        char hint[64];
        snprintf(hint, sizeof hint, " row %d/%d  col %d/%d ",
                 rs->nrows ? a->res_selrow + 1 : 0, rs->nrows,
                 a->res_coloff + 1, rs->ncols);
        draw_cell(f, r.x + r.w - (int)strlen(hint) - 1, r.y, (int)strlen(hint) + 1,
                  hint, a->dim_fg, a->header.bg, 0);
    }
}

/* Draw one highlighted SQL line at (x0,y) clipped to [x0, x0+w), skipping the
 * first `scroll` display columns. */
static void draw_sql_line(TimuiFrame *f, int x0, int y, int w, int scroll,
                          const char *s, int len, uint32_t bg, uint32_t deflt)
{
    HlTok toks[128];
    int nt = chat_highlight(s, len, "sql", toks, 128), ti = 0, i = 0, col = 0;
    while (i < len) {
        HlClass cls = HL_TEXT;
        uint32_t cp; int adv, gw, sx;
        TimuiStr ch;
        while (ti < nt && i >= toks[ti].off + toks[ti].len) ti++;
        if (ti < nt && i >= toks[ti].off) cls = toks[ti].cls;
        adv = timui_utf8_decode(s + i, (size_t)(len - i), &cp);
        if (adv <= 0) adv = 1;
        gw = timui_utf8_width(cp);
        sx = x0 + col - scroll;
        if (sx >= x0 && sx < x0 + w) {
            ch.ptr = s + i; ch.len = (size_t)adv;
            timui_label(f, sx, y, ch, timui_style_make(hl_color(cls, deflt), bg, 0));
        }
        col += gw > 0 ? gw : 0;
        i += adv;
    }
}

/* QUERY EDITOR: paint every line of the (possibly multi-line) compose buffer
 * with SQL highlighting. Row 0 honours the field's horizontal scroll. */
static void draw_sql_editor(TimuiFrame *f, App *a, TimuiRect r, int scroll_x)
{
    const char *line = a->compose;
    int row = 0;
    while (row < r.h) {
        const char *nl = strchr(line, '\n');
        int len = nl ? (int)(nl - line) : (int)strlen(line);
        draw_sql_line(f, r.x, r.y + row, r.w, row == 0 ? scroll_x : 0,
                      line, len, a->panel.bg, a->text_fg);
        if (!nl) break;
        line = nl + 1; row++;
    }
}

/* ===================================================================== *
 * Input handling (per active pane)
 * ===================================================================== */

/* Run the current editor buffer as a query and reset the results view. */
static void app_run(App *a)
{
    run_query(a->db, a->compose, &a->rs);
    a->res_selrow = 0; a->res_voff = 0; a->res_coloff = 0;
}

static void handle_tree(TimuiFrame *f, App *a, int viewport)
{
    TreeNode nodes[MAX_TABLES + MAX_TABLES * 4];
    int nvis = flatten(&a->schema, nodes, (int)(sizeof nodes / sizeof nodes[0]));
    if (nvis == 0) return;
    if (timui_key_pressed(f, TIMUI_KEY_DOWN)) a->tree_sel++;
    if (timui_key_pressed(f, TIMUI_KEY_UP))   a->tree_sel--;
    if (a->tree_sel < 0) a->tree_sel = 0;
    if (a->tree_sel >= nvis) a->tree_sel = nvis - 1;
    {
        TreeNode *nd = &nodes[a->tree_sel];
        SchemaTable *t = &a->schema.tab[nd->table];
        int right = timui_key_pressed(f, TIMUI_KEY_RIGHT);
        int left  = timui_key_pressed(f, TIMUI_KEY_LEFT);
        int enter = timui_key_pressed(f, TIMUI_KEY_ENTER);
        if (nd->kind == NODE_TABLE) {
            if (right) t->expanded = 1;
            if (left)  t->expanded = 0;
            if (enter) {                       /* expand + SELECT * FROM table */
                t->expanded = 1;
                snprintf(a->compose, sizeof a->compose,
                         "SELECT * FROM \"%s\" LIMIT 1000;", t->name);
                a->ed.cursor = strlen(a->compose); a->ed.scroll_x = 0;
                app_run(a);
            }
        } else {                               /* a column node */
            if (left) { /* collapse back to the parent table */
                int p; for (p = a->tree_sel; p > 0 && nodes[p].kind != NODE_TABLE; p--) {}
                a->tree_sel = p; a->schema.tab[nodes[p].table].expanded = 0;
            }
        }
    }
    a->tree_scroll = sqltbl_scroll_to(a->tree_sel, a->tree_scroll, viewport, nvis);
}

static void handle_results(TimuiFrame *f, App *a, int viewport)
{
    ResultSet *rs = &a->rs;
    int page = viewport > 1 ? viewport - 1 : 1;
    if (rs->nrows <= 0) return;
    if (timui_key_pressed(f, TIMUI_KEY_DOWN))      a->res_selrow++;
    if (timui_key_pressed(f, TIMUI_KEY_UP))        a->res_selrow--;
    if (timui_key_pressed(f, TIMUI_KEY_PAGE_DOWN)) a->res_selrow += page;
    if (timui_key_pressed(f, TIMUI_KEY_PAGE_UP))   a->res_selrow -= page;
    a->res_selrow -= timui_mouse_wheel(f);          /* wheel scrolls selection */
    if (timui_key_pressed(f, TIMUI_KEY_HOME))       a->res_selrow = 0;
    if (timui_key_pressed(f, TIMUI_KEY_END))        a->res_selrow = rs->nrows - 1;
    if (a->res_selrow < 0) a->res_selrow = 0;
    if (a->res_selrow >= rs->nrows) a->res_selrow = rs->nrows - 1;

    if (timui_key_pressed(f, TIMUI_KEY_RIGHT) && a->res_coloff < rs->ncols - 1) a->res_coloff++;
    if (timui_key_pressed(f, TIMUI_KEY_LEFT)  && a->res_coloff > 0)             a->res_coloff--;

    a->res_voff = sqltbl_scroll_to(a->res_selrow, a->res_voff, viewport, rs->nrows);
}

static void handle_editor(TimuiFrame *f, App *a, TimuiRect editor)
{
    /* Shift+Enter inserts a newline; plain Enter (via input_field) runs. */
    int se = timui_key_pressed_mods(f, TIMUI_KEY_ENTER, TIMUI_MOD_SHIFT);
    TimuiRect ifr = editor; ifr.h = 1;
    timui_set_focus(f, TIMUI_ID("query"));
    if (se) {
        size_t clen = strlen(a->compose); int cur = (int)a->ed.cursor;
        if (clen + 1 < sizeof a->compose && cur >= 0 && cur <= (int)clen) {
            memmove(a->compose + cur + 1, a->compose + cur, clen - cur + 1);
            a->compose[cur] = '\n'; a->ed.cursor = (size_t)cur + 1;
        }
    }
    if (timui_input_field_styled(f, TIMUI_ID("query"), ifr, &a->ed,
                                 timui_style_make(a->text_fg, a->panel.bg, 0)) && !se)
        app_run(a);
}

/* ===================================================================== *
 * Frame: layout → input → draw
 * ===================================================================== */

static void frame(TimuiFrame *f, App *a)
{
    TimuiRect root = timui_root(f), title, statusr, rule, editor, tree, vsep, results;
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int compose_rows, tree_vp, res_vp, mx, my;
    char tbar[160];

    /* editor grows with Shift+Enter lines (capped like the chat composer) */
    { int nl = 1; const char *p = a->compose; while (*p) if (*p++ == '\n') nl++;
      compose_rows = nl < 1 ? 1 : (nl > 6 ? 6 : nl); }

    /* global keys */
    if (timui_key_pressed(f, TIMUI_KEY_F10)) timui_quit(f->ui);
    if (timui_key_pressed(f, TIMUI_KEY_TAB)) a->focus = (a->focus + 1) % PANE_COUNT;

    /* layout (top→bottom, then left tree | results) */
    title   = timui_cut_top(&root, 1);
    statusr = timui_cut_bottom(&root, 1);
    editor  = timui_cut_bottom(&root, compose_rows);
    rule    = timui_cut_bottom(&root, 1);
    tree    = timui_cut_left(&root, TREE_W < root.w - 4 ? TREE_W : root.w / 3);
    vsep    = timui_cut_left(&root, 1);
    results = root;
    tree_vp = tree.h - 1;
    res_vp  = results.h - 1;

    /* click-to-focus (and row select in the results pane) */
    if (timui_mouse_clicked(f, &mx, &my)) {
        if (mx >= tree.x && mx < tree.x + tree.w)          a->focus = PANE_TREE;
        else if (my >= editor.y)                            a->focus = PANE_EDITOR;
        else if (mx >= results.x) {
            a->focus = PANE_RESULTS;
            if (my > results.y) {
                int row = a->res_voff + (my - results.y - 1);
                if (row >= 0 && row < a->rs.nrows) a->res_selrow = row;
            }
        }
    }

    /* per-pane input */
    if (a->focus == PANE_TREE)         handle_tree(f, a, tree_vp);
    else if (a->focus == PANE_RESULTS) handle_results(f, a, res_vp);
    if (a->focus != PANE_EDITOR) timui_set_focus(f, 0);  /* free the arrows */

    /* title bar */
    timui_draw_fill(buf, title, a->status_st);
    snprintf(tbar, sizeof tbar,
             " SQLite TUI \xE2\x80\x94 %s   \xC2\xB7   Tab: focus  \xE2\x86\x91\xE2\x86\x93\xE2\x86\x90\xE2\x86\x92: nav  Enter: run  F10: quit ",
             a->db_path);
    timui_label(f, title.x, title.y, timui_str_from_cstr(tbar), a->status_st);

    /* panes */
    draw_tree(f, a, tree, a->focus == PANE_TREE);
    timui_draw_vline(buf, vsep.x, vsep.y, vsep.h, timui_style_make(a->dim_fg, a->panel.bg, 0));
    draw_results(f, a, results, a->focus == PANE_RESULTS);

    /* editor: a rule, the ❯ prompt + field, then the highlighted lines */
    timui_draw_hline(buf, rule.x, rule.y, rule.w, timui_style_make(a->dim_fg, a->panel.bg, 0));
    {
        TimuiRect prompt = timui_cut_left(&editor, 2);
        timui_draw_fill(buf, prompt, a->panel);
        timui_label(f, prompt.x, prompt.y, TIMUI_STR_LIT("\xE2\x9D\xAF "),  /* ❯ */
                    timui_style_make(a->accent_fg, a->panel.bg,
                                     a->focus == PANE_EDITOR ? TIMUI_ATTR_BOLD : 0));
        timui_draw_fill(buf, editor, a->panel);
        if (a->focus == PANE_EDITOR) handle_editor(f, a, editor);
        draw_sql_editor(f, a, editor, a->ed.scroll_x);
    }

    /* status line: error (red) or the row-count/timing status */
    timui_draw_fill(buf, statusr, a->status_st);
    if (a->rs.has_error)
        timui_label(f, statusr.x + 1, statusr.y, timui_str_from_cstr(a->rs.error),
                    timui_style_make(a->err_fg, a->status_st.bg, TIMUI_ATTR_BOLD));
    else
        timui_label(f, statusr.x + 1, statusr.y, timui_str_from_cstr(a->rs.status),
                    a->status_st);
}

/* ===================================================================== *
 * Setup + main
 * ===================================================================== */

static void app_init_styles(App *a)
{
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
    a->panel     = timui_theme_style(&th, TIMUI_SLOT_PANEL);
    a->header    = timui_theme_style(&th, TIMUI_SLOT_STATUS);
    a->status_st = timui_theme_style(&th, TIMUI_SLOT_STATUS);
    a->sel       = timui_theme_style(&th, TIMUI_SLOT_SELECTION);
    a->text_fg   = timui_theme_style(&th, TIMUI_SLOT_TEXT).fg;
    a->dim_fg    = timui_theme_style(&th, TIMUI_SLOT_TEXT_DIM).fg;
    a->accent_fg = timui_theme_style(&th, TIMUI_SLOT_SUCCESS).fg;
    a->err_fg    = timui_theme_style(&th, TIMUI_SLOT_ERROR).fg;
}

int main(int argc, char **argv)
{
    App a;
    const char *db_path = NULL, *query = NULL;
    int headless = 0, frames = 0, cols = 100, rows = 30, i, fr;
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    TimuiFakeTransport fake;

    /* args: <db> [--query SQL] [--frames N] [--exit-after] [--headless]
     *       [--cols C] [--rows R] */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--query") == 0 && i + 1 < argc)       query = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { frames = atoi(argv[++i]); headless = 1; }
        else if (strcmp(argv[i], "--exit-after") == 0)             { headless = 1; if (!frames) frames = 1; }
        else if (strcmp(argv[i], "--headless") == 0)               { headless = 1; if (!frames) frames = 1; }
        else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc)   cols = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc)   rows = atoi(argv[++i]);
        else if (argv[i][0] != '-')                                db_path = argv[i];
    }
    if (!db_path) {
        fprintf(stderr, "usage: %s <db> [--query SQL] [--frames N | --exit-after]\n", argv[0]);
        return 2;
    }

    memset(&a, 0, sizeof a);
    a.db_path = db_path;
    a.ed.text = a.compose;
    a.ed.cap  = sizeof a.compose;
    a.focus   = PANE_EDITOR;

    if (sqlite3_open(db_path, &a.db) != SQLITE_OK) {
        fprintf(stderr, "sqlite_tui: cannot open %s: %s\n", db_path, sqlite3_errmsg(a.db));
        sqlite3_close(a.db);
        return 1;
    }
    build_schema(a.db, &a.schema);
    app_init_styles(&a);

    if (query) {                             /* seed the editor + run it once */
        snprintf(a.compose, sizeof a.compose, "%s", query);
        a.ed.cursor = strlen(a.compose);
        app_run(&a);
    }

    if (headless) {
        /* Fake transport: render `frames` frames with no tty, dump the last. */
        TimuiAllocator al = timui_default_allocator();
        if (cols < 1) cols = 100; if (rows < 1) rows = 30;
        timui_fake_init(&fake, &al);
        if (timui_open_for_test(&ui, timui_fake_transport(&fake), cols, rows, &al) != TIMUI_OK) {
            fprintf(stderr, "sqlite_tui: headless open failed\n");
            rs_clear(&a.rs); sqlite3_close(a.db); timui_fake_destroy(&fake);
            return 1;
        }
        for (fr = 0; fr < frames; fr++) {
            TimuiFrame *f = NULL;
            if (!timui_begin(ui, &f)) break;
            frame(f, &a);
            if (fr == frames - 1) {          /* dump the final grid for the smoke */
                char grid[1 << 16];
                timui_snapshot_grid(timui_frame_buffer(f), grid, sizeof grid);
                fprintf(stderr, "%s\n", grid);
            }
            timui_end(f);
        }
        fprintf(stderr, "sqlite_tui: headless %d frame(s) rows=%d cols=%d%s\n",
                frames, a.rs.nrows, a.rs.ncols, a.rs.has_error ? " ERROR" : "");
        timui_close(ui);
        timui_fake_destroy(&fake);
        rs_clear(&a.rs);
        sqlite3_close(a.db);
        return 0;
    }

    /* interactive: real terminal */
    cfg.title     = "sqlite_tui";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_MODERN_DARK;
    if (timui_open(&cfg, &ui) != TIMUI_OK) {
        fprintf(stderr, "sqlite_tui: not a terminal (try --exit-after for a headless smoke)\n");
        rs_clear(&a.rs); sqlite3_close(a.db);
        return 1;
    }
    while (!timui_should_quit(ui)) {
        TimuiFrame *f = NULL;
        if (!timui_begin(ui, &f)) break;
        frame(f, &a);
        timui_end(f);
    }
    timui_close(ui);
    rs_clear(&a.rs);
    sqlite3_close(a.db);
    return 0;
}
