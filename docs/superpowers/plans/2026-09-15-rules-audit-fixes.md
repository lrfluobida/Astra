# Rules Audit Fixes Implementation Plan

> For agentic workers: use superpowers:executing-plans to implement this approved repair scope task by task.

**Goal:** Fix the six reproduced rules-audit defects without changing the three-rocket layout, rear-first upgrades, or front half-wall policy.

**Architecture:** Keep deterministic C++ strategy decisions. Share predicted damage between weapons, assign returning actors to distinct weapon interaction positions, and attach bounded task evidence from the validated session before invoking the strategy. Reuse observed footprints for navigation and validation.

**Tech Stack:** Existing C++17, jsoncpp, WSL build scripts and regression harness; no new runtime dependencies or model calls.

**Model allocation:** Main combat/economy implementation stays in this session. The bounded task-memory integration uses one `gpt-5.6-sol` worker; code review uses `gpt-6-astra`. During the competition, combat/economy remains deterministic C++; only task solving uses the platform-provided model. History retention adds no model calls.

- [x] Add failing combat regressions for shared rocket damage and station footprint direction; fix `src/combat.cpp` using existing navigation geometry and coordinated attack plans.
- [x] Add failing strategy regressions for weapon staffing across multiple applied movement rounds and unaffordable reconstruction; fix `src/strategy.cpp`, preserving active-task pioneers and shared resource constraints.
- [x] Add failing second-cell task interaction regressions; share task interaction geometry in `src/navigation.*` and use it in `src/strategy.cpp` and `src/actions.cpp`.
- [x] Add failing multi-command task evidence regressions; update `src/session.*`, `src/protocol.hpp`, `src/main.cpp`, and `src/task_solver.cpp` so session validation precedes planning and current-task evidence survives retries. Bound retained UTF-8 text and clear it on task/match changes.
- [x] Run focused tests after each fix, then the Debug/Release/Sanitizer suites and package verification. Update README with changed behavior and the actual scope of replay validation.
- [x] Regenerate `dist/Astra-CoreGeek.tar.gz` and review the diff.

**Delivery:** Commit and push the verified changes to the existing `lrfluobida/Astra` remote on `codex/astra-design`.

**Verification:** 73 C++ tests pass in Debug, Release, and Sanitizer builds, together with HTTP and 1300-request replay checks. The extracted archive builds with the SDK-style CMake layout and passes the direct HTTP task pipeline. Source/test UTF-8, LF, and existing Chinese lines are preserved.

Regression tests must exercise observed actions or prompts, including advancing positions and cooldowns where relevant. The 1300-request replay remains a protocol endurance test, not a game simulator.
