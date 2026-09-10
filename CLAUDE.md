# CLAUDE.md

## Role
Act as Chronicle's architecture/audit partner. Inspect the current repo before proposing or changing code; do not assume issue descriptions are current.

## Priorities
- Protect real Skyrim behaviour, focus/lifecycle safety, UTF-8 handling and native-authoritative persistence.
- Prefer supported CommonLibSSE-NG/SKSE/PrismaUI paths over low-level hooks or invented adapters.
- Keep bridges explicit/allow-listed and game-facing work on the game thread.
- Preserve independently installable DLL-only architecture and vendored Skyrim-Suite contracts.
- Do not substitute preview fixtures or mocks for live Skyrim functionality.
- Avoid broad rewrites when a focused issue-level change is enough.

## Reviews
When auditing a PR, trace the actual runtime path, identify blockers separately from polish, check stale-generation/focus/config failure paths, and verify tests cover the behaviour claimed. Never mark Skyrim-only acceptance as complete from portable tests alone.
