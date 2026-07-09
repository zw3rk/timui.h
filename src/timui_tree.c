/* ---- tree widget (v0.2 + scroll) -------------------------------------- *
 * Two families:
 *   - timui_tree / timui_tree_mut : renders a flat list of visible nodes the app
 *     already flattened (unchanged; backward-compatible).
 *   - timui_tree_flatten + timui_tree_scroll / _mut : for LARGE trees — pass the
 *     FULL DFS node list (with expanded flags); the pure flattener yields the
 *     VISIBLE indices, and the widget windows them to the viewport.
 *
 * The visibility rule (a collapsed node hides its deeper subtree) lives in ONE
 * place — timui_tree_step_ — shared by the pure flattener (unit-tested in
 * tests/test_grid.c) and the scrollable widget, so the two can never drift.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* Sentinel "nothing hidden" depth watermark: any real node depth is below it. */
#define TIMUI_TREE_NOHIDE (1 << 30)

/* Step the visibility watermark past node `n` and report whether it is visible.
 * `hidden` holds the depth of the nearest enclosing COLLAPSED node: any node
 * deeper than that is a hidden descendant. A visible collapsed node re-arms the
 * watermark to its own depth; a visible expanded/leaf node clears it. */
static int timui_tree_step_(const TimuiTreeNode *n, int *hidden){
    int d = n->depth;
    if(d > *hidden) return 0;                                  /* hidden descendant */
    *hidden = (n->has_children && !n->expanded) ? d : TIMUI_TREE_NOHIDE;
    return 1;                                                  /* visible */
}

TIMUI_API int timui_tree_flatten(const TimuiTreeNode *nodes, int count, int *out, int cap){
    int i, hidden = TIMUI_TREE_NOHIDE, n = 0;
    if(!nodes || count <= 0) return 0;
    for(i = 0; i < count; i++){
        if(timui_tree_step_(&nodes[i], &hidden)){
            if(out && n < cap) out[n] = i;
            n++;                                               /* count even past cap */
        }
    }
    return n;
}

/* Draw one visible node (indent + expand marker + label) into row `y`. Shared by
 * the scroll widget; mirrors the prefix logic of the plain timui_tree below. */
static void timui_tree_draw_node_(Timui *ui, const TimuiTreeNode *node,
                                  TimuiRect r, int y, TimuiStyle st){
    char prefix[64];
    int pn = 0, j;
    /* indentation + expand marker. depth is app-supplied and unchecked, so bound
     * the loop to the buffer (reserve marker + space + NUL). */
    for(j = 0; j < node->depth && pn + 4 < (int)sizeof(prefix); j++){
        prefix[pn++] = ' '; prefix[pn++] = ' ';
    }
    if(node->has_children) prefix[pn++] = node->expanded ? '-' : '+';
    else                   prefix[pn++] = ' ';
    prefix[pn++] = ' ';
    prefix[pn] = '\0';
    timui_draw_row_(&ui->curr, TIMUI_RECT(r.x, y, r.w, 1), 0, timui_str_from_cstr(prefix), st);
    timui_draw_text(&ui->curr, r.x + pn, y, timui_str_from_cstr(node->label), st);
}

/* ----------------------------------------------------------------------- */
/* timui_tree / timui_tree_mut — flat visible list (UNCHANGED).              */
/* ----------------------------------------------------------------------- */
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
    /* Keyboard nav only when focused — process before drawing so the visible
     * selection and returned value cannot diverge for a frame. */
    { TimuiInteractResult ir2 = timui_interact_button(&ui->ia, id, r);
      res.focused = ir2.focused;
      if(ir2.focused) selected = timui_updown_nav_(f, selected, count);
    }
    content = timui_scroll_begin(f, r, 0);
    for(i = 0; i < count; i++){
        int y = content.y + i;
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TREE,
            i == selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
            i == selected ? TIMUI_STYLE_STATE_SELECTED : 0);
        timui_tree_draw_node_(ui, &nodes[i], TIMUI_RECT(content.x, y, r.w, 1), y, st);
    }
    timui_scroll_end(f);
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

/* ----------------------------------------------------------------------- */
/* timui_tree_scroll — full tree, windowed to the viewport.                  */
/* `selected`/`scroll` are positions in the VISIBLE list (0-based).          */
/* ----------------------------------------------------------------------- */
TIMUI_API TimuiTreeScrollResult timui_tree_scroll(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState state){
    TimuiTreeScrollResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int hidden, nvis, vis, sel, scroll, orig_sel, i, vp, wh;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !nodes || count <= 0) return res;
    ui = f->ui;

    /* Pass 1: count the visible nodes (so scroll/selection can be clamped). */
    hidden = TIMUI_TREE_NOHIDE; nvis = 0;
    for(i = 0; i < count; i++) if(timui_tree_step_(&nodes[i], &hidden)) nvis++;

    vis = r.h > 0 ? r.h : 1;
    sel = state.selected;
    if(nvis <= 0) sel = 0;
    else { if(sel < 0) sel = 0; if(sel >= nvis) sel = nvis - 1; }
    orig_sel = sel;

    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused) sel = timui_updown_nav_(f, sel, nvis);

    scroll = state.scroll < 0 ? 0 : state.scroll;
    wh = timui_mouse_wheel(f);
    if(wh && timui_mouse_wheel_over_(ui, r)){
        scroll -= wh;
        scroll = timui_page_slice(nvis, vis, scroll).first;
        if(sel < scroll) sel = scroll;
        if(sel >= scroll + vis) sel = scroll + vis - 1;
        if(nvis > 0){ if(sel >= nvis) sel = nvis - 1; } else sel = 0;
        if(sel < 0) sel = 0;
    }
    if(ir.clicked){
        int my  = ui->ia.mouse_y - r.y;
        int idx = scroll + my;
        if(my >= 0 && idx >= 0 && idx < nvis) sel = idx;
    }

    /* Keep the selection visible, then clamp the window to a valid range. */
    scroll = timui_scroll_to(sel, scroll, vis, nvis);
    scroll = timui_page_slice(nvis, vis, scroll).first;

    /* Pass 2: draw the visible nodes that land inside [scroll, scroll+vis). */
    timui_push_clip(f, r);
    hidden = TIMUI_TREE_NOHIDE; vp = 0;
    for(i = 0; i < count; i++){
        int row;
        if(!timui_tree_step_(&nodes[i], &hidden)) continue;
        row = vp - scroll;
        if(row >= 0 && row < vis){
            TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TREE,
                vp == sel ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                vp == sel ? TIMUI_STYLE_STATE_SELECTED : 0);
            timui_tree_draw_node_(ui, &nodes[i], r, r.y + row, st);
        }
        vp++;
    }
    timui_pop_clip(f);

    state.selected = sel;
    state.scroll   = scroll;
    res.state = state;
    res.state_changed = (sel != orig_sel);   /* pure clamp / scroll is not a change */
    return res;
}
TIMUI_API TimuiTreeScrollResult timui_tree_scroll_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState *state){
    TimuiTreeState in = state ? *state : (TimuiTreeState){0, 0};
    TimuiTreeScrollResult res = timui_tree_scroll(f, id, r, nodes, count, in);
    /* Persist the derived state (selection + scroll) whenever either moved. */
    if(state && (res.state_changed || res.state.scroll != in.scroll))
        *state = res.state;
    return res;
}
