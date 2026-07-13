# timui.h - backlog

Ideas parked for later. Not scheduled unless a section explicitly says so. Add a
date and a line of rationale when you pick one up.

## Recently shipped

**Shipped 2026-07-07** (four-additions goal): the radio player
(`examples/radio.c`), the SQLite TUI (`examples/sqlite_tui.c`), the man page
(`docs/timui.1.md` + `make man`), and full UAX #9 bidi via vendored SheenBidi
(opt-in `WITH_SHEENBIDI=1`).

**Shipped 2026-07-07** (layout + widget sweep goal): a constraint layout engine
(`timui_split`/`timui_grid`, LEN/PCT/FLEX/MIN/MAX), borders/gradient
(`timui_border`, `timui_lerp_rgb`), a virtual multi-column table, scrollable
tree, tabs, barchart/sparkline/gauge/meter/progress/spinner, and a code viewer
(`timui_code`), showcased by `examples/gallery.c`.

**Shipped 2026-07-07** (applications/examples): `examples/irc.c` provides a
plaintext IRC client over the reusable `examples/irc_proto.h` parser, with tabs,
nick table, scrollback, `/join /part /msg /nick /me /quit`, worker-thread socket
I/O via `timui_post`, offline `--demo`/`--replay`, `make check-irc`, and
`make smoke-irc`.

## Phase 1.5 selected follow-up

This is the next planned sweep. It is library/platform work, not a large app
push. Detailed pickup prompt: `docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt`.

- [ ] **Windows ConPTY backend** - replace the unsupported stub with a real
      `CreatePseudoConsole` transport: pipes, process lifetime, resize, close,
      runtime symbol probing, short-write/read handling, and `DWORD` chunking.
      Do not promise graphics as part of ConPTY itself; graphics are selected by
      terminal image protocol capability above the transport.
      - [x] `_WIN32` backend implementation, resize API, POSIX fallback tests,
            and MinGW compile seam in `make check`.
      - [ ] Real Windows ConPTY smoke run on a Windows host and captured
            operator evidence.
- [x] **Submit-capable multi-line text area** - add a result-returning,
      controlled API plus `_mut` convenience wrapper, preserving the current
      void `timui_text_area` compatibility wrapper. Refactor `examples/chat.c`
      and `examples/irc.c` to use it instead of local composer workarounds.
- [x] **Autocomplete / combobox** - field-attached popup selection over
      caller-owned options/state, with keyboard, mouse, clipping, UTF-8 labels,
      empty-list behavior, and cap-limited query tests.
- [x] **Toast / notification** - immediate-mode renderer over caller-owned
      notification data: severity, TTL, stacking, clipping, and manual dismiss.
- [x] **Split / resizable panes** - build on `timui_split`/`timui_split_ex` with
      caller-owned split state and a local divider-drag interaction. Full global
      drag/hover architecture remains Phase 2.
- [x] **CSS-like stylesheet parser/resolver foundation** - a small
      Textual-TCSS-inspired parser/resolver
      over existing styles: widget/id/class/state selectors, simple specificity,
      source order, and properties for fg/bg/attrs/border/padding/gap/gradient.
- [x] **Stylesheet widget application** - thread resolved stylesheet values into
      visible widgets without removing existing theme APIs or explicit style
      override paths.
- [x] **Grapheme clustering foundation** - next/prev/width helpers integrated
      into editing and truncation for ZWJ emoji, skin tones, regional
      indicators, combining marks, VS16, and CRLF.
- [x] **Protocol-neutral image layer + Sixel + iTerm2** - keep Kitty support,
      but refactor image emission behind protocol capabilities. Add Sixel first
      (including Windows Terminal usefulness), then iTerm2 inline images, with
      fallback placeholders preserved.
      - [x] Public image protocol enum, capability selector, force override,
            and non-Kitty placeholder fallback.
      - [x] iTerm2 inline image emitter for unclipped PNG draws, with Kitty
            cleanup when switching protocols and placeholder fallback for
            clipped draws.
      - [x] Sixel encoder/emitter for raw RGBA images with exact palettes up to
            16 opaque colours and bounded quantization beyond that cap.
      - [x] Cropped Sixel emission for clipped raw-RGBA draws.
      - [x] Raw-RGBA Sixel scaling to known terminal cell-pixel geometry.
      - [x] `TIMUI_NO_IMAGES` build mode keeps the image API available while
            stripping image caps/escapes and rendering `[img]` placeholders.
      - [x] PNG+RGBA sidecar constructor preserves PNG passthrough for
            Kitty/iTerm2 while giving Sixel caller-decoded pixels for emission
            and clipping.
      - [x] Built-in bounded PNG-only decode for plain PNG images forced to
            Sixel, reusing the same RGBA Sixel encoder and fallback behavior
            for malformed/oversized PNGs.
      - [x] Operator smoke harnesses exist for live image protocols
            (`nix develop -c make smoke-image-live PROTO=... FRAMES=N`) and
            Windows ConPTY (`nix develop -c make smoke-conpty-win32`);
            compile/headless harness checks are wired. Use
            `docs/runbooks/phase1-5-live-evidence.md` for protocol recapture
            and the remaining Windows ConPTY live smoke gate.
      - [x] Sixel terminal evidence via hosted Windows Terminal run
            `28982641529`.
      - [x] iTerm2 terminal evidence via hosted macOS iTerm2 run
            `28986249841`.

## Phase 2 backlog - architecture and applications

These are intentionally deferred. They become easier and less churn-heavy once
Phase 1.5 stabilizes widget result shapes, stylesheet application, image
capabilities, and grapheme-aware editing.

### Architecture & interaction

- [ ] **Elm-style component model** - optional layer over immediate mode, not a
      replacement. The repo already has the smaller `TimuiApp`/`timui_run`
      runner; pending work is a fuller `Model` / `Update` / `View` plus
      `Cmd`/`Sub` layer for async work. It should wrap stable widget APIs after
      Phase 1.5 rather than force a framework shape while widgets are still
      moving.
- [ ] **Focus traversal / tab order** - explicit focus graph, scoped focus
      groups, reverse traversal, disabled/hidden widgets, and deterministic
      behavior across panels, dialogs, menus, comboboxes, and split panes.
- [ ] **Keybinding maps / vim modes** - layered keymaps with mode names,
      conflict reporting, discoverability hooks, command dispatch, and example
      vi-like navigation. Build after focus semantics are explicit.
- [ ] **Richer mouse** - global pointer capture, drag lifecycle, hover intent,
      release-outside behavior, and scroll/drag routing. Phase 1.5 may add local
      splitter drag only; the general interaction contract belongs here.

### Applications

- [ ] **Matrix client** - large app proof, not core library scope. Needs HTTP
      long-poll/sync (`/sync`), rooms, threads, reactions, media, JSON/HTTP
      dependencies, async model stress, and E2E crypto (Olm/Megolm) from vetted
      libraries only. Write research docs and dependency/licensing notes before
      implementation.
- [ ] **SimpleX Chat client** - heavier than Matrix from a protocol/provenance
      standpoint. Research the SimpleX reference client, queue model, transport,
      and crypto boundaries first. Crypto comes only from vetted libraries.
- [ ] **Pi-style agent harness with Chez Scheme** - TUI over an agent loop where
      the agent rewrites Scheme. Scope a spike first: embedded Chez vs subprocess
      bridge, transcript/tool-call view, diff/apply pane, and a safe persistence
      model.

## Later / unscheduled parity

- [ ] **Markdown viewer** - a reusable library widget, likely built from the
      code/rich-text and hyperlink machinery once stylesheets settle.
- [ ] **Devtools / inspector** - Textual-style tree/box/style inspector and
      event log. Useful, but it depends on a stable style/component story.
- [ ] **Hot reload** - useful for stylesheets and examples after the stylesheet
      parser exists.
- [ ] **Accessibility** - screen-reader hints and alternate descriptions. Needs
      a clearer semantic widget model first.
- [ ] **Broader snapshots** - snapshot testing exists (`drive/` + goldens);
      broaden it as new widgets/protocols land.
- [ ] **Full generated UAX #29 grapheme tables** - the Phase 1.5 foundation
      covers the terminal clusters timui currently needs; full Unicode table
      generation remains useful parity work.
- [ ] **OpenTUI inspiration sweep** - review OpenTUI
      (`opentui.com`, `github.com/anomalyco/opentui`) for ideas worth grafting
      into timui.h without importing its runtime shape. Likely candidates:
      focus/input ergonomics, scroll/code/diff widget polish, animation/timeline
      hooks, inspector/devtools affordances, and renderer clipping/alpha
      lessons. Non-goals: React/Solid bindings, TypeScript-first APIs, and a
      wholesale component-framework replacement for the C immediate-mode core.

## Tooling

- [x] **Man page** - `docs/timui.1.md` (Markdown) to roff via pandoc, wired as
      `make man` and `make install-man`.
