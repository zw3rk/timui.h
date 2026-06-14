# Deep codebase review — pass 2 (2026-07-03)

type: Report
title: Second independent review pass — prior-fix audit + new findings (W-series)
date: 2026-07-03

A second pass over the same 7 domains, framed to (a) verify the pass-1 V1–V28
fixes and (b) find NEW defects. Prior-fix audit: **all of V1–V28 verified
correct at their sites**, with two caveats — V26's comment mislabels the
reserved macros as "defined" (W14), and V10's rollback silently assumes the
standard realloc-preserves-on-failure contract (W11). New findings are the
**W-series** below.

## CRITICAL — regression introduced by pass 1

### W4. V9 title sanitizer corrupts multibyte UTF-8 (my regression)
**Where:** `src/timui_term.c:87` — accept rule `(ch>=0x20 && ch<0x7f) || ch>=0xa0`.
**Defect:** rejecting the whole 0x80–0x9F C1 band strips **valid UTF-8
continuation bytes** (continuations occupy 0x80–0xBF). `Ü` = C3 9C loses 0x9C;
`字` = E5 AD 97 loses 0xAD. The pass-1 V9 fix closed the 0x9C OSC-injection
vector but at the cost of corrupting any non-ASCII title.
**Fix:** sanitize at the **codepoint** level — decode UTF-8, drop C0/DEL/C1
control *codepoints* (incl. U+009C, whose UTF-8 is C2 9C), re-encode printable
codepoints. This keeps `Ü`/`字` and drops only the control codepoint.

## HIGH

### W1. `timui_wakeup` is documented but does not exist
**Where:** THREADING.md, API.md, `include/timui.h:41` banner — zero
declarations/definitions in the source. A documented API that fails to compile.
**Fix:** the honest minimal fix — remove the wakeup references from the docs
and header (mark as future); a real wakeup needs a self-pipe/eventfd to
interrupt `poll()`.

### W2. `async_scan` never quits on completion
**Where:** `examples/async_scan.c:52` — `MSG_DONE` sets `done=1` but nothing
calls `timui_quit`; the app hangs until Esc. The example exists to demonstrate
the post→drain lifecycle but never terminates it.
**Fix:** quit when done.

### W3. `message_box` can permanently trap all input (V17 residue)
**Where:** `src/timui_widgets.c:299-310` — `modal_active=1` unconditionally;
cleared only inside the button loop, which `break`s when a button overflows the
clamped box. A narrow parent → no button renders → `modal_active` pinned → every
widget inert forever.
**Fix:** decouple the modal lifecycle from button rendering; guarantee a dismiss
path; cap the min-boxw bump at `parent.w-2`.

### W5. `timui_run()` silently truncates messages >256 bytes
**Where:** `src/timui_app.c:21-28` — drains into a fixed `buf[256]`, clamps with
no truncation signal; `timui_post` advertises arbitrary sizes.
**Fix:** document a 256-byte post limit for the runner, or grow to the returned
`*inout_size`.

## MEDIUM

- **W6** No SIGTERM/SIGHUP handling → `kill`/window-close leaves the terminal raw
  (violates the PRD "atexit restoration hook"). Needs a signal handler (documented
  single static `Timui*`).
- **W7** Non-tty / piped stdin hot-spins: the 16 ms `poll()` is gated on
  `termios_active`, so headless/piped input burns CPU. Gate on `have_transport`.
- **W8** `gen_golden.c` `fwrite(out, 1, n, …)` over-reads when the snapshot
  exceeds the 8192-byte buffer (latent — V15's unclamped return feeds a too-large
  `n`). Same in the golden-check `memcmp`. Clamp to `min(n, sizeof out)`.
- **W9** `render_diff` misses a hyperlink whose **URI** changes (same id, same
  glyph): ids are per-frame indices, so id-equality ≠ URI-equality → stale URL.
  Compare resolved URI strings.
- **W10** Panel/message title text isn't clipped → long strings bleed past the
  border. Push a body clip in `panel_begin`.
- **W11** V10 rollback depends on the undocumented realloc-preserves-on-failure
  contract; a custom allocator that frees-on-failure breaks it. Document or
  snapshot-and-restore the pointer.
- **W12** caps_detect always strips Kitty caps under tmux/screen/zellij even with
  passthrough configured (design default — document / require force_cap).
- **W13** ConPTY `(DWORD)n` truncation for >4 GiB writes (latent behind the
  UNSUPPORTED stub).
- **W14** async_scan join-after-break is correct only by ordering; the
  "join all producers before close" contract is unstated.

## LOW (easy fixes applied; rest documented)
- **L5** `render_cursor` no bounds check (negative coords → bogus CUP).
- **L6** `snapshot_grid(out=NULL,cap=0)` returns 0, not the would-be length.
- **L10** fuzz tests' `TIMUI_CHECK(1)` is vacuous under non-sanitizer `make test`.
- **L14** V26 comment says macros are "defined" — they aren't (recognized by name).
- Documented-only: L1 (double-Esc resolution), L2 (flush clock for direct feed),
  L3 (cross-feed UTF-8 split dangling ptr), L4 (put_glyph overwrites wide
  continuation), L7 (table column remainder gap), L8/L4cmdpal cap (V24 class),
  L9 (poll non-EINTR swallowed), L11 (127-byte title truncation), L12 (kitty
  partial-frame abort), L13 (async_scan recv size discipline), L15 (MPSC
  destroy-vs-post contract).
