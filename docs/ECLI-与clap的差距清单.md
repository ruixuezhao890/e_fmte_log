# ECLI 与 Rust clap 的差距清单

> 2026-10-02 逐条对照 clap 4 整理。**第一批、第二批已实现并验证**；第三批留在本文，
> 等你拍板再动。对照口径：clap 的 derive API（`#[derive(Parser)]` / `#[arg(...)]`）。
> 相关实现：`ecli/cli.hpp`（解析）、`ecli/command.hpp`（命令表）；说明见手册 5.10 / 5.11。

## 0. 一句话结论

**主干已对齐**（声明即推导、长短选项、位置参数、必填、默认值、类型取值、重复项、子命令、
帮助/报错、`--` 与负数）；差距分三类：**参数关系约束**、**取值形态边角**、**周边工具链**。
第三类基本是宿主工具打磨，对嵌入式没意义。

## 1. 已对齐（不用再想）

| 能力 | clap 写法 | ecli 写法 |
|---|---|---|
| 声明即推导 | `#[derive(Parser)]` | `E_FMT_DERIVE(struct args{...}, Cli)` |
| 短 / 长选项、自定义名 | `short` / `long = "x"` | `short` / `long = "x"` |
| 位置参数 | 声明顺序 / `index` | `pos`（裸 = 声明顺序）/ `pos = "2"` |
| 必填 | `required` | `required` |
| 帮助文本 | `help = "…"` | `help = "…"` |
| 默认值 | `default_value` / `default_value_t` | C++ 默认成员初始化（编译期零成本）|
| 类型取值 | `value_parser!` / `FromStr` / `ValueEnum` | 按字段真实 C++ 类型分派（整数含 `0x`/`0b`、浮点、枚举名或数字）|
| 重复选项 → 列表 | `ArgAction::Append` + `Vec` | 容器字段自动可重复 |
| `--` 之后全是位置参数 | ✓ | ✓ |
| 负数不当选项 | 需 `allow_negative_numbers` | 默认就认 |
| 子命令 | 树、可嵌套 | 扁平表 + **最长前缀**（旧写法：名字带空格即任意层级）；**嵌套 struct + `std::variant` 槽 + matchit 消费**（新写法，1.0 起：与 clap derive 同构，见 3.7.1）|
| 自定义输入源 | `get_matches_from(iter)` | 天生如此（token 表：argv / 串口一行 / 环形缓冲）|
| 不退出、返回错误 | `try_get_matches` | 只返回错误码，从不 exit |
| `-h` / `--help` | ✓（exit 0） | ✓（返回 `help_requested`）|

两条我们**默认比 clap 宽松**：`--no-flag`（clap 要用 `ArgAction::SetFalse` 另声明一个参数）
与 `--flag=false`（clap 的 flag 默认不收值）。

## 2. 第一批：已实现（小件，日常手感）

| 能力 | clap | ecli 写法 | 语义 / 边界 |
|---|---|---|---|
| 版本 | `#[command(version)]` → `-V/--version` | `-V` / `--version` → `error::version_requested` | 库不猜你的版本号：`write_version(app, version, buf, cap)` / 宿主 `version_string()`；命令表顶层同样认 |
| 别名 | `alias` / `short_alias` | `alias = "outfile"` / `short_alias = "F"` | 隐藏（不出现在帮助里），与 clap 的 `alias` 语义一致；`visible_alias` 未做 |
| 计数开关 | `ArgAction::Count` | `count`（标在整数字段上） | `-vvv` = 3、`--verbose --verbose` = 2；饱和自增不绕回；`--no-x` 对 count 不适用 |
| 值分隔 | `value_delimiter` | `delim = ","`（标在容器字段） | `--tag=a,b,c` → 3 项；空项如实保留（`a,,b` → 中间是空串）|
| 尾随参数 | `trailing_var_arg` / `allow_hyphen_values` | `trailing`（可重复位置参数）/ `hyphen`（某一项取值允许 `-` 开头） | `trailing` 出现后余下 token 全归它（`-x` 也算值）|
| Optional 语义 | `Option<T>`：给了才是 `Some` | `std::optional<T>` / `etl::optional<T>` 字段 | 没给保持空；给了才写入；内层支持标量 / 枚举 / 字符串 |

## 3. 第二批：已实现（关系约束，一套 `seen` 位图）

| 能力 | clap | ecli 写法 | 语义 |
|---|---|---|---|
| 依赖 | `requires` | `needs = "a"` | 给了本项就必须也给 a（**改名原因：`requires` 是 C++20 关键字**，属性里写它会触发 `-Wc++20-compat`）|
| 互斥 | `conflicts_with` | `conflicts = "m"` | 两者不能同时给（可重复标注多个）|
| 条件必填 | `required_unless_present` | `unless = "automatic"` | 本项必填，除非 automatic 给了 |
| 互斥组 | `ArgGroup`（`multiple = false`）| `group = "net"` | 同组字段最多给一个 |
| 至少一个 | `ArgGroup`（`required = true`）| `group_any = "src"` | 同组至少要给一个 |

实现要点（为什么几乎不花 Flash）：

- 关系标签在**编译期**解析成 `uint32` 位掩码（`requires_mask` / `conflicts_mask` /
  `unless_mask`），运行时只跟 `seen` 位图按位与 —— **零字符串表、零额外查表**。
- 引用的名字：**字段名或长选项名 / 别名都认**（`needs = "a"` 与 `needs = "long-name"` 等价）。
- 名字写错、选项名撞车（long / alias / short / short_alias 两两不同）都是**编译期报错**。
- 校验顺序：按字段声明顺序，未给的先查必填（`required` / `unless` / `group_any`），
  给了的再查互斥与依赖 —— 一次只报第一个问题。
- 新错误码：`conflict` / `missing_dependency` / `version_requested`；
  报错文本形如 `'--b' requires '--a'` / `'--y' conflicts with '--x'`。

## 4. 第三批：**待你拍板，未实现**

| # | 能力 | clap | 我们这边的代价与影响 |
|---|---|---|---|
| 1 | `did you mean` 建议 | `--verbos` → 建议 `--verbose`（Jaro-Winkler，`suggestions` 默认开）| 中：几百字节的朴素启发式（前缀 + 长度差）可裁剪；交互提升明显，嵌入式也能用 |
| 2 | shell 补全 | `clap_complete`（独立 crate：bash/zsh/fish/pwsh/elvish）| 中大：纯字符串拼接、零依赖可做；**只对宿主工具有价值**，嵌入式无关 |
| 3 | 环境变量回退 | `env = "APP_LEVEL"`（`env` feature，默认不开）| 小：需要 `getenv`，仅宿主；要做就得配一个裁剪开关 |

## 5. 故意不做（设计取舍，不是遗漏）

| 能力 | 为什么不 |
|---|---|
| 彩色帮助 / 终端宽度折行 / `help_template` | 串口里颜色是噪音；`wrap_help` 要探测终端宽度，库里不该有平台分支 |
| man 手册生成（`clap_mangen`）| 与 MCU / RTOS 目标无关 |
| 非 UTF-8 参数（`OsString`）| 我们没有 OS 抽象：token 就是 `string_view` |
| 运行时 `Command::mut_*` / `ignore_errors` | 我们全部编译期推导，运行时零表 |
| `String` 型错误对象 | 我们返回错误码 + 写进调用方缓冲（零堆） |
| 响应文件 `@args.txt` | clap 本体也没有内建（生态里是 `argfile`）；嵌入式更不需要 |
| builder API（`Command::new().arg()`）| 只有 derive 风格；改配置就是改代码 |
| `num_args(1..)` / `default_missing_value` / `require_equals` / `overrides_with` / `long_help` / `hide` / `display_order` / `global` / `multicall` | 边角需求；真要用时按需再加（每条都是几十行量级）|

## 6. ecli 比 clap 更贴合本场景的地方

1. **命令名模式段**：`"wifi set :ssid"` / `"log *rest"` 能从命令名里直接抽参数并注入同名字段
   （匹配交给 [matchit.cpp](https://github.com/BowenFu/matchit.cpp) 的 extractor）；
   clap 的子命令是静态树，命令名本身不承载参数。
   （嵌套 struct 的子命令名是推导出来的，不走模式段；要模式段就用扁平表写法。）
2. **一份命令表吃所有输入源**：argv、串口一行文本、蓝牙、键盘（clap 绑进程 argv，
   `get_matches_from` 只能喂字符串）。
2. **零堆、零异常、无 panic/exit**；clap 会分配、会 `exit`。
3. **编译期全解析**：标签 → 规格表 → 关系掩码都在编译期算完，运行时零字符串解析。
4. **能力门禁**：缺 `Cli` 标签直接编译报错。
5. **定长类型不静默截断**：`etl::string<N>` / `etl::vector<T,N>` 装不下 / 塞满都是显式错误。
6. **可裁剪 + 有体积读数**：`ECLI_*` 开关，手册 8.2c 有 Cortex-M4 实测。

## 7. 附：全部字段标签（一张表）

| 标签 | 形式 | 作用 |
|---|---|---|
| `short` / `long` | 裸 | 用字段名当短 / 长选项（`-l` / `--level`）|
| `short = "o"` / `long = "output"` | 带值 | 指定名字 |
| `alias = "outfile"` / `short_alias = "F"` | 带值 | 隐藏别名（帮助里不显示）|
| `pos` / `pos = "2"` | 裸 / 带值 | 位置参数（裸 = 按声明顺序编号）|
| `required` | 裸 | 必填 |
| `help = "…"` | 带值 | 帮助文本 |
| `skip` | 裸 | 不进命令行（内部字段 / 不支持的类型）|
| `command` | 裸 | 子命令槽：字段类型是 `std::variant`（首备选 = 注册表，见 3.7.1）|
| `count` | 裸 | 计数开关（整数字段，`-vvv`）|
| `delim = ","` | 带值 | 容器取值按分隔符切分 |
| `trailing` | 裸 | 可重复位置参数：出现后余下 token 全归它（含 `-x`）|
| `hyphen` | 裸 | 这一项的取值允许以 `-` 开头 |
| `needs = "b"` | 带值 | 依赖（clap `requires`）|
| `conflicts = "b"` | 带值 | 互斥（clap `conflicts_with`）|
| `unless = "b"` | 带值 | 本项必填除非 b 给了（clap `required_unless_present`）|
| `group = "g"` | 带值 | 同组最多给一个（clap `ArgGroup(multiple = false)`）|
| `group_any = "g"` | 带值 | 同组至少给一个（clap `ArgGroup(required = true)`）|

> 注意：`EFMT_DERIVE_MAX_TAGS` 默认 8 —— 一个字段同时用 `long + alias + short + short_alias +
> help + required + needs + conflicts` 就正好 8 个，再加就得 `-DEFMT_DERIVE_MAX_TAGS=10`。
> 关系标签（`needs` / `conflicts` / `unless`）可以重复标注多次。

## 8. 验证状态

- `tests/ecli_cli_extra_check.cpp`：39 项 × 宿主 / 嵌入式两配置（第一、二批全覆盖）
- 反例：`tests/ecli_compile_fail_names.cpp`（选项名撞车）、
  `tests/ecli_compile_fail_relation.cpp`（关系标签引用不存在的字段）
- 存量：`ecli_cli_check` 114 / 111、`ecli_command_check` 52 / 50、`ecli_cli_etl_check` 16
- 子命令 struct 化：`tests/ecli_subcommand_check.cpp`（解析层 static_assert + cli 集成 +
  matchit 消费 + 未选哨兵），体积读数见手册 3.10（6976 B，比扁平命令表更省）
- 全量：`tests/run_check.ps1 -Size`（含上述全部步骤与体积读数）
