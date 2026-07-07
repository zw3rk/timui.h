# Visual testing — timui.h

timui.h renders into a cell buffer and emits escape sequences. Two test
tiers validate that pipeline at increasing fidelity:

| tier | what it checks                          | deps       | target          |
|------|-----------------------------------------|------------|-----------------|
| **B** | the cell buffer matches a golden dump   | none       | `make goldens` / `make test` |
| **A** | emitted escapes reconstruct the screen  | libvterm   | `make vt-test`  |

Both run in CI: Tier B inside `check` (zero deps, every PR), Tier A in its
own `vterm` job using the neovim/Paul Evans libvterm API.

---

## Tier B — golden snapshots (no dependencies)

`timui_snapshot_grid(buf, out, cap)` serializes a `TimuiCellBuffer` to a
deterministic, diffable text form. One line per row, each cell as five
`|`-separated fields:

```
R<row>: <codepoint>|<fg>|<bg>|<attrs>|<width> ...
```

- **codepoint** — the literal glyph for printable ASCII (`0x20`–`0x7e`);
  `.` for an empty cell (codepoint 0); `U+XXXX` (uppercase hex) otherwise.
- **fg / bg** — `-` when the field is `TIMUI_COLOR_DEFAULT` (`0xffffffff`,
  i.e. `emit_sgr` emits no color SGR); otherwise 6-digit lowercase hex
  `rrggbb`. Pure black `0x000000` is literal black and distinct from default.
- **attrs** — `.` if none, else sorted flag letters:
  `b`(old) `d`(im) `i`(talic) `u`(nderline) `r`(everse) `k`(blink) `s`(trike).
- **width** — the cell's `width` field (1 or 2; 0 for a cleared/empty cell,
  since `cells_init` memsets to zero).

`timui_grid_eq(a, b, diff_out, diff_cap)` compares two grids field-by-field
(codepoint/fg/bg/attrs/width) and writes a one-cell diff on the first
mismatch. It is the shared primitive Tier A reuses, so both tiers report
mismatches in the same shape.

### Golden workflow

- **Scenes** live in `tests/scenes.h` and are built *identically* by
  `tools/gen_golden.c` (which writes `tests/golden/<scene>.txt`) and by the
  `test_snapshot_goldens` test — so the two producers cannot drift.
- **Regenerate** with `make goldens` whenever rendering intentionally
  changes, then review the `git diff` of `tests/golden/`.
- **A golden change in a PR is a signal**, not noise: it means the
  renderer's cell output shifted. The reviewer confirms the shift is
  intended (and that `tests/scenes.h` wasn't accidentally changed).
- **Staleness guard**: CI runs `make goldens && git diff --exit-code --
  tests/golden/`, and `make test` runs `test_snapshot_goldens`, so a
  rendering change that slips in without regeneration fails the gate with a
  clear, named message.

---

## Tier A — libvterm round-trip

`tests/test_vt_roundtrip.c` validates the renderer end-to-end against a
real VT emulator:

```
scene cell buffer
  -> timui_render_diff through a fake transport (captures the escapes)
  -> vterm_input_write into a fresh libvterm of the same size
  -> read back libvterm's screen grid
  -> compare it cell-by-cell against the scene
```

This catches SGR / CUP / OSC-8 / truecolor / wide-glyph bugs that
byte-substring checks cannot. Run it with `make vt-test` (it sets
`WITH_VTERM=1`, which resolves libvterm via pkg-config and compiles the
round-trip TU into a separate `build/test_vt` binary — the core
`make test` never depends on libvterm).

### Platform note

nixpkgs has multiple packages named around libvterm. This harness requires
`libvterm-neovim` (the Paul Evans/neovim API: `VTerm`, `VTermScreen`,
`vterm_new`, `vterm_input_write`, ...), whose pkg-config module is `vterm`.
The unrelated `libvterm` package exposes a different `vterm_t` API and is not
compatible with these tests.

The flake includes `libvterm-neovim`, so `nix develop -c make vt-test` runs on
both Linux CI and the supported Darwin dev shells.

### Cell-mapping footguns (TimuiCell ↔ VTermScreenCell)

These are the subtle bits, documented in `tests/test_vt_roundtrip.c`:

1. **`vterm_new(rows, cols)`** — libvterm takes rows then cols; timui
   buffers are `(w, h)`. Transpose when creating the vterm.
2. **Default colors**: timui `fg==TIMUI_COLOR_DEFAULT` /
   `bg==TIMUI_COLOR_DEFAULT` means "no SGR" ↔ libvterm
   `VTERM_COLOR_IS_DEFAULT_FG`/`BG`. **Check the default flag *before*
   `vterm_screen_convert_color_to_rgb`** — that call *resets* the default
   flags, so checking after conversion silently turns "default" into the
   default color's RGB.
3. **Empty cells render as space**: a timui cell with codepoint 0 is
   emitted as `' '` (render.c), and a reset libvterm screen is full of
   spaces — so codepoint 0 and `' '` are treated as interchangeable
   "blank" on both sides.
4. **Wide-glyph continuation cells**: timui marks the second column of a
   width-2 glyph with `TIMUI_CELL_CONTINUATION`; libvterm reports a
   placeholder there. The comparison **skips** continuation cells — the
   glyph is validated at its lead cell (which carries `width` 2).
5. **`DIM` is not recoverable through libvterm** (it has no faint
   attribute), so `TIMUI_ATTR_DIM` is masked out of the round-trip
   comparison. DIM is still verified by the Tier-B golden snapshot, which
   serializes it as `d`.
6. **Two-frame feed**: the round-trip first feeds `empty→prev` (to
   establish the vterm's state), then the diff `prev→curr` under test, then
   compares. This makes the partial-update scene meaningful — vterm must
   start in `prev`'s state so a single-cell diff lands correctly.
7. **Hyperlinks (OSC 8)**: `VTermScreenCell` has no link field, so libvterm
   parses OSC 8 but stores nothing. The hyperlink scene verifies only that
   the OSC 8 sequence doesn't corrupt the glyph stream (the link text still
   lands) — the link target itself is not round-trip-verifiable.
8. **Explicit test gating**: the eight scene tests are registered in
   `test_main.c` only when `TIMUI_WITH_VTERM_TESTS` is defined by the
   `WITH_VTERM=1` build path. Plain `make test` must not gain vterm references
   just because a system include path happens to contain a `vterm.h`.

---

## File map

| file | role |
|------|------|
| `src/timui_snapshot.c` | `timui_snapshot_grid`, `timui_grid_eq` |
| `tests/scenes.h` | shared deterministic scenes (panel/rainbow/attrs/wide) |
| `tools/gen_golden.c` | writes `tests/golden/<scene>.txt` |
| `tests/golden/*.txt` | committed reference snapshots |
| `tests/test_snapshot.c` | Tier B unit tests + golden regression check |
| `tests/test_vt_roundtrip.c` | Tier A libvterm round-trip scenes |
