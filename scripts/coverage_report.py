#!/usr/bin/env python3
"""Run gcov over instrumented src/ objects and write build/coverage/summary.txt."""

import os
import shutil
import subprocess
import sys
from pathlib import Path


def gcov_cmd():
    if shutil.which("llvm-cov"):
        return ["llvm-cov", "gcov"]
    if shutil.which("gcov"):
        return ["gcov"]
    if shutil.which("xcrun"):
        return ["xcrun", "llvm-cov", "gcov"]
    return None


def parse_gcov_counts(path):
    instrumented = 0
    executed = 0
    source = None
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return None

    for line in lines:
        marker = "Source:"
        if marker in line and line.strip().endswith(line.split(marker, 1)[-1]):
            if source is None and "Graph:" not in line and "Data:" not in line:
                idx = line.find(marker)
                if idx != -1:
                    source = line[idx + len(marker) :].strip()
                    continue
        if ":" not in line:
            continue
        count = line.split(":", 1)[0].strip()
        if count in ("", "-") or count.startswith("-"):
            continue
        instrumented += 1
        if count != "#####" and not count.startswith("===="):
            executed += 1

    if not source:
        return None
    return source, executed, instrumented


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} OBJ_DIR OUT_DIR")

    obj_dir = Path(sys.argv[1]).resolve()
    out_dir = Path(sys.argv[2]).resolve()
    root = Path(os.environ.get("ROOT", Path.cwd())).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    cmd = gcov_cmd()
    if cmd is None:
        print("ERROR: gcov or llvm-cov is required for make coverage", file=sys.stderr)
        raise SystemExit(1)

    src_gcno = sorted((obj_dir / "src").glob("*.gcno"))
    if not src_gcno:
        print(
            f"ERROR: no src/ .gcno files under {obj_dir}; rebuild with COVERAGE=1",
            file=sys.stderr,
        )
        raise SystemExit(1)

    for gcno in src_gcno:
        src = root / "src" / (gcno.stem + ".c")
        if not src.is_file():
            continue
        subprocess.run(
            cmd + ["-p", "-o", str(gcno.parent), str(src.relative_to(root))],
            cwd=root,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for gcov in root.glob("*.gcov"):
            dest = out_dir / gcov.name
            if dest.exists():
                dest.unlink()
            gcov.rename(dest)

    per_file = {}
    for gcov in sorted(out_dir.glob("src#*.gcov")):
        parsed = parse_gcov_counts(gcov)
        if not parsed:
            continue
        source, executed, instrumented = parsed
        if instrumented == 0:
            continue
        per_file[source] = (executed, instrumented)

    if not per_file:
        print("ERROR: no src/ coverage records in gcov output", file=sys.stderr)
        raise SystemExit(1)

    total_exec = sum(e for e, _ in per_file.values())
    total_inst = sum(i for _, i in per_file.values())
    pct = (100.0 * total_exec / total_inst) if total_inst else 0.0

    lines = [
        "mitr gcov coverage (src/)",
        f"Files: {len(per_file)}",
        f"Lines: {total_exec}/{total_inst} ({pct:.1f}%)",
        "",
    ]
    for source in sorted(per_file):
        executed, instrumented = per_file[source]
        fpct = (100.0 * executed / instrumented) if instrumented else 0.0
        lines.append(f"  {fpct:6.1f}%  {executed}/{instrumented}  {source}")
    lines.append("")

    summary = out_dir / "summary.txt"
    summary.write_text("\n".join(lines), encoding="utf-8")
    sys.stdout.write(summary.read_text(encoding="utf-8"))
    print(f"Wrote {summary}")


if __name__ == "__main__":
    main()
