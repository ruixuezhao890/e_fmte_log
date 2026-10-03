# 三个库的使用手册（ecli · eserde · matchit）

> 这里的文档目标不是"查 API"，而是**看完就知道怎么写代码**：每份都是"30 秒是什么 → 第一个能跑的例子 →
> 语法/标签全表 → 完整可抄示例 → 坑 → 怎么自测"。
> 手册里的**每段代码都有可编译版本**（`tests/*_manual_examples.cpp`，`tests/run_check.ps1` 每次都跑），
> 所以照着抄不会踩空 —— 这也是本仓库对文档的硬要求。

## 谁是谁：一张依赖图

```
efmt/                     格式化（地基，无第三方依赖）
├── elog/                 分级日志（需要外部 ETL）
├── eserde/               编译期反射基座：能力标签 + schema + 按索引取字段（可选层，不 include 零开销）
│   ├── eserde::json      JSON 写 / 读
│   └── eserde::cbor      CBOR 写 / 读
└── ecli/                 命令行解析（声明即推导，对标 Rust clap 的 derive）
    ├── ecli/command.hpp      命令表 / 子命令 / 命令名模式段   ← 段匹配交给 matchit
    └── ecli/elog_reply.hpp   命令回复复用 elog 已绑定的通道（可选层）

matchit/                  第三方冻结副本（Apache-2.0）：Rust `match` 表达式的 C++ 移植
                          —— 我们只用到它的一小块，见 MATCHIT 手册与 ../../matchit/PATCHES.md
```

| 你想干的事 | 看哪份 |
|---|---|
| 写一个命令行工具（宿主 argv），或者给串口/蓝牙做一套命令 | [ECLI-使用手册.md](ECLI-使用手册.md) |
| 把结构体存成 JSON / CBOR、再读回来，还能自己接一种新格式 | [ESERDE-使用手册.md](ESERDE-使用手册.md) |
| 命令名里要带参数段（`wifi set :ssid`、`log *rest`），想弄懂匹配怎么做的 | [MATCHIT-使用手册.md](MATCHIT-使用手册.md)（先看 ECLI 手册的命令表一章） |
| 想知道 ecli 为什么这么设计、和 clap 还差什么 | [../ECLI-命令行解析-方案.md](../ECLI-命令行解析-方案.md) · [../ECLI-与clap的差距清单.md](../ECLI-与clap的差距清单.md) |
| 想知道格式化 / 日志怎么写、每个宏花多少 Flash | [../EFMT-使用手册.md](../EFMT-使用手册.md)（18 章大手册） |

## 每份手册的目录（点进去直接跳）

三份都是同一骨架：**① 30 秒是什么 · ② 第一个能跑的例子 · ③ 逐块讲清楚（全表）· ④ 完整可抄示例 · ⑤ 坑与 FAQ · ⑥ 怎么自测**。

| 手册 | ③ 全表里有什么 | ④ 完整可抄示例 |
|---|---|---|
| [ECLI-使用手册.md](ECLI-使用手册.md) | 声明即推导 / 字段标签全表 / 取值类型全表 / 命令行语法 / 帮助·版本·报错 / 三个入口（argv·一行文本·token 表）/ 命令表 / 模式段 `:参数`·`*余下` / 回复通道与"一次绑定，多处使用" / 裁剪宏与代价 | **串口命令行骨架**：收字节 → 一行到齐 → dispatch → 回复 |
| [ESERDE-使用手册.md](ESERDE-使用手册.md) | 声明与能力标签 / 字段标签（格式名就是标签名）/ JSON / CBOR / 支持的类型 / 能力门禁 / 基座 API（schema·visit_fields·field_at）/ **自己接一种新格式（TOML-ish）** / 裁剪开关与代价 | **设备配置的保存与读回**（含缓冲判断与错误码处理） |
| [MATCHIT-使用手册.md](MATCHIT-使用手册.md) | 骨架三句话 / 返回值与收尾 / 字面量与通配 `_` / 比较·`and_`·`or_`·`not_` / `meet` / `app`+`some` / `Id<T>` / `ds` / `dsVia` / 范围与 `ooo` / `when` / 真实报错 | **本仓库的接缝**：`segment_extractor` + `match_segment` + 匹配排序，接一个端到端小工具 |

## 三条最短路线（每条约 30 分钟）

1. **先跑起来，再读文档**（强烈建议）：按 [../SANDBOX-命令台上手指南.md](../SANDBOX-命令台上手指南.md)
   把沙盒命令台跑起来 —— 里面 11 条命令把解析、命令表、模式段、日志、JSON/CBOR 都能亲手敲一遍看输出。
   看一眼真实行为，比读十页文档快。
2. **写自己的命令行工具**：ECLI 手册 ② 最小例子 → ③ 字段标签全表 → ④ 完整可抄示例（串口命令行骨架）。
3. **给已有结构体加序列化**：ESERDE 手册 ② 最小例子 → ③ JSON/CBOR 全表 → ④ 配置存取示例（含错误处理）。

## 三条约定（写文档时照这个来）

1. **手册里的代码必须能编译**：在对应的 `tests/*_manual_examples.cpp` 里放一份，`run_check.ps1` 会跑它 ——
   文档和实现脱节时，测试先红（同 `tests/efmt_manual_examples.cpp` 的老规矩）。
2. **新增任何能力，都要在 `docs/` 留一份"新手照着做就能跑"的使用文档**，与代码同一批提交。
3. 库的裁剪宏、体积读数、改动清单这类"清单型"内容以 [../EFMT-使用手册.md](../EFMT-使用手册.md)
   与各库就地 README（[`sandbox/README.md`](../../sandbox/README.md)、
   [`matchit/README.md`](../../matchit/README.md)、[`matchit/PATCHES.md`](../../matchit/PATCHES.md)）为准，这里不重复。

## 就地文档在哪

| 位置 | 内容 |
|---|---|
| [`ecli/cli.hpp`](../../ecli/cli.hpp) · [`ecli/command.hpp`](../../ecli/command.hpp) · [`ecli/elog_reply.hpp`](../../ecli/elog_reply.hpp) | 头文件顶部的用法注释（改代码时先看这里） |
| [`eserde/serde.hpp`](../../eserde/serde.hpp) · [`json.hpp`](../../eserde/json.hpp) · [`cbor.hpp`](../../eserde/cbor.hpp) | 同上 |
| [`matchit/README.md`](../../matchit/README.md) · [`PATCHES.md`](../../matchit/PATCHES.md) | 第三方副本：为什么引进、用在哪、改了什么、怎么升上游 |
| `tests/*_check.cpp` | 行为语义的权威（边界、错误码、反例都在这） |
