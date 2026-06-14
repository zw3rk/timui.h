/* ---- tree widget (v0.2) ----------------------------------------------- *
 * Renders a flat list of visible nodes (the app handles expand/collapse logic)
 * with depth indentation and expand markers. Selection via up/down + click. */
TIMUI_API void timui_tree(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int *selected){
    Timui *ui;
    int i;
    TimuiRect content;
    (void)id;
    if(!f || !f->ui || !nodes || count <= 0 || !selected) return;
    ui = f->ui;
    if(*selected < 0) *selected = 0;
    if(*selected >= count) *selected = count - 1;
    content = timui_scroll_begin(f, r, 0);
    for(i = 0; i < count; i++){
        int y = content.y + i;
        char prefix[32];
        int pn = 0, j;
        TimuiStyle st = timui_theme_style(&ui->theme,
            i == *selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        /* indentation + expand marker */
        for(j = 0; j < nodes[i].depth; j++){ prefix[pn++] = ' '; prefix[pn++] = ' '; }
        if(nodes[i].has_children) prefix[pn++] = nodes[i].expanded ? '-' : '+';
        else prefix[pn++] = ' ';
        prefix[pn++] = ' ';
        prefix[pn] = '\0';
        timui_draw_fill(&ui->curr, TIMUI_RECT(content.x, y, r.w, 1), st);
        timui_draw_text(&ui->curr, content.x, y, timui_str_from_cstr(prefix), st);
        timui_draw_text(&ui->curr, content.x + pn, y, timui_str_from_cstr(nodes[i].label), st);
    }
    timui_scroll_end(f);
    /* keyboard nav only when focused */
    { TimuiInteractResult ir2 = timui_interact_button(&ui->ia, id, r);
      if(ir2.focused){
        if(timui_key_pressed(f, TIMUI_KEY_UP) && *selected > 0) (*selected)--;
        else if(timui_key_pressed(f, TIMUI_KEY_DOWN) && *selected < count - 1) (*selected)++;
      }
    }
}
