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

## Capability gating

Detection (`timui_caps_detect`) is conservative and deterministic from
`TERM` / `TERM_PROGRAM` / `COLORTERM`: modern terminals get the full set;
multiplexers (**tmux / screen / zellij**) conservatively drop kitty-keyboard /
graphics / sync (unless passthrough is known); unknown terminals fall back to a
safe 16-colour, ASCII-friendly minimum. `force_on` / `force_off` masks override.

## Future (v0.2)

OSC 8 hyperlinks, the Kitty graphics protocol, and Windows ConPTY. The capability
flags (`TIMUI_CAP_OSC8_HYPERLINKS`, `TIMUI_CAP_KITTY_GRAPHICS`) are already
defined for them.
