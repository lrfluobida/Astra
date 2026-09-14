# Astra

Astra 是《未来战争》公司编程比赛的 C++17 参赛程序。当前仓库已完成协议、会话、动作裁决、HTTP 服务、回放验证，以及首版经济、导航、开拓者任务和夜间返航策略。

## 构建

在 WSL Ubuntu 中运行：

```bash
cd /mnt/e/develop/C++/Astra
bash scripts/build.sh debug
```

构建产物：

- `build/astra`：HTTP 服务和回放程序
- `build/astra_tests`：C++ 单元测试

可用构建模式为 `debug`、`release` 和 `sanitize`。

## 启动

判题系统传入端口时运行：

```bash
bash run.sh 8080
```

服务监听 `0.0.0.0`，接受任意 POST 路径。非法 JSON 会得到 HTTP 200 和保守空响应，服务随后仍可继续处理请求。

回放 JSONL 文件：

```bash
./build/astra --replay artifacts/match.jsonl
```

也可用 `--replay -` 从标准输入逐行读取。每行请求对应 stdout 的一行 JSON 响应，诊断写入 stderr。

## 测试

```bash
bash scripts/test.sh debug
bash scripts/test.sh release
bash scripts/test.sh sanitize
```

测试覆盖 UTF-8 JSON、协议缺失字段、重复回合、异步结果关联、共享资源和角色冲突、昼夜攻击边界、HTTP 异常恢复、经济路径选择、多人移动预留，以及 1300 回合连续回放。

## 当前能力边界

当前决策会根据商店价格和往返路程选择矿区，执行移动、采集和整包出售；多人路径会避开已观测障碍、建筑占地、角色占位和本回合已预留目标格。开拓者会前往并领取可用任务；角色会根据剩余白天路程提前返航，并在夜间向已观测基地周边回防。缺少关键字段、未知容量、不可达目标或缺少基地观测时会保守地放弃对应动作。

当前版本尚未实现建造、战斗目标选择和任务解答。规则资料没有给出可可靠推导的建造区时，程序不会猜测建造坐标。1300 回合回放证明程序能够连续响应并守住状态和命令边界，不代表已经具备最终比赛胜率。

正式判题环境的 Linux 版本、CPU 架构、编译命令、提交包格式和平台实际模型仍待公司平台说明确认。第三方依赖已固定在 `third_party/`，比赛运行和构建过程不联网下载。
