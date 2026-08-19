#!/usr/bin/env python3
"""Fail fast when the tracked OpenCode harness drifts from project policy."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AGENTS = ROOT / ".opencode" / "agents"
COMMAND = ROOT / ".opencode" / "commands" / "loop.md"
DEEPSEEK_MODEL = "openrouter/deepseek/deepseek-v4-flash-0731"


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
    for token in ("docs/ROADMAP.md", "REVIEW PENDING", "$ARGUMENTS",
                  "@deepseek-builder", "@deepseek-review-code",
                  "@deepseek-review-architecture"):
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

    all_harness = command + "\n" + "\n".join(agents.values())
    for stale in ("M03 Round 3", "M03_ROUND3_REVIEW_LOOP",
                  "SESSION_RESTART", "docs/STATUS.md", "sol-loop",
                  "sol-review-", "openrouter/openai/"):
        require(stale not in all_harness,
                f"stale harness reference remains: {stale}", failures)

    roadmap = read(ROOT / "docs" / "ROADMAP.md", failures)
    index = read(ROOT / "INDEX.plan", failures)
    require(re.search(r"\| M03 .*\| REVIEW PENDING \|", roadmap) is not None,
            "ROADMAP M03 state is not REVIEW PENDING", failures)
    require("M03 review pending" in index,
            "INDEX.plan does not mirror the M03 roadmap state", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: OpenCode harness structure and ownership rules are consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
