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
| `amalgamate`  | regenerate the release header into `release/`     |
| `fmt`         | clang-format sources (if available)               |
| `clean`       | remove `build/` and `release/`                    |

## Single-header drop-in

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit.

## Split build

`src/timui_core.c` already defines `TIMUI_IMPLEMENTATION` and includes `include/timui.h`; compile it once and link the rest of your program against the declarations. The unit tests are built this way (see the `test` target).

## Feature macros

`TIMUI_IMPLEMENTATION`, `TIMUI_NO_STDIO`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, `TIMUI_NO_UTF8_TABLES`, `TIMUI_API`. See the header for details.
