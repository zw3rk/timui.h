# timui.h — backlog

Ideas parked for later. Not scheduled; no commitment implied. Add a date + a
line of rationale when you pick one up.

**Shipped 2026-07-07** (four-additions goal): the **radio player** (`examples/radio.c`),
the **SQLite TUI** (`examples/sqlite_tui.c`), the **man page** (`docs/timui.1.md` +
`make man`), and **full UAX #9 bidi** via vendored SheenBidi (opt-in `WITH_SHEENBIDI=1`).
Checked off below.

**Shipped 2026-07-07** (layout + widget sweep goal): a **constraint layout engine**
(`timui_split`/`timui_grid`, LEN/PCT/FLEX/MIN/MAX) + **borders/gradient**
(`timui_border`, `timui_lerp_rgb`), a **virtual multi-column table** + **scrollable
tree**, **tabs**, **barchart/sparkline/gauge/meter/progress/spinner**, and a
**code viewer** (`timui_code`) — all first-class library widgets, showcased by
`examples/gallery.c`. Checked off below.

## Applications to build on timui.h
These are *demos/examples* that would stress the library and prove it out.

- [x] **Terminal internet-radio player** (the @kmdrfx UI —
      <https://nitter.net/kmdrfx/status/2074279530882093523>): a URL bar + 5
      station-preset tabs (FIP / WFMU / NPO / NPR / Dance Wave), a live **spectrum
      analyzer** (8 gradient bars 63 Hz–16 kHz with peak-hold caps), **PEAK/RMS
      meters**, a **stream-telemetry** key/value panel (state, buffer progress bar,
      decoded/played frames, underruns, reconnects, volume/pan), and a controls
      hint bar. Needs: minimp3 + miniaudio (both permissive single-header) + a small
      FFT (kissfft / pocketfft) for the spectrum, over timui panels/tabs/bar-chart/
      meters/table. A superb forcing function for the **bar-chart + gauge** widgets
      in the gaps below.
- [ ] **IRC client** — the natural next step after `chat.c`: a real protocol
      (RFC 1459/2812) over a socket, channels/tabs, nick list, `/commands`,
      scrollback per buffer. Mostly reuses the chat transcript + composer.
- [ ] **Matrix client** — much bigger: HTTP long-poll/sync (`/sync`), E2E crypto
      (Olm/Megolm — **must** come from a vetted lib, never hand-rolled per the
      crypto policy), rooms, threads, reactions, media. A good test of async +
      the threading model; likely needs a JSON + HTTP dependency.
- [ ] **SimpleX Chat client** — no identifiers/servers-of-record; needs the SimpleX
      messaging protocol + queue model + crypto (again: vetted lib only). Research
      the reference client first; heaviest of the three chat apps.
- [ ] **Pi-style agent harness** with **Chez Scheme** as the self-modification
      language — a TUI over an agent loop where the agent rewrites its own Scheme.
      Needs an embedded Chez (or a subprocess bridge), a transcript/tool-call view,
      a diff/apply pane. Big; scope a spike first (embed Chez + eval a snippet).
- [x] **SQLite TUI** — the most tractable. `sqlite3` (amalgamation, single .c) for
      the engine; a schema tree + a results **table widget** (see gaps) + a query
      editor (reuse the multi-line composer + the code highlighter). Good forcing
      function for the table + tree widgets we lack.

## Framework parity — what other TUI frameworks have that we don't (yet)
Surveyed: bubbletea/lipgloss (Go), ratatui (Rust), Textual (Python), Ink/blessed
(TS), notcurses/FTXUI (C/C++). timui already has: immediate-mode cells, threading
(MPSC), theme, Kitty graphics, hyperlinks, scroll, a widget set (button/input/
listbox/dialog/menu/modal/cmdpal), synchronized-update, and now (in the chat
example) word-wrap, a small bidi, and syntax-highlighted code. Missing:

**Widgets**
- [x] **Table / data grid** (columns, sort, scroll, selection) — the highest-value gap.
- [x] **Tree view** (expand/collapse) — file trees, JSON, schemas.
- [x] **Tabs / tab bar** (`timui_tabs`) — split / resizable panes still pending.
- [x] **Progress bar · spinner · throbber**.
- [x] **Sparkline / bar chart / plot** (ratatui has a charts module).
- [x] a reusable **code viewer** (`timui_code` + `timui_highlight`) — a **Markdown viewer** is still pending.
- [ ] **Autocomplete / combobox**, **text area** (promote the chat's multi-line composer), **toast/notification**.

**Layout & styling**
- [x] **Flexbox / grid / constraint layout** (ratatui `Layout`, Textual CSS grid, lipgloss).
- [x] rounded/double/thick **borders** (`timui_border`) + **gradients** (`timui_lerp_rgb`) — a **CSS-like stylesheet** (Textual TCSS) is still pending.

**Architecture & interaction**
- [ ] **Elm-style component model** (Model/Update/View + Cmd/Sub) as an optional layer over immediate mode.
- [ ] **Focus traversal / tab order**, **keybinding maps / vim modes**, richer **mouse** (drag, hover).

**Text / i18n / images**
- [x] **Full Unicode bidi** — ship it as an OPTIONAL opt-in, keeping our 2-level
      approximation as the always-on default. Library choice is a licensing call:
    - **SheenBidi** (Apache-2.0) — best fit: a small, self-contained UAX #9 C lib we
      can *vendor* like stb/msf_gif. No system dependency, license matches ours.
    - **FriBidi** is **LGPL-2.1+, NOT GPL** — linking it does *not* make timui GPL
      (LGPL explicitly permits linking without copyleft on the caller). A dynamic-
      link opt-in is clean; static linking adds only a relink obligation. But it is
      a system dependency, not vendorable.
    - **ICU** (permissive Unicode license) — fully correct but very large.
    Also **grapheme clustering** (ZWJ / skin-tone emoji, combining marks).
- [ ] **Sixel + iTerm2** image protocols (we have Kitty only).

**Dev experience**
- [ ] **Devtools / inspector** (Textual's console) + **hot reload**.
- [ ] **Accessibility** (screen-reader hints).
- [ ] Snapshot testing exists (`drive/` + goldens); broaden it.

## Tooling
- [x] **Man page** — `docs/timui.1.md` (Markdown) → roff via **pandoc**
      (`pandoc -s -t man docs/timui.1.md -o build/timui.1`), wired as `make man`
      + a `make install-man`. Small; good first pickup.
