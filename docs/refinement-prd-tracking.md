# timui.h refinement PRD tracking

Status for `timui.h-refinement-prd.md`. This file is the implementation
checklist for turning the PRD into artifact-backed work.

Legend: DONE = implemented and evidenced; PARTIAL = real support exists but the
PRD asks for a clearer contract, stronger checks, or a missing artifact; TODO =
not implemented; DEFERRED = intentionally outside this refinement pass.

| PRD area | Status | Evidence | Required close-out |
|---|---:|---|---|
| Release engineering | PARTIAL | `Makefile` has `amalgamate`, `release-check`, `www`; CI runs `make check`, `release-check`, goldens, `vt-test`, sanitizers; `CHANGELOG.md` exists. | Add generated versioned release artifacts under ignored `release/releases/<version>/`, a tarball, checksums, and metadata containing version, commit, and date. Add/check fuzz-oriented targets where practical. |
| Single-header architecture | DONE | `include/timui.h` is the public header; `src/timui.c` is a unity dev build; `tools/amalgamate.c` emits `release/timui.h`; `www` copies the fresh header. | Keep generated header self-contained and free of repo-relative `../src` includes. |
| Safer initialization | DONE | `TIMUI_CONFIG_INIT`, `timui_config_init`, `TIMUI_API_VERSION`, and config size/version validation are in `include/timui.h` and `src/timui_core.c`; tests cover defaults and mismatch rejection. `docs/API.md`, README, website, and examples now use the initializer. | Keep descriptor ownership/restoration docs current as lifecycle APIs evolve. |
| Lifecycle and error handling | PARTIAL | `timui_open`, low-level allocators, resize, termios, ConPTY return `TimuiResult`; docs describe begin/end loop. | Add an explicit-result begin API while preserving `timui_begin`; document blocking, quit, EOF/no-data, wakeup, shutdown, and restoration guarantees. |
| Threading contract | PARTIAL | `docs/THREADING.md`; `timui_post` copies into MPSC; TSAN CI; `TIMUI_NO_THREADS`. | Make queue ownership/copy/overflow/order limits explicit in docs; add result-returning post/emit helper if needed without breaking existing bool API. |
| Capability diagnostics | PARTIAL | `TimuiCaps`, `timui_caps_detect`, `timui_caps_image_protocol`, `timui_caps`; capability tests. | Add a deterministic diagnostics/report API or documented diagnostic helper explaining why caps are enabled/disabled, especially mux/image reductions. |
| Compatibility matrix | PARTIAL | `docs/TERMINAL_PROTOCOLS.md`, Phase 1.5 hosted evidence docs and runbooks. | Publish a compact compatibility matrix with supported/tested/compile-tested/experimental/unsupported states. |
| Unicode contract | PARTIAL | UTF-8 decode, width, grapheme helpers and tests; `docs/API.md` describes covered clusters. | Add a dedicated docs section for responsibility boundaries, Unicode policy/version, emoji/combining behavior, bidi limitations, and future UAX #29/UAX #9 gaps. |
| Image handling | PARTIAL | Protocol-neutral image API; Kitty/Sixel/iTerm2 emission; `TIMUI_NO_IMAGES`; many image protocol tests. | Document hard resource limits as public contract and add focused adversarial/fuzz-style image resource tests if gaps remain. |
| Rendering ownership | TODO | Internal `timui_renderer_reset`; resize resets renderer. | Add public `timui_invalidate` and `timui_full_redraw` with tests and docs for external writes, terminal reset, suspend/resume, and subprocess output. |
| Immediate-mode differentiator | DONE | Widgets use controlled results plus `_mut` wrappers; docs and examples follow caller-owned state. | Preserve this shape; avoid hidden global managers. |

## Notes

- Phase 1.5 feature work is largely present. This refinement pass should avoid
  redoing it and focus on contractual gaps, generated release artifacts, and
  docs that make the current guarantees auditable.
- Generated files under `release/` are ignored. Release checks should therefore
  regenerate and validate artifacts rather than expecting committed output.
