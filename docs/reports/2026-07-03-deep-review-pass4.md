# Deep codebase review — pass 4 (2026-07-03)

type: Report
title: Fourth pass — dynamic verification (ASAN/UBSAN) + adversarial/lifecycle/API
date: 2026-07-03

The first three passes were static (code reading) and fixed G/V, W, then X
([round 1](2026-07-03-deep-review.md), [2](2026-07-03-deep-review-pass2.md),
[3](2026-07-03-deep-review-pass3.md)). This pass added the **dynamic** dimension
the prior three lacked and an **adversarial/lifecycle/API** lens.

## Dynamic verification (new this pass)

- **UBSAN** (system clang, `-fno-sanitize-recover=undefined`): the full unit
  suite runs clean — no undefined behavior.
- **ASAN** (system clang, `detect_leaks=0`; nix-clang hangs on darwin, hence
  system clang): the full suite runs clean — no address errors.

This is strong evidence the prior memory fixes hold. It also explains how one
defect (Y1) slipped three static passes: its OOB write is a single byte at the
allocation tail, which ASAN's redzone didn't catch — it took an
adversarial-input review (cursor ≥ cap) to surface it.

## X-fix audit (pass-3 fixes re-verified)

All pass-3 X-fixes verified correct and regression-free: X1 tree prefix bound,
X2 render_diff wide-glyph last_x, X3 message_box modal clear, X4 text_area
cap==0, X5 listbox scroll clamp, X6 message_box boxw floor, X7 grow guards.
Mutation-tested: `test_tree_deep_safe` and `test_text_area_zero_cap_safe` each
FAIL without their fix (the former fires an ASAN stack-buffer-overflow).

## Fixed this pass (Y-series)

- **Y1 (HIGH)** `text_area` wrote `text[cursor]` trusting the caller-supplied
  cursor; `cursor >= cap` → heap OOB NUL write on focus. Clamped to `cap-1`.
- **Y2 (MEDIUM)** `draw_fill` with a huge rect: `r.y+r.h` signed-overflow (UB)
  + ~INT_MAX-iteration CPU burn. Now clamps the rect to the buffer first.
- **Y3 (MEDIUM)** `listbox` didn't clamp `selected` into `[0,count-1]` (tree/
  table do) → stale selection invisible/stuck. Clamped.
- **Y4 (MEDIUM)** `keymap_hit` short-circuited on the first binding for an
  action → a second key bound to the same action unreachable. Now tests all.
- **Y7 (LOW)** `cmdpal` matcher used `strlen`, ignoring `TimuiStr.len` →
  non-NUL-terminated slice over-read. Now takes the `TimuiStr`.
- **Y5/Y6 (LOW/docs)** documented that `timui_end` is single-use per `begin`,
  and that all three `TimuiAllocator` functions are required (realloc included).

## Documented this pass (not fixed — contract/design)

- **double-`end`** re-renders + re-swaps (no "frame active" guard); documented
  as single-use (Y5). Adding a guard is a minor state addition, deferred.
- **PLAUSIBLE (re-confirmed unreachable)**: `hyperlink_set` `int nc` overflow
  (links bounded by cell count ≪ INT_MAX); `clip_stack` push-dropped/pop-stale
  at >8 nesting (reset per frame, real nesting ≤3); `realloc`-required
  assumption (now documented, Y6).

## Verification

151 unit + 8 libvterm round-trip, UBSAN clean, ASAN clean, Tier-B goldens clean,
amalgamate + release-check green, TIMUI_NO_THREADS compiles.
