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
    img->id = 0;                 /* assigned on first transmit (timui_images_flush_) */
    /* pixel size from the PNG IHDR (width @16, height @20, big-endian) so a
     * placement can be cropped to a cell sub-rect (smooth scroll clipping). */
    img->px_w = img->px_h = 0;
    if(size >= 24){
        const unsigned char *d = (const unsigned char *)data;
        img->px_w = (int)(((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19]);
        img->px_h = (int)(((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23]);
    }
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
/* base64-encode + chunked transmit of the PNG bytes under `id` (a=t, one-time).
 * Uses the correct APC introducer ESC _ G (was ESC G — a real protocol bug that
 * meant no terminal ever recognised the image). */
static void kitty_transmit_(TimuiTransport *t, uint32_t id, const unsigned char *data, size_t len){
    size_t b64cap, b64len, sent;
    TimuiAllocator al = timui_default_allocator();
    char *buf;
    int first = 1;
    if(len == 0 || len > (SIZE_MAX - 1) / 4) return;
    b64cap = ((len + 2) / 3) * 4 + 1;
    buf = (char *)al.alloc(al.userdata, b64cap);
    if(!buf) return;
    b64len = b64_encode(data, len, buf, b64cap - 1);
    if(b64len > 0 && b64len != (size_t)-1){
        #define KITTY_CHUNK 4096
        sent = 0;
        while(sent < b64len){
            size_t chunk = b64len - sent;
            char hdr[48]; int hn = 0, is_last;
            const char *p;
            if(chunk > KITTY_CHUNK) chunk = KITTY_CHUNK;
            is_last = (sent + chunk >= b64len);
            hdr[hn++] = 0x1b; hdr[hn++] = '_'; hdr[hn++] = 'G';          /* APC + G */
            if(first){ /* transmit: direct(d) PNG(f=100) under id, quiet(q=2) */
                p = "a=t,t=d,f=100,q=2,i="; while(*p) hdr[hn++] = *p++;
                hn += fmt_uint(hdr + hn, id);
                hdr[hn++] = ',';
            }
            hdr[hn++] = 'm'; hdr[hn++] = '='; hdr[hn++] = is_last ? '0' : '1'; hdr[hn++] = ';';
            kitty_write_all(t, hdr, (size_t)hn);
            kitty_write_all(t, buf + sent, chunk);
            kitty_write_all(t, "\x1b\\", 2);
            sent += chunk; first = 0;
        }
        #undef KITTY_CHUNK
    }
    al.free(al.userdata, buf, b64cap);
}
/* place image `id` at the cursor, scaled to cols x rows cells, under placement
 * id `place_id` (a=p). A UNIQUE placement id per on-screen slot is essential:
 * several messages sharing one image (same id) must not all use the same
 * placement id, or each a=p replaces the previous and only one image shows. */
static void kitty_place_(TimuiTransport *t, uint32_t id, int cols, int rows, int place_id,
                         int sx, int sy, int sw, int sh){
    char b[128]; int n = 0; const char *p;
    b[n++] = 0x1b; b[n++] = '_'; b[n++] = 'G';
    p = "a=p,q=2,i="; while(*p) b[n++] = *p++;
    n += fmt_uint(b + n, id);
    p = ",p="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(place_id > 0 ? place_id : 1));
    if(sw > 0){   /* source-crop rectangle (pixels) so a scrolled image clips */
        p = ",x="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sx > 0 ? sx : 0));
        p = ",y="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sy > 0 ? sy : 0));
        p = ",w="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)sw);
        p = ",h="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sh > 0 ? sh : 1));
    }
    p = ",c="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(cols > 0 ? cols : 1));
    p = ",r="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(rows > 0 ? rows : 1));
    b[n++] = 0x1b; b[n++] = '\\';
    kitty_write_all(t, b, (size_t)n);
}
/* delete every visible placement (keeps image data: lowercase d=a). */
static void kitty_delete_all_placements(TimuiTransport *t){
    kitty_write_all(t, "\x1b_Ga=d,d=a\x1b\\", 12);
}
/* Transmit (once) + place every image recorded this frame, on top of the cell
 * diff. Each on-screen slot gets a distinct placement id (i+1) and is CUP'd to
 * its rect, scaled to its cell size. When the count SHRINKS (placements scrolled
 * away, or shuffled slots), clear last frame's placements first so nothing
 * lingers above the cells (a cell redraw can't erase a Kitty image), then
 * re-place this frame's set. Under synchronized output the clear+replace is
 * atomic, so there is no flicker. Skipped on the first frame (nothing to
 * clear), which keeps a lone draw to a single transmit+place. */
void timui_images_flush_(Timui *ui){
    int i;
    if(!ui) return;
    if(ui->img_last_count > 0)
        kitty_delete_all_placements(&ui->transport);
    for(i = 0; i < ui->img_place_count; i++){
        TimuiImage *img = ui->img_place[i].img;
        TimuiRect r    = ui->img_place[i].rect;   /* visible sub-rect */
        TimuiRect full = ui->img_place[i].full;   /* uncropped rect   */
        int sx = 0, sy = 0, sw = 0, sh = 0;
        char cup[32]; int cn = 0;
        if(!img) continue;
        /* If the visible rect is a vertical sub-slice of `full`, crop the source
         * pixels to match, so the image clips smoothly at a pane edge. */
        if(img->px_w > 0 && img->px_h > 0 && full.h > 0 && (r.y != full.y || r.h != full.h)){
            sx = 0; sw = img->px_w;
            sy = (int)((long)(r.y - full.y) * img->px_h / full.h);
            sh = (int)((long)r.h * img->px_h / full.h);
            if(sh < 1) sh = 1;
        }
        if(img->id == 0){                                   /* transmit once, keyed by id */
            img->id = ++ui->next_image_id;
            kitty_transmit_(&ui->transport, img->id, img->data, img->len);
        }
        cup[cn++] = 0x1b; cup[cn++] = '[';                  /* CUP to the top-left cell */
        cn += fmt_uint(cup + cn, (unsigned)(r.y + 1)); cup[cn++] = ';';
        cn += fmt_uint(cup + cn, (unsigned)(r.x + 1)); cup[cn++] = 'H';
        kitty_write_all(&ui->transport, cup, (size_t)cn);
        kitty_place_(&ui->transport, img->id, r.w, r.h, i + 1, sx, sy, sw, sh);
    }
    ui->img_last_count = ui->img_place_count;
}
/* Record an image placement (transmit + place happen on top of the cell diff in
 * timui_end, so the renderer can't clobber it). `visible` is where it's drawn;
 * `full` is the uncropped rect (== visible when not clipping). The caller
 * reserves the region (draws its own background, no text). */
static void image_record_(Timui *ui, TimuiImage *img, TimuiRect visible, TimuiRect full){
    if(timui_caps_has(&ui->caps, TIMUI_CAP_KITTY_GRAPHICS)){
        if(ui->img_place_count < (int)(sizeof(ui->img_place) / sizeof(ui->img_place[0]))){
            ui->img_place[ui->img_place_count].img  = img;
            ui->img_place[ui->img_place_count].rect = visible;
            ui->img_place[ui->img_place_count].full = full;
            ui->img_place_count++;
        }
    } else {
        /* placeholder fallback (cells) for non-Kitty terminals */
        timui_draw_fill(&ui->curr, visible,
                        timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_INPUT, 0));
        timui_draw_text(&ui->curr, visible.x, visible.y, TIMUI_STR_LIT("[img]"),
                        timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_TEXT_DIM, 0));
    }
}
TIMUI_API void timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r){
    if(!f || !f->ui || !img) return;
    image_record_(f->ui, img, r, r);
}
TIMUI_API void timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                        TimuiRect full, TimuiRect visible){
    if(!f || !f->ui || !img || visible.w <= 0 || visible.h <= 0) return;
    image_record_(f->ui, img, visible, full);
}
