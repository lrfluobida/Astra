# Astra 弱模型任务 Prompt 管线 Implementation Plan

**Goal:** 让 Astra 在自进化任务期间可靠完成 `prompt → llmResp → executeCmd/复核 → submitAnswer` 闭环，以较弱的平台模型获得尽可能高的任务正确率和速度分。

**Architecture:** 新增无状态 `task_solver`，只根据当前 `TurnObservation` 生成任务动作与顶层 prompt/executeCmd 候选。任务动作、夜间操炮和角色移动继续经过统一 `arbitrate`；`AgentSession` 负责额度、任务序号和异步结果关联。

**Tech Stack:** C++17、nlohmann/json、现有动作裁决和 HTTP/回放测试。

## 模型与成本分工

- GPT-5.6 Sol / high：设计低能力模型的输出协议、失败恢复和任务状态反例，只用于关键设计。
- GPT-5.6 Terra / medium：实现 C++ 状态映射、解析器和集成测试。
- GPT-5.6 Luna / low：运行格式变体、回归和常规失败分类。
- 比赛运行时只使用平台提供模型；经济、路径、战斗、JSON 检查和命令选择由本地 C++ 完成。

## Task 1: Prompt 协议与返回解析

- [x] 写失败测试：首次任务 prompt 包含完整题面、离线限制和严格单行 JSON 协议；无任务或无存活开拓者不调用模型。
- [x] 支持 `{"kind":"answer","answer":...}`、`{"kind":"command","command":"..."}`、Markdown JSON 代码块和纯文本答案。
- [x] JSON 结构损坏时生成修复 prompt，不把解释文字误当命令执行。
- [x] `lastCmdResult` 存在时生成带题面、实际结果和错误信息的复核 prompt。

## Task 2: 策略与会话集成

- [x] 模型答案生成高优先级 `submitAnswer`，阻止同一开拓者在该回合移动或操炮。
- [x] 模型命令生成仅任务期可用的 `executeCmd`；复用 session 的 pending 关联和重复请求缓存。
- [x] 未等待任务结果的工人和武器继续执行经济或夜间防守。
- [x] HTTP 测试覆盖首轮 prompt、命令、复核和提交四步链路。

## Task 3: 回归与交付

- [x] 1300 回放注入任务与模型/命令返回，验证 prompt/executeCmd/submitAnswer 字段和重复请求稳定。
- [x] 运行 debug、release、sanitize 全套；Release 1300 回合平均 0.198 ms、P95 0.270 ms、最大 5.026 ms。
- [x] 更新 README，复查 UTF-8 与中文，提交并推送。

## 完成标准

任务激活后不再空等；弱模型能在严格协议和纯文本回退下直接答题，也能通过离线沙盒获得证据后复核提交。任务外不误用 executeCmd，任务期模型调用不被本地每日三次计数阻断。
