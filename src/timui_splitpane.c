/* ---- split / resizable pane widget ------------------------------------ *
 * Caller-owned two-pane splitter. This intentionally implements only local
 * divider drag; richer pointer capture and hover routing belong to Phase 2. */

static float split_pane_ratio_(float ratio){
    if(!(ratio == ratio)) return 0.5f;   /* NaN */
    if(ratio < 0.0f) return 0.0f;
    if(ratio > 1.0f) return 1.0f;
    return ratio;
}

static int split_pane_axis_len_(TimuiRect r, TimuiAxis axis){
    int n = axis == TIMUI_AXIS_V ? r.h : r.w;
    return n > 0 ? n : 0;
}

static int split_pane_cross_len_(TimuiRect r, TimuiAxis axis){
    int n = axis == TIMUI_AXIS_V ? r.w : r.h;
    return n > 0 ? n : 0;
}

static int split_pane_first_size_(int desired, int avail, int min_first, int min_second){
    int lo, hi;
    if(avail <= 0) return 0;
    if(min_first < 0) min_first = 0;
    if(min_second < 0) min_second = 0;
    if(min_first + min_second > avail){
        int total = min_first + min_second;
        if(total <= 0) return avail / 2;
        return (int)(((long)avail * min_first + total / 2) / total);
    }
    lo = min_first;
    hi = avail - min_second;
    if(desired < lo) desired = lo;
    if(desired > hi) desired = hi;
    return desired;
}

static TimuiSplitPaneResult split_pane_layout_(TimuiRect r, TimuiAxis axis,
                                               TimuiSplitPaneState state){
    TimuiSplitPaneResult res;
    int axis_len = split_pane_axis_len_(r, axis);
    int cross_len = split_pane_cross_len_(r, axis);
    int div = axis_len > 0 ? 1 : 0;
    int avail = axis_len - div;
    int first;
    if(avail < 0) avail = 0;
    state.ratio = split_pane_ratio_(state.ratio);
    if(state.min_first < 0) state.min_first = 0;
    if(state.min_second < 0) state.min_second = 0;
    first = split_pane_first_size_((int)((float)avail * state.ratio + 0.5f),
                                   avail, state.min_first, state.min_second);
    state.ratio = avail > 0 ? (float)first / (float)avail : 0.0f;

    res.state = state;
    res.changed = false;
    res.hovered = false;
    res.dragging = false;
    if(axis == TIMUI_AXIS_V){
        res.first = TIMUI_RECT(r.x, r.y, cross_len, first);
        res.divider = TIMUI_RECT(r.x, r.y + first, cross_len, div);
        res.second = TIMUI_RECT(r.x, r.y + first + div, cross_len, avail - first);
    } else {
        res.first = TIMUI_RECT(r.x, r.y, first, cross_len);
        res.divider = TIMUI_RECT(r.x + first, r.y, div, cross_len);
        res.second = TIMUI_RECT(r.x + first + div, r.y, avail - first, cross_len);
    }
    return res;
}

static int split_pane_same_layout_(TimuiSplitPaneResult a, TimuiSplitPaneResult b){
    return a.first.x == b.first.x && a.first.y == b.first.y &&
           a.first.w == b.first.w && a.first.h == b.first.h &&
           a.divider.x == b.divider.x && a.divider.y == b.divider.y &&
           a.divider.w == b.divider.w && a.divider.h == b.divider.h &&
           a.second.x == b.second.x && a.second.y == b.second.y &&
           a.second.w == b.second.w && a.second.h == b.second.h;
}

static TimuiSplitPaneResult split_pane_drag_(TimuiSplitPaneResult base, TimuiRect r,
                                             TimuiAxis axis, int mouse_x, int mouse_y){
    TimuiSplitPaneState state = base.state;
    int axis_len = split_pane_axis_len_(r, axis);
    int avail = axis_len > 0 ? axis_len - 1 : 0;
    int first = axis == TIMUI_AXIS_V ? mouse_y - r.y : mouse_x - r.x;
    TimuiSplitPaneResult next;
    if(avail < 0) avail = 0;
    first = split_pane_first_size_(first, avail, state.min_first, state.min_second);
    state.ratio = avail > 0 ? (float)first / (float)avail : 0.0f;
    next = split_pane_layout_(r, axis, state);
    next.changed = !split_pane_same_layout_(base, next);
    next.dragging = true;
    next.hovered = base.hovered;
    return next;
}

static void split_pane_draw_(Timui *ui, TimuiSplitPaneResult res, TimuiAxis axis){
    TimuiStyle st;
    if(!ui || res.divider.w <= 0 || res.divider.h <= 0) return;
    st = timui_widget_style_(ui, TIMUI_WIDGET_SPLIT,
                             res.dragging ? TIMUI_SLOT_SELECTION :
                             res.hovered ? TIMUI_SLOT_BUTTON_HOVERED : TIMUI_SLOT_BORDER,
                             (res.dragging ? TIMUI_STYLE_STATE_ACTIVE : 0) |
                             (res.hovered ? TIMUI_STYLE_STATE_HOVERED : 0));
    timui_draw_fill(&ui->curr, res.divider, st);
    if(axis == TIMUI_AXIS_V)
        timui_draw_hline(&ui->curr, res.divider.x, res.divider.y, res.divider.w, st);
    else
        timui_draw_vline(&ui->curr, res.divider.x, res.divider.y, res.divider.h, st);
}

TIMUI_API TimuiSplitPaneResult timui_split_pane(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                TimuiAxis axis, TimuiSplitPaneState state){
    TimuiSplitPaneResult res;
    Timui *ui;
    TimuiInteractResult ir;
    if(axis != TIMUI_AXIS_V) axis = TIMUI_AXIS_H;
    res = split_pane_layout_(r, axis, state);
    if(!f || !f->ui) return res;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, res.divider);
    res.hovered = ir.hovered;
    res.dragging = ir.pressed;
    if(ir.pressed)
        res = split_pane_drag_(res, r, axis, ui->ia.mouse_x, ui->ia.mouse_y);
    split_pane_draw_(ui, res, axis);
    return res;
}

TIMUI_API TimuiSplitPaneResult timui_split_pane_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                    TimuiAxis axis, TimuiSplitPaneState *state){
    TimuiSplitPaneState in = {0.5f, 0, 0};
    TimuiSplitPaneResult res;
    if(state) in = *state;
    res = timui_split_pane(f, id, r, axis, in);
    if(state && res.changed) *state = res.state;
    return res;
}
