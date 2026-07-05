/*
 * amalgamate.c — produce a self-contained release header by inlining the
 * `#include "../src/..."` section directives of include/timui.h.
 *
 *   amalgamate <input.h> <output.h>     e.g. amalgamate include/timui.h release/timui.h
 *
 * A `#include "..."` whose path contains "/src/" is resolved relative to the
 * input file's directory and inlined recursively; every other line (system
 * `<...>` includes, code) passes through verbatim. The result is a flat,
 * single-file header identical to the dev build's single-TU view.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include <stdio.h>
#include <string.h>

static int process(const char *path, FILE *out, int depth);

static int starts_with(const char *s, const char *p){
    while(*p){ if(*s++ != *p++) return 0; }
    return 1;
}

/* If `line` is a `#include "../src/..."`, inline that file; return 1 if handled. */
static int try_inline(const char *line, const char *infile, FILE *out, int depth){
    char path[512];
    const char *p = line;
    const char *inc;
    const char *q;
    size_t len;
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '#') return 0;
    p++;
    while(*p == ' ' || *p == '\t') p++;
    if(!starts_with(p, "include")) return 0;
    p += strlen("include");
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '"') return 0;             /* only quote includes; leave <...> as-is */
    p++;
    inc = p;
    q = inc;
    while(*q && *q != '"') q++;
    if(!*q) return 0;
    len = (size_t)(q - inc);
    if(len == 0 || len >= sizeof path) return 0;
    memcpy(path, inc, len);
    path[len] = '\0';
    if(!strstr(path, "/src/")) return 0;   /* only our section includes */
    {
        char dir[512];
        char full[1024];
        const char *slash = strrchr(infile, '/');
        size_t dlen = slash ? (size_t)(slash - infile) + 1 : 0;
        if(dlen) memcpy(dir, infile, dlen);
        dir[dlen] = '\0';
        snprintf(dir + dlen, sizeof dir - dlen, "%s", path);  /* append path to dir */
        snprintf(full, sizeof full, "%s", dir);               /* (dir already holds full) */
        if(process(full, out, depth + 1) != 0) return 0;
    }
    return 1;
}

static int process(const char *path, FILE *out, int depth){
    FILE *in;
    char line[4096];
    if(depth > 32){ fprintf(stderr, "amalgamate: include depth exceeded at %s\n", path); return -1; }
    in = fopen(path, "rb");
    if(!in){ fprintf(stderr, "amalgamate: cannot open %s\n", path); return -1; }
    while(fgets(line, sizeof line, in)){
        if(!try_inline(line, path, out, depth)) fputs(line, out);
    }
    fclose(in);
    return 0;
}

int main(int argc, char **argv){
    FILE *out;
    if(argc < 3){ fprintf(stderr, "usage: %s <input.h> <output.h>\n", argv[0]); return 2; }
    out = fopen(argv[2], "wb");
    if(!out){ perror(argv[2]); return 1; }
    fputs("/* ---- timui.h -- amalgamated release header -- do not edit by hand. ------ */\n", out);
    if(process(argv[1], out, 0) != 0){ fclose(out); return 1; }
    if(fclose(out) != 0){ perror(argv[2]); return 1; }
    return 0;
}
