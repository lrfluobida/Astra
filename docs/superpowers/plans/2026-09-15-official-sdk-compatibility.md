# Astra 官方 C++ SDK 兼容迁移计划

**目标：** 移除参赛程序对 nlohmann/json 和 cpp-httplib 的运行依赖，使用官方 jsoncpp 与 POSIX socket，并提供适配 CentOS 7.6、GCC 8.3.1、CMake 3.16.5 的可提交构建入口。

## 已确认约束

- 官方环境为 64 位 CentOS 7.6，GCC/G++ 8.3.1，CMake 3.16.5，Make 4.2.1。
- C++ SDK 仅提供 jsoncpp 1.9.3 的头文件与 `libjsoncpp.so`；不允许其他第三方库。
- 判题器以 HTTP POST 发送一回合 JSON，请求体可能分包，响应必须包含准确的 `Content-Length`。
- GCC 8.3.1 支持当前代码需要的 C++17；CMake 显式设置 C++17，不降级现有 `optional` 和结构化绑定实现。
- 官方 jsoncpp 文件尚未进入 Astra 工作区；本地可用兼容版本验证，最终必须再用官方 1.9.3 文件构建一次。

## 模型与成本分工

- GPT-5.6 Sol / high：审查 SDK ABI、HTTP 边界和迁移设计，只用于高风险接口决策。
- GPT-5.6 Terra / medium：迁移 JSON 边界、原生 socket 和 CMake，实现主体代码。
- GPT-5.6 Luna / low：运行格式变体、分包请求、回放和常规回归。
- 比赛平台模型仍只用于自进化任务；SDK 迁移不增加运行时模型调用。

## Task 1：建立官方 JSON 边界

- [x] 新增 `json_io`，用 `Json::CharReaderBuilder` 严格解析，用 `Json::StreamWriterBuilder` 紧凑写出。
- [x] 将协议、会话、任务模型返回解析和 C++ 测试迁移到 `Json::Value`。
- [x] 保持缺失字段、错误类型、64 位整数、中文和未知字段的现有保守语义。
- [x] 参赛目标的源码与头文件不再引用 `nlohmann`。

## Task 2：替换 HTTP 服务器

- [x] 新增 POSIX socket 服务，循环读取完整请求头和 `Content-Length` 指定的请求体。
- [x] 限制头部与请求体大小，处理短读、短写、EINTR 和断连，不因畸形请求终止进程。
- [x] HTTP 测试覆盖正文分包、中文、错误 JSON、重复回合与现有任务闭环。
- [x] 参赛目标不再引用 `httplib`。

## Task 3：官方构建与交付

- [x] 新增 CMake 构建入口，明确 C++17，链接 `jsoncpp`，产物名为 `CoreGeek`。
- [x] 为 `SDK/bin/CoreGeek` 到 `SDK/ThirdParty/lib` 设置相对 RPATH，避免运行时找不到 `.so`。
- [x] 保留本地脚本构建和测试入口，并增加官方目录布局检查。
- [x] 生成不包含 nlohmann、httplib、测试和临时文件的提交包。
- [x] 运行 Debug、Release、Sanitizer、CMake、HTTP 和 1300 回合回放；Release 平均 0.285 ms、P95 0.384 ms、最大 22.685 ms。正式 jsoncpp 1.9.3 文件到位后仍需复验一次。

## 完成标准

参赛目标只依赖 C++ 标准库、POSIX 系统调用和官方 jsoncpp；在本地兼容库及官方 jsoncpp 1.9.3 下均可构建，分包 HTTP 请求可完整处理，现有策略与任务求解回归全部通过。若官方库文件仍缺失，只能标记本地兼容验证完成，不能声称正式 SDK 构建已经验证。
