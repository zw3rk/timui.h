/* ---- table widget (v0.2) ---------------------------------------------- *
 * A scrollable multi-column grid with a header row and row selection. */
TIMUI_API void timui_table(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state){
    Timui *ui;
    int col, row, x, hdr_h = 1, cw, vis;
    TimuiRect body, content;
    (void)id;
    if(!f || !f->ui || !state || ncols <= 0) return;
    ui = f->ui;
    cw = r.w / ncols;
    vis = r.h - hdr_h;
    if(vis < 1) vis = 1;
    if(state->selected < 0) state->selected = 0;
    if(nrows > 0 && state->selected >= nrows) state->selected = nrows - 1;
    if(state->scroll < 0) state->scroll = 0;
    if(state->selected < state->scroll) state->scroll = state->selected;
    if(state->selected >= state->scroll + vis) state->scroll = state->selected - vis + 1;
    /* header row */
    { TimuiStyle hs = timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL_TITLE);
      x = r.x;
      for(col = 0; col < ncols; col++){
          timui_draw_fill(&ui->curr, TIMUI_RECT(x, r.y, cw, 1), hs);
          if(headers && headers[col].ptr)
              timui_draw_text(&ui->curr, x + 1, r.y, headers[col], hs);
          x += cw;
      }
    }
    /* data rows (scrollable) */
    body = TIMUI_RECT(r.x, r.y + hdr_h, r.w, r.h - hdr_h);
    content = timui_scroll_begin(f, body, state->scroll);
    for(row = 0; row < nrows; row++){
        TimuiStyle st = timui_theme_style(&ui->theme,
            row == state->selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        x = content.x;
        for(col = 0; col < ncols; col++){
            const char *cell = cell_fn ? cell_fn(ud, row, col) : "";
            timui_draw_fill(&ui->curr, TIMUI_RECT(x, content.y + row, cw, 1), st);
            timui_draw_text(&ui->curr, x + 1, content.y + row, timui_str_from_cstr(cell), st);
            x += cw;
        }
    }
    timui_scroll_end(f);
    if(timui_key_pressed(f, TIMUI_KEY_UP) && state->selected > 0) state->selected--;
    if(timui_key_pressed(f, TIMUI_KEY_DOWN) && state->selected < nrows - 1) state->selected++;
}
