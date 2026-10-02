# 方案：ecli 命令行解析（对标 Rust clap 的 derive 用法）

> 状态：**阶段一、阶段二已实现并验证**（2026-10-02）。本文记录拍板结论、交付物与边界，
> 供后续阶段（多源控制台 / 权限分级）接着往上做。
> 备份：`pre-cli-20261002` 标签 + `docs/backup/pre_cli_20261002.bundle`。

## 1. 为什么这件事能成立

`eserde/serde.hpp` 的文件头注释里本来就写着"序列化本体（JSON / 二进制 / **CLI**）
由后续文件各自实现"。clap 的 derive 需要的东西基座已经齐了：

| clap（Rust） | 本项目基座 | 状态 |
|---|---|---|
| `#[derive(Parser)]` | `E_FMT_DERIVE(struct args{...}, Cli)` 能力标签 | 新增 `Cli` 标签 |
| `#[arg(short, long, help="…")]` | `[[efmt::arg(short, long, help = "…")]]` 编译期解析 | 已有（v1.9） |
| 字段名 / 类型名 / 枚举取值 schema | `eserde::field_name / field_type_name / enum_value` | 已有 |
| `FromStr` 取值 | `traits.hpp` 搬运助手（`integer_fits` / `push_checked` / `is_writable_string` …） | 已有 |
| 缺能力就编译报错 | `has_cap_v` + `static_assert` | 已有 |
| 零堆 / 零异常 / 零依赖 / 可裁剪 | 全库约定 + 开关宏 | 已有 |

## 2. 拍板结论（用户 2026-10-02 决定）

| 岔路口 | 选择 |
|---|---|
| 交付范围 | **只做参数解析 + 帮助/报错**（命令表 / 子命令与多源控制台留到后续阶段）|
| 落点与命名 | **新顶层目录 `ecli/`**，能力标签 **`Cli`**（与 elog / eserde 平级，依赖 eserde 基座）|
| 字段标签写法 | **clap 同名多标签**：`short` / `long` / `pos` / `required` / `help` / `skip` |
| 多输入源 | **库只给零件**：`line_reader` + `tokenize` + `parse` + 帮助/报错文本；谁 poll、谁开缓冲由调用方主循环写 |
| 首个用例 | **桌面 / 宿主工具（argv）**，但同时把串口 / 蓝牙那条路留成同一套（解析器不认识 argv）|

## 3. 交付物（阶段一）

| 文件 | 内容 |
|---|---|
| `ecli/cli.hpp` | 词法（`tokenize` / `from_argv`）、行装配（`line_reader`）、规格推导（标签 → `option_view`）、解析（`parse`）、文本（`write_usage` / `write_help` / `write_error` + 宿主 `*_string`）|
| `tests/ecli_cli_check.cpp` | 114 项：标签规格编译期断言、argv 各种写法、一行文本词法、错误路径与出错位置、帮助/报错文本与截断语义、行装配器、整条链路 |
| `tests/ecli_cli_etl_check.cpp` | 12 项：`etl::string` 装不下报错（不静默截断）、`etl::vector` 塞满报 `too_many_values`、`etl::string_view` 零拷贝 |
| `tests/ecli_compile_fail_caps.cpp` | 反例：没标 `Cli` 就想解析 → 编译期报错「这个类型不能做命令行参数」|
| `tests/run_check.ps1` | 新增宿主 / 嵌入式 / ETL 三个 pass + 一条编译失败反例 |
| `sandbox/main.cpp` | 可跑示例：argv 解析 + 一行文本解析 + 帮助 + 报错（13/13 自检通过）|

## 4. API 速览

```cpp
E_FMT_DERIVE(struct args {
  [[efmt::arg(short, long, help = "verbose output")]]        bool verbose = false;
  [[efmt::arg(short = "o", long = "output")]]                const char *out = nullptr;
  [[efmt::arg(long = "tag")]]                                etl::vector<etl::string<8>, 4> tags;
  [[efmt::arg(pos = "1", required)]]                         etl::string<64> input;
  [[efmt::arg(skip)]]                                        int internal = 7;
}, Cli);

ecli::parse(argc, argv, a);                                  // 宿主
ecli::parse(line, a, scratch, sizeof(scratch), &info);        // 一行文本（scratch 由调用方管寿命）
ecli::parse(tokens, a, &info);                                // 已有 token 表

char text[512];
ecli::write_help<args>("app", "about", text, sizeof(text));
ecli::write_error<args>("app", e, info, text, sizeof(text));
```

语义咬定（与 `json::read_from` 一致）：**副本上解析、全成功才赋回**；未给的字段保持
默认成员初始化值；`-h` / `--help` 返回 `error::help_requested`（不是失败）。

## 5. 多输入源：零件分工

```
串口 / 蓝牙 / 键盘        ← 传输层：字节流（你自己的 HAL / 扫描 / 事件）
   ↓  line_reader<MaxLine>    每源一个实例（否则两路输入会串词）
一行文本
   ↓  tokenize(line, scratch)  引号 / 转义；无引号的 token 零拷贝指原文
token 表
   ↓  parse(tokens, args)      不重入、不分配、不阻塞
args + error_info
   ↓  write_error / write_help → 谁问的就回给谁
```

- 库**不提供**线程原语、不碰 HAL、不做源注册表（阶段一决定）。
- RTOS 场景：中断只往 SPSC 环形缓冲塞字节，解码与执行留在同一个任务里。
- 每个源一份行缓冲是硬要求；`parse` 与命令执行本身不重入（主循环按固定顺序 poll）。

## 6. 明确不做（本阶段）

shell 补全、env 回退、自定义 value_parser、`nargs` 多值、嵌套结构体分组选项、
选项名相似度建议（did you mean）、`--version`、子命令与命令表、多源控制台与权限分级。

## 7. 验证

```powershell
.\tests\run_check.ps1            # 含新增的 ecli 三个 pass + 编译失败反例
.\tests\run_check.ps1 -Size      # Flash / RAM 报告（ecli 的体积代价可对照）
```

- 宿主与嵌入式（`-DEFMT_ENABLE_HOSTED=0`）两种配置都跑；ETL 版单独一步（没有 ETL 时跳过）。
- 体积（Cortex-M4 / newlib-nano / `-Os` / 整程序 `--gc-sections`，`-Size` 三行读数）：
  基线 396 B → 只 `parse` **4808 B**（+4.4 KB）→ 再带 `write_help`/`write_error` **6576 B**（+6.2 KB）。
  细节与"每个参数类型一份 `run<T>` ≈ 1.2 KB"的说明见手册 8.2c。
- 顺带修掉两处设计问题（由测试逼出来的）：`parse(一行文本)` 的出错 token 曾指向函数内
  栈缓冲（悬垂）→ 改成"零拷贝快路径 + 调用方给 scratch"的重载；`tokenize` 把无引号
  长 token 也算进 scratch 限额 → 现在只有真去引号 / 反转义的 token 才占 scratch。
- 体积优化：整数溢出判定不再用 64 位除法（那会把 `__udivmoddi4` 720 B 拖进固件）。

## 8. 阶段二：命令表 + 子命令（已交付）

`ecli/command.hpp`：命令表是 `constexpr` 静态数组 `{name, help, invoke}`，
**命令名带空格就是子命令**，匹配用**最长 token 前缀**（`"wifi set x"` 命中 `"wifi set"` 而不是
`"wifi"`）—— 没有树、没有插值、没有 `new`。每个命令一个自包含 thunk
（由 `command_of<Args, Fn>()` 生成）：自己声明参数类型、自己 `parse`、自己把帮助/报错写回 reply。

- 处理函数签名统一 `void(const Args &, reply)`；reply 两指针（ctx + 写函数），
  提供 `reply_to<uart_write>()` / `buffer_reply` / `stdout_reply()` / `string_reply()` / 空 reply。
- 内置帮助：`help` / `-h` / `--help` / `?` 列命令表；`help wifi set`、`wifi set -h` 给该命令用法。
- 未知命令 → `error::unknown_command` + 命令表；命令内错误复用阶段一的 `write_error`。
- 测试 `tests/ecli_command_check.cpp` 52 项 × 宿主/嵌入式两配置；sandbox 示例 16/16。
- 体积（Cortex-M4 `-Size`）：命令表 + 2 条命令 **8836 B**（相对只 `parse` 多约 4.0 KB，
  主要是每条命令一份 thunk/parse 实例）。

## 9. 后续（等拍板）

1. **阶段三**：可选 `ecli/cli_console.hpp` —— 源注册表 + 轮询顺序 + 按源回复 +
   每源权限等级（如蓝牙只读）。
2. 版本号与发布：按工程习惯由你定（发布前不擅自改版本号、不推远端）。

## 9. 已知上限

- 单类型字段 ≤ `EFMT_DERIVE_MAX_FIELDS`（默认 16，上限 32）；单字段标签 ≤ 8。
- 选项匹配是线性扫描 constexpr 表（O(token × 字段)），嵌入式量级足够，不做哈希。
- `const char*` / `string_view` 字段是零拷贝别名：指向 argv 或 scratch，必须活得比
  参数结构体久（用带 scratch 的重载，别用只吃一行的便利版）。
- `parse(一行文本)` 便利版自带栈缓冲（约 `ECLI_MAX_TOKENS × 16 + ECLI_MAX_LINE` 字节），
  所以它故意不返回 `error_info`：需要出错 token 就用带 scratch 的重载。
