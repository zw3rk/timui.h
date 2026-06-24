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

## Debugging input (drag-drop / paste)

Set `TIMUI_TRACE=<file>` to append a raw-input trace — one line per `read()` and
per bracketed paste, with `ESC` shown as `\e` and other control bytes as `\xNN`:

    TIMUI_TRACE=/tmp/timui-trace.log nix develop -c make run-chat
    # …drag a file into the input, quit, then inspect:
    grep -E 'READ|PASTE' /tmp/timui-trace.log

This shows exactly what the terminal sends (e.g. a drag-drop may arrive as
`\e[200~<path>\e[201~`, possibly split across several reads).

## Single-header drop-in

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit.

## Split build

`src/timui_core.c` already defines `TIMUI_IMPLEMENTATION` and includes `include/timui.h`; compile it once and link the rest of your program against the declarations. The unit tests are built this way (see the `test` target).

## Feature macros

`TIMUI_IMPLEMENTATION`, `TIMUI_NO_STDIO`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, `TIMUI_NO_UTF8_TABLES`, `TIMUI_API`. See the header for details.
