/* ---- toast / notification renderer ------------------------------------ *
 * Immediate-mode, caller-owned notifications. The widget only reports which
 * original item was clicked for dismissal; callers decide whether to remove it. */
static int toast_alive_(const TimuiToast *t, uint64_t now_ms){
    if(!t || t->dismissed) return 0;
    if(t->ttl_ms == 0) return 1;
    if(now_ms < t->created_ms) return 1;
    return now_ms - t->created_ms < t->ttl_ms;
}
static TimuiStyle toast_style_(Timui *ui, TimuiToastSeverity severity){
    TimuiStyleSlot slot = TIMUI_SLOT_STATUS;
    if(severity == TIMUI_TOAST_SUCCESS) slot = TIMUI_SLOT_SUCCESS;
    else if(severity == TIMUI_TOAST_WARNING) slot = TIMUI_SLOT_WARNING;
    else if(severity == TIMUI_TOAST_ERROR) slot = TIMUI_SLOT_ERROR;
    return timui_widget_style_(ui, TIMUI_WIDGET_TOAST, slot, 0);
}
TIMUI_API TimuiToastResult timui_toasts(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        const TimuiToast *toasts, int count,
                                        uint64_t now_ms){
    TimuiToastResult res;
    Timui *ui;
    int i, y;
    res.dismissed = -1;
    res.visible_count = 0;
    if(!f || !f->ui || !toasts || count <= 0 || r.w <= 0 || r.h <= 0) return res;
    ui = f->ui;
    y = r.y;
    timui_push_clip(f, r);
    for(i = 0; i < count; i++){
        TimuiRect tr;
        TimuiStyle st;
        TimuiInteractResult ir;
        if(!toast_alive_(&toasts[i], now_ms)) continue;
        if(y + 3 > r.y + r.h) break;
        tr = TIMUI_RECT(r.x, y, r.w, 3);
        st = toast_style_(ui, toasts[i].severity);
        ir = timui_interact_button(&ui->ia, id + (TimuiId)(i + 1), tr);
        if(ir.clicked && ui->ia.mouse_released) res.dismissed = i;
        timui_draw_fill(&ui->curr, tr,
                        timui_widget_style_(ui, TIMUI_WIDGET_TOAST, TIMUI_SLOT_PANEL, 0));
        timui_draw_box(&ui->curr, tr, TIMUI_BORDER_ROUND, st);
        widget_draw_text_clipped(f, tr, tr.x + 2, tr.y, toasts[i].title, st);
        widget_draw_text_clipped(f, tr, tr.x + 2, tr.y + 1, toasts[i].message,
                                 timui_widget_style_(ui, TIMUI_WIDGET_TOAST, TIMUI_SLOT_TEXT, 0));
        if(tr.w >= 5)
            timui_draw_text(&ui->curr, tr.x + tr.w - 4, tr.y, TIMUI_STR_LIT("[x]"), st);
        res.visible_count++;
        y += 3;
    }
    timui_pop_clip(f);
    return res;
}
