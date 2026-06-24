/*
 * vt_render.c — replay a captured terminal byte stream (see pty_drive) through a
 * minimal VT model and print the final screen as plain text. This makes a raw
 * capture human-readable and script-assertable: e.g.
 *   make drive-chat && ./build/vt_render recordings/chat.raw | grep "you: hi"
 * so a scripted session becomes an acceptance check. Handles CUP, printable
 * glyphs (with deferred auto-wrap + scroll), and skips SGR / mode / OSC.
 *
 * usage: vt_render [--cols N] [--rows N] [FILE]   (FILE or stdin)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXW 400
#define MAXH 200
static unsigned int g[MAXH][MAXW];
static int W = 80, H = 40, cx, cy, pending, autowrap = 1;

static void scroll_up(void){
    int y, x;
    for(y = 0; y < H - 1; y++) for(x = 0; x < W; x++) g[y][x] = g[y+1][x];
    for(x = 0; x < W; x++) g[H-1][x] = ' ';
}
/* Minimal wcwidth mirroring timui_utf8_width: combining -> 0, CJK/emoji -> 2. */
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
static void put(unsigned int cp){
    int w = cp_width(cp);
    if(w == 0) return;                            /* combining/zero-width: don't place or advance */
    if(pending && autowrap){ cx = 0; cy++; pending = 0; if(cy >= H){ scroll_up(); cy = H - 1; } }
    if(cy >= 0 && cy < H && cx >= 0 && cx < W) g[cy][cx] = cp;
    if(w >= 2 && cy >= 0 && cy < H && cx + 1 < W) g[cy][cx + 1] = ' ';   /* blank continuation cell */
    cx += w;
    if(cx >= W){ cx = W; pending = 1; }
}
/* decode one UTF-8 codepoint; returns bytes consumed */
static int utf8(const unsigned char *s, int n, unsigned int *out){
    if(n <= 0){ *out = 0; return 1; }
    if(s[0] < 0x80){ *out = s[0]; return 1; }
    if((s[0] & 0xE0) == 0xC0 && n >= 2){ *out = ((s[0]&0x1F)<<6)|(s[1]&0x3F); return 2; }
    if((s[0] & 0xF0) == 0xE0 && n >= 3){ *out = ((s[0]&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F); return 3; }
    if((s[0] & 0xF8) == 0xF0 && n >= 4){ *out = ((s[0]&0x07)<<18)|((s[1]&0x3F)<<12)|((s[2]&0x3F)<<6)|(s[3]&0x3F); return 4; }
    *out = 0xFFFD; return 1;
}
static void feed(const unsigned char *s, long n){
    long i = 0;
    while(i < n){
        unsigned char c = s[i];
        if(c == 0x1b && i+1 < n && s[i+1] == '['){          /* CSI */
            long j = i + 2; int priv = 0, p[8], np = 1, k;
            for(k = 0; k < 8; k++) p[k] = 0;
            if(j < n && s[j] == '?'){ priv = 1; j++; }
            while(j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')){
                if(s[j] == ';'){ if(np < 8) np++; } else if(np-1 < 8) p[np-1] = p[np-1]*10 + (s[j]-'0');
                j++;
            }
            if(j < n){
                char f = (char)s[j];
                if(!priv && f == 'H'){ cy = (np>=1?p[0]:1)-1; cx = (np>=2?p[1]:1)-1; if(cx<0)cx=0; if(cy<0)cy=0; pending=0; }
                else if(priv && p[0] == 7 && (f=='l'||f=='h')) autowrap = (f=='h');
                i = j + 1; continue;
            }
        }
        if(c == 0x1b && i+1 < n && s[i+1] == ']'){           /* OSC ... ST/BEL */
            long j = i + 2;
            while(j < n && s[j] != 0x07 && !(s[j]==0x1b && j+1<n && s[j+1]=='\\')) j++;
            if(j < n && s[j] == 0x1b) j++;
            i = (j < n) ? j + 1 : n; continue;
        }
        if(c == 0x1b){ i += 2; continue; }
        if(c == '\r'){ cx = 0; pending = 0; i++; continue; }
        if(c == '\n'){ cy++; if(cy >= H){ scroll_up(); cy = H-1; } pending = 0; i++; continue; }
        if(c < 0x20){ i++; continue; }
        { unsigned int cp; int adv = utf8(s + i, (int)(n - i), &cp); put(cp); i += adv; }
    }
}

int main(int argc, char **argv){
    const char *path = NULL; int i, x, y;
    unsigned char *buf; long cap = 1 << 16, len = 0; FILE *fp;
    for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "--cols") && i+1 < argc) W = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--rows") && i+1 < argc) H = atoi(argv[++i]);
        else path = argv[i];
    }
    if(W > MAXW) W = MAXW; if(H > MAXH) H = MAXH;
    for(y = 0; y < H; y++) for(x = 0; x < W; x++) g[y][x] = ' ';
    fp = path ? fopen(path, "rb") : stdin;
    if(!fp){ perror("open"); return 1; }
    buf = (unsigned char *)malloc((size_t)cap);
    for(;;){
        int ch = fgetc(fp);
        if(ch == EOF) break;
        if(len >= cap){ cap *= 2; buf = (unsigned char *)realloc(buf, (size_t)cap); }
        buf[len++] = (unsigned char)ch;
    }
    if(path) fclose(fp);
    feed(buf, len);
    free(buf);
    for(y = 0; y < H; y++){
        int last = W - 1; while(last >= 0 && g[y][last] == ' ') last--;   /* trim trailing spaces */
        for(x = 0; x <= last; x++){
            unsigned int cp = g[y][x];
            if(cp < 0x80) putchar((int)cp);
            else if(cp < 0x800){ putchar((int)(0xC0|(cp>>6))); putchar((int)(0x80|(cp&0x3F))); }
            else { putchar((int)(0xE0|(cp>>12))); putchar((int)(0x80|((cp>>6)&0x3F))); putchar((int)(0x80|(cp&0x3F))); }
        }
        putchar('\n');
    }
    return 0;
}
