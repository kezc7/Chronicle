# AGENTS.md

## Project
Chronicle is the PrismaUI MMO-style chat/activity log for Skyrim SE/AE.

## Working rules
- Read `README.md`, open GitHub issues, existing tests, and the current implementation before editing.
- Treat HEAD as authoritative; issue text may lag behind the code.
- Preserve working behaviour and architecture. Make the smallest coherent change for the assigned issue.
- Target Skyrim AE 1.6.1170, SKSE64, Address Library, CommonLibSSE-NG and PrismaUI.
- Keep the mod DLL-driven: no ESP/ESL/ESM, Papyrus or MCM unless an issue explicitly requires and justifies it.
- Skyrim is authoritative for game state. Do not invent or simulate game events as a substitute for real runtime integration.
- Keep Skyrim/RE object access on the correct game thread and copy plain owned data across async/UI boundaries.
- Native config/state is authoritative where designed; browser preview/localStorage must not silently replace game persistence.
- Keep native↔JS bridges explicit and allow-listed. No generic eval/execute bridge.
- Reject stale callbacks/work across view, profile, world and load generations.
- UI/focus failures must fail safely and never leave stuck input, cursor, focus or altered window mode.
- Preserve UTF-8 safety for all game-originated text.
- Do not introduce a runtime dependency on `Skyrim-Suite`; shared contracts/assets are vendored/copied.

## Validation
- Run the relevant native and browser tests plus formatting/lint checks.
- Build/package the SKSE DLL when the environment supports it.
- Do not claim an in-game acceptance criterion passed unless it was actually tested in Skyrim.
- Document remaining in-game verification explicitly.

## Pull requests
- Work on a branch; do not commit directly to `main`.
- Inspect the linked issue and current HEAD before editing, then stay within that issue's scope.
- Preserve existing working behaviour and avoid unrelated refactors or generated/build junk.
- Run all relevant tests, builds, packaging, formatting and lint/diff checks that the repo supports.
- Never claim Skyrim, MO2, controller or runtime behaviour was tested when it was not.
- Use a PR title that describes the delivered change.
- Use `Closes #N` only when the issue is genuinely complete; otherwise use `Refs #N`.
- In the PR body summarise what changed, tests run, build/package result, technical compromises, unavailable APIs or test-only mocks, and every remaining in-game/runtime verification gate.
- Respond to review feedback with follow-up commits on the same PR rather than opening replacement PRs unless explicitly asked.
- Before asking for merge, re-read the issue acceptance criteria and verify each claimed item against the implementation.
