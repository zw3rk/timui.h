/* ---- command palette (v0.2) ------------------------------------------- *
 * A popup with a filter input and a filtered command list. Returns the
 * activated command index, or -1. */
static int cmd_matches(const char *cmd, const char *filter){
    /* simple substring match (case-insensitive for ASCII) */
    size_t cl = strlen(cmd), fl = strlen(filter);
    size_t i, j;
    if(fl == 0) return 1;
    if(fl > cl) return 0;
    for(i = 0; i + fl <= cl; i++){
        for(j = 0; j < fl; j++){
            char a = cmd[i + j], b = filter[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) break;
        }
        if(j == fl) return 1;
    }
    return 0;
}
TIMUI_API int timui_command_palette(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState *state){
    Timui *ui;
    TimuiRect input_r, list_r;
    int i, matched_count = 0, activated = -1;
    int matched_idx[256];
    (void)id;
    if(!f || !f->ui || !commands || count <= 0 || !state) return -1;
    ui = f->ui;
    timui_panel_begin(f, id, r, TIMUI_STR_LIT("Command Palette"), TIMUI_BORDER_SINGLE);
    input_r = TIMUI_RECT(r.x + 1, r.y + 1, r.w - 2, 1);
    list_r  = TIMUI_RECT(r.x + 1, r.y + 2, r.w - 2, r.h - 3);
    timui_input_line_buf(f, id + 1, input_r, state->filter, sizeof state->filter);
    /* filter */
    for(i = 0; i < count && matched_count < 256; i++){
        const char *s = commands[i].ptr ? commands[i].ptr : "";
        if(cmd_matches(s, state->filter)) matched_idx[matched_count++] = i;
    }
    if(state->selected < 0) state->selected = 0;
    if(state->selected >= matched_count) state->selected = matched_count > 0 ? matched_count - 1 : 0;
    /* draw matched commands */
    { TimuiRect content = timui_scroll_begin(f, list_r, 0);
      for(i = 0; i < matched_count; i++){
          int orig = matched_idx[i];
          TimuiStyle st = timui_theme_style(&ui->theme,
              i == state->selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
          timui_draw_fill(&ui->curr, TIMUI_RECT(content.x, content.y + i, list_r.w, 1), st);
          timui_draw_text(&ui->curr, content.x + 1, content.y + i, commands[orig], st);
      }
      timui_scroll_end(f);
    }
    /* V18: only steer the palette when its filter input is focused, so drawing
     * the palette without focusing it doesn't swallow arrow/Enter from siblings. */
    if(ui->ia.focus == id + 1){
        if(timui_key_pressed(f, TIMUI_KEY_UP) && state->selected > 0) state->selected--;
        else if(timui_key_pressed(f, TIMUI_KEY_DOWN) && state->selected < matched_count - 1) state->selected++;
        if(timui_key_pressed(f, TIMUI_KEY_ENTER) && matched_count > 0){
            activated = matched_idx[state->selected];
            state->filter[0] = '\0';
            state->selected = 0;
        }
    }
    timui_panel_end(f);
    return activated;
}
