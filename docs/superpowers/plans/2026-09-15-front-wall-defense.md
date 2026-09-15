# Front Wall Defense Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建造迎怪侧 10 格半墙，并保持两座远端火箭优先升级。

**Architecture:** `defense` 负责镜像一致的前墙选点，`strategy` 为墙工生成采石、移动和建墙动作，同时保留另一工人的远端升级与经济动作。

**Tech Stack:** C++17、现有 BFS 导航、动作裁决器和回放测试。

---

### Task 1: Front half-wall geometry

- [x] Test ten selected wall cells, ring membership, pressure-facing shape, and mirrored order.
- [x] Implement deterministic projection ranking in `src/defense.cpp`.
- [x] Run focused tests.

### Task 2: Stone collection and wall construction

- [x] Test that one worker collects stone while the other upgrades.
- [x] Test that a worker with enough stone builds an observed missing front wall.
- [x] Implement wall worker assignment, batch material collection, navigation, and item reservation.
- [x] Run focused tests.

### Task 3: Documentation and delivery

- [x] Update README, context, and ADR.
- [x] Verify UTF-8 rendering.
- [x] Run Debug, Release, Sanitizer, HTTP, 1300-round replay, and package checks.
- [x] Regenerate the archive, commit, and push.
