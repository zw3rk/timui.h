# Unicode contract

timui owns terminal-cell semantics, not font rendering.

## timui owns

- UTF-8 decoding and invalid-byte replacement.
- Terminal-cell display width for the codepoint ranges it supports.
- Grapheme-safe cursor movement, deletion, and truncation for the covered
  terminal UI cases.
- Cursor positioning in cells, including wide-cell continuation handling.

## Terminal emulators own

- Glyph rasterization.
- Font fallback and missing-glyph choice.
- Complex shaping.
- Colour emoji rendering.
- Exact appearance of ambiguous-width glyphs.

## Current policy

The implementation is range-based and deliberately small; it does not claim a
complete generated Unicode table version. The covered cluster behavior includes
CRLF, combining marks, variation selectors, emoji skin-tone modifiers,
regional-indicator flags, and ZWJ emoji runs. `timui_display_width` and
`timui_fit_cell` use the grapheme helpers so truncation does not split covered
clusters before appending an ellipsis.

Width policy:

- C0/DEL/C1 controls and combining/format modifiers have width 0.
- Replacement character U+FFFD has width 1.
- CJK/fullwidth ranges and emoji presentation ranges have width 2.
- Printable non-wide codepoints have width 1.

## Known limits

- Full generated UAX #29 grapheme tables remain future work.
- Full UAX #9 bidi is not part of core timui rendering. Example code may opt
  into vetted external bidi support where explicitly documented.
- Complex script shaping is left to the terminal and font stack.
- Ambiguous-width policy is intentionally conservative; applications that need
  exact typography should avoid depending on ambiguous glyph widths.
