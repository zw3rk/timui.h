/* ---- interaction state ------------------------------------------------ */
TIMUI_API void timui_interact_init(TimuiInteract *ia, const TimuiAllocator *alloc){
    if(!ia) return;
    ia->hot = ia->active = ia->focus = 0;
    ia->mouse_x = ia->mouse_y = 0;
    ia->mouse_down = ia->mouse_down_prev = 0;
    ia->mouse_pressed = ia->mouse_released = 0;
    ia->tab_pressed = ia->activate_pressed = 0;
    ia->tab_order = NULL;
    ia->tab_count = ia->tab_cap = 0;
    ia->alloc = alloc;          /* kept for growing tab_order on push (V24) */
    ia->focus_advance = 0;
    ia->modal_active = 0;
}
TIMUI_API void timui_interact_destroy(TimuiInteract *ia){
    if(!ia || !ia->tab_order || !ia->alloc) return;
    ia->alloc->free(ia->alloc->userdata, ia->tab_order, (size_t)ia->tab_cap * sizeof(TimuiId));
    ia->tab_order = NULL;
    ia->tab_cap = ia->tab_count = 0;
}
TIMUI_API void timui_interact_set_mouse(TimuiInteract *ia, int x, int y, int down){
    if(!ia) return;
    ia->mouse_x = x;
    ia->mouse_y = y;
    ia->mouse_down = down ? 1 : 0;
}
TIMUI_API void timui_interact_set_keys(TimuiInteract *ia, int tab, int activate){
    if(!ia) return;
    if(tab) ia->tab_pressed = 1;
    if(activate) ia->activate_pressed = 1;
}
TIMUI_API void timui_interact_begin(TimuiInteract *ia){
    if(!ia) return;
    ia->mouse_pressed  = ia->mouse_down && !ia->mouse_down_prev;
    ia->mouse_released = !ia->mouse_down && ia->mouse_down_prev;
    ia->mouse_down_prev = ia->mouse_down;
    ia->hot = 0;                 /* recomputed from this frame's submissions */
    ia->tab_count = 0;
    ia->focus_advance = ia->tab_pressed;
    ia->tab_pressed = 0;
    /* modal_active persists across frames; message_box re-asserts it each
     * frame it is called, and clears it on button click. When the caller
     * stops calling message_box, modal_active remains 1 — the caller must
     * set ui->ia.modal_active = 0 when dismissing the modal. */
}
TIMUI_API TimuiInteractResult timui_interact_button(TimuiInteract *ia, TimuiId id, TimuiRect r){
    TimuiInteractResult res = {0, 0, 0, 0, 0};
    int hover;
    if(!ia) return res;
    if(ia->modal_active){      /* modal focus trap: widgets behind the modal are inert */
        int in_m = timui_rect_contains_(ia->modal_rect, ia->mouse_x, ia->mouse_y);
        if(!in_m) return res;
    }
    hover = timui_rect_contains_(r, ia->mouse_x, ia->mouse_y);
    if(hover) ia->hot = id;
    if(hover && ia->mouse_pressed){ ia->active = id; ia->focus = id; }
    res.hovered = hover;
    res.focused = (ia->focus == id);
    res.active  = (ia->active == id);
    res.pressed = res.active && ia->mouse_down;
    if(res.active && ia->mouse_released){
        res.clicked = hover;    /* click only if released over the active widget */
        ia->active = 0;
    }
    if(res.focused && ia->activate_pressed){
        res.clicked = 1;        /* Enter/Space activates the focused widget */
        ia->activate_pressed = 0;
    }
    /* Register in the Tab cycle (dynamically grown, V24 — no fixed cap). */
    if(ia->tab_count == ia->tab_cap){
        int ncap = ia->tab_cap ? ia->tab_cap * 2 : 16;
        TimuiId *n = NULL;
        if(ia->alloc && (size_t)ncap <= SIZE_MAX / sizeof(TimuiId))
            n = (TimuiId *)ia->alloc->realloc(ia->alloc->userdata, ia->tab_order,
                (size_t)ia->tab_cap * sizeof(TimuiId), (size_t)ncap * sizeof(TimuiId));
        if(!n) return res;             /* OOM or no allocator: skip (focus still works via click) */
        ia->tab_order = n;
        ia->tab_cap = ncap;
    }
    ia->tab_order[ia->tab_count++] = id;
    return res;
}
TIMUI_API void timui_interact_end(TimuiInteract *ia){
    int i, idx;
    if(!ia || !ia->focus_advance || ia->tab_count == 0) return;
    idx = -1;
    for(i = 0; i < ia->tab_count; i++)
        if(ia->tab_order[i] == ia->focus){ idx = i; break; }
    ia->focus = ia->tab_order[(idx + 1) % ia->tab_count];
}

/* ---- widgets ---------------------------------------------------------- */
static void widget_draw_text_clipped(TimuiFrame *f, TimuiRect clip, int x, int y,
                                     TimuiStr text, TimuiStyle style){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_push_clip(f, clip);
    timui_draw_text(&ui->curr, x, y, text, style);
    timui_pop_clip(f);
}

TIMUI_API TimuiButtonResult timui_button(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label){
    TimuiButtonResult br = {false, false, false, false};
    TimuiInteractResult ir;
    TimuiStyleSlot slot;
    TimuiStyle st;
    Timui *ui;
    if(!f || !f->ui) return br;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    br.clicked = ir.clicked;
    br.pressed = ir.pressed;
    br.hovered = ir.hovered;
    br.focused = ir.focused;
    slot = ir.active ? TIMUI_SLOT_BUTTON_ACTIVE
          : ir.hovered ? TIMUI_SLOT_BUTTON_HOVERED
          : ir.focused ? TIMUI_SLOT_BUTTON_FOCUSED
          : TIMUI_SLOT_BUTTON;
    st = timui_widget_style_(ui, TIMUI_WIDGET_BUTTON, slot,
                             (ir.active ? TIMUI_STYLE_STATE_ACTIVE : 0) |
                             (ir.hovered ? TIMUI_STYLE_STATE_HOVERED : 0) |
                             (ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0));
    timui_draw_fill(&ui->curr, r, st);
    widget_draw_text_clipped(f, r, r.x + 1, r.y + (r.h > 1 ? (r.h - 1) / 2 : 0), label, st);
    return br;
}
TIMUI_API void timui_label(TimuiFrame *f, int x, int y, TimuiStr text, TimuiStyle style){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_draw_text(&ui->curr, x, y, text, style);
}
TIMUI_API TimuiRect timui_panel_begin(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr title, uint32_t border_flags){
    TimuiRect body = {0, 0, 0, 0};
    Timui *ui;
    (void)id;
    if(!f || !f->ui) return body;
    ui = f->ui;
    timui_draw_box(&ui->curr, r, border_flags,
                   timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_BORDER, 0));
    timui_push_clip(f, r);   /* W10: clip title + body content to the panel rect */
    if(title.ptr && title.len)
        timui_draw_text(&ui->curr, r.x + 1, r.y, title,
                        timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_PANEL_TITLE, 0));
    body.x = r.x + 1; body.y = r.y + 1;
    body.w = r.w - 2; body.h = r.h - 2;
    if(body.w < 0) body.w = 0;
    if(body.h < 0) body.h = 0;
    timui_draw_fill(&ui->curr, body,
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_PANEL, 0));
    return body;
}
TIMUI_API void timui_panel_end(TimuiFrame *f){ if(f) timui_pop_clip(f); }
static TimuiBoolEdit bool_widget(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label,
                                 bool value, int is_radio){
    TimuiBoolEdit be = {false, value, false, false};
    TimuiInteractResult ir;
    Timui *ui;
    TimuiStyle st;
    char box[4];
    if(!f || !f->ui) return be;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    be.hovered = ir.hovered;
    be.focused = ir.focused;
    if(ir.clicked){
        be.changed = true;
        be.value = is_radio ? true : !value;   /* radio selects; checkbox toggles */
    }
    st = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                             ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                             ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
    box[0] = is_radio ? '(' : '[';
    box[1] = value ? (is_radio ? 'o' : 'x') : ' ';
    box[2] = is_radio ? ')' : ']';
    box[3] = ' ';
    timui_push_clip(f, r);
    timui_draw_text(&ui->curr, r.x, r.y, (TimuiStr){ box, 4 }, st);
    timui_draw_text(&ui->curr, r.x + 4, r.y, label,
                    timui_widget_style_(ui, TIMUI_WIDGET_INPUT, TIMUI_SLOT_TEXT, 0));
    timui_pop_clip(f);
    return be;
}
TIMUI_API TimuiBoolEdit timui_checkbox(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool value){
    return bool_widget(f, id, r, label, value, 0);
}
TIMUI_API bool timui_checkbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool *value){
    TimuiBoolEdit be;
    if(!value) return false;
    be = timui_checkbox(f, id, r, label, *value);
    if(be.changed) *value = be.value;
    return *value;
}
TIMUI_API TimuiBoolEdit timui_radio(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool selected){
    return bool_widget(f, id, r, label, selected, 1);
}
TIMUI_API void timui_function_bar(TimuiFrame *f, TimuiRect r, TimuiStr text){
    Timui *ui;
    TimuiStyle st;
    if(!f || !f->ui) return;
    ui = f->ui;
    st = timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_STATUS, 0);
    timui_draw_row_(&ui->curr, r, 0, text, st);
}
/* ---- UTF-8 / grapheme edit helpers (shared with timui_text_area) -------- *
 * text_in carries UTF-8 (since the G8 fix), so text inputs must append and
 * delete whole clusters — a byte-wise append can split a multibyte char at the
 * cap boundary, and a codepoint-wise backspace can leave a dangling skin-tone
 * modifier, variation selector, or joiner sequence. */

/* byte length of a well-formed UTF-8 sequence starting at lead byte b (1..4),
 * or 0 if b is not a lead. */
static int utf8_lead_len(unsigned char b){
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}
static int single_line_text_byte_(unsigned char b){
    return b >= 0x20 && b != 0x7f;
}
/* New length after removing one complete grapheme cluster from the end of
 * buf[0..len). The state structs keep byte cursors, so callers still pass and
 * receive byte offsets. */
static size_t utf8_drop_last(const char *buf, size_t len){
    return timui_grapheme_prev(buf, len, len);
}
/* ---- in-line editing primitives (F1.2) --------------------------------- *
 * All operate on a NUL-terminated buffer; utf8_drop_last(buf, cursor) already
 * gives the previous grapheme boundary (Left / Backspace). */

/* Byte offset after the grapheme at `cursor`, clamped to len (Right / Delete). */
static size_t utf8_next_(const char *buf, size_t cursor, size_t len){
    if(cursor >= len) return len;
    return timui_grapheme_next(buf, len, cursor);
}
/* Start of the line containing `pos` (after the preceding \n/\r, or 0). */
static size_t line_start_(const char *buf, size_t pos){
    while(pos > 0 && buf[pos - 1] != '\n' && buf[pos - 1] != '\r') pos--;
    return pos;
}
/* End of the line containing `pos` (before the next \n/\r, or end). */
static size_t line_end_(const char *buf, size_t pos){
    size_t len = strlen(buf);
    while(pos < len && buf[pos] != '\n' && buf[pos] != '\r') pos++;
    return pos;
}
/* Insert `n` bytes at byte offset `at`. Returns 1 on success, 0 if it won't fit
 * (len + n + 1 > cap). The tail (incl. the NUL) is shifted right. Callers pass
 * whole codepoints so nothing is split at the UTF-8 byte boundary. */
static int text_insert_(char *buf, size_t cap, size_t at, const char *bytes, size_t n){
    size_t len = strlen(buf);
    if(at > len) at = len;
    if(len + n + 1 > cap) return 0;
    memmove(buf + at + n, buf + at, len - at + 1);   /* +1 also moves the NUL */
    memcpy(buf + at, bytes, n);
    return 1;
}
/* Erase byte range [from, to). Returns the new cursor (= clamped `from`). */
static size_t text_erase_(char *buf, size_t from, size_t to){
    size_t len = strlen(buf);
    if(from > len) from = len;
    if(to > len) to = len;
    if(to <= from) return from;
    memmove(buf + from, buf + to, len - to + 1);     /* +1 also moves the NUL */
    return from;
}
static size_t text_len_bounded_(const char *buf, size_t cap){
    size_t len = 0;
    while(len < cap && buf[len]) len++;
    return len;
}

TIMUI_API bool timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle st;
    size_t len;
    bool submitted = false;
    if(!f || !f->ui || !buf || cap == 0) return false;
    ui = f->ui;
    len = text_len_bounded_(buf, cap);
    if(len >= cap){
        len = cap - 1;
        buf[len] = '\0';
    }
    {
        int submit = ui->ia.activate_pressed;   /* capture before interact_button consumes it */
        ir = timui_interact_button(&ui->ia, id, r);   /* click to focus */
        if(ir.focused){
            int i = 0;
            /* append whole UTF-8 codepoints; skip one that won't fit intact */
            while(i < ui->text_in_len){
                size_t m = (size_t)utf8_lead_len((unsigned char)ui->text_in[i]);
                if(!single_line_text_byte_((unsigned char)ui->text_in[i])){ i++; continue; }
                if(m == 0) m = 1;                       /* defensive: stray byte */
                if(len + m >= cap) break;               /* no room for the codepoint + NUL */
                while(m-- > 0 && i < ui->text_in_len) buf[len++] = ui->text_in[i++];
            }
            buf[len] = '\0';
            if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && len > 0){
                len = utf8_drop_last(buf, len);         /* delete a whole cluster */
                buf[len] = '\0';
            }
            if(submit) submitted = true;
            ui->text_in_len = 0;       /* consumed by the focused input */
            ui->key_in = 0;
        }
    }
    st = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                             ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                             ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
    timui_draw_fill(&ui->curr, r, st);
    widget_draw_text_clipped(f, r, r.x, r.y, (TimuiStr){ buf, len }, st);
    return submitted;
}
/* Display column of the cursor: sum of grapheme widths over buf[0..upto) (F1.5). */
static int display_col_(const char *buf, size_t upto){
    size_t i = 0, len = strlen(buf);
    int col = 0;
    if(upto > len) upto = len;
    while(i < upto){
        size_t n = timui_grapheme_next(buf, len, i);
        if(n <= i) n = i + 1;
        if(n > upto) n = upto;
        col += timui_grapheme_width(buf + i, n - i);
        i = n;
    }
    return col;
}
/* Row (0-based, split on \n / \r / \r\n) and display column of the cursor,
 * for placing the hardware cursor in a multi-line editor (F1.4). */
static void text_pos_(const char *buf, size_t cursor, int *out_row, int *out_col){
    size_t i = 0, len = strlen(buf), line_start = 0;
    int row = 0;
    if(cursor > len) cursor = len;
    while(i < cursor){
        if(buf[i] == '\n' || buf[i] == '\r'){
            if(buf[i] == '\r' && i + 1 < cursor && buf[i + 1] == '\n') i++;
            row++; i++; line_start = i;
        } else i++;
    }
    *out_row = row;
    *out_col = display_col_(buf + line_start, cursor - line_start);
}
static void input_field_insert_span_(TimuiInputState *st, const char *src, int nbytes){
    int j = 0;
    while(st && src && j < nbytes){
        int n = utf8_lead_len((unsigned char)src[j]);
        size_t m = (size_t)(n > 0 ? n : 1);
        if(!single_line_text_byte_((unsigned char)src[j])){ j++; continue; }
        if(j + (int)m > nbytes) m = (size_t)(nbytes - j);
        if(!text_insert_(st->text, st->cap, st->cursor, src + j, m)) break;
        st->cursor += m;
        j += (int)m;
    }
}
static void input_field_apply_key_(TimuiInputState *st, unsigned key){
    size_t len;
    if(!st || !st->text) return;
    len = strlen(st->text);
    if(key == TIMUI_KEYIN_LEFT)  st->cursor = utf8_drop_last(st->text, st->cursor);
    if(key == TIMUI_KEYIN_RIGHT) st->cursor = utf8_next_(st->text, st->cursor, len);
    if(key == TIMUI_KEYIN_HOME)  st->cursor = 0;
    if(key == TIMUI_KEYIN_END)   st->cursor = len;
    if(key == TIMUI_KEYIN_BACKSPACE && st->cursor > 0){
        size_t prev = utf8_drop_last(st->text, st->cursor);
        st->cursor = text_erase_(st->text, prev, st->cursor);
    }
    if(key == TIMUI_KEYIN_DELETE){
        size_t nxt = utf8_next_(st->text, st->cursor, strlen(st->text));
        (void)text_erase_(st->text, st->cursor, nxt);
    }
    if(key == TIMUI_KEYIN_KILL_EOL){
        st->text[st->cursor] = '\0';
    }
    if(key == TIMUI_KEYIN_KILL_BOL){
        size_t rest = strlen(st->text + st->cursor);
        memmove(st->text, st->text + st->cursor, rest + 1);
        st->cursor = 0;
    }
    if(key == TIMUI_KEYIN_KILL_WORD){
        size_t c = st->cursor, w = c;
        while(w > 0 && st->text[w-1] == ' ') w--;
        while(w > 0 && st->text[w-1] != ' ') w--;
        memmove(st->text + w, st->text + c, strlen(st->text + c) + 1);
        st->cursor = w;
    }
}
static bool input_field_core(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiInputState *st,
                             const TimuiStyle *ovr){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle style;
    bool submitted = false;
    if(!f || !f->ui || !st || !st->text || st->cap == 0) return false;
    ui = f->ui;
    { size_t text_len = text_len_bounded_(st->text, st->cap);
      if(text_len >= st->cap){ text_len = st->cap - 1; st->text[text_len] = '\0'; }
      if(st->cursor > text_len) st->cursor = text_len; }   /* Y1-style: distrust caller cursor */
    {
        ir = timui_interact_button(&ui->ia, id, r);
        if(ir.focused){
            int oi;
            for(oi = 0; oi < ui->edit_count; oi++){
                TimuiEditOp *op = &ui->edit_ops[oi];
                if(op->kind == TIMUI_EDIT_TEXT){
                    input_field_insert_span_(st, ui->text_in + op->start, op->len);
                } else if(op->kind == TIMUI_EDIT_KEY && op->key == TIMUI_EDIT_KEY_ENTER_){
                    submitted = true;
                    timui_defer_edit_ops_after_(ui, oi + 1);
                    break;
                } else if(op->kind == TIMUI_EDIT_KEY){
                    input_field_apply_key_(st, op->key);
                }
            }
            ui->text_in_len = 0;
            ui->enter_count = 0;
            ui->edit_count = 0;
            ui->key_in = 0;
        }
    }
    /* horizontal scroll: keep the cursor column within [scroll_x, scroll_x+w) */
    { int ccol = display_col_(st->text, st->cursor);
      if(ccol < st->scroll_x) st->scroll_x = ccol;
      if(r.w > 0 && ccol >= st->scroll_x + r.w) st->scroll_x = ccol - r.w + 1;
      if(st->scroll_x < 0) st->scroll_x = 0;
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          ui->cursor_x = r.x + (ccol - st->scroll_x);
          ui->cursor_y = r.y;
          ui->cursor_visible = 1;
      }
    }
    style = ovr ? *ovr
                : timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                                      ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                                      ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
    timui_draw_fill(&ui->curr, r, style);
    /* clip to the field and shift the text left by scroll_x so the visible
     * window tracks the cursor (put_glyph drops the clipped leading columns). */
    timui_push_clip(f, r);
    timui_draw_text(&ui->curr, r.x - st->scroll_x, r.y, timui_str_from_cstr(st->text), style);
    timui_pop_clip(f);
    return submitted;
}
/* Themed single-line editor (INPUT/INPUT_FOCUSED slots). */
TIMUI_API bool timui_input_field(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiInputState *st){
    return input_field_core(f, id, r, st, NULL);
}
/* Same, but drawn with a caller-supplied `style` (e.g. to blend the field into a
 * surrounding panel instead of the themed input box). Editing/cursor behaviour
 * is identical. */
TIMUI_API bool timui_input_field_styled(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        TimuiInputState *st, TimuiStyle style){
    return input_field_core(f, id, r, st, &style);
}
TIMUI_API TimuiListResult timui_listbox(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        TimuiListState state, int count, TimuiLabelFn label, void *userdata){
    TimuiListResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int orig, i, visible;
    res.state_changed = 0; res.activated = 0; res.focused = 0;
    res.state = state; res.selected = state.selected;
    if(!f || !f->ui || count < 0) return res;
    ui = f->ui;
    /* Y3: clamp selection into range (siblings tree/table do this) so a stale
     * or mis-seeded selected (e.g. after the list shrinks) self-heals. */
    if(count == 0) state.selected = 0;
    else{ if(state.selected < 0) state.selected = 0; if(state.selected >= count) state.selected = count - 1; }
    orig = state.selected;
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        if((ui->key_in & TIMUI_KEYIN_UP) && state.selected > 0) state.selected--;
        if((ui->key_in & TIMUI_KEYIN_DOWN) && state.selected < count - 1) state.selected++;
    }
    visible = r.h > 0 ? r.h : 0;
    if(state.scroll < 0) state.scroll = 0;
    if(state.selected < state.scroll) state.scroll = state.selected;
    if(visible > 0 && state.selected >= state.scroll + visible) state.scroll = state.selected - visible + 1;
    if(state.scroll < 0) state.scroll = 0;
    /* upper-bound scroll so it can't outrun the list tail (keeps trailing
     * viewport rows filled instead of leaving an unstyled gap). */
    if(count <= visible) state.scroll = 0;
    else if(state.scroll > count - visible) state.scroll = count - visible;
    if(ir.clicked){
        int my = ui->ia.mouse_y - r.y;
        int idx = state.scroll + my;
        if(idx >= 0 && idx < count){ state.selected = idx; res.activated = 1; }
    }
    for(i = 0; i < visible; i++){
        int idx = state.scroll + i;
        TimuiStyleSlot slot;
        TimuiStyle st;
        const char *s;
        if(idx >= count) break;
        s = label ? label(userdata, idx) : "";
        slot = (idx == state.selected) ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT;
        st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX, slot,
                                 idx == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
        timui_draw_row_(&ui->curr, TIMUI_RECT(r.x, r.y + i, r.w, 1), 0, timui_str_from_cstr(s), st);
    }
    if(state.selected != orig) res.state_changed = 1;
    res.state = state;
    res.selected = state.selected;
    return res;
}
TIMUI_API TimuiListResult timui_listbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                            TimuiListState *state, int count, TimuiLabelFn label, void *userdata){
    TimuiListResult res;
    TimuiListState empty = {0, 0};
    if(!state) return timui_listbox(f, id, r, empty, count, label, userdata);
    res = timui_listbox(f, id, r, *state, count, label, userdata);
    if(res.state_changed) *state = res.state;
    return res;
}
TIMUI_API int timui_message_box(TimuiFrame *f, TimuiId id, TimuiRect parent,
                                TimuiStr title, TimuiStr message,
                                const TimuiStr *buttons, int count){
    Timui *ui;
    int i, boxw, boxh, bx, by, btnx, clicked = -1;
    if(!f || !f->ui || count <= 0) return -1;
    ui = f->ui;
    boxw = (int)message.len + 4;
    { int btnw = 0; for(i = 0; i < count; i++) btnw += (int)buttons[i].len + 4; if(btnw > boxw) boxw = btnw; }
    if(boxw < 10) boxw = 10;
    if(boxw > parent.w - 2) boxw = parent.w - 2;   /* never exceed the parent */
    if(boxw < 2) boxw = 2;                         /* floor: never a negative/zero width */
    boxh = 5;
    if(boxh > parent.h - 2) boxh = parent.h - 2;
    if(boxh < 3) boxh = 3;
    bx = parent.x + (parent.w - boxw) / 2;
    by = parent.y + (parent.h - boxh) / 2;
    ui->ia.modal_active = 1;
    ui->ia.modal_rect = TIMUI_RECT(bx, by, boxw, boxh);
    timui_panel_begin(f, id, TIMUI_RECT(bx, by, boxw, boxh), title, TIMUI_BORDER_DOUBLE);
    timui_label(f, bx + 2, by + 1, message,
                timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_TEXT, 0));
    btnx = bx + 2;
    { int any_btn = 0;
      for(i = 0; i < count; i++){
        int maxw = (bx + boxw) - btnx;            /* remaining width inside the box */
        int w = (int)buttons[i].len + 2;
        TimuiRect br;
        if(maxw < 3) break;                        /* no room for even a minimal button */
        if(w > maxw) w = maxw;                      /* W3: clamp so the button fits+renders */
        br = TIMUI_RECT(btnx, by + boxh - 2, w, 1);
        if(timui_button(f, id + (TimuiId)(i + 1), br, buttons[i]).clicked){ clicked = i; ui->ia.modal_active = 0; }
        btnx += w + 1;
        any_btn = 1;
      }
      /* W3 residual: if the parent is so narrow that NO button could render,
       * don't pin modal_active — an undismissable modal would trap all input. */
      if(!any_btn) ui->ia.modal_active = 0;
    }
    timui_panel_end(f);   /* pop the clip panel_begin pushed */
    return clicked;
}
TIMUI_API void timui_label_hyperlink(TimuiFrame *f, int x, int y, TimuiStr text, const char *uri, TimuiStyle style){
    Timui *ui;
    uint32_t id;
    if(!f || !f->ui) return;
    ui = f->ui;
    id = uri ? timui_hyperlink_set(&ui->curr, uri) : 0;
    timui_draw_text_linked(&ui->curr, x, y, text, style, id);
}
