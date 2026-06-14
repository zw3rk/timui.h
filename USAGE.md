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

## Single-header drop-in

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit.

## Split build

`src/timui_core.c` already defines `TIMUI_IMPLEMENTATION` and includes `include/timui.h`; compile it once and link the rest of your program against the declarations. The unit tests are built this way (see the `test` target).

## Feature macros

`TIMUI_IMPLEMENTATION`, `TIMUI_NO_STDIO`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, `TIMUI_NO_UTF8_TABLES`, `TIMUI_API`. See the header for details.
