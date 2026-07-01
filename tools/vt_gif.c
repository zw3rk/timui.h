/*
 * vt_gif.c — render a captured terminal byte stream (see pty_drive) to PIXELS,
 * including Kitty-graphics inline images, and emit a PNG (final frame) or an
 * animated GIF. This is the headless recorder asciinema/agg/VHS can't be: they
 * drop Kitty images because those are terminal-rasterized pixels, not text
 * cells. We already parse the cell stream + Kitty APC; this adds a monospace
 * font rasterizer + PNG decode + image compositing (see docs/research/
 * kitty-gif-renderer.md, Path A).
 *
 *   usage: vt_gif [--cols N] [--rows M] [--png OUT.png | --gif OUT.gif]
 *                 [--timing FILE] [--fps N] [FILE]
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "vendor/stb_image_resize2.h"   /* NB: UBSAN flags this lib's internal
    * negative-index pointer arithmetic on the --width path; benign (output is
    * correct), and only in a sanitizer build. vt_gif's own code is UBSAN-clean. */
#define MSF_GIF_IMPL
#include "vendor/msf_gif.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include "vendor/vt_font_ttf.h"   /* subset DejaVu Sans Mono (make gen-font-ttf) */
#include "vendor/emoji_atlas.h"   /* curated Twemoji colour-emoji atlas (make gen-emoji) */
#include "vendor/vt_font_cjk.h"   /* bundled Unifont CJK bitmaps, deflated (make gen-cjk) */

#define MAXW 400
#define MAXH 200
#define MAXIMG 64
#define DEF_FG 0xCCCCCCu
#define DEF_BG 0x0E0E14u
/* attr bits */
#define A_IT   16u
#define A_BOLD 1u
#define A_REV  2u
#define A_UL   4u
#define A_DIM  8u

typedef struct { unsigned int cp, fg, bg; unsigned char attr; } Cell;
static Cell g[MAXH][MAXW];
static int W = 100, H = 30, cx, cy, pending, autowrap = 1;
static unsigned int cur_fg = DEF_FG, cur_bg = DEF_BG;
static unsigned char cur_attr = 0;

/* ---- Kitty graphics: retained PNG (base64) + current placements ---------- */
static struct { unsigned id; unsigned char *b64; long len, cap; } tx[MAXIMG];
static int tx_n;
static struct { unsigned id, place; int x, y, c, r, sx, sy, sw, sh; } pl[MAXIMG];
static int pl_n;
static unsigned cur_tx;

static int tx_index(unsigned id){
    int k;
    for(k = 0; k < tx_n; k++) if(tx[k].id == id) return k;
    if(tx_n >= MAXIMG) return -1;
    tx[tx_n].id = id; tx[tx_n].b64 = NULL; tx[tx_n].len = tx[tx_n].cap = 0;
    return tx_n++;
}
static void tx_append(unsigned id, const unsigned char *b, long n){
    int k = tx_index(id);
    if(k < 0 || n <= 0) return;
    if(tx[k].len + n > tx[k].cap){
        long nc = tx[k].cap ? tx[k].cap * 2 : 4096;
        while(nc < tx[k].len + n) nc *= 2;
        tx[k].b64 = (unsigned char *)realloc(tx[k].b64, (size_t)nc);
        tx[k].cap = nc;
    }
    memcpy(tx[k].b64 + tx[k].len, b, (size_t)n);
    tx[k].len += n;
}

/* ---- colour ------------------------------------------------------------- */
static const unsigned int ansi16[16] = {
    0x000000,0xCD0000,0x00CD00,0xCDCD00,0x2222DD,0xCD00CD,0x00CDCD,0xE5E5E5,
    0x7F7F7F,0xFF5555,0x55FF55,0xFFFF55,0x5C5CFF,0xFF55FF,0x55FFFF,0xFFFFFF };
static unsigned int xterm256(int n){
    if(n < 0) n = 0;
    if(n < 16) return ansi16[n];
    if(n < 232){ int c = n - 16, r = c / 36, gg = (c / 6) % 6, b = c % 6;
        int R = r ? r*40+55 : 0, G = gg ? gg*40+55 : 0, B = b ? b*40+55 : 0;
        return ((unsigned)R<<16)|((unsigned)G<<8)|(unsigned)B; }
    { int v = (n - 232) * 10 + 8; return ((unsigned)v<<16)|((unsigned)v<<8)|(unsigned)v; }
}
static void sgr(const int *p, int np){
    int i;
    if(np <= 0){ cur_fg = DEF_FG; cur_bg = DEF_BG; cur_attr = 0; return; }
    for(i = 0; i < np; i++){
        int v = p[i];
        if(v == 0){ cur_fg = DEF_FG; cur_bg = DEF_BG; cur_attr = 0; }
        else if(v == 1) cur_attr |= A_BOLD;
        else if(v == 2) cur_attr |= A_DIM;
        else if(v == 3) cur_attr |= A_IT;
        else if(v == 4) cur_attr |= A_UL;
        else if(v == 7) cur_attr |= A_REV;
        else if(v == 22) cur_attr &= ~(A_BOLD | A_DIM);
        else if(v == 23) cur_attr &= ~A_IT;
        else if(v == 24) cur_attr &= ~A_UL;
        else if(v == 27) cur_attr &= ~A_REV;
        else if(v >= 30 && v <= 37) cur_fg = ansi16[v - 30];
        else if(v == 39) cur_fg = DEF_FG;
        else if(v >= 40 && v <= 47) cur_bg = ansi16[v - 40];
        else if(v == 49) cur_bg = DEF_BG;
        else if(v >= 90 && v <= 97)  cur_fg = ansi16[8 + v - 90];
        else if(v >= 100 && v <= 107) cur_bg = ansi16[8 + v - 100];
        else if(v == 38 && i + 2 < np && p[i+1] == 5){ cur_fg = xterm256(p[i+2]); i += 2; }
        else if(v == 48 && i + 2 < np && p[i+1] == 5){ cur_bg = xterm256(p[i+2]); i += 2; }
        else if(v == 38 && i + 4 < np && p[i+1] == 2){
            cur_fg = ((unsigned)p[i+2]<<16)|((unsigned)p[i+3]<<8)|(unsigned)p[i+4]; i += 4; }
        else if(v == 48 && i + 4 < np && p[i+1] == 2){
            cur_bg = ((unsigned)p[i+2]<<16)|((unsigned)p[i+3]<<8)|(unsigned)p[i+4]; i += 4; }
    }
}

/* ---- base64 decode ------------------------------------------------------ */
static long b64decode(const unsigned char *in, long n, unsigned char *out){
    static signed char T[256];
    static int init = 0;
    long i, o = 0; unsigned int acc = 0; int bits = 0;
    if(!init){
        int k; const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(k = 0; k < 256; k++) T[k] = -1;
        for(k = 0; k < 64; k++) T[(unsigned char)A[k]] = (signed char)k;
        init = 1;
    }
    for(i = 0; i < n; i++){
        signed char d = T[in[i]];
        if(d < 0) continue;                 /* skip newlines / '=' padding */
        acc = (acc << 6) | (unsigned)d; bits += 6;
        if(bits >= 8){ bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 0xFF);
                       acc &= (1u << bits) - 1u; }   /* keep only leftover bits (no overflow) */
    }
    return o;
}

/* ---- VT cell model ------------------------------------------------------ */
static void scroll_up(void){
    int y, x;
    for(y = 0; y < H - 1; y++) for(x = 0; x < W; x++) g[y][x] = g[y+1][x];
    for(x = 0; x < W; x++){ g[H-1][x].cp = ' '; g[H-1][x].fg = DEF_FG; g[H-1][x].bg = DEF_BG; g[H-1][x].attr = 0; }
}
static int cp_width(unsigned int cp){
    if(cp < 0x20 || cp == 0x7F) return 0;
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;
    if((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
       (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
       (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
       (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F000)) return 2;
    /* emoji-presentation BMP (✨ ❤ ⚡ ⭐ …) render as square 2-cell glyphs, not
     * squished into one cell — matches how terminals show them. */
    if((cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF)) return 2;
    return 1;
}
static void set_cell(int y, int x, unsigned int cp){
    if(y < 0 || y >= H || x < 0 || x >= W) return;
    g[y][x].cp = cp; g[y][x].fg = cur_fg; g[y][x].bg = cur_bg; g[y][x].attr = cur_attr;
}
static void put(unsigned int cp){
    int w = cp_width(cp);
    if(w == 0) return;
    if(pending && autowrap){ cx = 0; cy++; pending = 0; if(cy >= H){ scroll_up(); cy = H - 1; } }
    set_cell(cy, cx, cp);
    if(w >= 2) set_cell(cy, cx + 1, ' ');
    cx += w;
    if(cx >= W){ cx = W; pending = 1; }
}
static int utf8(const unsigned char *s, int n, unsigned int *out){
    if(n <= 0){ *out = 0; return 1; }
    if(s[0] < 0x80){ *out = s[0]; return 1; }
    if((s[0] & 0xE0) == 0xC0 && n >= 2){ *out = ((s[0]&0x1F)<<6)|(s[1]&0x3F); return 2; }
    if((s[0] & 0xF0) == 0xE0 && n >= 3){ *out = ((s[0]&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F); return 3; }
    if((s[0] & 0xF8) == 0xF0 && n >= 4){ *out = ((s[0]&0x07)<<18)|((s[1]&0x3F)<<12)|((s[2]&0x3F)<<6)|(s[3]&0x3F); return 4; }
    *out = 0xFFFD; return 1;
}

/* Parse one Kitty APC (ESC _ G ... ST). Advances nothing; caller passes the key
 * region [ks,ke) and the payload [ps,pe). */
static void kitty(const unsigned char *s, long ks, long ke, long ps, long pe){
    char action = 0, delkind = 0;
    unsigned id = 0, place = 0; int cc = 0, rr = 0, sx = 0, sy = 0, sw = 0, sh = 0;
    long j = ks;
    while(j < ke){
        char key = (char)s[j]; j++;
        if(j < ke && s[j] == '='){
            j++;
            if(key == 'a'){ if(j < ke) action = (char)s[j++]; }
            else if(key == 'd'){ if(j < ke) delkind = (char)s[j++]; }
            else {
                long v = 0; int neg = 0;
                if(j < ke && s[j] == '-'){ neg = 1; j++; }
                while(j < ke && s[j] >= '0' && s[j] <= '9'){ v = v*10 + (s[j]-'0'); j++; }
                if(neg) v = -v;
                switch(key){
                    case 'i': id = (unsigned)v; break;   case 'p': place = (unsigned)v; break;
                    case 'c': cc = (int)v; break;        case 'r': rr = (int)v; break;
                    case 'x': sx = (int)v; break;        case 'y': sy = (int)v; break;
                    case 'w': sw = (int)v; break;        case 'h': sh = (int)v; break;
                    default: break;
                }
            }
        }
        if(j < ke && s[j] == ',') j++;
    }
    if(action == 't'){ cur_tx = id; tx_append(id, s + ps, pe - ps); }
    else if(action == 0 && cur_tx){ tx_append(cur_tx, s + ps, pe - ps); }   /* continuation chunk */
    else if(action == 'p'){
        if(pl_n < MAXIMG){
            pl[pl_n].id = id; pl[pl_n].place = place; pl[pl_n].x = cx; pl[pl_n].y = cy;
            pl[pl_n].c = cc; pl[pl_n].r = rr; pl[pl_n].sx = sx; pl[pl_n].sy = sy;
            pl[pl_n].sw = sw; pl[pl_n].sh = sh; pl_n++;
        }
    }
    else if(action == 'd' && (delkind == 'a' || delkind == 0)) pl_n = 0;   /* delete all placements */
}

/* Replay bytes into the model. Returns the number of bytes CONSUMED: if the
 * chunk ends mid-sequence (a CSI/OSC/APC/UTF-8 split across a read boundary, as
 * the --timing offsets do), it stops before the partial tail so the caller can
 * re-feed it with the next chunk. Feeding a split sequence would desync the
 * parser and smear cells — the bug that put stray text on the image row. */
static long feed(const unsigned char *s, long n){
    long i = 0;
    while(i < n){
        unsigned char c = s[i];
        if(c == 0x1b){
            if(i+1 >= n) return i;                          /* incomplete: lone ESC */
            if(s[i+1] == '['){                              /* CSI */
                long j = i + 2; int priv = 0, p[32], np = 1, k;
                for(k = 0; k < 32; k++) p[k] = 0;
                if(j < n && (s[j] == '?' || s[j] == '>' || s[j] == '=')){ priv = 1; j++; }
                while(j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';' || s[j] == ':')){
                    if(s[j] == ';' || s[j] == ':'){ if(np < 32) np++; }
                    else if(np-1 < 32) p[np-1] = p[np-1]*10 + (s[j]-'0');
                    j++;
                }
                if(j >= n) return i;                        /* incomplete: no final byte */
                { char f = (char)s[j];
                  if(!priv && f == 'H'){ cy=(np>=1?p[0]:1)-1; cx=(np>=2?p[1]:1)-1; if(cx<0)cx=0; if(cy<0)cy=0; pending=0; }
                  else if(!priv && f == 'm') sgr(p, np);
                  else if(priv && p[0] == 7 && (f=='l'||f=='h')) autowrap = (f=='h'); }
                i = j + 1; continue;
            }
            if(s[i+1] == ']'){                              /* OSC ... ST/BEL */
                long j = i + 2;
                while(j < n && s[j] != 0x07 && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
                if(j >= n) return i;                        /* incomplete: no terminator */
                i = (s[j] == 0x1b) ? j + 2 : j + 1; continue;
            }
            if(s[i+1] == '_'){                              /* APC — Kitty graphics */
                long j = i + 2;
                if(j < n && s[j] == 'G'){
                    long ks, ke, ps, pe;
                    j++; ks = j;
                    while(j < n && s[j] != ';' && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
                    ke = j;
                    ps = (j < n && s[j] == ';') ? j + 1 : j; pe = ps;
                    while(pe < n && !(s[pe]==0x1b && pe+1<n && s[pe+1]=='\\')) pe++;
                    if(pe >= n) return i;                   /* incomplete: no ST — re-feed */
                    kitty(s, ks, ke, ps, pe);
                    i = pe + 2; continue;
                } else {
                    long j2 = i + 2;
                    while(j2 < n && !(s[j2]==0x1b && j2+1<n && s[j2+1]=='\\')) j2++;
                    if(j2 >= n) return i;
                    i = j2 + 2; continue;
                }
            }
            i += 2; continue;                               /* other 2-byte ESC (have both) */
        }
        if(c == '\r'){ cx = 0; pending = 0; i++; continue; }
        if(c == '\n'){ cy++; if(cy >= H){ scroll_up(); cy = H-1; } pending = 0; i++; continue; }
        if(c == '\b'){ if(cx > 0) cx--; pending = 0; i++; continue; }
        if(c < 0x20){ i++; continue; }
        { int need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
          unsigned int cp; int adv;
          if(n - i < need) return i;                       /* incomplete UTF-8 at boundary */
          adv = utf8(s + i, (int)(n - i), &cp); put(cp); i += adv; }
    }
    return i;
}

/* ---- rasterization ------------------------------------------------------ */
/* ---- text face: stb_truetype outline rasterizer + glyph cache ------------ *
 * The cache is the tool's one deliberate mutable global: glyph rasterization is
 * pure given (cp, scale) and the scale is fixed per run, so memoizing coverage
 * bitmaps by codepoint is a safe, contained optimization for a many-frame run. */
static int cellw = 8, cellh = 16;   /* cellw derived from face 0; cellh from --cell-h */

/* Primary text face = bundled DejaVu Sans Mono in 4 styles (regular/bold/oblique/
 * bold-oblique); the variant is picked from the cell's bold/italic attributes.
 * Fallback chain `face[]` holds the CJK outline faces (system fonts). */
typedef struct { stbtt_fontinfo info; float scale; int baseline, loaded; } Face;
static Face pface[4];                 /* 0=regular 1=bold 2=oblique 3=bold-oblique */
#define MAXFACE 8
static Face face[MAXFACE];            /* CJK/RTL fallback faces (system) */
static int nface;
static int use_system_fonts;          /* --system-fonts: chain OS CJK fonts */
static const char *cjk_font_override; /* --cjk-font PATH */

/* read a whole file into a malloc'd buffer (a system font); leaked for the run */
static unsigned char *load_file(const char *path, long *out_len){
    FILE *f = fopen(path, "rb");
    unsigned char *b; long n;
    if(!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)(n > 0 ? n : 1));
    if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){ free(b); b = NULL; }
    fclose(f);
    if(out_len) *out_len = n;
    return b;
}
static void face_add(const unsigned char *data){
    int asc, desc, gap;
    if(nface >= MAXFACE || !data) return;
    if(!stbtt_InitFont(&face[nface].info, data, stbtt_GetFontOffsetForIndex(data, 0))) return;
    face[nface].scale = stbtt_ScaleForPixelHeight(&face[nface].info, (float)cellh);
    stbtt_GetFontVMetrics(&face[nface].info, &asc, &desc, &gap);
    face[nface].baseline = (int)(asc * face[nface].scale + 0.5f);
    nface++;
}
static void pface_load(Face *p, const unsigned char *data){
    int asc, desc, gap;
    if(!stbtt_InitFont(&p->info, data, stbtt_GetFontOffsetForIndex(data, 0))) return;
    p->scale = stbtt_ScaleForPixelHeight(&p->info, (float)cellh);
    stbtt_GetFontVMetrics(&p->info, &asc, &desc, &gap);
    p->baseline = (int)(asc * p->scale + 0.5f);
    p->loaded = 1;
}
static void font_init(void){
    int adv, lsb;
    pface_load(&pface[0], vt_font_ttf_regular);  /* primary DejaVu, 4 styles */
    pface_load(&pface[1], vt_font_ttf_bold);
    pface_load(&pface[2], vt_font_ttf_oblique);
    pface_load(&pface[3], vt_font_ttf_boldob);
    stbtt_GetCodepointHMetrics(&pface[0].info, 'M', &adv, &lsb);
    cellw = (int)(adv * pface[0].scale + 0.5f);
    if(cellw < 1) cellw = 1;
    /* CJK face(s): an explicit --cjk-font, or (--system-fonts) the first few OS
     * CJK fonts found — chained so Han + Hangul + Kana are all covered. */
    if(cjk_font_override){
        long n; unsigned char *d = load_file(cjk_font_override, &n);
        if(d) face_add(d);
        else fprintf(stderr, "vt_gif: --cjk-font: cannot read %s\n", cjk_font_override);
    } else if(use_system_fonts){
        static const char *cands[] = {                     /* script-specific first, so a */
            "/System/Library/Fonts/ArialHB.ttc",           /* CJK font can't shadow these */
            "/System/Library/Fonts/GeezaPro.ttc",          /* Arabic            */
            "/System/Library/Fonts/PingFang.ttc",          /* modern macOS CJK  */
            "/System/Library/Fonts/Hiragino Sans GB.ttc",  /* Han (C/J)         */
            "/System/Library/Fonts/AppleSDGothicNeo.ttc",  /* Hangul (Korean)   */
            "/System/Library/Fonts/Arial Unicode.ttf",     /* broad BMP fallback*/
            NULL };
        int i, added = 0;
        for(i = 0; cands[i] && nface < MAXFACE; i++){
            long n; unsigned char *d = load_file(cands[i], &n);
            if(d){ face_add(d); added = 1; }
        }
        if(!added) fprintf(stderr, "vt_gif: --system-fonts: no CJK font found (falling back)\n");
    }
}

/* Bundled Unifont CJK bitmap fallback (always present, after any system CJK). The
 * deflated (cp:u32-LE, 16x16 bitmap:32B) records are inflated once at startup. */
static unsigned char *cjk_raw;
static int cjk_count;
static void cjk_init(void){
    int outlen = 0;
    cjk_raw = (unsigned char *)stbi_zlib_decode_malloc((const char *)vt_font_cjk_z, vt_font_cjk_zlen, &outlen);
    if(cjk_raw && outlen == VT_CJK_RAW) cjk_count = VT_CJK_COUNT;
    else { free(cjk_raw); cjk_raw = NULL; }
}
static const unsigned char *cjk_bitmap(unsigned int cp){   /* 32-byte 16x16 glyph, or NULL */
    int lo = 0, hi = cjk_count - 1;
    while(lo <= hi){
        int mid = (lo + hi) / 2;
        const unsigned char *r = cjk_raw + (long)mid * 36;
        unsigned rec = (unsigned)r[0] | ((unsigned)r[1] << 8) | ((unsigned)r[2] << 16) | ((unsigned)r[3] << 24);
        if(rec == cp) return r + 4;
        if(rec < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Rasterized glyph: grayscale coverage + offsets + baseline (bitmap glyphs sit at
 * the cell top). Cached per (cp, style); cov may be NULL (space / missing). */
typedef struct { unsigned int cp; int style; unsigned char *cov; int w, h, xoff, yoff, baseline, bitmap; } Glyph;
static Glyph gcache[8192];
static int   gcache_n;
/* style: 0=regular 1=bold 2=italic 3=bold-italic (matches pface[]) */
static Glyph *get_glyph(unsigned int cp, int style){
    int i, f;
    Glyph *g2;
    for(i = 0; i < gcache_n; i++) if(gcache[i].cp == cp && gcache[i].style == style) return &gcache[i];
    if(gcache_n >= (int)(sizeof gcache / sizeof gcache[0])) return NULL;
    g2 = &gcache[gcache_n++];
    g2->cp = cp; g2->style = style; g2->cov = NULL;
    g2->w = g2->h = g2->xoff = g2->yoff = g2->baseline = g2->bitmap = 0;
    if(pface[0].loaded && stbtt_FindGlyphIndex(&pface[0].info, (int)cp) != 0){   /* primary DejaVu */
        Face *p = pface[style & 3].loaded ? &pface[style & 3] : &pface[0];
        g2->cov = stbtt_GetCodepointBitmap(&p->info, p->scale, p->scale,
                                           (int)cp, &g2->w, &g2->h, &g2->xoff, &g2->yoff);
        g2->baseline = p->baseline;
        return g2;
    }
    for(f = 0; f < nface; f++){                   /* CJK outline fallback (system fonts) */
        if(stbtt_FindGlyphIndex(&face[f].info, (int)cp) == 0) continue;
        g2->cov = stbtt_GetCodepointBitmap(&face[f].info, face[f].scale, face[f].scale,
                                           (int)cp, &g2->w, &g2->h, &g2->xoff, &g2->yoff);
        g2->baseline = face[f].baseline;
        return g2;
    }
    if(cjk_raw){                                   /* bundled Unifont CJK bitmap fallback */
        const unsigned char *bm = cjk_bitmap(cp);
        if(bm){
            int gw = 2*cellw, gh = cellh, py, px;  /* fullwidth: 2 cells */
            g2->cov = (unsigned char *)malloc((size_t)gw*gh);
            g2->w = gw; g2->h = gh; g2->bitmap = 1;
            for(py = 0; py < gh; py++) for(px = 0; px < gw; px++){   /* nearest-neighbour 16x16 -> cell */
                int sx = px*16/gw, sy = py*16/gh;
                int bit = (bm[sy*2 + (sx>>3)] >> (7 - (sx&7))) & 1;
                g2->cov[py*gw+px] = bit ? 255 : 0;
            }
        }
    }
    return g2;
}
/* decoded-image cache (decode each transmitted id once) */
static struct { unsigned id; unsigned char *rgba; int w, h; } dec[MAXIMG];
static int dec_n;
static int decode_image(unsigned id, unsigned char **rgba, int *iw, int *ih){
    int k; long blen; unsigned char *png; int w, h, comp; unsigned char *out;
    for(k = 0; k < dec_n; k++) if(dec[k].id == id){ *rgba = dec[k].rgba; *iw = dec[k].w; *ih = dec[k].h; return dec[k].rgba != NULL; }
    k = -1;
    { int t; for(t = 0; t < tx_n; t++) if(tx[t].id == id) k = t; }
    if(k < 0 || dec_n >= MAXIMG){ *rgba = NULL; return 0; }
    png = (unsigned char *)malloc((size_t)tx[k].len);
    blen = b64decode(tx[k].b64, tx[k].len, png);
    out = stbi_load_from_memory(png, (int)blen, &w, &h, &comp, 4);
    free(png);
    dec[dec_n].id = id; dec[dec_n].rgba = out; dec[dec_n].w = w; dec[dec_n].h = h; dec_n++;
    *rgba = out; *iw = w; *ih = h;
    return out != NULL;
}
/* blend a solid colour over a dest RGBA pixel with coverage cov (0-255) */
static void blend(unsigned char *px, unsigned int col, int cov){
    int r = (int)((col>>16)&0xFF), gg = (int)((col>>8)&0xFF), b = (int)(col&0xFF);
    px[0] = (unsigned char)((px[0]*(255-cov) + r*cov)/255);
    px[1] = (unsigned char)((px[1]*(255-cov) + gg*cov)/255);
    px[2] = (unsigned char)((px[2]*(255-cov) + b*cov)/255);
    px[3] = 255;
}
/* ---- colour emoji face ---------------------------------------------------- *
 * --system-emoji reads the macOS Apple Color Emoji font and pulls the PNG for a
 * codepoint from its `sbix` bitmap-strike table (cmap -> glyphID via stbtt,
 * largest strike). Bundled Twemoji is a follow-up; this is the native path. */
static unsigned char *emoji_data;
static stbtt_fontinfo emoji_font;
static long emoji_strike_off;
static int  emoji_ready, use_system_emoji;

static unsigned      u16be_(const unsigned char *p){ return ((unsigned)p[0]<<8)|p[1]; }
static unsigned long u32be_(const unsigned char *p){
    return ((unsigned long)p[0]<<24)|((unsigned long)p[1]<<16)|((unsigned long)p[2]<<8)|p[3]; }
static long sfnt_table(const unsigned char *d, long fo, const char *tag){
    int n = (int)u16be_(d + fo + 4), i; long rec = fo + 12;
    for(i = 0; i < n; i++, rec += 16) if(memcmp(d + rec, tag, 4) == 0) return (long)u32be_(d + rec + 8);
    return -1;
}
static void emoji_init(void){
    long fo, sbix, n; int nstr, i, best = 0;
    if(!use_system_emoji) return;
    emoji_data = load_file("/System/Library/Fonts/Apple Color Emoji.ttc", &n);
    if(!emoji_data){ fprintf(stderr, "vt_gif: --system-emoji: Apple Color Emoji not found\n"); return; }
    fo = stbtt_GetFontOffsetForIndex(emoji_data, 0);
    if(!stbtt_InitFont(&emoji_font, emoji_data, fo)){ emoji_data = NULL; return; }
    sbix = sfnt_table(emoji_data, fo, "sbix");
    if(sbix < 0){ fprintf(stderr, "vt_gif: --system-emoji: no sbix table\n"); emoji_data = NULL; return; }
    nstr = (int)u32be_(emoji_data + sbix + 4);
    for(i = 0; i < nstr; i++){                     /* pick the largest-ppem strike */
        long so = sbix + (long)u32be_(emoji_data + sbix + 8 + 4*i);
        int ppem = (int)u16be_(emoji_data + so);
        if(ppem >= best){ best = ppem; emoji_strike_off = so; }
    }
    emoji_ready = 1;
}
static int is_emoji(unsigned int cp){
    return cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}
/* decoded-emoji cache: cp -> RGBA (NULL if the font has no colour glyph) */
static struct { unsigned int cp; unsigned char *rgba; int w, h; } edec[512];
static int edec_n;
static int get_emoji(unsigned int cp, unsigned char **rgba, int *w, int *h){
    int i, gid, comp; const unsigned char *st; long go, gn, gd; unsigned char *out = NULL;
    for(i = 0; i < edec_n; i++) if(edec[i].cp == cp){ *rgba = edec[i].rgba; *w = edec[i].w; *h = edec[i].h; return edec[i].rgba != NULL; }
    if(edec_n >= (int)(sizeof edec / sizeof edec[0])) return 0;
    if(emoji_ready){                               /* 1. system Apple Color Emoji (sbix) */
        gid = stbtt_FindGlyphIndex(&emoji_font, (int)cp);
        if(gid > 0 && gid < emoji_font.numGlyphs){
            st = emoji_data + emoji_strike_off + 4;    /* skip ppem, ppi */
            go = (long)u32be_(st + 4*gid); gn = (long)u32be_(st + 4*(gid+1));
            if(gn - go > 8 && (unsigned)u32be_(emoji_data + emoji_strike_off + go + 4) == 0x706E6720u){ /* 'png ' */
                gd = emoji_strike_off + go + 8;        /* originX(2)+originY(2)+tag(4) */
                out = stbi_load_from_memory(emoji_data + gd, (int)((gn - go) - 8), w, h, &comp, 4);
            }
        }
    }
    if(!out){                                      /* 2. bundled Twemoji atlas (default) */
        for(i = 0; i < emoji_atlas_n; i++) if(emoji_atlas[i].cp == cp){
            out = stbi_load_from_memory(emoji_atlas[i].png, emoji_atlas[i].len, w, h, &comp, 4);
            break;
        }
    }
    edec[edec_n].cp = cp; edec[edec_n].rgba = out;
    edec[edec_n].w = out ? *w : 0; edec[edec_n].h = out ? *h : 0; edec_n++;
    *rgba = out;
    return out != NULL;
}

static void render_frame(unsigned char *fb, int pw, int ph){
    int y, x, gy, gx, k;
    /* Pass 1 — backgrounds. Kept separate from glyphs so a wide (2-cell) glyph
     * drawn in pass 2 isn't overpainted by the NEXT cell's background fill. */
    for(y = 0; y < H; y++) for(x = 0; x < W; x++){
        Cell cell = g[y][x];
        unsigned int bg = (cell.attr & A_REV) ? cell.fg : cell.bg;
        for(gy = 0; gy < cellh; gy++) for(gx = 0; gx < cellw; gx++){
            unsigned char *px = fb + ((long)(y*cellh+gy)*pw + (x*cellw+gx))*4;
            px[0]=(unsigned char)((bg>>16)&0xFF); px[1]=(unsigned char)((bg>>8)&0xFF);
            px[2]=(unsigned char)(bg&0xFF);       px[3]=255;
        }
    }
    /* Pass 2 — glyphs on top. */
    for(y = 0; y < H; y++) for(x = 0; x < W; x++){
        Cell cell = g[y][x];
        unsigned int fg = (cell.attr & A_REV) ? cell.bg : cell.fg;
        Glyph *gl;
        if(cell.attr & A_DIM) fg = ((fg>>1)&0x7F7F7F);
        if(!cell.cp || cell.cp == ' ') continue;
        if(is_emoji(cell.cp)){                    /* colour emoji: composite RGBA */
            unsigned char *er; int ew, eh;
            if(get_emoji(cell.cp, &er, &ew, &eh)){
                int nc = cp_width(cell.cp) >= 2 ? 2 : 1;
                int dw = nc*cellw, dh = cellh, dx0 = x*cellw, dy0 = y*cellh, dy2, dx2;
                for(dy2 = 0; dy2 < dh; dy2++) for(dx2 = 0; dx2 < dw; dx2++){
                    int sx = dx2*ew/dw, sy = dy2*eh/dh, px_ = dx0+dx2, py_ = dy0+dy2, a;
                    unsigned char *s2, *d2;
                    if(px_ >= pw || py_ >= ph) continue;
                    s2 = er + ((long)sy*ew+sx)*4; d2 = fb + ((long)py_*pw+px_)*4; a = s2[3];
                    d2[0]=(unsigned char)((d2[0]*(255-a)+s2[0]*a)/255);
                    d2[1]=(unsigned char)((d2[1]*(255-a)+s2[1]*a)/255);
                    d2[2]=(unsigned char)((d2[2]*(255-a)+s2[2]*a)/255);
                    d2[3]=255;
                }
                continue;                          /* emoji drawn; skip the outline glyph */
            }
        }
        gl = get_glyph(cell.cp, ((cell.attr & A_BOLD) ? 1 : 0) | ((cell.attr & A_IT) ? 2 : 0));
        if(gl && gl->cov){
            int ox = x*cellw + gl->xoff, oy = y*cellh + (gl->bitmap ? 0 : gl->baseline) + gl->yoff;
            for(gy = 0; gy < gl->h; gy++) for(gx = 0; gx < gl->w; gx++){
                int c = gl->cov[gy*gl->w+gx], px_ = ox+gx, py_ = oy+gy;
                if(c && px_ >= 0 && px_ < pw && py_ >= 0 && py_ < ph)
                    blend(fb + ((long)py_*pw + px_)*4, fg, c);
            }
        }
    }
    /* composite Kitty images on top, cropped to their source sub-rect */
    for(k = 0; k < pl_n; k++){
        unsigned char *src; int iw, ih;
        int dx0 = pl[k].x*cellw, dy0 = pl[k].y*cellh;
        int dw = pl[k].c*cellw, dh = pl[k].r*cellh;
        int cxr, cyr, cwr, chr, dy2, dx2;
        if(dw <= 0 || dh <= 0 || !decode_image(pl[k].id, &src, &iw, &ih) || iw <= 0 || ih <= 0) continue;
        cxr = pl[k].sw > 0 ? pl[k].sx : 0;   cyr = pl[k].sh > 0 ? pl[k].sy : 0;
        cwr = pl[k].sw > 0 ? pl[k].sw : iw;  chr = pl[k].sh > 0 ? pl[k].sh : ih;
        for(dy2 = 0; dy2 < dh; dy2++) for(dx2 = 0; dx2 < dw; dx2++){
            int px_ = dx0+dx2, py_ = dy0+dy2, sxp, syp; unsigned char *s2, *d2; int a;
            if(px_ < 0 || px_ >= pw || py_ < 0 || py_ >= ph) continue;
            sxp = cxr + dx2*cwr/dw; syp = cyr + dy2*chr/dh;
            if(sxp < 0) sxp = 0; if(sxp >= iw) sxp = iw-1;
            if(syp < 0) syp = 0; if(syp >= ih) syp = ih-1;
            s2 = src + ((long)syp*iw + sxp)*4; d2 = fb + ((long)py_*pw + px_)*4;
            a = s2[3];
            d2[0] = (unsigned char)((d2[0]*(255-a) + s2[0]*a)/255);
            d2[1] = (unsigned char)((d2[1]*(255-a) + s2[1]*a)/255);
            d2[2] = (unsigned char)((d2[2]*(255-a) + s2[2]*a)/255);
            d2[3] = 255;
        }
    }
}

/* Downscale the rendered frame to (ow x oh) if requested, and write it as a
 * numbered PNG when --frames-dir is set; returns the buffer to feed the encoder. */
static unsigned char *frame_emit(unsigned char *fb, int pw, int ph, unsigned char *rb,
                                 int ow, int oh, const char *frames_dir, int *frame_no){
    unsigned char *o = fb;
    if(ow != pw || oh != ph){
        stbir_resize_uint8_srgb(fb, pw, ph, 0, rb, ow, oh, 0, STBIR_RGBA);
        o = rb;
    }
    if(frames_dir){
        char p[600];
        snprintf(p, sizeof p, "%s/frame_%05d.png", frames_dir, (*frame_no)++);
        stbi_write_png(p, ow, oh, 4, o, ow*4);
    }
    return o;
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv){
    const char *path = NULL, *png_out = NULL, *gif_out = NULL, *timing_path = NULL, *frames_dir = NULL, *outro = NULL;
    int i, x, y, pw, ph, fps = 15, out_width = 0, bit_depth = 16, ow, oh;
    unsigned char *buf, *fb, *rb = NULL; long cap = 1<<16, len = 0; FILE *fp;
    for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "--cols") && i+1 < argc) W = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--rows") && i+1 < argc) H = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--png") && i+1 < argc) png_out = argv[++i];
        else if(!strcmp(argv[i], "--gif") && i+1 < argc) gif_out = argv[++i];
        else if(!strcmp(argv[i], "--timing") && i+1 < argc) timing_path = argv[++i];
        else if(!strcmp(argv[i], "--fps") && i+1 < argc) fps = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--cell-h") && i+1 < argc) cellh = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--scale") && i+1 < argc) cellh = (int)(16 * atof(argv[++i]) + 0.5);
        else if(!strcmp(argv[i], "--system-fonts")) use_system_fonts = 1;
        else if(!strcmp(argv[i], "--system-emoji")) use_system_emoji = 1;
        else if(!strcmp(argv[i], "--cjk-font") && i+1 < argc) cjk_font_override = argv[++i];
        else if(!strcmp(argv[i], "--width") && i+1 < argc) out_width = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--bit-depth") && i+1 < argc) bit_depth = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--frames-dir") && i+1 < argc) frames_dir = argv[++i];
        else if(!strcmp(argv[i], "--outro") && i+1 < argc) outro = argv[++i];
        else path = argv[i];
    }
    if(fps < 1) fps = 15;
    if(bit_depth < 1 || bit_depth > 16) bit_depth = 16;
    if(cellh < 4) cellh = 16;
    font_init();                     /* outline face chain + cellw from the font */
    cjk_init();                      /* bundled Unifont CJK bitmap fallback */
    emoji_init();                    /* colour emoji face (--system-emoji) */
    if(W > MAXW) W = MAXW; if(H > MAXH) H = MAXH;
    for(y = 0; y < H; y++) for(x = 0; x < W; x++){ g[y][x].cp=' '; g[y][x].fg=DEF_FG; g[y][x].bg=DEF_BG; g[y][x].attr=0; }
    fp = path ? fopen(path, "rb") : stdin;
    if(!fp){ perror("open"); return 1; }
    buf = (unsigned char *)malloc((size_t)cap);
    for(;;){ int ch = fgetc(fp); if(ch == EOF) break;
        if(len >= cap){ cap *= 2; buf = (unsigned char *)realloc(buf, (size_t)cap); }
        buf[len++] = (unsigned char)ch; }
    if(path) fclose(fp);

    pw = W*cellw; ph = H*cellh;
    fb = (unsigned char *)malloc((size_t)pw*ph*4);
    /* optional downscale to --width (aspect preserved) for smaller output */
    ow = out_width > 0 ? out_width : pw;
    oh = out_width > 0 ? (int)((long)out_width * ph / pw) : ph;
    if(oh < 1) oh = 1;
    if(ow != pw || oh != ph) rb = (unsigned char *)malloc((size_t)ow*oh*4);

    if(gif_out || frames_dir){
        /* Incremental replay: at each frame tick feed the bytes captured up to
         * that wall-clock moment (from the --timing sidecar; even byte slices if
         * absent), render, and append a GIF frame. */
        long *tms = NULL, *toff = NULL; int tn = 0, tcap = 0;
        int interval = 1000 / fps, cs = (interval + 5) / 10, nframes = 0, frame_no = 0;
        long fed = 0, total, T;
        MsfGifState gs;
        MsfGifResult res;
        unsigned char *o;
        if(cs < 1) cs = 1;
        if(timing_path){
            FILE *tf = fopen(timing_path, "r"); long a, b;
            if(tf){
                while(fscanf(tf, "%ld %ld", &a, &b) == 2){
                    if(tn >= tcap){ tcap = tcap ? tcap*2 : 1024;
                        tms = realloc(tms, (size_t)tcap*sizeof(long)); toff = realloc(toff, (size_t)tcap*sizeof(long)); }
                    tms[tn] = a; toff[tn] = b; tn++;
                }
                fclose(tf);
            }
        }
        total = tn > 0 ? tms[tn-1] : 0;
        if(gif_out) msf_gif_begin(&gs, ow, oh);
        if(tn > 0){
            int k = 0;
            for(T = 0; T <= total; T += interval){
                long target = 0;
                while(k < tn && tms[k] <= T){ target = toff[k]; k++; }
                if(k > 0 && target == 0) target = toff[k-1];
                if(target > len) target = len;
                /* advance by CONSUMED bytes; a partial trailing sequence waits
                 * for the next tick's larger target (feed() stops before it). */
                while(fed < target){ long got = feed(buf + fed, target - fed); if(got == 0) break; fed += got; }
                render_frame(fb, pw, ph);
                o = frame_emit(fb, pw, ph, rb, ow, oh, frames_dir, &frame_no);
                if(gif_out) msf_gif_frame(&gs, o, cs, bit_depth, ow*4);
                nframes++;
            }
        } else {                                   /* no timing: 60 even byte slices */
            int N = 60, fr;
            for(fr = 1; fr <= N; fr++){
                long target = len * fr / N;
                while(fed < target){ long got = feed(buf + fed, target - fed); if(got == 0) break; fed += got; }
                render_frame(fb, pw, ph);
                o = frame_emit(fb, pw, ph, rb, ow, oh, frames_dir, &frame_no);
                if(gif_out) msf_gif_frame(&gs, o, cs, bit_depth, ow*4);
                nframes++;
            }
        }
        while(fed < len){ long got = feed(buf + fed, len - fed); if(got == 0) break; fed += got; }
        render_frame(fb, pw, ph);                  /* final settled frame, held ~1s */
        o = frame_emit(fb, pw, ph, rb, ow, oh, frames_dir, &frame_no);
        if(gif_out) msf_gif_frame(&gs, o, 100, bit_depth, ow*4);
        nframes++;
        if(outro){                                  /* slow crossfade to a centred splash */
            unsigned char *content = (unsigned char *)malloc((size_t)pw*ph*4);
            unsigned char *cf = (unsigned char *)malloc((size_t)pw*ph*4);
            int N = 26, fr, tw = 0, tx, ty, col; long pp, tot = (long)pw*ph*4;
            size_t oi = 0, olen = strlen(outro);
            memcpy(content, fb, (size_t)tot);
            pl_n = 0;                               /* drop image placements so no logo bleeds into the splash */
            for(y = 0; y < H; y++) for(x = 0; x < W; x++){ g[y][x].cp=' '; g[y][x].fg=0xE6E6E6; g[y][x].bg=0x000000; g[y][x].attr=0; }
            while(oi < olen){ unsigned int cp; int a = utf8((const unsigned char*)outro+oi, (int)(olen-oi), &cp);
                              tw += cp_width(cp); oi += a>0?a:1; }
            tx = (W - tw)/2; if(tx < 0) tx = 0; ty = H/2; col = tx; oi = 0;
            while(oi < olen){ unsigned int cp; int a = utf8((const unsigned char*)outro+oi, (int)(olen-oi), &cp);
                              int w = cp_width(cp);
                              if(w > 0 && ty >= 0 && ty < H && col >= 0 && col < W) g[ty][col].cp = cp;
                              col += w; oi += a>0?a:1; }
            render_frame(fb, pw, ph);               /* fb = splash */
            for(fr = 1; fr <= N; fr++){
                int aa = fr*255/N;
                for(pp = 0; pp < tot; pp++) cf[pp] = (unsigned char)((content[pp]*(255-aa) + fb[pp]*aa)/255);
                o = frame_emit(cf, pw, ph, rb, ow, oh, frames_dir, &frame_no);
                if(gif_out) msf_gif_frame(&gs, o, cs, bit_depth, ow*4);
                nframes++;
            }
            o = frame_emit(fb, pw, ph, rb, ow, oh, frames_dir, &frame_no);   /* hold the splash */
            if(gif_out) msf_gif_frame(&gs, o, 180, bit_depth, ow*4);
            nframes++;
            free(content); free(cf);
        }
        if(gif_out){
            res = msf_gif_end(&gs);
            if(res.data){ FILE *of = fopen(gif_out, "wb");
                if(of){ fwrite(res.data, 1, res.dataSize, of); fclose(of); }
                fprintf(stderr, "wrote %s (%d frames, %dx%d px, %.1f KB)\n",
                        gif_out, nframes, ow, oh, res.dataSize/1024.0);
            } else fprintf(stderr, "gif encode failed\n");
            msf_gif_free(res);
        }
        if(frames_dir)
            fprintf(stderr, "wrote %d frames to %s/frame_%%05d.png — e.g.\n"
                    "  ffmpeg -framerate %d -i %s/frame_%%05d.png -c:v libx264 -pix_fmt yuv420p out.mp4\n",
                    frame_no, frames_dir, fps, frames_dir);
        free(tms); free(toff);
    } else {
        feed(buf, len);
        render_frame(fb, pw, ph);
        if(png_out){
            int dummy = 0;
            unsigned char *o = frame_emit(fb, pw, ph, rb, ow, oh, NULL, &dummy);
            if(!stbi_write_png(png_out, ow, oh, 4, o, ow*4)){ fprintf(stderr, "png write failed\n"); return 1; }
            fprintf(stderr, "wrote %s (%dx%d px)\n", png_out, ow, oh);
        } else {
            fprintf(stderr, "usage: vt_gif --cols N --rows M "
                    "(--png OUT | --gif OUT | --frames-dir DIR) "
                    "[--timing F --fps N --width N --scale F --cell-h N --bit-depth N "
                    "--system-fonts --system-emoji] [FILE]\n");
        }
    }
    free(buf); free(fb); free(rb);
    return 0;
}
