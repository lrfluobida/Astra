# Astra 夜间战斗与基地防守 Implementation Plan

**Goal:** 在不猜建造区和机器人下一步的前提下，让 Astra 操控已观测己方武器，优先消灭直接威胁基地的机器人，并继续收割其他机器人以提高 PVE 生存分与击杀分。

**Architecture:** 新增纯函数战斗规划器，为每座可用武器计算合法目标与收益，再穷举最多三座武器和三个角色的邻接匹配。战斗候选与返航候选一起交给现有 `arbitrate`，攻击优先占用操作者，未操炮角色继续回防。

**Tech Stack:** C++17、现有协议与动作裁决层、合成战斗场景、HTTP 与 1300 回合回放。

## 范围约束

- 只操控请求中已观测、存活且字段完整的己方武器；不从地图图片猜建造坐标。
- `targetTeam` 明确等于我方阵营时提高生存威胁权重；其他存活机器人仍是 PVE 得分目标，不作排除。
- `targetTeam` 缺失时按普通击杀价值和基地距离参与选择，保留对官方样例缺字段的兼容性。
- 火箭使用文档明确的中心 20、周围 8 格 10 点溅射并允许重叠；电磁炮只对严格共线目标估算穿透；加特林只生成满足 90° 锥形的目标。
- 不预测机器人移动与攻击选择；收益只基于当前血量、类型、位置、目标阵营和己方基地距离。

## Task 1: 武器目标规划

**Model:** GPT-5.6 Sol (`gpt-5.6-sol`, high) 设计伤害与反例；Terra / medium 实现；Luna / low 跑常规测试。

**Files:** Create `src/combat.hpp`, `src/combat.cpp`, `tests/combat_test.cpp`; modify `scripts/build.sh`.

- [x] 写失败测试：白天不攻击；优先处理威胁我方的机器人且不遗漏其他 PVE 得分目标；火箭选择可覆盖群体的 3×3 落点；电磁炮选择能穿透共线目标的远端；字段缺失或无射程内目标返回空。
- [x] 为机器人建立确定性威胁权重：攻击我方、距基地更近、攻击力更高、可完成击杀时收益更高；未知 `targetTeam` 降权。
- [x] 实现三类武器目标生成，输出经过 `validate_attack` 可接受的 `CandidateAction`。
- [x] 运行战斗与动作测试。

## Task 2: 操作者联合分配与策略合并

**Model:** GPT-5.6 Sol (`gpt-5.6-sol`, high) 检查匹配反例；Terra / medium 实现；Luna / low 回归。

**Files:** Modify `src/combat.cpp`, `src/strategy.cpp`, `tests/combat_test.cpp`, `tests/strategy_test.cpp`.

- [x] 写失败测试：三座武器与三名角色取得最大总收益且不重复操作者；同一角色只能操控一座武器；未操炮角色继续返航。
- [x] 穷举最多三座武器到相邻存活角色的匹配，按总收益、攻击数、武器 ID、操作者 ID 确定性破同分。
- [x] 夜间将攻击候选置于返航动作之前并统一裁决；白天保持现有经济和任务策略。
- [x] 运行完整 C++ 单测。

## Task 3: 服务回归与推送

**Model:** GPT-5.6 Luna (`gpt-5.6-luna`, medium) 执行常规回归；只有机制失败才使用 Sol / high。

**Files:** Modify `tests/http_test.py`, `tests/replay_test.py`, `README.md`, this plan.

- [x] HTTP 场景证明夜间返回合法攻击，重复请求稳定，操作者不会同时移动。
- [x] 回放加入己方武器与针对我方的机器人，校验 attack 的 `controllerId`、目标数和角色占用。
- [x] 运行 debug、release、sanitize 全套；1300 回合 release 平均 0.193 ms、P95 0.250 ms、最大 4.347 ms。
- [x] 复查 UTF-8 与中文，提交并推送到 `codex/astra-design`。

## 完成标准

夜间存在已观测可用武器、邻接操作者和合法机器人目标时，HTTP 服务输出确定性攻击；直接威胁基地的机器人获得更高权重，其他机器人仍参与 PVE 击杀得分；所有攻击和角色动作经过统一裁决。建造区和机器人未来移动仍等待官方联调。
