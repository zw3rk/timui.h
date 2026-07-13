/* ---- combobox / autocomplete ------------------------------------------ *
 * A single-focus editable field with an attached filtered popup. The caller
 * owns query storage and selection state; options are immutable TimuiStrs. */
static int cb_match_(TimuiStr opt, const char *query){
    size_t ol = opt.len, ql = strlen(query ? query : "");
    size_t i, j;
    if(ql == 0) return 1;
    if(!opt.ptr || ql > ol) return 0;
    for(i = 0; i + ql <= ol; i++){
        for(j = 0; j < ql; j++){
            char a = opt.ptr[i + j], b = query[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) break;
        }
        if(j == ql) return 1;
    }
    return 0;
}
static int cb_text_byte_(unsigned char b){
    return b >= 0x20 && b != 0x7f;
}
static int cb_filter_(const TimuiStr *options, int count, const char *query, int *matches, int max){
    int i, n = 0;
    if(!options || count <= 0 || !matches || max <= 0) return 0;
    for(i = 0; i < count && n < max; i++){
        TimuiStr opt = options[i].ptr ? options[i] : (TimuiStr){ "", 0 };
        if(cb_match_(opt, query)) matches[n++] = i;
    }
    return n;
}
static int cb_insert_text_(TimuiComboboxState *st, const char *src, int nbytes){
    int j = 0, changed = 0;
    while(j < nbytes){
        int n = utf8_lead_len((unsigned char)src[j]);
        size_t m = (size_t)(n > 0 ? n : 1);
        if(!cb_text_byte_((unsigned char)src[j])){ j++; continue; }
        if(j + (int)m > nbytes) m = (size_t)(nbytes - j);
        if(!text_insert_(st->query, st->cap, st->cursor, src + j, m)) break;
        st->cursor += m;
        j += (int)m;
        changed = 1;
    }
    return changed;
}
static void cb_copy_option_(TimuiComboboxState *st, TimuiStr opt){
    size_t out = 0, i = 0;
    if(!st || !st->query || st->cap == 0) return;
    while(i < opt.len && out + 1 < st->cap){
        size_t n = timui_grapheme_next(opt.ptr, opt.len, i);
        if(n <= i) n = i + 1;
        if(out + (n - i) + 1 > st->cap) break;
        memcpy(st->query + out, opt.ptr + i, n - i);
        out += n - i;
        i = n;
    }
    st->query[out] = '\0';
    st->cursor = out;
    st->scroll_x = 0;
}
static void cb_scroll_to_selected_(TimuiComboboxState *st, int visible, int count){
    if(st->selected < 0) st->selected = 0;
    if(count <= 0){ st->selected = 0; st->scroll = 0; return; }
    if(st->selected >= count) st->selected = count - 1;
    if(st->scroll < 0) st->scroll = 0;
    if(visible <= 0){ st->scroll = 0; return; }
    if(st->selected < st->scroll) st->scroll = st->selected;
    if(st->selected >= st->scroll + visible) st->scroll = st->selected - visible + 1;
    if(count <= visible) st->scroll = 0;
    else if(st->scroll > count - visible) st->scroll = count - visible;
}
TIMUI_API TimuiComboboxResult timui_combobox(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState state){
    TimuiComboboxResult res;
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect field, popup;
    int matches[256], match_count = 0, visible, i;
    int user_changed = 0, query_changed = 0;
    int accept_filtered = -1;
    res.state = state;
    res.state_changed = 0;
    res.query_changed = 0;
    res.activated = -1;
    res.selected = -1;
    res.match_count = 0;
    res.focused = 0;
    if(!f || !f->ui || !state.query || state.cap == 0 || count < 0) return res;
    ui = f->ui;
    { size_t len = text_len_bounded_(state.query, state.cap);
      if(len >= state.cap){ len = state.cap - 1; state.query[len] = '\0'; }
      if(state.cursor > len) state.cursor = len; }
    field = TIMUI_RECT(r.x, r.y, r.w, r.h > 0 ? 1 : 0);
    popup = TIMUI_RECT(r.x, r.y + 1, r.w, r.h > 1 ? r.h - 1 : 0);
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        size_t len;
        size_t cursor_before;
        if(ui->text_in_len > 0){
            query_changed = cb_insert_text_(&state, ui->text_in, ui->text_in_len);
            if(query_changed){ state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1; }
        }
        len = strlen(state.query);
        cursor_before = state.cursor;
        if(ui->key_in & TIMUI_KEYIN_LEFT)  state.cursor = utf8_drop_last(state.query, state.cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) state.cursor = utf8_next_(state.query, state.cursor, len);
        if(ui->key_in & TIMUI_KEYIN_HOME)  state.cursor = 0;
        if(ui->key_in & TIMUI_KEYIN_END)   state.cursor = len;
        if(state.cursor != cursor_before) user_changed = 1;
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && state.cursor > 0){
            size_t prev = utf8_drop_last(state.query, state.cursor);
            state.cursor = text_erase_(state.query, prev, state.cursor);
            query_changed = 1; state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1;
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(state.query, state.cursor, strlen(state.query));
            if(nxt > state.cursor){
                (void)text_erase_(state.query, state.cursor, nxt);
                query_changed = 1; state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1;
            }
        }
        if(ui->key_pressed == TIMUI_KEY_ESCAPE && state.open){
            state.open = 0;
            user_changed = 1;
        }
        ui->text_in_len = 0;
        ui->enter_count = 0;
        ui->key_in = 0;
    }
    match_count = cb_filter_(options, count, state.query, matches, (int)(sizeof matches / sizeof matches[0]));
    visible = popup.h > 0 ? popup.h : 0;
    cb_scroll_to_selected_(&state, visible, match_count);
    if(match_count > 0) res.selected = matches[state.selected];
    if(ir.focused && state.open && match_count > 0){
        if(timui_key_pressed(f, TIMUI_KEY_UP) && state.selected > 0){
            state.selected--; user_changed = 1;
        }
        if(timui_key_pressed(f, TIMUI_KEY_DOWN) && state.selected < match_count - 1){
            state.selected++; user_changed = 1;
        }
        cb_scroll_to_selected_(&state, visible, match_count);
    }
    if(ir.clicked && ui->ia.mouse_released){
        int my = ui->ia.mouse_y - r.y;
        if(my == 0){ state.open = 1; user_changed = 1; }
        else if(state.open && my > 0 && my <= visible){
            int row = state.scroll + my - 1;
            if(row >= 0 && row < match_count) accept_filtered = row;
        }
    }
    if(ir.focused && state.open && timui_key_pressed(f, TIMUI_KEY_ENTER) && match_count > 0)
        accept_filtered = state.selected;
    if(accept_filtered >= 0 && accept_filtered < match_count){
        int orig = matches[accept_filtered];
        cb_copy_option_(&state, options[orig]);
        res.activated = orig;
        res.selected = orig;
        query_changed = 1;
        user_changed = 1;
        state.open = 0;
        state.selected = accept_filtered;
    } else if(match_count > 0) {
        res.selected = matches[state.selected];
    }
    { int ccol = display_col_(state.query, state.cursor);
      int scroll_x = state.scroll_x;
      if(ccol < scroll_x) scroll_x = ccol;
      if(field.w > 0 && ccol >= scroll_x + field.w) scroll_x = ccol - field.w + 1;
      if(scroll_x < 0) scroll_x = 0;
      state.scroll_x = scroll_x;
      if(ir.focused && field.h > 0){
          ui->cursor_x = field.x + (ccol - state.scroll_x);
          ui->cursor_y = field.y;
          ui->cursor_visible = 1;
      }
    }
    { TimuiStyle fst = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
          ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
          ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
      timui_draw_fill(&ui->curr, field, fst);
      timui_push_clip(f, field);
      timui_draw_text(&ui->curr, field.x - state.scroll_x, field.y, timui_str_from_cstr(state.query), fst);
      timui_pop_clip(f); }
    if(state.open && popup.h > 0){
        timui_push_clip(f, popup);
        for(i = 0; i < popup.h; i++){
            int mi = state.scroll + i;
            TimuiStyle st;
            TimuiStr label;
            if(mi >= match_count) break;
            st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX,
                mi == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                mi == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
            label = options[matches[mi]].ptr ? options[matches[mi]] : TIMUI_STR_LIT("");
            timui_draw_row_(&ui->curr, TIMUI_RECT(popup.x, popup.y + i, popup.w, 1), 0, label, st);
        }
        timui_pop_clip(f);
    }
    res.state = state;
    res.state_changed = user_changed;
    res.query_changed = query_changed;
    res.match_count = match_count;
    res.focused = ir.focused;
    return res;
}
TIMUI_API TimuiComboboxResult timui_combobox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState *state){
    TimuiComboboxState empty;
    TimuiComboboxResult res;
    memset(&empty, 0, sizeof empty);
    res = timui_combobox(f, id, r, options, count, state ? *state : empty);
    if(state && res.state_changed) *state = res.state;
    return res;
}
