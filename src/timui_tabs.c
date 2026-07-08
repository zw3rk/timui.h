/* timui_tabs.c — tab-bar widget (W2).
 *
 * A single-row bar of labeled tabs with a boxed/highlighted active tab (the
 * radio.c look), Left/Right + mouse-click selection, and horizontal overflow
 * scrolling that keeps the selected tab in view. The geometry is split into
 * three pure, I/O-free helpers so the math is unit-testable in isolation and an
 * app can reuse it for its own hit-testing:
 *
 *   timui_tabs_layout   — each tab's [x, x+w) span from labels + widths + sep
 *   timui_tabs_scroll   — a scroll offset that keeps `selected` visible
 *   timui_tab_visible   — does a span overlap the viewport
 *
 * This file is a SECTION of the unity build: it is textually #included from
 * include/timui.h under TIMUI_IMPLEMENTATION, so it carries no includes/guards
 * and may call any already-declared public primitive (timui_utf8_*, the theme,
 * the draw primitives, the interaction state) plus the internal Timui fields.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* Cap on tabs a single bar lays out/draws in one frame (spans live on the
 * stack — a bar with more than this is pathological; extra tabs are ignored). */
#define TIMUI_TABS_MAX 64

/* Display width (terminal columns) of a NUL-terminated UTF-8 label, summing
 * per-codepoint widths so CJK/emoji count as 2 and combining marks as 0. */
static int timui_tab_text_width_(const char *s){
    int w = 0;
    size_t i = 0, len;
    if(!s) return 0;
    len = strlen(s);
    while(i < len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        if(adv <= 0) adv = 1;                 /* never stall on a bad byte */
        w += timui_utf8_width(cp);
        i += (size_t)adv;
    }
    return w;
}

static int timui_tabs_clamp_i64_(int64_t v){
    if(v < (int64_t)INT_MIN) return INT_MIN;
    if(v > (int64_t)INT_MAX) return INT_MAX;
    return (int)v;
}

/* Pure layout: place n tabs left-to-right. Each tab is drawn as " LABEL " — one
 * pad column on each side of its display width — and consecutive tabs are
 * separated by `sep` columns. Writes up to `max` spans into `out` (out may be
 * NULL to only measure) and returns the total content width. */
TIMUI_API int timui_tabs_layout(const char *const *labels, int n, int sep,
                                TimuiTabSpan *out, int max){
    int i, x = 0;
    if(n < 0) n = 0;
    if(sep < 0) sep = 0;
    for(i = 0; i < n; i++){
        int lw = (labels && labels[i]) ? timui_tab_text_width_(labels[i]) : 0;
        int tw = lw + 2;                      /* the ' LABEL ' box (pad each side) */
        if(out && i < max){ out[i].x = x; out[i].w = tw; }
        x += tw;
        if(i + 1 < n) x += sep;               /* sep goes BETWEEN tabs, not after */
    }
    return x;
}

/* Pure scroll: pick a column offset that keeps tab `selected` visible in a
 * `width`-column viewport, moving as little as possible from `cur_scroll`.
 * Reveals the selected tab's left edge if it is off to the left, or its right
 * edge if off to the right; a tab wider than the viewport pins its left edge so
 * the label start stays readable. Result is clamped to [0, max(0,total-width)]. */
TIMUI_API int timui_tabs_scroll(const TimuiTabSpan *spans, int n, int selected,
                                int width, int cur_scroll){
    int64_t scroll, total, maxscroll, end, sx, sw, vw;
    if(!spans || n <= 0) return 0;
    if(selected < 0) selected = 0;
    if(selected >= n) selected = n - 1;
    total = (int64_t)spans[n - 1].x + (int64_t)spans[n - 1].w; /* end of the last tab */
    scroll = cur_scroll < 0 ? 0 : cur_scroll;
    sx = (int64_t)spans[selected].x;
    sw = (int64_t)spans[selected].w;
    vw = (int64_t)width;
    end = sx + sw;
    if(sx < scroll)                                    /* left of the view */
        scroll = sx;
    else if(width > 0 && end > scroll + vw)             /* right of the view */
        scroll = end - vw;
    if(width > 0 && sw > vw)                            /* oversize: show label start */
        scroll = sx;
    maxscroll = total - vw;
    if(maxscroll < 0) maxscroll = 0;
    if(scroll > maxscroll) scroll = maxscroll;
    if(scroll < 0) scroll = 0;
    return timui_tabs_clamp_i64_(scroll);
}

/* Pure visibility: does `span` overlap the viewport [scroll, scroll+width)? */
TIMUI_API int timui_tab_visible(TimuiTabSpan span, int scroll, int width){
    int64_t sx = (int64_t)span.x;
    int64_t sw = (int64_t)span.w;
    int64_t sc = (int64_t)scroll;
    int64_t vw = (int64_t)width;
    return (sx < sc + vw && sx + sw > sc) ? 1 : 0;
}

/* The interactive widget. Draws the bar, highlights *selected, applies Left/
 * Right (when focused) and mouse clicks, overflow-scrolls to keep the selection
 * visible, writes the resulting index back through *selected, and returns it. */
TIMUI_API int timui_tabs(TimuiFrame *f, TimuiId id, TimuiRect r,
                         const char *const *labels, int n, int *selected){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiTabSpan spans[TIMUI_TABS_MAX];
    TimuiStyle bar_st, sel_st, txt_st;
    int i, sel, y, scroll;

    if(!f || !f->ui) return selected ? *selected : 0;
    ui = f->ui;
    if(!selected) return 0;
    sel = *selected;
    bar_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_PANEL, 0);

    /* No tabs: clear the bar, normalize the selection, and bail out. */
    if(n <= 0){
        timui_draw_fill(&ui->curr, r, bar_st);
        *selected = 0;
        return 0;
    }
    if(n > TIMUI_TABS_MAX) n = TIMUI_TABS_MAX;
    if(sel < 0) sel = 0;
    if(sel >= n) sel = n - 1;                 /* self-heal a stale selection (Y3) */

    /* Register the whole bar so it joins the focus + Tab cycle and we can see
     * click / keyboard activation, exactly as the other widgets do. */
    ir = timui_interact_button(&ui->ia, id, r);

    /* Keyboard: Left/Right step the selection when the bar is focused (mirrors
     * the listbox's Up/Down over the accumulated key bitmask). */
    if(ir.focused){
        if((ui->key_in & TIMUI_KEYIN_LEFT)  && sel > 0)     sel--;
        if((ui->key_in & TIMUI_KEYIN_RIGHT) && sel < n - 1) sel++;
    }

    /* Lay the tabs out, then choose a scroll that keeps `sel` visible. cur=0 is
     * the canonical offset: tab 0 sits flush-left and later tabs only scroll in
     * once they would otherwise overflow the bar. */
    timui_tabs_layout(labels, n, 1, spans, n);
    scroll = timui_tabs_scroll(spans, n, sel, r.w, 0);

    /* Mouse: a click landing on a visible tab selects it. Hit-test in bar-local,
     * scroll-adjusted columns and require the pointer inside the bar (so a
     * keyboard activation with the mouse elsewhere can't grab a tab). */
    if(ir.clicked && timui_rect_contains_(r, ui->ia.mouse_x, ui->ia.mouse_y)){
        int lx = ui->ia.mouse_x - r.x + scroll;       /* column in the unscrolled bar */
        for(i = 0; i < n; i++){
            if(lx >= spans[i].x && lx < spans[i].x + spans[i].w){ sel = i; break; }
        }
        scroll = timui_tabs_scroll(spans, n, sel, r.w, scroll);   /* keep the pick in view */
    }

    /* ---- draw: boxed/highlighted active tab, dim inactive labels ---- */
    y = r.y;
    sel_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_SELECTION,
                                 TIMUI_STYLE_STATE_SELECTED);
    txt_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_TEXT, 0);
    txt_st.bg = bar_st.bg;                    /* inactive labels sit on the bar bg */
    timui_draw_fill(&ui->curr, r, bar_st);    /* clear the bar */
    timui_push_clip(f, r);                    /* clip any overflow to the bar */
    for(i = 0; i < n; i++){
        int sx;
        TimuiStr lab;
        if(!timui_tab_visible(spans[i], scroll, r.w)) continue;
        sx = r.x + spans[i].x - scroll;       /* screen column of this tab's box */
        lab = timui_str_from_cstr((labels && labels[i]) ? labels[i] : "");
        if(i == sel){
            TimuiStyle hi = sel_st;
            hi.attrs |= TIMUI_ATTR_BOLD;
            timui_draw_fill(&ui->curr, TIMUI_RECT(sx, y, spans[i].w, 1), hi);  /* highlight box */
            timui_draw_text(&ui->curr, sx + 1, y, lab, hi);                    /* padded label */
        } else {
            timui_draw_text(&ui->curr, sx + 1, y, lab, txt_st);
        }
    }
    timui_pop_clip(f);

    *selected = sel;
    return sel;
}
