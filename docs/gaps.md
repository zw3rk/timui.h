# Known gaps and limitations — timui.h

Status after four review rounds (2026-07-03). Rounds 1–3 (static) fixed G/V, W,
and X ([round 1](reports/2026-07-03-deep-review.md),
[2](reports/2026-07-03-deep-review-pass2.md),
[3](reports/2026-07-03-deep-review-pass3.md)); round 4 added **dynamic
verification** (ASAN+UBSAN clean) and an adversarial/lifecycle/API lens, filing
the Y-series ([round 4](reports/2026-07-03-deep-review-pass4.md)); round 5 was
user-directed (W6/W11/W12/W14/V24). Most of all five sets are fixed; this file
tracks what remains.

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

Round 2 (W-series, all fixed): W1 phantom timui_wakeup docs removed · W2
async_scan quits on completion · W3 message_box modal trap (decouple + clamp
buttons — also closes V17) · W4 title sanitizer UTF-8 regression (codepoint-
level) · W5 runner 4096-byte buffer + documented limit · W7 non-tty hot-spin
(non-blocking fd + idle sleep) · W8 gen_golden fwrite clamp · W9 hyperlink
URI-change re-emit (cache the URI, not the per-frame id) · W10 panel content
clip · L5 cursor bounds · L6 snapshot size-query · L10 non-vacuous fuzz · L14
reserved-macro comment · V23 kitty write-all loop.

## Still open (lower priority — documented limitations)

- **W13** ConPTY `(DWORD)n` truncation for >4 GiB writes (latent behind the
  UNSUPPORTED stub).
- **V27** colour model: `fg==0`/`bg==0` means "default" (no SGR), so pure black
  (`0x000000`) is indistinguishable from default. Affects black-foreground
  themes on non-black-default terminals. A proper fix needs a sentinel or
  has-fg/has-bg flag (a model change; tracked for an ADR).
- **G6** `id_stack_push` OOM is silently dropped (void return) — can corrupt
  the widget id hierarchy. Changing the return type is an API break.
- **G7** event-queue overflow beyond 16/frame is counted internally but not
  exposed to the caller (no public getter).
- **G10** ConPTY backend is a stub (returns UNSUPPORTED); no `_WIN32` skeleton.
- **G13** theme coverage: DOS_GRAY / MODERN_* don't set MENU / BUTTON_ACTIVE /
  TEXT_DIM explicitly (inherit defaults). Cosmetic.
- **LOW (round 2, documented)**: L1 double-Esc resolution · L2 flush clock for
  direct feed callers · L3 cross-feed UTF-8 split dangling event ptr · L4
  put_glyph overwrites a wide-glyph continuation · L7 table column remainder
  gap · L8 cmdpal matched_idx[256] cap · L9 poll non-EINTR errors swallowed ·
  L11 127-byte title buffer truncation · L12 kitty partial-frame silent abort ·
  L13 async_scan recv size discipline · L15 MPSC destroy-vs-post contract.

Round 5 (user-directed, all fixed): W6 SIGTERM/SIGHUP/SIGQUIT terminal-
restoration handler (single static Timui* carve-out; timui_restore_terminal is
public) · W11 documented the TimuiAllocator.realloc preserves-on-failure
contract · W12 caps passthrough probe (keep Kitty caps under a mux when the
outer terminal is kitty-family) · W14 documented the join-before-close shutdown
ordering · V24 dynamic tab_order (dropped the fixed [64] cap; interact_init
now takes the allocator, interact_destroy frees).

Round 3 (X-series, all fixed): X1 tree depth overflow · X2 render_diff wide-
glyph cursor-run width · X3 message_box modal trap (clear modal_active when
no button renders) · X4 text_area cap==0 guard · X5 listbox scroll tail clamp
· X6 message_box boxw floor at 2 · X7 id_stack/fake-transport grow overflow
guards. Documented (PLAUSIBLE/theoretical): hyperlink_set int-nc overflow
(unreachable — links bounded by cell count) · clip_stack push-dropped/pop-
stale at >8 nesting (theoretical — reset per frame, real nesting shallow) ·
non-tty O_NONBLOCK skipped if fcntl(F_GETFL) fails (rare).

Round 4 (Y-series, all fixed; found via adversarial review + ASAN/UBSAN which
are clean): Y1 text_area cursor>=cap OOB NUL write (clamped) · Y2 draw_fill
huge-rect signed-overflow UB + CPU burn (rect clamped to buffer) · Y3 listbox
selected clamp (mirrors tree/table) · Y4 keymap_hit multi-binding reachability
· Y7 cmdpal matcher respects TimuiStr.len · Y5/Y6 docs (end single-use,
realloc required).

## Verification

`nix develop -c make test` (unit + Tier-B goldens), `make vt-test` (Tier A
libvterm round-trip — libvterm is Linux-only in nixpkgs; see
[`docs/visual-tests.md`](visual-tests.md)), `make amalgamate` + `release-check`.
148→154 unit + 8 libvterm round-trip green after round 5; UBSAN clean; ASAN
clean (system clang); goldens clean; amalgamate + release-check pass;
TIMUI_NO_THREADS compiles.
