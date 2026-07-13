#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


PASS_NEEDLE = "PASS conpty smoke: observed TIMUI_CONPTY_SMOKE"
REQUIRED_FILES = (
    "conpty-acceptance.json",
    "conpty-smoke.command.txt",
    "conpty-smoke.meta.txt",
    "evidence.md",
)


def fail(message):
    print(f"FAIL conpty evidence: {message}", file=sys.stderr)
    return 1


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ValueError(f"cannot read {path.name}: {exc}") from exc


def manifest_path(artifact_dir, value, field):
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field} must be a non-empty relative path")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"{field} must stay inside the artifact directory")
    return artifact_dir / path


def require_exact(mapping, key, expected):
    got = mapping.get(key)
    if got != expected:
        raise ValueError(f"{key} is {got!r}, expected {expected!r}")


def require_bool_true(mapping, key):
    got = mapping.get(key)
    if got is not True:
        raise ValueError(f"{key} must be JSON true")


def verify(artifact_dir, commit):
    artifact_dir = artifact_dir.resolve()
    if not artifact_dir.is_dir():
        raise ValueError(f"artifact dir does not exist: {artifact_dir}")

    for name in REQUIRED_FILES:
        if not (artifact_dir / name).is_file():
            raise ValueError(f"missing {name}")

    try:
        manifest = json.loads(read_text(artifact_dir / "conpty-acceptance.json"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"conpty-acceptance.json is not valid JSON: {exc}") from exc
    if not isinstance(manifest, dict):
        raise ValueError("conpty-acceptance.json must contain an object")

    require_exact(manifest, "commit", commit)
    require_bool_true(manifest, "passTokenPresent")
    require_bool_true(manifest, "accepted")
    status = manifest.get("status")
    if type(status) is not int or status != 0:
        raise ValueError(f"status is {status!r}, expected integer 0")

    stdout_path = manifest_path(artifact_dir, manifest.get("stdout"), "stdout")
    stderr_path = manifest_path(artifact_dir, manifest.get("stderr"), "stderr")
    status_path = manifest_path(artifact_dir, manifest.get("statusFile"), "statusFile")
    for path in (stdout_path, stderr_path, status_path):
        if not path.is_file():
            raise ValueError(f"missing manifest sidecar {path.name}")

    stdout = read_text(stdout_path)
    if PASS_NEEDLE not in stdout:
        raise ValueError(f"{stdout_path.name} does not contain the ConPTY PASS token")

    status_text = read_text(status_path).strip()
    if status_text != "0":
        raise ValueError(f"{status_path.name} is {status_text!r}, expected '0'")

    evidence = read_text(artifact_dir / "evidence.md")
    if commit not in evidence:
        raise ValueError("evidence.md does not record the expected commit")

    command = read_text(artifact_dir / "conpty-smoke.command.txt").strip()
    if not command:
        raise ValueError("conpty-smoke.command.txt is empty")
    if "smoke-conpty-win32" not in command:
        raise ValueError("conpty-smoke.command.txt does not run smoke-conpty-win32")

    meta = read_text(artifact_dir / "conpty-smoke.meta.txt")
    for needle in (
        f"commit={commit}",
        "os_env=Windows_NT",
        "status=0",
        "passTokenPresent=True",
        "accepted=True",
    ):
        if needle not in meta:
            raise ValueError(f"conpty-smoke.meta.txt does not contain {needle!r}")

    print(f"PASS conpty evidence: {artifact_dir} commit={commit}")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(
        description="Validate a hosted Windows ConPTY evidence artifact directory."
    )
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args(argv)

    try:
        return verify(args.artifact_dir, args.commit)
    except ValueError as exc:
        return fail(str(exc))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
