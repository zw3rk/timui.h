# Compatibility matrix

Status vocabulary:

- **supported**: intended to work for the stated capability class.
- **tested**: exercised by unit, CI, or hosted-runner evidence named below.
- **compile-tested**: compiled for the platform/backend, without a live UI claim.
- **experimental**: usable enough to keep, but not broad enough to promise.
- **unsupported**: deliberately disabled or absent.

| Environment | Status | Core terminal UI | Images | Evidence |
|---|---:|---|---|---|
| Ghostty | supported | Modern POSIX terminal profile: truecolor, SGR mouse, bracketed paste, focus, sync output, OSC 8, Kitty-family keyboard/graphics when advertised. | Kitty protocol when not inside a multiplexer. | Capability unit tests (`tests/test_caps.c`); local/live evidence should record terminal version before release claims. |
| Kitty | supported | Same modern Kitty-family profile as Ghostty. | Kitty graphics preferred over Sixel/iTerm2 when present. | Protocol emission/unit tests and vt_gif renderer checks; collect native terminal evidence for release notes when needed. |
| WezTerm | supported | Modern POSIX terminal profile, but not treated as Kitty-family for keyboard passthrough under muxes. | Sixel when explicitly available; otherwise text placeholder. | Capability unit tests for non-Kitty modern mux behavior. |
| iTerm2 | tested | POSIX terminal profile, with conservative capability detection. | iTerm2 inline images for unclipped PNG-backed draws; clipped draws fall back to placeholder. | Hosted macOS iTerm2 evidence run `28986249841`; protocol tests in `tests/test_images_pty.c`. |
| Windows Terminal | tested | Windows ConPTY transport is implemented and smoked; a full Win32 terminal mode backend remains narrower than POSIX raw mode. | Sixel evidence accepted; ConPTY itself is not a graphics abstraction. | Hosted Windows Terminal Sixel run `28982641529`; hosted Windows ConPTY smoke run `29226547099`; MinGW compile seam in `make check`. |
| xterm / xterm-256color | supported | Safe fallback profile: 16/256-color text UI, legacy CSI keys, no Kitty-only assumptions. | Sixel only when forced/advertised by the caller; otherwise placeholder. | Unit tests for unknown and 256-color fallback capability detection. |
| tmux / screen / zellij | supported | Conservative multiplexer profile. SGR mouse remains enabled; Kitty keyboard/sync are kept only for known Kitty-family outer terminals. | Image protocols are stripped by default. | Capability unit tests for mux reduction and Kitty passthrough. |
| SSH sessions | supported | Works over ordinary fd-backed terminals; capabilities depend on the remote `TERM` and inherited outer-terminal variables. | Same terminal/mux rules as local sessions. | No network-specific code path; record host, terminal, mux, and `$TERM` in live evidence. |
| Win32 console without ConPTY | unsupported | No classic console backend. | Unsupported. | Non-Windows and unsupported backend tests return `TIMUI_ERR_UNSUPPORTED`. |

Do not promote an environment from **supported** to **tested** in public copy
without an artifact-backed command, CI job, screenshot/recording, or run id.
