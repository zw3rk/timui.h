/* ---- optional functional runner --------------------------------------- *
 * UI-thread message queue (emit during view, recv into update) + the runner. */
TIMUI_API bool timui_emit(TimuiFrame *f, uint32_t type, const void *data, size_t size){
    return f && f->ui && timui_mpsc_post(&f->ui->postq, type, data, size) != 0;
}
TIMUI_API bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size){
    return ui && timui_mpsc_post(&ui->postq, type, data, size) != 0;
}
TIMUI_API bool timui_recv(Timui *ui, uint32_t *out_type, void *out_buf, size_t *inout_size){
    return ui && timui_mpsc_recv(&ui->postq, out_type, out_buf, inout_size) != 0;
}
TIMUI_API void timui_frame_quit(TimuiFrame *f){
    if(f && f->ui) timui_quit(f->ui);
}
TIMUI_API int timui_run(const TimuiConfig *cfg, TimuiApp *app){
    Timui *ui = NULL;
    if(!cfg || !app || !app->view || timui_open(cfg, &ui) != TIMUI_OK) return 1;
    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        uint32_t type = 0;
        unsigned char buf[256];
        size_t sz;
        if(!timui_begin(ui, &f)) break;
        app->view(f, app->model);
        sz = sizeof buf;
        while(timui_recv(ui, &type, buf, &sz)){
            if(sz > sizeof buf) sz = sizeof buf;   /* clamp to prevent stack over-read */
            if(app->update) app->update(app->model, type, buf, sz);
            sz = sizeof buf;
        }
        timui_end(f);
    }
    timui_close(ui);
    return 0;
}
