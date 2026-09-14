# 第三方依赖

本目录固定保存比赛程序构建所需的单头文件。构建过程不会联网下载依赖。

| 文件 | 版本 | 官方来源 | SHA-256 |
|---|---|---|---|
| `nlohmann/json.hpp` | nlohmann/json v3.12.0 | `single_include/nlohmann/json.hpp` | `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63` |
| `nlohmann.LICENSE.MIT` | nlohmann/json v3.12.0 | `LICENSE.MIT` | `46A65CFFD1EA955132D95A8DD921640714A8D6B537D2E4E482D31145AE95B603` |
| `httplib.h` | cpp-httplib v0.56.0 | `httplib.h` | `1F99E51881C4C9D0649B27C611442C2F4D9BCFEC5A22A14D5FCD1F8106F730B4` |
| `cpp-httplib.LICENSE` | cpp-httplib v0.56.0 | `LICENSE` | `4B45CBE16D7B71B89AE6127E26E0D90A029198CA5E958AD8E3D0B8BBED364D8B` |

来源标签：

- <https://github.com/nlohmann/json/releases/tag/v3.12.0>
- <https://github.com/yhirose/cpp-httplib/releases/tag/v0.56.0>

仅使用普通 HTTP。当前阶段不启用 TLS、压缩及其他可选依赖。
