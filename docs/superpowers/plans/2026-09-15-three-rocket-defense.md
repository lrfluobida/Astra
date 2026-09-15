# Three-Rocket Defense Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从基地位置自动推导合法建造区，首日建造三座火箭发射台，并按远端优先顺序购买和使用升级券。

**Architecture:** 新增独立 `defense` 模块处理基地几何、三火箭布局和升级目标排序；`strategy` 只负责把目标分配给角色并生成逐回合动作。布局以地图中心方向作为未知刷怪坐标时的压力代理，因此左上、右下两个半场会自动镜像。

**Tech Stack:** C++17、jsoncpp、现有 BFS 导航、现有动作裁决器。

**模型成本分工：** 设计、规则核对和最终策略评审使用高推理模型；确定性代码实现与常规重构使用中等成本编码模型；编译、回放、压测和日志归纳使用低成本模型。比赛运行时仅自进化任务调用平台模型，建造、升级、经济和战斗均由 C++ 完成。

---

### Task 1: Correct station geometry

**Files:**
- Modify: `src/navigation.cpp`
- Modify: `src/actions.cpp`
- Test: `tests/navigation_test.cpp`
- Test: `tests/actions_test.cpp`
- Test: `tests/protocol_test.cpp`

- [x] Write failing expectations for a top-left anchor whose 2×2 footprint extends downward.
- [x] Run the focused tests and confirm failure.
- [x] Correct occupancy, interaction ring, and movement collision geometry.
- [x] Run the focused tests and commit.

### Task 2: Derive build rings and rocket layout

**Files:**
- Create: `src/defense.hpp`
- Create: `src/defense.cpp`
- Create: `tests/defense_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `scripts/build.sh`

- [x] Test the 12-cell blue ring, 20-cell yellow ring, three distinct rocket positions, and mirrored layout.
- [x] Run the focused tests and confirm failure.
- [x] Implement deterministic ring and layout derivation.
- [x] Run the focused tests and commit.

### Task 3: Build three rockets

**Files:**
- Modify: `src/strategy.cpp`
- Test: `tests/strategy_test.cpp`

- [x] Test that two workers target distinct missing rocket sites and reserve 25 gold each.
- [x] Run the focused tests and confirm failure.
- [x] Assign missing sites before economy while preserving task and night priorities.
- [x] Run the focused tests and commit.

### Task 4: Upgrade far rockets first

**Files:**
- Modify: `src/actions.cpp`
- Modify: `src/strategy.cpp`
- Test: `tests/actions_test.cpp`
- Test: `tests/strategy_test.cpp`

- [x] Test legal shop purchases and adjacent voucher use.
- [x] Test that both far rockets reach each tier before the near rocket.
- [x] Run the focused tests and confirm failure.
- [x] Implement one-worker shop routing, purchasing, target routing, and voucher use.
- [x] Run the focused tests and commit.

### Task 5: Document and package

**Files:**
- Modify: `README.md`
- Modify: `CONTEXT.md`
- Modify: `docs/adr/0002-pve-performance-objective.md`
- Create: `docs/adr/0003-derived-three-rocket-layout.md`

- [x] Document the two-half position swap, ranking objective, geometry assumption, and fallback pressure direction.
- [x] Re-open all edited text as UTF-8 and verify Chinese text.
- [x] Run Debug, Release, Sanitizer, HTTP, 1300-round replay, and package checks.
- [ ] Regenerate `dist/Astra-CoreGeek.tar.gz`, commit, and push.
