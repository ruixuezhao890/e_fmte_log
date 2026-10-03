# efmt / elog / ecli 沙盒

> **新手从这里开始**：[沙盒命令台：新手 10 分钟上手](../docs/SANDBOX-命令台上手指南.md)
> （怎么跑、敲什么、看到什么、怎么加一条自己的命令）。本文是配套的就地笔记：
> include 拓扑、宏开关总表、实测坑。

CLion 里 **File → Open** 选这个 `sandbox` 目录即可（里面有 `CMakeLists.txt`，CLion 会自己 configure）。
C++17；`main.cpp` 是一个**手玩命令台**：一行一条命令、回车执行，亲眼看着输出。
`tests/run_check.ps1` 会连它一起编、一起跑（跑的是 `--check` 冒烟，不进交互）。

## 三种跑法

```bash
./sandbox                 # 默认：进命令台（CLion 里按 Run 也是这条）
./sandbox num 42          # 一次性：argv 那条路，同一个命令表、同一个解析器
./sandbox --check         # 冒烟：把命令表当脚本跑一遍，只看有没有异常（run_check.ps1 用这条）
./sandbox --repl          # 与直接跑等价（管道喂命令时更清楚：cat cmds.txt | ./sandbox --repl）
```

## 命令台里敲什么

开局会把命令表和几条"照着敲"的例子一起打出来，`help` 随时再看一遍：

| 敲这个 | 看什么 |
|---|---|
| `num 42` / `num 0x2a` / `num -7` | 一个整数的七种看法：十进制 / 十六进制 / 二进制 / 补零 / 左中右对齐 |
| `fmt 你好` | 文本排版：对齐、居中、截断（精度按**字节**，一个汉字 3 字节） |
| `me` | 自定义类型的三种注册写法（AUTO / FIELDS / DERIVE）+ `{:#}` 多行 |
| `json` / `cbor` | 同一个 `person` 写→读一圈，顺带看两种格式的字节数（cbor 27 B vs json 43 B） |
| `log` / `log warn` / `log off` / `log trace` | 日志分级：换级别之后再看哪几行会被过滤（`[级别] [文件:行 函数]` 前缀也在这看） |
| `level 0` / `level 5` / `level 99` | **处理函数里直接用 matchit 的 `match` 表达式**（字面量 / 区间 `and_(_ >= 1, _ <= 9)` / 通配 `_`）；数字由命令名模式 `level :n` 捕获 |
| `net` / `net set mynet` | **两段式子命令**（名字带空格 = 子命令，最长前缀优先）：裸敲 `net` 走概览，`net set xxx` 被更长的那条接走，`xxx` 由模式段 `:ssid` 捕获；`net set`（少一段）会命中 `net` 并报 too many arguments |
| `echo hi` / `echo --upper hi` | 开关 + 位置参数 |
| `args -v -o a.bin --level 7 --tag net in.txt` | 完整选项集：bool 开关 / 短选项 / 取值 / 可重复 / 位置参数 |
| `help` / `help args` | 命令表 / 某条命令的 usage 与选项表（等价 `args -h`） |
| 故意敲错 | `num`（缺必填）、`num abc`（值不对）、`net set`（子命令少一段）、`nope`（未知命令）—— 报错都带 usage |
| `-V` | 版本（`version_requested`，版本行由 sandbox 自己打） |
| `quit` / `exit` / Ctrl+Z 回车 | 退出 |

> stdin 的字节是**逐字节**喂进 `ecli::line_reader` 的 —— 跟串口 / 蓝牙收到字节、攒够一行再解析
> 完全是同一条路径（所以 `--repl` 能直接吃管道脚本）；回话走 reply 通道，真机上把
> `create_logger` 的 sink 换成串口写函数就跟着回串口了（回复复用那条通道，见下），
> 想另指一路才用 `ecli::reply_to<uart_write>()` / `ecli::reply_to_sink(串口 sink)`。

## 输出走哪条路（本仓库的规矩）

**文本格式化一律 efmt，文本输出一律 elog**。sandbox 里分成三条路：

| 内容 | 怎么出 |
|---|---|
| 日志行（`log` 命令那五行、`--check` 的总结） | `ELOG_INFO` / `ELOG_WARN` / `ELOG_ERROR` —— 带级别与 `文件:行 函数` 前缀 |
| 命令的答复 / 命令台界面文字 | `ecli::reply_to_default_logger()`（= 复用 `main` 里 `create_logger` 绑的那条通道）—— **原样字节**，不加日志前缀、不按行截断 |
| 交互提示符 `"> "` | 原样字节 —— elog 每行都要补前缀和换行，做不了提示符 |

答复为什么不塞进 `ELOG_INFO`：`logger::log` 是**日志语义**（固定前缀 + 自己补换行 + 整行超过
`ELOG_MAX_RECORD_SIZE` 就整行丢弃），而 usage / help 是**多行整块文本**，长度上限是调用方的
缓冲区。elog 的 `sink` 才是它的"字节出口"，语义正好对上 —— `ecli/elog_reply.hpp` 就这一个职责。
（sandbox 里负责"efmt 格式化 → reply"的是 `replyf()`，4 行。）

## include 根怎么接的（clone 下来就能编）

代码里全是本库的目录约定形状 `<middleware/efmt/...>`（88 处）与 `<middleware/etl/...>`（23 处），
但仓库里**没有** `middleware/` 这一层，所以 CMake 在**构建目录**里现搭一个。最终只有两个 include 根：

| 路径 | 提供 |
|---|---|
| `<build>/middleware/`（CMake 自建） | `<middleware/efmt/...>`、`<middleware/etl/...>` |
| `..`（仓库根） | `<elog/elog.hpp>`、`<ecli/...>`、`<matchit/matchit.h>` |

自建的这一层优先用 **junction（Windows）/ symlink（其它平台）** —— 活链接，改了 `efmt/` 里的头文件
立刻生效；链接建不出来才退回复制，那时 configure 会打印"改了头文件要重跑 configure"。
不写进源码树、不需要管理员权限，`git clone` 之后直接 configure 就能编（以前它依赖
`tests/include/` 下由 `run_check.ps1` 临时建的 junction，而 `tests/include/` 被 `.gitignore` 忽略，
所以别人 clone 下来第一步就编不过）。

**ETL 是外部依赖**（本仓库不带），CMake 依次找：`-DETL_ROOT=` → 环境变量 `EFMT_ETL_INCLUDE`
→ 相邻目录 `../etl-master/include`、`../etl/include`、`third_party/etl/include` → 都没有就在 configure
期明确报错（给出下载地址和两条可抄的命令），不留到编译期。两种写法都收：指 `etl/` 的父目录，
或直接指 `etl/` 本身。

> ETL 自带 `string.h` 等与系统头同名的头：**别把 `etl/` 当 include 根**（会遮蔽 `<cstring>`，
> MinGW 实测 include 链崩掉）。`-DETL_ROOT` / `EFMT_ETL_INCLUDE` 只用来**定位目录**，不会被塞进 `-I`；
> 代码里一律写 `<middleware/etl/...>`。

`tests/run_check.ps1` 走的是另一条路：它用 `tests/include/` 下的两个 junction（只在本机存在，
`.gitignore` 忽略），所以那边的 `-EtlInclude` / `EFMT_ETL_INCLUDE` 指 **`etl/` 本身**。
要删那种 junction 只能用 `cmd /c rmdir <路径>`（**不带** `/s`，带 `/s` 会跟着进目标目录把内容删掉）。

## 跑

CLion 右上角选 `sandbox` 目标直接 Run。命令行等价（`ETL 位置` 那行只在 ETL 不在相邻目录时才需要）：

```bash
cd <仓库根>
cmake -S sandbox -B build -G Ninja -DETL_ROOT=<...>/etl-master/include
cmake --build build
./build/sandbox          # Windows: .\build\sandbox.exe
```

## 想试的开关

在 CLion 的 **Settings → CMake → CMake options** 里加：

- `-DEFMT_DERIVE_SHOW_TYPE=1`（默认）→ 推导输出带类型名：`point { x = 3, y = 4 }`、枚举 `state::x`；想省 Flash 就 `-DEFMT_DERIVE_SHOW_TYPE=0` 看 `{ x = 3, y = 4 }`

其它开关直接写在 `main.cpp` 顶部 `#define`（都能用 `-D` 覆盖，定义见 `efmt/core/format_base.hpp`）：

| 宏 | 默认 | 作用 |
|---|---|---|
| `EFMT_ENABLE_FLOAT` | 1 | 0 = 完全不编浮点通道 |
| `EFMT_ENABLE_CONTAINER_FORMAT` | 宿主 1 | 0 = 不格式化 vector/map/tuple |
| `EFMT_ENABLE_DYNAMIC_STRING` | 宿主 1 | 0 = 不支持 std::string 参数 |
| `EFMT_ENABLE_ANSI_STYLES` | 宿主 1 | 颜色转义（`main.cpp` 里已关成 0） |
| `EFMT_DERIVE_STYLE_MULTILINE` | 1 | `{:#}` 多行缩进；关它省 ~15 B，`{:#}` 退化为单行 |
| `EFMT_DERIVE_ENABLE_CAPS` | 1 | 0 = 不登记能力标签（`E_FMT_DERIVE(decl, Debug, Serialize)` 的标签位） |
| `EFMT_DERIVE_ENABLE_SCHEMA` | 1 | 0 = 不生成 schema 原料（`eserde` 依赖它） |
| `EFMT_DERIVE_ENABLE_TAGS` | 1 | 0 = 不解析 `[[efmt::arg(...)]]` 字段标签 |
| `ECLI_MAX_TOKENS` | 16 | 一条命令最多几个 token |
| `ECLI_MAX_LINE` | 192 | 去引号缓冲 / `line_reader` 行宽 |
| `ECLI_ENABLE_HELP` | 1 | 0 = 裁掉 usage/help 文本（省 Flash） |
| `ECLI_REPLY_BUFFER` | 384（`main.cpp` 里设成 768） | 命令表帮助/报错的栈缓冲 —— 命令多、帮助是中文时会顶到上限，桌面不抠这点栈 |
| `ELOG_MAX_RECORD_SIZE` | 384 | 单条日志的记录缓冲 |
| `ELOG_MAX_LOGGERS` | 8 | logger 槽位数 |

## 上手就踩得到的几个点（已实测）

- **bool 打出来是 `1`/`0`**，不是 `true`/`false`（嵌入式省 Flash 的取舍）。
- `E_FMT_DERIVE` 默认**不带类型名**：`{ p = {x=3, y=4}, ts = 9 }`；要 `frame { ... }` 就开上面的开关。
- 几种注册写法可混用，但不能对**同一类型**重复注册（会重定义 `efmt_derive_format`）。
- `E_FMT_DERIVE` **至少要有一个字段**（空结构体解析不出来）。无参命令共用一个带
  `[[efmt::arg(skip)]]` 占位字段的参数类型即可（见 `main.cpp` 的 `no_args`）。
- 字段类型名里**不能有顶层逗号**：`etl::vector<int, 8>` 得先 `using` 一个别名，或换成 `std::vector<int>`。
- 参数里的**裸 `const char*` 是零拷贝指向输入缓冲**：argv 里每个 token 自带结尾 `0`，打出来正好；
  而"一行文本"里的 token 只是视图，当 C 字符串打会一直读到行尾 —— 这条路请用 `etl::string<N>` /
  `std::string`（装不下会如实报 `value_too_long`，不静默截断）。
- `elog` 直接调 `logger->info(...)` 时位置信息是 `<unknown>:0 <unknown>`；要 `文件:行 函数` 前缀
  就用 `ELOG_INFO(...)` 宏（或 `log_at` 传 `ELOG_SOURCE_LOCATION`）。
- `multi_sink` 按指针保存 `user_data`：用它创建的 logger 不能活得比这个 `multi_sink` 对象长。
- 级别被过滤掉的消息**直接返回**：不输出、也不报错（`set_level` 之后就这个行为）。
