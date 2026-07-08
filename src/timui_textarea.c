/* ---- text-area widget (v0.2) ------------------------------------------ *
 * A multi-line text editor. Click to focus, type to insert, Backspace/Delete
 * remove whole grapheme clusters. Lines are split on '\n'. Plain Enter can
 * submit when requested; Shift-Enter and paste insert line breaks. */
static int text_area_insert_span_(TimuiTextAreaState *st, const char *src, int nbytes){
    int j = 0;
    int changed = 0;
    while(j < nbytes){
        int n = utf8_lead_len((unsigned char)src[j]);
        size_t m = (size_t)(n > 0 ? n : 1);     /* defensive: stray byte as 1 */
        if(j + (int)m > nbytes) m = (size_t)(nbytes - j);
        if(!text_insert_(st->text, st->cap, st->cursor, src + j, m)) break;
        st->cursor += m;
        j += (int)m;
        changed = 1;
    }
    return changed;
}
static int text_area_insert_newline_(TimuiTextAreaState *st){
    if(!text_insert_(st->text, st->cap, st->cursor, "\n", 1)) return 0;
    st->cursor++;
    return 1;
}
static int text_area_enter_submits_(const Timui *ui, int enter_index, uint32_t flags){
    if(!(flags & TIMUI_TEXT_AREA_ENTER_SUBMITS)) return 0;
    return !(ui->enter_mods[enter_index] & TIMUI_MOD_SHIFT);
}
static void text_area_defer_after_enter_(Timui *ui, int at, int enter_index){
    int tail = ui->text_in_len - at;
    int k;
    if(tail < 0) tail = 0;
    if(tail > (int)sizeof(ui->pending_in)) tail = (int)sizeof(ui->pending_in);
    memcpy(ui->pending_in, ui->text_in + at, (size_t)tail);
    ui->pending_in_len = tail;
    ui->pending_enter_count = ui->enter_count - enter_index - 1;
    if(ui->pending_enter_count < 0) ui->pending_enter_count = 0;
    for(k = 0; k < ui->pending_enter_count; k++){
        ui->pending_enter_at[k] = ui->enter_at[enter_index + 1 + k] - at;
        ui->pending_enter_mods[k] = ui->enter_mods[enter_index + 1 + k];
    }
}
static void text_area_process_text_(Timui *ui, TimuiTextAreaState *st,
                                    uint32_t flags, TimuiTextAreaResult *res){
    int j = 0;
    int e;
    for(e = 0; e < ui->enter_count; e++){
        int at = ui->enter_at[e];
        if(at < j) at = j;
        if(at > ui->text_in_len) at = ui->text_in_len;
        if(text_area_insert_span_(st, ui->text_in + j, at - j)) res->changed = 1;
        if(text_area_enter_submits_(ui, e, flags)){
            res->submitted = 1;
            text_area_defer_after_enter_(ui, at, e);
            return;
        }
        if(text_area_insert_newline_(st)) res->changed = 1;
        j = at;
    }
    if(text_area_insert_span_(st, ui->text_in + j, ui->text_in_len - j)) res->changed = 1;
}
TIMUI_API TimuiTextAreaResult timui_text_area_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                 TimuiTextAreaState st, uint32_t flags){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect content;
    TimuiTextAreaResult res;
    size_t i;
    int y = 0;
    res.state = st;
    res.changed = 0;
    res.submitted = 0;
    res.focused = 0;
    if(!f || !f->ui || !st.text || st.cap == 0) return res;
    { size_t text_len = text_len_bounded_(st.text, st.cap);
      if(text_len >= st.cap){ text_len = st.cap - 1; st.text[text_len] = '\0'; }
      if(st.cursor > text_len) st.cursor = text_len; }   /* Y1: untrusted cursor -> OOB */
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        /* Insert typed codepoints at the cursor. Deletion/movement below is
         * grapheme-aware; insertion remains codepoint-by-codepoint and
         * cap-bounded, so invalid partial UTF-8 is not created.
         * The edit helpers live in the widgets section, in scope via the unity
         * build. */
        text_area_process_text_(ui, &st, flags, &res);
        /* cursor movement (one step per frame — the key_in bitmask can't count
         * repeats; key auto-repeat delivers one per frame). */
        if(ui->key_in & TIMUI_KEYIN_LEFT)  st.cursor = utf8_drop_last(st.text, st.cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) st.cursor = utf8_next_(st.text, st.cursor, strlen(st.text));
        if(ui->key_in & TIMUI_KEYIN_HOME)  st.cursor = line_start_(st.text, st.cursor);
        if(ui->key_in & TIMUI_KEYIN_END)   st.cursor = line_end_(st.text, st.cursor);
        /* deletion: backspace removes the cluster before the cursor, DELETE
         * the one at the cursor. */
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st.cursor > 0){
            size_t prev = utf8_drop_last(st.text, st.cursor);
            st.cursor = text_erase_(st.text, prev, st.cursor);
            res.changed = 1;
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(st.text, st.cursor, strlen(st.text));
            if(nxt > st.cursor){
                (void)text_erase_(st.text, st.cursor, nxt);
                res.changed = 1;
            }
        }
        ui->text_in_len = 0;
        ui->enter_count = 0;
        ui->key_in = 0;
    }
    { TimuiStyle sst = timui_widget_style_(ui, TIMUI_WIDGET_TEXT_AREA,
          ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
          ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
      content = timui_scroll_begin(f, r, st.scroll_y);
      i = 0;
      while(i < st.cap && st.text[i]){
          size_t ls = i;
          while(i < st.cap && st.text[i] && st.text[i] != '\n'){
              if(st.text[i] == '\r') break;  /* \r or \r\n line break */
              i++;
          }
          timui_draw_text(&ui->curr, content.x, content.y + y,
                          (TimuiStr){ st.text + ls, i - ls }, sst);
          if(i < st.cap && (st.text[i] == '\r' || st.text[i] == '\n')){
              if(st.text[i] == '\r' && i + 1 < st.cap && st.text[i+1] == '\n') i++;
              i++;
          }
          y++;
      }
      /* auto-scroll to keep the cursor visible (computed before scroll_begin next frame) */
      {  int cursor_row = 0;
         size_t ci;
         for(ci = 0; ci < st.cursor && ci < st.cap; ci++)
             if(st.text[ci] == '\n' || st.text[ci] == '\r'){
                 cursor_row++;
                 if(st.text[ci] == '\r' && ci + 1 < st.cap && st.text[ci+1] == '\n') ci++;
             }
         if(cursor_row < st.scroll_y) st.scroll_y = cursor_row;
         if(cursor_row >= st.scroll_y + r.h) st.scroll_y = cursor_row - r.h + 1;
         if(st.scroll_y < 0) st.scroll_y = 0;
      }
      timui_scroll_end(f);
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          int crow, ccol;
          text_pos_(st.text, st.cursor, &crow, &ccol);
          if(crow >= st.scroll_y && crow < st.scroll_y + r.h && ccol < r.w){
              ui->cursor_x = r.x + ccol;                /* content.x == r.x (vertical scroll only) */
              ui->cursor_y = r.y + (crow - st.scroll_y);
              ui->cursor_visible = 1;
          }
      }
    }
    res.state = st;
    return res;
}
TIMUI_API TimuiTextAreaResult timui_text_area_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                  TimuiTextAreaState *state, uint32_t flags){
    TimuiTextAreaResult res;
    if(!state){
        TimuiTextAreaState empty = {0};
        return timui_text_area_ex(f, id, r, empty, flags);
    }
    res = timui_text_area_ex(f, id, r, *state, flags);
    *state = res.state;
    return res;
}
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *state){
    (void)timui_text_area_mut(f, id, r, state, TIMUI_TEXT_AREA_DEFAULT);
}
