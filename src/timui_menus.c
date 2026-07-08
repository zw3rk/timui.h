/* ---- menu bar + popups (T5.7) + menu focus (T4.5) -------------------- *
 * menu_bar_begin/end bracket the bar; menu_begin draws a header and toggles
 * its popup open on click (returning whether it is open); menu_item items draw
 * in the popup and report a click (which closes the menu). An outside click
 * (a press not on any header/item) closes any open menu. Z27: all state lives
 * in the caller-owned TimuiMenuBar (no hidden fields in Timui) — `open` persists
 * across frames; the layout cursor is frame-scoped, reset by menu_bar_begin. */
TIMUI_API void timui_menu_bar_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiRect r){
    Timui *ui;
    if(!f || !f->ui || !bar) return;
    ui = f->ui;
    bar->bar_x = r.x;
    bar->bar_y = r.y;
    bar->clicked = 0;
    timui_draw_fill(&ui->curr, r, timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_MENU, 0));
}
TIMUI_API int timui_menu_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect hdr;
    TimuiInteractResult ir;
    int is_open;
    if(!f || !f->ui || !bar) return 0;
    ui = f->ui;
    hdr = TIMUI_RECT(bar->bar_x, bar->bar_y, (int)label.len + 2, 1);
    ir = timui_interact_button(&ui->ia, id, hdr);
    if(ir.hovered && ui->ia.mouse_pressed) bar->clicked = 1;   /* press on header */
    if(ir.clicked) bar->open = (bar->open == id) ? 0 : id;     /* release toggles */
    is_open = (bar->open == id);
    {
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_MENU,
            is_open ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU,
            is_open ? TIMUI_STYLE_STATE_ACTIVE : 0);
        timui_draw_row_(&ui->curr, hdr, 1, label, st);
    }
    bar->bar_x += hdr.w;
    if(is_open){ bar->item_x = hdr.x; bar->item_y = bar->bar_y + 1; }
    return is_open;
}
TIMUI_API int timui_menu_item(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect r;
    TimuiInteractResult ir;
    if(!f || !f->ui || !bar) return 0;
    ui = f->ui;
    r = TIMUI_RECT(bar->item_x, bar->item_y, (int)label.len + 4, 1);
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.hovered && ui->ia.mouse_pressed) bar->clicked = 1;   /* press on item */
    {
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_MENU,
            ir.hovered ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU,
            ir.hovered ? TIMUI_STYLE_STATE_HOVERED : 0);
        timui_draw_row_(&ui->curr, r, 2, label, st);
    }
    bar->item_y++;
    if(ir.clicked){ bar->open = 0; return 1; }                 /* release selects */
    return 0;
}
TIMUI_API void timui_menu_end(TimuiFrame *f){ (void)f; }
TIMUI_API void timui_menu_bar_end(TimuiFrame *f, TimuiMenuBar *bar){
    Timui *ui;
    if(!f || !f->ui || !bar) return;
    ui = f->ui;
    if(bar->open && ui->ia.mouse_pressed && !bar->clicked) bar->open = 0;
}
