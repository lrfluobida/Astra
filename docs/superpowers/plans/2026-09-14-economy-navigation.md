# Astra 经济与导航基线 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Astra 在白天能够确定性地选择矿区、避障移动、采矿、回售并让开拓者领取可用任务，夜晚停止远行并向基地回防。

**Architecture:** 新增纯函数网格导航器与经济候选生成器，通过现有 `arbitrate` 统一解决角色、位置和资源冲突。`BaselineStrategy` 只读取 `TurnObservation` 并产生候选；HTTP 主程序把裁决后的 `Decision` 交给 `AgentSession`，因此重复请求、额度和 pending 仍由会话层唯一管理。

**Tech Stack:** C++17、现有 nlohmann/json 与测试框架、Python HTTP/回放集成测试。

---

## 范围约束

- 仅使用请求内实际 `vendorShopList` 价格，不让 LLM参与寻路或经济计算。
- 建造区坐标尚未公开，策略不生成 build 候选。
- 敌方不可见移动和机器人下一步无法预测；路径只对当前可观察占用做保守规划。
- 回防目标为己方 2×2 基地周围可站立格，不走入基地占地。
- 首版不预测未来新闻价格，不宣称经济选择是全局最优。
- 基地来源是 `teamOur.roles` 中已识别的 `station`，其 `pos` 按官方接口文档解释为 2×2 左上角；矿和小贩来源是 `mapInfo.zones` 的 `neutralType`；任务位置与归属来源是 `teamOur.playerTasks`。任一所需观测缺失时跳过依赖它的候选，不从 `raw` 猜坐标。
- station 左上角约定来自《未来战争》v1.0 接口文档 1.3.1；在 `tests/protocol_test.cpp` 用合成 station `(10,24)` 断言占地展开为 `(10,24)`、`(11,24)`、`(10,25)`、`(11,25)`，正式平台仍需联调复核。

## Task 0: 同回合冲突请求保护

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium)。

**Files:** Modify `src/session.cpp`, `tests/session_test.cpp`.

- [x] **Step 1:** 写失败测试：同一回合语义相同的 JSON 返回缓存响应；同一回合载荷不同则返回 `{"roleCommandMap":{}}`，不重复消费 LLM、结果或回合状态。
- [x] **Step 2:** 在 `AgentSession::handle` 的同回合分支比较 `last_request_ == input`；相同返回 `last_response_`，不同返回保守空 Decision，且不覆盖缓存。
- [x] **Step 3:** 运行 `bash scripts/build.sh debug && ./build/astra_tests session`，预期全部通过；提交并推送。

## Task 1: 八方向保守寻路

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium)。仅场景数据补充可用 GPT-5.6 Luna (`gpt-5.6-luna`, low)。

**Files:** Create `src/navigation.hpp`, `src/navigation.cpp`, `tests/navigation_test.cpp`; modify `tests/protocol_test.cpp`, `scripts/build.sh`.

- [x] **Step 1:** 写失败测试：绕开中立点和单位；从 station 左上角展开的基地四格均阻挡；允许从两个相邻障碍物的对角间通过；不可达返回空；缺少 station 时基地目标为空；两个角色预留不同下一格且禁止位置交换。
- [x] **Step 2:** 定义 `NavigationGrid`、`NavigationReservations` 与 `next_step_toward_any(turn, actor, goals, reservations)`；预留对象显式保存已选下一格和 `起点→终点` 边，目标可传多个可站立格，返回最短路的第一步和完整距离。
- [x] **Step 3:** 用 BFS 搜索观测到的地图宽高和八邻域；固定邻居顺序保证重复输入得到相同结果。当前可见中立点、建筑、角色和机器人均阻挡，当前 actor 起点例外；预留终点和反向边阻挡后续角色。
- [x] **Step 4:** 单独生成某目标周围切比雪夫距离 1 的可站立交互格；不把矿区、小贩、任务点本身当成可站立终点。
- [x] **Step 5:** 运行 `bash scripts/build.sh debug` 与 `./build/astra_tests navigation`，提交并推送。

## Task 2: 经济和任务动作合法化

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium)。规则字段测试用 Luna / low；同一规则失败两次才升级 Sol / high。

**Files:** Modify `src/actions.cpp`; modify `tests/actions_test.cpp`. 数据直接使用现有 `ZoneObservation`、`TaskPointObservation`、`UnitObservation`，不增加重复协议模型。

- [x] **Step 1:** 写失败测试覆盖 collect、sell、acceptTask：`health` 缺失、零或负数都保守拒绝，另覆盖 round 70/71、目标/名称、切比雪夫邻接、矿物类型、背包重复项计数、任务来自 `teamOur.playerTasks`、`isValid` 与 `coldDownRounds`。
- [x] **Step 2:** 放行完整校验后的 collect：仅存活工人、白天、一个相邻矿区目标。
- [x] **Step 3:** 放行完整校验后的 sell：存活角色邻接 vendor，只允许 stone/iron/copper，`command.number` 表示一次批量出售数量，数量为正且背包足够；`reservation.items[sellingActorId][name]` 以出售角色作为库存 owner，必须与数量一致并进入共享预留。本动作的 `actionKey` 也等于 sellingActorId，但校验依据是 owner 语义而非命令键巧合。
- [x] **Step 4:** 放行完整校验后的 acceptTask：仅存活开拓者，当前无任务，邻接己方有效任务点且冷却为 0。
- [x] **Step 5:** 运行动作全套测试，确认 collect/sell/acceptTask 与移动、操炮、物品预留共用 actor-use 冲突路径，其他未完成动作仍明确拒绝；提交并推送。

## Task 3: 白天经济候选生成

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium)。候选评分反例最终用一次 GPT-5.6 Sol (`gpt-5.6-sol`, high) 集中评审，不做多模型投票。

**Files:** Create `src/strategy.hpp`, `src/strategy.cpp`, `tests/strategy_test.cpp`; modify `scripts/build.sh`.

- [x] **Step 1:** 写失败测试：邻矿立即采集；背包有矿且邻接小贩时批量卖总价最高矿；远处矿按下述精确收益率排序；两个工人不得争同一下一格；容量缺失、价格缺失或非正、矿/小贩不可达时不生成对应候选。
- [x] **Step 2:** 实现 `BaselineStrategy::decide(turn)`，按工人顺序从全部可达经济目标中选择一个已预留下一个格的最优候选，再统一交给 `arbitrate`；若裁决仍拒绝，保守空动作，不用同一 actor 的低质量候选掩盖规则不一致。
- [x] **Step 3:** 背包剩余容量定义为 `backPackCapability - backpack.size()`；容量缺失、容量不为正、当前物品数大于容量或算得 `q=min(10, 剩余容量)` 不大于零时，不生成出发采矿候选。其余矿点收益率为 `price*q/(到矿交互格距离 + q个collect回合 + 矿交互格到小贩交互格距离 + 1个sell回合)`；两段距离都用导航器到合法交互格的最短路。背包剩余容量不足 10，或白天剩余回合不大于预计回售总回合加 2 时，以小贩为目标。无法找到安全目标时返回空动作。
- [x] **Step 4:** 使用响应中的实际正价格；缺少价格、价格为零/负数或容量未知/零/小于当前库存时不做出发估值。收益率相同时依次按预计总收益高、总回合少、copper/iron/stone、坐标字典序确定，测试每个 tie-break。
- [x] **Step 5:** 运行策略测试和 1300 回放测试；提交并推送。

## Task 4: 开拓者接任务与夜间回防

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium)。边界测试用 Luna / low。

**Files:** Modify `src/strategy.cpp`, `tests/strategy_test.cpp`.

- [x] **Step 1:** 写失败测试：开拓者邻接有效任务点立即 acceptTask；有 `phaseTask` 时不离开；不可达任务点返回空；夜晚基线策略让三个角色只向已观测 station 周边回防，不生成采矿/出售/接任务；station 缺失时不猜回防目标并返回空。
- [x] **Step 2:** 白天为开拓者选择最近的己方有效任务点交互格；任务已激活时保留位置，为后续解题模块让出动作。
- [x] **Step 3:** 这是首版显式基线：从白天剩余回合不足“到基地交互格最短距离 + 2”时开始回防；夜晚无战斗模块时始终回防。后续威胁策略接入时再用可观测风险覆盖该基线。
- [x] **Step 4:** 多角色按“携矿工人、另一工人、开拓者”顺序规划并预留下一格，最终仍经过动作裁决器。
- [x] **Step 5:** 运行策略与动作测试；提交并推送。

## Task 5: 接入服务并回归

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium) 实现；GPT-5.6 Luna (`gpt-5.6-luna`, medium) 跑常规回归。关键失败才用 Sol / high。

**Files:** Modify `src/main.cpp`, `scripts/build.sh`, `tests/http_test.py`, `tests/replay_test.py`, `README.md`. `main.cpp` 持有一个 `BaselineStrategy`；每次合法请求执行 `parse_turn → strategy.decide → arbitrate → session.handle(input, decision)`，不改变 session 公共接口。

- [ ] **Step 1:** 写 HTTP 失败测试，证明最小白天观测不再总是空响应，并验证相同请求响应稳定。
- [ ] **Step 2:** HTTP 与回放入口解析一次用于策略；合法观测调用 `BaselineStrategy::decide`，把已裁决 Decision 交给 `AgentSession::handle(input, decision)`。session 为防御边界会再次解析；任一层解析失败都返回保守空响应且不得修改 session。用同一组有效/缺字段输入测试两层解析结论一致，重复请求测试验证策略输出稳定且 session 不重复推进。
- [ ] **Step 3:** 回放生成器固定覆盖 round 70/71、130/131、角色在 300–319 消失后重现、400/600 任务更替、每 257 回合一条损坏 JSON 和一次完全相同的重复请求。断言每个非空命令 action/必填字段合法、重复请求响应相同、损坏请求响应为空，且不再强制正常回合全部为空。另在 HTTP 集成场景让策略同时生成目标格冲突或同一 actor 冲突候选，断言响应只保留 `arbitrate` 接受的一项并且共享库存未超额预留。
- [ ] **Step 4:** 运行 debug、release、sanitize 全套；记录 1300 回合平均、P95 和最大延迟。
- [ ] **Step 5:** README 更新当前已实现能力与未知建造区限制；提交并推送。

## 完成标准

最小白天观测会产生确定性的合法经济或任务动作；夜晚没有武器时角色回防；重复请求不重复推进状态；所有输出经过统一裁决。正式地图建造区、真实对手移动和平台环境仍必须通过官方联调确认。
