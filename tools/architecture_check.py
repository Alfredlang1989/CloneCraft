#!/usr/bin/env python3
"""Hard architecture gate for Omnigrid project-local includes.

This complements clang-tidy: clang-tidy analyzes C++ AST/semantics, while this
script enforces module dependency direction and reports include cycles. Rules
are data-driven in tools/architecture_rules.json.

Source-file inspection is parallel; results are merged in sorted source order so
reports remain deterministic.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inc"}


def load_rules(path: Path):
    data = json.loads(path.read_text(encoding="utf-8"))
    modules = data["modules"]
    modules.sort(key=lambda m: len(m["prefix"]), reverse=True)
    external_forbidden: dict[str, tuple[str, ...]] = {}
    for policy in data.get("externalIncludePolicies", []):
        prefixes = tuple(policy.get("forbiddenPrefixes", []))
        for module_name in policy.get("modules", []):
            external_forbidden[module_name] = external_forbidden.get(module_name, ()) + prefixes
    source_patterns = []
    for policy in data.get("sourcePatternPolicies", []):
        source_patterns.append(
            (
                set(policy.get("modules", [])),
                re.compile(policy["pattern"]),
                policy.get("message", "forbidden source pattern"),
            )
        )
    return (
        modules,
        tuple(data.get("ignoreIncludePrefixes", [])),
        int(data.get("warnCppLinesAbove", 0)),
        external_forbidden,
        source_patterns,
    )


def module_for(rel: str, modules):
    for module in modules:
        if rel.startswith(module["prefix"]):
            return module["name"]
    return None


def resolve_project_include(root: Path, source: Path, include: str) -> Path | None:
    candidates = [root / "src" / include, source.parent / include, root / include]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def find_cycle(graph: dict[str, set[str]]) -> list[str] | None:
    visiting: set[str] = set()
    visited: set[str] = set()
    stack: list[str] = []

    def dfs(node: str):
        visiting.add(node)
        stack.append(node)
        for nxt in sorted(graph.get(node, set())):
            if nxt == node:
                continue
            if nxt in visiting:
                idx = stack.index(nxt)
                return stack[idx:] + [nxt]
            if nxt not in visited:
                cycle = dfs(nxt)
                if cycle:
                    return cycle
        stack.pop()
        visiting.remove(node)
        visited.add(node)
        return None

    for node in sorted(graph):
        if node not in visited:
            cycle = dfs(node)
            if cycle:
                return cycle
    return None


@dataclass
class SourceResult:
    rel_source: str
    module: str | None
    line_count: int = 0
    errors: list[str] = field(default_factory=list)
    edges: list[tuple[str, str, int]] = field(default_factory=list)
    header_basename: str | None = None
    large_cpp: bool = False


def inspect_source(
    source: Path,
    root: Path,
    modules,
    ignored_prefixes: tuple[str, ...],
    warn_cpp_lines: int,
    external_forbidden: dict[str, tuple[str, ...]],
    source_patterns,
    allowed: dict[str, set[str]],
) -> SourceResult:
    rel_source = source.relative_to(root).as_posix()
    src_module = module_for(rel_source, modules)
    result = SourceResult(rel_source=rel_source, module=src_module)
    if src_module is None:
        result.errors.append(f"unclassified source: {rel_source}")
        return result

    try:
        text = source.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return result

    lines = text.splitlines()
    result.line_count = len(lines)
    result.large_cpp = (
        warn_cpp_lines > 0
        and source.suffix in {".cpp", ".cc", ".cxx"}
        and result.line_count > warn_cpp_lines
    )
    if source.suffix in {".h", ".hpp"}:
        result.header_basename = source.name

    for policy_modules, pattern, message in source_patterns:
        if src_module not in policy_modules:
            continue
        match = pattern.search(text)
        if match:
            line_no = text.count("\n", 0, match.start()) + 1
            result.errors.append(
                f"forbidden source pattern in {src_module}: {rel_source}:{line_no}: {message}"
            )

    for line_no, line in enumerate(lines, start=1):
        match = INCLUDE_RE.match(line)
        if not match:
            continue
        include = match.group(1)
        for prefix in external_forbidden.get(src_module, ()):
            if include.startswith(prefix):
                result.errors.append(
                    f"forbidden external include in {src_module}: "
                    f"{rel_source}:{line_no} includes {include}"
                )
                break
        if include.startswith(ignored_prefixes):
            continue
        target = resolve_project_include(root, source, include)
        if target is None:
            continue
        try:
            rel_target = target.relative_to(root).as_posix()
        except ValueError:
            continue
        dst_module = module_for(rel_target, modules)
        if dst_module is None:
            continue
        result.edges.append((rel_target, dst_module, line_no))
        if dst_module not in allowed.get(src_module, set()):
            result.errors.append(
                f"forbidden dependency {src_module} -> {dst_module}: "
                f"{rel_source}:{line_no} includes {include}"
            )

    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--rules", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    args = parser.parse_args()

    if args.jobs < 1:
        parser.error("--jobs must be >= 1")

    root = args.root.resolve()
    rules_path = (args.rules or root / "tools/architecture_rules.json").resolve()
    modules, ignored_prefixes, warn_cpp_lines, external_forbidden, source_patterns = load_rules(
        rules_path
    )
    allowed = {m["name"]: set(m["mayDependOn"]) for m in modules}

    src_root = root / "src"
    files = sorted(p for p in src_root.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES)

    def scan(source: Path) -> SourceResult:
        return inspect_source(
            source,
            root,
            modules,
            ignored_prefixes,
            warn_cpp_lines,
            external_forbidden,
            source_patterns,
            allowed,
        )

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        # executor.map preserves input order; deterministic report/error ordering.
        results = list(pool.map(scan, files))

    errors: list[str] = []
    graph: dict[str, set[str]] = defaultdict(set)
    file_graph: dict[str, set[str]] = defaultdict(set)
    edge_examples: dict[tuple[str, str], str] = {}
    loc_by_module: dict[str, int] = defaultdict(int)
    basename_sources: dict[str, list[str]] = defaultdict(list)
    large_cpp: list[tuple[str, int]] = []

    for result in results:
        errors.extend(result.errors)
        if result.module is None:
            continue
        loc_by_module[result.module] += result.line_count
        if result.header_basename:
            basename_sources[result.header_basename].append(result.rel_source)
        if result.large_cpp:
            large_cpp.append((result.rel_source, result.line_count))
        for rel_target, dst_module, line_no in result.edges:
            file_graph[result.rel_source].add(rel_target)
            graph[result.module].add(dst_module)
            edge_examples.setdefault(
                (result.module, dst_module), f"{result.rel_source}:{line_no} -> {rel_target}"
            )

    for name, paths in sorted(basename_sources.items()):
        if len(paths) > 1:
            errors.append(f"duplicate header basename '{name}': " + ", ".join(paths))

    cycle = find_cycle(graph)
    if cycle:
        details = []
        for a, b in zip(cycle, cycle[1:]):
            details.append(edge_examples.get((a, b), f"{a}->{b}"))
        errors.append("module dependency cycle: " + " -> ".join(cycle) + " | " + " ; ".join(details))

    file_cycle = find_cycle(file_graph)
    if file_cycle:
        errors.append("project include cycle: " + " -> ".join(file_cycle))

    report_lines = [
        "Omnigrid architecture report",
        f"Parallel source scan jobs: {args.jobs}",
        "",
        "Modules / source LOC:",
    ]
    for name in sorted(loc_by_module):
        report_lines.append(f"  {name:18s} {loc_by_module[name]:6d}")
    report_lines += ["", "Large translation units (refactor warning only):"]
    if large_cpp:
        for path, lines in sorted(large_cpp, key=lambda item: item[1], reverse=True):
            report_lines.append(f"  {path:55s} {lines:6d} lines")
    else:
        report_lines.append("  (none)")
    report_lines += ["", "Project include edges:"]
    for src in sorted(graph):
        deps = ", ".join(sorted(d for d in graph[src] if d != src)) or "(none)"
        report_lines.append(f"  {src:18s} -> {deps}")
    report_lines += ["", f"Result: {'FAIL' if errors else 'PASS'}"]
    if errors:
        report_lines.append("Errors:")
        report_lines.extend(f"  - {e}" for e in errors)
    report = "\n".join(report_lines) + "\n"
    print(report, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
