/* ---- command palette (v0.2) ------------------------------------------- *
 * A popup with a filter input and a filtered command list. Controlled: takes
 * TimuiCmdPaletteState by value and returns the new state plus the activated
 * command index (or -1) in the result; timui_command_palette_mut writes back. */
static int cmd_matches(TimuiStr cmd, const char *filter){
    /* simple substring match (case-insensitive for ASCII); uses cmd.len so a
     * non-NUL-terminated TimuiStr slice is honored (not strlen). */
    size_t cl = cmd.len, fl = strlen(filter);
    size_t i, j;
    if(fl == 0) return 1;
    if(fl > cl) return 0;
    for(i = 0; i + fl <= cl; i++){
        for(j = 0; j < fl; j++){
            char a = cmd.ptr[i + j], b = filter[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) break;
        }
        if(j == fl) return 1;
    }
    return 0;
}
static int cmd_filter_(const TimuiStr *commands, int count, const char *filter,
                       int *matched_idx, int max){
    int i, matched_count = 0;
    if(!commands || !matched_idx || max <= 0 || count <= 0) return 0;
    for(i = 0; i < count && matched_count < max; i++){
        TimuiStr cmd = commands[i].ptr ? commands[i] : (TimuiStr){ "", 0 };
        if(cmd_matches(cmd, filter)) matched_idx[matched_count++] = i;
    }
    return matched_count;
}
TIMUI_API TimuiCmdPaletteResult timui_command_palette(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState state){
    TimuiCmdPaletteResult res;
    Timui *ui;
    TimuiRect input_r, list_r;
    int i, matched_count = 0, sel_in = state.selected;
    int matched_idx[256];
    char filter_in[64];
    res.state = state; res.activated = -1; res.state_changed = 0;
    if(!f || !f->ui || !commands || count <= 0) return res;
    ui = f->ui;
    memcpy(filter_in, state.filter, sizeof filter_in);   /* snapshot to detect edits */
    timui_panel_begin(f, id, r, TIMUI_STR_LIT("Command Palette"), TIMUI_BORDER_SINGLE);
    input_r = TIMUI_RECT(r.x + 1, r.y + 1, r.w - 2, 1);
    list_r  = TIMUI_RECT(r.x + 1, r.y + 2, r.w - 2, r.h - 3);
    timui_input_line_buf(f, id + 1, input_r, state.filter, sizeof state.filter);
    matched_count = cmd_filter_(commands, count, state.filter, matched_idx,
                                (int)(sizeof matched_idx / sizeof matched_idx[0]));
    if(state.selected < 0) state.selected = 0;
    if(state.selected >= matched_count) state.selected = matched_count > 0 ? matched_count - 1 : 0;
    /* V18: only steer the palette when its filter input is focused, so drawing
     * the palette without focusing it doesn't swallow arrow/Enter from siblings.
     * Process before drawing so selection/activation state and pixels agree. */
    if(ui->ia.focus == id + 1){
        state.selected = timui_updown_nav_(f, state.selected, matched_count);
        if(timui_key_pressed(f, TIMUI_KEY_ENTER) && matched_count > 0){
            res.activated = matched_idx[state.selected];
            state.filter[0] = '\0';
            state.selected = 0;
            matched_count = cmd_filter_(commands, count, state.filter, matched_idx,
                                        (int)(sizeof matched_idx / sizeof matched_idx[0]));
        }
    }
    /* draw matched commands */
    { TimuiRect content = timui_scroll_begin(f, list_r, 0);
      for(i = 0; i < matched_count; i++){
          int orig = matched_idx[i];
          TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX,
              i == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
              i == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
          timui_draw_row_(&ui->curr, TIMUI_RECT(content.x, content.y + i, list_r.w, 1), 1, commands[orig], st);
      }
      timui_scroll_end(f);
    }
    timui_panel_end(f);
    res.state = state;
    res.state_changed = (state.selected != sel_in ||
                         memcmp(state.filter, filter_in, sizeof filter_in) != 0);
    return res;
}
TIMUI_API TimuiCmdPaletteResult timui_command_palette_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState *state){
    TimuiCmdPaletteState in;
    TimuiCmdPaletteResult res;
    if(state) in = *state; else memset(&in, 0, sizeof in);
    res = timui_command_palette(f, id, r, commands, count, in);
    if(state && res.state_changed) *state = res.state;   /* write back edits/nav/activation reset */
    return res;
}
