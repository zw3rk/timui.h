# Website product brief - timui.h

Date: 2026-07-07

This brief turns the current repository state into website direction. It is
based on a full source/docs/examples/test review plus focused subagent reviews
of the core API, examples, and build/test maturity. The goal is a website that
is a little tongue-in-cheek without overselling what the library can do today.

## Product in one line

`timui.h` is a single-header C99 immediate-mode TUI library for modern
terminals: DOS-era ergonomics, Ghostty/kitty-class terminal features, no
ncurses, no runtime stack.

Shorter:

> A single C99 header for modern terminal UIs. No ncurses. No runtime circus.

## The website thesis

The website should sell a very specific contradiction:

- It is old-school C, header-only, plain values, file descriptors, `make`.
- It targets modern terminals: truecolor, synchronized output, Kitty keyboard,
  SGR mouse, bracketed paste, OSC 8 hyperlinks, and Kitty graphics.
- It looks fondly at DOS/Turbo Vision/Midnight Commander, but it is not trying
  to recreate `ncurses` pain with a fresh coat of paint.
- It is serious engineering wrapped in a knowing joke: using a header-only C
  library for TUIs while Go, Rust, TypeScript, and Python eat CLI tooling is
  absurd in exactly the right way.

The tone should be dry and self-aware. Aim the jokes at legacy APIs, runtime
bloat, and terminal weirdness; never at users.

## Primary audience

- C and C++ programmers who want a small terminal UI layer.
- Systems programmers and single-binary CLI authors.
- Developers building file managers, debuggers, dashboards, monitors, database
  browsers, chat/log viewers, or local agent tools.
- People who miss the dense utility of DOS/Borland/Midnight Commander UIs.
- Developers who want a C ABI usable from Zig, Odin, Nim, D, Rust FFI, etc.
- People who do not want a Go/Rust/Node/Python runtime just to draw a listbox.

The website should not try to convert everyone. The stronger message is:
this is for people who look at a single header and think, "yes, thank you."

## What it is

`timui.h` is an immediate-mode terminal UI runtime:

- App code redraws every frame with `timui_begin` / widget calls / `timui_end`.
- Widgets are controlled by default: caller-owned state in, intent out.
- `_mut` wrappers exist for small tools and fast experiments.
- Rendering goes through a cell buffer and a truecolor diff renderer; unchanged
  frames emit nothing.
- Input is an incremental parser for legacy CSI, Kitty keyboard, SGR mouse,
  bracketed paste, focus events, text, and UTF-8.
- Terminal capability detection is conservative and deterministic, with safe
  fallback behavior for unknown terminals and multiplexers.
- Async work uses a single documented path: worker threads post messages with
  `timui_post`, while the UI thread owns rendering and the terminal.
- Tests can use a fake transport and snapshot/grid comparison without needing a
  real terminal.

Evidence: `include/timui.h:1-17`, `include/timui.h:176-205`,
`include/timui.h:208-330`, `include/timui.h:521-528`,
`include/timui.h:642-651`, `docs/DESIGN.md:22-81`,
`docs/THREADING.md:3-31`.

## Current product surface

Core:

- Header-only / stb-style usage via `#define TIMUI_IMPLEMENTATION`.
- Split development build via `src/timui.c` and included sections.
- C99 baseline, libc hard dependency, optional pthreads.
- POSIX raw-mode terminal backend.
- Win32 ConPTY is implemented behind `_WIN32`, runtime-probed, and
  compile-checked, but live Windows Terminal smoke evidence is still pending.

Rendering and terminal:

- Cell buffer, clipping, drawing primitives, panels, borders.
- Truecolor SGR, text attributes, cursor placement, synchronized output.
- OSC 8 hyperlinks.
- Kitty graphics image placement and clipped image drawing, iTerm2 inline
  images for unclipped PNG draws, and Sixel for raw RGBA images with a bounded
  exact-or-quantized palette, including cropped raw-RGBA draws; placeholder
  fallback outside supported image protocol paths.
- Minimal UTF-8 decode and display width, including CJK/fullwidth awareness.

Input and interaction:

- Legacy keyboard sequences and Kitty keyboard protocol.
- SGR mouse, wheel, click position, focus events.
- Bracketed paste and paste splitting across reads.
- Hot/active/focus state, keyboard activation, tab cycling.
- Programmatic focus and typed-text accessors.

Widgets:

- Label, panel, function bar.
- Button, checkbox, radio, message box.
- Input line, `input_field`, styled input field, text area.
- Listbox, table, tree, command palette.
- Menu bar and menu items.
- Scroll view, keymaps, clipboard helper.

Evidence: `README.md:8-24`, `include/timui.h:225-330`,
`include/timui.h:459-496`, `include/timui.h:617-660`,
`include/timui.h:699-751`, `include/timui.h:792-863`.

## Proof points for the site

Use proof, not just claims:

- `nix develop -c make test` currently passes with over 200 tests.
- CI runs `make check`, `make release-check`, golden staleness checks,
  libvterm round-trip tests, and address/undefined/thread sanitizers.
- `make build` compiles all examples in single-header mode.
- `make release-check` regenerates and compiles the amalgamated release header.
- Tier B visual tests compare cell-buffer golden snapshots.
- Tier A visual tests replay emitted terminal bytes through libvterm.
- `tools/vt_gif` can rasterize terminal captures to PNG/GIF/WebP/MP4 assets,
  including Kitty images, CJK, emoji, font variants, and output controls.

Evidence: `Makefile:68-83`, `Makefile:131-156`, `Makefile:161-248`,
`Makefile:289-336`, `.github/workflows/ci.yml:20-55`,
`docs/visual-tests.md:3-13`, `docs/visual-tests.md:60-85`.

Important copy cleanup: keep proof current. Prefer "over 200 tests" in planning
docs, or update any exact public count together with `make test`.

## Demo story

The website's first viewport should show the real product, not an abstract
landing page. Lead with the chat demo or a tight carousel of real terminal
screens.

Best hero media:

- `make webp-chat-demo` for `recordings/chat-demo.{webp,mp4}`.
- Fallback: `make gif-chat-demo` for `recordings/chat-demo.gif`.
- For a crisp static fallback, generate a PNG from `tools/vt_gif`.

Headline demo: `examples/chat.c`.

- Thread-safe background message path via `timui_post`.
- Scrollback, timestamps, message history, markdown-ish spans.
- OSC 8 hyperlinks and clickable links.
- Inline local PNGs through Kitty graphics, with fallback placeholder.
- CJK/emoji/wide glyph rendering.
- Scripted autoplay through `examples/chat.demo`.

Evidence: `examples/chat.c:1-18`, `examples/chat.c:38-40`,
`examples/chat.c:49-59`, `examples/chat.c:102-104`,
`examples/chat.c:999-1115`, `examples/chat.demo:1-5`,
`USAGE.md:59-79`.

Secondary demos:

- `file_manager`: read-only Midnight-Commander-style two-pane browser using
  menu bar, panels, tables, function bar, and modal viewer. Evidence:
  `examples/file_manager.c:1-13`.
- `todo`: model/view/update discipline in plain C with controlled widgets.
  Evidence: `examples/todo.c:1-16`.
- `procmon`: htop-lite process monitor showing the table widget and diff
  renderer under frequent updates. Evidence: `examples/procmon.c:1-15`.
- `mini_commander`: small DOS/MC skeleton in roughly one screen of C. Evidence:
  `examples/mini_commander.c:1-7`.

## Positioning against alternatives

Against ncurses:

- Not a curses wrapper.
- Not a retained object hierarchy.
- Not pretending the terminal is permanently stuck in 1988.
- Still respects fallback terminals, but optimizes for modern terminal reality.

Against lower-level terminal libraries:

- Not just raw ANSI or a cell grid.
- Includes frame lifecycle, focus, widgets, themes, input parsing, messages,
  testing hooks, and examples.

Against Go/Rust/TypeScript/Python TUI stacks:

- No runtime dependency.
- No package-manager ceremony for embedding.
- C ABI and header-only distribution.
- Fits single-binary systems tools and small utilities.

Website line:

> Modern terminal UI, in the language your build system already knows how to
> compile.

## Voice and copy bank

Use sparingly. The site should feel competent first, mischievous second.

Hero headline options:

- `timui.h`
- `Terminal UIs, in one C99 header.`
- `DOS energy. Modern terminal protocol. No ncurses.`
- `Immediate-mode TUI for people who still like knowing where their bytes go.`

Subheads:

- `A single-header C99 immediate-mode TUI for Ghostty, kitty, WezTerm,
  Alacritty, foot, Rio, and the boring terminals too.`
- `Build dense terminal tools with panels, inputs, tables, menus, images,
  hyperlinks, mouse input, and a diff renderer that does not repaint the world
  for sport.`
- `For when importing half the internet to draw a checkbox feels excessive.`

Punchlines:

- `No ncurses. You have suffered enough.`
- `Curse curses, not your users.`
- `A header file with opinions. Mostly about not linking ncurses.`
- `C99: because sometimes the smallest runtime is no runtime.`
- `The terminal is weird. timui.h is weird back.`
- `One header. Many escape sequences. Fewer regrets.`
- `Immediate mode, delayed midlife crisis.`

Button labels:

- `Watch the demo`
- `Read the header`
- `Run make`
- `Steal the hello world`
- `Browse the examples`

Avoid:

- Claiming it is "the future of CLIs."
- Pretending C is universally better than Go/Rust/TypeScript.
- Sounding hostile to users who prefer other stacks.
- Using "enterprise" language unless the page is intentionally parodying it.

## Recommended website structure

1. Hero: real terminal media, product name, one-sentence promise, two actions.
2. Tiny code sample: `#define TIMUI_IMPLEMENTATION`, config, begin/end loop,
   one or two widgets.
3. Demo gallery: chat, file manager, todo, procmon, mini commander.
4. Why this exists: immediate mode, C ownership, no retained widget tree, no
   ncurses.
5. Feature grid: terminal protocols, widgets, async messages, images/links,
   testing/rendering, single-header release.
6. Quality proof: over 200 tests, CI gates, sanitizers, libvterm, goldens,
   release-check.
7. Honest limitations: Windows smoke pending, terminal-dependent images/Shift+Enter,
   limited bidi/grapheme support, pre-1.0 API movement.
8. Get started: `nix develop -c make`, `make run-chat`, drop-in header pattern.

## Code snippet for the site

Keep the first snippet small. Show the loop shape and controlled result, not
every subsystem.

```c
#include <unistd.h>

#define TIMUI_IMPLEMENTATION
#include "timui.h"

int main(void) {
    TimuiConfig cfg = {0};
    Timui *ui = 0;
    bool enabled = true;

    cfg.title = "hello timui";
    cfg.input_fd = STDIN_FILENO;
    cfg.output_fd = STDOUT_FILENO;
    cfg.profile = TIMUI_PROFILE_AUTO;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE |
                TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme = TIMUI_THEME_DOS_BLUE;

    if (timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while (!timui_should_quit(ui)) {
        TimuiFrame *f = 0;
        TimuiRect root, body;

        if (!timui_begin(ui, &f)) break;
        root = timui_root(f);
        body = timui_panel_begin(f, TIMUI_ID("main"), root,
                                 TIMUI_STR_LIT("timui.h"),
                                 TIMUI_BORDER_DOUBLE);

        if (timui_button(f, TIMUI_ID("ok"),
                         TIMUI_RECT(body.x + 2, body.y + 2, 12, 1),
                         TIMUI_STR_LIT("Do thing")).clicked) {
            enabled = !enabled;
        }

        timui_checkbox_mut(f, TIMUI_ID("enabled"),
                           TIMUI_RECT(body.x + 2, body.y + 4, 18, 1),
                           TIMUI_STR_LIT("Enabled"), &enabled);

        timui_panel_end(f);
        if (timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);
        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
```

## Claims that are safe

- Single-header C99 immediate-mode TUI.
- No ncurses dependency.
- POSIX terminal backend works; Windows ConPTY is implemented, runtime-probed,
  and compile-checked, with live Windows smoke evidence still pending.
- Truecolor diff renderer and synchronized output support.
- Incremental input parser for legacy CSI, Kitty keyboard, SGR mouse, paste,
  focus, text, and UTF-8.
- Controlled widgets plus `_mut` convenience wrappers.
- Tables, trees, command palette, menus, text input, text area, panels, function
  bar, message boxes.
- Thread-safe `timui_post`; all other UI/frame/widget APIs are UI-thread only.
- Fake transport, snapshot/golden tests, libvterm round-trip verification.
- Kitty graphics support where the terminal supports it, iTerm2 inline images
  for unclipped PNG draws, and raw-RGBA Sixel including quantization and
  clipped/scaled raw draws; fallback placeholder elsewhere.

## Claims to avoid or qualify

- Do not say "works on Windows" yet. ConPTY is implemented, runtime-probed, and
  compile-checked, but the live Windows Terminal smoke run is still pending.
- Do not say "full Unicode" or "full bidi." The chat example has a useful
  Hebrew/Arabic approximation and CJK/emoji rendering support, but full UAX #9,
  grapheme clusters, ZWJ emoji, and skin-tone sequences remain future work.
- Do not say "images work everywhere." They are terminal-protocol-dependent
  (Kitty graphics, iTerm2 inline images, or raw-RGBA Sixel today), with a
  fallback placeholder. PNG-to-Sixel and live iTerm2/Sixel terminal evidence
  remain open.
- Do not imply `make check` alone is the whole project gate. CI also runs
  `release-check`, golden staleness checks, `vt-test`, and sanitizers.
- Do not claim a website build system exists. This repo has product docs and
  media tooling, not a site pipeline yet.
- Do not claim the API is 1.0 stable. Current version is `0.2.0`, and the
  changelog records breaking pre-1.0 changes.

Evidence: `include/timui.h:12-17`, `include/timui.h:843-863`,
`docs/gaps.md:44-54`, `docs/research/rtl-chat-layout.md:49-52`,
`USAGE.md:74-79`, `.github/workflows/ci.yml:20-55`.

## Critical review notes for website planning

- The codebase is materially more than a scaffold. The header, README,
  examples, tests, and CI all support that claim.
- The strongest story is "small C API over modern terminal machinery."
- The flagship demo is chat, but it is also the most terminal-dependent. The
  site should provide static/video fallback assets and make the demo reproducible
  through Makefile targets.
- The docs are not all equally fresh. `docs/backlog.md` still lists table/tree
  as gaps, while the header and examples show them implemented. `USAGE.md`
  still says bundled Twemoji/Unifont are a follow-up in one paragraph even
  though current Makefile checks exercise bundled emoji/CJK. Treat
  `include/timui.h`, `CHANGELOG.md`, `Makefile`, tests, and current command
  output as stronger evidence than backlog notes.
- The README test count is stale. Fix it or avoid hardcoding a number in public
  copy.
- `.PHONY` coverage in the Makefile is partial. Low product risk, but a cleanup
  pass before a public launch would be cheap.
- There is no dedicated `tests/regression/` namespace despite regression-style
  tests in the main suite. Do not advertise a named regression namespace unless
  one is added.

## First website build tasks

1. Generate hero media:
   `nix develop -c make webp-chat-demo`
2. Generate at least three static screenshots:
   `chat`, `file_manager`, `procmon` or `todo`.
3. Update public docs before launch:
   README test count, stale `USAGE.md` vt_gif note, stale backlog table/tree
   items.
4. Add a website Makefile target once the site stack is chosen.
5. Keep the first page asset-led: real terminal media, code snippet, examples,
   proof section, honest limitations.

## Summary positioning

`timui.h` should present as a compact, opinionated C library for people who want
real terminal applications without swallowing a framework runtime. The website
can be funny because the premise is funny. The engineering underneath should be
shown plainly: tests, CI, examples, protocol support, and generated media.

Suggested final hero:

> `timui.h`
>
> Terminal UIs, in one C99 header.
>
> DOS energy, modern terminal protocol, no ncurses. Build dense terminal tools
> with immediate-mode widgets, truecolor rendering, mouse input, hyperlinks,
> images, and a testable renderer - all from a header your C compiler already
> understands.
