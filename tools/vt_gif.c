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
#define MSF_GIF_IMPL
#include "vendor/msf_gif.h"
#include "vendor/vt_font.h"

#define MAXW 400
#define MAXH 200
#define MAXIMG 64
#define DEF_FG 0xCCCCCCu
#define DEF_BG 0x0E0E14u
/* attr bits */
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
        else if(v == 4) cur_attr |= A_UL;
        else if(v == 7) cur_attr |= A_REV;
        else if(v == 22) cur_attr &= ~(A_BOLD | A_DIM);
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
    long i, o = 0; int acc = 0, bits = 0;
    if(!init){
        int k; const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(k = 0; k < 256; k++) T[k] = -1;
        for(k = 0; k < 64; k++) T[(unsigned char)A[k]] = (signed char)k;
        init = 1;
    }
    for(i = 0; i < n; i++){
        signed char d = T[in[i]];
        if(d < 0) continue;                 /* skip newlines / '=' padding */
        acc = (acc << 6) | d; bits += 6;
        if(bits >= 8){ bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 0xFF); }
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
       (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF)) return 2;
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

static void feed(const unsigned char *s, long n){
    long i = 0;
    while(i < n){
        unsigned char c = s[i];
        if(c == 0x1b && i+1 < n && s[i+1] == '['){          /* CSI */
            long j = i + 2; int priv = 0, p[32], np = 1, k;
            for(k = 0; k < 32; k++) p[k] = 0;
            if(j < n && (s[j] == '?' || s[j] == '>' || s[j] == '=')){ priv = 1; j++; }
            while(j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';' || s[j] == ':')){
                if(s[j] == ';' || s[j] == ':'){ if(np < 32) np++; }
                else if(np-1 < 32) p[np-1] = p[np-1]*10 + (s[j]-'0');
                j++;
            }
            if(j < n){
                char f = (char)s[j];
                if(!priv && f == 'H'){ cy=(np>=1?p[0]:1)-1; cx=(np>=2?p[1]:1)-1; if(cx<0)cx=0; if(cy<0)cy=0; pending=0; }
                else if(!priv && f == 'm') sgr(p, np);
                else if(priv && p[0] == 7 && (f=='l'||f=='h')) autowrap = (f=='h');
                i = j + 1; continue;
            }
        }
        if(c == 0x1b && i+1 < n && s[i+1] == ']'){          /* OSC ... ST/BEL */
            long j = i + 2;
            while(j < n && s[j] != 0x07 && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
            if(j < n && s[j] == 0x1b) j++;
            i = (j < n) ? j + 1 : n; continue;
        }
        if(c == 0x1b && i+1 < n && s[i+1] == '_'){          /* APC — Kitty graphics */
            long j = i + 2;
            if(j < n && s[j] == 'G'){
                long ks, ke, ps, pe;
                j++; ks = j;
                while(j < n && s[j] != ';' && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
                ke = j;
                ps = (j < n && s[j] == ';') ? j + 1 : j; pe = ps;
                while(pe < n && !(s[pe]==0x1b && pe+1<n && s[pe+1]=='\\')) pe++;
                kitty(s, ks, ke, ps, pe);
                j = (pe < n) ? pe + 2 : pe;
            } else {
                while(j < n && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
                if(j < n) j += 2;
            }
            i = j; continue;
        }
        if(c == 0x1b){ i += 2; continue; }
        if(c == '\r'){ cx = 0; pending = 0; i++; continue; }
        if(c == '\n'){ cy++; if(cy >= H){ scroll_up(); cy = H-1; } pending = 0; i++; continue; }
        if(c == '\b'){ if(cx > 0) cx--; pending = 0; i++; continue; }
        if(c < 0x20){ i++; continue; }
        { unsigned int cp; int adv = utf8(s + i, (int)(n - i), &cp); put(cp); i += adv; }
    }
}

/* ---- rasterization ------------------------------------------------------ */
static const unsigned char *glyph(unsigned int cp){
    int k;
    for(k = 0; k < vt_font_n; k++) if(vt_font[k].cp == cp) return vt_font[k].cov;
    return NULL;
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
static void render_frame(unsigned char *fb, int pw, int ph){
    int y, x, gy, gx, k;
    (void)ph;
    for(y = 0; y < H; y++) for(x = 0; x < W; x++){
        Cell cell = g[y][x];
        unsigned int fg = cell.fg, bg = cell.bg;
        const unsigned char *cov;
        if(cell.attr & A_REV){ unsigned int t = fg; fg = bg; bg = t; }
        if(cell.attr & A_DIM) fg = ((fg>>1)&0x7F7F7F);
        for(gy = 0; gy < VT_FONT_CH; gy++) for(gx = 0; gx < VT_FONT_CW; gx++){
            unsigned char *px = fb + ((long)(y*VT_FONT_CH+gy)*pw + (x*VT_FONT_CW+gx))*4;
            px[0]=(unsigned char)((bg>>16)&0xFF); px[1]=(unsigned char)((bg>>8)&0xFF);
            px[2]=(unsigned char)(bg&0xFF);       px[3]=255;
        }
        cov = (cell.cp && cell.cp != ' ') ? glyph(cell.cp) : NULL;
        if(cov) for(gy = 0; gy < VT_FONT_CH; gy++) for(gx = 0; gx < VT_FONT_CW; gx++){
            int c = cov[gy*VT_FONT_CW+gx];
            if(c) blend(fb + ((long)(y*VT_FONT_CH+gy)*pw + (x*VT_FONT_CW+gx))*4, fg, c);
        }
    }
    /* composite Kitty images on top, cropped to their source sub-rect */
    for(k = 0; k < pl_n; k++){
        unsigned char *src; int iw, ih;
        int dx0 = pl[k].x*VT_FONT_CW, dy0 = pl[k].y*VT_FONT_CH;
        int dw = pl[k].c*VT_FONT_CW, dh = pl[k].r*VT_FONT_CH;
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

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv){
    const char *path = NULL, *png_out = NULL, *gif_out = NULL;
    int i, x, y, pw, ph;
    unsigned char *buf, *fb; long cap = 1<<16, len = 0; FILE *fp;
    for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "--cols") && i+1 < argc) W = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--rows") && i+1 < argc) H = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--png") && i+1 < argc) png_out = argv[++i];
        else if(!strcmp(argv[i], "--gif") && i+1 < argc) gif_out = argv[++i];
        else path = argv[i];
    }
    if(W > MAXW) W = MAXW; if(H > MAXH) H = MAXH;
    for(y = 0; y < H; y++) for(x = 0; x < W; x++){ g[y][x].cp=' '; g[y][x].fg=DEF_FG; g[y][x].bg=DEF_BG; g[y][x].attr=0; }
    fp = path ? fopen(path, "rb") : stdin;
    if(!fp){ perror("open"); return 1; }
    buf = (unsigned char *)malloc((size_t)cap);
    for(;;){ int ch = fgetc(fp); if(ch == EOF) break;
        if(len >= cap){ cap *= 2; buf = (unsigned char *)realloc(buf, (size_t)cap); }
        buf[len++] = (unsigned char)ch; }
    if(path) fclose(fp);
    feed(buf, len);
    free(buf);

    pw = W*VT_FONT_CW; ph = H*VT_FONT_CH;
    fb = (unsigned char *)malloc((size_t)pw*ph*4);
    render_frame(fb, pw, ph);
    if(png_out){
        if(!stbi_write_png(png_out, pw, ph, 4, fb, pw*4)){ fprintf(stderr, "png write failed\n"); return 1; }
        fprintf(stderr, "wrote %s (%dx%d px)\n", png_out, pw, ph);
    } else if(gif_out){
        fprintf(stderr, "gif output needs frame timing — not yet wired (see --png)\n");
    } else {
        fprintf(stderr, "usage: vt_gif --cols N --rows M (--png OUT | --gif OUT) [FILE]\n");
    }
    free(fb);
    (void)gif_out;
    return 0;
}
