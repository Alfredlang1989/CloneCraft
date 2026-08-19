# OmniGrid OpenCode harness

Run `/loop` for exactly one current milestone candidate.

- `deepseek-loop` selects and orchestrates the milestone.
- `deepseek-builder` implements bounded reviewer findings.
- `deepseek-review-code` and `deepseek-review-architecture` review the same
  snapshot.

On dual PASS, `tools/milestone_state.py` accepts exactly the selected milestone
and `tools/create_harness_backup.sh` records the terminal snapshot. The loop
then stops; the next `/loop` selects the following eligible milestone.

The canonical project rules remain `AGENTS.md`, `docs/ARCHITECTURE.md`, the
roadmap and the selected milestone contract. This directory does not duplicate
those rules.
