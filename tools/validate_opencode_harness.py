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
DEPENDENCY_TOOL = ROOT / "tools" / "check_host_dependencies.py"
WORKCONTAINER_TOOL = ROOT / "tools" / "create_workcontainer_zip.sh"
THIRD_PARTY_MODULE = ROOT / "cmake" / "OmnigridThirdParty.cmake"


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
                  "python3 tools/milestone_state.py --verify-chain",
                  "python3 tools/milestone_state.py --mark-review-pending Mxx",
                  "python3 tools/check_host_dependencies.py --current",
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
        '"python3 tools/milestone_state.py --verify-chain": allow',
        '"python3 tools/milestone_state.py --mark-review-pending M*": allow',
        '"python3 tools/milestone_state.py --accept M*": allow',
        '"python3 tools/check_host_dependencies.py --current": allow',
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
        require("python3 tools/check_host_dependencies.py --current" in
                agents[name], f"{name} cannot run dependency preflight", failures)

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
    require(DEPENDENCY_TOOL.is_file(), "dependency preflight is missing", failures)
    require(WORKCONTAINER_TOOL.is_file(),
            "workcontainer packaging tool is missing", failures)
    require(THIRD_PARTY_MODULE.is_file(),
            "third-party CMake module is missing", failures)
    require(STATE_TOOL.stat().st_mode & 0o111 != 0 if STATE_TOOL.exists() else False,
            "milestone state tool is not executable", failures)
    require(BACKUP_TOOL.stat().st_mode & 0o111 != 0 if BACKUP_TOOL.exists() else False,
            "backup tool is not executable", failures)
    require(DEPENDENCY_TOOL.stat().st_mode & 0o111 != 0
            if DEPENDENCY_TOOL.exists() else False,
            "dependency preflight is not executable", failures)
    require(WORKCONTAINER_TOOL.stat().st_mode & 0o111 != 0
            if WORKCONTAINER_TOOL.exists() else False,
            "workcontainer packaging tool is not executable", failures)

    for shell_tool in (BACKUP_TOOL, WORKCONTAINER_TOOL):
        if not shell_tool.is_file():
            continue
        shell_syntax = subprocess.run(
            ["bash", "-n", str(shell_tool)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        require(shell_syntax.returncode == 0,
                f"{shell_tool.name} shell syntax failed: "
                f"{shell_syntax.stderr.strip()}",
                failures)

    if DEPENDENCY_TOOL.is_file():
        dependency = subprocess.run(
            [sys.executable, str(DEPENDENCY_TOOL), "--vendored-only"],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        require(dependency.returncode == 0 and
                "ENTT_STATUS=READY" in dependency.stdout and
                "OVERALL_STATUS=READY" in dependency.stdout,
                f"vendored dependency validation failed: "
                f"{dependency.stderr.strip() or dependency.stdout.strip()}",
                failures)

    cmake = read(ROOT / "CMakeLists.txt", failures)
    third_party_cmake = read(THIRD_PARTY_MODULE, failures)
    require("include(cmake/OmnigridThirdParty.cmake)" in cmake,
            "CMake does not load the third-party boundary", failures)
    require("EnTT::EnTT" in cmake and "test_third_party_entt" in cmake,
            "CMake does not compile the vendored EnTT smoke test", failures)
    require("add_library(EnTT::EnTT ALIAS omnigrid_entt)" in third_party_cmake,
            "third-party module does not expose EnTT::EnTT", failures)
    require("add_library(OmniGrid::RocksDB ALIAS omnigrid_rocksdb)" in
            third_party_cmake,
            "third-party module does not expose OmniGrid::RocksDB", failures)
    require(not (ROOT / "third_party" / "rocksdb").exists(),
            "RocksDB must remain host-provided, not vendored", failures)

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

        chain = subprocess.run(
            [sys.executable, str(STATE_TOOL), "--verify-chain"],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        require(chain.returncode == 0 and
                "FINAL_MILESTONE=COMPLETE" in chain.stdout,
                f"milestone chain validation failed: {chain.stderr.strip()}",
                failures)

        if current is not None and current.group(1) != "COMPLETE":
            identifier = current.group(1)
            state_match = re.search(
                r"(?m)^CURRENT_STATE=(ACCEPTED|REVIEW PENDING|OPEN)$",
                state.stdout,
            )
            require(state_match is not None,
                    "current milestone has no state", failures)
            current_state = state_match.group(1) if state_match else ""

            if current_state == "OPEN":
                mark = subprocess.run(
                    [sys.executable, str(STATE_TOOL),
                     "--mark-review-pending", identifier, "--dry-run"],
                    cwd=ROOT, capture_output=True, text=True, check=False,
                )
                require(mark.returncode == 0 and "WRITE=NO" in mark.stdout and
                        "NEW_STATE=REVIEW PENDING" in mark.stdout,
                        f"review-pending dry-run failed: {mark.stderr.strip()}",
                        failures)

                premature_accept = subprocess.run(
                    [sys.executable, str(STATE_TOOL), "--accept", identifier,
                     "--dry-run"],
                    cwd=ROOT, capture_output=True, text=True, check=False,
                )
                require(premature_accept.returncode != 0,
                        "OPEN milestone can be accepted without review-pending",
                        failures)
            elif current_state == "REVIEW PENDING":
                dry_accept = subprocess.run(
                    [sys.executable, str(STATE_TOOL), "--accept", identifier,
                     "--dry-run"],
                    cwd=ROOT, capture_output=True, text=True, check=False,
                )
                require(dry_accept.returncode == 0 and
                        "WRITE=NO" in dry_accept.stdout,
                        f"milestone accept dry-run failed: "
                        f"{dry_accept.stderr.strip()}", failures)

            chain_match = re.search(r"(?m)^ACCEPTANCE_CHAIN=([^\n]+)$",
                                    chain.stdout)
            if chain_match is not None:
                remaining = chain_match.group(1).split(">")
                if len(remaining) > 1:
                    skip = subprocess.run(
                        [sys.executable, str(STATE_TOOL),
                         "--mark-review-pending", remaining[1], "--dry-run"],
                        cwd=ROOT, capture_output=True, text=True, check=False,
                    )
                    require(skip.returncode != 0,
                            "milestone state tool permits dependency skipping",
                            failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: OpenCode harness structure and ownership rules are consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
