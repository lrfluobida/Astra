# Astra

Astra 是《未来战争》公司编程比赛的 C++17 参赛程序。当前仓库已完成协议、会话、动作裁决、HTTP 服务、回放验证，以及首版经济、导航、开拓者任务、弱模型任务求解、夜间返航和武器防守策略。

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

本地脚本会使用 `.tmp` 中的 jsoncpp 兼容开发包。参赛程序源码只引用官方允许的 `json/json.h`，不再依赖 nlohmann/json 或 cpp-httplib。

## 官方 SDK 提交包

生成仅包含 CMake 与参赛源码的压缩包：

```bash
bash scripts/package_sdk.sh
```

产物为 `dist/Astra-CoreGeek.tar.gz`，目录根为 `SDK/CoreGeek/`。将其覆盖到公司提供的 SDK 后，应保留官方的 `SDK/ThirdParty/include/json/` 和 `SDK/ThirdParty/lib/libjsoncpp.so`，再从 `SDK/CoreGeek` 运行 CMake。默认构建类型为 Release，程序输出到 `SDK/bin/CoreGeek`；二进制通过相对 RPATH 加载官方 jsoncpp，无需修改系统库路径。

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

测试覆盖 UTF-8 JSON、协议缺失字段、重复回合、异步结果关联、共享资源和角色冲突、昼夜攻击边界、三类武器目标选择、任务 prompt/命令/复核/提交闭环、HTTP 分包和异常恢复、官方 SDK 提交包、经济路径选择、多人移动预留，以及 1300 回合连续回放。

## 当前能力边界

当前决策会根据商店价格和往返路程选择矿区，执行移动、采集和整包出售；多人路径会避开已观测障碍、建筑占地、角色占位和本回合已预留目标格。开拓者会前往并领取可用任务；角色会根据剩余白天路程提前返航，并在夜间向已观测基地周边回防。缺少关键字段、未知容量、不可达目标或缺少基地观测时会保守地放弃对应动作。

夜间存在已观测武器和邻接角色时，策略会优先消灭正在威胁我方基地的机器人，并继续攻击其他可得分机器人：火箭搜索 3×3 聚类溅射落点，电磁炮估算共线穿透，加特林保持目标在合法 90° 锥形内；最多三座武器与三个角色通过联合匹配分配，操作者不会同时移动。

自进化任务激活后，程序会向平台模型发送包含完整题面、离线限制和严格 JSON 输出协议的 prompt。模型可直接返回答案，也可请求一条沙盒命令；程序会把实际命令结果交给模型复核后再由开拓者提交。Markdown JSON、纯文本答案和损坏结构都有明确兼容或修复路径，活动任务中的开拓者昼夜均驻守任务点。

当前版本尚未实现建造。规则资料没有给出可可靠推导的建造区时，程序不会猜测建造坐标；战斗规划也不预测机器人下一步。1300 回合回放证明程序能够连续响应并守住状态和命令边界，不代表已经达到最终 PVE 表现上限。

正式环境已确认是 CentOS 7.6、GCC 8.3.1、CMake 3.16.5 和 Make 4.2.1。当前已用 jsoncpp 1.9.5 兼容包验证构建与运行；收到的官方 jsoncpp 1.9.3 文件尚未放入本机工作区，因此正式提交前仍需用官方头文件和 `.so` 做最后一次构建。平台实际模型与上传界面的文件格式仍待公司平台确认。
