/*
 * gen_golden.c — regenerate tests/golden/<scene>.txt snapshots (Tier B visual tests).
 *
 * Standalone tool: pulls in the whole library via TIMUI_IMPLEMENTATION, builds
 * each shared scene from tests/scenes.h, and writes its full-grid serialization
 * (timui_snapshot_grid) to tests/golden/<name>.txt. Run from the repo root via
 * `make goldens`; re-run whenever rendering intentionally changes, then review
 * the diff in the commit. The golden-backed unit test rebuilds the same scenes
 * and compares, so `make test` fails if the output drifts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIMUI_IMPLEMENTATION
#include "../include/timui.h"
#include "../tests/scenes.h"

#include <stdio.h>

typedef void (*scene_fn)(TimuiCellBuffer *, const TimuiAllocator *);

static const struct { const char *name; scene_fn build; } kScenes[] = {
    { "panel",   scene_panel   },
    { "rainbow", scene_rainbow },
    { "attrs",   scene_attrs   },
    { "wide",    scene_wide    },
};

int main(void){
    TimuiAllocator al = timui_default_allocator();
    size_t i;
    for(i = 0; i < sizeof(kScenes) / sizeof(kScenes[0]); i++){
        TimuiCellBuffer b;
        char path[128], out[8192];
        size_t n;
        FILE *f;
        kScenes[i].build(&b, &al);
        n = timui_snapshot_grid(&b, out, sizeof out);
        snprintf(path, sizeof path, "tests/golden/%s.txt", kScenes[i].name);
        f = fopen(path, "wb");
        if(!f){
            fprintf(stderr, "gen_golden: cannot open %s (is tests/golden present? run `mkdir -p tests/golden`)\n", path);
            timui_cells_destroy(&b);
            return 1;
        }
        /* snapshot_grid returns the would-be length (snprintf-style), which can
         * exceed the buffer; write only the bytes actually present. */
        { size_t wr = n < sizeof out ? n : sizeof out - 1;
          fwrite(out, 1, wr, f); }
        fputc('\n', f);                 /* trailing newline: git/editor friendly */
        fclose(f);
        printf("wrote %s (%zu bytes)\n", path, n);
        timui_cells_destroy(&b);
    }
    return 0;
}
