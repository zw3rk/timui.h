/* ---- Kitty graphics images (v0.2) ------------------------------------- *
 * Accept PNG bytes; when the terminal supports Kitty graphics, transmit via
 * ESC_G (base64-encoded PNG, f=100); otherwise draw a "[img]" placeholder.
 * Image caching/placement lifecycle is deferred (re-transmit per frame). */

/* Write all len bytes, looping past short writes. A real fd transport may
 * deliver fewer bytes than requested; without this a graphics chunk can split
 * across the header/payload/ST boundary and corrupt the image (G5 residual). */
static void kitty_write_all(TimuiTransport *t, const void *data, size_t len){
    const unsigned char *p = (const unsigned char *)data;
    size_t off = 0;
    if(!t || !t->write) return;
    while(off < len){
        int w = t->write(t, p + off, len - off);
        if(w <= 0) break;                /* error / would-block: best-effort, stop */
        off += (size_t)w;
    }
}

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
        size_t b64cap, b64len;
        TimuiAllocator al = timui_default_allocator();
        char *buf;
        if(img->len > (SIZE_MAX - 1) / 4) return;     /* base64 size would overflow */
        b64cap = ((img->len + 2) / 3) * 4 + 1;
        buf = (char *)al.alloc(al.userdata, b64cap);
        if(buf){
            b64len = b64_encode(img->data, img->len, buf, b64cap - 1);
            if(b64len > 0 && b64len != (size_t)-1){
                /* Chunk at 4096 bytes with m=1 continuation (Kitty protocol) */
                #define KITTY_CHUNK 4096
                size_t sent = 0;
                int first = 1;
                while(sent < b64len){
                    size_t chunk = b64len - sent;
                    char hdr[40]; int hn = 0;
                    int is_last;
                    if(chunk > KITTY_CHUNK) chunk = KITTY_CHUNK;
                    is_last = (sent + chunk >= b64len);
                    /* header: first chunk has a=T,t=d,f=100; all chunks carry m=0/1 */
                    hn = 0;
                    hdr[hn++] = 0x1b; hdr[hn++] = 'G';
                    if(first){
                        hdr[hn++] = 'a'; hdr[hn++] = '='; hdr[hn++] = 'T';
                        hdr[hn++] = ','; hdr[hn++] = 't'; hdr[hn++] = '='; hdr[hn++] = 'd';
                        hdr[hn++] = ','; hdr[hn++] = 'f'; hdr[hn++] = '='; hdr[hn++] = '1'; hdr[hn++] = '0'; hdr[hn++] = '0';
                        hdr[hn++] = ',';
                    }
                    hdr[hn++] = 'm'; hdr[hn++] = '=';
                    hdr[hn++] = is_last ? '0' : '1';
                    hdr[hn++] = ';';
                    kitty_write_all(&ui->transport, hdr, (size_t)hn);
                    kitty_write_all(&ui->transport, buf + sent, chunk);
                    kitty_write_all(&ui->transport, "\x1b\\", 2);
                    sent += chunk;
                    first = 0;
                }
                #undef KITTY_CHUNK
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
