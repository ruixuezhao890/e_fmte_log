# efmt / elog 沙盒

CLion 里 **File → Open** 选这个 `sandbox` 目录即可（里面有 `CMakeLists.txt`，CLion 会自己 configure 并生成 `cmake-build-*`）。
C++17；`main.cpp` 是一次功能走查：efmt 打印 → eserde 基座 → JSON → **CBOR** → elog × ETL → **ecli 命令行 / 命令表**，
自带一组自检（跑完打印 `N/N checks passed`，N = `check()` 的条数；全过才返回 0）。
`tests/run_check.ps1` 会连它一起编、一起跑（沙盒也是库的消费者），接口漂了这里会先红。

## include 根怎么接的

CMakeLists 里加了三条，前两条和 `tests/run_check.ps1` 用的完全一致，第三条是 ETL 显式依赖：

| 路径 | 提供 |
|---|---|
| `../tests/include` | `<middleware/efmt/...>`、`<middleware/etl/...>` |
| `..` | `<elog/elog.hpp>` |
| `ETL_ROOT`（CMake cache 变量） | `<etl/...>`（ETL 真实头文件**父目录**，即包含 `etl/` 子目录的那一层） |

`tests/include/middleware/efmt` 是指向 `efmt/` 的 junction，`middleware/etl` 指向外部 etl-master 的 `include/etl`。
**别删这两个 junction**；真要删只能用 `cmd /c rmdir <路径>`（不带 `/s`，带 `/s` 会跟着进目标目录把内容删掉）。

**ETL 位置变了**：`cmake -DETL_ROOT=新路径`（CLion：Settings → CMake → CMake options），
或重做 junction（先 `cmd /c rmdir tests\include\middleware\etl`，再 `mklink /J ...`，见 `tests/run_check.ps1`）。
注意 ETL_ROOT 要指到 `etl/` 的**父目录**（如 `etl-master/include`）：ETL 自带 `string.h` 等与系统头同名的头，
把 `etl/` 本身加进 include 路径会遮蔽 `<cstring>` 等系统头（MinGW 实测 include 链崩掉）。

`main.cpp` 中段是序列化走查：`E_FMT_DERIVE(struct person { ... }, Debug, Serialize, Deserialize)`
—— **能力标签是门禁**（写要 `Serialize`、读要 `Deserialize`，缺了是编译期报错，嵌套成员同样要写）——
然后 JSON 写读一圈、CBOR 写读一圈（二进制，同一个 person：CBOR 49 B vs JSON 63 B）。

末尾是 `elog × ETL 类型` 自检段：`etl::string` / `etl::vector`（含嵌套）/ `etl::optional`
直接格式化，并演示 ETL 类型进 elog 日志（elog 对用户默认打开容器格式，MCU 上打 `etl::vector` 开箱即用）。

末尾是 `ecli 命令行解析` 自检段：同一份 `E_FMT_DERIVE(struct cli_args { ... }, Cli)` 声明，
既吃宿主 `argv`（`-vo dump.bin --level=7 input.txt`），也吃设备端的"一行文本"
（`--level 9 "in put.txt"`，带引号与空格）—— 解析器不认识 argv，只认识 token 表；
顺带打印自动生成的帮助（usage + 选项表）与报错文本（含 usage）。

再末尾是 `ecli 命令表` 自检段：一张 `constexpr` 表（`status` / 模式命令 `wifi set :ssid` / `level :n` / `echo`），
依次分发 `status -v` / `wifi set -s mynet` / `wifi set`（必填缺失）/ `help wifi set` / `nope`（未知命令），
回复用 `buffer_reply` 收下来做断言（真实项目里换成 `reply_to<uart_write>()` 就回给串口），
`$ 命令 [状态]` 这类报告行本身走 `ELOG_INFO`。

## 输出走哪条路（本仓库的规矩）

**文本格式化一律 efmt，文本输出一律 elog**。sandbox 里分成三条路：

| 内容 | 怎么出 |
|---|---|
| 日志行 / 自检行（`section` / `check` / 结果 / 命令状态） | `ELOG_INFO` / `ELOG_ERROR` —— 带级别与 `文件:行 函数` 前缀（失败行是 error 级），默认 logger 在 `main` 开头就建好 |
| 命令回复（usage / help / 报错 / 命令自己回的话） | `ecli::elog_stdout_reply()`（= `ecli::reply_to_sink(elog 的 stdout sink)`）—— **原样字节**，不加日志前缀、不按行截断 |
| 交互提示符 `"> "` | 原样字节（`std::fputs`）—— elog 每行都要补前缀和换行，做不了提示符 |

回复为什么不塞进 `ELOG_INFO`：`logger::log` 是**日志语义**（固定前缀 + 自己补换行 + 整行超过
`ELOG_MAX_RECORD_SIZE` 就整行丢弃），而 usage / help 是**多行整块文本**，长度上限是调用方的
缓冲区。elog 的 `sink` 才是它的"字节出口"，语义正好对上 —— `ecli/elog_reply.hpp` 就这一个职责。

## 亲手敲命令验收（ecli 命令台）

```bash
./sandbox                 # 直接跑 = 进命令台（CLion 里按 Run 也是这条），开局会列出命令表
./sandbox --check         # 只跑自动自检（run_check.ps1 用这条，免得测试卡在等人输入）
./sandbox --repl          # 与直接跑等价（管道喂命令脚本时写出来更清楚）：
                          #   cat cmds.txt | ./sandbox --repl
```

提示符下**一行一条命令、回车执行**，参数空格分开（`--opt=value` 与 `--opt value` 都行）：

| 敲这个 | 看什么 |
|---|---|
| `status` / `status -v` | 单命令与短选项 |
| `wifi set -s mynet -p pw` | 子命令（名字带空格 = 最长前缀匹配）+ 长短选项混用 |
| `level 0` / `level 5` / `level 99` | **直接在处理函数里用 matchit 的 `match` 表达式**（字面量 / 区间 `and_(_ >= 1, _ <= 9)` / 通配 `_`）三种分支；数字本身由命令名模式 `level :n` 捕获 |
| `echo --upper hello` / `echo "hello world"` | 开关 + 位置参数 / 引号包空格 |
| `help` / `help wifi set` | 命令表 / 某命令的 usage 与选项表（等价 `wifi set -h`）|
| `nope` / `wifi set` / `status --wat` / `echo "abc` | 未知命令 / 缺必填 / 未知选项 / 引号没闭合 —— 报错都带 usage |
| `-V` | 版本（`version_requested`，版本行由 sandbox 自己打）|
| `quit` / `exit` / Ctrl+Z 回车 | 退出 |

一次性模式（宿主工具那条路，argv 直接分发）：`./sandbox status -v`、`./sandbox wifi set -s m -p p`。

> stdin 的字节是**逐字节**喂进 `ecli::line_reader` 的 —— 跟串口 / 蓝牙收到字节、攒够一行再解析
> 完全是同一条路径；回话走 reply 通道（这里的出口是 `ecli::elog_stdout_reply()`，真机上换成
> `ecli::reply_to<uart_write>()` 或 `ecli::reply_to_sink(串口 sink)`）。

## 跑

CLion 右上角选 `sandbox` 目标直接 Run。命令行等价：

```
cmake -S sandbox -B build -G Ninja
cmake --build build
./build/sandbox
```

## 想试的开关

在 CLion 的 **Settings → CMake → CMake options** 里加：

- `-DEFMT_DERIVE_SHOW_TYPE=1` → 推导输出带类型名：`point { x = 3, y = 4 }`（默认不带，Cortex-M4 上省约 1.35 KB Flash）

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
| `ECLI_REPLY_BUFFER` | 384 | 命令表帮助/报错的栈缓冲 |
| `ELOG_MAX_RECORD_SIZE` | 384 | 单条日志的记录缓冲 |
| `ELOG_MAX_LOGGERS` | 8 | logger 槽位数 |

## 上手就踩得到的几个点（已实测）

- **bool 打出来是 `1`/`0`**，不是 `true`/`false`（嵌入式省 Flash 的取舍）。
- `E_FMT_DERIVE` 默认**不带类型名**：`{ p = {x=3, y=4}, ts = 9 }`；要 `frame { ... }` 就开上面的开关。
- 四种注册写法可混用但不能对**同一类型**重复注册（会重定义 `efmt_derive_format`）。
- `elog` 直接调 `logger->try_info(...)` 时位置信息是 `<unknown>:0 <unknown>`；要 `文件:行 函数` 前缀就用 `ELOG_INFO(...)` 宏（或 `log_at` 传 `ELOG_SOURCE_LOCATION`）。
- `multi_sink` 按指针保存 `user_data`：用它创建的 logger 不能活得比这个 `multi_sink` 对象长。
- 等级过滤掉的消息仍返回成功（`void_result` 有值），不报错。
