/* ---- menu bar + popups (T5.7) + menu focus (T4.5) -------------------- *
 * menu_bar_begin/end bracket the bar; menu_begin draws a header and toggles
 * its popup open on click (returning whether it is open); menu_item items draw
 * in the popup and report a click (which closes the menu). An outside click
 * (a press not on any header/item) closes any open menu. */
TIMUI_API void timui_menu_bar_begin(TimuiFrame *f, TimuiRect r){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    ui->menu_bar_x = r.x;
    ui->menu_bar_y = r.y;
    ui->menu_clicked = 0;
    timui_draw_fill(&ui->curr, r, timui_theme_style(&ui->theme, TIMUI_SLOT_MENU));
}
TIMUI_API int timui_menu_begin(TimuiFrame *f, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect hdr;
    TimuiInteractResult ir;
    int is_open;
    if(!f || !f->ui) return 0;
    ui = f->ui;
    hdr = TIMUI_RECT(ui->menu_bar_x, ui->menu_bar_y, (int)label.len + 2, 1);
    ir = timui_interact_button(&ui->ia, id, hdr);
    if(ir.hovered && ui->ia.mouse_pressed) ui->menu_clicked = 1;   /* press on header */
    if(ir.clicked) ui->open_menu = (ui->open_menu == id) ? 0 : id; /* release toggles */
    is_open = (ui->open_menu == id);
    {
        TimuiStyle st = timui_theme_style(&ui->theme, is_open ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU);
        timui_draw_fill(&ui->curr, hdr, st);
        timui_draw_text(&ui->curr, hdr.x + 1, hdr.y, label, st);
    }
    ui->menu_bar_x += hdr.w;
    if(is_open){ ui->menu_item_x = hdr.x; ui->menu_item_y = ui->menu_bar_y + 1; }
    return is_open;
}
TIMUI_API int timui_menu_item(TimuiFrame *f, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect r;
    TimuiInteractResult ir;
    if(!f || !f->ui) return 0;
    ui = f->ui;
    r = TIMUI_RECT(ui->menu_item_x, ui->menu_item_y, (int)label.len + 4, 1);
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.hovered && ui->ia.mouse_pressed) ui->menu_clicked = 1;   /* press on item */
    {
        TimuiStyle st = timui_theme_style(&ui->theme, ir.hovered ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU);
        timui_draw_fill(&ui->curr, r, st);
        timui_draw_text(&ui->curr, r.x + 2, r.y, label, st);
    }
    ui->menu_item_y++;
    if(ir.clicked){ ui->open_menu = 0; return 1; }                 /* release selects */
    return 0;
}
TIMUI_API void timui_menu_end(TimuiFrame *f){ (void)f; }
TIMUI_API void timui_menu_bar_end(TimuiFrame *f){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    if(ui->open_menu && ui->ia.mouse_pressed && !ui->menu_clicked) ui->open_menu = 0;
}
