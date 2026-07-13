# timui.h refinement PRD tracking

Status for `timui.h-refinement-prd.md`. This file is the implementation
checklist for turning the PRD into artifact-backed work.

Legend: DONE = implemented and evidenced; PARTIAL = real support exists but the
PRD asks for a clearer contract, stronger checks, or a missing artifact; TODO =
not implemented; DEFERRED = intentionally outside this refinement pass.

| PRD area | Status | Evidence | Required close-out |
|---|---:|---|---|
| Release engineering | PARTIAL | `Makefile` has `amalgamate`, `release`, `release-check`, `www`; `release` writes ignored `release/releases/<version>/timui.h`, `timui.tar.gz`, `MANIFEST`, and `SHA256SUMS`; `release-check` verifies checksums/metadata and standalone header compile. CI runs `make check`, `release-check`, goldens, `vt-test`, sanitizers; `CHANGELOG.md` exists. | Add/check fuzz-oriented targets and a documented tag/GitHub-release procedure. |
| Single-header architecture | DONE | `include/timui.h` is the public header; `src/timui.c` is a unity dev build; `tools/amalgamate.c` emits `release/timui.h`; `www` copies the fresh header. | Keep generated header self-contained and free of repo-relative `../src` includes. |
| Safer initialization | DONE | `TIMUI_CONFIG_INIT`, `timui_config_init`, `TIMUI_API_VERSION`, and config size/version validation are in `include/timui.h` and `src/timui_core.c`; tests cover defaults and mismatch rejection. `docs/API.md`, README, website, and examples now use the initializer. | Keep descriptor ownership/restoration docs current as lifecycle APIs evolve. |
| Lifecycle and error handling | DONE | `timui_begin_result` returns explicit `TimuiResult` values while `timui_begin` remains a bool wrapper; tests cover invalid args, quit/closed, EOF, runtime I/O error, and wrapper behavior. `docs/API.md` documents no-data redraws, 16 ms poll behavior, EOF, quit, wakeup latency, shutdown, and restoration. | Consider adding a wake fd only if future applications need sub-poll wakeup latency. |
| Threading contract | DONE | `timui_post_result` and `timui_emit_result` preserve the old bool wrappers; tests cover OK, invalid args, and OOM. `docs/THREADING.md` documents copying, ownership, FIFO ordering, allocation/overflow behavior, `TIMUI_NO_THREADS`, and close-vs-producer ordering. | Keep TSAN coverage in CI as the contract evolves. |
| Capability diagnostics | PARTIAL | `TimuiCaps`, `timui_caps_detect`, `timui_caps_image_protocol`, `timui_caps`; capability tests. | Add a deterministic diagnostics/report API or documented diagnostic helper explaining why caps are enabled/disabled, especially mux/image reductions. |
| Compatibility matrix | DONE | `docs/COMPATIBILITY.md` publishes supported/tested/compile-tested/experimental/unsupported states for Ghostty, Kitty, WezTerm, iTerm2, Windows Terminal, xterm, tmux/screen/zellij, SSH, and Win32 console. `check-refinement-docs` verifies the required vocabulary. | Promote entries only when artifact-backed evidence exists. |
| Unicode contract | DONE | `docs/UNICODE.md` documents the timui-vs-terminal ownership split, range-based Unicode policy, covered cluster behavior, bidi/shaping limits, and future UAX #29/UAX #9 gaps; `docs/API.md` links it. | Replace range tables with generated versioned Unicode data only if the implementation later does so. |
| Image handling | DONE | `docs/IMAGE_LIMITS.md` documents PNG/RGBA byte, pixel, dimension, and placement limits; public override macros live in `include/timui.h`; tests reject oversized PNG and RGBA inputs; `docs/API.md` and `docs/TERMINAL_PROTOCOLS.md` link the limits. | Fuzz harnesses remain tracked under release engineering because they are a cross-cutting release gate. |
| Rendering ownership | DONE | `timui_invalidate` resets cached terminal state, `timui_full_redraw` forces the next frame to repaint every cell, and unit tests cover both semantics. `docs/API.md` documents external writes, terminal reset, suspend/resume, and subprocess output. | Revisit image cache recovery if a future live terminal reset test proves Kitty/iTerm2 image data needs retransmission hooks. |
| Immediate-mode differentiator | DONE | Widgets use controlled results plus `_mut` wrappers; docs and examples follow caller-owned state. | Preserve this shape; avoid hidden global managers. |

## Notes

- Phase 1.5 feature work is largely present. This refinement pass should avoid
  redoing it and focus on contractual gaps, generated release artifacts, and
  docs that make the current guarantees auditable.
- Generated files under `release/` are ignored. Release checks should therefore
  regenerate and validate artifacts rather than expecting committed output.
