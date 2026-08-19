---
description: Build or review exactly one current OmniGrid milestone
agent: deepseek-loop
subtask: false
---

Run one complete, bounded OmniGrid milestone loop: detect -> plan -> build ->
dual review -> persist state -> backup -> stop. Read `AGENTS.md` and
`docs/ROADMAP.md`, then use the state tool's exact current candidate. Read its
complete `docs/milestones/Mxx.md` contract and only the relevant references,
findings, source, tests and build files.

Live snapshot at command start:

```text
HEAD: !`git rev-parse HEAD`
BRANCH: !`git branch --show-current`
STATUS:
!`git status --short --branch`
CURRENT MILESTONE:
!`python3 tools/milestone_state.py --current`
UNSTAGED DIFF STAT:
!`git diff --stat`
STAGED DIFF STAT:
!`git diff --cached --stat`
HARNESS SELF-CHECK:
!`python3 tools/validate_opencode_harness.py`
SNAPSHOT FINGERPRINT:
!`tools/create_harness_backup.sh --fingerprint-only`
```

Additional operator constraint:

$ARGUMENTS

Do not start the following milestone. Emit one compact `MILESTONE_PLAN`, then
send an `OPEN` candidate to @deepseek-builder. A `REVIEW PENDING` candidate is
reviewed before further builder work. After every builder pass the primary
orchestrator refreshes Graphify once, then dispatches @deepseek-review-code and
@deepseek-review-architecture against the same snapshot. Findings go back to
the builder in bounded cycles.

On dual PASS, finalize exactly the selected M with
`python3 tools/milestone_state.py --accept Mxx`; that controlled transition
validates the resulting state and creates its PASS backup as one rollback-safe
operation. Report the next M and stop. A failed/blocked candidate is not
advanced; create its terminal backup and stop. Never stage, commit, push or
continue directly into the next milestone.
