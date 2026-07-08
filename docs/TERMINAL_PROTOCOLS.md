# Terminal protocols — timui.h

What `timui.h` speaks on the wire, and where it falls back.

## Keyboard

- **Legacy CSI** (always): arrows (`ESC[A/B/C/D`), Home/End (`H`/`F`), the
  `CSI <n> ~` numerics (Delete/PageUp/PageDown/Insert, F5–F12), and SS3
  (`ESC O P/Q/R/S` for F1–F4). Modifiers via `CSI 1;<mods> <final>`.
- **Kitty keyboard protocol** (`CSI <code>;<mods> u`): parsed when the terminal
  sends it — codes map to keys (`27`→Esc, `13`→Enter, …) and mods decode
  (Shift/Alt/Ctrl/Super/Hyper/Meta).
- **Ctrl** keys (0x01–0x1a) and **Alt+**`<c>` (`ESC <c>`).

## Mouse

**SGR mouse** (`CSI < Cb ; Cx ; Cy M/m`): press/release, wheel (Cb 64/65 →
`wheel_y` ±1), drag/motion (Cb bit 0x20), and modifier bits (Shift/Alt/Ctrl).

## Paste / focus

- **Bracketed paste**: `CSI 200~ … CSI 201~` → one `PASTE` event (a view into
  the fed bytes; chunked across feeds).
- **Focus**: `CSI I` / `CSI O` → focus-in / focus-out.

## Output

- **Synchronized output** (DEC 2026): `CSI ? 2026 h` … `CSI ? 2026 l` wrapping a
  frame, emitted only when `TIMUI_CAP_SYNC_OUTPUT` is set. Cursor hide/show
  (`CSI ? 25 l/h`) is the fallback.
- **Truecolour SGR** (`CSI 38;2;r;g;b m` / `CSI 48;2;…m`) + attrs
  (bold/dim/italic/underline/blink/reverse/strike).
- **Alternate screen** (`CSI ? 1049 h/l`), title (`OSC 0 ; … BEL`).
- **OSC 8 hyperlinks**: emitted when `TIMUI_CAP_OSC8_HYPERLINKS` is set, with
  cell-level link tracking for hit-testing.
- **Terminal images**: `timui_image_*` accepts PNG bytes. Protocol selection is
  exposed via `timui_caps_image_protocol` / `timui_image_protocol`, with Kitty
  preferred when available, then Sixel, then iTerm2. This release emits Kitty
  graphics and iTerm2 inline images (`OSC 1337;File=...`) for unclipped draws.
  Sixel caps, and iTerm2 clipped draws, deliberately fall back to the same text
  placeholder instead of emitting unsupported or lossy escapes.

## Capability gating

Detection (`timui_caps_detect`) is conservative and deterministic from
`TERM` / `TERM_PROGRAM` / `COLORTERM`: modern terminals get the full set;
multiplexers (**tmux / screen / zellij**) conservatively drop image protocols
and, unless passthrough is known, kitty-keyboard / sync; unknown terminals fall
back to a safe 16-colour, ASCII-friendly minimum. `force_on` / `force_off`
masks override, and `timui_force_image_protocol` switches the active image cap
set for tests or user overrides.

## Planned

- **Windows ConPTY**: currently a runtime-unsupported backend stub. It belongs
  below the protocol layer as a transport/lifecycle backend, not as a graphics
  abstraction.
- **Sixel emitter**: planned as an additional wire protocol behind the existing
  image API/capability model. The capability enum and selector are in place,
  but Sixel needs a real pixel source/encoder before timui can honestly emit it.
