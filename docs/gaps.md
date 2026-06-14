# Known gaps and limitations — timui.h

Status as of the 2026-07-03 whole-codebase review. The original G1–G17 items
(below) were filed after the first review round; the 2026-07-03 deep review
(found in [`reports/2026-07-03-deep-review.md`](reports/2026-07-03-deep-review.md))
filed V1–V28. Most of both sets are now fixed; this file tracks what remains.

## Resolved (fixed in tree)

G1 msgq compaction · G2 text-area scroll · G3 title injection (V9 finishes it:
strip C0/DEL/C1, not just BEL/ESC) · G4 clipboard single-write · G5 kitty
chunking + m=0/m=1 + b64 size_t (V22) · G8 UTF-8 text input · G9 keymap mods ·
G11 multiplexer anchored prefix · G12 overlong/surrogate UTF-8 + V16 upper
bound (>U+10FFFF) · G14 realloc(0) · G15 cells_resize return (V10: prev-first
rollback) · G16 snapshot NULL-cells guard · G17 cmdpal cap raised to 256.

V1 mpsc overflow guard · V2 feed-boundary byte loss · V5 mod+mouse wheel ·
V6 diff-renderer wide-glyph (skip continuation, width/flags in equality) ·
V7/V8 UTF-8 text inputs (codepoint append/delete) · V9 title sanitizer ·
V10 resize partial-OOM divergence · V11 termios double-free · V12 3-fragment
paste terminator · V13 empty paste event · V14 NUL ignored · V15 snapshot
return contract · V16 UTF-8 upper bound · V18 cmdpal focus-gate · V19 cells
overflow guard · V20 arena non-pow2 align · V21 msgq null-data · V22 b64
overflow · V25 amalgamate prereqs · V26 reserved feature macros · V28 snapshot
test typo. Plus a build fix: `$(TEST_BIN)`/examples/tools now rebuild on any
`src/timui_*.c` section edit (LIB_SECTIONS), and `make test` is self-contained
after `make clean` (V3/V4).

## Still open (lower priority — documented limitations)

- **V17** `message_box` silently drops buttons that don't fit the panel width.
  Documented behavior; a real fix would wrap/shrink labels or return a sentinel.
- **V23 / T5** kitty graphics transmits each chunk as three separate transport
  writes (header, payload, ST) with no write-all loop; a transport that does
  short writes could split a chunk. Loop on write per segment.
- **V24** `tab_order` is a fixed `[64]`; the 65th focusable widget is silently
  unreachable by Tab. Document the cap or grow dynamically.
- **V27** colour model: `fg==0`/`bg==0` means "default" (no SGR), so pure black
  (`0x000000`) is indistinguishable from default. Affects black-foreground
  themes on non-black-default terminals. A proper fix needs a sentinel or
  has-fg/has-bg flag (a model change; tracked for an ADR). The Tier-B golden
  and the libvterm round-trip both document this aliasing.
- **G6** `id_stack_push` OOM is silently dropped (void return) — can corrupt
  the widget id hierarchy. Changing the return type is an API break.
- **G7** event-queue overflow beyond 16/frame is counted internally but not
  exposed to the caller (no public getter).
- **G10** ConPTY backend is a stub (returns UNSUPPORTED); no `_WIN32` skeleton.
- **G13** theme coverage: DOS_GRAY / MODERN_* don't set MENU / BUTTON_ACTIVE /
  TEXT_DIM explicitly (inherit defaults). Cosmetic.

## Verification

`nix develop -c make test` (unit + Tier-B goldens), `make vt-test` (Tier A
libvterm round-trip — libvterm is Linux-only in nixpkgs; see
[`docs/visual-tests.md`](visual-tests.md)), `make amalgamate` + `release-check`.
141 unit + 8 libvterm round-trip green at the time of writing.
