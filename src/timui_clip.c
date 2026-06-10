/* ---- clip stack ------------------------------------------------------- *
 * push_clip intersects the active clip with rect (so nested panels shrink it);
 * pop_clip restores the previous. Drawing (put_glyph) skips cells outside the
 * active clip. Reset each frame in timui_begin. */
static TimuiRect clip_intersect(TimuiRect a, TimuiRect b){
    TimuiRect r;
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    r.x = x1; r.y = y1;
    r.w = x2 > x1 ? x2 - x1 : 0;
    r.h = y2 > y1 ? y2 - y1 : 0;
    return r;
}
TIMUI_API void timui_push_clip(TimuiFrame *f, TimuiRect rect){
    Timui *ui;
    TimuiCellBuffer *b;
    TimuiRect active;
    if(!f || !f->ui) return;
    ui = f->ui;
    b = &ui->curr;
    if(ui->clip_count < 8){
        ui->clip_stack[ui->clip_count].clip = b->clip;
        ui->clip_stack[ui->clip_count].has_clip = b->has_clip;
        ui->clip_count++;
    }
    active = b->has_clip ? b->clip : TIMUI_RECT(0, 0, b->w, b->h);
    b->clip = clip_intersect(active, rect);
    b->has_clip = 1;
}
TIMUI_API void timui_pop_clip(TimuiFrame *f){
    Timui *ui;
    TimuiCellBuffer *b;
    if(!f || !f->ui) return;
    ui = f->ui;
    b = &ui->curr;
    if(ui->clip_count > 0){
        ui->clip_count--;
        b->clip = ui->clip_stack[ui->clip_count].clip;
        b->has_clip = ui->clip_stack[ui->clip_count].has_clip;
    }
}
