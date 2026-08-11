#!/usr/bin/env python3
"""Repository-local Graphify harness for CloneCraft coding agents.

This helper never modifies operating-system packages.  It creates a pinned
Graphify virtual environment below .tools/ and drives a deterministic code-only
structural graph in graphify-out/.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import venv


ROOT = Path(__file__).resolve().parents[1]
TOOLS_ROOT = ROOT / ".tools" / "graphify"
VENV_ROOT = TOOLS_ROOT / "venv"
OUTPUT_ROOT = ROOT / "graphify-out"
GRAPH_JSON = OUTPUT_ROOT / "graph.json"
GRAPHIFY_PACKAGE = "graphifyy"
GRAPHIFY_VERSION = "0.9.29"


def fail(message: str, code: int = 2) -> "NoReturn":
    print(f"[graphify-harness] ERROR: {message}", file=sys.stderr)
    raise SystemExit(code)


def run(command: list[str], *, cwd: Path = ROOT, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("[graphify-harness] + " + " ".join(command))
    return subprocess.run(command, cwd=cwd, text=True, check=check)


def python_in_venv() -> Path:
    if os.name == "nt":
        return VENV_ROOT / "Scripts" / "python.exe"
    return VENV_ROOT / "bin" / "python"


def graphify_executable() -> Path:
    if os.name == "nt":
        return VENV_ROOT / "Scripts" / "graphify.exe"
    return VENV_ROOT / "bin" / "graphify"


def require_supported_python() -> None:
    if sys.version_info < (3, 10):
        fail(
            "Graphify requires Python 3.10 or newer. "
            "Do not install/upgrade system Python from an autonomous agent run."
        )


def installed_version() -> str | None:
    python = python_in_venv()
    if not python.is_file():
        return None
    probe = subprocess.run(
        [
            str(python),
            "-c",
            (
                "import importlib.metadata as m; "
                f"print(m.version('{GRAPHIFY_PACKAGE}'))"
            ),
        ],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if probe.returncode != 0:
        return None
    return probe.stdout.strip() or None


def bootstrap() -> None:
    require_supported_python()
    current = installed_version()
    if current == GRAPHIFY_VERSION and graphify_executable().is_file():
        print(f"[graphify-harness] Graphify {current} already available in {VENV_ROOT}")
        return

    TOOLS_ROOT.mkdir(parents=True, exist_ok=True)
    if not python_in_venv().is_file():
        print(f"[graphify-harness] Creating project-local virtual environment: {VENV_ROOT}")
        try:
            venv.EnvBuilder(with_pip=True, clear=False).create(VENV_ROOT)
        except Exception as exc:  # venv/ensurepip availability is host-specific
            fail(
                "Could not create the project-local Python virtual environment: "
                f"{exc}. Do not install OS packages automatically; document the missing host dependency."
            )

    python = python_in_venv()
    if not python.is_file():
        fail(f"Virtual environment Python was not created at {python}")

    run(
        [
            str(python),
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--upgrade",
            f"{GRAPHIFY_PACKAGE}=={GRAPHIFY_VERSION}",
        ]
    )

    current = installed_version()
    if current != GRAPHIFY_VERSION or not graphify_executable().is_file():
        fail(
            f"Expected Graphify {GRAPHIFY_VERSION}, found {current or 'nothing usable'} "
            f"under {VENV_ROOT}"
        )
    print(f"[graphify-harness] Ready: Graphify {current}")


def require_graphify() -> Path:
    executable = graphify_executable()
    current = installed_version()
    if current != GRAPHIFY_VERSION or not executable.is_file():
        fail(
            f"Pinned Graphify {GRAPHIFY_VERSION} is not ready under .tools/. "
            "Run: python3 tools/graphify_agent.py bootstrap"
        )
    return executable


def refresh() -> None:
    executable = require_graphify()
    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(executable),
            "extract",
            str(ROOT),
            "--code-only",
            "--out",
            str(OUTPUT_ROOT),
        ]
    )
    if not GRAPH_JSON.is_file():
        fail(f"Graphify completed without producing {GRAPH_JSON}")
    print(f"[graphify-harness] Structural graph refreshed: {GRAPH_JSON}")


def query(question: str) -> None:
    executable = require_graphify()
    if not GRAPH_JSON.is_file():
        fail(
            "No graphify-out/graph.json exists. Run: "
            "python3 tools/graphify_agent.py refresh"
        )
    run([str(executable), "query", question, "--graph", str(GRAPH_JSON)])


def status() -> None:
    current = installed_version()
    print(f"[graphify-harness] project root : {ROOT}")
    print(f"[graphify-harness] pinned       : {GRAPHIFY_PACKAGE}=={GRAPHIFY_VERSION}")
    print(f"[graphify-harness] installed    : {current or 'no project-local install'}")
    print(f"[graphify-harness] executable   : {graphify_executable()}")
    print(f"[graphify-harness] graph        : {'present' if GRAPH_JSON.is_file() else 'missing'}")
    print("[graphify-harness] policy       : code-only structural baseline; no implicit external LLM backend")

    git_probe = subprocess.run(
        ["git", "status", "--short"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if git_probe.returncode == 0:
        dirty = [line for line in git_probe.stdout.splitlines() if line.strip()]
        print(f"[graphify-harness] working tree : {'dirty (' + str(len(dirty)) + ' paths)' if dirty else 'clean'}")
    else:
        print("[graphify-harness] working tree : git status unavailable")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="CloneCraft project-local Graphify agent harness"
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="show local Graphify/graph state")
    sub.add_parser("bootstrap", help="create/update the pinned project-local Graphify venv")
    sub.add_parser("refresh", help="rebuild the local deterministic code-only structural graph")
    query_parser = sub.add_parser("query", help="query graphify-out/graph.json")
    query_parser.add_argument("question", help="architecture/codebase question")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "status":
        status()
    elif args.command == "bootstrap":
        bootstrap()
    elif args.command == "refresh":
        refresh()
    elif args.command == "query":
        query(args.question)
    else:  # argparse prevents this; keep a defensive branch for agents/tools.
        fail(f"Unknown command: {args.command}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
