/* ---- clip stack ------------------------------------------------------- *
 * push_clip intersects the active clip with rect (so nested panels shrink it);
 * pop_clip restores the previous. Drawing (put_glyph) skips cells outside the
 * active clip. Reset each frame in timui_begin. */
static TimuiRect clip_intersect(TimuiRect a, TimuiRect b){
    TimuiRect r;
    int64_t ax2 = (int64_t)a.x + (int64_t)a.w;
    int64_t ay2 = (int64_t)a.y + (int64_t)a.h;
    int64_t bx2 = (int64_t)b.x + (int64_t)b.w;
    int64_t by2 = (int64_t)b.y + (int64_t)b.h;
    int64_t x1 = a.x > b.x ? (int64_t)a.x : (int64_t)b.x;
    int64_t y1 = a.y > b.y ? (int64_t)a.y : (int64_t)b.y;
    int64_t x2 = ax2 < bx2 ? ax2 : bx2;
    int64_t y2 = ay2 < by2 ? ay2 : by2;
    int64_t rw = x2 > x1 ? x2 - x1 : 0;
    int64_t rh = y2 > y1 ? y2 - y1 : 0;
    r.x = x1 < INT_MIN ? INT_MIN : (x1 > INT_MAX ? INT_MAX : (int)x1);
    r.y = y1 < INT_MIN ? INT_MIN : (y1 > INT_MAX ? INT_MAX : (int)y1);
    r.w = rw > INT_MAX ? INT_MAX : (int)rw;
    r.h = rh > INT_MAX ? INT_MAX : (int)rh;
    return r;
}
TIMUI_API void timui_push_clip(TimuiFrame *f, TimuiRect rect){
    Timui *ui;
    TimuiCellBuffer *b;
    TimuiRect active;
    if(!f || !f->ui) return;
    ui = f->ui;
    b = &ui->curr;
    if(ui->clip_count >= ui->clip_cap){
        int nc;
        void *ns;
        if(ui->clip_cap > INT_MAX / 2) return;
        nc = ui->clip_cap ? ui->clip_cap * 2 : 8;
        ns = ui->alloc.realloc(ui->alloc.userdata, ui->clip_stack,
                               (size_t)ui->clip_cap * sizeof(*ui->clip_stack),
                               (size_t)nc * sizeof(*ui->clip_stack));
        if(!ns) return;
        ui->clip_stack = ns;
        ui->clip_cap = nc;
    }
    ui->clip_stack[ui->clip_count].clip = b->clip;
    ui->clip_stack[ui->clip_count].has_clip = b->has_clip;
    ui->clip_count++;
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
