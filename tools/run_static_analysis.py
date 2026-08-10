#!/usr/bin/env python3
"""Run clang-tidy over project sources from CMake's compile_commands.json."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def find_clang_tidy() -> str | None:
    override = os.environ.get("CLANG_TIDY")
    if override:
        return override if shutil.which(override) or Path(override).is_file() else None
    for name in ["clang-tidy", "clang-tidy-20", "clang-tidy-19", "clang-tidy-18", "clang-tidy-17", "clang-tidy-16", "clang-tidy-15", "clang-tidy-14"]:
        path = shutil.which(name)
        if path:
            return path
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    args = parser.parse_args()

    root = args.root.resolve()
    build_dir = args.build_dir.resolve()
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        print(f"[static-analysis] missing compile database: {database}", file=sys.stderr)
        return 2

    clang_tidy = find_clang_tidy()
    if not clang_tidy:
        print("[static-analysis] clang-tidy not found.", file=sys.stderr)
        print("[static-analysis] Set CLANG_TIDY=/path/to/clang-tidy or install/provide it yourself.", file=sys.stderr)
        print("[static-analysis] This tool never installs OS packages.", file=sys.stderr)
        return 2

    commands = json.loads(database.read_text(encoding="utf-8"))
    sources: set[Path] = set()
    analyzed_roots = [(root / "src").resolve(), (root / "tests").resolve()]
    for entry in commands:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry.get("directory", root)) / path
        path = path.resolve()
        in_analyzed_root = False
        for analyzed_root in analyzed_roots:
            try:
                path.relative_to(analyzed_root)
                in_analyzed_root = True
                break
            except ValueError:
                pass
        if in_analyzed_root and "third_party" not in path.parts and path.suffix in {".cpp", ".cc", ".cxx"}:
            sources.add(path)

    if not sources:
        print("[static-analysis] no src/tests C++ translation units found", file=sys.stderr)
        return 2

    version = subprocess.run([clang_tidy, "--version"], text=True, capture_output=True)
    first_line = (version.stdout or version.stderr).splitlines()[0] if (version.stdout or version.stderr) else clang_tidy
    print(f"[static-analysis] {first_line}")
    print(f"[static-analysis] analyzing {len(sources)} translation units with {args.jobs} workers")

    def run_one(source: Path):
        proc = subprocess.run(
            [clang_tidy, "--quiet", "-p", str(build_dir), str(source)],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        # clang-tidy may still print aggregate counts and header-filter hints even
        # with --quiet. They describe suppressed diagnostics (mostly system/third-party
        # headers), not actionable project warnings. Keep real diagnostics, drop only
        # those known-noise summary lines.
        noise_patterns = (
            re.compile(r"^\d+ warnings generated\.$"),
            re.compile(r"^Suppressed \d+ warnings .*\.$"),
            re.compile(r"^Use -header-filter=.*$"),
        )
        filtered_lines = [
            line for line in proc.stdout.splitlines()
            if not any(pattern.match(line) for pattern in noise_patterns)
        ]
        output = "\n".join(filtered_lines).strip()
        return source, proc.returncode, output

    failures = 0
    outputs: list[tuple[str, str]] = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(run_one, source) for source in sorted(sources)]
        for future in as_completed(futures):
            source, code, output = future.result()
            rel = source.relative_to(root).as_posix()
            if output.strip():
                outputs.append((rel, output.rstrip()))
            if code != 0:
                failures += 1
                print(f"[static-analysis] FAIL {rel}")
            else:
                print(f"[static-analysis] OK   {rel}")

    for rel, output in sorted(outputs):
        print(f"\n--- clang-tidy: {rel} ---\n{output}")

    if failures:
        print(f"[static-analysis] FAILED: {failures} translation unit(s)", file=sys.stderr)
        return 1
    print("[static-analysis] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
