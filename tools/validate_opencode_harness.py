#!/usr/bin/env python3
"""Fail fast when the tracked OpenCode harness drifts from project policy."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AGENTS = ROOT / ".opencode" / "agents"
COMMAND = ROOT / ".opencode" / "commands" / "loop.md"
DEEPSEEK_MODEL = "openrouter/deepseek/deepseek-v4-flash-0731"
STATE_TOOL = ROOT / "tools" / "milestone_state.py"
BACKUP_TOOL = ROOT / "tools" / "create_harness_backup.sh"


def read(path: Path, failures: list[str]) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        failures.append(f"cannot read {path.relative_to(ROOT)}: {exc}")
        return ""


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    config_text = read(ROOT / "opencode.json", failures)
    try:
        config = json.loads(config_text)
    except json.JSONDecodeError as exc:
        failures.append(f"opencode.json is invalid JSON: {exc}")
        config = {}

    require(config.get("default_agent") == "deepseek-loop",
            "default_agent must be deepseek-loop", failures)
    require(config.get("subagent_depth") == 1,
            "subagent_depth must be 1", failures)
    require(config.get("share") == "disabled",
            "share must be disabled", failures)
    require(config.get("instructions") == ["AGENTS.md"],
            "instructions must contain only AGENTS.md", failures)

    command = read(COMMAND, failures)
    require(re.search(r"(?m)^agent: deepseek-loop$", command) is not None,
            "/loop must use deepseek-loop", failures)
    require(re.search(r"(?m)^subtask: false$", command) is not None,
            "/loop must be a primary command", failures)
    for token in ("docs/ROADMAP.md", "MILESTONE_PLAN", "$ARGUMENTS",
                  "@deepseek-builder", "@deepseek-review-code",
                  "@deepseek-review-architecture",
                  "python3 tools/milestone_state.py --current",
                  "tools/create_harness_backup.sh --fingerprint-only"):
        require(token in command, f"/loop is missing {token}", failures)

    agent_names = (
        "deepseek-loop", "deepseek-builder", "deepseek-review-code",
        "deepseek-review-architecture",
    )
    agents = {name: read(AGENTS / f"{name}.md", failures)
              for name in agent_names}
    require(re.search(r"(?m)^mode: primary$", agents["deepseek-loop"])
            is not None, "deepseek-loop must be primary", failures)
    for name in agent_names[1:]:
        require(re.search(r"(?m)^mode: subagent$", agents[name]) is not None,
                f"{name} must be a subagent", failures)
        require(re.search(r"(?m)^\s*edit: deny$", agents[name]) is not None
                if name.startswith("deepseek-review") else True,
                f"{name} must deny edits", failures)

    for name, text in agents.items():
        require(f"model: {DEEPSEEK_MODEL}" in text,
                f"{name} does not use the required DeepSeek model", failures)

    primary = agents["deepseek-loop"]
    require(re.search(r"(?m)^\s*edit: deny$", primary) is not None,
            "deepseek-loop must deny direct edits", failures)
    for name in agent_names[1:]:
        require(re.search(rf"(?m)^\s{{4}}{re.escape(name)}: allow$", primary)
                is not None, f"deepseek-loop cannot dispatch {name}", failures)

    refresh_holders = [name for name, text in agents.items()
                       if '"graphify update .": allow' in text]
    require(refresh_holders == ["deepseek-loop"],
            "only deepseek-loop may allow graphify update .", failures)
    for name in agent_names[1:]:
        require('"graphify update' not in agents[name],
                f"{name} exposes Graphify refresh permission", failures)

    require('"./compile.sh": allow' in agents["deepseek-review-code"],
            "code reviewer must own compile.sh", failures)
    for name in ("deepseek-loop", "deepseek-builder",
                 "deepseek-review-architecture"):
        require('"./compile.sh": allow' not in agents[name],
                f"{name} must not allow compile.sh", failures)

    for permission in (
        '"python3 tools/milestone_state.py --current": allow',
        '"python3 tools/milestone_state.py --accept M*": allow',
        '"tools/create_harness_backup.sh --fingerprint-only": allow',
        '"tools/create_harness_backup.sh --milestone M* --loop-status *": allow',
    ):
        require(permission in primary,
                f"deepseek-loop is missing permission: {permission}", failures)
    for name in agent_names[1:]:
        require("milestone_state.py" not in agents[name],
                f"{name} may mutate milestone state", failures)
        require("create_harness_backup.sh" not in agents[name],
                f"{name} may create terminal backups", failures)

    all_harness = command + "\n" + "\n".join(agents.values())
    for stale in ("M03 Round 3", "M03_ROUND3_REVIEW_LOOP",
                  "SESSION_RESTART", "docs/STATUS.md", "sol-loop",
                  "sol-review-", "openrouter/openai/"):
        require(stale not in all_harness,
                f"stale harness reference remains: {stale}", failures)

    roadmap = read(ROOT / "docs" / "ROADMAP.md", failures)
    index = read(ROOT / "INDEX.plan", failures)
    for accepted in ("M01", "M02", "M03"):
        require(re.search(rf"\| {accepted} .*\| ACCEPTED \|", roadmap) is not None,
                f"ROADMAP baseline {accepted} is not ACCEPTED", failures)
    require(re.search(r"M\d{2} (?:accepted|review pending|open)", index,
                      re.IGNORECASE) is None,
            "INDEX.plan duplicates milestone status", failures)

    require(STATE_TOOL.is_file(), "milestone state tool is missing", failures)
    require(BACKUP_TOOL.is_file(), "backup tool is missing", failures)
    require(STATE_TOOL.stat().st_mode & 0o111 != 0 if STATE_TOOL.exists() else False,
            "milestone state tool is not executable", failures)
    require(BACKUP_TOOL.stat().st_mode & 0o111 != 0 if BACKUP_TOOL.exists() else False,
            "backup tool is not executable", failures)

    if BACKUP_TOOL.is_file():
        backup_syntax = subprocess.run(
            ["bash", "-n", str(BACKUP_TOOL)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        require(backup_syntax.returncode == 0,
                f"backup tool shell syntax failed: {backup_syntax.stderr.strip()}",
                failures)

    if STATE_TOOL.is_file():
        state = subprocess.run(
            [sys.executable, str(STATE_TOOL), "--current"],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        require(state.returncode == 0,
                f"milestone state validation failed: {state.stderr.strip()}", failures)
        current = re.search(r"(?m)^CURRENT_MILESTONE=(M\d{2}|COMPLETE)$",
                            state.stdout)
        require(current is not None,
                "milestone state tool returned no current milestone", failures)
        if current is not None and current.group(1) != "COMPLETE":
            dry_run = subprocess.run(
                [sys.executable, str(STATE_TOOL), "--accept", current.group(1),
                 "--dry-run"],
                cwd=ROOT, capture_output=True, text=True, check=False,
            )
            require(dry_run.returncode == 0 and "WRITE=NO" in dry_run.stdout,
                    f"milestone dry-run transition failed: {dry_run.stderr.strip()}",
                    failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: OpenCode harness structure and ownership rules are consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
