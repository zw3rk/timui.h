# Release runbook

Public release procedure for timui.h.

## Preconditions

- Work is merged to `master` by fast-forward and the worktree is clean.
- `include/timui.h` has the intended `TIMUI_VERSION_STRING`.
- `CHANGELOG.md` has an entry for the version and no stale "opening shortly"
  language remains in website copy.
- Hosted visual evidence is current enough for the claims in
  [COMPATIBILITY.md](../COMPATIBILITY.md).

## Local gates

Run these from the repository root:

```sh
nix develop -c make test
nix develop -c make build
nix develop -c make release-check VERSION=0.2.0
nix develop -c make www
nix develop -c make check
nix develop -c make fuzz
nix develop -c make test-san SAN=address
nix develop -c make test-san SAN=undefined
nix develop -c make test-san SAN=thread
nix develop -c make vt-test
```

`make fuzz` runs the deterministic parser/image fuzz regression corpus. On
Linux it defaults to ASAN+UBSAN; on platforms where the sanitizer runtime is not
reliable, pass `FUZZ_SAN=address,undefined` only after proving it terminates.

## Artifacts

Build artifacts with the release version:

```sh
nix develop -c make release VERSION=0.2.0
```

The generated, ignored release payload is:

- `release/timui.h`
- `release/releases/0.2.0/timui.h`
- `release/releases/0.2.0/timui.tar.gz`
- `release/releases/0.2.0/MANIFEST`
- `release/releases/0.2.0/SHA256SUMS`

Verify the manifest records the intended version, commit, and UTC date; verify
`shasum -a 256 -c SHA256SUMS` passes inside the version directory.

## Tag and GitHub release

After CI is green on the release commit:

```sh
git tag -a v0.2.0 -m "timui.h 0.2.0"
git push github v0.2.0
gh release create v0.2.0 \
  release/releases/0.2.0/timui.h \
  release/releases/0.2.0/timui.tar.gz \
  release/releases/0.2.0/MANIFEST \
  release/releases/0.2.0/SHA256SUMS \
  --draft \
  --title "timui.h 0.2.0" \
  --notes-file CHANGELOG.md
```

Keep the GitHub release as a draft until the attached artifacts, checksums,
website header, and compatibility claims are rechecked. Do not retag a public
release; cut a new patch version instead.
