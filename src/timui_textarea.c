/* ---- text-area widget (v0.2) ------------------------------------------ *
 * A multi-line text editor. Click to focus, type to insert, backspace deletes.
 * Lines are split on '\n'. (Cursor movement beyond append-at-end is future.) */
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *st){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect content;
    size_t i;
    int y = 0;
    if(!f || !f->ui || !st || !st->text || st->cap == 0) return;
    if(st->cursor >= st->cap) st->cursor = st->cap - 1;   /* Y1: untrusted cursor -> OOB */
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.focused){
        int j = 0;
        /* insert typed codepoints AT the cursor (mid-string, F1.3), whole
         * codepoints only; stop when one won't fit (text_insert_ is cap-bounded).
         * The edit helpers live in the widgets section, in scope via the unity
         * build. */
        while(j < ui->text_in_len){
            int n = utf8_lead_len((unsigned char)ui->text_in[j]);
            size_t m = (size_t)(n > 0 ? n : 1);     /* defensive: stray byte as 1 */
            if(j + (int)m > ui->text_in_len) m = (size_t)(ui->text_in_len - j);
            if(!text_insert_(st->text, st->cap, st->cursor, ui->text_in + j, m)) break;
            st->cursor += m; j += (int)m;
        }
        /* cursor movement (one step per frame — the key_in bitmask can't count
         * repeats; key auto-repeat delivers one per frame). */
        if(ui->key_in & TIMUI_KEYIN_LEFT)  st->cursor = utf8_drop_last(st->text, st->cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) st->cursor = utf8_next_(st->text, st->cursor, strlen(st->text));
        if(ui->key_in & TIMUI_KEYIN_HOME)  st->cursor = line_start_(st->text, st->cursor);
        if(ui->key_in & TIMUI_KEYIN_END)   st->cursor = line_end_(st->text, st->cursor);
        /* deletion: backspace removes the codepoint before the cursor, DELETE
         * the one at the cursor. */
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st->cursor > 0){
            size_t prev = utf8_drop_last(st->text, st->cursor);
            st->cursor = text_erase_(st->text, prev, st->cursor);
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(st->text, st->cursor, strlen(st->text));
            (void)text_erase_(st->text, st->cursor, nxt);
        }
        ui->text_in_len = 0;
        ui->key_in = 0;
    }
    { TimuiStyle sst = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
      content = timui_scroll_begin(f, r, st->scroll_y);
      i = 0;
      while(i < st->cap && st->text[i]){
          size_t ls = i;
          while(i < st->cap && st->text[i] && st->text[i] != '\n'){
              if(st->text[i] == '\r') break;  /* \r or \r\n line break */
              i++;
          }
          timui_draw_text(&ui->curr, content.x, content.y + y,
                          (TimuiStr){ st->text + ls, i - ls }, sst);
          if(i < st->cap && (st->text[i] == '\r' || st->text[i] == '\n')){
              if(st->text[i] == '\r' && i + 1 < st->cap && st->text[i+1] == '\n') i++;
              i++;
          }
          y++;
      }
      /* auto-scroll to keep the cursor visible (computed before scroll_begin next frame) */
      {  int cursor_row = 0;
         size_t ci;
         for(ci = 0; ci < st->cursor && ci < st->cap; ci++)
             if(st->text[ci] == '\n' || st->text[ci] == '\r'){
                 cursor_row++;
                 if(st->text[ci] == '\r' && ci + 1 < st->cap && st->text[ci+1] == '\n') ci++;
             }
         if(cursor_row < st->scroll_y) st->scroll_y = cursor_row;
         if(cursor_row >= st->scroll_y + r.h) st->scroll_y = cursor_row - r.h + 1;
         if(st->scroll_y < 0) st->scroll_y = 0;
      }
      timui_scroll_end(f);
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          int crow, ccol;
          text_pos_(st->text, st->cursor, &crow, &ccol);
          if(crow >= st->scroll_y && crow < st->scroll_y + r.h && ccol < r.w){
              ui->cursor_x = r.x + ccol;                /* content.x == r.x (vertical scroll only) */
              ui->cursor_y = r.y + (crow - st->scroll_y);
              ui->cursor_visible = 1;
          }
      }
    }
}
