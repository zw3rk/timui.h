/* ---- Kitty graphics images (v0.2) ------------------------------------- *
 * Accept PNG bytes; when the terminal supports Kitty graphics, transmit via
 * ESC_G (base64-encoded PNG, f=100); otherwise draw a "[img]" placeholder.
 * Image caching/placement lifecycle is deferred (re-transmit per frame). */
TIMUI_API void timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable){
    if(!ui) return;
    if(enable) ui->caps.flags |= (uint32_t)cap;
    else       ui->caps.flags &= ~(uint32_t)cap;
}
TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size){
    TimuiImage *img;
    TimuiAllocator al;
    (void)ui;
    if(!data || size == 0) return NULL;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, size);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    memcpy(img->data, data, size);
    img->len = size;
    return img;
}
TIMUI_API void timui_image_free(Timui *ui, TimuiImage *img){
    TimuiAllocator al;
    (void)ui;
    if(!img) return;
    al = timui_default_allocator();
    if(img->data) al.free(al.userdata, img->data, img->len);
    al.free(al.userdata, img, sizeof *img);
}
TIMUI_API void timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r){
    Timui *ui;
    if(!f || !f->ui || !img) return;
    ui = f->ui;
    if(timui_caps_has(&ui->caps, TIMUI_CAP_KITTY_GRAPHICS)){
        size_t b64cap = ((img->len + 2) / 3) * 4 + 1;
        TimuiAllocator al = timui_default_allocator();
        char *buf = (char *)al.alloc(al.userdata, b64cap);
        if(buf){
            int b64len = b64_encode(img->data, img->len, buf, b64cap - 1);
            if(b64len > 0){
                if(ui->transport.write) (void)ui->transport.write(&ui->transport, "\x1bGa=T,t=d,f=100;", 17);
                if(ui->transport.write) (void)ui->transport.write(&ui->transport, buf, (size_t)b64len);
                if(ui->transport.write) (void)ui->transport.write(&ui->transport, "\x1b\\", 2);
            }
            al.free(al.userdata, buf, b64cap);
            return;
        }
    }
    /* placeholder fallback */
    timui_draw_fill(&ui->curr, r, timui_theme_style(&ui->theme, TIMUI_SLOT_INPUT));
    timui_draw_text(&ui->curr, r.x, r.y, TIMUI_STR_LIT("[img]"),
                    timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT_DIM));
}
