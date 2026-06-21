/*
 * file_manager.c — a read-only, Midnight-Commander-style two-pane browser.
 *
 * A functional/controlled demo: main() owns the whole model (two directory
 * panes + a modal file viewer); the immediate-mode widgets are pure renderers
 * of that model, and the model is mutated only here in the frame loop. Nothing
 * on disk is ever modified — this browses and views files, no copy/delete/mkdir
 * (deliberately, for safety).
 *
 * Widgets exercised: menu bar (File / View / Help), a 0.5 column split, two
 * bordered panels, a scrollable table per pane (Name / Size / Type), a function
 * bar, and a text-area overlay used as the file/help viewer. Both mouse (menu
 * bar) and keyboard drive the UI.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* ---- Model ------------------------------------------------------------- *
 * A pane is a directory listing plus its cursor/scroll. entries[] is a flat,
 * capped snapshot of a single directory; it is rebuilt on every navigation. */
#define MAX_ENTRIES 4096
#define VIEW_CAP    (32 * 1024)   /* bounded read: huge/binary files stay safe */

typedef struct {
    char name[256];
    long size;
    int  is_dir;
} Entry;

typedef struct {
    char  path[1024];
    Entry entries[MAX_ENTRIES];
    int   count;
    int   selected;
    int   scroll;
} Pane;

/* ---- Path helpers ------------------------------------------------------ */

/* Join base + name, collapsing the root "/" case so we never emit "//name". */
static void path_join(char *out, size_t cap, const char *base, const char *name){
    if(strcmp(base, "/") == 0) snprintf(out, cap, "/%s", name);
    else                       snprintf(out, cap, "%s/%s", base, name);
}

/* Order for the listing: ".." first, then directories (alpha), then files
 * (alpha). A stable, predictable layout is the whole point of a file browser. */
static int cmp_entry(const void *a, const void *b){
    const Entry *ea = (const Entry *)a, *eb = (const Entry *)b;
    int add_a = (strcmp(ea->name, "..") == 0);
    int add_b = (strcmp(eb->name, "..") == 0);
    if(add_a != add_b) return add_a ? -1 : 1;          /* ".." pinned to top   */
    if(ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1; /* dirs before files */
    return strcmp(ea->name, eb->name);
}

/* Rescan p->path into p->entries. Unreadable directories yield an (almost)
 * empty pane rather than an error — the loop must never crash on a bad dir. */
static void pane_scan(Pane *p){
    DIR *d;
    struct dirent *de;
    p->count = 0;
    p->selected = 0;
    p->scroll = 0;
    /* Every directory but the filesystem root gets a ".." to walk upward. */
    if(strcmp(p->path, "/") != 0){
        Entry *e = &p->entries[p->count++];
        snprintf(e->name, sizeof e->name, "..");
        e->is_dir = 1;
        e->size = 0;
    }
    d = opendir(p->path);
    if(d){
        while((de = readdir(d)) != NULL){
            Entry *e;
            struct stat st;
            char full[2048];
            if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if(p->count >= MAX_ENTRIES) break;          /* cap the snapshot     */
            e = &p->entries[p->count];
            path_join(full, sizeof full, p->path, de->d_name);
            /* stat (not d_type) keeps us on portable POSIX and resolves the
             * real kind even through symlinks; failure degrades to a file. */
            if(stat(full, &st) == 0){
                e->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
                e->size = (long)st.st_size;
            }else{
                e->is_dir = 0;
                e->size = 0;
            }
            snprintf(e->name, sizeof e->name, "%s", de->d_name);
            p->count++;
        }
        closedir(d);
    }
    if(p->count > 1)
        qsort(p->entries, (size_t)p->count, sizeof p->entries[0], cmp_entry);
}

/* Enter the selected row when it is a directory: resolve ".." by trimming the
 * last path component, else join the child name, then rescan in place. */
static void pane_enter(Pane *p){
    Entry *e;
    char np[sizeof p->path];
    if(p->count == 0) return;
    e = &p->entries[p->selected];
    if(!e->is_dir) return;                              /* files open via F3    */
    if(strcmp(e->name, "..") == 0){
        char *slash;
        snprintf(np, sizeof np, "%s", p->path);
        slash = strrchr(np, '/');
        if(slash == np) np[1] = '\0';                   /* parent is the root   */
        else if(slash)  *slash = '\0';
    }else{
        path_join(np, sizeof np, p->path, e->name);
    }
    snprintf(p->path, sizeof p->path, "%s", np);
    pane_scan(p);
}

/* ---- Table cell provider ---------------------------------------------- *
 * Called back synchronously by timui_table for every visible cell; ud is the
 * pane being rendered. The Size scratch is reused per call — safe because the
 * table copies each returned string into the cell buffer before asking again. */
static const char *cell_fn(void *ud, int row, int col){
    Pane *p = (Pane *)ud;
    static char buf[32];
    Entry *e;
    if(!p || row < 0 || row >= p->count) return "";
    e = &p->entries[row];
    switch(col){
        case 0: return e->name;
        case 1:
            if(e->is_dir) return "";
            snprintf(buf, sizeof buf, "%ld", e->size);
            return buf;
        case 2: return e->is_dir ? "<DIR>" : "file";
        default: return "";
    }
}

/* ---- Viewer ------------------------------------------------------------ */

/* Load up to VIEW_CAP bytes of the selected file, sanitizing every byte that
 * is not a printable ASCII char, '\n' or '\t' to '.' so binary content can be
 * shown safely in the text area without corrupting the terminal. */
static void load_view(Pane *p, Entry *e, char *buf, size_t cap){
    char full[2048];
    FILE *fp;
    size_t n, i;
    path_join(full, sizeof full, p->path, e->name);
    fp = fopen(full, "rb");
    if(!fp){
        snprintf(buf, cap, "(cannot open %s)", e->name);
        return;
    }
    n = fread(buf, 1, cap - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    for(i = 0; i < n; i++){
        unsigned char c = (unsigned char)buf[i];
        if(c != '\n' && c != '\t' && (c < 0x20 || c >= 0x7f)) buf[i] = '.';
    }
}

/* True if any control/navigation/function key fired this frame. Printable text
 * arrives as TEXT (not KEY) events, so this covers the practical "any key"
 * used to dismiss the viewer: ESC, Enter, Tab, arrows, F1..F12, etc. */
static int any_key_pressed(TimuiFrame *f){
    int k;
    for(k = TIMUI_KEY_ESCAPE; k <= TIMUI_KEY_F12; k++)
        if(timui_key_pressed(f, (TimuiKey)k)) return 1;
    return 0;
}

/* ---- Rendering --------------------------------------------------------- */

/* Fit a path into a title of at most `maxw` columns, keeping the tail (the
 * current directory is the interesting end) with a "..." elision prefix. */
static void fit_title(char *out, size_t cap, const char *path, int maxw){
    int len = (int)strlen(path);
    if(maxw < 4){ if(cap) out[0] = '\0'; return; }
    if(len <= maxw) snprintf(out, cap, "%s", path);
    else            snprintf(out, cap, "...%s", path + (len - (maxw - 3)));
}

/* Draw one pane: bordered panel + a Name/Size/Type table over its body. The
 * title is overdrawn so the active pane reads black-on-cyan (classic MC),
 * the inactive one dim grey-on-blue. Selection/scroll come straight from the
 * model — the table is used purely controlled; its result is discarded. */
static void draw_pane(TimuiFrame *f, Pane *p, TimuiRect r, int active,
                      TimuiId panel_id, TimuiId table_id){
    const TimuiStr headers[3] = {
        TIMUI_STR_LIT("Name"), TIMUI_STR_LIT("Size"), TIMUI_STR_LIT("Type")
    };
    const TimuiStr no_title = { NULL, 0 };
    TimuiRect body;
    TimuiStyle ts;
    char title[1040];
    int vis;

    body = timui_panel_begin(f, panel_id, r, no_title, TIMUI_BORDER_DOUBLE);

    ts = active ? timui_style_make(0x000000, 0x00AAAA, TIMUI_ATTR_BOLD)
                : timui_style_make(0xAAAAAA, 0x0000AA, 0);
    fit_title(title, sizeof title, p->path, r.w - 2);
    timui_label(f, r.x + 1, r.y, timui_str_from_cstr(title), ts);

    /* Keep the selected row visible (the table would do this internally, but
     * we own scroll so the model stays authoritative and persistent). */
    vis = body.h - 1;                                   /* minus the header row */
    if(vis < 1) vis = 1;
    if(p->selected < p->scroll)          p->scroll = p->selected;
    if(p->selected >= p->scroll + vis)   p->scroll = p->selected - vis + 1;
    if(p->scroll < 0)                    p->scroll = 0;

    {
        TimuiTableState st;
        st.selected = p->selected;
        st.scroll = p->scroll;
        (void)timui_table(f, table_id, body, headers, 3, p->count, cell_fn, p, st);
    }
    timui_panel_end(f);
}

static const char HELP_TEXT[] =
    "timui.h file manager — read-only two-pane browser\n"
    "\n"
    "  Tab .......... switch the active pane\n"
    "  Up / Down .... move the selection\n"
    "  Enter ........ open the highlighted directory (.. goes up)\n"
    "  F3 ........... view the highlighted file (read-only)\n"
    "  F1 ........... this help\n"
    "  F10 / ESC .... quit\n"
    "\n"
    "The menu bar (File / View / Help) is mouse-clickable.\n"
    "This browser never modifies anything on disk.\n"
    "\n"
    "Press any key to close.\n";

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;

    /* The model, owned by main(). Large but well within the main-thread stack. */
    static Pane left, right;                            /* static: keep the ~2MB model off the frame */
    static char viewer_buf[VIEW_CAP + 1];
    TimuiTextAreaState viewer = { viewer_buf, sizeof viewer_buf, 0, 0 };
    TimuiMenuBar menubar = {0};
    int active = 0;                                     /* 0 = left, 1 = right  */
    int viewer_open = 0;

    /* Both panes start at the current working directory. */
    if(!getcwd(left.path, sizeof left.path)) snprintf(left.path, sizeof left.path, "/");
    snprintf(right.path, sizeof right.path, "%s", left.path);
    pane_scan(&left);
    pane_scan(&right);

    cfg.title     = "timui.h file manager";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    /* Interactive-only: with no terminal on either end (piped / headless /
     * `</dev/null`) there is nothing to drive the loop and no key can ever
     * arrive to quit, so bail out instead of spinning forever. */
    if(!isatty(cfg.input_fd) || !isatty(cfg.output_fd)) return 1;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;     /* raw-mode/setup guard  */

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, menu_r, keys_r, left_r, right_r;
        Pane *ap;
        int was_open;                                   /* modal state at frame start */
        int do_quit = 0, do_view = 0, do_help = 0;

        if(!timui_begin(ui, &f)) break;
        was_open = viewer_open;
        ap = active ? &right : &left;

        root   = timui_root(f);
        menu_r = timui_cut_top(&root, 1);
        keys_r = timui_cut_bottom(&root, 1);
        timui_split_cols(root, 0.5f, &left_r, &right_r);

        /* Input — suppressed while the viewer owns the screen. */
        if(!was_open){
            if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
                timui_quit(ui);
            if(timui_key_pressed(f, TIMUI_KEY_TAB)){ active = !active; ap = active ? &right : &left; }
            if(timui_key_pressed(f, TIMUI_KEY_UP)   && ap->selected > 0)               ap->selected--;
            if(timui_key_pressed(f, TIMUI_KEY_DOWN) && ap->selected < ap->count - 1)   ap->selected++;
            if(timui_key_pressed(f, TIMUI_KEY_ENTER)) pane_enter(ap);
            if(timui_key_pressed(f, TIMUI_KEY_F1)){
                snprintf(viewer_buf, sizeof viewer_buf, "%s", HELP_TEXT);
                viewer.cursor = 0; viewer.scroll_y = 0; viewer_open = 1;
            }
            if(timui_key_pressed(f, TIMUI_KEY_F3) && ap->count > 0 &&
               !ap->entries[ap->selected].is_dir){
                load_view(ap, &ap->entries[ap->selected], viewer_buf, sizeof viewer_buf);
                viewer.cursor = 0; viewer.scroll_y = 0; viewer_open = 1;
            }
        }

        /* Two bordered panes with their tables (drawn before the menu so any
         * open menu popup overlays them). */
        draw_pane(f, &left,  left_r,  active == 0, TIMUI_ID("pane_l"), TIMUI_ID("tbl_l"));
        draw_pane(f, &right, right_r, active == 1, TIMUI_ID("pane_r"), TIMUI_ID("tbl_r"));

        timui_function_bar(f, keys_r,
            TIMUI_STR_LIT(" F1 Help  Tab Switch  Enter Open  F3 View  F10 Quit "));

        /* Menu bar last so its drop-downs paint on top of the panels. */
        timui_menu_bar_begin(f, &menubar, menu_r);
        if(timui_menu_begin(f, &menubar, TIMUI_ID("m_file"), TIMUI_STR_LIT("File"))){
            if(timui_menu_item(f, &menubar, TIMUI_ID("m_quit"), TIMUI_STR_LIT("Quit"))) do_quit = 1;
            timui_menu_end(f);
        }
        if(timui_menu_begin(f, &menubar, TIMUI_ID("m_view"), TIMUI_STR_LIT("View"))){
            if(timui_menu_item(f, &menubar, TIMUI_ID("m_v3"), TIMUI_STR_LIT("View file  F3"))) do_view = 1;
            timui_menu_end(f);
        }
        if(timui_menu_begin(f, &menubar, TIMUI_ID("m_help"), TIMUI_STR_LIT("Help"))){
            if(timui_menu_item(f, &menubar, TIMUI_ID("m_about"), TIMUI_STR_LIT("About"))) do_help = 1;
            timui_menu_end(f);
        }
        timui_menu_bar_end(f, &menubar);

        if(!was_open){
            if(do_quit) timui_quit(ui);
            if(do_help){
                snprintf(viewer_buf, sizeof viewer_buf, "%s", HELP_TEXT);
                viewer.cursor = 0; viewer.scroll_y = 0; viewer_open = 1;
            }
            if(do_view && ap->count > 0 && !ap->entries[ap->selected].is_dir){
                load_view(ap, &ap->entries[ap->selected], viewer_buf, sizeof viewer_buf);
                viewer.cursor = 0; viewer.scroll_y = 0; viewer_open = 1;
            }
        }

        /* Modal viewer overlay (topmost). Any control key dismisses it — but
         * only if it was already open at the start of this frame, so the very
         * keypress/click that opened it does not also close it. */
        if(viewer_open){
            TimuiRect ov = timui_inset(timui_root(f), 2);
            TimuiRect vbody = timui_panel_begin(f, TIMUI_ID("viewer"), ov,
                TIMUI_STR_LIT(" Viewer — press any key to close "), TIMUI_BORDER_DOUBLE);
            timui_text_area(f, TIMUI_ID("view_ta"), vbody, &viewer);
            timui_panel_end(f);
            if(was_open && any_key_pressed(f)) viewer_open = 0;
        }

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
