# Known gaps and limitations — timui.h

Status after seven review rounds. Rounds 1–3 (static) fixed G/V, W,
and X ([round 1](reports/2026-07-03-deep-review.md),
[2](reports/2026-07-03-deep-review-pass2.md),
[3](reports/2026-07-03-deep-review-pass3.md)); round 4 added **dynamic
verification** (ASAN+UBSAN clean) and an adversarial/lifecycle/API lens, filing
the Y-series ([round 4](reports/2026-07-03-deep-review-pass4.md)); rounds 5–6
were user-directed (round 5: W6/W11/W12/W14/V24; round 6: G6/G7/G13). Round 7
(2026-07-05) fanned six independent lenses and filed the **Z-series**
([round 7](reports/2026-07-05-deep-review-pass7.md)). Most of all sets are
fixed; this file tracks what remains. The Phase 1.5 planning pass now marks the
next selected library/platform gaps separately from lower-priority residuals.

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

## Selected for Phase 1.5

These are known limitations promoted into the next planned follow-up. See
[`docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt`](goals/phase1_5-platform-widgets-style-text-image.goal.txt).

- **G10/W13** Windows ConPTY is implemented behind `_WIN32`, runtime-probed for
  ConPTY entry points, and covered by the MinGW compile seam in `make check`
  plus POSIX fallback/helper tests. `make smoke-conpty-win32` is available for
  operator evidence, but do not claim Windows support until a real Windows
  Terminal smoke run is green and recorded per
  `docs/runbooks/phase1-5-live-evidence.md`.
- **Image protocol emitters** still need iTerm2 terminal evidence.
  Kitty graphics, iTerm2 inline PNG images, raw-RGBA Sixel, PNG+RGBA sidecar
  Sixel, and built-in bounded plain-PNG-to-Sixel decode are in tree. Hosted
  Windows Terminal Sixel evidence is accepted in run `28982641529` and recorded
  per `docs/runbooks/phase1-5-live-evidence.md`. Remaining image gap: real
  iTerm2 terminal capture via `nix develop -c make smoke-image-live
  PROTO=iterm2 FRAMES=N`, or deterministic iTerm2 protocol evidence if hosted
  macOS TCC keeps blocking GUI capture. Multiplexer image passthrough is
  deliberately conservative for Phase 1.5: image caps are stripped under
  tmux/screen/zellij unless a later pass designs and tests a narrower
  passthrough contract.
  ConPTY should stay a transport, not a Windows-specific graphics abstraction.

## Still open (documented limitations, not Phase 1.5)

- **LOW (round 2, documented)**: L1 double-Esc resolution · L2 flush clock for
  direct feed callers · L3 cross-feed UTF-8 split dangling event ptr · L4
  put_glyph overwrites a wide-glyph continuation · L7 table column remainder
  gap · L8 cmdpal matched_idx[256] cap · L9 poll non-EINTR errors swallowed ·
  L11 127-byte title buffer truncation · L12 kitty partial-frame silent abort ·
  L13 async_scan recv size discipline · L15 MPSC destroy-vs-post contract.
- **Unicode parity:** the Phase 1.5 grapheme foundation covers the terminal
  clusters currently exercised by editing and truncation, but full generated
  UAX #29 grapheme tables remain future work.

Round 6 (user-directed, all fixed): G6 id_stack_push returns TimuiResult (OOM
is detectable; the caller skips the paired pop, so a failing grow no longer
corrupts the id hierarchy) · G7 timui_events_dropped() exposes the per-frame
queue-overflow count (read + reset) · G13 theme slot coverage — every builtin
theme now gives the interactive-state slots (TEXT_DIM / SELECTION /
BUTTON_FOCUSED / BUTTON_ACTIVE / MENU_ACTIVE) a style visually distinct from
its resting base: MODERN_DARK/LIGHT fill the slots that used to inherit the
plain fg-on-bg default, DOS_GRAY's BUTTON_ACTIVE (which equalled BUTTON) is now
inverted, and MONO differentiates via SGR attrs (dim/bold/reverse) since it has
no colour to spend. Guarded by a structural invariant test across all five
themes plus an out-of-range/NULL slot-lookup negative test.

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

## Round 7 (Z-series, 2026-07-05) — resolved

Six-lens parallel review ([pass 7](reports/2026-07-05-deep-review-pass7.md)).
Memory surface confirmed clean; yield was input correctness + honesty + coverage.
160→174 unit tests.

**Fixed in tree:** Z1 false "Phase 0 scaffold" header banner (the header is the
docs) · Z2 input UTF-8 decoder now rejects overlong/surrogate/>U+10FFFF →
U+FFFD (was NUL/invalid injection defeating V14; the input decoder is
independent of the render one G12/V16 fixed) · Z3 ESC mid-CSI/SS3 aborts and
restarts (ECMA-48) instead of resyncing to ground and leaking the tail as text ·
Z4 CSI `:` sub-parameter (Kitty event-type/alternate-key) discarded via a new
`sub_param` field instead of resyncing · Z6 single shared `timui_utf8_encode_`
in `timui_int.h` (was a fourth inlined copy) · Z7 glyph emit unified through
`put_glyph_link` (killed the triplicated wide-glyph continuation logic) · Z8
`TIMUI_KEYIN_LEFT/RIGHT/HOME/END/DELETE` marked reserved + dead stores dropped
(no widget consumes them; inputs are append-at-end) · Z9 `draw_box`/`hline`/
`vline` clamp extreme geometry (sibling of Y2's draw_fill guard; UB + unbounded
loop, not a write overrun) · Z10 impl-only macros (`R_EMIT`, …) `#undef`'d at
each section end · Z11 `SnapBuf`/`ConptyCtx` → `TimuiSnapBuf`/`TimuiConptyCtx` ·
Z12 `timui_ui_resize` → `TimuiResult` (resize OOM now signalable, G6/G7 class) ·
Z13 orphaned `TimuiDialogResult` / unused cell flags / `Cell.image_id` annotated
reserved · Z14 rect-cut `cut_*` no longer mislabeled "pure" · **Z15–Z24**
coverage added (lifecycle-OOM rollback with a net-byte allocator, message API,
hline/vline [Z17], layout siblings, label_hyperlink, function_bar, tab-grow OOM,
hyperlink_set NULL/OOM/truncation, `timui_run` negatives, str_len/now_ms).

**Rejected:** Z5 — "release-check regenerates instead of verifying → CI drift"
was based on a false premise: `release/` is **gitignored** (a generated
artifact, never committed), so there is no tracked copy to drift and a
`git diff` guard would be a no-op. `release-check`'s regenerate-and-compile is
the correct guard.

**Also fixed (maintainer-approved follow-up, breaking changes OK pre-release):**
Z25 termios `tcsetattr`-failure branch now exercised via an inert-in-production
test seam (`timui_termios_fail_tcsetattr_for_test`), guarding the V11 double-free
invariant — there is no portable way to fail a real fd's tcsetattr while
tcgetattr succeeds · **Z26** tree/table/command_palette gained value-returning
controlled forms + `_mut` twins mirroring listbox (fixes the tree unconditional
`*selected` write-back aliasing) · **Z27** the menu bar is now controlled via a
caller-owned `TimuiMenuBar` — `open` persists and is observable, and 6 hidden
fields were removed from `Timui` · **Z28** the "fill row + draw text" and
"focused up/down nav" scaffolding is factored into `timui_draw_row_` /
`timui_updown_nav_` in `timui_int.h`.

Round-7 total: 160→176 unit tests; ASAN/UBSAN clean; goldens byte-identical;
amalgamate + release-check green.

## Historical verification record

`nix develop -c make test` (unit + Tier-B goldens), `make vt-test` (Tier A
libvterm round-trip — requires `libvterm-neovim`; see
[`docs/visual-tests.md`](visual-tests.md)), `make amalgamate` + `release-check`.
The exact unit-test counts in the review notes are historical snapshots, not
Phase 1.5 acceptance criteria; capture a fresh baseline when implementation
starts. Round-6/Round-7 verification recorded clean unit tests, Tier-B goldens,
ASAN/UBSAN, amalgamation/release-check, `TIMUI_NO_THREADS`, and libvterm
round-trips where available.
