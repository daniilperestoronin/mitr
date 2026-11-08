#!/usr/bin/env python3
"""Write compile_commands.json for clangd / clang-tidy -p."""

import json
import os
from pathlib import Path


def main() -> None:
    root = Path(os.environ.get("ROOT", os.getcwd())).resolve()
    cc = os.environ.get("CC", "cc")
    cflags = os.environ.get("CFLAGS", "")
    test_flags = os.environ.get("TEST_FLAGS", "")
    test_files = set(os.environ.get("TEST_FILES", "").split())
    objdir = Path(os.environ.get("OBJDIR", "build/obj"))
    files = os.environ.get("FILES", "").split()
    if not files:
        raise SystemExit("FILES is empty")

    entries = []
    for rel in files:
        src = root / rel
        obj = root / objdir / Path(rel).with_suffix(".o")
        flags = f"{cflags} {test_flags}" if rel in test_files else cflags
        command = f"{cc} {flags} -c {src} -o {obj}"
        entries.append(
            {
                "directory": str(root),
                "file": str(src),
                "command": command,
            }
        )

    out = root / "compile_commands.json"
    out.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {out} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
