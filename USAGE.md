# USAGE — timui.h

All targets run inside the nix dev shell via the self-documenting Makefile:

    nix develop -c make          # print the target menu (default)
    nix develop -c make help     # same

## Common targets

| target        | action                                            |
|---------------|---------------------------------------------------|
| `build`       | compile all examples (single-header mode)         |
| `test`        | compile + run unit tests (`build/test_unit`)      |
| `run`         | build + run `examples/hello`                      |
| `run-<name>`  | build + run one example (e.g. `run-chat`, `run-editor`) |
| `check`       | `build` + `test` gate                             |
| `goldens`     | regenerate `tests/golden/*.txt` snapshots (Tier B) |
| `vt-test`     | unit tests + libvterm round-trip tests (Tier A; needs libvterm — Linux) |
| `amalgamate`  | regenerate the release header into `release/`     |
| `fmt`         | clang-format sources (if available)               |
| `clean`       | remove `build/` and `release/`                    |

## SQLite TUI (`sqlite_tui`)

A three-pane terminal SQLite browser (`examples/sqlite_tui.c`): a schema tree
(tables/views → columns), a scrollable results grid (fitted column widths with
ellipsis, horizontal + vertical scroll, row selection), and a multi-line SQL
editor with syntax highlighting. **Tab** cycles focus (tree → results → editor);
arrows navigate; **Enter** runs the query (**Shift+Enter** inserts a newline);
**F10** quits.

    nix develop -c make run-sqlite-tui DB=path/to.db   # interactive (default :memory:)
    nix develop -c make check-sqlite-tui               # unit-test the PURE layout helpers
    nix develop -c make smoke-sqlite-tui               # headless: fixture → SELECT → 1 frame

Direct invocation and the non-interactive (headless) path used by the smoke:

    ./build/sqlite_tui <db> [--query "<sql>"] [--frames N | --exit-after]
                            [--headless] [--cols C] [--rows R]

- `--query "<sql>"` runs one statement at startup and seeds the editor.
- `--exit-after` / `--frames N` / `--headless` render N frames (default 1)
  through a fake transport — **no tty needed** — and dump the final frame's cell
  grid to **stderr** (plus a `rows=N cols=M` summary), so it is CI-able.

The SQLite amalgamation (public domain, vendored under `tools/vendor/sqlite3/`)
is compiled once to `build/sqlite3.o` (`SQLITE_THREADSAFE=1`, lean OMIT flags)
and linked into the example. The pure column-fit + paging/scroll units live in
`examples/sqlite_table.h` and are unit-tested standalone (`tests/test_sqlite_table.c`).

## IRC client (`irc`)

A minimal RFC 1459 / 2812 IRC client (`examples/irc.c`): server + channel +
query buffers shown as **tabs**, a per-buffer **scrollback** ring with
timestamps + rich text (`*bold*` `_italic_` `` `code` `` + http links), the
active channel's **nick list** (`timui_table`), and a slash-command **composer**
— all in a 3-pane `timui_split` (tabs · scrollback + nicks · composer) inside
rounded borders, with a status line (nick · server · active buffer · state).

**Commands** (typed in the composer): `/connect <host> [port]` goes live from an
offline session (starts the network worker on demand), plus
`/join /part /msg /nick /me /quit`. **Keys:** `↑`/`↓` recall sent lines,
`Shift+←`/`→` switch channels (or click a tab), `Esc`/`F10` quit. The composer
keeps focus, so clicking a tab switches channel without taking away your typing.

    nix develop -c make run-irc                          # offline --demo (no network)
    nix develop -c make run-irc HOST=irc.libera.chat     # connect live (best-effort)
    nix develop -c make run-irc HOST=irc.libera.chat NICK=me CHAN='#timui'
    nix develop -c make check-irc                        # unit-test the PURE parser
    nix develop -c make smoke-irc                        # headless: canned transcript → 1 frame

Direct invocation and the non-interactive (headless) paths:

    ./build/irc                                          # offline demo (default when no --connect)
    ./build/irc --connect HOST [--port 6667] [--nick N] [--channel '#c']
    ./build/irc --demo   [--frames N]                    # built-in canned transcript, no network
    ./build/irc --replay FILE [--frames N]               # feed raw IRC lines from a file

- **Composer commands:** `/join #chan`, `/part [#chan]`, `/msg nick text`,
  `/nick newnick`, `/me action`, `/quit`; plain text becomes a PRIVMSG to the
  active buffer (locally echoed, as servers don't echo your own messages).
- **Scrollback:** mouse wheel / PgUp / PgDn scroll the active buffer; **F10** quits.
- **Networking is plaintext TCP only** (no TLS, no vendored deps, no crypto). A
  background worker thread owns the socket (`getaddrinfo`/`connect`/`poll`/`read`),
  auto-replies PING→PONG, reconnects with bounded backoff, and hands each parsed
  line to the UI via `timui_post`; the worker is joined **before** `timui_close`
  (W14). The pure parser (`examples/irc_proto.h`) is allocation-free and
  unit-tested (`tests/test_irc.c`); the message handler (`irc_feed`) is separable
  from the socket, so `--demo`/`--replay` drive the exact same model code as a
  live connection.

The composer is the submit-capable single-line `timui_input_field` (Enter sends);
`timui_text_area` has no submit event, so it isn't used for the send-on-Enter line.

## Widget & layout library (`gallery`)

The library ships a constraint **layout** engine (`timui_split`/`timui_grid` over
`TIMUI_LEN/PCT/FLEX/MIN/MAX`), **borders** (`timui_border`, 4 line styles →
inner rect) + `timui_lerp_rgb`, and widgets: **tabs** (`timui_tabs`), a **virtual
multi-column table** (`timui_table_ex`), a **scrollable tree** (`timui_tree_scroll`),
**charts/indicators** (`timui_barchart`/`timui_sparkline`/`timui_gauge`/`timui_meter`/
`timui_progress`/`timui_spinner`), and a syntax-highlighted **code viewer**
(`timui_code` + `timui_highlight`). `examples/gallery.c` shows them all on one screen.

    nix develop -c make run-gallery      # interactive showcase
    nix develop -c make smoke-gallery    # headless: render a frame + assert
    # per-widget pure-unit tests:
    nix develop -c make check-layout check-tabs check-chart check-syntax check-grid

## Visual testing

Two tiers validate the renderer (see `docs/visual-tests.md`): **Tier B**
golden cell-buffer snapshots (zero deps, runs in `make test`) and **Tier A**
libvterm round-trip — feeds the renderer's emitted escapes through a real VT
emulator and compares the grid (`make vt-test`, libvterm is Linux-only in
nixpkgs). Regenerate snapshots with `make goldens` whenever rendering
intentionally changes, then review the `git diff` of `tests/golden/`.

## Recording & headless driving

For diagnosing a rendering glitch, capture the **raw byte stream** — not a video.
It is exactly what the render verifier consumes.

| target          | action                                                            |
|-----------------|-------------------------------------------------------------------|
| `rec-<name>`    | record a real interactive session to `recordings/<name>.cast` (asciinema; you drive it, quit with F10/ESC) |
| `drive-<name>`  | run `<name>` **headless** in a pty, feed scripted keystrokes from `recordings/<name>.in` (raw byte file; missing → none), capture the output to `recordings/<name>.raw`, and render the final screen to `recordings/<name>.txt` |
| `accept`        | headless acceptance smoke — drives `editor` with `tests/drive/editor.in` and asserts the typed text renders (timing-dependent, so **not** part of `make check`) |

`recordings/` is git-ignored (artifacts); acceptance input scripts live under
`tests/drive/*.in` (checked in). Tools: `tools/pty_drive.c` (pty runner + input
injector) and `tools/vt_render.c` (replays a raw stream through a VT model and
prints the grid — `./build/vt_render recordings/<name>.raw`; add `--images` to
also parse Kitty-graphics APC sequences and report image transmits + placements,
the headless check for inline images). `--delay-ms` simulates typing speed;
`pty_drive` waits for the app's first output before sending input (so keystrokes
aren't echoed or flushed by raw-mode entry).

Example — replay a captured session through the render model, or grep a headless
run as an assertion:

    nix develop -c make rec-chat            # record; produces recordings/chat.cast
    asciinema play recordings/chat.cast     # watch it back
    ./build/vt_render recordings/chat.raw    # or reconstruct the grid from a drive

## GIF/PNG with Kitty images (`vt_gif`)

asciinema/agg/VHS can't capture Kitty-graphics images (they're terminal pixels).
`tools/vt_gif.c` rasterizes a capture to pixels — text + colour + composited
images — as a PNG (final frame) or an animated GIF.

| target / cmd | what |
|---|---|
| `make gif-chat-demo` | autoplay `chat --demo` → `recordings/chat-demo.gif` (headless, no screen recorder) |
| `make webp-chat-demo` | also `recordings/chat-demo.{mp4,webp}` (H.264 + gif2webp, far smaller than the GIF) |
| `make check-vt-gif-all` | all renderer checks (smoke · glyphs · CJK · emoji · output) |
| `make check-chat-text` | chat word-wrap + bidi + fenced-block layout unit tests |
| `make check-chat-highlight` | chat syntax-highlighter unit tests |
| `make gen-font-ttf`  | regenerate the subset TTF face (`tools/vendor/vt_font_ttf.h`, fonttools via nix) |

The `chat` example renders **multi-line messages** (word-wrapped to the pane),
**fenced code blocks** (` ```lang … ``` `) with syntax highlighting, Hebrew/Arabic
**RTL** (right-aligned, per-line), inline images indented to the text column, and a
**multi-line composer** (Shift+Enter inserts a newline where the Kitty keyboard
protocol is active, e.g. Ghostty; plain Enter sends). In `--demo` scripts, `\n` in a
`msg`/`say` line becomes a real newline, so a script can post a code block on one line.

`vt_gif` renders text with `stb_truetype` (bundled DejaVu subset → any glyph it
has, antialiased). Flags: `--cell-h N` / `--scale F` (size), `--system-fonts`
(chain the OS CJK fonts — macOS Hiragino/AppleSDGothicNeo), `--system-emoji`
(Apple Color Emoji), `--width N` (downscale), `--bit-depth N`, `--frames-dir DIR`
(PNG sequence → ffmpeg MP4/WebP). *(Bundled Twemoji/Unifont for flag-free CJK/emoji
are a follow-up — see `docs/research/vt-gif-v2-plan.md`.)*

    # manual pipeline for any timui app (needs a --timing sidecar for frame pacing)
    ./build/pty_drive --cols 90 --rows 22 --run-ms 30000 \
        --out cap.raw --timing cap.timing -- ./build/chat --demo examples/chat.demo < /dev/null
    ./build/vt_gif --cols 90 --rows 22 --fps 12 --system-fonts --system-emoji \
        --timing cap.timing --gif out.gif cap.raw
    ./build/vt_gif --cols 90 --rows 22 --png out.png cap.raw            # single final frame
    ./build/vt_gif --cols 90 --rows 22 --width 720 --frames-dir frames cap.raw   # -> ffmpeg mp4/webp

    # turn the chat into a recorded GIF by hand: --demo autoplays for a screen recorder
    ./build/chat --demo examples/chat.demo    # or `make run-chat-demo` / `rec-chat-demo`

See `docs/research/kitty-gif-renderer.md` for design, limits, and the Path-B
(libghostty) upgrade path.

## Debugging input (drag-drop / paste)

Set `TIMUI_TRACE=<file>` to append a raw-input trace — one line per `read()` and
per bracketed paste, with `ESC` shown as `\e` and other control bytes as `\xNN`:

    TIMUI_TRACE=/tmp/timui-trace.log nix develop -c make run-chat
    # …drag a file into the input, quit, then inspect:
    grep -E 'READ|PASTE' /tmp/timui-trace.log

This shows exactly what the terminal sends (e.g. a drag-drop may arrive as
`\e[200~<path>\e[201~`, possibly split across several reads).

## Single-header drop-in

Download the amalgamated release header from `https://timui.dev/timui.h`, or
use `www/timui.h` after `make www`.

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit.

## Split build

For repository development, compile `src/timui.c` once and include
`include/timui.h` elsewhere for declarations. Public consumers should prefer the
amalgamated release header.

## Feature macros

Implemented knobs: `TIMUI_IMPLEMENTATION`, `TIMUI_NO_THREADS`, and `TIMUI_API`.
Reserved compatibility no-ops: `TIMUI_NO_STDIO`, `TIMUI_NO_IMAGES`, and
`TIMUI_NO_UTF8_TABLES`.
