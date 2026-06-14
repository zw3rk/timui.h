# Deep codebase review — 2026-07-03

type: Report
title: Full-codebase defect review (7-domain fan-out), triaged and source-grounded
date: 2026-07-03

A fresh, whole-codebase review run after the visual-testing work landed on master
(`e0b6396`). Seven parallel reviewers covered: core/memory, rendering, input
parsing, terminal/transport/graphics, widgets, API/build/tools, and tests.
Every finding below was **re-verified against the actual source** before being
kept; false positives were dropped (notably the OSC-8 "not re-closed across
frames" claim — `renderer_reset` is called only at open/resize, not per-frame,
so `last_link` persists and the link lifecycle is correct).

Findings are grouped by severity. Each cites `file:line`, the defect, a concrete
trigger, and the fix applied (commit referenced inline).

---

## CRITICAL

### V1. `timui_mpsc_post` has no integer-overflow guard (heap overflow)
**Where:** `src/timui_core.c:469` (`n = alloc(sizeof(*n) + size)`).
**Defect:** The cross-thread public post path allocates `sizeof(node)+size`
with no overflow check, unlike its single-threaded sibling `timui_msgq_emit`
which guards `size > SIZE_MAX - hdr` (line 387). A wrapped `size` → tiny alloc →
`memcpy(n->data, data, size)` writes ~SIZE_MAX bytes.
**Trigger:** a worker calls `timui_post(ui, t, ptr, (size_t)-8)`.
**Fix:** `if(size > SIZE_MAX - sizeof(*n)) return 0;` before the alloc.
**Test:** `test_mpsc_overflow_guard` posts a wrapped size, asserts 0 + no crash.

### V2. Bytes lost at a feed boundary in ESC / UTF-8 resync
**Where:** `src/timui_input.c:257` and `:318` — both `if(i > 0) i--;`.
**Defect:** On an invalid ESC-second-byte (or invalid UTF-8 continuation) the
parser emits an event, resets to ground, and reprocesses the byte via `i--`…
but the `i > 0` guard skips the reprocess exactly when `i==0` (ESC/lead was the
last byte of the previous feed, resync byte is `b[0]` of this feed) → that byte
is silently dropped.
**Trigger:** `feed("\x1b")` then `feed("\xc3…")` drops the `\xc3`.
**Fix:** reprocess unconditionally — `i--` (size_t wrap to SIZE_MAX, loop's `i++`
lands on 0). Verified safe (no indexing between the `i--` and the loop increment).
**Test:** `test_input_esc_resync_no_loss`, `test_input_utf8_resync_no_loss`.

### V3. `test_pty_hello_exits_on_esc` is vacuous
**Where:** `tests/test_kitty_pty.c` ends in `TIMUI_CHECK(1)`.
**Defect:** The Esc-quit assertion was defanged to "informational"; the test
passes even if hello never exits. It masks any regression in the Esc-quit path.
**Fix:** assert the exit (`WIFEXITED`) when the child exits in time; otherwise
print a documented SKIP (sandbox) instead of asserting true. Record `ok`.

### V4. `make test` does not depend on `build/hello`
**Where:** `Makefile` `test` target; `tests/test_kitty_pty.c` `execl("build/hello")`.
**Defect:** after `make clean && make test`, `build/hello` is missing → the pty
test forks a non-existent binary → spurious `TIMUI_CHECK(0)`. Observed directly.
**Fix:** the pty test skips cleanly when `access("build/hello", X_OK)` fails
(matches the existing no-pty skip pattern), so `make test` is self-contained.

---

## HIGH

### V5. Modifier+mouse wheel loses its direction
**Where:** `src/timui_input.c:49-54` — `wheel_y = (code==64)?1:(code==65?-1:0)`.
**Defect:** exact-value match. Shift+wheel-up → code 68, Ctrl+wheel → 80… none
equal 64/65, so `wheel_y=0` and the scroll delta is silently lost.
**Fix:** detect by button bits — `((code&0x03)==0)?+1:(((code&0x03)==1)?-1:0)`.
**Test:** `test_mouse_wheel_with_mods`.

### V6. Diff renderer corrupts wide glyphs (continuation cells) + stale halves
**Where:** `src/timui_render.c:324-336`.
**Defect:** (a) the equality predicate ignores `width`/`flags`, so a wide→narrow
change leaves the old 2nd column stale; (b) line 336 emits a literal space for
any codepoint-0 cell including a `CONTINUATION` cell, so when a continuation
cell differs from prev it clobbers the right half of a just-drawn wide glyph.
**Fix:** skip `TIMUI_CELL_CONTINUATION` cells in the diff loop (owned by their
lead); add `pc->width==cc->width && pc->flags==cc->flags` to the equality so
wide↔narrow transitions re-emit (the now-empty cell is drawn as a space,
clearing the stale half).
**Test:** `test_render_diff_wide_to_narrow`, `test_render_diff_narrow_to_wide`.

### V7/V8. Text inputs corrupt multibyte UTF-8 (byte-wise append + 1-byte backspace)
**Where:** `src/timui_widgets.c:181-184` (`input_line_buf`),
`src/timui_textarea.c:15-19` (`text_area`).
**Defect:** `text_in` is UTF-8 (since the G8 fix), but both widgets append and
delete one byte at a time → a codepoint split at the cap boundary, or a
backspace of a multibyte char, leaves malformed UTF-8 in the buffer (renders as
replacement glyphs forever). No overrun (cap math is correct).
**Fix:** append/delete whole codepoints (decode the leading-byte length, skip a
codepoint that won't fit; walk back over a full sequence on backspace).
**Test:** `test_input_line_utf8_*`, `test_text_area_utf8_*`.

### V9. Title sanitizer misses the C1 String Terminator (OSC injection)
**Where:** `src/timui_term.c:82-83` — strips only `0x07`/`0x1b`.
**Defect:** a bare `0x9C` (C1 ST) closes the OSC just like `ESC \`, so bytes
after it escape into the terminal stream. G3 was only partially closed.
**Fix:** reject/replace all bytes `< 0x20` and the C1 range `0x80-0x9F`.
**Test:** `test_title_rejects_c1_st`.

---

## MEDIUM

### V10. `ui_resize` partial-OOM diverges curr/prev/dimensions
**Where:** `src/timui_core.c:229-232`. curr resized first; if prev's resize
fails the function returns with curr at the new size but `ui->w/h` and prev at
the old. **Fix:** resize prev first; on curr failure roll prev back; commit
`ui->w/h` only when both succeed. (G15 sharpened.) **Test:** counting allocator
that fails the 2nd resize → dims stay consistent.

### V11. `termios` tcsetattr-failure path double-frees
**Where:** `src/timui_term.c:123` — `free(orig)` without `t->saved=NULL`;
`destroy` keys off `t->saved`. **Fix:** null `t->saved` in the error branch.
**Test:** counting/failing allocator path or a tcsetattr-fail injection.

### V12. Paste terminator split across 3+ feeds injects terminator bytes
**Where:** `src/timui_input.c:171-178`. The cross-feed handler's `else` flushes
a *still-matching prefix* as paste content instead of re-deferring. **Fix:** add
a `matched && len < need` branch that appends the new bytes to `paste_tail`.
**Test:** `test_paste_cross_feed_three_fragments`.

### V13. Empty paste event for back-to-back START/END
**Where:** `src/timui_input.c:188`. **Fix:** guard `&b[i] > paste_ptr` before
the END-emit. **Test:** `test_paste_empty_no_event`.

### V14. Control bytes 0x00 / 0x1c-0x1f → wrong codepoints
**Where:** `src/timui_input.c:222-226`. NUL emits codepoint 0; 28-31 emitted as
printable. **Fix:** skip `c==0`; map the rest to a distinct handling. **Test:**
`test_input_nul_ignored`.

### V15. `timui_snapshot_grid` clamps its return (violates snprintf contract)
**Where:** `src/timui_snapshot.c:117`. Returns `cap-1` on overflow instead of
the would-be length. **Fix:** return `s.len` unclamped; NUL-terminate guarded.
**Test:** `test_snapshot_grid_returns_would_be_length`.

### V16. UTF-8 decode accepts codepoints > U+10FFFF
**Where:** `src/timui_render.c` utf8 decode (G12 residual). **Fix:** reject
`cp > 0x10FFFF` → U+FFFD. **Test:** `test_utf8_decode_above_max`.

### V17. `message_box` silently drops buttons that don't fit
**Where:** `src/timui_widgets.c:248-276`. **Fix:** document/clamp or return a
distinct sentinel. (Lower priority — UX.)

### V18. Command palette nav not focus-gated
**Where:** `src/timui_cmdpal.c:52-53`. **Fix:** gate UP/DOWN/ENTER on the input
being focused.

---

## LOW / hardening

- **V19** `cells_init/resize`: guard `w*h` and `*sizeof(TimuiCell)` overflow
  (`src/timui_render.c`).
- **V20** `arena_alloc`: reject non-power-of-two alignment (`src/timui_core.c`).
- **V21** `msgq_emit`: reject `size>0 && data==NULL` (`src/timui_core.c`).
- **V22** `b64_encode`: return `size_t` / guard int overflow (clipboard + kitty, G5).
- **V23** kitty chunking: loop the transport write per segment (G5 partial).
- **V24** `tab_order[64]`: document the cap.
- **V25** Makefile `amalgamate`: prereqs cosmetic (phony) — use wildcard or drop.
- **V26** `TIMUI_NO_STDIO`/`NO_IMAGES`/`NO_UTF8_TABLES`: documented but gate
  nothing — mark "reserved".
- **V27** `R4` black==default aliasing: a *model* limitation (fg=0 means default,
  so pure-black fg is unrepresentable). Documented in `snapshot.c`; affects
  black-fg themes. Proper fix needs a sentinel/has-flag — deferred to an ADR.
- **V28** `test_snapshot_row_eq`: negative assertion has wrong length (typo).

## Dropped (verified false-positives)
- OSC-8 "not re-closed across frames" — `last_link` persists (no per-frame reset).
- Focused table/tree "double-consume" keys — focus exclusivity saves it.
- `def_realloc(p,0)`, msgq compaction, UTF-8 text_in, kitty m-flag placement,
  MPSC destroy/drain — all verified clean / already fixed.

## Test-coverage additions (from the tests review)
- keymap: overflow boundary (`[32]`) + `keymap_hit` matching (S6).
- clip: nested push/pop + underflow safety (S7).
- `render_diff`: dimension-mismatch (resize-grow) behavior (S4).
- kitty graphics: >4096-byte chunking (two frames, `m=1` then `m=0`) +
  `image_from_png` negative inputs (S3).
