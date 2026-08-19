#!/usr/bin/env python3
"""Detect and advance the single authoritative OmniGrid milestone state."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROADMAP = ROOT / "docs" / "ROADMAP.md"
ACTIVE_FINDINGS = ROOT / "docs" / "ACTIVE_FINDINGS.md"
BACKUP_TOOL = ROOT / "tools" / "create_harness_backup.sh"
VALID_STATES = {"ACCEPTED", "REVIEW PENDING", "OPEN"}
MILESTONE_ID = re.compile(r"^M\d{2}$")


class StateError(RuntimeError):
    pass


@dataclass(frozen=True)
class Milestone:
    identifier: str
    state: str
    dependency: str
    contract: str
    line_index: int
    cells: tuple[str, str, str, str]


def parse_roadmap(text: str) -> tuple[list[str], list[Milestone]]:
    lines = text.splitlines(keepends=True)
    milestones: list[Milestone] = []
    seen: set[str] = set()

    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("|") or stripped.startswith("|---"):
            continue
        cells = tuple(cell.strip() for cell in stripped.strip("|").split("|"))
        if len(cells) != 4:
            continue
        match = re.match(r"^(M\d{2})\b", cells[0])
        if match is None:
            continue
        identifier = match.group(1)
        if identifier in seen:
            raise StateError(f"duplicate milestone row: {identifier}")
        if cells[1] not in VALID_STATES:
            raise StateError(f"invalid state for {identifier}: {cells[1]}")
        seen.add(identifier)
        milestones.append(Milestone(
            identifier=identifier,
            state=cells[1],
            dependency=cells[2],
            contract=cells[3].strip("`"),
            line_index=index,
            cells=cells,
        ))

    if not milestones:
        raise StateError("no milestone rows found")
    validate_sequence(milestones)
    return lines, milestones


def dependency_ids(milestone: Milestone) -> tuple[str, ...]:
    return tuple(re.findall(r"\bM\d{2}\b", milestone.dependency))


def validate_sequence(milestones: list[Milestone]) -> None:
    identifiers = {milestone.identifier for milestone in milestones}
    seen_nonaccepted = False
    review_pending = 0

    for milestone in milestones:
        if milestone.state == "ACCEPTED":
            if seen_nonaccepted:
                raise StateError(
                    f"accepted milestone appears after open work: {milestone.identifier}"
                )
        else:
            seen_nonaccepted = True
        if milestone.state == "REVIEW PENDING":
            review_pending += 1
        for dependency in dependency_ids(milestone):
            if dependency not in identifiers:
                raise StateError(
                    f"{milestone.identifier} references unknown dependency {dependency}"
                )

    if review_pending > 1:
        raise StateError("more than one milestone is REVIEW PENDING")


def current_milestone(milestones: list[Milestone]) -> Milestone | None:
    pending = [m for m in milestones if m.state == "REVIEW PENDING"]
    if pending:
        return pending[0]

    states = {m.identifier: m.state for m in milestones}
    for milestone in milestones:
        if milestone.state != "OPEN":
            continue
        if all(states[dependency] == "ACCEPTED"
               for dependency in dependency_ids(milestone)):
            return milestone

    if any(m.state == "OPEN" for m in milestones):
        raise StateError("open milestones remain but every dependency path is blocked")
    return None


def print_milestone(prefix: str, milestone: Milestone | None) -> None:
    if milestone is None:
        print(f"{prefix}_MILESTONE=COMPLETE")
        return
    print(f"{prefix}_MILESTONE={milestone.identifier}")
    print(f"{prefix}_STATE={milestone.state}")
    print(f"{prefix}_DEPENDENCY={milestone.dependency}")
    print(f"{prefix}_CONTRACT={milestone.contract}")


def atomic_write(path: Path, text: str) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent, prefix=f".{path.name}.", text=True
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, path.stat().st_mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def accept(identifier: str, dry_run: bool) -> None:
    if MILESTONE_ID.fullmatch(identifier) is None:
        raise StateError(f"invalid milestone id: {identifier}")
    text = ROADMAP.read_text(encoding="utf-8")
    lines, milestones = parse_roadmap(text)
    current = current_milestone(milestones)
    if current is None:
        raise StateError("all milestones are already accepted")
    if current.identifier != identifier:
        raise StateError(
            f"cannot accept {identifier}; current milestone is {current.identifier}"
        )

    cells = list(current.cells)
    cells[1] = "ACCEPTED"
    newline = "\n" if lines[current.line_index].endswith("\n") else ""
    lines[current.line_index] = "| " + " | ".join(cells) + " |" + newline
    updated = "".join(lines)
    _, updated_milestones = parse_roadmap(updated)
    next_milestone = current_milestone(updated_milestones)

    findings_original = ACTIVE_FINDINGS.read_text(encoding="utf-8")
    findings_updated = (
        "# Active review findings\n\n"
        "This file is a small live queue, not a historical review log. "
        "Only unresolved\nblockers for the currently selected milestone "
        "belong here.\n\nCurrent scope: **none**\n\n"
        f"{identifier} findings were resolved and accepted with "
        f"{identifier}. New findings are added\nonly when they remain "
        "unresolved at the end of a review cycle; finished items\nare "
        "removed rather than accumulated here.\n"
    )

    backup_output = "BACKUP=SKIPPED_DRY_RUN"
    if not dry_run:
        try:
            atomic_write(ROADMAP, updated)
            atomic_write(ACTIVE_FINDINGS, findings_updated)
            backup = subprocess.run(
                [str(BACKUP_TOOL), "--milestone", identifier,
                 "--loop-status", "PASS"],
                cwd=ROOT, capture_output=True, text=True, check=False,
            )
            if backup.returncode != 0:
                detail = backup.stderr.strip() or backup.stdout.strip()
                raise StateError(f"PASS backup failed: {detail}")
            backup_output = backup.stdout.strip()
        except (OSError, StateError) as exc:
            atomic_write(ROADMAP, text)
            atomic_write(ACTIVE_FINDINGS, findings_original)
            raise StateError(f"finalization rolled back: {exc}") from exc

    print(f"ACCEPTED_MILESTONE={identifier}")
    print(f"PREVIOUS_STATE={current.state}")
    print("NEW_STATE=ACCEPTED")
    print_milestone("NEXT", next_milestone)
    print(f"WRITE={'NO' if dry_run else 'YES'}")
    print(f"FINDINGS_CLEARED={'NO' if dry_run else 'YES'}")
    print(backup_output)


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--current", action="store_true")
    action.add_argument("--state", metavar="MXX")
    action.add_argument("--accept", metavar="MXX")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.dry_run and args.accept is None:
        parser.error("--dry-run is valid only with --accept")

    try:
        text = ROADMAP.read_text(encoding="utf-8")
        _, milestones = parse_roadmap(text)
        if args.current:
            print_milestone("CURRENT", current_milestone(milestones))
        elif args.state is not None:
            match = next((m for m in milestones if m.identifier == args.state), None)
            if match is None:
                raise StateError(f"unknown milestone: {args.state}")
            print(f"MILESTONE={match.identifier}")
            print(f"STATE={match.state}")
            print(f"DEPENDENCY={match.dependency}")
            print(f"CONTRACT={match.contract}")
        else:
            accept(args.accept, args.dry_run)
        return 0
    except (OSError, StateError) as exc:
        print(f"MILESTONE_STATE_ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
