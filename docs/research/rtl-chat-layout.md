# RTL chat layout in a cell-grid TUI

How to lay Hebrew/Arabic messages out correctly when the terminal does **no**
bidi reordering itself (each cell is drawn positionally). Source: multi-agent
research pass against Unicode UAX #9, W3C i18n, and platform RTL guidance.

## Findings

1. **Row = three independent pieces: timestamp · sender label · message body.**
   Only the **body** is a bidi paragraph. Keep the sender name and timestamp as
   fixed "chrome"; do **not** concatenate `"sender: body"` and reorder the whole
   thing (that moves the sender to the far right and, unisolated, detaches the
   colon — the classic first-strong failure). Real RTL apps mirror the *layout*
   (leading/trailing), but each text field keeps its own internal order.
   [Apple "Supporting RTL Languages"; Material "Bidirectionality"; W3C
   "Strings and bidi".]

2. **Base direction = first strong character of the body** (UAX #9 P2/P3), *not*
   a majority/dominance count, and computed over the **body only** (excluding the
   Latin sender name — otherwise a name like `avi` forces LTR). Better still is
   per-message locale metadata; W3C says heuristics "SHOULD NOT" be relied on.

3. **Emoji are neutral (Bidi_Class = ON)** — verified in `DerivedBidiClass.txt`
   (`1F400..1F6D8 ; ON`, `26AD..2767 ; ON`). They behave exactly like `!`/`?`:
   in a **base-RTL** paragraph a trailing emoji resolves to R (UAX #9 N1/N2) and
   lands at the **visual LEFT** (logical end of the RTL run). The "wrong side"
   bug is always a base-direction bug, never an emoji bug.

4. **Reordering (UAX #9 L2)** reverses contiguous runs from the highest level
   down. A single whole-string reverse — or "reverse each RTL run in isolation"
   — silently breaks embedded numbers (13→31), embedded Latin words, and
   trailing-neutral placement. The correct engine is **FriBidi**
   (`fribidi_log2vis`) or **ICU** (`ubidi_*`), which is what mlterm/VTE/Pango use.
   Arabic **shaping** (base letters → presentation forms) is a *separate* step
   done in logical order **before** bidi.

## What this project implements (`examples/chat.c`)

A pragmatic **2-level** approximation — enough for chat lines, not full UAX #9:

- `first_strong_rtl(body)` picks the base direction from the body.
- Sender label + timestamp are drawn as left chrome; for a base-RTL body only
  the body is `bidi_visual(…, base_rtl=1)` and right-aligned.
- `bidi_visual` shapes Arabic first (`ARJOIN` table), then for base-RTL reverses
  the whole body (level 1 — so a trailing emoji/`!` lands left) and un-reverses
  embedded Latin/number runs (level 2). For base-LTR it reverses RTL runs in
  place.

**Known gaps vs. full UBA:** numbers embedded *inside* RTL beyond a single run,
nested embeddings/overrides, isolates, and bracket mirroring (N0/L4). The
correct fix if these matter is to link **FriBidi** and replace `bidi_visual`
with `fribidi_log2vis` (keeping the Arabic shaping step). FriBidi is a build
dependency, so it would need a `flake.nix` change + env reload.

## Sources
- Unicode UAX #9 (Bidi Algorithm) — P2/P3, W1–W7, N0–N2, L1/L2/L4:
  <https://www.unicode.org/reports/tr9/>
- UCD `DerivedBidiClass.txt` (emoji = ON):
  <https://www.unicode.org/Public/UNIDATA/extracted/DerivedBidiClass.txt>
- W3C i18n — inline bidi basics, strings & bidi, bidi controls:
  <https://www.w3.org/International/articles/inline-bidi-markup/uba-basics>
- FriBidi <https://github.com/fribidi/fribidi> · ICU bidi
  <https://unicode-org.github.io/icu/userguide/transforms/bidi.html>
- UAX #11 East Asian Width (cell widths; emoji = 2):
  <https://www.unicode.org/reports/tr11/>
