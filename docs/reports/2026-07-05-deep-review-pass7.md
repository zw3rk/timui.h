# Deep codebase review — pass 7 (2026-07-05)

type: Report
title: Seventh pass — six-lens parallel review (memory / adversarial / functional / DRY-CLEAN-SOLID / API / coverage)
date: 2026-07-05

Rounds 1–4 were the numbered report passes (static G/V, W, X; then dynamic +
adversarial Y — see [1](2026-07-03-deep-review.md),
[2](2026-07-03-deep-review-pass2.md), [3](2026-07-03-deep-review-pass3.md),
[4](2026-07-03-deep-review-pass4.md)); rounds 5–6 were user-directed and landed
inline (W6/W11/W12/W14/V24; G6/G7/G13; V27/ADR 0001) with no separate report.
This **round 7** fanned six independent read-only reviewers across distinct
lenses — memory-safety/UB, adversarial-input, functional/immutable adherence,
DRY/CLEAN/SOLID, API/single-header hygiene, and test-coverage — each grounded in
`file:line`, adversarially self-refuted, and de-duplicated against the whole
prior G/V/W/X/Y/L history. New findings take the **Z-series**.

Baseline going in: `make check` green (**160** unit tests), ASAN/UBSAN clean
(carried from round 4/6), `release/timui.h` byte-identical to a fresh
amalgamation of `src/`.

## Headline

The untrusted-input **memory** surface is genuinely clean after six rounds — the
feed state machine, both UTF-8 decoders, mouse/kitty/paste parsing, the title
sanitizer, and the diff renderer all carry correct earlier clamps/guards; the
one memory finding (Z9) is a low-severity *consistency* gap, not a reachable
overrun. The real yield this round is in **input correctness** (three parser
misparse/injection defects the memory lens can't see because nothing crashes),
**honest documentation** (a header banner that tells readers the library is an
unimplemented stub), and **error-path test depth** (the injectable allocator is
wired into only three of the many functions that can OOM).

## Findings

Severity/confidence and a one-line reachability are given per item; full
evidence lived in the per-lens agent reports. `[fix]` = actionable this pass;
`[rec]` = recommendation deferred for a decision (breaking public-API change).

### High

- **Z1 `[fix]` — header banner is false ("Phase 0 scaffold … lifecycle functions
  report `TIMUI_ERR_UNSUPPORTED`").** `include/timui.h:12-15` (and the
  "terminal backend lands in Phase 2" tag at `:174`) describe a non-functional
  scaffold; `timui_open` (`src/timui_core.c:134`) is fully implemented and never
  returns `UNSUPPORTED`, and the entire render/widget/menu/table/tree/snapshot
  stack ships. For a single-header library the header *is* the front-page docs,
  so the first thing an integrator reads is actively wrong. **Fix:** rewrite the
  banner to the shipped v0.1 state; note the one real stub (ConPTY → UNSUPPORTED,
  G10). (High = actively misleading, not a code bug.)

### Medium — input correctness (untrusted terminal bytes)

- **Z2 `[fix]` — input UTF-8 decoder skips overlong/surrogate/range validation
  → NUL & invalid-UTF-8 injection.** `src/timui_input.c:128-133` (`utf8_lead`)
  and the case-4 emit (`:326-334`) never apply the overlong / `D800..DFFF` /
  `>10FFFF` rejection that the *render* decoder does (`src/timui_render.c:88-93`).
  Trigger `C0 80` → `emit_text(codepoint=0)` → `timui_core.c:246` re-encodes a
  real `0x00` into `text_in`, which `input_line_buf`/`text_area` store into the
  app's C-string — defeating the V14 ground-state NUL guard (embedded NUL =
  filename/command truncation). Surrogate/above-max bytes land as ill-formed
  UTF-8. **Fix:** validate `utf8_cp` after `utf8_need==0` exactly as the render
  decoder does and substitute `0xFFFD` before `emit_text`. Regression:
  `regression/test_z2_input_utf8_validate.c`.

- **Z3 `[fix]` — an ESC arriving mid-CSI/SS3 resyncs to ground and drops the
  ESC, leaking the next sequence body as text.** `src/timui_input.c:317`
  (case-2 fall-through `p->state = 0`) consumes an unhandled byte with no
  restart; ESC (0x1b < 0x40) hits it, and case-3 (SS3) eats it as a bogus final.
  `1b5b33 1b5b41` (`ESC[3` truncated, then `ESC[A` Up) loses the Up-arrow and
  injects literal `"[A"`. ECMA-48/xterm require ESC to *abort and restart* a
  sequence. **Fix:** handle `c==0x1b` at the top of case 2 and case 3 →
  `state=1`, restamp `esc_since_ms`. Regression:
  `regression/test_z3_esc_mid_csi_restart.c`.

- **Z4 `[fix]` — a CSI `:` sub-parameter (Kitty event-type / alternate-key
  reports) hits the same resync path → key dropped, sub-param tail injected.**
  `:` (0x3A) sits between `'9'`(0x39) and `;`(0x3B) and matches no case-2 branch,
  falling to `:317`. `1b5b39373b313a3375` (`ESC[97;1:3u`) drops the 'a' key and
  injects `"3u"`. `:` is a legal ECMA-48 parameter-substring separator emitted by
  Kitty flags 2/4. **Fix:** add a persistent `sub_param` field to
  `TimuiInputParser`; on `:` enter sub-param mode (ignore digits until `;`/final)
  instead of resyncing. Regression: `regression/test_z4_csi_subparam.c`.

### Medium — code quality / CI

- **Z5 `[REJECTED on verification]` — "release-check regenerates instead of
  verifying; drift invisible to CI".** The API lens assumed `release/timui.h` is
  a *committed* file that could go stale. It is **not**: `.gitignore:2` ignores
  `/release/`, so the amalgamated header is a generated artifact that is never
  committed — there is no tracked copy that can drift, and a
  `git diff --exit-code -- release/timui.h` guard would be a no-op (the path is
  ignored). `release-check`'s regenerate-and-compile is the correct and
  sufficient guard for a generated single header (it proves the amalgamation
  produces a compilable drop-in). The lens's "byte-identical to the committed
  copy" was a comparison against a stale local build artifact, not a tracked
  file. No change made; `ci.yml` left as-is.

- **Z6 `[fix]` — fourth hand-inlined UTF-8 encoder.** `src/timui_core.c:246-249`
  re-implements `utf8_encode` (`src/timui_render.c:277`) byte-for-byte because
  `core.c` is amalgamated before `render.c`, so the static isn't yet in scope.
  Two encoders to keep in sync (a future 4-byte fix must touch both). **Fix:**
  hoist one `timui_utf8_encode_` into `src/timui_int.h` (included first) and call
  it from `core.c`, `render.c`, and the title sanitizer in `term.c`.

- **Z7 `[fix]` — `put_glyph` / `draw_text` / `draw_text_linked` triplicate the
  subtle wide-glyph continuation logic** (`src/timui_render.c:111-210`), the
  exact site of the prior V6/X2 bugs; `draw_text_linked` differs only by setting
  `hyperlink_id`. **Fix:** give `put_glyph` a `uint32_t link` parameter (0 =
  unlinked); `draw_text_linked` collapses to `draw_text`'s loop with the link
  argument, and the continuation-blank logic lives once. Guarded by the existing
  wide-glyph + OSC-8 tests.

- **Z8 `[fix]` — dead / half-wired `TIMUI_KEYIN_*` flags + misleading comment.**
  `TIMUI_KEYIN_LEFT/RIGHT` are set every frame (`src/timui_core.c:238-239`) but
  read by no widget; `HOME/END/DELETE` are declared only (`include/timui.h:230-234`)
  and never set or read. `input_line_buf` is append-only and `text_area`'s own
  comment says "cursor movement … is future", so there is no widget to consume
  them. The `:228` comment ("accumulated per frame for the focused input")
  implies all work. **Fix (honest, minimal):** drop the dead LEFT/RIGHT set
  branches, mark LEFT/RIGHT/HOME/END/DELETE `reserved (not yet wired)` — the same
  treatment `timui.h:45` already gives reserved feature macros — and correct the
  comment to name only the live BACKSPACE/UP/DOWN. (Wiring real cursor editing is
  a feature, tracked separately, not a defect fix.)

### Low — hygiene

- **Z9 `[fix]` — `draw_box`/`draw_hline`/`draw_vline` lack the Y2 `rect_clamp_buf`
  guard `draw_fill` got** (`src/timui_render.c:233-263`). On extreme
  app-supplied geometry (`TIMUI_RECT(1,0,INT_MAX,3)`), `r.x+r.w-1` is signed-
  overflow UB and the edge loops run ~2 billion iterations. Not a write overrun
  (`put_glyph` bounds-checks every cell) — the Y2 UB/DoS profile on the three
  siblings Y2 missed. **Fix:** clamp at entry, mirroring `draw_fill`. Regression
  pairs with Z17.

- **Z10 `[fix]` — implementation-only macros leak into the consumer TU without
  `#undef`; `R_EMIT` is un-prefixed** (`src/timui_render.c:293`; also
  `TIMUI_EMIT` term.c, `TIMUI_RUN_BUF` app.c, `TIMUI_ID_ROOT`/`TIMUI_MPSC_*`
  core.c). The `KITTY_CHUNK` `#undef` (`timui_kitty.c:89`) shows the intended
  discipline, applied to only one macro. **Fix:** `#undef` each at its section
  end.

- **Z11 `[fix]` — un-prefixed internal typedefs `SnapBuf` / `ConptyCtx` leak**
  (`src/timui_snapshot.c:44`, `timui_conpty.c:10`) — every other file-scope
  typedef is `Timui`-prefixed; in the single-TU implementation build these reach
  the consumer's namespace. **Fix:** rename `TimuiSnapBuf` / `TimuiConptyCtx`.

- **Z12 `[fix]` — `timui_ui_resize` returns `void`; a resize-time OOM is
  unsignalable** (`src/timui_core.c:287-303`) — the same class G6/G7 closed. The
  V10 rollback keeps state consistent but the app can't learn the surface didn't
  grow. **Fix:** return `TimuiResult` (source-compatible; existing callers ignore
  it).

- **Z13 `[fix]` — orphaned public API surface** (`include/timui.h`):
  `TimuiDialogResult`/`TIMUI_DIALOG_*` (`:257-263`) are declared but `message_box`
  returns a raw `int`; `TimuiCell.image_id` (`:400`) and
  `TIMUI_CELL_DIRTY/WIDE/IMAGE/LINK` (`:386-389`) are never read/written (only
  `CONTINUATION` is live). An integrator can't tell load-bearing from vestigial.
  **Fix:** annotate them `reserved` (matching the reserved-macro style) or remove;
  keep only what's live.

- **Z14 `[fix]` — rect-cut family mislabeled "pure".** `include/timui.h:372`
  banners `cut_*`/`inset`/`pad`/`split_*` as "pure", but the four `cut_*`
  (`src/timui_core.c:638-669`) mutate `*r` in place (the RectCut idiom).
  **Fix:** split the banner so `cut_*` is documented as the intentional in-place
  carve-out; `inset`/`pad`/`split_*` stay pure.

### Test-coverage gaps (all `[fix]` — additive)

Public-API breadth is ~90%; the deficit is **error-path depth** — the injectable
allocator is exercised in only three spots (arena, cells-resize, id_stack-push).

- **Z15 (Med-High)** — `timui_setup`'s four partial-OOM rollback branches
  (`src/timui_core.c:49,54,57,84`) are never driven; a leak/double-destroy there
  is invisible to ASAN (no test enters the branch). Add a counting-allocator
  test looping `fail_at ∈ [2..4]` over `timui_open_for_test`, asserting
  `TIMUI_ERR_OUT_OF_MEMORY`, `*out==NULL`, and zero net bytes outstanding.
- **Z16 (Med)** — message API `timui_emit`/`timui_post`/`timui_recv`/
  `timui_frame_quit` (`src/timui_app.c`) has zero coverage.
- **Z17 (Med)** — `timui_draw_hline`/`timui_draw_vline` untested (not reached via
  `draw_box`); pairs with the Z9 huge-geometry regression.
- **Z18 (Low-Med)** — `cut_left`/`cut_right`/`pad`/`split_rows` untested siblings
  of the covered `cut_top`/`split_cols`.
- **Z19 (Med)** — `timui_label_hyperlink` (`src/timui_widgets.c:349`) frame-level
  wrapper untested (+ the `uri==NULL` branch).
- **Z20 (Low-Med)** — `timui_function_bar` untested.
- **Z21 (Low-Med)** — `timui_interact_button` tab_order grow-OOM branch
  (`src/timui_widgets.c:72-78`) untested (success path covered by V24).
- **Z22 (Low-Med)** — `timui_hyperlink_set` NULL/OOM/URI-truncation branches
  untested (positive path only).
- **Z23 (Med)** — `timui_run` untested; at least the cheap negative guards
  (`NULL cfg/app/view → 1`) are synthesizable without a tty.
- **Z24 (Low)** — `timui_str_len` / `timui_now_ms` untested.
- **Z25 (Low)** — the V11 termios `tcsetattr`-failure double-free branch has no
  guarding test (happy round-trip only); add failure injection.

### Recommendations — deferred (breaking public-API decisions)

- **Z26 `[rec]` (Med) — v0.2 state widgets are mutation-only with no value twin
  and not named `_mut`.** `timui_tree`/`timui_table`/`timui_command_palette`
  take a pointer and mutate it (tree clamps `*selected` unconditionally *every*
  frame → aliasing surprise on a shared/derived index), unlike `timui_listbox`
  which offers a value-returning controlled form + a `timui_listbox_mut` twin.
  This is genuine drift from the house controlled-widget style. The clean fix
  mirrors listbox — add value-returning `TimuiTreeResult`/… forms, rename the
  pointer forms `*_mut` — but that **renames three public functions**, so it is
  a decision for the maintainer, not a silent change.
- **Z27 `[rec]` (Low) — the menu bar is the lone uncontrolled widget** — the
  framework owns `open_menu` plus a 6-field layout cursor in `Timui`
  (`src/timui_int.h:54-55`); the app can't observe/snapshot which menu is open.
  Bringing it into the controlled family is a menu-API redesign; deferred.
- **Z28 `[opt]` (Low) — list-like widgets duplicate "fill row + draw text" and
  "focused up/down nav"** ~9× across list/tree/table/cmdpal/menu; extractable to
  two internal helpers. Safe refactor, low urgency.

## Rejected (representative — full lists in the lens reports)

Memory: input `i--` feed-boundary wrap (V2, self-correcting), `paste_tail[6]`
bounds (V12 invariant holds), `cell_link_uri` post-swap read, b64/kitty size
math, all fixed local format buffers — traced clean. Adversarial: SGR coords
never index a buffer; CAN/SUB mid-CSI correctly abort. Functional: the SIGTERM
static, arena, interact/tab_order, msgq, and `TIMUI_COLOR_DEFAULT` are all
sanctioned/documented carve-outs. API: src↔release parity (byte-identical),
147/147 symbol match, external-symbol namespacing — all clean.

## Prioritized TODO

Order: correctness → honesty → CI integrity → hygiene → coverage → recommend.
Each `[fix]` bugfix lands red-first in a `regression/`-named test before its fix
(TDD gate); each keeps `make check` + `test-san SAN=address,undefined` green and
re-runs `amalgamate` + `release-check`.

- [x] **Z2** input UTF-8 overlong/surrogate/range validation (+ regression)
- [x] **Z3** ESC-mid-CSI/SS3 abort-and-restart (+ regression)
- [x] **Z4** CSI `:` sub-parameter handling (+ struct field + regression)
- [x] **Z9** clamp `draw_box`/`hline`/`vline` geometry (+ Z17 regression)
- [x] **Z1** rewrite the false header banner
- [x] **Z8** honest `TIMUI_KEYIN_*` reserved marking + drop dead stores
- [x] **Z14** correct the rect-cut "pure" banner
- [~] **Z5** REJECTED — `release/` is gitignored; no committed copy can drift
- [x] **Z6** hoist shared `timui_utf8_encode_` into `timui_int.h`
- [x] **Z7** unify glyph emit via `put_glyph_link(..., link)`
- [x] **Z10** `#undef` leaking impl macros (`R_EMIT`, …)
- [x] **Z11** prefix `TimuiSnapBuf` / `TimuiConptyCtx`
- [x] **Z12** `timui_ui_resize` → `TimuiResult`
- [x] **Z13** mark orphaned `TimuiDialogResult` / cell flags / `image_id` reserved
- [x] **Z15–Z24** coverage: lifecycle-OOM, message API, hline/vline (Z17), layout
      siblings, label_hyperlink, function_bar, tab-grow OOM, hyperlink_set
      neg/OOM, `timui_run` negatives, getters
- [~] **Z25** DEFERRED — termios `tcsetattr`-failure needs syscall mocking (no
      portable injection point); happy-path round-trip stays covered
- [ ] **Z26/Z27** decide with maintainer (breaking widget/menu API); **Z28**
      optional list-widget helper extraction

Net: 174 unit tests (was 160), +14 (4 regression, 10 coverage). ASAN + UBSAN
clean (system clang on darwin — nix-clang's ASAN hangs there, per pass 4; CI
runs both on Linux); `amalgamate` + `release-check` green.

## Verification (to hold after fixes)

`make check` (unit + Tier-B goldens) green with the new count; `test-san
SAN=address` and `SAN=undefined` clean; `amalgamate` + `release-check` pass and
`release/timui.h` diff-clean; `TIMUI_NO_THREADS` compiles; every `[fix]` bugfix
has a red→green regression under `regression/`.
