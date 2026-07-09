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
static void timui_app_drain_updates_(Timui *ui, TimuiApp *app){
    TimuiMpscNode *n;
    if(!ui || !app) return;
    while((n = timui_mpsc_pop_node_(&ui->postq)) != NULL){
        if(app->update) app->update(app->model, n->type, n->data, n->size);
        timui_mpsc_free_node_(&ui->postq, n);
    }
}
TIMUI_API int timui_app_frame(Timui *ui, TimuiApp *app){
    TimuiFrame *f = NULL;
    if(!ui || !app || !app->view || timui_should_quit(ui)) return 0;
    if(!timui_begin(ui, &f)) return 0;
    app->view(f, app->model);
    timui_end(f);
    timui_app_drain_updates_(ui, app);
    return 1;
}
TIMUI_API int timui_run(const TimuiConfig *cfg, TimuiApp *app){
    Timui *ui = NULL;
    if(!cfg || !app || !app->view || timui_open(cfg, &ui) != TIMUI_OK) return 1;
    while(!timui_should_quit(ui)){
        if(!timui_app_frame(ui, app)) break;
    }
    timui_close(ui);
    return 0;
}
