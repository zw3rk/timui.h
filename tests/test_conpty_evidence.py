#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PASS_NEEDLE = "PASS conpty smoke: observed TIMUI_CONPTY_SMOKE"


def write_artifact(root: Path, commit: str, *, manifest=None, stdout=None, status="0",
                   evidence_commit=None, command=None, meta=None):
    root.mkdir(parents=True, exist_ok=True)
    if manifest is None:
        manifest = {
            "commit": commit,
            "status": 0,
            "passTokenPresent": True,
            "accepted": True,
            "stdout": "conpty-smoke.stdout",
            "stderr": "conpty-smoke.stderr",
            "statusFile": "conpty-smoke.status",
        }
    (root / "conpty-acceptance.json").write_text(json.dumps(manifest), encoding="utf-8")
    (root / "conpty-smoke.stdout").write_text(stdout if stdout is not None else PASS_NEEDLE + "\n", encoding="utf-8")
    (root / "conpty-smoke.stderr").write_text("", encoding="utf-8")
    (root / "conpty-smoke.status").write_text(status + "\n", encoding="ascii")
    (root / "conpty-smoke.command.txt").write_text(
        command if command is not None else "make smoke-conpty-win32\n",
        encoding="utf-8",
    )
    if meta is None:
        meta = "\n".join([
            f"commit={commit}",
            "os_env=Windows_NT",
            "status=0",
            "passTokenPresent=True",
            "accepted=True",
            "",
        ])
    (root / "conpty-smoke.meta.txt").write_text(meta, encoding="utf-8")
    (root / "evidence.md").write_text(f"- Commit: {evidence_commit or commit}\n", encoding="utf-8")


def run_verify(repo: Path, artifact: Path, commit: str):
    return subprocess.run(
        [
            sys.executable,
            str(repo / "tools" / "verify_conpty_evidence.py"),
            "--artifact-dir",
            str(artifact),
            "--commit",
            commit,
        ],
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def expect_pass(name: str, proc):
    if proc.returncode != 0:
        raise AssertionError(f"{name}: expected pass, got {proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}")


def expect_fail(name: str, proc):
    if proc.returncode == 0:
        raise AssertionError(f"{name}: expected failure\nstdout={proc.stdout}\nstderr={proc.stderr}")


def main():
    repo = Path(__file__).resolve().parents[1]
    commit = os.environ.get("TIMUI_TEST_COMMIT")
    if commit is None:
        commit = "0123456789abcdef"
    elif not commit.strip():
        raise AssertionError("TIMUI_TEST_COMMIT must not be empty")
    tmp = Path(tempfile.mkdtemp(prefix="timui-conpty-evidence-"))
    try:
        valid = tmp / "valid"
        write_artifact(valid, commit)
        expect_pass("valid hosted artifact", run_verify(repo, valid, commit))

        missing_manifest = tmp / "missing-manifest"
        write_artifact(missing_manifest, commit)
        (missing_manifest / "conpty-acceptance.json").unlink()
        expect_fail("missing manifest", run_verify(repo, missing_manifest, commit))

        malformed = tmp / "malformed-json"
        write_artifact(malformed, commit)
        (malformed / "conpty-acceptance.json").write_text("{not json", encoding="utf-8")
        expect_fail("malformed json", run_verify(repo, malformed, commit))

        wrong_commit = tmp / "wrong-commit"
        write_artifact(wrong_commit, "badc0ffee")
        expect_fail("wrong commit", run_verify(repo, wrong_commit, commit))

        accepted_false = tmp / "accepted-false"
        write_artifact(accepted_false, commit, manifest={
            "commit": commit,
            "status": 0,
            "passTokenPresent": True,
            "accepted": False,
            "stdout": "conpty-smoke.stdout",
            "stderr": "conpty-smoke.stderr",
            "statusFile": "conpty-smoke.status",
        })
        expect_fail("accepted false", run_verify(repo, accepted_false, commit))

        pass_false = tmp / "pass-token-false"
        write_artifact(pass_false, commit, manifest={
            "commit": commit,
            "status": 0,
            "passTokenPresent": False,
            "accepted": True,
            "stdout": "conpty-smoke.stdout",
            "stderr": "conpty-smoke.stderr",
            "statusFile": "conpty-smoke.status",
        })
        expect_fail("pass token false", run_verify(repo, pass_false, commit))

        bad_status = tmp / "bad-status"
        write_artifact(bad_status, commit, status="2")
        expect_fail("status file nonzero", run_verify(repo, bad_status, commit))

        missing_stdout_token = tmp / "missing-stdout-token"
        write_artifact(missing_stdout_token, commit, stdout="cmd banner only\n")
        expect_fail("stdout missing pass token", run_verify(repo, missing_stdout_token, commit))

        evidence_mismatch = tmp / "evidence-mismatch"
        write_artifact(evidence_mismatch, commit, evidence_commit="feedface")
        expect_fail("evidence commit mismatch", run_verify(repo, evidence_mismatch, commit))

        path_escape = tmp / "path-escape"
        write_artifact(path_escape, commit, manifest={
            "commit": commit,
            "status": 0,
            "passTokenPresent": True,
            "accepted": True,
            "stdout": "../conpty-smoke.stdout",
            "stderr": "conpty-smoke.stderr",
            "statusFile": "conpty-smoke.status",
        })
        expect_fail("manifest path escape", run_verify(repo, path_escape, commit))

        wrong_command = tmp / "wrong-command"
        write_artifact(wrong_command, commit, command="make check-conpty-win32-smoke-compile\n")
        expect_fail("wrong command", run_verify(repo, wrong_command, commit))

        missing_windows_meta = tmp / "missing-windows-meta"
        write_artifact(missing_windows_meta, commit, meta="\n".join([
            f"commit={commit}",
            "status=0",
            "passTokenPresent=True",
            "accepted=True",
            "",
        ]))
        expect_fail("missing Windows metadata", run_verify(repo, missing_windows_meta, commit))
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    main()
