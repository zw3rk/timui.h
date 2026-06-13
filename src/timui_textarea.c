/* ---- text-area widget (v0.2) ------------------------------------------ *
 * A multi-line text editor. Click to focus, type to insert, backspace deletes.
 * Lines are split on '\n'. (Cursor movement beyond append-at-end is future.) */
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *st){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect content;
    size_t i;
    int y = 0;
    if(!f || !f->ui || !st || !st->text) return;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.focused){
        int j;
        for(j = 0; j < ui->text_in_len && st->cursor + 1 < st->cap; j++)
            st->text[st->cursor++] = ui->text_in[j];
        st->text[st->cursor] = '\0';
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st->cursor > 0)
            st->text[--st->cursor] = '\0';
        ui->text_in_len = 0;
        ui->key_in = 0;
    }
    { TimuiStyle sst = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
      content = timui_scroll_begin(f, r, st->scroll_y);
      i = 0;
      while(i < st->cap && st->text[i]){
          size_t ls = i;
          while(i < st->cap && st->text[i] && st->text[i] != '\n') i++;
          timui_draw_text(&ui->curr, content.x, content.y + y,
                          (TimuiStr){ st->text + ls, i - ls }, sst);
          if(i < st->cap && st->text[i] == '\n') i++;
          y++;
      }
      /* auto-scroll to keep the cursor visible */
      {  int cursor_row = 0;
         size_t ci;
         for(ci = 0; ci < st->cursor && ci < st->cap; ci++) if(st->text[ci] == '\n') cursor_row++;
         if(cursor_row < st->scroll_y) st->scroll_y = cursor_row;
         if(cursor_row >= st->scroll_y + r.h) st->scroll_y = cursor_row - r.h + 1;
         if(st->scroll_y < 0) st->scroll_y = 0;
      }
      timui_scroll_end(f);
    }
}
