# Vendored EnTT

OmniGrid vendors the official EnTT v3.16.0 modular header distribution so M04
and later milestones do not depend on an OS package or a network fetch. The
modular tree is intentionally used instead of the 3.2 MB amalgamated header so
code-indexing and review tools do not ingest one enormous translation unit.

- upstream tag: `v3.16.0`
- upstream commit: `b4e58bdd364ad72246c123a0c28538eab3252672`
- include: `<entt/entt.hpp>`
- CMake target: `EnTT::EnTT`
- license: MIT, preserved in `LICENSE`

`vendor.json` records provenance and pins the SHA-256 of `vendor.sha256`. The
harness preflight verifies all 88 licensed distribution files and rejects a
missing, added, modified or unlicensed vendored copy.
