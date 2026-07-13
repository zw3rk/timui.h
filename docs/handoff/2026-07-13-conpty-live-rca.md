---
type: Report
title: Windows ConPTY Live Smoke RCA
date: 2026-07-13
---

# Windows ConPTY Live Smoke RCA

## Landing State

- Branch: `conpty-evidence-manifest-2026-07-13`
- Base commit: `ae58a8aea26fd1cde394642a6cdb3d842b666aed`
  (`docs: stamp conpty rca handoff`)
- RCA commit: `8098b11a58e6a5538ecc8590d0e748ab49517aec`
- Evidence manifest commit: `4105026d23c3a5e705334b7e5af30a9815ca3eb3`
- Evidence verifier commit: `72d6abd82fdc313badd9008ed84fba29fde0fe6b`
- Input/handle RCA commits:
  `ebf8826ad3dc68fa5db5db9cacc802c8312f1ac6`,
  `7ee14ce1e61fb57f83ce3d1200c30a39102ddf75`, and
  `44bb495b1f7937357f117b42563b50ba08c08e08`.
- Accepted hosted run: `29226547099` on branch
  `phase1-5-conpty-evidence-ci`, commit
  `44bb495b1f7937357f117b42563b50ba08c08e08`.
- Merge/push status: pushed to PR branch `phase1-5-conpty-evidence-ci`; do not
  push directly to `master`.
- Downloaded hosted artifact: `artifacts/gh-runs/28982641529/` (gitignored
  scratch, fetched with `gh run download 28982641529 --repo zw3rk/timui.h
  --name hosted-visual-windows-terminal`).
- Accepted hosted artifact: `artifacts/gh-runs/29226547099/` (gitignored
  scratch, fetched with `gh run download 29226547099 --repo zw3rk/timui.h
  --name hosted-visual-windows-terminal`).

## Read First

- `tools/conpty_smoke_win32.c`
- `docs/runbooks/phase1-5-live-evidence.md`
- `docs/research/conpty/REVIEW.md`
- `docs/handoff/2026-07-09-hosted-visual-probe.md`

## Accepted State

- Windows ConPTY backend implementation has accepted hosted smoke evidence for
  Phase 1.5. The accepted machine predicate is:
  `nix develop -c make verify-conpty-evidence ARTIFACT_DIR=artifacts/gh-runs/29226547099 COMMIT=44bb495b1f7937357f117b42563b50ba08c08e08`.
- Hosted run `29226547099` accepted: `conpty-acceptance.json` records status
  `0`, `passTokenPresent: true`, `accepted: true`, Windows runner metadata, and
  the exact `smoke-conpty-win32` command.
- Historical hosted run `28982641529` is useful RCA evidence, not accepted ConPTY
  evidence. `conpty-smoke.stdout` contains the initial `cmd.exe` banner and
  prompt, while `conpty-smoke.stderr` says:
  `conpty smoke: sentinel not observed; captured 166 bytes`.
- That artifact proves the smoke reached a live child shell and read initial
  ConPTY output. It does not prove that input written to the ConPTY was consumed
  by the shell.

## Change Made

- `tools/conpty_smoke_win32.c` now writes only `echo TIMUI_CONPTY_SMOKE` first,
  using LF newlines to match Microsoft's ConPTY input example. It writes `exit`
  only after the sentinel read attempt.
- If LF does not produce the sentinel, the smoke closes that session and retries
  the previous CRLF form in a fresh ConPTY session.
- Short writes are treated as failures. Failure output now includes attempts,
  last write count, and a bounded escaped excerpt of captured bytes.
- `make check-conpty-smoke-tool` compiles a portable helper test for the smoke
  script selection, exact-write helper, and sentinel matching. Both
  `make check` and `make check-conpty` run it.
- `tools/ci/hosted_visual_windows.ps1` now emits
  `conpty-acceptance.json`, `conpty-smoke.command.txt`, and
  `conpty-smoke.meta.txt` so hosted Windows runs have a machine-readable
  acceptance predicate in addition to stdout/stderr.
- `make verify-conpty-evidence ARTIFACT_DIR=... COMMIT=...` validates a
  downloaded hosted Windows artifact against that predicate. The fixture test
  rejects missing/malformed manifests, wrong commits, false acceptance flags,
  nonzero status files, missing PASS tokens, evidence.md mismatch, and unsafe
  manifest paths. It now also rejects compile-only commands and artifacts that
  do not record Windows host metadata.
- Follow-up RCA fixed two hosted-run defects: the smoke now submits the
  `cmd.exe` line with carriage return first, and `timui_conpty_open` sets
  `STARTF_USESTDHANDLES` with null std handles so a redirected parent process
  cannot leak stdout/stdin/stderr into the ConPTY child.

## Verification Already Run

- Red: `nix develop -c make check-conpty-smoke-tool` failed before
  `smoke_script_for_attempt` existed.
- Red: `nix develop -c make check-conpty-smoke-tool` failed again after the
  test was tightened to require split echo/exit scripts and exact-write
  behavior.
- Green: `nix develop -c make check-conpty-smoke-tool`.
- Green: `nix develop -c make check-conpty-win32-smoke-compile`.
- Green: `nix develop -c make check-hosted-visual-windows`.
- Green: `nix develop -c make check-conpty-evidence-artifacts`.
- Green: `nix develop -c make check-conpty`.
- Green: `nix develop -c make man`.
- Green: `nix develop -c make check`.
- Green hosted evidence: run `29226547099` completed on GitHub Actions at
  commit `44bb495b1f7937357f117b42563b50ba08c08e08`.
- Green local artifact verification:
  `nix develop -c make verify-conpty-evidence ARTIFACT_DIR=artifacts/gh-runs/29226547099 COMMIT=44bb495b1f7937357f117b42563b50ba08c08e08`.

## Blocker

- None for the Phase 1.5 ConPTY smoke gate. Future changes touching
  `src/timui_conpty.c`, `tools/conpty_smoke_win32.c`, or
  `tools/ci/hosted_visual_windows.ps1` should recapture or explicitly justify
  why the accepted run remains representative.

## Next Safe Move

- Keep the accepted run linked from the Phase 1.5 docs. If recapturing, trigger
  `Hosted visual probes`, download `hosted-visual-windows-terminal`, and run
  `nix develop -c make verify-conpty-evidence ARTIFACT_DIR=... COMMIT=...`
  before updating any accepted-state language.
