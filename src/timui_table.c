/* ---- table widget (v0.2 + virtual grid) ------------------------------- *
 * Two families:
 *   - timui_table / timui_table_mut : the original fixed-width grid (unchanged;
 *     backward-compatible for examples/file_manager.c + procmon.c).
 *   - timui_table_ex / timui_table_ex_mut : a VIRTUAL multi-column grid over a
 *     TimuiTableModel (row count + cell accessor, so large sets aren't
 *     materialized) with a STICKY header, per-column content-fit widths, and
 *     both vertical + horizontal scroll.
 *
 * The layout kernel (display width, column fit, cell truncation, paging/scroll)
 * is pure + side-effect-free and unit-tested in tests/test_grid.c — lifted from
 * examples/sqlite_table.h so the widget and standalone callers share one path.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* ----------------------------------------------------------------------- */
/* Pure data-grid math (declared TIMUI_API in include/timui.h).              */
/* ----------------------------------------------------------------------- */

/* Display width of the first `len` bytes of a UTF-8 buffer (internal: also
 * serves TimuiStr headers, which need not be NUL-terminated). */
static int timui_disp_width_n_(const char *s, size_t len){
    size_t i;
    int w = 0;
    if(!s) return 0;
    for(i = 0; i < len;){
        size_t n = timui_grapheme_next(s, len, i);
        if(n <= i) n = i + 1;                  /* never stall on malformed input */
        w += timui_grapheme_width(s + i, n - i);
        i = n;
    }
    return w;
}

TIMUI_API int timui_display_width(const char *s){
    return s ? timui_disp_width_n_(s, strlen(s)) : 0;
}

TIMUI_API int timui_col_fit_width(const int *cellw, int n, int maxw, int minw){
    int i, m = 0, w;
    if(cellw)
        for(i = 0; i < n; i++)
            if(cellw[i] > m) m = cellw[i];
    w = m;
    if(w > maxw) w = maxw;                      /* cap  */
    if(w < minw) w = minw;                      /* floor (also handles maxw<minw) */
    return w;
}

/* U+2026 HORIZONTAL ELLIPSIS — one display column (3 UTF-8 bytes). */
#define TIMUI_ELLIPSIS_ "\xE2\x80\xA6"

TIMUI_API int timui_fit_cell(const char *s, int width, char *out, size_t cap, int *ellipsis){
    size_t i, len, o = 0;
    int used = 0, full, budget;
    if(ellipsis) *ellipsis = 0;
    if(!out || cap == 0) return 0;
    out[0] = '\0';
    if(!s) s = "";
    len = strlen(s);
    full = timui_disp_width_n_(s, len);
    if(width <= 0){                            /* no room at all */
        if(ellipsis) *ellipsis = (full > 0);
        return 0;
    }
    if(full <= width){                         /* fits whole — copy verbatim */
        for(i = 0; i < len;){
            size_t n = timui_grapheme_next(s, len, i);
            int gw;
            if(n <= i) n = i + 1;
            if(o + (n - i) >= cap){
                if(ellipsis) *ellipsis = 1;
                break;
            }
            gw = timui_grapheme_width(s + i, n - i);
            memcpy(out + o, s + i, n - i);
            o += n - i;
            used += gw;
            i = n;
        }
        out[o] = '\0';
        return used;
    }
    if(cap < sizeof(TIMUI_ELLIPSIS_)){         /* no room for ellipsis + NUL */
        if(ellipsis) *ellipsis = 1;
        return 1;
    }
    /* Truncate: reserve the last column for the ellipsis; never split a wide
     * glyph (when one straddles the budget the ellipsis lands early and the
     * caller pads the slack). */
    budget = width - 1;
    for(i = 0; i < len;){
        size_t n = timui_grapheme_next(s, len, i);
        int gw;
        if(n <= i) n = i + 1;
        gw = timui_grapheme_width(s + i, n - i);
        if(used + gw > budget) break;
        if(o + (n - i) + sizeof(TIMUI_ELLIPSIS_) > cap) break;  /* keep room for "…" + NUL */
        memcpy(out + o, s + i, n - i);
        o += n - i;
        used += gw;
        i = n;
    }
    if(o + 3 < cap){ memcpy(out + o, TIMUI_ELLIPSIS_, 3); o += 3; }
    out[o] = '\0';
    if(ellipsis) *ellipsis = 1;
    return used + 1;                           /* content columns + ellipsis */
}

TIMUI_API TimuiSlice timui_page_slice(int total, int viewport, int offset){
    TimuiSlice s;
    int maxoff;
    s.first = 0; s.count = 0;
    if(total <= 0) return s;
    maxoff = total - viewport;
    if(maxoff < 0) maxoff = 0;
    if(offset < 0) offset = 0;
    if(offset > maxoff) offset = maxoff;
    s.first = offset;
    if(viewport <= 0){ s.count = 0; return s; }
    s.count = total - offset;
    if(s.count > viewport) s.count = viewport;
    return s;
}

TIMUI_API int timui_scroll_to(int sel, int offset, int viewport, int total){
    (void)total;
    if(viewport <= 0) return offset;
    if(sel < offset) offset = sel;
    else if(sel >= offset + viewport) offset = sel - viewport + 1;
    if(offset < 0) offset = 0;
    return offset;
}

/* ----------------------------------------------------------------------- */
/* timui_table / timui_table_mut — original fixed-width grid (UNCHANGED).    */
/* Controlled: takes TimuiTableState by value and returns the new state in    */
/* the result; timui_table_mut is the write-back convenience twin.           */
/* ----------------------------------------------------------------------- */
TIMUI_API TimuiTableResult timui_table(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState state){
    TimuiTableResult res;
    Timui *ui;
    int col, row, x, hdr_h = 1, cw, vis, orig;
    TimuiRect body, content;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || ncols <= 0) return res;
    ui = f->ui;
    cw = r.w / ncols;
    vis = r.h - hdr_h;
    if(vis < 1) vis = 1;
    if(state.selected < 0) state.selected = 0;
    if(nrows > 0 && state.selected >= nrows) state.selected = nrows - 1;
    orig = state.selected;                 /* post-clamp: a pure clamp is not a change */
    if(state.scroll < 0) state.scroll = 0;
    if(state.selected < state.scroll) state.scroll = state.selected;
    if(state.selected >= state.scroll + vis) state.scroll = state.selected - vis + 1;
    /* Keyboard nav only when focused — process before drawing so the highlight
     * and returned state agree in the same frame. */
    { TimuiInteractResult tir = timui_interact_button(&ui->ia, id, r);
      res.focused = tir.focused;
      if(tir.focused) state.selected = timui_updown_nav_(f, state.selected, nrows);
    }
    if(state.selected < state.scroll) state.scroll = state.selected;
    if(state.selected >= state.scroll + vis) state.scroll = state.selected - vis + 1;
    /* header row */
    { TimuiStyle hs = timui_widget_style_(ui, TIMUI_WIDGET_TABLE, TIMUI_SLOT_PANEL_TITLE, 0);
      x = r.x;
      for(col = 0; col < ncols; col++){
          TimuiStr h = (headers && headers[col].ptr) ? headers[col] : (TimuiStr){ NULL, 0 };
          timui_draw_row_(&ui->curr, TIMUI_RECT(x, r.y, cw, 1), 1, h, hs);   /* draw_text skips NULL */
          x += cw;
      }
    }
    /* data rows (scrollable) */
    body = TIMUI_RECT(r.x, r.y + hdr_h, r.w, r.h - hdr_h);
    content = timui_scroll_begin(f, body, state.scroll);
    for(row = 0; row < nrows; row++){
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TABLE,
            row == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
            row == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
        x = content.x;
        for(col = 0; col < ncols; col++){
            const char *cell = cell_fn ? cell_fn(ud, row, col) : "";
            timui_draw_row_(&ui->curr, TIMUI_RECT(x, content.y + row, cw, 1), 1, timui_str_from_cstr(cell), st);
            x += cw;
        }
    }
    timui_scroll_end(f);
    res.state = state;
    res.state_changed = (state.selected != orig);
    return res;
}
TIMUI_API TimuiTableResult timui_table_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state){
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0, 0};
    TimuiTableResult res = timui_table(f, id, r, headers, ncols, nrows, cell_fn, ud, in);
    if(state && res.state_changed) *state = res.state;   /* write back only on a real change */
    return res;
}

/* ----------------------------------------------------------------------- */
/* timui_table_ex — virtual multi-column grid (sticky header, h+v scroll).   */
/* ----------------------------------------------------------------------- */
#define TIMUI_GRID_MAX_COLS 64      /* columns fitted per frame (stack budget) */
#define TIMUI_GRID_COL_MIN  3       /* default per-column width floor          */
#define TIMUI_GRID_COL_MAX  24      /* default per-column width cap            */
#define TIMUI_GRID_SAMPLE   128     /* default rows sampled for width fitting  */
#define TIMUI_GRID_COL_GAP  1       /* blank cells between columns             */
#define TIMUI_GRID_HSTEP    4       /* cells scrolled per Left/Right key       */
#define TIMUI_GRID_CELLBUF  256     /* per-cell fit scratch                    */

/* Fit each of the first `ncols` (<= TIMUI_GRID_MAX_COLS) columns to its content:
 * the widest of the header + a bounded row sample, capped/floored. Returns the
 * number of columns actually fitted (clamped to the cap). */
static int timui_grid_fit_cols_(const TimuiTableModel *m, int *colw){
    int c, r, ncols, srows, maxw, minw, sample;
    ncols  = m->ncols < TIMUI_GRID_MAX_COLS ? m->ncols : TIMUI_GRID_MAX_COLS;
    minw   = m->col_min > 0 ? m->col_min : TIMUI_GRID_COL_MIN;
    maxw   = m->col_max > 0 ? m->col_max : TIMUI_GRID_COL_MAX;
    sample = m->sample  > 0 ? m->sample  : TIMUI_GRID_SAMPLE;
    srows  = m->nrows < sample ? m->nrows : sample;
    for(c = 0; c < ncols; c++){
        int widest = 0;
        if(m->headers && m->headers[c].ptr){
            int hw = timui_disp_width_n_(m->headers[c].ptr, m->headers[c].len);
            if(hw > widest) widest = hw;
        }
        for(r = 0; r < srows; r++){
            const char *txt = m->cell_fn ? m->cell_fn(m->ud, r, c) : "";
            int cw = timui_display_width(txt);
            if(cw > widest) widest = cw;
        }
        colw[c] = timui_col_fit_width(&widest, 1, maxw, minw);
    }
    return ncols;
}

/* Fit a (possibly non-NUL-terminated) TimuiStr into `width` columns -> out. */
static void timui_grid_fit_str_(TimuiStr s, int width, char *out, size_t cap){
    char tmp[TIMUI_GRID_CELLBUF];
    size_t n = s.ptr ? (s.len < sizeof(tmp) - 1 ? s.len : sizeof(tmp) - 1) : 0;
    int ell;
    if(n) memcpy(tmp, s.ptr, n);
    tmp[n] = '\0';
    timui_fit_cell(tmp, width, out, cap, &ell);
}

TIMUI_API TimuiTableResult timui_table_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState state){
    TimuiTableResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int colw[TIMUI_GRID_MAX_COLS];
    int ncols, nrows, vis, total_w, sel, scroll, hscroll, orig_sel, c, cx, y, wh;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !model || model->ncols <= 0) return res;
    ui = f->ui;
    nrows = model->nrows;
    ncols = timui_grid_fit_cols_(model, colw);

    /* Total grid width (cells): sum of fitted widths + gaps between columns. */
    total_w = 0;
    for(c = 0; c < ncols; c++) total_w += colw[c] + (c ? TIMUI_GRID_COL_GAP : 0);

    /* Vertical layout: one sticky header row, the rest is the scroll body. */
    vis = r.h - 1;
    if(vis < 1) vis = 1;

    /* Selection clamp (self-heals a stale selected, like listbox/tree). */
    sel = state.selected;
    if(nrows <= 0) sel = 0;
    else { if(sel < 0) sel = 0; if(sel >= nrows) sel = nrows - 1; }
    orig_sel = sel;

    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;

    scroll  = state.scroll  < 0 ? 0 : state.scroll;
    hscroll = state.hscroll < 0 ? 0 : state.hscroll;

    if(ir.focused){
        /* Up/Down move the selection; Left/Right pan horizontally. */
        sel = timui_updown_nav_(f, sel, nrows);
        if(timui_key_pressed(f, TIMUI_KEY_LEFT))  hscroll -= TIMUI_GRID_HSTEP;
        if(timui_key_pressed(f, TIMUI_KEY_RIGHT)) hscroll += TIMUI_GRID_HSTEP;
        if(timui_key_pressed(f, TIMUI_KEY_HOME))  hscroll = 0;
    }

    /* Mouse wheel scrolls the body and drags the selection into the new window. */
    wh = timui_mouse_wheel(f);
    if(wh && timui_mouse_wheel_over_(ui, r)){
        scroll -= wh;
        scroll = timui_page_slice(nrows, vis, scroll).first;
        if(sel < scroll) sel = scroll;
        if(sel >= scroll + vis) sel = scroll + vis - 1;
        if(nrows > 0){ if(sel >= nrows) sel = nrows - 1; } else sel = 0;
        if(sel < 0) sel = 0;
    }

    /* Click selects a body row (ignoring clicks on the sticky header). */
    if(ir.clicked){
        int my = ui->ia.mouse_y - (r.y + 1);
        int idx = scroll + my;
        if(my >= 0 && idx >= 0 && idx < nrows) sel = idx;
    }

    /* Keep the selection visible, then clamp both axes to a valid window. */
    scroll  = timui_scroll_to(sel, scroll, vis, nrows);
    scroll  = timui_page_slice(nrows, vis, scroll).first;
    hscroll = timui_page_slice(total_w, r.w, hscroll).first;

    /* --- draw (clipped to r so h-scroll overflow and header/body stay inside). */
    if(r.h >= 1 && r.w >= 1){
        timui_push_clip(f, r);
        /* sticky header: h-scrolled only, never v-scrolled. */
        { TimuiStyle hs = timui_widget_style_(ui, TIMUI_WIDGET_TABLE, TIMUI_SLOT_PANEL_TITLE, 0);
          timui_draw_fill(&ui->curr, TIMUI_RECT(r.x, r.y, r.w, 1), hs);
          cx = r.x - hscroll;
          for(c = 0; c < ncols; c++){
              char buf[TIMUI_GRID_CELLBUF];
              TimuiStr h = model->headers ? model->headers[c] : (TimuiStr){ NULL, 0 };
              timui_grid_fit_str_(h, colw[c], buf, sizeof buf);
              timui_draw_text(&ui->curr, cx, r.y, timui_str_from_cstr(buf), hs);
              cx += colw[c] + TIMUI_GRID_COL_GAP;
          }
        }
        /* body rows: the visible vertical slice, each h-scrolled. */
        { TimuiSlice rows = timui_page_slice(nrows, vis, scroll);
          int i;
          for(i = 0; i < rows.count; i++){
              int row = rows.first + i;
              TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TABLE,
                  row == sel ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                  row == sel ? TIMUI_STYLE_STATE_SELECTED : 0);
              y = r.y + 1 + i;
              /* fill the whole row first for a continuous selection highlight */
              timui_draw_fill(&ui->curr, TIMUI_RECT(r.x, y, r.w, 1), st);
              cx = r.x - hscroll;
              for(c = 0; c < ncols; c++){
                  char buf[TIMUI_GRID_CELLBUF];
                  int ell;
                  const char *txt = model->cell_fn ? model->cell_fn(model->ud, row, c) : "";
                  timui_fit_cell(txt, colw[c], buf, sizeof buf, &ell);
                  timui_draw_text(&ui->curr, cx, y, timui_str_from_cstr(buf), st);
                  cx += colw[c] + TIMUI_GRID_COL_GAP;
              }
          }
        }
        timui_pop_clip(f);
    }

    state.selected = sel;
    state.scroll   = scroll;
    state.hscroll  = hscroll;
    res.state = state;
    /* A pure clamp is not a change; only a real selection move counts (matches
     * timui_table). Scroll is derived state — the _mut twin persists it, but it
     * alone does not flag state_changed. */
    res.state_changed = (sel != orig_sel);
    return res;
}

TIMUI_API TimuiTableResult timui_table_ex_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState *state){
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0, 0};
    TimuiTableResult res = timui_table_ex(f, id, r, model, in);
    /* Write back the full derived state (selection + both scroll offsets), so
     * scrolling persists across frames — but only when something actually moved
     * (selection changed, or a scroll offset was adjusted). */
    if(state && (res.state_changed ||
                 res.state.scroll  != in.scroll ||
                 res.state.hscroll != in.hscroll))
        *state = res.state;
    return res;
}
