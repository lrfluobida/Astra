# Astra

Astra 是《未来战争》公司编程比赛的 C++17 参赛程序。当前仓库已完成协议、会话、动作裁决、HTTP 服务和回放验证基础，后续在此基础上接入经济、导航、战斗和任务策略。

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

测试覆盖 UTF-8 JSON、协议缺失字段、重复回合、异步结果关联、共享资源和角色冲突、昼夜攻击边界、HTTP 异常恢复，以及 1300 回合连续回放。

## 当前能力边界

当前决策默认返回合法空动作，尚未接入采矿、建造、导航、战斗目标选择和任务解答策略。1300 回合回放通过只能证明程序连续响应和状态边界稳定，不能证明具备生存能力或比赛胜率。

正式判题环境的 Linux 版本、CPU 架构、编译命令、提交包格式和平台实际模型仍待公司平台说明确认。第三方依赖已固定在 `third_party/`，比赛运行和构建过程不联网下载。
