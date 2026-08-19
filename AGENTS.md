# OmniGrid agent entrypoint

Keep this file short. It is only a bootstrap pointer, not another architecture
or roadmap document.

Every coding agent MUST:

1. read `docs/ARCHITECTURE.md` completely before acting;
2. treat that architecture as non-negotiable;
3. read the selected `docs/milestones/Mxx.md` completely;
4. read only domain references relevant to that milestone/current finding;
5. inspect current source/tests/git state instead of trusting old reports;
6. never install/modify OS packages or commit/push unless the user explicitly
   changes that policy.

Canonical documents:

- architecture: `docs/ARCHITECTURE.md`
- order/status: `docs/ROADMAP.md`
- milestone contracts: `docs/milestones/`
- unresolved current blockers: `docs/ACTIVE_FINDINGS.md`
- navigation/read order: `INDEX.plan`

Do not create another master plan, status ledger, session restart file or copied
architecture summary.
