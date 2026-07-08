/*
 * gallery.c — a one-screen showcase of the timui.h widget + layout library.
 *
 * Lays out (via the constraint solver timui_split) a set of bordered panels,
 * each demonstrating one library widget on canned data: the tab bar, the virtual
 * multi-column table, the scrollable tree, the spectrum bar chart, the
 * gauge/meter/progress indicators + spinner, combobox autocomplete, toast
 * notifications, and the syntax-highlighted code viewer. Everything is
 * static/animated-by-tick so the screen is deterministic;
 * `--frames N` renders N frames and quits (for a headless smoke through a pty).
 *
 * Run:  nix develop -c make run-gallery      Smoke: make smoke-gallery
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <string.h>
#include <stdlib.h>

/* A 0..1 triangle wave from an integer phase — cheap deterministic animation
 * (no libm, so the example builds under the generic single-file rule). */
static float tri(int phase){ int m = ((phase % 40) + 40) % 40; return (m < 20 ? m : 40 - m) / 20.0f; }

/* ---- canned data ------------------------------------------------------- */

/* Virtual table: a tiny "process" list fetched on demand by cell_fn. */
static const char *const TBL[][3] = {
    { "1042", "timui-render",  "running"  },
    { "2381", "chat",          "sleeping" },
    { "  77", "radio-stream",  "running"  },
    { "5540", "sqlite_tui",    "stopped"  },
    { " 913", "gallery",       "running"  },
    { "3120", "file_manager",  "sleeping" },
};
#define TBL_ROWS ((int)(sizeof TBL / sizeof TBL[0]))
static const char *tbl_cell(void *ud, int row, int col){
    (void)ud;
    if(row < 0 || row >= TBL_ROWS || col < 0 || col > 2) return "";
    return TBL[row][col];
}

/* A small file tree (DFS order; depth + expanded flags). */
static const TimuiTreeNode TREE[] = {
    { 0, "timui.h/",   1, 1 },
    { 1, "include/",   1, 1 },
    { 2, "timui.h",    0, 0 },
    { 1, "src/",       1, 1 },
    { 2, "timui_layout.c", 0, 0 },
    { 2, "timui_tabs.c",   0, 0 },
    { 2, "timui_chart.c",  0, 0 },
    { 1, "examples/",  1, 0 },
    { 2, "gallery.c",  0, 0 },
    { 1, "Makefile",   0, 0 },
};
#define TREE_N ((int)(sizeof TREE / sizeof TREE[0]))

static const char *const TAB_LABELS[] = { "Overview", "Data", "Charts", "Code" };
#define TAB_N ((int)(sizeof TAB_LABELS / sizeof TAB_LABELS[0]))

static const char CODE_SRC[] =
    "/* the constraint layout solver */\n"
    "int timui_split(TimuiRect area, TimuiAxis ax,\n"
    "                const TimuiConstraint *c, int n,\n"
    "                TimuiRect *out) {\n"
    "    int fixed = 0, flex = 0;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        if (c[i].kind == TIMUI_CON_FLEX)\n"
    "            flex += c[i].value;   /* weight */\n"
    "        else fixed += solve_fixed(c[i]);\n"
    "    return tile(area, ax, c, n, out);\n"
    "}\n";

int main(int argc, char **argv){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    int max_frames = 0, frames = 0, tick = 0, tab_sel = 0, code_scroll = 0;
    TimuiTableState tstate = {0, 0, 0};
    TimuiTreeState  trstate = {0, 0};
    TimuiBarState   bars = {{0}, 0};
    char combo_query[32] = "ga";
    TimuiComboboxState combo = { combo_query, sizeof combo_query, 2, 0, 1, 0, 0 };

    { int i; for(i = 1; i < argc; i++)
        if(!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]); }

    cfg.title = "timui.h gallery"; cfg.input_fd = 0; cfg.output_fd = 1;
    cfg.profile = TIMUI_PROFILE_AUTO;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme = TIMUI_THEME_MODERN_DARK;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    { TimuiTheme th   = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
      TimuiStyle panel = timui_theme_style(&th, TIMUI_SLOT_PANEL);
      uint32_t   bg    = panel.bg;
      uint32_t   text  = timui_theme_style(&th, TIMUI_SLOT_TEXT).fg;
      uint32_t   dim   = timui_theme_style(&th, TIMUI_SLOT_TEXT_DIM).fg;
      uint32_t   ok    = timui_theme_style(&th, TIMUI_SLOT_SUCCESS).fg;
      uint32_t   accent = 0x6CB6FFu;
      TimuiStyle border_st = timui_style_make(dim, bg, 0);
      TimuiStyle title_st  = timui_style_make(accent, bg, TIMUI_ATTR_BOLD);

      /* fixed column budgets keep the table + tree readable */
      const TimuiTableModel model = {
          NULL, 3, TBL_ROWS, tbl_cell, NULL, 4, 14, 128
      };
      const TimuiStr headers[3] = {
          TIMUI_STR_LIT("pid"), TIMUI_STR_LIT("name"), TIMUI_STR_LIT("state")
      };
      const TimuiStr combo_opts[5] = {
          TIMUI_STR_LIT("gauge"), TIMUI_STR_LIT("gallery"), TIMUI_STR_LIT("graph"),
          TIMUI_STR_LIT("gradient"), TIMUI_STR_LIT("grid")
      };

      while(!timui_should_quit(ui)){
          TimuiFrame *f = NULL;
          TimuiRect root, rows[4], cols[3], lcol[2], mcol[2];
          const TimuiConstraint vmain[] = { TIMUI_LEN(1), TIMUI_LEN(1), TIMUI_FLEX(1), TIMUI_LEN(1) };
          const TimuiConstraint h3[]     = { TIMUI_FLEX(1), TIMUI_FLEX(1), TIMUI_FLEX(1) };
          const TimuiConstraint v2[]     = { TIMUI_FLEX(1), TIMUI_FLEX(1) };
          float vals[8]; int b;

          if(!timui_begin(ui, &f)) break;
          root = timui_root(f);

          /* Esc / F10 quit (interactive); the headless smoke exits via --frames. */
          if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
              timui_quit(ui);

          /* header · tabs · body(3 cols) · status */
          timui_split_v(root, vmain, 4, rows);
          { TimuiTableModel m = model; m.headers = headers;   /* header labels */
            timui_label(f, rows[0].x + 1, rows[0].y,
                        TIMUI_STR_LIT("timui.h — widget & layout gallery"), title_st);
            (void)timui_tabs(f, TIMUI_ID("tabs"), rows[1], TAB_LABELS, TAB_N, &tab_sel);

            timui_split_h(rows[2], h3, 3, cols);
            timui_split_v(cols[0], v2, 2, lcol);
            timui_split_v(cols[1], v2, 2, mcol);

            /* left column: table + tree */
            { TimuiRect in = timui_border(f, lcol[0], TIMUI_BOX_ROUNDED, TIMUI_STR_LIT(" table "), border_st);
              (void)timui_table_ex_mut(f, TIMUI_ID("tbl"), in, &m, &tstate); }
            { TimuiRect in = timui_border(f, lcol[1], TIMUI_BOX_ROUNDED, TIMUI_STR_LIT(" tree "), border_st);
              (void)timui_tree_scroll_mut(f, TIMUI_ID("tree"), in, TREE, TREE_N, &trstate); }

            /* middle column: spectrum bars + indicators */
            { TimuiRect in = timui_border(f, mcol[0], TIMUI_BOX_ROUNDED, TIMUI_STR_LIT(" barchart "), border_st);
              TimuiBarOpts o = {0};
              o.max = 1.0f; o.lo = 0xC3143Cu; o.hi = 0xB3E5FCu; o.track = bg;
              o.peak_decay = 0.04f; o.cap_light = 0.5f; o.gap = 1;
              for(b = 0; b < 8; b++)
                  vals[b] = 0.1f + 0.85f * tri(tick * 2 + b * 5);
              timui_barchart(f, in, vals, 8, o, &bars); }
            { TimuiRect in = timui_border(f, mcol[1], TIMUI_BOX_ROUNDED, TIMUI_STR_LIT(" indicators "), border_st);
              float p = tri(tick);
              TimuiStyle g = timui_style_make(ok, 0x10131Au, 0);
              if(in.h >= 4 && in.w > 6){
                  timui_label(f, in.x, in.y,     TIMUI_STR_LIT("progress"), timui_style_make(dim, bg, 0));
                  timui_progress(f, TIMUI_RECT(in.x, in.y + 1, in.w, 1), p, g);
                  timui_label(f, in.x, in.y + 2, TIMUI_STR_LIT("meter"),    timui_style_make(dim, bg, 0));
                  timui_meter(f, TIMUI_RECT(in.x, in.y + 3, in.w, 1), p, 0.9f, g);
                  timui_spinner(f, in.x + in.w - 1, in.y, tick,
                                timui_style_make(accent, bg, TIMUI_ATTR_BOLD));
                  if(in.h >= 8){
                      timui_label(f, in.x, in.y + 4, TIMUI_STR_LIT("combobox"), timui_style_make(dim, bg, 0));
                      (void)timui_combobox_mut(f, TIMUI_ID("combo"), TIMUI_RECT(in.x, in.y + 5, in.w, in.h - 5),
                                               combo_opts, 5, &combo);
                  }
              } }

            /* right column: syntax-highlighted code viewer */
            { TimuiRect in = timui_border(f, cols[2], TIMUI_BOX_ROUNDED, TIMUI_STR_LIT(" code "), border_st);
              timui_code(f, in, CODE_SRC, (int)(sizeof CODE_SRC - 1), "c", &code_scroll); }

            /* status */
            timui_label(f, rows[3].x + 1, rows[3].y,
                        TIMUI_STR_LIT("Tab/click widgets · arrows scroll · Esc/F10 quit"),
                        timui_style_make(dim, bg, 0));
            if(root.w >= 20 && root.h >= 9){
                const TimuiToast demo_toasts[2] = {
                    { TIMUI_STR_LIT("Styles loaded"), TIMUI_STR_LIT("Still no runtime circus"),
                      TIMUI_TOAST_SUCCESS, 0, 0, 0 },
                    { TIMUI_STR_LIT("Reminder"), TIMUI_STR_LIT("Curse curses, kindly"),
                      TIMUI_TOAST_INFO, 0, 0, 0 }
                };
                int tw = root.w > 36 ? 32 : root.w - 2;
                int tx = root.x + root.w - tw - 1;
                if(tw >= 18)
                    (void)timui_toasts(f, TIMUI_ID("gallery-toasts"),
                                       TIMUI_RECT(tx, root.y + 2, tw, 6),
                                       demo_toasts, 2, (uint64_t)tick * 100u);
            }
            (void)text;
          }

          timui_end(f);
          tick++;
          if(max_frames > 0 && ++frames >= max_frames) timui_quit(ui);
      }
    }

    timui_close(ui);
    return 0;
}
