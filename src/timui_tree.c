/* ---- tree widget (v0.2) ----------------------------------------------- *
 * Renders a flat list of visible nodes (the app handles expand/collapse logic)
 * with depth indentation and expand markers. Selection via up/down + click.
 * Controlled: takes `selected` by value and returns the new selection in the
 * result; timui_tree_mut is the write-back convenience twin. */
TIMUI_API TimuiTreeResult timui_tree(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int selected){
    TimuiTreeResult res;
    Timui *ui;
    int i, orig;
    TimuiRect content;
    res.selected = selected; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !nodes || count <= 0) return res;
    ui = f->ui;
    if(selected < 0) selected = 0;
    if(selected >= count) selected = count - 1;
    orig = selected;                       /* post-clamp: a pure clamp is not a change */
    content = timui_scroll_begin(f, r, 0);
    for(i = 0; i < count; i++){
        int y = content.y + i;
        char prefix[64];
        int pn = 0, j;
        TimuiStyle st = timui_theme_style(&ui->theme,
            i == selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        /* indentation + expand marker. depth is app-supplied and unchecked, so
         * bound the loop to the buffer (reserve marker + space + NUL). */
        for(j = 0; j < nodes[i].depth && pn + 4 < (int)sizeof(prefix); j++){
            prefix[pn++] = ' '; prefix[pn++] = ' ';
        }
        if(nodes[i].has_children) prefix[pn++] = nodes[i].expanded ? '-' : '+';
        else prefix[pn++] = ' ';
        prefix[pn++] = ' ';
        prefix[pn] = '\0';
        timui_draw_row_(&ui->curr, TIMUI_RECT(content.x, y, r.w, 1), 0, timui_str_from_cstr(prefix), st);
        timui_draw_text(&ui->curr, content.x + pn, y, timui_str_from_cstr(nodes[i].label), st);
    }
    timui_scroll_end(f);
    /* keyboard nav only when focused */
    { TimuiInteractResult ir2 = timui_interact_button(&ui->ia, id, r);
      res.focused = ir2.focused;
      if(ir2.focused) selected = timui_updown_nav_(f, selected, count);
    }
    res.selected = selected;
    res.state_changed = (selected != orig);
    return res;
}
TIMUI_API TimuiTreeResult timui_tree_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int *selected){
    TimuiTreeResult res = timui_tree(f, id, r, nodes, count, selected ? *selected : 0);
    if(selected && res.state_changed) *selected = res.selected;   /* write back only on a real change */
    return res;
}
