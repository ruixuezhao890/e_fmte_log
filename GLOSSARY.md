# EFmt · ELog · ESerde · ECli

纯头文件的嵌入式 C++17 库族：格式化（`efmt/`）、分级日志（`elog/`）、序列化（`eserde/`）、命令行（`ecli/`）。
本文件描述它们如何分层、如何从"声明"推导出代码与 schema、以及输出往哪里走。只收本项目自己的概念，不收通用编程术语。

## Language

### 分层与依赖

**本体**：
`efmt/` 的核心：格式化与输出路由。零依赖，不认识 elog、eserde、ecli。
_Avoid_: 核心库、efmt 库、主库

**可选层**：
`elog/`、`eserde/`、`ecli/` 三个目录。每个都只单向依赖本体，不 include 就是零开销；三者之间也有固定依赖方向（`ecli` → `eserde/traits.hpp` → `eserde/serde.hpp` → `efmt`）。
_Avoid_: 外挂、插件、模块、拓展包

**基座**：
`eserde/` 整个目录（`serde.hpp` 的能力查询 + schema，`traits.hpp` 的形状判定 + 取值搬运）。各格式（json / cbor / cli）只构建在它上面，不重复判定。
_Avoid_: 底座、框架、基础设施、core

**冻结副本**：
原样入库的第三方代码（`matchit/`）。改动不做上游合并，只记进 `PATCHES.md`。
_Avoid_: vendored 库、第三方依赖、子模块

**单向依赖**：
上层认识下层，下层不认识上层。判断一个改动该放哪一层的唯一依据。
_Avoid_: 分层架构、依赖倒置

**零开销**：
"不 include / 不用某能力，就不产生代码或数据"。裁剪宏关闭路径后，体积与行为一字不变。
_Avoid_: 按需加载、惰性实例化（那是手段，不是概念）

### 声明与推导

**声明原文**：
`E_FMT_DERIVE` 的第一个参数：整段类型声明文本本身（不是类型名）。宏把它 `#` 成字符串喂给 schema 解析。
_Avoid_: 声明参数、类型参数、decl 文本

**推导**：
宏把声明原文变成代码的动作：字段类型由 `&Type::字段` 推出、显示名由 `#字段` 生成、解析器由字段类型推出。对标 Rust 的 `#[derive]`。
_Avoid_: 派生、自动派生、代码生成、反射

**编译期反射**：
在编译期问"这个类型有什么能力 / 哪些字段 / 带什么标签"的能力，由基座提供（`has_cap_v` / `field_count` / `field_name` / `find_by_tag`）。它消费推导的产物，本身不做推导。
_Avoid_: 反射、运行时反射、RTTI

**注册**：
类型被 `E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM` 登记过。判据是"能拿到它的声明原文"；结构体与枚举各有自己的入口。
_Avoid_: 登记、绑定、声明类型

**schema**：
从声明原文解析出来的字段名 / 类型名 / 标签 / 枚举取值表，全 `constexpr`，只在编译期存在一次。
_Avoid_: 元数据、描述表、反射信息

**能力标签**：
写在 `E_FMT_DERIVE(声明, Debug, Serialize)` 标签位上的类型：`Debug`（本体）、`Serialize` / `Deserialize`（eserde）、`Cli`（ecli）。本体只把它们原样登记，**含义由定义它的那一层给**。
_Avoid_: 特性、trait、标记、capability

**能力门禁**：
缺能力标签就在编译期报错的规则（`write_to` 要 `Serialize`、`parse` 要 `Cli`），报错信息指明该补哪个标签。
_Avoid_: 校验、检查、开关

**字段标签**：
写在字段上的 C++ 属性整体：`[[efmt::arg(short, long, help = "…")]]`、`json = "别名"`、`cbor = "skip"`。cli 与 json/cbor 共用同一套。
_Avoid_: 注解、字段属性、arg 标签

**标签键**：
字段标签里的一个键（`short` / `long` / `pos` / `help` / `json` / `cbor`）。schema 按 `tag_t` 存，按名反查用 `find_by_tag`。
_Avoid_: tag、key、标签项

**老宏**：
`E_FMT_FORMATTER_*` 家族（`FIELDS` / `ENUM` / `AUTO` / `AUTO_N` / `FN`）。靠 ADL 生效，必须写在**类型所在的命名空间**里；功能上仍可用，但新代码一律走 `E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM`（完全自定义输出仍用 `E_FMT_FORMATTER_FN`：它吃一个 lambda）。
_Avoid_: 旧宏、废弃宏、legacy

**类型内一行**：
`E_FMT_FIELDS(字段…)` 写在类型体里面的写法：只列字段名，类型 / 显示名 / 成员指针全自动。专供 `E_FMT_DERIVE` 覆盖不到的场合——声明里有 `#if`、模板结构体、字段数超上限、给已有类型补一行。
_Avoid_: 成员声明宏、内联宏

**裁剪宏**：
`EFMT_*` / `ELOG_*` / `ECLI_*` / `ESERDE_ENABLE_*` 开关。命令行 `-D` 先于头文件默认值，优先生效。
_Avoid_: 开关宏、配置宏、feature flag

### 形状与取值

**形状判定**：
只认成员函数（`data` / `size` / `begin` / `end` / `clear` / `push_back` / `max_size`）来判断一个类型"长什么样"，不认具体类型名——所以 std 容器与 ETL 容器走同一套代码。
_Avoid_: 类型探测、traits 判定、类型萃取

**取值搬运**：
从对象取值 / 往对象写值的共用助手。各格式只管"写进什么字节、从什么字节读"，"这个类型怎么取值"一律问它。
_Avoid_: 序列化助手、读写器、编解码

### 输出与回复

**通道**：
统称"输出往哪走"。可路由到 UART / RTT / ITM / SD 卡 / 缓冲区 / stdout；接上 elog 后只绑定一次。
_Avoid_: 管道、接口、总线、输出流

**输出处理器**：
本体的输出落点：一个 `(const char* data, size_t size)` 回调，`set_output_handler` 注册一次全局生效。
进程级单例，**谁改谁恢复**：临时改道用 `output_handler_scope`（栈式，退出自动还原），
缓冲输出写满标 `...(truncated)`。
_Avoid_: sink（本体层不用这个词）、handler、回调

**sink**：
elog 层的输出落点。一个 logger 一个 sink，可用 `make_sink` / `multi_sink` 组合。
_Avoid_: 输出处理器、通道、目标

**logger**：
elog 注册表里的一个命名日志器（名字 + 等级 + sink）。多个 logger 各自独立，运行期可用 `set_level` 改等级。
_Avoid_: 日志器实例、日志通道、log 对象

**一次绑定，多处使用**：
通道只在接 elog 时绑定一次，日志与命令回复共用同一条。命令回复走 `ecli::reply_to_logger` / `reply_to_default_logger`，不另接一路。
_Avoid_: 复用输出、共享 sink

**reply**：
ecli 的回复句柄：命令的输出回给"发起命令的那一路"——argv 来的回 stdout，串口来的回串口。由调用方选定，库不认识具体设备。
_Avoid_: 输出、回调、返回值

### 命令行

**命令表**：
一张零堆静态数组：`{名字, 帮助, 处理函数, 帮助函数}`（第四字段 `help_of<Args>()` 是帮助专用
thunk，`help <命令>` 直达；不写则退回借 `-h` 通道）。`dispatch` 只认 token 表与 reply。
_Avoid_: 注册表、路由表、命令树

**处理函数**：
每个命令一个自包含函数：自己声明参数类型、自己 parse、自己把帮助/报错写回 reply。
_Avoid_: thunk、handler、回调、命令实现

**模式段**：
命令名里的变长段：`:name`（吃一个 token 并捕获）、`*name`（吃余下全部）。
_Avoid_: 通配符、占位符、参数段

**段特异性优先**：
命令名匹配顺序：先比段特异性，再比 token 数。所以不需要树，也不需要 `new`。
_Avoid_: 最长匹配、优先匹配

**多输入源**：
argv / 串口 / 蓝牙 / 键盘各自一路。每路一个 `line_reader`（字节 → 一行）与自己的行缓冲，汇到同一个 tokenize → parse；解析与执行不重入，由调用方在主循环里串起来。
_Avoid_: 多路复用、输入复用

**命令台**：
`sandbox/` 里的手玩示例：跑起来、亲手敲命令、亲眼看输出。所有示例的默认形态。
_Avoid_: 控制台、REPL、演示程序、demo

**冒烟自检**：
给 CI 用的 `--check` 一条命令式检查，与命令台分开两条路。
_Avoid_: 单元测试、自测脚本

### 约定

**snprintf 语义**：
写缓冲区的统一约定：返回值 < size 表示完整写入；装不下如实报错（或追加 `...(truncated)`），绝不静默截断。
_Avoid_: 缓冲区约定、返回值约定
