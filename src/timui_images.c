/* ---- Terminal images (v0.2) ------------------------------------------- *
 * Accept PNG bytes and raw RGBA pixels; unsupported protocol/data pairs draw
 * a "[img]" placeholder instead of guessing. */

#ifndef TIMUI_NO_IMAGES
#ifndef TIMUI_IMAGE_PNG_MAX_DIMENSION
#define TIMUI_IMAGE_PNG_MAX_DIMENSION 4096
#endif
#ifndef TIMUI_IMAGE_PNG_MAX_PIXELS
#define TIMUI_IMAGE_PNG_MAX_PIXELS 16777216u
#endif
#ifndef STBI_MAX_DIMENSIONS
#define TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS TIMUI_IMAGE_PNG_MAX_DIMENSION
#endif
#ifndef STB_IMAGE_STATIC
#define TIMUI_UNDEF_STB_IMAGE_STATIC
#define STB_IMAGE_STATIC
#endif
#ifndef STB_IMAGE_IMPLEMENTATION
#define TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#ifndef STBI_ONLY_PNG
#define TIMUI_UNDEF_STBI_ONLY_PNG
#define STBI_ONLY_PNG
#endif
#ifndef STBI_NO_STDIO
#define TIMUI_UNDEF_STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#ifndef STBI_NO_LINEAR
#define TIMUI_UNDEF_STBI_NO_LINEAR
#define STBI_NO_LINEAR
#endif
#ifndef STBI_NO_HDR
#define TIMUI_UNDEF_STBI_NO_HDR
#define STBI_NO_HDR
#endif
#ifndef STBI_NO_THREAD_LOCALS
#define TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#define STBI_NO_THREAD_LOCALS
#endif
#ifndef STBI_NO_FAILURE_STRINGS
#define TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#define STBI_NO_FAILURE_STRINGS
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../tools/vendor/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#undef STBI_NO_FAILURE_STRINGS
#undef TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#endif
#ifdef TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#undef STBI_NO_THREAD_LOCALS
#undef TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#endif
#ifdef TIMUI_UNDEF_STBI_NO_HDR
#undef STBI_NO_HDR
#undef TIMUI_UNDEF_STBI_NO_HDR
#endif
#ifdef TIMUI_UNDEF_STBI_NO_LINEAR
#undef STBI_NO_LINEAR
#undef TIMUI_UNDEF_STBI_NO_LINEAR
#endif
#ifdef TIMUI_UNDEF_STBI_NO_STDIO
#undef STBI_NO_STDIO
#undef TIMUI_UNDEF_STBI_NO_STDIO
#endif
#ifdef TIMUI_UNDEF_STBI_ONLY_PNG
#undef STBI_ONLY_PNG
#undef TIMUI_UNDEF_STBI_ONLY_PNG
#endif
#ifdef TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#undef TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#endif
#ifdef TIMUI_UNDEF_STB_IMAGE_STATIC
#undef STB_IMAGE_STATIC
#undef TIMUI_UNDEF_STB_IMAGE_STATIC
#endif
#ifdef TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#undef STBI_MAX_DIMENSIONS
#undef TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#endif
#endif

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

#ifndef TIMUI_NO_IMAGES
static int image_png_header_(const void *data, size_t size, int *out_w, int *out_h){
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    const unsigned char *d;
    uint32_t w, h;
    if(!data || size < 24) return 0;
    d = (const unsigned char *)data;
    if(memcmp(d, sig, sizeof sig) != 0) return 0;
    if(d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R') return 0;
    w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
    h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
    if(w == 0 || h == 0 || w > (uint32_t)INT_MAX || h > (uint32_t)INT_MAX) return 0;
    if(out_w) *out_w = (int)w;
    if(out_h) *out_h = (int)h;
    return 1;
}
#endif

TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size){
    TimuiImage *img;
    TimuiAllocator al;
    int w = 0, h = 0;
    (void)ui;
    if(!data || size == 0 || size > (size_t)TIMUI_IMAGE_PNG_MAX_BYTES) return NULL;
#ifdef TIMUI_NO_IMAGES
    (void)w; (void)h;
#else
    if(!image_png_header_(data, size, &w, &h)) return NULL;
#endif
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, size);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    memcpy(img->data, data, size);
    img->len = size;
    img->rgba = NULL;
    img->rgba_len = 0;
    img->kind = TIMUI_IMAGE_KIND_PNG;
    img->stride = 0;
    img->id = 0;                 /* assigned on first transmit (timui_images_flush_) */
    /* pixel size from the PNG IHDR (width @16, height @20, big-endian) so a
     * placement can be cropped to a cell sub-rect (smooth scroll clipping). */
    img->px_w = w;
    img->px_h = h;
    return img;
}

static int image_rgba_size_(int w, int h, int stride, size_t *out_row, size_t *out_total){
    size_t row;
    uint64_t pixels;
    if(w <= 0 || h <= 0) return 0;
    if(w > TIMUI_IMAGE_MAX_DIMENSION || h > TIMUI_IMAGE_MAX_DIMENSION) return 0;
    pixels = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    if(pixels > (uint64_t)TIMUI_IMAGE_MAX_PIXELS) return 0;
    if(w > INT_MAX / 4) return 0;
    row = (size_t)w * 4u;
    if(stride < (int)row) return 0;
    if((size_t)h > SIZE_MAX / row) return 0;
    if(out_row) *out_row = row;
    if(out_total) *out_total = row * (size_t)h;
    return 1;
}

static void image_copy_rows_(unsigned char *dst, const unsigned char *src,
                             int h, size_t row, int stride){
    int y;
    for(y = 0; y < h; y++)
        memcpy(dst + (size_t)y * row, src + (size_t)y * (size_t)stride, row);
}

#ifndef TIMUI_NO_IMAGES
static int image_png_preflight_(const TimuiImage *img, int *out_w, int *out_h){
    int w = 0, h = 0;
    uint64_t pixels;
    if(!img || img->len == 0 || img->len > (size_t)TIMUI_IMAGE_PNG_MAX_BYTES ||
       img->len > (size_t)INT_MAX) return 0;
    if(!image_png_header_(img->data, img->len, &w, &h)) return 0;
    if(w > TIMUI_IMAGE_PNG_MAX_DIMENSION || h > TIMUI_IMAGE_PNG_MAX_DIMENSION) return 0;
    pixels = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    if(pixels > (uint64_t)TIMUI_IMAGE_PNG_MAX_PIXELS) return 0;
    if((uint32_t)w > (uint32_t)(INT_MAX / 4)) return 0;
    if(out_w) *out_w = (int)w;
    if(out_h) *out_h = (int)h;
    return 1;
}

static int image_decode_png_rgba_(TimuiImage *img){
    int want_w = 0, want_h = 0, w = 0, h = 0, comp = 0;
    size_t row = 0, total = 0;
    unsigned char *rgba;
    if(!img) return 0;
    if(img->rgba) return 1;
    if(img->kind != TIMUI_IMAGE_KIND_PNG) return 0;
    if(!image_png_preflight_(img, &want_w, &want_h)) return 0;
    rgba = stbi_load_from_memory(img->data, (int)img->len, &w, &h, &comp, 4);
    (void)comp;
    if(!rgba) return 0;
    if(w != want_w || h != want_h || !image_rgba_size_(w, h, w * 4, &row, &total)){
        stbi_image_free(rgba);
        return 0;
    }
    img->rgba = rgba;
    img->rgba_len = total;
    img->px_w = w;
    img->px_h = h;
    img->stride = (int)row;
    return 1;
}
#endif

TIMUI_API TimuiImage *timui_image_from_rgba(Timui *ui, const void *rgba, int w, int h, int stride){
    TimuiImage *img;
    TimuiAllocator al;
    size_t row = 0, total = 0;
    const unsigned char *src;
    (void)ui;
    if(!rgba || !image_rgba_size_(w, h, stride, &row, &total)) return NULL;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, total);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    src = (const unsigned char *)rgba;
    image_copy_rows_(img->data, src, h, row, stride);
    img->len = total;
    img->rgba = img->data;
    img->rgba_len = total;
    img->id = 0;
    img->px_w = w;
    img->px_h = h;
    img->kind = TIMUI_IMAGE_KIND_RGBA;
    img->stride = (int)row;
    return img;
}
TIMUI_API TimuiImage *timui_image_from_png_rgba(Timui *ui, const void *png,
                                                size_t png_size,
                                                const void *rgba,
                                                int w, int h, int stride){
    TimuiImage *img;
    TimuiAllocator al;
    size_t row = 0, total = 0;
    const unsigned char *src;
#ifndef TIMUI_NO_IMAGES
    int png_w = 0, png_h = 0;
#endif
    (void)ui;
    if(!png || png_size == 0 || png_size > (size_t)TIMUI_IMAGE_PNG_MAX_BYTES ||
       !rgba || !image_rgba_size_(w, h, stride, &row, &total))
        return NULL;
#ifndef TIMUI_NO_IMAGES
    if(!image_png_header_(png, png_size, &png_w, &png_h)) return NULL;
    if(png_w != w || png_h != h) return NULL;
#endif
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    memset(img, 0, sizeof *img);
    img->data = (unsigned char *)al.alloc(al.userdata, png_size);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    img->rgba = (unsigned char *)al.alloc(al.userdata, total);
    if(!img->rgba){
        al.free(al.userdata, img->data, png_size);
        al.free(al.userdata, img, sizeof *img);
        return NULL;
    }
    memcpy(img->data, png, png_size);
    src = (const unsigned char *)rgba;
    image_copy_rows_(img->rgba, src, h, row, stride);
    img->len = png_size;
    img->rgba_len = total;
    img->id = 0;
    img->px_w = w;
    img->px_h = h;
    img->kind = TIMUI_IMAGE_KIND_PNG_RGBA;
    img->stride = (int)row;
    return img;
}
TIMUI_API void timui_image_free(Timui *ui, TimuiImage *img){
    TimuiAllocator al;
    (void)ui;
    if(!img) return;
    al = timui_default_allocator();
    if(img->rgba && img->rgba != img->data){
#ifndef TIMUI_NO_IMAGES
        if(img->kind == TIMUI_IMAGE_KIND_PNG)
            stbi_image_free(img->rgba);
        else
#endif
            al.free(al.userdata, img->rgba, img->rgba_len);
    }
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

typedef struct {
    int sx, sy, sw, sh;
} SixelCrop_;

typedef struct {
    SixelColor_ colors[SIXEL_MAX_COLORS];
    int count;
    int quantized;
} SixelPalette_;

static int image_rect_emit_valid_(TimuiRect r);
static int image_rect_contains_(TimuiRect outer, TimuiRect inner);

static int image_has_png_(const TimuiImage *img){
    return img && img->data && img->len > 0 &&
           (img->kind == TIMUI_IMAGE_KIND_PNG || img->kind == TIMUI_IMAGE_KIND_PNG_RGBA);
}

static const unsigned char *image_rgba_(const TimuiImage *img){
    if(!img) return NULL;
    return img->rgba ? img->rgba :
           ((img->kind == TIMUI_IMAGE_KIND_RGBA) ? img->data : NULL);
}

static size_t image_rgba_len_(const TimuiImage *img){
    if(!img) return 0;
    if(img->rgba) return img->rgba_len;
    return (img->kind == TIMUI_IMAGE_KIND_RGBA) ? img->len : 0;
}

static int sixel_is_rgba_(const TimuiImage *img){
    size_t row, stride, need;
    if(!img || !image_rgba_(img)) return 0;
    if(img->px_w <= 0 || img->px_h <= 0 || img->px_w > INT_MAX / 4) return 0;
    if(img->stride <= 0) return 0;
    row = (size_t)img->px_w * 4u;
    stride = (size_t)img->stride;
    if(stride < row) return 0;
    if((size_t)(img->px_h - 1) > (SIZE_MAX - row) / stride) return 0;
    need = (size_t)(img->px_h - 1) * stride + row;
    return image_rgba_len_(img) >= need;
}

static int sixel_palette_index_(const SixelColor_ *pal, int count,
                                unsigned char r, unsigned char g, unsigned char b){
    int i;
    for(i = 0; i < count; i++)
        if(pal[i].r == r && pal[i].g == g && pal[i].b == b) return i;
    return -1;
}

static const SixelColor_ sixel_quant16_[SIXEL_MAX_COLORS] = {
    {   0,   0,   0 }, { 128,   0,   0 }, {   0, 128,   0 }, { 128, 128,   0 },
    {   0,   0, 128 }, { 128,   0, 128 }, {   0, 128, 128 }, { 192, 192, 192 },
    { 128, 128, 128 }, { 255,   0,   0 }, {   0, 255,   0 }, { 255, 255,   0 },
    {   0,   0, 255 }, { 255,   0, 255 }, {   0, 255, 255 }, { 255, 255, 255 }
};

static unsigned sixel_color_dist_(SixelColor_ c,
                                  unsigned char r, unsigned char g, unsigned char b){
    int dr = (int)c.r - (int)r;
    int dg = (int)c.g - (int)g;
    int db = (int)c.b - (int)b;
    return (unsigned)(dr * dr + dg * dg + db * db);
}

static SixelColor_ sixel_quantize_color_(unsigned char r, unsigned char g, unsigned char b){
    int i, best = 0;
    unsigned best_dist = sixel_color_dist_(sixel_quant16_[0], r, g, b);
    for(i = 1; i < SIXEL_MAX_COLORS; i++){
        unsigned dist = sixel_color_dist_(sixel_quant16_[i], r, g, b);
        if(dist < best_dist){
            best = i;
            best_dist = dist;
        }
    }
    return sixel_quant16_[best];
}

static int sixel_palette_add_(SixelPalette_ *pal, SixelColor_ c){
    if(!pal) return 0;
    if(sixel_palette_index_(pal->colors, pal->count, c.r, c.g, c.b) >= 0) return 1;
    if(pal->count >= SIXEL_MAX_COLORS) return 0;
    pal->colors[pal->count++] = c;
    return 1;
}

static int sixel_crop_(const TimuiImage *img, TimuiRect full, TimuiRect visible, SixelCrop_ *out){
    int64_t fx0, fy0, fx1, fy1;
    int64_t sx0, sy0, sx1, sy1;
    if(!sixel_is_rgba_(img) || !out || !image_rect_contains_(full, visible))
        return 0;
    fx0 = (int64_t)visible.x - (int64_t)full.x;
    fy0 = (int64_t)visible.y - (int64_t)full.y;
    fx1 = fx0 + (int64_t)visible.w;
    fy1 = fy0 + (int64_t)visible.h;
    sx0 = fx0 * (int64_t)img->px_w / (int64_t)full.w;
    sy0 = fy0 * (int64_t)img->px_h / (int64_t)full.h;
    sx1 = (fx1 * (int64_t)img->px_w + (int64_t)full.w - 1) / (int64_t)full.w;
    sy1 = (fy1 * (int64_t)img->px_h + (int64_t)full.h - 1) / (int64_t)full.h;
    if(sx0 < 0) sx0 = 0;
    if(sy0 < 0) sy0 = 0;
    if(sx1 > img->px_w) sx1 = img->px_w;
    if(sy1 > img->px_h) sy1 = img->px_h;
    if(sx1 <= sx0 || sy1 <= sy0) return 0;
    out->sx = (int)sx0;
    out->sy = (int)sy0;
    out->sw = (int)(sx1 - sx0);
    out->sh = (int)(sy1 - sy0);
    return 1;
}

static int sixel_palette_crop_(const TimuiImage *img, SixelCrop_ crop, SixelPalette_ *pal){
    int x, y;
    if(!sixel_is_rgba_(img) || !pal) return 0;
    if(crop.sx < 0 || crop.sy < 0 || crop.sw <= 0 || crop.sh <= 0) return 0;
    if(crop.sx > img->px_w || crop.sy > img->px_h) return 0;
    if(crop.sw > img->px_w - crop.sx || crop.sh > img->px_h - crop.sy) return 0;
    memset(pal, 0, sizeof *pal);
    for(y = crop.sy; y < crop.sy + crop.sh; y++){
        const unsigned char *row = image_rgba_(img) + (size_t)y * (size_t)img->stride;
        for(x = crop.sx; x < crop.sx + crop.sw; x++){
            const unsigned char *px = row + (size_t)x * 4u;
            if(px[3] < 128) continue;
            if(sixel_palette_index_(pal->colors, pal->count, px[0], px[1], px[2]) >= 0)
                continue;
            if(pal->count >= SIXEL_MAX_COLORS) goto quantize;
            pal->colors[pal->count].r = px[0];
            pal->colors[pal->count].g = px[1];
            pal->colors[pal->count].b = px[2];
            pal->count++;
        }
    }
    return pal->count > 0;

quantize:
    memset(pal, 0, sizeof *pal);
    pal->quantized = 1;
    for(y = crop.sy; y < crop.sy + crop.sh; y++){
        const unsigned char *row = image_rgba_(img) + (size_t)y * (size_t)img->stride;
        for(x = crop.sx; x < crop.sx + crop.sw; x++){
            const unsigned char *px = row + (size_t)x * 4u;
            SixelColor_ q;
            if(px[3] < 128) continue;
            q = sixel_quantize_color_(px[0], px[1], px[2]);
            if(!sixel_palette_add_(pal, q)) return 0;
        }
    }
    return pal->count > 0;
}

static int sixel_palette_(const TimuiImage *img, SixelPalette_ *pal){
    SixelCrop_ crop;
    if(!sixel_is_rgba_(img)) return 0;
    crop.sx = 0;
    crop.sy = 0;
    crop.sw = img->px_w;
    crop.sh = img->px_h;
    return sixel_palette_crop_(img, crop, pal);
}

static int sixel_image_supported_(const TimuiImage *img){
    SixelPalette_ pal;
    return sixel_palette_(img, &pal);
}

static int sixel_image_supported_at_(const TimuiImage *img, TimuiRect full, TimuiRect visible){
    SixelPalette_ pal;
    SixelCrop_ crop;
    return sixel_crop_(img, full, visible, &crop) &&
           sixel_palette_crop_(img, crop, &pal);
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

static int sixel_palette_pixel_matches_(const SixelPalette_ *pal, int idx,
                                        const unsigned char *px){
    SixelColor_ c;
    if(!pal || !px || idx < 0 || idx >= pal->count || px[3] < 128) return 0;
    c = pal->quantized ? sixel_quantize_color_(px[0], px[1], px[2])
                       : (SixelColor_){ px[0], px[1], px[2] };
    return c.r == pal->colors[idx].r && c.g == pal->colors[idx].g &&
           c.b == pal->colors[idx].b;
}

static void sixel_target_size_(SixelCrop_ crop, TimuiRect r, int cell_px_w, int cell_px_h,
                               int *out_w, int *out_h){
    int w = crop.sw, h = crop.sh;
    if(cell_px_w > 0 && cell_px_h > 0 &&
       r.w > 0 && r.h > 0 &&
       r.w <= INT_MAX / cell_px_w && r.h <= INT_MAX / cell_px_h){
        w = r.w * cell_px_w;
        h = r.h * cell_px_h;
    }
    if(out_w) *out_w = w;
    if(out_h) *out_h = h;
}

static const unsigned char *sixel_sample_pixel_(const TimuiImage *img, SixelCrop_ crop,
                                                int x, int y, int out_w, int out_h){
    int sx, sy;
    if(!img || out_w <= 0 || out_h <= 0) return NULL;
    sx = crop.sx + (int)(((int64_t)x * (int64_t)crop.sw) / (int64_t)out_w);
    sy = crop.sy + (int)(((int64_t)y * (int64_t)crop.sh) / (int64_t)out_h);
    if(sx < crop.sx) sx = crop.sx;
    if(sy < crop.sy) sy = crop.sy;
    if(sx >= crop.sx + crop.sw) sx = crop.sx + crop.sw - 1;
    if(sy >= crop.sy + crop.sh) sy = crop.sy + crop.sh - 1;
    return image_rgba_(img) + (size_t)sy * (size_t)img->stride + (size_t)sx * 4u;
}

static int sixel_emit_(TimuiTransport *t, const TimuiImage *img, TimuiRect r, TimuiRect full,
                       int cell_px_w, int cell_px_h){
    SixelPalette_ pal;
    SixelCrop_ crop;
    int ci, x, band;
    int out_w, out_h;
    char b[64];
    int n;
    const char *p;
    if(!t || !t->write || !image_rect_emit_valid_(r)) return 0;
    if(!sixel_crop_(img, full, r, &crop)) return 0;
    if(!sixel_palette_crop_(img, crop, &pal)) return 0;
    sixel_target_size_(crop, r, cell_px_w, cell_px_h, &out_w, &out_h);
    if(out_w <= 0 || out_h <= 0) return 0;
    if(!image_cup_(t, r.x, r.y)) return 0;
    image_write_all_(t, "\x1bP0;1;0q", sizeof("\x1bP0;1;0q") - 1);
    n = 0;
    b[n++] = '"'; b[n++] = '1'; b[n++] = ';'; b[n++] = '1'; b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)out_w); b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)out_h);
    image_write_all_(t, b, (size_t)n);
    for(ci = 0; ci < pal.count; ci++) sixel_emit_color_def_(t, ci, pal.colors[ci]);
    for(band = 0; band < out_h; band += 6){
        for(ci = 0; ci < pal.count; ci++){
            n = 0;
            b[n++] = '#';
            n += fmt_uint(b + n, (unsigned)(ci + 1));
            image_write_all_(t, b, (size_t)n);
            for(x = 0; x < out_w; x++){
                int bit;
                unsigned bits = 0;
                for(bit = 0; bit < 6; bit++){
                    int y = band + bit;
                    const unsigned char *px;
                    if(y >= out_h) continue;
                    px = sixel_sample_pixel_(img, crop, x, y, out_w, out_h);
                    if(sixel_palette_pixel_matches_(&pal, ci, px))
                        bits |= (1u << bit);
                }
                b[0] = (char)(0x3f + bits);
                image_write_all_(t, b, 1);
            }
            p = (ci + 1 < pal.count) ? "$" : ((band + 6 < out_h) ? "-" : "");
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
            if(!image_has_png_(img)) continue;
            if(r.x < 0 || r.y < 0 || r.x == INT_MAX || r.y == INT_MAX) continue;
            /* If the visible rect is a sub-slice of `full`, crop the source
             * pixels to match, so the image clips smoothly at pane edges. */
            if(img->px_w > 0 && img->px_h > 0 && full.w > 0 && full.h > 0 &&
               (r.x != full.x || r.w != full.w || r.y != full.y || r.h != full.h)){
                int64_t sx64 = ((int64_t)r.x - (int64_t)full.x) * (int64_t)img->px_w / (int64_t)full.w;
                int64_t sw64 = (int64_t)r.w * (int64_t)img->px_w / (int64_t)full.w;
                int64_t sy64 = ((int64_t)r.y - (int64_t)full.y) * (int64_t)img->px_h / (int64_t)full.h;
                int64_t sh64 = (int64_t)r.h * (int64_t)img->px_h / (int64_t)full.h;
                if(sx64 < 0) sx64 = 0;
                if(sx64 > img->px_w) sx64 = img->px_w;
                if(sw64 < 1) sw64 = 1;
                if(sx64 + sw64 > img->px_w) sw64 = (int64_t)img->px_w - sx64;
                if(sw64 < 1) sw64 = 1;
                if(sy64 < 0) sy64 = 0;
                if(sy64 > img->px_h) sy64 = img->px_h;
                if(sh64 < 1) sh64 = 1;
                if(sy64 + sh64 > img->px_h) sh64 = (int64_t)img->px_h - sy64;
                if(sh64 < 1) sh64 = 1;
                sx = (int)sx64;
                sw = (int)sw64;
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
            emitted += sixel_emit_(&ui->transport, ui->img_place[i].img,
                                   ui->img_place[i].rect, ui->img_place[i].full,
                                   ui->cell_px_w, ui->cell_px_h);
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
#ifndef TIMUI_NO_IMAGES
    if(protocol == TIMUI_IMAGE_PROTOCOL_SIXEL && img && img->kind == TIMUI_IMAGE_KIND_PNG)
        (void)image_decode_png_rgba_(img);
#endif
    if((protocol == TIMUI_IMAGE_PROTOCOL_KITTY && image_has_png_(img)) ||
       (protocol == TIMUI_IMAGE_PROTOCOL_ITERM2 && image_has_png_(img) && image_rect_same_(visible, full)) ||
       (protocol == TIMUI_IMAGE_PROTOCOL_SIXEL &&
        ((image_rect_same_(visible, full) && sixel_image_supported_(img)) ||
         (!image_rect_same_(visible, full) && sixel_image_supported_at_(img, full, visible))))){
        if(!image_rect_emit_valid_(visible)){
            image_placeholder_(ui, visible);
            return;
        }
        if(ui->img_place_count < (int)(sizeof(ui->img_place) / sizeof(ui->img_place[0]))){
            ui->img_place[ui->img_place_count].img  = img;
            ui->img_place[ui->img_place_count].rect = visible;
            ui->img_place[ui->img_place_count].full = full;
            ui->img_place_count++;
        } else {
            image_placeholder_(ui, visible);
        }
    } else {
        image_placeholder_(ui, visible);
    }
}
TIMUI_API void timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r){
    Timui *ui;
    TimuiRect active, visible;
    if(!f || !f->ui || !img || r.w <= 0 || r.h <= 0) return;
    ui = f->ui;
    active = ui->curr.has_clip ? ui->curr.clip : TIMUI_RECT(0, 0, ui->curr.w, ui->curr.h);
    visible = timui_intersect_rect_(r, active);
    if(visible.w <= 0 || visible.h <= 0) return;
    image_record_(ui, img, visible, r);
}
TIMUI_API void timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                        TimuiRect full, TimuiRect visible){
    Timui *ui;
    TimuiRect active;
    if(!f || !f->ui || !img || visible.w <= 0 || visible.h <= 0) return;
    ui = f->ui;
    active = ui->curr.has_clip ? ui->curr.clip : TIMUI_RECT(0, 0, ui->curr.w, ui->curr.h);
    visible = timui_intersect_rect_(visible, active);
    if(visible.w <= 0 || visible.h <= 0) return;
    image_record_(ui, img, visible, full);
}
