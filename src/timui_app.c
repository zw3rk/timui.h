/* ---- optional functional runner --------------------------------------- *
 * UI-thread message queue (emit during view, recv into update) + the runner. */
TIMUI_API TimuiResult timui_emit_result(TimuiFrame *f, uint32_t type, const void *data, size_t size){
    if(!f || !f->ui) return TIMUI_ERR_INVALID_ARGUMENT;
    return timui_mpsc_post_result(&f->ui->postq, type, data, size);
}
TIMUI_API bool timui_emit(TimuiFrame *f, uint32_t type, const void *data, size_t size){
    return timui_emit_result(f, type, data, size) == TIMUI_OK;
}
TIMUI_API TimuiResult timui_post_result(Timui *ui, uint32_t type, const void *data, size_t size){
    if(!ui) return TIMUI_ERR_INVALID_ARGUMENT;
    return timui_mpsc_post_result(&ui->postq, type, data, size);
}
TIMUI_API bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size){
    return timui_post_result(ui, type, data, size) == TIMUI_OK;
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
    TimuiResult r;
    if(!ui || !app || !app->view || timui_should_quit(ui)) return 0;
    r = timui_begin_result(ui, &f);
    if(r != TIMUI_OK) return 0;
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
