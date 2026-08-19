---
description: Build or review exactly one current OmniGrid milestone
agent: deepseek-loop
subtask: false
---

Run one bounded OmniGrid milestone loop. Read `AGENTS.md`, then select exactly
one candidate from `docs/ROADMAP.md`: the first `REVIEW PENDING` milestone, or
otherwise the first `OPEN` milestone whose dependency is accepted. Read its
complete `docs/milestones/Mxx.md` contract and only the relevant references,
findings, source, tests and build files.

Live snapshot at command start:

```text
HEAD: !`git rev-parse HEAD`
BRANCH: !`git branch --show-current`
STATUS:
!`git status --short --branch`
UNSTAGED DIFF STAT:
!`git diff --stat`
STAGED DIFF STAT:
!`git diff --cached --stat`
HARNESS SELF-CHECK:
!`python3 tools/validate_opencode_harness.py`
```

Additional operator constraint:

$ARGUMENTS

Do not start the following milestone. A `REVIEW PENDING` candidate is reviewed
before any new builder work. An `OPEN` candidate receives one compact task
packet for @deepseek-builder. After every builder pass the primary orchestrator
refreshes Graphify once, then dispatches @deepseek-review-code and
@deepseek-review-architecture against the same snapshot. Findings go back to
the builder in bounded cycles. Stop on dual acceptance, a real blocker, or the
cycle limit; never stage, commit or push.
