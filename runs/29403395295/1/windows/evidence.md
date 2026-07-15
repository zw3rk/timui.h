# Windows Hosted Visual Probe

## Metadata
- Commit: a9ab7e3ea72f11c73f9ea9a27b5a4d0ba2a45118
- Date UTC: 2026-07-15T09:09:57Z
- Runner OS: Windows
- Frames: 120
- GUI frames: 900
- OS env: Windows_NT
- Shell: PowerShell 7.6.3
- Windows build path: MSYS2 make is used here because the flake has Linux/Darwin systems only.
- Image smoke compiler: MSYS /usr/bin/gcc for POSIX headers.
- ConPTY compiler: UCRT64 /ucrt64/bin/gcc for native Win32 APIs.

- MSYS2 location: D:\a\_temp\msys64
- MSYS2 bash: D:\a\_temp\msys64\usr\bin\bash.exe
- MSYS2 root: /d/a/timui.h/timui.h

## Build and stream diagnostics
- image-smoke-build: exit 0
- conpty-smoke: exit 0
- ConPTY acceptance manifest: conpty-acceptance.json
- ConPTY PASS token present: True
- ConPTY accepted: True
- sixel-diagnostic: exit 0
- direct-sixel-diagnostic: exit 0

## Windows screenshot sanity
- cmd.exe launch sanity: started
- screen capture cmd-sanity.png: wrote PNG

## Windows Terminal GUI attempt
- wt-version: captured file metadata
- wt-version-command: exit 0
- ConPTY acceptance manifest: conpty-acceptance.json
- ConPTY PASS token present: True
- ConPTY accepted: True

### Direct Sixel control
- Direct Sixel Windows Terminal launch: started
- screen capture windows-terminal-direct-sixel-12s.png: wrote PNG

### timui Sixel smoke
- Windows Terminal launch: started
- screen capture windows-terminal-sixel-12s.png: wrote PNG

## Outcome
- ConPTY smoke counts only if conpty-smoke.stdout contains PASS conpty smoke: observed TIMUI_CONPTY_SMOKE.
- cmd-sanity.png only proves hosted Windows screenshot mechanics. It is not image-protocol evidence.
- The direct Sixel screenshot is a control: it proves whether hosted Windows Terminal renders Sixel at all, independent of timui.
- Sixel visual evidence counts only if a windows-terminal-sixel-*.png visibly shows the live image smoke in Windows Terminal with visible image tiles, not placeholders.
- The sixel typescript, direct.sixel, DCS counts, and DCS raster metrics are diagnostics only; they do not replace a visual screenshot or recording.
