/* ---- table widget (v0.2) ---------------------------------------------- *
 * A scrollable multi-column grid with a header row and row selection.
 * Controlled: takes TimuiTableState by value and returns the new state in the
 * result; timui_table_mut is the write-back convenience twin. */
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
    /* header row */
    { TimuiStyle hs = timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL_TITLE);
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
        TimuiStyle st = timui_theme_style(&ui->theme,
            row == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        x = content.x;
        for(col = 0; col < ncols; col++){
            const char *cell = cell_fn ? cell_fn(ud, row, col) : "";
            timui_draw_row_(&ui->curr, TIMUI_RECT(x, content.y + row, cw, 1), 1, timui_str_from_cstr(cell), st);
            x += cw;
        }
    }
    timui_scroll_end(f);
    /* keyboard nav only when focused — call interact_button once to avoid
     * double-registering the table id in the tab order */
    { TimuiInteractResult tir = timui_interact_button(&ui->ia, id, r);
      res.focused = tir.focused;
      if(tir.focused) state.selected = timui_updown_nav_(f, state.selected, nrows);
    }
    res.state = state;
    res.state_changed = (state.selected != orig);
    return res;
}
TIMUI_API TimuiTableResult timui_table_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state){
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0};
    TimuiTableResult res = timui_table(f, id, r, headers, ncols, nrows, cell_fn, ud, in);
    if(state && res.state_changed) *state = res.state;   /* write back only on a real change */
    return res;
}
