# Deep codebase review — pass 3 (2026-07-03)

type: Report
title: Third independent review pass — prior-fix audit + new findings (X-series)
date: 2026-07-03

A third pass. Passes 1–2 fixed G1–G17, V1–V28, and W1–W14+LOWs
([round 1](2026-07-03-deep-review.md), [round 2](2026-07-03-deep-review-pass2.md)).
This pass audited the W-fixes (all verified correct/regression-free) and hunted
for new defects with fresh eyes on property/logic/integer edges, test-suite
correctness, and build/release integrity. Three reviewer domains completed
(render/widget, core/app, property/logic); test-suite and build were covered
inline (no vacuous assertions remain; TIMUI_NO_THREADS/amalgamate/release-check
all green).

## Fixed this pass (X-series)

- **X1 (HIGH)** `timui_tree` — `prefix[32]` stack overflow for nodes with
  `depth >= 15` (2 bytes/level + marker/space/NUL; app-supplied depth unchecked).
  Larger `prefix[64]` + a loop bound reserving room for the marker/space/NUL.
- **X2 (MEDIUM)** `render_diff` — `last_x = x + 1` ignored a wide glyph's width;
  at a clip edge (continuation omitted by `put_glyph`) the cursor run didn't
  resync and following changed cells rendered one column right. Now
  `last_x = x + (width>=2 ? 2 : 1)`.
- **X3 (MEDIUM)** `message_box` — W3 residual: `modal_active` stayed pinned when
  the parent was too narrow to render any button, trapping all input. Now clears
  `modal_active` when no button could render.
- **X4 (LOW)** `timui_text_area` — missing `cap==0` guard → OOB NUL write when
  focused (input_line_buf had the guard). Added.
- **X5 (LOW)** `timui_listbox` — `scroll` was never upper-clamped, so it could
  outrun the list tail, leaving trailing viewport rows unstyled. Clamped to
  `[0, count-visible]`.
- **X6 (LOW)** `message_box` — `boxw` could go negative for `parent.w < 12`;
  cleaned the clamp to floor at 2.
- **X7 (LOW)** unguarded `*2` capacity grows in `id_stack_push` and the fake-
  transport write buffer (classic overflow→small-alloc shape). Both guarded
  against size_t overflow.

## Documented this pass (PLAUSIBLE / theoretical — not fixed)

- **hyperlink_set** `int nc = link_cap*2` — same overflow shape, but
  unreachable: hyperlinks are bounded by cell count, far below INT_MAX.
- **clip_stack** push-at-capacity (>8 nested clips) saves no state but pop
  restores stale data — theoretical; the stack is reset each frame and real
  nesting is shallow (≤3). The push guard prevents buffer overflow.
- **non-tty O_NONBLOCK** is skipped if `fcntl(F_GETFL)` fails on the input fd,
  leaving a blocking read; rare (an fd valid for read but not fcntl).

## Verification

148 unit + 8 libvterm round-trip (154), Tier-B goldens clean, amalgamate +
release-check green, TIMUI_NO_THREADS compiles clean. Two prior review angles
that 429'd mid-run (test-suite, build) were covered inline.
