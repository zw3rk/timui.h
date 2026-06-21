/*
 * procmon.c — htop-lite, read-only process monitor (timui.h demo).
 *
 * Showcases the table widget and the diff renderer under frequent updates:
 * the poll loop ticks at ~60 fps, but `ps` is re-run at most once per second,
 * so between refreshes the diff renderer emits almost nothing — an efficient
 * live display. READ-ONLY by design: there is deliberately no kill/signal
 * action, so the monitor can never perturb the processes it watches.
 *
 * Data source: `ps -eo pid,pcpu,pmem,comm` via popen(). This is portable
 * across macOS and Linux (no /proc parsing). Rows are sorted in-process;
 * keys 1/2/3/4 pick the sort column (toggling asc/desc), F10/ESC quit.
 *
 * Exercises: table widget · live timer refresh · sorting · diff-renderer
 * efficiency · popen parsing.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* popen()/pclose() are POSIX.1. Under -std=c99 glibc defines __STRICT_ANSI__
 * and hides them unless a feature-test macro is set; _DEFAULT_SOURCE exposes
 * them (plus the BSD/POSIX surface timui.h uses on Linux). macOS exposes the
 * full API by default, so scope this to Linux to leave the Darwin build
 * untouched. Must precede every #include. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#  define _DEFAULT_SOURCE 1
#endif

#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Data model ------------------------------------------------------- */

#define PROC_MAX 512

typedef struct {
    int    pid;
    double cpu;        /* %CPU  */
    double mem;        /* %MEM  */
    char   comm[64];   /* command name (may contain internal spaces) */
} Proc;

typedef struct {
    Proc procs[PROC_MAX];
    int  count;
    int  available;    /* 0 => `ps` could not be run — keep last data, say so */
} ProcTable;

/* Sort columns, aligned with the table's column order and the 1/2/3/4 keys. */
enum { SORT_PID = 0, SORT_CPU, SORT_MEM, SORT_CMD };

/* qsort() takes no context argument in C99 (qsort_r is non-portable), so the
 * comparator reads the active key/direction from two file-scope statics. They
 * are written only immediately before each qsort() call and read only by
 * cmp_proc — the mutable surface is two ints, confined to sorting. */
static int g_sort_key  = SORT_CPU;
static int g_sort_desc = 1;          /* default view: CPU, descending */

static int cmp_proc(const void *a, const void *b){
    const Proc *pa = (const Proc *)a;
    const Proc *pb = (const Proc *)b;
    int r = 0;
    switch(g_sort_key){
    case SORT_PID: r = (pa->pid > pb->pid) - (pa->pid < pb->pid); break;
    case SORT_CPU: r = (pa->cpu > pb->cpu) - (pa->cpu < pb->cpu); break;
    case SORT_MEM: r = (pa->mem > pb->mem) - (pa->mem < pb->mem); break;
    case SORT_CMD: r = strcmp(pa->comm, pb->comm);                break;
    default: break;
    }
    return g_sort_desc ? -r : r;
}

static void sort_table(ProcTable *t){
    if(t->count > 1)
        qsort(t->procs, (size_t)t->count, sizeof t->procs[0], cmp_proc);
}

/* Re-run `ps` and repopulate `t`. Returns 1 on success (even with 0 rows), 0
 * if `ps` could not be started — in which case the previous contents are kept
 * and t->available is cleared so the UI can report it. Parsing is deliberately
 * lax: the header line is skipped, leading whitespace is tolerated, and
 * malformed lines are dropped rather than aborting the scan. */
static int refresh_table(ProcTable *t){
    FILE *ps;
    char  line[512];
    int   n = 0;
    int   have_mem;

    /* 2>/dev/null: some hardened macOS contexts refuse %mem ("requires
     * entitlement") and print a warning; keep it off the alt-screen. */
    ps = popen("ps -eo pid,pcpu,pmem,comm 2>/dev/null", "r");
    if(!ps){ t->available = 0; return 0; }

    /* Read the header row and detect whether the %MEM column actually came
     * through — if the OS dropped it, the columns shift left, so parse
     * pid/cpu/comm and report MEM as 0.0 rather than mis-reading comm. */
    if(fgets(line, sizeof line, ps)){
        have_mem = (strstr(line, "MEM") != NULL);
        while(n < PROC_MAX && fgets(line, sizeof line, ps)){
            Proc p;
            int  ok;
            p.comm[0] = '\0';
            p.mem     = 0.0;
            /* The space before %63[^\n] skips the padding ps inserts, so comm
             * begins at the first non-space char; internal spaces (e.g.
             * "Google Chrome Helper") are preserved. A blank comm (rare kernel
             * thread) still yields the required leading numeric fields. */
            if(have_mem)
                ok = (sscanf(line, " %d %lf %lf %63[^\n]",
                             &p.pid, &p.cpu, &p.mem, p.comm) >= 3);
            else
                ok = (sscanf(line, " %d %lf %63[^\n]",
                             &p.pid, &p.cpu, p.comm) >= 2);
            if(ok) t->procs[n++] = p;
        }
    }
    pclose(ps);
    t->count     = n;
    t->available = 1;
    return 1;
}

/* ---- Table cell formatting -------------------------------------------- */

/* timui's table calls this once per visible cell and consumes the returned
 * pointer immediately (the text is copied into the cell buffer before the next
 * call). A small ring of static buffers keeps successive cells independent. */
static const char *proc_cell(void *ud, int row, int col){
    const ProcTable *t = (const ProcTable *)ud;
    static char ring[8][80];
    static unsigned slot = 0;
    char       *b;
    const Proc *p;

    if(row < 0 || row >= t->count) return "";
    p = &t->procs[row];
    b = ring[slot];
    slot = (slot + 1u) & 7u;

    switch(col){
    case 0:  snprintf(b, sizeof ring[0], "%d",   p->pid);  break;
    case 1:  snprintf(b, sizeof ring[0], "%.1f", p->cpu);  break;
    case 2:  snprintf(b, sizeof ring[0], "%.1f", p->mem);  break;
    case 3:  snprintf(b, sizeof ring[0], "%s",   p->comm); break;
    default: b[0] = '\0'; break;
    }
    return b;
}

/* ---- Sort-key handling ------------------------------------------------ */

/* Apply a sort-column request (from a 1/2/3/4 keypress). Re-selecting the
 * active column toggles asc/desc; switching column resets to a sensible
 * default (descending for the numeric columns, ascending for PID/COMMAND). */
static void apply_sort_key(int key){
    if(key == g_sort_key){
        g_sort_desc = !g_sort_desc;
    } else {
        g_sort_key  = key;
        g_sort_desc = (key == SORT_CPU || key == SORT_MEM) ? 1 : 0;
    }
}

/* A plain digit arrives as typed text (a TEXT event), not a TimuiKey code;
 * timui_char_pressed reads it without touching library internals. Returns the
 * chosen SORT_* column ('1'->PID .. '4'->CMD), or -1 if no digit was pressed. */
static int poll_sort_key(const TimuiFrame *f){
    if(timui_char_pressed(f, '1')) return 0;
    if(timui_char_pressed(f, '2')) return 1;
    if(timui_char_pressed(f, '3')) return 2;
    if(timui_char_pressed(f, '4')) return 3;
    return -1;
}

int main(void){
    TimuiConfig     cfg    = {0};
    Timui          *ui     = NULL;
    ProcTable       table  = {0};
    TimuiTableState tstate = {0, 0};
    uint64_t        last_refresh = 0;
    int             primed = 0;          /* first `ps` refresh not yet done */

    /* Automatic aggregate init from compound literals (TIMUI_STR_LIT) — valid
     * C99 and pedantic-clean; avoids the static-initializer constant-expression
     * pitfall. Column order matches the SORT_* enum and the 1/2/3/4 keys. */
    const TimuiStr headers[4] = {
        TIMUI_STR_LIT("PID"),  TIMUI_STR_LIT("CPU%"),
        TIMUI_STR_LIT("MEM%"), TIMUI_STR_LIT("COMMAND")
    };

    cfg.title     = "timui top";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_MODERN_DARK;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect   root, title, bar;
        uint64_t    now;
        int         key;
        char        titlebuf[96];

        if(!timui_begin(ui, &f)) break;

        /* Quit on ESC or F10. */
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
            timui_quit(ui);

        /* Sort-column selection (1/2/3/4); re-sort at once so the reordering is
         * visible before the next ~1 s data refresh. */
        key = poll_sort_key(f);
        if(key >= 0){ apply_sort_key(key); sort_table(&table); }

        /* Throttled data refresh: run `ps` at most once per second. The loop
         * still ticks at ~60 fps, but the grid changes only ~1×/s, so the diff
         * renderer emits almost nothing on the frames in between. */
        now = timui_now_ms();
        if(!primed || now - last_refresh >= 1000){
            refresh_table(&table);
            sort_table(&table);
            last_refresh = now;
            primed       = 1;
        }

        /* Layout: title bar on top, function bar on the bottom, table between. */
        root  = timui_root(f);
        title = timui_cut_top(&root, 1);
        bar   = timui_cut_bottom(&root, 1);

        if(table.available)
            snprintf(titlebuf, sizeof titlebuf,
                     " timui top — %d processes  (1s refresh) ", table.count);
        else
            snprintf(titlebuf, sizeof titlebuf, " timui top — ps unavailable ");
        timui_function_bar(f, title, timui_str_from_cstr(titlebuf));

        timui_table_mut(f, TIMUI_ID("procs"), root, headers, 4, table.count,
                        proc_cell, &table, &tstate);

        timui_function_bar(f, bar,
            TIMUI_STR_LIT(" 1 PID  2 CPU  3 MEM  4 CMD (sort)   F10 Quit "));

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
