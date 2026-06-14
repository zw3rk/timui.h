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
        int in_m = (ia->mouse_x >= ia->modal_rect.x && ia->mouse_x < ia->modal_rect.x + ia->modal_rect.w &&
                   ia->mouse_y >= ia->modal_rect.y && ia->mouse_y < ia->modal_rect.y + ia->modal_rect.h);
        if(!in_m) return res;
    }
    hover = (ia->mouse_x >= r.x && ia->mouse_x < r.x + r.w &&
             ia->mouse_y >= r.y && ia->mouse_y < r.y + r.h);
    if(hover) ia->hot = id;
    if(hover && ia->mouse_pressed){ ia->active = id; ia->focus = id; }
    res.hovered = hover;
    res.focused = (ia->focus == id);
    res.active  = (ia->active == id);
    res.pressed = res.active && ia->mouse_down;
    if(res.active && ia->mouse_released){
        res.clicked = 1;        /* released over the active widget */
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
    st = timui_theme_style(&ui->theme, slot);
    timui_draw_fill(&ui->curr, r, st);
    timui_draw_text(&ui->curr, r.x + 1, r.y + (r.h > 1 ? (r.h - 1) / 2 : 0), label, st);
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
    timui_draw_box(&ui->curr, r, border_flags, timui_theme_style(&ui->theme, TIMUI_SLOT_BORDER));
    timui_push_clip(f, r);   /* W10: clip title + body content to the panel rect */
    if(title.ptr && title.len)
        timui_draw_text(&ui->curr, r.x + 1, r.y, title, timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL_TITLE));
    body.x = r.x + 1; body.y = r.y + 1;
    body.w = r.w - 2; body.h = r.h - 2;
    if(body.w < 0) body.w = 0;
    if(body.h < 0) body.h = 0;
    timui_draw_fill(&ui->curr, body, timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL));
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
    st = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
    box[0] = is_radio ? '(' : '[';
    box[1] = value ? (is_radio ? 'o' : 'x') : ' ';
    box[2] = is_radio ? ')' : ']';
    box[3] = ' ';
    timui_draw_text(&ui->curr, r.x, r.y, (TimuiStr){ box, 4 }, st);
    timui_draw_text(&ui->curr, r.x + 4, r.y, label, timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT));
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
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_draw_fill(&ui->curr, r, timui_theme_style(&ui->theme, TIMUI_SLOT_STATUS));
    timui_draw_text(&ui->curr, r.x, r.y, text, timui_theme_style(&ui->theme, TIMUI_SLOT_STATUS));
}
/* ---- UTF-8 codepoint helpers (shared with timui_text_area) ------------- *
 * text_in carries UTF-8 (since the G8 fix), so text inputs must append and
 * delete whole codepoints — a byte-wise append splits a multibyte char at the
 * cap boundary, and a 1-byte backspace leaves a dangling lead byte. Both
 * corrupt the buffer into permanently invalid UTF-8. */

/* byte length of a well-formed UTF-8 sequence starting at lead byte b (1..4),
 * or 0 if b is not a lead. */
static int utf8_lead_len(unsigned char b){
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}
/* New length after removing one complete UTF-8 codepoint from the end of
 * buf[0..len): walk back over trailing continuation bytes (0x80-0xBF) to the
 * lead byte, then drop the lead. */
static size_t utf8_drop_last(const char *buf, size_t len){
    size_t i = len;
    while(i > 0 && ((unsigned char)buf[i - 1] & 0xC0) == 0x80) i--;
    if(i > 0) i--;
    return i;
}

TIMUI_API bool timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle st;
    size_t len;
    bool submitted = false;
    if(!f || !f->ui || !buf || cap == 0) return false;
    ui = f->ui;
    {
        int submit = ui->ia.activate_pressed;   /* capture before interact_button consumes it */
        ir = timui_interact_button(&ui->ia, id, r);   /* click to focus */
        len = strlen(buf);
        if(ir.focused){
            int i = 0;
            /* append whole UTF-8 codepoints; skip one that won't fit intact */
            while(i < ui->text_in_len){
                size_t m = (size_t)utf8_lead_len((unsigned char)ui->text_in[i]);
                if(m == 0) m = 1;                       /* defensive: stray byte */
                if(len + m >= cap) break;               /* no room for the codepoint + NUL */
                while(m-- > 0 && i < ui->text_in_len) buf[len++] = ui->text_in[i++];
            }
            buf[len] = '\0';
            if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && len > 0){
                len = utf8_drop_last(buf, len);         /* delete a whole codepoint */
                buf[len] = '\0';
            }
            if(submit) submitted = true;
            ui->text_in_len = 0;       /* consumed by the focused input */
            ui->key_in = 0;
        }
    }
    st = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
    timui_draw_fill(&ui->curr, r, st);
    timui_draw_text(&ui->curr, r.x, r.y, timui_str_from_cstr(buf), st);
    return submitted;
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
        st = timui_theme_style(&ui->theme, slot);
        timui_draw_fill(&ui->curr, TIMUI_RECT(r.x, r.y + i, r.w, 1), st);
        timui_draw_text(&ui->curr, r.x, r.y + i, timui_str_from_cstr(s), st);
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
    timui_label(f, bx + 2, by + 1, message, timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT));
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

