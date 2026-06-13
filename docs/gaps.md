# Known gaps and limitations — timui.h

Tracked from the 2026-07-03 deep codebase review. Each entry is grounded in
the source and has a concrete fix direction. Items are ordered by impact.

---

## MAJOR gaps (wrong/suboptimal behavior, no crash)

### G1. msgq slab never compacted after partial drain
**File:** `src/timui_core.c` (msgq_emit/msgq_recv reclaim logic)
**Problem:** The queue only resets `head`/`tail` to 0 when **fully** drained.
After partial drain, `head` advances but `tail` stays near the end →
subsequent emits fail with "full" even though the front of the slab is
unused. Degrades to one-shot-per-fill under sustained producer/consumer rate
mismatch.
**Fix:** compact (memmove) the remaining data to the front when head > 0 and
the next emit doesn't fit, or use a ring buffer.
**Test:** partial drain → emit should succeed.

### G2. Text-area widget never scrolls
**File:** `src/timui_textarea.c` (`scroll_y` hard-coded to 0)
**Problem:** `timui_scroll_begin(f, r, 0)` is called with literal 0, so the
text area renders all lines from the top with no vertical offset. Lines
beyond the viewport are iterated and decoded (O(text) per frame) but clipped
away. Typing appends at the end but the view never follows the cursor.
**Fix:** Add `int scroll_y` to `TimuiTextAreaState`; wire it to
`scroll_begin`; update it when the cursor moves past the viewport bottom.
**Test:** 20-line text in a 3-row viewport → verify scroll_y > 0.

### G3. OSC title injection (BEL/ESC in title string)
**File:** `src/timui_term.c:78-82` (timui_screen_enter)
**Problem:** The title is emitted verbatim into the OSC 0 sequence. A title
containing BEL (0x07) prematurely closes the OSC and injects bytes into the
terminal stream; an ESC byte can inject arbitrary control sequences. If the
title originates from an untrusted source (filename, remote data) this is a
terminal-injection vector.
**Fix:** Strip or escape 0x07 (BEL) and 0x1b (ESC) from the title before
emitting. Same fix needed for the clipboard path if it ever takes raw text.
**Test:** title containing BEL/ESC → no premature OSC close in fake output.

### G4. Clipboard partial-write produces malformed OSC 52
**File:** `src/timui_clipboard.c:31-33`
**Problem:** Three separate `t->write` calls emit the OSC 52 header, base64
payload, and terminator. Return values are discarded (`(void)`). If the
transport's `write` does a partial write (fd_write returns byte count,
ignored), the terminal receives a truncated sequence (no `ESC\` terminator)
that swallows subsequent output.
**Fix:** Build the full OSC 52 sequence in a single buffer and write it in
one call; or loop on write until all bytes are sent.
**Test:** transport that does 1-byte-per-write → output still complete.

### G5. Kitty graphics: no payload chunking
**File:** `src/timui_kitty.c:36-49`
**Problem:** The entire base64 payload is emitted in a single `write`. The
Kitty graphics protocol requires chunking at ~4 KiB boundaries with the
`m=1` continuation flag. Terminals (including kitty) may silently truncate or
reject oversized single chunks. Any non-trivial PNG fails to display.
Also: `b64len` is `int`, so images >2 GiB overflow it. Missing `i=` image
ID → re-transmit per frame with no dedup.
**Fix:** Chunk the base64 into ≤4 KiB blocks with `m=1` continuation;
add `i=<id>` for caching; loop the transport write.
**Test:** 100 KiB PNG → fake output contains multiple `\x1b_G` chunks with
`m=1` on all but the last.

### G6. id_stack_push OOM silently corrupts widget id hierarchy
**File:** `src/timui_core.c:273-281`
**Problem:** When the geometric grow realloc fails, the push is silently
dropped (return type is `void`). A widget that pushed an id, drew children,
then popped, will pop the **wrong** (stale) seed, corrupting the id
hierarchy for all subsequent siblings. No error reporting to the caller.
**Fix:** Change return type to `TimuiResult` (breaking API change) or add an
`int*` out-parameter for status. At minimum, document the limitation.
**Test:** counting allocator that fails on the 2nd alloc → verify pop
doesn't corrupt the stack.

### G7. Event queue silently drops events beyond 16 per frame
**File:** `src/timui_int.h` (`events[16]`), `src/timui_core.c:21-25`
**Problem:** More than 16 events in one frame (e.g. a large paste burst, a
mouse drag with high polling rate) silently discards events past 16. No
counter, no flag, no overflow indicator.
**Fix:** Add an `events_dropped` counter; or grow the ring dynamically; or
coalesce mouse-motion events.
**Test:** inject 20 events → verify `events_dropped == 4`.

### G8. text_in drops non-ASCII codepoints (UTF-8 input unsupported)
**File:** `src/timui_core.c:171-174`
**Problem:** The text accumulator (`ui->text_in`) only stores codepoints
< 0x80. Any non-ASCII character (é, 中, emoji) is silently ignored. This
means `timui_input_line_buf` and `timui_text_area` cannot accept
international text input.
**Fix:** UTF-8-encode the codepoint into `text_in` (using the existing
`utf8_encode` helper) instead of storing a raw byte. Ensure the buffer has
room for up to 4 bytes per codepoint.
**Test:** feed a 2-byte UTF-8 sequence → text_in receives the encoded bytes.

---

## MINOR gaps (robustness, cleanup, edge cases)

### G9. `keymap_hit` ignores stored modifiers
**File:** `src/timui_keymap.c:12-21`
**Problem:** `timui_keymap_hit` only checks `timui_key_pressed(f, key)` — it
never compares the stored `mods` field. Two bindings differing only by
modifiers (e.g. Ctrl+C vs plain C) are indistinguishable; the first match
wins.
**Fix:** Add a `timui_key_pressed_mods(f, key, mods)` API that also checks
the last event's modifiers, then compare in `keymap_hit`.

### G10. ConPTY stub returns UNSUPPORTED on all platforms
**File:** `src/timui_conpty.c`
**Problem:** No `#ifdef _WIN32` skeleton; a Windows build silently gets a
no-op backend. The comment promises Windows support "when available" with no
hook point.
**Fix:** Add a `#ifdef _WIN32` branch that calls `CreatePseudoConsole` and
wraps the handles in a `TimuiTransport`.

### G11. Multiplexer detection uses unanchored `strstr`
**File:** `src/timui_term.c:185`
**Problem:** `strstr(term, "screen")` matches hypothetical `TERM=screen-reader-tui`,
falsely reducing capabilities. Low risk today but brittle.
**Fix:** Anchor on known prefixes (`strncmp(term, "screen", 6)`,
`strncmp(term, "tmux", 4)`, etc.).

### G12. UTF-8 decoder accepts overlong encodings and surrogate halves
**File:** `src/timui_render.c:62-79`
**Problem:** No check that a 2-byte sequence decodes to ≥ U+0080, etc.
Overlong `\xC0\x80` decodes to U+0000; `\xED\xA0\x80` decodes to a
surrogate. Garbage-in produces wrong glyphs (no crash).
**Fix:** Add overlong/surrogate range checks in `timui_utf8_decode`.

### G13. Theme coverage gaps
**File:** `src/timui_render.c` (theme_builtin)
**Problem:** `DOS_GRAY`, `MODERN_DARK`, `MODERN_LIGHT` never set
`TIMUI_SLOT_MENU` / `TIMUI_SLOT_MENU_ACTIVE` / `TIMUI_SLOT_BUTTON_ACTIVE` /
`TIMUI_SLOT_TEXT_DIM` explicitly — they inherit the loop default. Menus and
status bars render with body-text colors. Cosmetic.
**Fix:** Set all 18 slots explicitly in each theme branch.

### G14. `def_realloc(p, 0)` frees the pointer on glibc
**File:** `src/timui_core.c:478`
**Problem:** `realloc(p, 0)` is implementation-defined — on glibc it frees
`p` and returns NULL. Any future 0-size realloc would silently free the
buffer while the caller keeps using the old pointer.
**Fix:** Guard: `if(ns == 0) ns = 1;` in `def_realloc`.

### G15. `cells_resize` ignores the return value
**File:** `src/timui_core.c:207` (`timui_ui_resize`)
**Problem:** `timui_cells_resize` return value is ignored. If resize fails
(OOM), the buffers may be in a half-resized state while `ui->w/h` are
updated, leaving dimensions inconsistent with the cell array.
**Fix:** Check the return; if either resize fails, revert `ui->w/h`.

### G16. Snapshot render crashes on NULL cells
**File:** `src/timui_snapshot.c:6-9`
**Problem:** Guard checks `buf->h` but not `buf->cells != NULL`. If a
buffer failed init (OOM, then h was set to 0 — caught by the h guard), the
dereference is safe today, but latent if any future code path sets h > 0
with cells == NULL.
**Fix:** Add `!buf->cells` to the guard.

### G17. Command palette fixed-size `matched_idx[128]`
**File:** `src/timui_cmdpal.c:26`
**Problem:** If more than 128 commands match the filter, matches beyond 128
are invisible and unreachable. `state->selected` caps at 127.
**Fix:** Use dynamic allocation, or document the limit, or binary-search +
render without a separate index array.

---

## Priority for fixing

| Priority | Items |
|---|---|
| **Do next** | G2 (text-area scroll), G3 (title injection), G8 (UTF-8 input) |
| **Do soon** | G1 (msgq compaction), G4 (clipboard retry), G5 (kitty chunking), G6 (id_stack OOM) |
| **Do eventually** | G7 (event queue), G9 (keymap mods), G14-G17 (robustness) |
| **Platform** | G10 (ConPTY Windows) |
| **Cosmetic** | G11 (strstr anchor), G12 (overlong UTF-8), G13 (theme slots) |
