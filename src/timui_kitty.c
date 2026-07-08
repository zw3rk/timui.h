/* ---- Terminal images (v0.2) ------------------------------------------- *
 * Accept PNG bytes; Kitty terminals get APC graphics, iTerm2 gets OSC 1337,
 * and unsupported paths draw a "[img]" placeholder. */

/* Write all len bytes, looping past short writes. A real fd transport may
 * deliver fewer bytes than requested; without this a graphics chunk can split
 * across the header/payload/ST boundary and corrupt the image (G5 residual). */
static void image_write_all_(TimuiTransport *t, const void *data, size_t len){
    const unsigned char *p = (const unsigned char *)data;
    size_t off = 0;
    if(!t || !t->write) return;
    while(off < len){
        size_t chunk = len - off;
        int w;
        if(chunk > 4096) chunk = 4096;
        w = t->write(t, p + off, chunk);
        if(w <= 0) break;                /* error / would-block: best-effort, stop */
        off += ((size_t)w > chunk) ? chunk : (size_t)w;
    }
}

static int image_fmt_size_(char *buf, size_t v){
    char tmp[32];
    int n = 0, i;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while(v > 0);
    for(i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
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
    img->kind = TIMUI_IMAGE_KIND_PNG;
    img->stride = 0;
    img->id = 0;                 /* assigned on first transmit (timui_images_flush_) */
    /* pixel size from the PNG IHDR (width @16, height @20, big-endian) so a
     * placement can be cropped to a cell sub-rect (smooth scroll clipping). */
    img->px_w = img->px_h = 0;
    if(size >= 24){
        const unsigned char *d = (const unsigned char *)data;
        uint32_t w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
        uint32_t h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
        img->px_w = (w <= (uint32_t)INT_MAX) ? (int)w : 0;
        img->px_h = (h <= (uint32_t)INT_MAX) ? (int)h : 0;
    }
    return img;
}
TIMUI_API TimuiImage *timui_image_from_rgba(Timui *ui, const void *rgba, int w, int h, int stride){
    TimuiImage *img;
    TimuiAllocator al;
    size_t row, total;
    const unsigned char *src;
    int y;
    (void)ui;
    if(!rgba || w <= 0 || h <= 0) return NULL;
    if(w > INT_MAX / 4) return NULL;
    row = (size_t)w * 4u;
    if(stride < (int)row) return NULL;
    if((size_t)h > SIZE_MAX / row) return NULL;
    total = row * (size_t)h;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, total);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    src = (const unsigned char *)rgba;
    for(y = 0; y < h; y++)
        memcpy(img->data + (size_t)y * row, src + (size_t)y * (size_t)stride, row);
    img->len = total;
    img->id = 0;
    img->px_w = w;
    img->px_h = h;
    img->kind = TIMUI_IMAGE_KIND_RGBA;
    img->stride = (int)row;
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
            image_write_all_(t, hdr, (size_t)hn);
            image_write_all_(t, buf + sent, chunk);
            image_write_all_(t, "\x1b\\", 2);
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
    image_write_all_(t, b, (size_t)n);
}
/* delete every visible placement (keeps image data: lowercase d=a). */
static void kitty_delete_all_placements(TimuiTransport *t){
    image_write_all_(t, "\x1b_Ga=d,d=a\x1b\\", 12);
}

static int image_cup_(TimuiTransport *t, int x, int y){
    char cup[32];
    int cn = 0;
    if(x < 0 || y < 0 || x == INT_MAX || y == INT_MAX) return 0;
    cup[cn++] = 0x1b; cup[cn++] = '[';
    cn += fmt_uint(cup + cn, (unsigned)(y + 1)); cup[cn++] = ';';
    cn += fmt_uint(cup + cn, (unsigned)(x + 1)); cup[cn++] = 'H';
    image_write_all_(t, cup, (size_t)cn);
    return 1;
}

static int iterm2_emit_(TimuiTransport *t, const TimuiImage *img, TimuiRect r){
    size_t b64cap, b64len;
    TimuiAllocator al = timui_default_allocator();
    char *buf;
    char hdr[160];
    int hn = 0;
    const char *p;
    if(!t || !t->write || !img || !img->data || img->len == 0 || r.w <= 0 || r.h <= 0) return 0;
    if(r.x < 0 || r.y < 0) return 0;
    if(img->len > (SIZE_MAX - 1) / 4) return 0;
    b64cap = ((img->len + 2) / 3) * 4 + 1;
    buf = (char *)al.alloc(al.userdata, b64cap);
    if(!buf) return 0;
    b64len = b64_encode(img->data, img->len, buf, b64cap - 1);
    if(b64len == 0 || b64len == (size_t)-1){
        al.free(al.userdata, buf, b64cap);
        return 0;
    }
    if(!image_cup_(t, r.x, r.y)){
        al.free(al.userdata, buf, b64cap);
        return 0;
    }
    hdr[hn++] = 0x1b; hdr[hn++] = ']';
    p = "1337;File=inline=1;size="; while(*p) hdr[hn++] = *p++;
    hn += image_fmt_size_(hdr + hn, img->len);
    p = ";width="; while(*p) hdr[hn++] = *p++;
    hn += fmt_uint(hdr + hn, (unsigned)r.w);
    p = ";height="; while(*p) hdr[hn++] = *p++;
    hn += fmt_uint(hdr + hn, (unsigned)r.h);
    p = ";preserveAspectRatio=0:"; while(*p) hdr[hn++] = *p++;
    image_write_all_(t, hdr, (size_t)hn);
    image_write_all_(t, buf, b64len);
    image_write_all_(t, "\x1b\\", 2);
    al.free(al.userdata, buf, b64cap);
    return 1;
}

#define SIXEL_MAX_COLORS 16

typedef struct {
    unsigned char r, g, b;
} SixelColor_;

static int image_rect_emit_valid_(TimuiRect r);

static int sixel_is_rgba_(const TimuiImage *img){
    return img && img->kind == TIMUI_IMAGE_KIND_RGBA && img->data &&
           img->px_w > 0 && img->px_h > 0 && img->px_w <= INT_MAX / 4 &&
           (size_t)img->stride >= (size_t)img->px_w * 4u;
}

static int sixel_palette_index_(const SixelColor_ *pal, int count,
                                unsigned char r, unsigned char g, unsigned char b){
    int i;
    for(i = 0; i < count; i++)
        if(pal[i].r == r && pal[i].g == g && pal[i].b == b) return i;
    return -1;
}

static int sixel_palette_(const TimuiImage *img, SixelColor_ *pal, int *out_count){
    int x, y, count = 0;
    if(!sixel_is_rgba_(img) || !pal || !out_count) return 0;
    for(y = 0; y < img->px_h; y++){
        const unsigned char *row = img->data + (size_t)y * (size_t)img->stride;
        for(x = 0; x < img->px_w; x++){
            const unsigned char *px = row + (size_t)x * 4u;
            if(px[3] < 128) continue;
            if(sixel_palette_index_(pal, count, px[0], px[1], px[2]) >= 0) continue;
            if(count >= SIXEL_MAX_COLORS) return 0;
            pal[count].r = px[0];
            pal[count].g = px[1];
            pal[count].b = px[2];
            count++;
        }
    }
    *out_count = count;
    return count > 0;
}

static int sixel_image_supported_(const TimuiImage *img){
    SixelColor_ pal[SIXEL_MAX_COLORS];
    int count = 0;
    return sixel_palette_(img, pal, &count);
}

static unsigned sixel_pct_(unsigned char v){
    return (unsigned)(((unsigned)v * 100u + 127u) / 255u);
}

static void sixel_emit_color_def_(TimuiTransport *t, int idx, SixelColor_ c){
    char b[64];
    int n = 0;
    const char *p;
    b[n++] = '#'; n += fmt_uint(b + n, (unsigned)(idx + 1));
    p = ";2;"; while(*p) b[n++] = *p++;
    n += fmt_uint(b + n, sixel_pct_(c.r)); b[n++] = ';';
    n += fmt_uint(b + n, sixel_pct_(c.g)); b[n++] = ';';
    n += fmt_uint(b + n, sixel_pct_(c.b));
    image_write_all_(t, b, (size_t)n);
}

static int sixel_emit_(TimuiTransport *t, const TimuiImage *img, TimuiRect r){
    SixelColor_ pal[SIXEL_MAX_COLORS];
    int count = 0;
    int ci, x, band;
    char b[64];
    int n;
    const char *p;
    if(!t || !t->write || !image_rect_emit_valid_(r)) return 0;
    if(!sixel_palette_(img, pal, &count)) return 0;
    if(!image_cup_(t, r.x, r.y)) return 0;
    image_write_all_(t, "\x1bP0;1;0q", sizeof("\x1bP0;1;0q") - 1);
    n = 0;
    b[n++] = '"'; b[n++] = '1'; b[n++] = ';'; b[n++] = '1'; b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)img->px_w); b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)img->px_h);
    image_write_all_(t, b, (size_t)n);
    for(ci = 0; ci < count; ci++) sixel_emit_color_def_(t, ci, pal[ci]);
    for(band = 0; band < img->px_h; band += 6){
        for(ci = 0; ci < count; ci++){
            n = 0;
            b[n++] = '#';
            n += fmt_uint(b + n, (unsigned)(ci + 1));
            image_write_all_(t, b, (size_t)n);
            for(x = 0; x < img->px_w; x++){
                int bit;
                unsigned bits = 0;
                for(bit = 0; bit < 6; bit++){
                    int y = band + bit;
                    const unsigned char *px;
                    if(y >= img->px_h) continue;
                    px = img->data + (size_t)y * (size_t)img->stride + (size_t)x * 4u;
                    if(px[3] >= 128 && px[0] == pal[ci].r && px[1] == pal[ci].g && px[2] == pal[ci].b)
                        bits |= (1u << bit);
                }
                b[0] = (char)(0x3f + bits);
                image_write_all_(t, b, 1);
            }
            p = (ci + 1 < count) ? "$" : ((band + 6 < img->px_h) ? "-" : "");
            if(*p) image_write_all_(t, p, 1);
        }
    }
    image_write_all_(t, "\x1b\\", 2);
    return 1;
}

/* Transmit/place or emit every image recorded this frame, on top of the cell
 * diff. Kitty gets explicit placement lifecycle management; iTerm2 is a direct
 * inline image write with no placement ids or delete escape. */
void timui_images_flush_(Timui *ui){
    int i;
    int emitted = 0;
    TimuiImageProtocol protocol;
    if(!ui) return;
    protocol = timui_image_protocol(ui);
    if(ui->img_last_count > 0 && ui->img_last_protocol == TIMUI_IMAGE_PROTOCOL_KITTY)
        kitty_delete_all_placements(&ui->transport);
    if(protocol == TIMUI_IMAGE_PROTOCOL_KITTY){
        for(i = 0; i < ui->img_place_count; i++){
            TimuiImage *img = ui->img_place[i].img;
            TimuiRect r    = ui->img_place[i].rect;   /* visible sub-rect */
            TimuiRect full = ui->img_place[i].full;   /* uncropped rect   */
            int sx = 0, sy = 0, sw = 0, sh = 0;
            if(!img) continue;
            if(r.x < 0 || r.y < 0 || r.x == INT_MAX || r.y == INT_MAX) continue;
            /* If the visible rect is a vertical sub-slice of `full`, crop the source
             * pixels to match, so the image clips smoothly at a pane edge. */
            if(img->px_w > 0 && img->px_h > 0 && full.h > 0 && (r.y != full.y || r.h != full.h)){
                int64_t sy64 = ((int64_t)r.y - (int64_t)full.y) * (int64_t)img->px_h / (int64_t)full.h;
                int64_t sh64 = (int64_t)r.h * (int64_t)img->px_h / (int64_t)full.h;
                sx = 0;
                sw = img->px_w;
                if(sy64 < 0) sy64 = 0;
                if(sy64 > img->px_h) sy64 = img->px_h;
                if(sh64 < 1) sh64 = 1;
                if(sy64 + sh64 > img->px_h) sh64 = (int64_t)img->px_h - sy64;
                if(sh64 < 1) sh64 = 1;
                sy = (int)sy64;
                sh = (int)sh64;
            }
            if(img->id == 0){                                   /* transmit once, keyed by id */
                img->id = ++ui->next_image_id;
                kitty_transmit_(&ui->transport, img->id, img->data, img->len);
            }
            if(image_cup_(&ui->transport, r.x, r.y)){
                kitty_place_(&ui->transport, img->id, r.w, r.h, i + 1, sx, sy, sw, sh);
                emitted++;
            }
        }
    } else if(protocol == TIMUI_IMAGE_PROTOCOL_ITERM2){
        for(i = 0; i < ui->img_place_count; i++)
            emitted += iterm2_emit_(&ui->transport, ui->img_place[i].img, ui->img_place[i].rect);
    } else if(protocol == TIMUI_IMAGE_PROTOCOL_SIXEL){
        for(i = 0; i < ui->img_place_count; i++)
            emitted += sixel_emit_(&ui->transport, ui->img_place[i].img, ui->img_place[i].rect);
    }
    ui->img_last_count = emitted;
    ui->img_last_protocol = emitted ? protocol : TIMUI_IMAGE_PROTOCOL_NONE;
}

static int image_rect_same_(TimuiRect a, TimuiRect b){
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static int image_rect_emit_valid_(TimuiRect r){
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x != INT_MAX && r.y != INT_MAX;
}

static int image_rect_contains_(TimuiRect outer, TimuiRect inner){
    int64_t ox = outer.x, oy = outer.y, ow = outer.w, oh = outer.h;
    int64_t ix = inner.x, iy = inner.y, iw = inner.w, ih = inner.h;
    if(ow <= 0 || oh <= 0 || iw <= 0 || ih <= 0) return 0;
    return ix >= ox && iy >= oy && ix + iw <= ox + ow && iy + ih <= oy + oh;
}

static void image_placeholder_(Timui *ui, TimuiRect visible){
    if(!ui || visible.w <= 0 || visible.h <= 0) return;
    timui_draw_fill(&ui->curr, visible,
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_INPUT, 0));
    timui_draw_text(&ui->curr, visible.x, visible.y, TIMUI_STR_LIT("[img]"),
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_TEXT_DIM, 0));
}

/* Record an image placement (transmit + place happen on top of the cell diff in
 * timui_end, so the renderer can't clobber it). `visible` is where it's drawn;
 * `full` is the uncropped rect (== visible when not clipping). The caller
 * reserves the region (draws its own background, no text). */
static void image_record_(Timui *ui, TimuiImage *img, TimuiRect visible, TimuiRect full){
    TimuiImageProtocol protocol;
    if(!ui || visible.w <= 0 || visible.h <= 0) return;
    if(!image_rect_contains_(full, visible)){
        image_placeholder_(ui, visible);
        return;
    }
    protocol = timui_image_protocol(ui);
    if(protocol == TIMUI_IMAGE_PROTOCOL_KITTY ||
       (protocol == TIMUI_IMAGE_PROTOCOL_ITERM2 && image_rect_same_(visible, full)) ||
       (protocol == TIMUI_IMAGE_PROTOCOL_SIXEL && image_rect_same_(visible, full) && sixel_image_supported_(img))){
        if(!image_rect_emit_valid_(visible)){
            image_placeholder_(ui, visible);
            return;
        }
        if(ui->img_place_count < (int)(sizeof(ui->img_place) / sizeof(ui->img_place[0]))){
            ui->img_place[ui->img_place_count].img  = img;
            ui->img_place[ui->img_place_count].rect = visible;
            ui->img_place[ui->img_place_count].full = full;
            ui->img_place_count++;
        }
    } else {
        image_placeholder_(ui, visible);
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
