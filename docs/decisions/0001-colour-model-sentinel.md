# ADR 0001 — Colour model: a sentinel for "default", 0x000000 is black

date: 2026-07-03
status: Accepted

## Context

`TimuiStyle`/`TimuiCell` store foreground/background as packed `0xRRGGBB`
(`uint32_t`). Before this decision, the value `0` was overloaded to mean
**"default"** — `emit_sgr` emitted no colour SGR when `fg==0`/`bg==0`. Pure
black (`0x000000 == 0`) was therefore indistinguishable from default and
**unrepresentable**. This was the V27 gap: a black-foreground theme slot
(e.g. DOS_GRAY's button) rendered with the terminal's default foreground
instead of black on any terminal whose default fg wasn't already black.

## Decision

Reserve an **out-of-range sentinel** `TIMUI_COLOR_DEFAULT` (`0xFFFFFFFF`,
above the 24-bit `0xRRGGBB` space) for "default / no SGR". `0x000000` becomes
literal black.

- `timui_style_make` / `th_mk` pass `fg`/`bg` through unchanged: `0` = black,
  `TIMUI_COLOR_DEFAULT` = default.
- `emit_sgr` emits truecolor when `fg != TIMUI_COLOR_DEFAULT` (so `0x000000`
  emits `38;2;0;0;0`); default emits no colour SGR.
- `cells_clear` sets each cleared cell's `fg`/`bg` to the sentinel (the empty
  state is "default", not "black").
- `snapshot_grid` prints `-` for the sentinel, `000000` for black.
- The libvterm round-trip maps `VTERM_COLOR_IS_DEFAULT_FG/BG` ↔ sentinel.

## Considered alternatives

- **Per-cell `has_fg`/`has_bg` flag bits** (in the spare `flags` field).
  Correct and `memset`-friendly, but two concepts (sentinel in the style, flag
  in the cell) and more touch points (draw must set the flag, emit/equality/
  snapshot must read it). Rejected for complexity.
- **Near-black hack** (`TIMUI_COLOR_BLACK = 0x000001`). No model change, but
  not real black and surprising. Rejected.
- **Map `0 → default` at `style_make`** (backward compatible). Preserves the
  bug (black still unrepresentable through the public constructor). Rejected.

## Consequences

- `cells_clear` is no longer a pure `memset` (it sets `fg`/`bg` to the
  sentinel). Cost: one pass over `w×h` cells per frame — negligible against
  rendering, and only the `fg`/`bg` fields are touched (the rest stays 0).
- The builtin themes need **no changes**: every `0x000000`/`black` in them is
  intentional black (button text, input bg, …) — under the old model those
  were silently default; now they are correctly black.
- Callers (tests/examples) that passed `0` for "default" must use
  `TIMUI_COLOR_DEFAULT`; those that passed `0` for black-on-coloured keep `0`.
- Pure black is now first-class: the DOS themes render correctly on
  non-black-default terminals.
