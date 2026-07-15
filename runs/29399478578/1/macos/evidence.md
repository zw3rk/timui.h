# macOS Hosted Visual Probe

## Metadata
- Commit: 702424879c5b45efa873ea86ead4eb9d5beedd7a
- Date UTC: 2026-07-15T08:04:24Z
- Runner OS: macOS
- Frames: 120
- GUI frames: 900
- TERM: dumb
- TERM_PROGRAM: unset
- Nix action outcome: skipped

## Build and stream diagnostics
- Build path: native make fallback (Nix unavailable, skipped, or install failed)
- image-smoke-build: exit 0
- iterm2-diagnostic: exit 0

## macOS screenshot sanity
- Terminal.app launch sanity: exit 0
- Terminal.app screenshot sanity: exit 0

## iTerm2 GUI attempt
- brew-install-iterm2: exit 0
- iTerm2 app: /Applications/iTerm.app
- iTerm2 first-launch remediation: exit 0

## iTerm2 Python API screenshot attempt
- iterm2-defaults-enable-api: exit 0
- iterm2-defaults-allow-inline-display: exit 0
- iterm2-defaults-allow-inline-display-selection: exit 0
- iterm2-defaults-allow-big-download: exit 0
- iterm2-defaults-allow-big-download-selection: exit 0
- iTerm2 API no-auth marker: exit 0
- iterm2-api-venv: exit 0
- iterm2-api-pip: exit 0
- iterm2-api-pyobjc: exit 0
- iTerm2 API upstream overlay: exit 0
- iTerm2 open: exit 0
- iTerm2 direct executable launch: skipped, open succeeded
- iTerm2 Python API screenshot: exit 1

## iTerm2 OS screenshot diagnostics
- iTerm2 window list: exit 0
- screencapture iTerm2 window after 12s: exit 0
- screencapture iTerm2 region after 12s: exit 0
- screencapture after 12s: exit 0
- screencapture after 24s: exit 0

## Outcome
- Accepted: manual inspection required.
- terminal-sanity.png only proves hosted macOS screenshot mechanics. It is not image-protocol evidence.
- Count this as iTerm2 live visual evidence only if iterm2-window-12s.png visibly shows the timui image smoke with PNG-backed image tiles, not placeholders. API, region, and full-screen captures are diagnostics when present.
- The typescript and OSC 1337 count are diagnostics only; they do not replace a visual screenshot or recording.
