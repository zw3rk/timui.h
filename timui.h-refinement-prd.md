# timui.h Refinement PRD

## Purpose

Move timui.h from an impressive technical prototype into a trustworthy
production-ready C99 terminal UI library.

Core thesis:

> One header. Plain C. Modern terminal capabilities. No ncurses.

The next milestone is not more features. It is confidence.

------------------------------------------------------------------------

## 1. Release Engineering

### Requirements

Provide:

-   Public source repository
-   Tagged releases
-   Changelog
-   CI configuration
-   Test suite
-   Sanitizer configuration
-   Fuzz targets
-   Example applications

Every release should provide immutable artifacts:

    /timui.h
    /releases/<version>/timui.h
    /releases/<version>/timui.tar.gz
    /releases/<version>/SHA256SUMS

Include version, commit hash, and release date.

------------------------------------------------------------------------

## 2. Single Header Architecture

Keep the single-header user experience while maintaining modular
internals.

Recommended structure:

    src/
      core/
      renderer/
      input/
      widgets/
      images/
      platforms/

    include/
      timui.h

Generated release:

``` c
#define TIMUI_IMPLEMENTATION
#include "timui.h"
```

------------------------------------------------------------------------

## 3. Safer Initialization

Introduce:

``` c
TimuiConfig cfg = TIMUI_CONFIG_INIT;
```

or:

``` c
timui_config_init(&cfg);
```

Include:

-   struct size
-   API version
-   input/output descriptors
-   feature flags

Document descriptor ownership and restoration behaviour.

------------------------------------------------------------------------

## 4. Lifecycle and Error Handling

Replace ambiguous boolean failures with explicit results.

Example:

``` c
typedef enum {
    TIMUI_OK,
    TIMUI_QUIT,
    TIMUI_EOF,
    TIMUI_IO_ERROR,
    TIMUI_OUT_OF_MEMORY
} TimuiResult;
```

Document:

-   blocking behaviour
-   wakeup semantics
-   shutdown ordering
-   terminal restoration guarantees

------------------------------------------------------------------------

## 5. Threading Contract

Keep rendering single-threaded.

Workers communicate through messages.

Document:

-   message ownership
-   copying behaviour
-   queue size
-   overflow handling
-   ordering guarantees
-   shutdown races

Prefer a result enum over a boolean return.

------------------------------------------------------------------------

## 6. Capability Diagnostics

Terminal support depends on:

    application
     -> ssh
     -> tmux/zellij
     -> terminal emulator

Expose capability inspection:

``` c
TimuiCaps timui_caps(Timui *);
```

Provide diagnostics explaining why capabilities are enabled or disabled.

------------------------------------------------------------------------

## 7. Compatibility Matrix

Publish tested combinations:

-   Ghostty
-   Kitty
-   WezTerm
-   iTerm2
-   Windows Terminal
-   xterm
-   tmux
-   SSH sessions

Differentiate:

-   supported
-   tested
-   compile-tested
-   experimental
-   unsupported

------------------------------------------------------------------------

## 8. Unicode Contract

Document responsibility boundaries.

timui owns:

-   UTF-8 decoding
-   grapheme handling
-   terminal-cell width
-   cursor positioning

Terminal emulators own:

-   glyph rendering
-   fonts
-   shaping

Document:

-   Unicode version
-   width policy
-   emoji handling
-   combining marks
-   bidi limitations

------------------------------------------------------------------------

## 9. Image Handling

Image protocols require explicit resource limits.

Document:

-   maximum image bytes
-   maximum decoded pixels
-   cache limits
-   image lifetime
-   cleanup behaviour

Add fuzzing for:

-   image decoders
-   graphics protocols

------------------------------------------------------------------------

## 10. Rendering Ownership

Provide:

``` c
void timui_invalidate(Timui *);
void timui_full_redraw(Timui *);
```

Document:

-   external writes
-   subprocess output
-   terminal reset handling
-   suspend/resume behaviour

------------------------------------------------------------------------

## 11. Preserve the Main Differentiator

The strongest design choice is:

> Controlled immediate mode with explicit state ownership.

Keep APIs such as:

``` c
timui_checkbox(frame, id, rect, label, value);
```

and mutable helpers:

``` c
timui_checkbox_mut(frame, id, rect, label, &value);
```

Avoid hidden global widget state.

------------------------------------------------------------------------

## 12. Demonstration Applications

Lead with:

### Mini Commander

Shows:

-   panes
-   navigation
-   dialogs
-   classic terminal UI workflows

### Application UI

Shows:

-   tables
-   trees
-   forms
-   menus

### Modern Demo

Shows:

-   chat
-   images
-   async updates
-   hyperlinks

------------------------------------------------------------------------

## 13. Testing Evidence

Replace raw test counts with categorized reports:

    Unit tests
    Renderer snapshots
    Input parser tests
    Widget tests
    Protocol tests
    Fuzz targets

    ASAN
    UBSAN
    TSAN

    Linux
    macOS
    Windows compile tests

------------------------------------------------------------------------

## Version 0.3 Acceptance Criteria

-   Public repository works
-   Reproducible releases exist
-   CI is published
-   Sanitizers run
-   Fuzzing exists
-   Initialization API is stable
-   Lifecycle errors are documented
-   Threading rules are documented
-   Capability diagnostics exist
-   Compatibility matrix exists
-   Unicode policy exists
-   Image limits exist
-   Mini Commander demo exists

------------------------------------------------------------------------

## Final Direction

timui.h should become the library that a careful C developer trusts
enough to vendor.

The next milestone is not more capability.

The next milestone is confidence.
