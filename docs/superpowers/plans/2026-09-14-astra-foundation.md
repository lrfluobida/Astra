# Astra 协议与决策基础 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **模型与成本：** 按每个 Task 的 Model 字段显式选模，使用相关文件和精简任务说明，不默认继承旗舰主模型或完整对话。普通主会话建议 GPT-5.6 Terra / medium；本文件不自动切换应用当前模型。升级与评审规则以总设计第 1.2 节为准。

**Goal:** 实现第一天的可运行基础：HTTP 输入、结构化观测、局内状态、动作裁决、保守响应和连续回放，作为后续经济、战斗与解题模块的接入点。

**Architecture:** 使用 C++17 单进程主程序；服务和回放共用 AgentSession。状态更新与响应生成按请求串行处理，LLM/沙盒任务只记录和返回平台字段，不在主程序等待或执行。首阶段策略可返回空动作，后续模块通过候选动作接入同一个裁决器。

**Tech Stack:** C++17、g++、nlohmann/json、cpp-httplib、Python 标准库 HTTP 集成测试；直接编译脚本，无必需 CMake/Ninja。

---

## 范围与依赖

对应 [总设计](../specs/2026-09-14-astra-design.md) 的第 2、3 节和第 6 节中协议/状态/回放要求；一周路线在总设计第 7 节。本计划只覆盖第一阶段，不能用空动作服务宣称已完成参赛策略或具备比赛胜率。

后续阶段按“经济与导航 → 战斗与操作员调度 → 任务与配方 → 新闻宝藏 → 对照优化”实施，每阶段基于真实文件和前一阶段结果补充短计划，不一次性预造全部题型和模拟框架。

所有文本按 UTF-8 读写；编辑已有文件前严格检查编码并保留中文及行尾。新 shell 脚本用 UTF-8 无 BOM、LF。构建产物放 build/，不要加入 Git。

所有项目文件落在 E:/develop/C++/Astra，项目自行生成的回放、日志和临时文件也使用该根目录下的 artifacts/、.tmp/，按需创建。不得继续在 D 盘原项目副本实现或生成构建文件。执行 shell 时显式设置工作目录；WSL 示例工作目录为 /mnt/e/develop/C++/Astra。

本机已核实 Windows 有 MinGW-w64 g++ 8.1.0、Python 3.7.9；WSL Ubuntu 有 Python 3.12.3，尚无 g++。Windows 编译器可用于纯内核测试，HTTP 服务优先在 WSL 构建和测试：cpp-httplib 上游明确不支持或测试 MinGW，不能将 HTTP 联调押在该组合上。[HTTP 库平台说明](https://github.com/yhirose/cpp-httplib#supported-platforms)

开发阶段将必要头文件和许可证随源码保存，运行和构建不在线下载依赖。起始版本选择已查证的 [nlohmann/json v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0) 和 [cpp-httplib v0.56.0](https://github.com/yhirose/cpp-httplib/releases/tag/v0.56.0)，先做本地编译验证；只启用普通 HTTP，不启用 TLS、压缩和其他可选依赖。若兼容性测试失败，依据错误调整并记录实际版本，不自动追踪 latest。

## 文件布局

所有项目相对路径均以 E:/develop/C++/Astra 为根；WSL 中对应 /mnt/e/develop/C++/Astra。

| 文件 | 职责 |
|---|---|
| third_party/nlohmann/json.hpp、third_party/nlohmann/LICENSE.MIT | 固定 JSON 依赖与许可证 |
| third_party/httplib.h、third_party/cpp-httplib.LICENSE | 固定 HTTP 依赖与许可证 |
| third_party/README.md | 版本、官方来源、可选功能和实际编译验证 |
| src/protocol.hpp、src/protocol.cpp | 观测结构、显式解析、响应序列化 |
| src/session.hpp、src/session.cpp | 对局记忆、请求关联、回合推进、重复处理 |
| src/actions.hpp、src/actions.cpp | 候选动作、共享资源预留、合法性检查 |
| src/main.cpp | 参数处理、HTTP/回放入口、顶层异常收束 |
| tests/test_main.cpp、tests/test_support.hpp | 小型断言与命名测试入口 |
| tests/protocol_test.cpp、tests/session_test.cpp、tests/actions_test.cpp | 行为测试 |
| tests/fixtures/minimal_turn.json | 明确标记为合成的基础观测 |
| tests/fixtures/README.md | 样例来源、合成部分及覆盖目标 |
| tests/http_test.py、tests/replay_test.py | 启停进程、HTTP、UTF-8、多回合与延迟检查 |
| scripts/build.sh | 编译服务和测试，支持 debug/release |
| scripts/test.sh | 单元、HTTP、回放测试入口 |
| run.sh | 接收端口并 exec 已构建程序 |
| .gitignore、README.md | 构建排除、运行方法、阶段能力边界 |

不为尚未实现的经济、战斗或模型模块创建空类、插件注册器或接口工厂。

## Task 1: 核实工具链并建立最小构建

**Model:** GPT-5.6 Luna (`gpt-5.6-luna`, low) 负责文件、依赖和脚本；出现编译兼容错误且常规定位未解决时交 GPT-5.6 Terra (`gpt-5.6-terra`, medium)。安装、编译和测试通过工具执行，不为每条命令另起模型任务。

**Files:** Create third_party/ 下上述文件、scripts/build.sh、tests/test_main.cpp、tests/test_support.hpp、.gitignore。

- [x] **Step 1:** 确认 WSL Ubuntu 仍存在。安装实现所需的 g++：在 PowerShell 分别运行 `wsl -d Ubuntu -u root -- apt-get update` 和 `wsl -d Ubuntu -u root -- apt-get install -y g++`。这是开发环境变更，执行前说明；如安装失败保留错误，先用已有 MinGW 验证纯内核，不把 HTTP 阶段标为完成。
- [x] **Step 2:** 从固定官方标签下载头文件和许可证，记录版本与来源；严格 UTF-8 验证第三方文本。不改第三方源文件，不引入联网构建或额外依赖管理器。
- [x] **Step 3:** 建立可按名称过滤的断言测试入口。先用仅包含标准库与 JSON 的测试验证中文原文往返、`std::optional` 和整数类型，并确认失败断言确实返回非零。
- [x] **Step 4:** 创建增量维护显式源文件列表的构建脚本。编译参数为 `-std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc -Ithird_party`，release 使用 `-O2`，debug 使用 `-O0 -g`；WSL 调试验证可加 `-fsanitize=address,undefined -fno-omit-frame-pointer`。产物为 build/astra 和 build/astra_tests；本步尚无 main.cpp 时先只构建测试。
- [x] **Step 5:** 在 WSL 项目目录运行 `bash scripts/build.sh debug` 和 `./build/astra_tests toolchain`，预期退出码 0，中文 JSON 字符串完全一致。记录实际 g++ 版本；Windows 产物不能替代 Linux 产物。
- [x] **Step 6:** 仅提交本任务已验证的源码、脚本与许可证。

## Task 2: 观测解析与响应契约

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium) 负责协议与序列化；明确规则下的合成数据可交 Luna / medium。关键响应契约随 Task 4 统一评审，不重复评审整份文档。

**Files:** Create src/protocol.hpp、src/protocol.cpp、tests/protocol_test.cpp、tests/fixtures/minimal_turn.json、tests/fixtures/README.md；modify scripts/build.sh。

- [x] **Step 1:** 根据接口文档手工建立最小合成观测：41×32，Astra challenger，一座 2×2 基地、两工人、一开拓者、75 金币、空任务和机器人数组。保留中文 worldNews。不要原样复制 response.txt 的重复键，也不要把缺失的样例字段说成正式规则。
- [x] **Step 2:** 写失败测试：必需坐标缺失不会变成 (0,0)；未知 roleType 不产生可操控角色；未知字段不破坏解析；缺失 cooldown/targetTeam/timeoutRounds 表示未知并限制相关策略，不以零值证明可攻击或任务未超时。
- [x] **Step 3:** 在 src/protocol.hpp 定义 Pos、UnitObservation、TaskPointObservation、TurnObservation、ParseResult，以及最小 Decision 响应结构（roleCommandMap 与可选 prompt/executeCmd），由 src/protocol.cpp 实现解析和编码。使用可选值表示文档样例缺失的信息。至少保留 mapInfo、teamOur、teamEnemy、robot、phaseTask、上回合结果、worldNews、商店和 errors。TurnObservation 的 raw 字段保留完整 JSON，后续不因未建模字段丢失信息；Task 3 的会话和 Task 4 的裁决器直接复用 Decision，使本任务可独立编译。
- [x] **Step 4:** 编写并实现响应序列化测试：空响应准确输出对象 `{"roleCommandMap":{}}`；字典键用 ID 字符串；attack 使用武器键和角色 controllerId；prompt/executeCmd 未使用时可省略；中文字符串不转成转义序列或乱码。
- [x] **Step 5:** 运行 `bash scripts/build.sh debug`、`./build/astra_tests protocol`，先观察失败再实现至通过。测试比较 JSON 语义及原始中文值，不依赖对象键顺序。
- [x] **Step 6:** 更新样例说明，记录哪些字段来自规则、哪些数值只是测试场景；提交本任务变更。

预期公共函数契约：

```cpp
ParseResult parse_turn(const nlohmann::json& input);
nlohmann::json encode_response(const Decision& decision);
```

ParseResult 必须携带失败原因；不能通过 catch-all 静默生成伪造的有效观测。

## Task 3: 局内状态与异步结果关联

**Model:** GPT-5.6 Sol (`gpt-5.6-sol`, high) 负责状态边界、任务序号和结果关联；这一环节直接影响正确性，不先用 Luna 反复尝试。按已经确定的接口补充普通测试数据时可交 Terra / medium。

**Files:** Create src/session.hpp、src/session.cpp、tests/session_test.cpp；modify tests/test_main.cpp、scripts/build.sh。

- [x] **Step 1:** 写序列测试：round 1→2 正常推进；同轮相同输入返回相同响应且不重复消费结果；同轮不同输入不重复推进记忆；round 倒退/阵营变化触发清空局内状态；两个任务题面相同也能区分不同接取过程。
- [x] **Step 2:** 定义 AgentSession 的 `nlohmann::json handle(const nlohmann::json&)`，保留上一请求的完整 JSON、实际响应及 pending LLM/command 的发送回合、本地任务序号。用 JSON 语义相等比较重复输入，不增加哈希依赖。
- [x] **Step 3:** 写失败测试：旧任务结果不会写入新任务；没发 prompt 时不将意外 llmResp 当作当前建议；同回合发出的 executeCmd 不能产生已知结果。实现 pending 元数据与结果分派基础，不在此阶段做真正解题。
- [x] **Step 4:** 实现以回合、阵营、teamId、已发送接取动作及观测变化驱动的状态更新。新闻同一天只登记一次原文；重复请求不重复扣 LLM 配额。缺少明确任务状态时采取普通日额度，并暂停依赖任务权限的命令。
- [x] **Step 5:** 运行 `bash scripts/build.sh debug`、`./build/astra_tests session`，预期全部序列通过；提交本任务变更。

本阶段的 Decision 默认是空动作；日志要清楚表明策略尚未接入，不能将该响应当作完整参赛能力。

## Task 4: 动作归属与共享资源裁决

**Model:** GPT-5.6 Sol (`gpt-5.6-sol`, high) 负责角色、资源与顶层请求的统一裁决。完成后用独立的 Sol / high 对 Task 2–4 的关键变更做一次评审；仅将发现的具体缺陷交回修正，相关测试通过后不再全量复审。

**Files:** Create src/actions.hpp、src/actions.cpp、tests/actions_test.cpp；modify src/session.cpp、scripts/build.sh。

- [x] **Step 1:** 建立 CandidateAction 和 Reservation，明确 actionKey、controllerId、金币/物品需求、目标位置、排序优先级。候选由测试注入，首阶段不额外实现寻路或战斗策略。
- [x] **Step 2:** 写失败测试：同一工人操炮时不能同时移动；同一工人不能同时操两座炮；两个工人各花 25 金时，共有 25 金只能接受一项；同一张升级券不能被两项动作消费；目标格争抢和位置交换不能同时获准。
- [x] **Step 3:** 实现资源预留与可观察前提检查：己方存活角色/武器、昼夜、邻接与射程、显式 cooldown、目标数和加特林角度；未知建造区不批准 build。机器人下一步未知，不宣称能避免所有实际碰撞。
- [x] **Step 4:** 为所有已定义动作保留必需字段检查。未完成规则支持的动作返回明确“不支持”原因，不透传未检查的模型或策略 JSON。已知合法空候选集合必须产出有效空响应。
- [x] **Step 4a:** 将顶层 prompt/executeCmd 纳入同一个最终裁决。测试任务与新闻同时请求模型时只发送一项；最后一笔日常额度不会被重复消费；开拓者被最终移动计划带离任务点或选中 submitAnswer 时不发送依赖任务继续存在的命令；只有最终发送的请求登记 pending。
- [x] **Step 5:** 测试关键边界：round 70/71、130/131；基地四格坐标；同角度/90 度边界；重复火箭落点允许表示但伤害规则不在此阶段实现。
- [x] **Step 6:** 运行 `bash scripts/build.sh debug`、`./build/astra_tests actions`，预期冲突场景只有一组一致的获准动作；提交本任务变更。

关键行为断言的完整示例应按测试工具的实际接口落地：

```cpp
const auto accepted = arbitrate(turn, {tower_attack, worker_move}, rules);
require(accepted.actor_use_count(worker_id) == 1,
        "attack and move must not use the same worker");
require(accepted.gold_reserved() <= turn.gold,
        "accepted actions must fit shared gold");
```

这些名字是待实现接口契约；先写出测试对象与失败案例，再补最小实现，不只验证序列化与自身输出相同。

## Task 5: HTTP 服务、运行入口与回放模式

**Model:** GPT-5.6 Terra (`gpt-5.6-terra`, medium) 实现服务与会话集成；GPT-5.6 Luna (`gpt-5.6-luna`, medium) 可独立实现 HTTP 测试和启动脚本。共享状态的接口变更必须回到 Task 3–4 的契约，不自行扩展。

**Files:** Create src/main.cpp、run.sh、tests/http_test.py；modify scripts/build.sh。

- [ ] **Step 1:** 用 Python 标准库写 HTTP 进程测试，命令为 `python3 tests/http_test.py --binary ./build/astra`。测试以参数列表启动进程、等待短周期就绪并在 finally 中终止自己的子进程，不使用跨 shell 拼接进程清理命令。
- [ ] **Step 2:** 测试端口来自命令行，POST `/` 和 `/act` 都得到 JSON 对象；正文包含中文；服务连续接收两个不同回合；非法 JSON 返回 HTTP 200 和保守空动作，随后有效请求仍可处理。路径未在比赛文档固定，服务注册通用 POST 路由，平台明确后再收敛。
- [ ] **Step 3:** 使用 cpp-httplib 接入 AgentSession，以互斥保护一次完整的状态读取、决策和提交。串行调度避免共享状态竞态；并发请求只作为健壮性检查，不假设平台会并发调度不同回合。
- [ ] **Step 4:** 实现参数形式 `astra <port>` 和 `astra --replay <input.jsonl>`。回放每行是独立请求，stdout 每行一个响应 JSON，诊断写 stderr。未知参数或非法端口在启动时明确失败，不启动无效服务。
- [ ] **Step 5:** 写 run.sh，从脚本所在目录定位 build/astra，并用 `exec` 转发端口。启动过程不下载依赖、不自动重编译、不要求 Python 常驻服务。
- [ ] **Step 6:** 运行 `bash scripts/build.sh debug`、`python3 tests/http_test.py --binary ./build/astra`。预期进程存活、HTTP 响应可解析、中文保持一致；加入 `bash run.sh` 的真实启动测试，记录 Linux 本地结果，提交本任务变更。

## Task 6: 1300 回合与预算降级验证

**Model:** GPT-5.6 Luna (`gpt-5.6-luna`, medium) 编写常规回放脚本和文档；GPT-5.6 Terra (`gpt-5.6-terra`, medium) 负责预算降级与失败定位。1300 回合由程序执行，不逐回合调用模型。涉及状态/裁决契约的新缺陷交 Sol / high 做针对性修复和复核。

**Files:** Create tests/replay_test.py、scripts/test.sh；modify src/session.hpp、src/session.cpp、tests/session_test.cpp、README.md。

- [ ] **Step 1:** 增加轻量可注入时钟的预算对象，只在 session 内使用；不建立通用调度框架。测试基线响应先准备，搜索预算已过时直接保留基线，记录降级原因。
- [ ] **Step 2:** 生成 1300 行合成回放，包含昼夜边界、角色死亡/重新出现、任务更替、中文新闻、缺失可选字段和少量损坏输入。每轮响应必须可解析；损坏输入不得改写已确认的有效局内状态。
- [ ] **Step 3:** 用真实可测行为验收状态，而不只检查空输出：单元序列验证任务序号和重复请求；动作测试验证冲突预算；HTTP 集成验证连续服务。明确空动作 1300 轮不证明生存策略完成。
- [ ] **Step 4:** 运行 `bash scripts/test.sh`，执行单元测试、HTTP 测试及 `python3 tests/replay_test.py --binary ./build/astra --rounds 1300`。输出测试退出码、回合数和响应延迟统计。初期本机目标 max < 1 秒，正式平台仍以 < 5 秒限制复测；若未达标报告实测数据并定位，不以目标代替证据。
- [ ] **Step 5:** release 编译后重复一次集成与回放；debug sanitizer 测试无错误。只有新修改或失败才继续重跑相应测试。
- [ ] **Step 6:** README 记录明确可执行的构建、启动、测试命令和当前阶段能力；记录尚未验证的正式 OS/架构/提交方式，完成本阶段提交。

## 执行与交付

任务 1→2→3 为状态接口主链，任务 4 可在协议契约稳定后独立实现；任务 5 等待 session 契约稳定，任务 6 做整合。可并行处理 actions 和 HTTP 测试文件，避免多个代理同时修改 session.cpp 或 build.sh。

通常最多两个执行子代理同时推进，第三个名额留给有明确对象的评审。Luna/Terra 连续两次有证据的修正未解决同一问题时带最小复现升级一档；Sol 仍未解决重大问题时才使用 GPT-6 Astra (`gpt-6-astra`, high)。不默认使用 max/xhigh、Fast 模式、全上下文复制或多模型投票。这里的角色分工是开发流程，不新增参赛程序对 OpenAI API 的调用。

有 usage 数据时在已有执行记录中保留实际模型、推理强度、用量、重试和验收结果，按完成任务的总消耗判断是否省钱；没有数据就记录未知，不把官方费率示例当成实际账单。比赛运行时的固定模型及调用策略见总设计第 1.3 节。

阶段完成必须展示实际测试结果和本地启动方式。随后直接进入经济与导航阶段的短计划；在官方建造区未确认时使用明确标记来源的地图配置与合成测试，不能通过猜测宣称正式可用。
