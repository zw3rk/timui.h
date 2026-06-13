/* ---- scroll view (v0.2) ----------------------------------------------- *
 * A clipped, scrollable viewport. scroll_begin pushes a clip to `viewport`
 * and returns a content rect shifted up by `scroll_y`; the app draws content
 * into that rect (items above/below the viewport are clipped away).
 * scroll_end pops the clip. The app adjusts scroll_y on arrow/wheel input. */
TIMUI_API TimuiRect timui_scroll_begin(TimuiFrame *f, TimuiRect viewport, int scroll_y){
    TimuiRect content = viewport;
    if(!f) return content;
    timui_push_clip(f, viewport);
    content.y -= scroll_y;
    return content;
}
TIMUI_API void timui_scroll_end(TimuiFrame *f){
    timui_pop_clip(f);
}
