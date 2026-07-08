---
type: Report
title: Hosted Visual Probe Follow-up
date: 2026-07-09
---

# Hosted Visual Probe Follow-up

## Landing State

- Branch: `hosted-visual-rerun-fix`
- Integrated local master: `4dac663`
  (`docs: record hosted visual probe results`)
- Hosted-probe implementation inspected: `4ea5b51`
  (`ci: refine hosted visual runner setup`)
- Remote push: `github master` pushed through `4dac663`
- GitHub workflow: `Hosted visual probes`
- Latest run inspected: `28979373796`
- Run URL: `https://github.com/zw3rk/timui.h/actions/runs/28979373796`

## Read First

- `.github/workflows/hosted-visual-probe.yml`
- `tools/ci/hosted_visual_macos.sh`
- `tools/ci/hosted_visual_windows.ps1`
- `docs/runbooks/phase1-5-live-evidence.md`
- `docs/handoff/2026-07-08-phase1-5-status.md`

## Accepted State

- The manual hosted-runner workflow now exists on remote `master`.
- The workflow uploads macOS and Windows artifacts even when the visual probe is
  inconclusive.
- Local validation before the runner attempts:
  - `bash -n tools/ci/hosted_visual_macos.sh`
  - `nix shell nixpkgs#actionlint -c actionlint .github/workflows/hosted-visual-probe.yml`
  - `nix shell nixpkgs#powershell -c pwsh ... Parser.ParseFile(...)`
  - `nix develop -c make check-conpty-win32-smoke-compile`

## Hosted Runs Tried

- `28978545945`: first run. macOS failed before probe at Nix install; Windows
  exposed MSYS2 first-run path pollution and Windows Terminal command quoting.
- `28978893252`: workflow completed. macOS built fallback binary but AppleScript
  failed; Windows Terminal opened an About dialog and the POSIX image smoke did
  not build natively.
- `28979182947`: workflow completed. Windows Terminal modal removed and ConPTY
  smoke runner hardened, but macOS AppleScript still failed and Windows builds
  still did not produce acceptable visual evidence.
- `28979373796`: latest inspected run. Both jobs completed and uploaded
  artifacts, but no Phase 1.5 live evidence was accepted.

## Latest Artifact Results

- Local downloaded artifacts:
  `artifacts/gh-runs/28979373796/hosted-visual-macos-iterm2/` and
  `artifacts/gh-runs/28979373796/hosted-visual-windows-terminal/`
  (`/artifacts/` is gitignored).
- macOS:
  - `image-smoke-build.status`: `0`
  - `osascript.status`: `1`
  - `osc1337-count.txt`: `0`
  - `iterm2-screen.png`: desktop/dock only; no iTerm2 timui smoke window.
  - Verdict: rejected/inconclusive for iTerm2 live image evidence.
- Windows:
  - `image-smoke-build.status`: `2`
  - `conpty-smoke.status`: `2`
  - `sixel-dcs-count.txt`: `0`
  - `windows-terminal-sixel.png`: runner log window only; no timui smoke.
  - `conpty-smoke.stdout`: native smoke runner compiles, then shows only
    `cmd.exe` banner/prompt.
  - `conpty-smoke.stderr`: `sentinel not observed`.
  - Verdict: rejected/inconclusive for both Sixel visual evidence and ConPTY
    sentinel evidence.

## Next Safe Move

Use a self-hosted or manual GUI machine for accepted Phase 1.5 visual evidence.
Hosted runners are useful diagnostics, but the current hosted macOS session does
not let us drive iTerm2 with AppleScript, and hosted Windows does not provide a
usable POSIX terminal-image build path for `examples/image_smoke.c`. For ConPTY,
debug the Windows smoke runner on an interactive Windows host where the
intermediate byte stream can be inspected live before promoting a CI result.
