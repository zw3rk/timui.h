/*
 * test_snapshot.c — full-grid snapshot + grid-equality unit tests (Tier B).
 *
 * Exercises timui_snapshot_grid (deterministic full-grid serialization for
 * golden-file visual testing) and timui_grid_eq (cell-by-cell comparison,
 * also reused by the libvterm round-trip harness).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"
#include "scenes.h"   /* shared scene builders — also used by tools/gen_golden.c */

#include <string.h>

/* Build a small, deterministic 4x2 scene reused across these tests:
 *   R0: "Hi" white-on-default, trailing cells empty
 *   R1: "Yo" orange(0xff8800)-on-blue(0x102030) BOLD, trailing cells empty
 * Empty cells come from cells_init's memset, so width==0 (printed as '.|-|-|.|0'). */
static void grid_scene(TimuiCellBuffer *b, const TimuiAllocator *al){
    timui_cells_init(b, 4, 2, al);
    timui_draw_text(b, 0, 0, TIMUI_STR_LIT("Hi"),
                    timui_style_make(0xFFFFFF, 0, 0));
    timui_draw_text(b, 0, 1, TIMUI_STR_LIT("Yo"),
                    timui_style_make(0xFF8800, 0x102030, TIMUI_ATTR_BOLD));
}

TIMUI_TEST(test_snapshot_grid_full){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    char out[512];
    size_t n;
    /* Exact serialization: one line per row, cells space-separated, fields
     * '|'-separated. Default colors render as '-', attrs as '.' or flags. */
    static const char expected[] =
        "R0: H|ffffff|-|.|1 i|ffffff|-|.|1 .|-|-|.|0 .|-|-|.|0\n"
        "R1: Y|ff8800|102030|b|1 o|ff8800|102030|b|1 .|-|-|.|0 .|-|-|.|0";
    grid_scene(&b, &al);
    n = timui_snapshot_grid(&b, out, sizeof out);
    TIMUI_CHECK(n == strlen(expected));
    TIMUI_CHECK(strcmp(out, expected) == 0);
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_snapshot_grid_eq_same){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer a, c;
    char diff[256];
    grid_scene(&a, &al);
    grid_scene(&c, &al);
    diff[0] = 'X';
    TIMUI_CHECK(timui_grid_eq(&a, &c, diff, sizeof diff) == 1);
    TIMUI_CHECK(diff[0] == 'X');                 /* diff left untouched on match */
    timui_cells_destroy(&a);
    timui_cells_destroy(&c);
}

TIMUI_TEST(test_snapshot_grid_eq_diff){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer a, c;
    char diff[256] = "";
    grid_scene(&a, &al);
    grid_scene(&c, &al);
    /* Mutate one foreground (cell 1,0: 'i' white -> green) and expect a diff. */
    timui_cells_get(&c, 1, 0)->fg = 0x00FF00;
    TIMUI_CHECK(timui_grid_eq(&a, &c, diff, sizeof diff) == 0);
    TIMUI_CHECK(strstr(diff, "mismatch at (1,0)") != NULL);
    TIMUI_CHECK(strstr(diff, "i|ffffff") != NULL);   /* expected side (from a) */
    TIMUI_CHECK(strstr(diff, "i|00ff00") != NULL);   /* got side (mutated green) */
    timui_cells_destroy(&a);
    timui_cells_destroy(&c);
}

TIMUI_TEST(test_snapshot_grid_eq_dim){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer a, c;
    char diff[256] = "";
    timui_cells_init(&a, 4, 2, &al);
    timui_cells_init(&c, 3, 2, &al);
    TIMUI_CHECK(timui_grid_eq(&a, &c, diff, sizeof diff) == 0);
    TIMUI_CHECK(strstr(diff, "dimension mismatch: 4x2 vs 3x2") != NULL);
    timui_cells_destroy(&a);
    timui_cells_destroy(&c);
}

/* ---- golden-file regression check --------------------------------------- *
 * Rebuild each shared scene, re-serialize it, and compare byte-for-byte
 * against the committed tests/golden/<name>.txt. A mismatch means the
 * renderer's cell output drifted; regenerate with `make goldens` and review
 * the diff. Reads files relative to the repo root (make test runs from there). */
static int golden_check(const char *name,
                        void (*build)(TimuiCellBuffer *, const TimuiAllocator *),
                        const TimuiAllocator *al){
    TimuiCellBuffer b;
    char path[128], got[8192], file[8192];
    FILE *f;
    size_t gn, fn, i, m;
    int ok;
    build(&b, al);
    gn = timui_snapshot_grid(&b, got, sizeof got);
    snprintf(path, sizeof path, "tests/golden/%s.txt", name);
    f = fopen(path, "rb");
    if(!f){
        printf("  golden MISSING: %s  (run `make goldens`)\n", path);
        timui_cells_destroy(&b);
        return 0;
    }
    fn = fread(file, 1, sizeof(file) - 1, f);
    fclose(f);
    while(fn > 0 && (file[fn - 1] == '\n' || file[fn - 1] == '\r')) fn--;  /* strip trailing newline */
    file[fn] = '\0';
    ok = (gn == fn && memcmp(got, file, gn) == 0);
    if(!ok){
        printf("  golden MISMATCH: %s  (expected %zu bytes, got %zu)\n", path, fn, gn);
        m = gn < fn ? gn : fn;
        for(i = 0; i < m; i++)
            if(got[i] != file[i]){
                printf("    first diff at byte %zu: expected 0x%02x, got 0x%02x\n",
                       i, (unsigned char)file[i], (unsigned char)got[i]);
                break;
            }
    }
    timui_cells_destroy(&b);
    return ok;
}

TIMUI_TEST(test_snapshot_goldens){
    TimuiAllocator al = timui_default_allocator();
    TIMUI_CHECK(golden_check("panel",   scene_panel,   &al));
    TIMUI_CHECK(golden_check("rainbow", scene_rainbow, &al));
    TIMUI_CHECK(golden_check("attrs",   scene_attrs,   &al));
    TIMUI_CHECK(golden_check("wide",    scene_wide,    &al));
}

