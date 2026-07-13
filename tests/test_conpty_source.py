#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "timui_conpty.c"


def main():
    src = SRC.read_text(encoding="utf-8")
    create_pc = src.index("api.create_pseudo_console")
    create_proc = src.index("CreateProcessW", create_pc)
    between = src[create_pc:create_proc]
    forbidden = (
        "conpty_close_handle_(&in_read)",
        "conpty_close_handle_(&out_write)",
    )
    for needle in forbidden:
        if needle in between:
            raise SystemExit(
                f"{SRC}: {needle} appears before CreateProcessW; "
                "ConPTY setup must keep those pipe ends alive until the child is created"
            )
    required_before_create_process = (
        "si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES",
        "si.StartupInfo.hStdInput = NULL",
        "si.StartupInfo.hStdOutput = NULL",
        "si.StartupInfo.hStdError = NULL",
    )
    for needle in required_before_create_process:
        if needle not in between:
            raise SystemExit(
                f"{SRC}: {needle} is missing before CreateProcessW; "
                "ConPTY children must not inherit redirected parent std handles"
            )


if __name__ == "__main__":
    main()
