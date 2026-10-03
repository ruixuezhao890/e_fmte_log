# ECLI 使用手册：命令行解析 + 命令表

> `ecli` 是这个仓库里"命令行 ↔ 结构体"那一层：把一个 C++ 结构体的声明直接推导成
> 解析器、帮助文本与命令表。对标 Rust [clap](https://docs.rs/clap) 的 derive 用法。
>
> **本手册里出现的每个代码块都在 [`tests/ecli_manual_examples.cpp`](../../tests/ecli_manual_examples.cpp)
> 里编译并跑过断言**（225 checks / 0 failures）。手册与实现脱节时，那个文件会先红 ——
> 这是本仓库的规矩，见 [测试文件头注释](../../tests/ecli_manual_examples.cpp)。
>
> 相关文档：
> [EFMT 使用手册 5.10 / 5.11 / 13.9](../EFMT-使用手册.md) ·
> [ECLI 与 clap 的差距清单](../ECLI-与clap的差距清单.md) ·
> [ECLI 命令行解析方案（设计取舍）](../ECLI-命令行解析-方案.md) ·
> [沙盒命令台上手指南](../SANDBOX-命令台上手指南.md) ·
> [`sandbox/README.md`](../../sandbox/README.md)

---

## 1. 30 秒：这是什么

| 你想干的事 | ecli 给你的东西 |
|---|---|
| 解析 argv（`myapp -v --level 7 in.txt`）| `ecli::parse(argc, argv, a)` |
| 解析串口 / 蓝牙 / 键盘敲进来的一行文本 | `ecli::line_reader` + `ecli::parse(line, a, scratch, size)` |
| 多命令 / 子命令（`wifi set -s mynet`）| `ecli::command` 表 + `ecli::dispatch(...)` |
| usage / help / 报错文本 | `ecli::write_usage / write_help / write_error`（snprintf 语义，写进你的缓冲区）|
| 命令的答复回给"发起命令的那一路" | `ecli::reply`（两个指针，零堆）|

三个头文件，按需 include：

```cpp
#include <ecli/cli.hpp>          // 解析 + 帮助/报错文本（只需 efmt + eserde 基座）
#include <ecli/command.hpp>      // 命令表 / 子命令 / 模式段（可选，构建在 cli.hpp 之上）
#include <ecli/elog_reply.hpp>   // 可选：命令回复走 elog 已经绑好的那条通道
```

前置条件与性格：

- **C++17**，零外部依赖（模式段那层用仓库自带的 `matchit/` 冻结副本，不用就 `-DECLI_ENABLE_PATTERN_COMMANDS=0` 裁掉）、**零堆、零异常、从不 exit**（全程返回错误码）。
- 参数类型由 `E_FMT_DERIVE(struct args { … }, Cli)` 推导 —— 和 `Serialize` / `Deserialize` 是同一套能力标签，
  **字段名一个字都不用写**（见 [EFMT 手册第 5 章](../EFMT-使用手册.md)）。
- 依赖方向是单向的：`ecli → eserde/traits.hpp → efmt`。efmt / elog / eserde **都不认识 ecli**；不 include 就是零开销。
- 解析器**不认识 argv**，只认识 token 表 —— 所以 argv、串口、蓝牙、键盘四种来源共用同一个解析器和同一张命令表。

和 clap 的关系：主干已对齐（声明即推导 / 长短选项 / 位置参数 / 必填 / 重复项 / 子命令 / `--` / 负数），
差距与"故意不做"的清单在 [ECLI-与clap的差距清单.md](../ECLI-与clap的差距清单.md)，本手册不重复抄。

---

## 2. 第一个能跑的例子

存成 `first_app.cpp`，编译运行：

```bash
# 仓库根目录下（include 根：tests/include 提供 <middleware/efmt/...>，仓库根本身提供 <ecli/...>）
g++ -std=c++17 -O2 -Itests/include -I. first_app.cpp -o first_app
./first_app -v --level 7 in.txt     # → verbose=1 level=7 input=in.txt
./first_app -h                      # → usage: app [options] [input] …  （退 0）
./first_app --level=abc             # → error: invalid value 'abc' for '--level' …（退 1）
```

```cpp
// first_app.cpp
#include <middleware/efmt/core/format.hpp>   // E_FMT_DERIVE 与 efmt
#include <middleware/etl/string.h>           // etl::string（嵌入式常用；宿主也可以换 std::string）
#include <ecli/cli.hpp>                      // 解析器

#include <cstdio>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE(struct args {
  [[efmt::arg(short, long, help = "verbose output")]]       bool verbose = false;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]] int level = 3;
  [[efmt::arg(pos = "1", help = "input file")]]             etl::string<32> input;
}, Cli);

int main(int argc, char **argv) {
  args a{};
  error_info info{};
  const error e = parse(argc, argv, a, &info);

  if (e == error::help_requested) {   // -h / --help：不是失败，打帮助后正常退出
    char text[512];
    write_help<args>("app", "我的第一个命令行工具", text, sizeof(text));
    std::printf("%s", text);
    return 0;
  }
  if (e != error::ok) {               // 真错：写进缓冲区，怎么输出由你决定
    char text[512];
    write_error<args>("app", e, info, text, sizeof(text));
    std::printf("%s", text);
    return 1;
  }
  std::printf("verbose=%d level=%d input=%s\n", a.verbose ? 1 : 0, a.level, a.input.c_str());
  return 0;
}
```

这段有几个"设计如此"的地方，先记住，后面会展开：

- `parse` **在副本上解析，全部成功才赋回**（与 `json::read_from` 一致）：失败时 `a` 一根毫毛不动。
- 没给的字段保持**结构体的默认成员初始化值**（这里 `level = 3`）—— 默认值不写在标签里。
- `-h` / `--help` / `-V` / `--version` **不是失败**：分别返回 `error::help_requested` / `error::version_requested`。
- 帮助与报错都是"写进你的缓冲区"（snprintf 语义）：库不 printf、不分配、不管你怎么输出。

> 本节示例由 [`tests/ecli_manual_examples.cpp`](../../tests/ecli_manual_examples.cpp) 编译验证
> （函数 `first_app_printf` 与上面逐字相同）。

---

## 3. 逐块讲清楚

### 3.1 声明即推导：`E_FMT_DERIVE(struct args { … }, Cli)`

一个能力标签 + 一行宏，解析器就知道了全部字段：

```cpp
E_FMT_DERIVE(struct args {
  [[efmt::arg(short, long, help = "verbose output")]]       bool verbose = false;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]] int level = 3;
  [[efmt::arg(pos = "1", help = "input file")]]             etl::string<32> input;
}, Cli);
```

- 第一个参数是**声明本身**，后面全是**能力标签**（`Cli` 在这一层有含义，efmt 只原样登记）。
- 没标 `Cli` 的类型不能当命令行参数 —— **编译期**就报错：

```text
./ecli/cli.hpp:1341:17: error: static assertion failed: 这个类型不能做命令行参数：缺 Cli 能力标签。
写成 E_FMT_DERIVE(struct args { ... }, Cli) 就能解析了
```

（反例源码：[tests/ecli_compile_fail_caps.cpp](../../tests/ecli_compile_fail_caps.cpp)）

- 推导出来的"规格表"是 `constexpr` 的，**编译期**就能查（运行时零字符串解析）：

```cpp
static_assert(is_cli_args_v<args>, "标了 Cli 才能解析");
static_assert(option_count<args>() == 3);
static_assert(option<args>(0).long_name == "verbose");
static_assert(option<args>(0).short_name == 'v');
static_assert(!option<args>(0).takes_value, "bool 是开关，不收取值");
static_assert(option<args>(2).position == 1, "pos = \"1\" 是第 1 个位置参数");

// 运行时也能查（同一张表）
option<args>(1).field;   // "level"   —— 字段名
option<args>(0).help;    // "verbose output"
```

`option<T>(i)` 返回的 `option_view`（见 [ecli/cli.hpp](../../ecli/cli.hpp)）里有什么：

| 成员 | 含义 |
|---|---|
| `field` | 字段名（帮助里显示、报错里定位）|
| `long_name` / `long_alias` | 长选项名 / `alias = "…"` 的隐藏别名（空 = 没有）|
| `short_name` / `short_alias` | 短选项字符 / `short_alias = "…"`（`0` = 没有）|
| `position` | 位置参数序号（1 起；`0` = 不是位置参数）|
| `help` / `required` / `skip` | `help = "…"` / `required` / `skip` |
| `takes_value` / `repeatable` / `count` / `trailing` / `hyphen` / `delim` | 取值形态（bool 与 `count` 不收值；容器可重复；`delim` 是切分字符）|
| `requires_mask` / `conflicts_mask` / `unless_mask` | 关系约束编译期化成位掩码（运行时只跟 `seen` 位图按位与）|

**和 clap 的对应**：

| clap（Rust）| ecli（C++17）|
|---|---|
| `#[derive(Parser)] struct Args { … }` | `E_FMT_DERIVE(struct args { … }, Cli)` |
| `#[arg(short, long, help = "…")]` | `[[efmt::arg(short, long, help = "…")]]` |
| `default_value_t = 3` | C++ 默认成员初始化 `int level = 3;`（编译期零成本）|
| `Option<T>` ↔ 是否给了 | `std::optional<T>` / `etl::optional<T>` 字段 |
| `Vec<T>` + `ArgAction::Append` | 容器字段（`std::vector` / `etl::vector`）自动可重复 |
| `Parser::parse()`（会 exit）| `ecli::parse(...)` 返回 `error`，**从不 exit** |

### 3.2 字段标签全表

标签写在字段声明的**前面或后面**都行，全部在编译期解析。

| 标签 | 形式 | 作用 |
|---|---|---|
| `short` / `long` | 裸 | 用字段名当短 / 长选项（`-l` / `--level`）|
| `short = "o"` / `long = "output"` | 带值 | 指定名字 |
| `alias = "outfile"` | 带值 | **隐藏长别名**（帮助里不显示；clap 的 `alias` 语义）|
| `short_alias = "F"` | 带值 | **隐藏短别名**（帮助里不显示）|
| `pos` / `pos = "2"` | 裸 / 带值 | 位置参数；裸 `pos` 按**声明顺序**编号 |
| `required` | 裸 | 必填（没给 → `missing_required`）|
| `help = "…"` | 带值 | 帮助文本（帮助表右栏）|
| `skip` | 裸 | 不进命令行（内部字段 / 类型不支持时的逃生门）|
| `count` | 裸 | 计数开关：`-vvv` → 3（标在整数字段上，饱和自增）|
| `delim = ","` | 带值 | 容器取值按分隔符切：`--tag=a,b,c` → 三项 |
| `trailing` | 裸 | 可重复位置参数：**一拿到第一个 token** 就把余下的全收走（连 `-x` 也算值）|
| `hyphen` | 裸 | 这一项的取值允许以 `-` 开头 |
| `needs = "b"` | 带值 | 依赖：给了本项就必须也给 b（clap `requires`；**`requires` 是 C++20 关键字，故改名**）|
| `conflicts = "b"` | 带值 | 互斥：两者不能同时给 |
| `unless = "b"` | 带值 | 本项必填，**除非** b 给了（clap `required_unless_present`）|
| `group = "g"` | 带值 | 同组字段最多给一个（互斥组）|
| `group_any = "g"` | 带值 | 同组字段至少给一个 |

**默认值不写标签**，就是 C++ 成员初始化：`int level = 3;`。

#### 一段代码看全（除关系约束外）

```cpp
E_FMT_DERIVE(struct tag_args {
  [[efmt::arg(short, long, count, help = "计数开关：-vvv = 3")]]   int verbose = 0;
  [[efmt::arg(short = "o", long = "output", help = "输出文件")]]   etl::string<16> out;
  [[efmt::arg(long = "name", alias = "moniker")]]                 etl::string<16> name;
  [[efmt::arg(short = "f", short_alias = "F", long = "file")]]    etl::string<16> file;
  [[efmt::arg(long = "tag", delim = ",", help = "a,b,c")]]        std::vector<std::string> tags;
  [[efmt::arg(long = "raw", hyphen, help = "取值可以以 - 开头")]]  etl::string<16> raw;
  [[efmt::arg(long = "mode", required, help = "必填项")]]          etl::string<8> mode;
  [[efmt::arg(pos = "1", help = "位置参数")]]                      etl::string<16> input;
  [[efmt::arg(skip)]]                                             int internal = 7;   // 不进命令行
}, Cli);
```

各种写法的效果（每一条都有断言）：

```cpp
tag_args a{};
parse("-vvv --mode m", a, scratch, sizeof(scratch));          // → a.verbose == 3（count 累加）
parse("--moniker x --mode m --output o", a, …);               // → a.name == "x"（隐藏长别名）
parse("-F f.txt --mode m", a, …);                             // → a.file == "f.txt"（隐藏短别名）
parse("--mode m --tag=a,b,c", a, …);                          // → a.tags == {"a","b","c"}
parse("--mode m --raw -weird", a, …);                         // → a.raw == "-weird"（hyphen 放行）
parse("in.txt", a, …);                                        // → missing_required（--mode 没给）
parse("--mode m --internal 5", a, …);                         // → unknown_option（skip 字段不是选项）
```

`count` 是**饱和自增**（到目标类型上限就不再涨，不绕回），`--no-x` 对 `count` 不适用。

别名只影响解析，**不出现在帮助里**；`skip` 字段也不出现：

```cpp
char buf[1024];
write_help<tag_args>("app", "tags", buf, sizeof(buf));
// buf 里有 "-v, --verbose" / "--output" / 有 help 文本
// buf 里没有 "moniker" / 没有 "-F" / 没有 "internal"
```

#### `trailing`：余下的 token 全归它

```cpp
// trailing 只能标在【可重复的位置参数】上，而且它一拿到第一个 token 就把余下的全收走
E_FMT_DERIVE(struct trail_args {
  [[efmt::arg(pos = "1", trailing, help = "余下 token 全收")]] std::vector<std::string> rest;
}, Cli);

trail_args t{};
parse("in.txt -x --not-an-option", t, scratch, sizeof(scratch));
// → t.rest == {"in.txt", "-x", "--not-an-option"}（连 -x 也当值收下）
```

#### 关系约束：`needs` / `conflicts` / `unless` / `group` / `group_any`

```cpp
E_FMT_DERIVE(struct rel_args {
  [[efmt::arg(long = "a", help = "基座")]]                             int a = 0;
  [[efmt::arg(long = "b", needs = "a", help = "依赖 --a")]]            int b = 0;
  [[efmt::arg(long = "x", group = "net")]]                             bool x = false;
  [[efmt::arg(long = "y", group = "net")]]                             bool y = false;
  [[efmt::arg(long = "p", group_any = "src")]]                         bool p = false;
  [[efmt::arg(long = "q", group_any = "src")]]                         bool q = false;
  [[efmt::arg(long = "cfg", unless = "automatic", help = "除非 auto")]] int cfg = 0;
  [[efmt::arg(long = "automatic")]]                                    bool auto_mode = false;
  [[efmt::arg(long = "k", conflicts = "m", help = "别和 --m 一起给")]]  bool k = false;
  [[efmt::arg(long = "m")]]                                            bool m = false;
}, Cli);
```

| 敲什么 | 结果 |
|---|---|
| `--b 5` | `missing_dependency`（`--b` 依赖 `--a`），报错文本 `'--b' requires '--a'` |
| `--a 1 --b 5 --automatic --p` | `ok` |
| `--k --m --automatic --p` | `conflict`，文本 `'--k' conflicts with '--m'` |
| `--x --y --automatic --p` | `conflict`（`group = "net"` 同组互斥）|
| `--automatic` | `missing_required`（`group_any = "src"` 一个都没给）|
| `--p` | `missing_required`（`unless = "automatic"`，而 `--automatic` 没给）|
| `--p --automatic` / `--p --cfg 3` | `ok`（前者靠自动项豁免，后者自己给了）|

要点：

- 关系标签里的名字**字段名或长选项名都认**（`needs = "a"` 与 `needs = "long-name"` 等价），可以重复标注多个。
- 编译期化成 `uint32` 位掩码（`requires_mask` / `conflicts_mask` / `unless_mask`），运行时只跟 `seen` 位图按位与 —— 零字符串表。
- 校验顺序：**按字段声明顺序**，未给的先查必填（`required` / `unless` / `group_any`），给了的再查互斥与依赖；**一次只报第一个问题**。
- 名字写错（`needs = "nosuchfield"`）与选项名撞车（两个字段抢同一个 `long`）都是**编译期报错**。

> 本节示例由 [`tests/ecli_manual_examples.cpp`](../../tests/ecli_manual_examples.cpp) 的 `check_tags()` 编译验证。

### 3.3 取值类型全表

按字段的**真实 C++ 类型**分派（与 eserde 的 json / cbor 共用同一套取值助手）：

| 字段类型 | 行为 |
|---|---|
| `bool` | 开关：`--flag` / `--no-flag` / `--flag=false`（也认 `on/off`、`yes/no`、`1/0`）|
| 各种整数 | 十进制 / `0x` 十六进制 / `0b` 二进制；允许前导 `+/-`；溢出、负数进无符号 → `invalid_value` |
| `float` / `double` | `1.5` / `-1.5e2`；**不认 `inf` / `nan`**（只认字面量）|
| 注册过的枚举（`E_FMT_DERIVE_ENUM`）| 按取值名 `--mode slow`；也收数字 `--mode 5`；认不出 → `invalid_value` |
| `char[N]` | 拷贝进数组，自动补 `\0`；装不下 → `value_too_long`（**不静默截断**）|
| `std::string` / `etl::string<N>` | 拷贝；`etl::string` 装不下 → `value_too_long`（它的 `assign` 本身会静默截断，CLI 先问容量挡掉）|
| `const char*` / `std::string_view` / `etl::string_view` | **零拷贝别名**：指向 argv 或调用方的 scratch（寿命见第 5 节）|
| 可增长容器（`std::vector` / `etl::vector`）| 重复选项依次 push；定长容器满了 → `too_many_values`；配 `delim` 可一次给多项 |
| `std::optional<T>` / `etl::optional<T>` | **给了才是 Some**；没给保持空（clap `Option<T>` 的语义）；内层支持标量 / 枚举 / 字符串 |
| 其它类型 | 编译期报错，除非标 `skip` |

```cpp
E_FMT_DERIVE_ENUM(enum class mode { fast, slow = 5, err = -1 });

// 类型名里的顶层逗号会把 E_FMT_DERIVE 的第一个参数切断 —— 先 typedef 消掉
using int2 = etl::vector<int, 2>;

E_FMT_DERIVE(struct type_args {
  [[efmt::arg(long = "flag")]]  bool flag = false;               // 开关
  [[efmt::arg(long = "dec")]]   int dec = 0;                     // 十进制 / 0x / 0b / 负数
  [[efmt::arg(long = "hex")]]   unsigned hex = 0;
  [[efmt::arg(long = "mode")]]  mode m = mode::fast;             // 枚举名或数字
  [[efmt::arg(long = "ratio")]] double ratio = 0.0;              // 1.5 / -1.5e2
  [[efmt::arg(long = "name")]]  char name[8] = "";               // 定长字符数组（拷贝）
  [[efmt::arg(long = "text")]]  std::string text;                // 动态字符串
  [[efmt::arg(long = "fixed")]] etl::string<8> fixed;            // 定长字符串（装不下报错）
  [[efmt::arg(long = "view")]]  std::string_view view;           // 零拷贝别名
  [[efmt::arg(long = "opt")]]   std::optional<int> opt;          // 给了才是 Some
  [[efmt::arg(long = "eopt")]]  etl::optional<int> eopt;
  [[efmt::arg(long = "tag")]]   std::vector<std::string> tags;   // 可重复
  [[efmt::arg(long = "id")]]    int2 ids;                        // etl::vector<int, 2>
  [[efmt::arg(pos = "1")]]      etl::string_view first;          // 位置参数也能零拷贝
}, Cli);
```

常见写法与结果：

```cpp
const char *argv[] = {"app",  "--flag", "--dec", "0x10", "--hex",  "0b101",
                      "--mode", "slow",  "--ratio", "-1.5e2", "in"};
parse(11, argv, a);              // → flag=true, dec=16, hex=5, m=mode::slow, ratio=-150.0, first="in"

parse_line("--name abc --fixed fix --tag x --tag y --id 3 --id 4", a);   // 拷贝 / 重复 / 定长容器
parse_line("--flag=false", a);   // → flag=false（开关也能取值）
parse_line("--no-flag", a);      // → flag=false（--no-<flag> 关掉开关）
parse_line("--mode 5", a);       // → mode::slow（枚举也收数字）

parse_line("--name 12345678", a);        // → value_too_long（char[8] 装不下 8 个字符 + \0）
parse_line("--fixed 123456789", a);      // → value_too_long（etl::string<8>）
parse_line("--id 1 --id 2 --id 3", a);   // → too_many_values（etl::vector<int,2> 塞满）
parse_line("--dec 99999999999", a);      // → invalid_value（溢出）
parse_line("--hex=-1", a);               // → invalid_value（负数进无符号）
parse_line("--mode turbo", a);           // → invalid_value（枚举认不出）

parse_line("--opt 7 --eopt 9", a);       // → *a.opt == 7、*a.eopt == 9
```

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_value_types()` 编译验证；
> ETL 类型的对照检查在 [tests/ecli_cli_etl_check.cpp](../../tests/ecli_cli_etl_check.cpp)。

### 3.4 命令行语法

**argv 这条路**（宿主工具）：

| 写法 | 例子 |
|---|---|
| 长选项带值 | `--level 7` / `--level=7` |
| 短选项带值 | `-l 7` / `-l7` / `-l=7` |
| 短选项聚簇 | `-vl 7`（= `-v -l 7`）、`-vo out.txt` |
| `--no-<flag>` | `--no-verbose`（把 bool 置假）|
| 开关取值 | `--verbose=false` |
| 结束选项扫描 | `--` 之后**全是位置参数**（连 `--level=1` 也当值）|
| 负数 | `-3` / `-1.5e2` **不会被当成选项**（`-` 后面是数字或 `.`）|

```cpp
{ const char *argv[] = {"app", "-vl", "7", "in.txt"};  parse(4, argv, a); }  // -v -l 7 → level=7
{ const char *argv[] = {"app", "-l=9", "in.txt"};      parse(3, argv, a); }  // -l=9 去等号 → 9
{ const char *argv[] = {"app", "-l7", "in.txt"};       parse(3, argv, a); }  // 粘连取值 → 7
{ const char *argv[] = {"app", "--level", "-3", "in"}; parse(4, argv, a); }  // 负数当值 → -3
{ const char *argv[] = {"app", "--", "--level=1"};     parse(3, argv, a); }  // → input="--level=1"，level 仍是 3
```

**一行文本这条路**（串口 / 蓝牙 / 键盘 / 测试）：空白切分；单引号与双引号都能包住空格；
反斜杠转义 `\n \t \r \0`（其余字符取它本身，所以 `\ ` 就是空格）：

```cpp
parse_line("-v \"in put.txt\"", a);   // → input == "in put.txt"（引号里的空格算一个 token）
parse_line("in\\ put.txt", a);        // → input == "in put.txt"（反斜杠转义空格）
parse_line("-v \"abc", a);            // → error::bad_quote（引号没闭合）
```

两条路的词法差别只有这些：argv 里每个 token 自带结尾 `0`，一行文本里的 token 只是视图
（这条差别引出一个大坑，见 [5.2](#52-裸-const-char-在一行文本里会读到行缓冲外面)）。

### 3.5 帮助 / 版本 / 报错

四个"写文本"的函数（**snprintf 语义**：返回完整长度，能写多少写多少，永远 NUL 结尾；
`buf = nullptr` 时只量长度）：

```cpp
char buf[1024];

write_usage<args>("app", buf, sizeof(buf));              // "usage: app [options] [input]"
write_help<args>("app", "我的工具", buf, sizeof(buf));     // about + usage + 选项表 + 内置 -h/--help
write_error<args>("app", e, info, buf, sizeof(buf));     // "error: …" + usage（一条消息一行）
write_version("app", "1.2.3", buf, sizeof(buf));         // "app 1.2.3\n"（版本号由你给，库不猜）

const std::size_t need = write_usage<args>("app", nullptr, 0);   // 先量长度，再决定缓冲区多大
```

宿主还有四个返回 `std::string` 的便利版（`EFMT_ENABLE_DYNAMIC_STRING` 打开时才有，即宿主默认有）：

```cpp
std::string s = usage_string<args>("app");
std::string h = help_string<args>("app", "我的工具");
std::string x = error_string<args>("app", e, info);
std::string v = version_string("app", "1.2.3");     // "app 1.2.3\n"
```

帮助文本长什么样（`write_help<args>("app", "我的工具", …)` 的实际内容）：

```text
我的工具

usage: app [options] [input]

options:
  -v, --verbose        verbose output
  -l, --level <value>  0..9
  [input]              input file
  -h, --help           show this help
```

左栏宽度按**最长的一项**算（内置 `-h, --help` 至少占 12 列）；`skip` 字段与隐藏别名都不出现。

**行为与错误码**：`-h` / `--help` → `error::help_requested`；`-V` / `--version` → `error::version_requested`；
两者都**不算失败**，字段也不被改动（调用方打帮助/版本，正常退出 —— 和 clap 打完帮助退 0 一个意思）。
注意：如果某个字段自己占了 `-h` / `--help` / `-V` / `--version` 这个名字，它归字段，不再是内置行为。

错误码全表（`ecli/cli.hpp` 的 `enum class error`，配 `error_name()` 打印）：

| 错误码 | 什么时候 | `write_error` 的文本形状 |
|---|---|---|
| `ok` | 成功 | — |
| `help_requested` | `-h` / `--help` | `error: help requested` |
| `version_requested` | `-V` / `--version` | `error: version requested` |
| `unknown_option` | `--foo` / `-x` 没有这个选项 | `error: unknown option '--foo'` |
| `missing_value` | `--out` 后面没有取值 | `error: '--out' needs a value` |
| `invalid_value` | 取值转换失败（不是数字 / 装不下 / 枚举认不出）| `error: invalid value 'abc' for '--level'` |
| `value_too_long` | 字符串目标装不下（**不静默截断**）| `error: value '…' is too long for '--name'` |
| `missing_required` | `required` / `unless` / `group_any` 没满足 | `error: missing required option '--ssid'` |
| `too_many_args` | 位置参数多给了 | `error: too many arguments: 'extra'` |
| `too_many_values` | 可重复选项把定长容器塞满了 | `error: too many values for '--tag'` |
| `too_many_tokens` | token 数超 `ECLI_MAX_TOKENS`，或去引号后超 `ECLI_MAX_LINE` | `error: command line too long (…)` |
| `bad_quote` | 一行文本里引号没闭合 | `error: unterminated quote` |
| `unknown_command` | 命令表里没有这个命令 | `error: unknown command 'nope'` + 命令表 |
| `conflict` | `conflicts` / `group` 同时给 | `error: '--k' conflicts with '--m'` |
| `missing_dependency` | `needs` 的依赖没给 | `error: '--b' requires '--a'` |

报错文本里指代选项的写法：有长名用 `--long`，否则 `-s`，位置参数用 `<字段名>`。

`error_info` 带你定位到出错的 token 与字段：

```cpp
error_info info{};
const error e = parse("--level=abc", a, scratch, sizeof(scratch), &info);
info.token;         // "abc"     —— 出问题的 token（选项名或取值）
info.index;         // 0 起的 token 下标
info.option_index;  // 字段在 schema 里的下标（npos = 没有）
info.other_index;   // conflict / missing_dependency 里的"另一项"
```

**`dispatch` 从不 exit、从不抛**：它把所有问题变成**返回的错误码** + 一段写进 reply 的文本，
调用方想怎么处理就怎么处理（比如 argv 工具退 1、串口只回一句话继续跑）。

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_texts()` 编译验证。

---

### 3.6 三个入口：argv / 一行文本 / token 表

解析器只认识 token 表（`token_list`），三个入口最后都汇到同一个 `parse(tokens, out, info)`：

```cpp
// 入口一：argv（零拷贝指向 argv）
const char *argv[] = {"app", "--level", "9", "in.txt"};
const token_list tokens = from_argv(4, argv);     // 跳过 argv[0]
args a{};
parse(tokens, a);

// 入口二：一行文本 + 调用方给的 scratch
char scratch[ECLI_MAX_LINE];
const token_list t2 = tokenize("-v --level=8 \"in put.txt\"", scratch, sizeof(scratch));
error_info info{};
parse(t2, a, &info);      // t2.count == 3，引号里的空格算一个 token

// 入口三：一步到位（内部帮你 tokenize）
parse("--level 6 in.txt", a, scratch, sizeof(scratch), &info);   // 有 info，scratch 你管寿命
parse("--level 6 in.txt", a);                                    // 便利版：自带栈缓冲，没有 info

// 宿主主入口的简写
parse(argc, argv, a, &info);
```

三步的分工（**库只给零件，传输层是你的事**）：

```text
串口 / 蓝牙 / 键盘        ← 你自己的 HAL / 扫描 / 事件（库不碰）
   ↓  line_reader<MaxLine>      每源一个实例（共用一块缓冲会让两路输入串词）
一行文本
   ↓  tokenize(line, scratch)   引号 / 转义；无引号的 token 零拷贝指原文
token 表
   ↓  parse(tokens, args)
args + error_info
   ↓  write_error / write_help → 谁问的就回给谁
```

`line_reader` 的行为（见 [ecli/cli.hpp](../../ecli/cli.hpp)）：

```cpp
line_reader<128> rx;          // 模板参数是行宽上限，默认 ECLI_MAX_LINE
bool ready = rx.put('a');     // 逐字节喂；返回 true = 攒够一行
rx.line();                    // 到齐的那一行（string_view，指内部缓冲：再 put 之前用掉）
rx.overflow();                // 这一行超长了（超长部分被丢掉，但你能看见）
rx.clear();                   // 收完这一行，开始下一行（也可以直接继续 put）
```

- 只认 `\r` `\n` `\r\n`（CRLF 只算一次）与退格（`\b` / `0x7F`）。
- **空行不产生命令**（`put` 返回 false）。
- 超长置 `overflow()`，那一次解析会返回 `too_many_tokens`。

上限：一条命令最多 `ECLI_MAX_TOKENS`（默认 16）个 token，超了报 `too_many_tokens`，**绝不静默丢参数**。

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_entries()` 编译验证。

### 3.7 命令表：`command` / `command_of` / `help_of` / `dispatch`

命令表是一个**零堆的 `constexpr` 数组**，每项四个字段：`{名字, 帮助, 处理函数 thunk, 帮助 thunk}`。
**名字带空格就是子命令** —— 没有树、没有插值、没有 `new`。

```cpp
E_FMT_DERIVE(struct status_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct wifi_args {
  [[efmt::arg(short = "s", long = "ssid", required, help = "network name")]] etl::string<16> ssid;
  [[efmt::arg(short = "p", long = "pass", help = "password")]]               etl::string<16> pass;
}, Cli);

static void status_run(const status_args &a, reply out) {
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void wifi_set_run(const wifi_args &a, reply out) {
  // …干活，需要回话就 out.put(…)
  out.put_lit("ok\n");
}

static constexpr command kCommands[] = {
    {"status", "show link status", command_of<status_args, status_run>(), help_of<status_args>()},
    {"wifi", "wifi summary", command_of<status_args, wifi_summary_run>(), help_of<status_args>()},
    {"wifi set", "set ssid / password", command_of<wifi_args, wifi_set_run>(), help_of<wifi_args>()},
};
```

- 处理函数签名两种，按你写的那个自动选：
  **`void(const Args &, reply)`** 或 **`void(const Args &, const params &, reply)`**（想看模式段捕获的原始值时用后者）。
- `reply` 参数不用就**留空名字**（写 `reply` 而不是 `out`），免得 `-Wunused-parameter`。
- 命令自己声明参数类型、自己 `parse`、自己把帮助/报错写回 reply —— 这一整套由 `command_of<Args, Fn>()` 生成的 thunk 包办。
- **第四字段 `help_of<Args>()` 是帮助专用 thunk**（`help <命令>` 直达，见下）：它不解析参数、不注入捕获、**不执行处理函数**。忘写也没事 —— 聚合初始化自动补 `nullptr`，`help <命令>` 退回借 `-h` 通道的旧路径，行为不变（新写建议带上）。

三个分发入口，同一个表：

```cpp
char scratch[ECLI_MAX_LINE];
dispatch(kCommands, "wifi set -s mynet -p secret", scratch, sizeof(scratch), out);  // 一行文本
dispatch(kCommands, tokens, out);                                                    // 已有 token 表
dispatch(kCommands, argc, argv, out);                                                // 宿主 argv
```

**匹配规则：最长前缀优先。** `wifi` 与 `wifi set` 同时在表里时：

```cpp
dispatch(kCommands, "wifi", out);                  // → 命中 "wifi"（概览）
dispatch(kCommands, "wifi set -s n1", out);        // → 命中 "wifi set"（子命令）
```

命令内部出错时，回复里是该命令自己的 usage：

```text
error: missing required option '--ssid'

usage: wifi set [options]
```

**内置帮助（保留词，命令表里别用这些名字）**：`help` / `-h` / `--help` / `?` 列命令表；
`help wifi set` 或 `wifi set -h` 给出该命令的 usage + 选项表（都不执行处理函数）。
`help <命令>` 走命令自己的 **`help_of<Args>()` 帮助 thunk**（命令表第四字段，v1.11 之后加入）：直达帮助文本，
连参数解析都跳过 —— 就算该命令的参数类型真的声明了 `-h` 选项，帮助请求也不会被误当成
真选项解析、更不会执行处理函数。旧代码不写第四字段时自动退回借 `-h` 通道的路径，行为不变。
`-V` / `--version` 返回 `error::version_requested`，版本行由你打。

```cpp
dispatch(kCommands, "", out);                // 空行 = 列命令（返回 help_requested，不算失败）
dispatch(kCommands, "help", out);            // 同上
dispatch(kCommands, "help wifi set", out);   // 该子命令的 usage + 选项表
dispatch(kCommands, "-V", out);              // version_requested
dispatch(kCommands, "nope", out);            // unknown_command + 命令表
```

单独把命令表列出来（自己选回哪条通道）：

```cpp
char buf[512];
buffer_reply b{buf, sizeof(buf), 0};
write_command_list(kCommands, b.as_reply());     // "commands:\n  status  show link status\n  …"
```

未知命令的回复长这样（带整张表）：

```text
error: unknown command 'nope'

commands:
  status          show link status
  wifi            wifi summary
  wifi set        set ssid / password
  help [command]  show this help
```

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_command_table()` / `check_builtin_help()` 编译验证。

### 3.8 命令名里的模式段：`:参数` / `*余下`

命令名可以写**模式段**，捕获到的值会**按名字注入到参数结构体的同名字段**：

| 写法 | 含义 |
|---|---|
| `wifi` | 字面量段：必须与 token 相等 |
| `:ssid` | 参数段：吃掉一个 token，按名字 `ssid` 注入同名字段（标量 / 字符串 / 枚举 / 整数）|
| `*rest` | 余下段：吃掉余下**全部** token（可为 0 个），只能放在末尾；注入**容器字段**（逐个 push）|

```cpp
// 无参命令的占位类型：E_FMT_DERIVE 至少要有一个字段，skip 让它不进命令行
E_FMT_DERIVE(struct no_args {
  [[efmt::arg(skip)]] int unused = 0;
}, Cli);

E_FMT_DERIVE(struct set_args {
  [[efmt::arg(skip)]] int n = -1;              // 由 "level :n" 捕获注入（捕获名 = 字段名）
}, Cli);

E_FMT_DERIVE(struct log_args {
  [[efmt::arg(skip)]]         std::vector<std::string> rest;   // 由 "log *rest" 逐个 push
  [[efmt::arg(long = "tag")]] etl::string<16> tag;
}, Cli);

E_FMT_DERIVE(struct ssid_args {
  [[efmt::arg(skip)]] etl::string<16> ssid;    // 由 "net set :ssid" 捕获注入
}, Cli);

static void level_run(const set_args &a, reply out) {
  // a.n 已经是捕获到的数字
  out.put_lit("ok\n");
}

// 想看原始捕获值就用三参数形式
static void log_run(const log_args &a, const params &p, reply out) {
  for (const std::string &s : a.rest) { /* … */ }
  p.count;                     // 捕获项数（= p.size()）
  p.size();                    // 同上
  params::capacity;            // 上限（= ECLI_MAX_CAPTURES）
  p.overflow;                  // 捕获项超上限了
  p.spec;                      // 本次匹配吃掉的【非 * 段】数（排序用）
  p.name_at(0);                // 第 0 个捕获的名字（"rest"）
  p.value_at(0);               // :name = 该 token；*name = 收下的第一个 token（可能空）
  p.is_rest(0);                // 是不是 *name
  p.rest_count(0);             // *name 收了几个
  p.rest_at(0, 1);             // *name 的第 1 个
  p.get("rest", "(无)");        // 按名字取（带兜底值）
  p.has("tag");                // 有没有这个捕获
  out.put_lit("done\n");
}

static constexpr command kPatternCommands[] = {
    {"level :n", "设置等级", command_of<set_args, level_run>()},
    {"log *rest", "记录一行", command_of<log_args, log_run>()},
    {"net", "网络概览（兜底）", command_of<no_args, net_run>()},
    {"net set :ssid", "设置 SSID", command_of<ssid_args, net_set_run>()},
};
```

`params` 的每个取值口（这一段在测试里逐个钉过）：

| 成员 | 含义 |
|---|---|
| `count` / `size()` | 捕获项数 |
| `capacity` | 上限（= `ECLI_MAX_CAPTURES`）|
| `overflow` | 捕获项超上限了（不静默丢，但值不全）|
| `spec` | 本次匹配吃掉的**非 `*` 段**数（分发排序用）|
| `name_at(i)` / `value_at(i)` | 第 i 个捕获的名字 / 值（`:name` 是该 token，`*name` 是收下的第一个 token）|
| `is_rest(i)` | 第 i 个是不是 `*name` |
| `rest_count(i)` / `rest_at(i, k)` | `*name` 收了几个 / 第 k 个 |
| `has(name)` / `get(name, fallback)` | 按名字查 / 按名字取 |

敲下去的对应关系：

| 敲什么 | 结果 |
|---|---|
| `level 5` | 命中 `level :n` → `a.n == 5` |
| `level abc` | `invalid_value`（注入到 int 时转不过去），回复带 `usage: level :n` |
| `log a b c` | `log *rest` → `a.rest == {"a","b","c"}`；`p.size() == 1`、`p.rest_count(0) == 3` |
| `log` | 同上，收 0 个也算命中（`p.rest_count(0) == 0`）|
| `net set home` | `net set :ssid` → `a.ssid == "home"` |
| `net` | 命中更短的 `net`（兜底）|
| `net set` | **段数不够**：`net set :ssid` 要 3 段，落到 `net`，多出的 token 报 `too_many_args` |
| `help net set` | help 是**前缀匹配**：能摸到 `net set :ssid` 并打它的 usage |

规则细读：

- **匹配排序**：段特异性优先（`*` 段不算特异性），再比吃掉的 token 数。所以 `net set :ssid`
  赢过 `net`，`sensor read` 赢过 `sensor *rest` —— catch-all 不会盖掉更具体的命令。
  同一前缀下 `x :n`（特异性 2）也会抢走 `x *rest`（特异性 1）的输入 ——
  想让两条这类命令共存，就给它们**各自不同的前缀**（测试里的探针命令就是 `prest *rest` / `pnum :n`）。
- **注入规则**：字段名相同才注入；没有同名字段**不报错**（值仍在 `params` 里）。
  `*name` **只注入容器字段**，标量字段不注入（免得"最后一个赢"这种意外）。
  同名既是选项又是捕获时，**捕获后写**（捕获赢）—— 所以专门喂捕获的字段建议标 `skip`。
- **段数要够**：`wifi set`（两段）**不会**命中 `wifi set :ssid`。想让半截输入也有友好提示，
  就在表里再补一条 `{"wifi set", …}`。
- 匹配本体是第三方 [matchit.cpp](../../matchit/README.md)（冻结副本在 `matchit/`）：
  我们只把"一个 token ↔ 一个段模式"的判定交给它（extractor + `some` + 通配 `_`），不自己写模式解释器。
  **别用它的 `Id<T>` 接捕获值** —— 那存的是指针，出了 match 表达式就悬垂（我们踩过）。

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_pattern_commands()` 编译验证；
> 更细的边界（裁剪版行为）见 [tests/ecli_pattern_check.cpp](../../tests/ecli_pattern_check.cpp)。

### 3.9 回复通道全表与"一次绑定，多处使用"

`reply` 就是**两个指针**（上下文 + 写函数），零堆、可拷贝、可为空（空 = 丢弃输出）：

| 你怎么造它 | 出口 | 什么时候用 |
|---|---|---|
| `reply_to<uart_write>()` | 你的 `void(const char*, std::size_t)` 写函数 | 嵌入式最常用（不依赖 elog）|
| `buffer_reply b{buf, sizeof(buf), 0}` + `b.as_reply()` | 定长缓冲（snprintf 语义，永远 NUL 结尾）| 测试 / 小缓冲 |
| `reply{}` | 丢弃 | 只跑副作用的命令 |
| `stdout_reply()` | stdout（`EFMT_ENABLE_STDIO`）| 宿主调试 |
| `string_reply(s)` | 追加到 `std::string`（`EFMT_ENABLE_DYNAMIC_STRING`）| 宿主 / 测试 |
| `reply_to_logger(*lg)` | elog 的 sink（**首选**，复用日志已绑好的通道）| 有 logger 的时候 |
| `reply_to_default_logger()` | 默认 logger 的 sink | logger 指针不在手边 |
| `reply_to_sink(elog 的 sink)` | elog 的输出后端（回复仍原样）| 手上有裸 sink |
| `elog_stdout_reply()` | elog 的 stdout sink（内部持静态 sink，不会踩临时对象）| 只想回 stdout |

```cpp
// 1) 裸写函数：库直接调你的 (data, size)
dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to<raw_write>());

// 2) 定长缓冲
char buf[512];
buffer_reply b{buf, sizeof(buf), 0};
dispatch(kCommands, "status -v", scratch, sizeof(scratch), b.as_reply());   // b.used = 完整长度

// 3) 丢弃输出（命令照跑）
dispatch(kCommands, "status", scratch, sizeof(scratch), reply{});

// 4) 宿主
std::string out;
dispatch(kCommands, "wifi", scratch, sizeof(scratch), string_reply(out));
dispatch(kCommands, "wifi", scratch, sizeof(scratch), stdout_reply());
```

**回复通道在 `ecli/reply.hpp`**：`reply` 结构与上面全部适配器（含 `detail::send_text`，截断如实标记的
底层）都在那里；`command.hpp` 只负责命令表与分发，不重复定义。`ecli/elog_reply.hpp` 的
`reply_to_sink / reply_to_logger` 系列照旧（可选层，不 include 不占代码）。

**产文本 + 送达一条龙（`send_*`）**：`write_usage / write_help / write_error / write_version`
是"写进**调用方给的缓冲**"（snprintf 语义，按 `buf = nullptr` 量长度；嵌入式 / 精确尺寸用）。
`send_usage<T> / send_help<T> / send_error<T> / send_version`（`ecli/cli.hpp`）是它们的
"**一行送走**"版 —— 自持 `ECLI_REPLY_BUFFER` 栈缓冲（默认 384 B），装不下如实追加
`...(truncated)`，返回 snprintf 语义的**完整长度**（返回值 ≤ `ECLI_REPLY_BUFFER` 即完整送达；
超过说明截断，差多少就是返回值 - `ECLI_REPLY_BUFFER`）：

```cpp
// 宿主：一行拿 std::string（= send_* + string_reply 的薄封装，API 不变）
std::string usage = usage_string<Args>("app");
std::string help  = help_string<Args>("app", "我的工具");
std::string err   = error_string<Args>("app", e, info);
std::string ver   = version_string("app", "1.2.3");

// 嵌入式：一行回串口（版本号照旧由调用方给 —— 库不猜你的版本）
const std::size_t n = send_help<Args>("app", "我的工具", reply_to<uart_write>());
if (n > ECLI_REPLY_BUFFER) { /* 截断了：把 ECLI_REPLY_BUFFER 调大再发 */ }
```

**什么时候用 reply，什么时候用 ELOG_\***：本仓库的分工是"文本格式化一律 efmt，文本输出一律 elog"。

| 内容 | 走哪条 |
|---|---|
| 日志行 / 诊断（要级别、要来源前缀）| `ELOG_INFO` / `ELOG_WARN` / `ELOG_ERROR` |
| 命令回复（usage / help / 报错 / 命令自己回的话）| `reply` 通道 —— **原样字节** |

回复为什么不塞进 `ELOG_INFO`：`logger::log` 是**日志语义**（固定加 `[级别] [文件:行 函数] ` 前缀、
自己补换行、整行超 `ELOG_MAX_RECORD_SIZE` 就整行丢弃），而 usage / help 是**多行整块文本**，
上限是调用方的缓冲区。elog 的 `sink` 才是它的"字节出口" —— `ecli/elog_reply.hpp` 就这一个职责。

#### 一次绑定，多处使用

通道只在**一处**定义/绑定（`sink` = 你的 `(data, size)` 写函数），日志、命令回复、后续任何消费者
都**复用同一个已绑定对象**，不在第二处再绑一次：

```cpp
// ★ 唯一的绑定点：通道定义一次，挂到 logger 上
e_log::logger *app =
    e_log::create_logger("console", e_log::make_sink(&uart_write), e_log::level::debug);
e_log::set_default_logger(*app);              // ELOG_* 与 reply_to_default_logger() 都走它

ELOG_INFO("boot {}", 1);                      // 日志那一路：带级别与来源前缀
dispatch(kCommands, line, scratch, sizeof(scratch),
         reply_to_logger(*app));              // 回复那一路：原样字节，同一条通道
```

- 取用口是 `logger::output_sink()`（返回 `const sink&`）；`reply_to_logger(*lg)` 就是它的薄封装。
- **换物理出口只改 `create_logger` 那一个参数**，日志与命令回复一起跟着走。
- 绑的是 `multi_sink`（串口 + 蓝牙 + 屏，最多 4 路）时，回复也自动跟着扇出。
- **按源回复是另一回事**：`reply` 是每次 `dispatch` 的入参，所以"串口问的回串口、蓝牙问的回蓝牙"
  天然成立，与"一次绑定"不冲突（同一条通道照旧复用，跨通道按需现传）。
- **ecli 本体不认识 elog**：不 include `elog_reply.hpp` 时，用 `reply_to<uart_write>()` 直接写，零依赖零开销。

一个硬约束：`reply` 只存指针，**sink 必须比这次 `dispatch` 活得久**。
传临时 sink（`reply_to_sink(e_log::stdout_sink())`）会被**删除重载当场拦成编译错误**：

```text
tests/ecli_compile_fail_elog_temp.cpp:13:46: error: use of deleted function
'ecli::reply ecli::reply_to_sink(e_log::sink&&)'
```

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_reply_channels()` 编译验证；
> 更完整的说明见 [EFMT 手册 13.9](../EFMT-使用手册.md)。

### 3.10 裁剪宏与代价

| 宏 | 默认 | 作用 | 关掉/调小的代价 |
|---|---|---|---|
| `ECLI_MAX_TOKENS` | 16 | 一条命令行最多几个 token | 超了报 `too_many_tokens`（绝不静默丢参数）|
| `ECLI_MAX_LINE` | 192 | 去引号缓冲大小；也是 `line_reader` 的默认行宽 | 带引号/转义的长 token 装不下 → `too_many_tokens` |
| `ECLI_ENABLE_HELP` | 1 | usage / help 文本开关 | `=0`：整段裁掉（省 Flash），`-h` / `--help` 也不再特殊处理 |
| `ECLI_REPLY_BUFFER` | 384 | 命令表帮助/报错的**栈**缓冲 | 命令多、帮助长时会看到 `...(truncated)`（如实标记，不静默丢）|
| `ECLI_ENABLE_PATTERN_COMMANDS` | 1 | 命令名模式段（`:name` / `*rest`）| `=0`：不 include matchit，`:name` / `*name` 当字面量 |
| `ECLI_MAX_CAPTURES` | 8 | 一次命令最多记几个捕获（`params::capacity`）| 超了 `params::overflow = true` |

```cpp
// 手册 3.10：默认配置的值（真改了宏就得跟着改断言）
static_assert(ECLI_MAX_TOKENS == 16);
static_assert(ECLI_MAX_LINE == 192);
static_assert(ECLI_ENABLE_HELP == 1);
static_assert(ECLI_REPLY_BUFFER == 384);
static_assert(ECLI_ENABLE_PATTERN_COMMANDS == 1);
static_assert(ECLI_MAX_CAPTURES == 8);
```

体积数字**只引用实测**（Cortex-M4 / newlib-nano / `-Os` / 整程序 `--gc-sections`，
出自 [EFMT 手册 8.2c](../EFMT-使用手册.md) 与 [tests/ecli_size_probe.cpp](../../tests/ecli_size_probe.cpp)，
`tests/run_check.ps1 -Size` 会重新打印这几行）：

| 样例（同一份参数类型：`bool` + `const char*` + `int` + `char[32]`）| .text | 相对基线 |
|---|---|---|
| 只留声明与 schema（不调用解析）| 396 B | — |
| 调用一次 `parse`（词法 + 取值 + 匹配 + 关系约束）| 5200 B | +4.7 KB |
| 再带上 `write_help` / `write_error` | 7168 B | +6.6 KB |
| 命令表（2 条命令 + 一次 `dispatch`）| 10580 B | +10.2 KB |
| 同上 + `-DECLI_ENABLE_PATTERN_COMMANDS=0` | 9924 B | +9.5 KB |
| 1 条 `:param` 模式命令（含 matchit）| 7184 B | +6.8 KB |
| 命令表 + 回复改走 elog 的 sink | 10624 B | ↑ 只多 **44 B** |

几条读这份表的经验（细节见 8.2c）：

- 帮助/报错文本**不调用就不进固件**；`ECLI_ENABLE_HELP=0` 再省掉 `-h`/`--help` 的内置处理。
- **每个参数类型各一份实例**（`parse<T>` ≈ 1.2 KB 量级）：命令多时这是主要开销 ——
  想省就把多个命令**合并到同一个参数类型**上。
- 命令表框架（`dispatch` + 回复通道 + 命令列表）约 1.4 KB；剩下的是每条命令一份自包含 thunk。
- 参数结构体里**没有浮点字段就不会实例化浮点取值路径**。
- 不带 `ecli/elog_reply.hpp` 就是一行代码都不进固件；`reply_to_sink` 版只多 44 B、RAM 一分不涨。

---

## 4. 完整可抄示例：串口命令行骨架

一个真机上能落地的形状：**串口收字节 → `line_reader` 攒一行 → `dispatch` → 回复走
`reply_to_default_logger()`**；日志走 `ELOG_*`，回复走 reply，各是各的格式。
命令表、参数类型、处理函数与 [沙盒](../../sandbox/main.cpp) 里那套写法完全同源。

```cpp
// 假的串口：写函数收字节（真机上换成 HAL_UART_Transmit）
static std::string g_uart_tx;
static bool uart_write(const char *data, std::size_t size, void *) {
  g_uart_tx.append(data, size);
  return true;
}

E_FMT_DERIVE(struct dev_status_args {
  [[efmt::arg(short, long, help = "细节")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct dev_ssid_args {
  [[efmt::arg(skip)]] etl::string<16> ssid;   // 由 "net set :ssid" 捕获
}, Cli);

E_FMT_DERIVE(struct dev_level_args {
  [[efmt::arg(skip)]] int n = -1;             // 由 "level :n" 捕获
}, Cli);

E_FMT_DERIVE(struct dev_log_args {
  [[efmt::arg(pos = "1", help = "trace/debug/info/warn/error/off")]] etl::string<8> level;
}, Cli);

static void run_dev_status(const dev_status_args &a, reply out) {
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void run_dev_ssid(const dev_ssid_args &a, reply out) {
  ELOG_INFO("ssid 已切到 {}", a.ssid);   // 日志：带级别与来源前缀
  out.put_lit("ssid ok\n");              // 回复：原样字节
}

static void run_dev_level(const dev_level_args &a, reply out) {
  ELOG_INFO("level -> {}", a.n);
  out.put_lit("level ok\n");
}

static void run_dev_log(const dev_log_args &a, reply out) {
  e_log::logger *const lg = e_log::default_logger();
  if (lg == nullptr) {
    out.put_lit("no logger\n");
    return;
  }

  static const struct {
    const char *name;
    e_log::level value;
  } kLevels[] = {
      {"trace", e_log::level::trace}, {"debug", e_log::level::debug},
      {"info", e_log::level::info},   {"warn", e_log::level::warn},
      {"error", e_log::level::error}, {"off", e_log::level::off},
  };

  if (!a.level.empty()) {
    const e_log::level *found = nullptr;
    for (const auto &lv : kLevels) {
      if (a.level == lv.name) found = &lv.value;
    }
    if (found == nullptr) {
      out.put_lit("不认识的级别：trace/debug/info/warn/error/off\n");
      return;
    }
    lg->set_level(*found);
  }

  ELOG_DEBUG("debug 行：级别比它高就不会出来");
  ELOG_WARN("warn 行：级别 <= warn 就出来（现在是 {}）", e_log::to_string(lg->current_level()));
  out.put_lit("ok\n");
}

static constexpr command kConsole[] = {
    {"status", "链路状态", command_of<dev_status_args, run_dev_status>()},
    {"net set :ssid", "设置 SSID", command_of<dev_ssid_args, run_dev_ssid>()},
    {"level :n", "设置等级", command_of<dev_level_args, run_dev_level>()},
    {"log", "查看 / 切换日志级别", command_of<dev_log_args, run_dev_log>()},
};

static constexpr const char *kVersion = "0.1.0";

// 每个输入源一个行缓冲；解析与执行不重入，都在主循环里串着来
static line_reader<128> g_rx;
static char g_line_scratch[ECLI_MAX_LINE];
static error g_last_error = error::ok;

static void console_boot() {
  // ★ 唯一的绑定点：通道在这里定义一次，日志与命令回复都复用它
  e_log::logger *app =
      e_log::create_logger("console", e_log::make_sink(&uart_write), e_log::level::debug);
  if (app != nullptr) {
    e_log::set_default_logger(*app);   // ELOG_* 与 reply_to_default_logger() 都走它
  }
}

static void on_uart_byte(char c) {
  if (!g_rx.put(c)) return;                       // 还没凑够一行
  const std::string_view line = g_rx.line();
  const reply out = reply_to_default_logger();    // 谁问的回给谁（这里只有一条串口）
  g_last_error = dispatch(kConsole, line, g_line_scratch, sizeof(g_line_scratch), out);
  if (g_last_error == error::version_requested) {
    char v[64];
    const std::size_t n = write_version("console", kVersion, v, sizeof(v));
    out.put(std::string_view(v, n < sizeof(v) ? n : sizeof(v) - 1));
  }
  g_rx.clear();
}

// 测试里用这个喂字节；真机上把这一行换成 std::getchar() 循环（见下面）
static void feed(const char *bytes) {
  for (const char *p = bytes; *p != 0; ++p) on_uart_byte(*p);
}
```

真机上的"字节从哪来"只有一行差别（把 `feed()` 换成"从串口 / 键盘取一个字节"）：

```cpp
int main() {
  console_boot();
  for (int ch = std::getchar(); ch != EOF; ch = std::getchar()) {   // 串口中断 / RTOS 任务里同形状
    on_uart_byte(static_cast<char>(ch));
  }
  return 0;
}
```

> 这一小段是**取字节的适配**，不在示例测试里（示例测试用 `feed()` 喂固定字节）：
> 同形状的真代码在 [`sandbox/main.cpp`](../../sandbox/main.cpp) 的 `read_line()` 里，
> `run_check.ps1` 会编译并跑它。

跑起来会看到（同一路串口上，日志带前缀、回复原样）：

```text
> status -v
link: up (detail)
> net set home
[info] [<你的源文件>:52 run_dev_ssid] ssid 已切到 home
ssid ok
> log warn
[warn] [<你的源文件>:96 run_dev_log] warn 行：级别 <= warn 就出来（现在是 warn）
ok
> nope
error: unknown command 'nope'

commands:
  status          链路状态
  net set :ssid   设置 SSID
  level :n        设置等级
  log             查看 / 切换日志级别
  help [command]  show this help
```

要点：

- **输入**：每个源一个 `line_reader`；中断里只往环形缓冲塞字节，解码与执行留给同一个任务（库不提供线程原语、不碰 HAL）。
- **输出**：日志与回复**复用同一条通道**（`create_logger` 那一次绑定），换物理出口只改那一个参数。
- **错误**：`dispatch` 把错误码返回给你 —— 想分类处理（未知命令只提示、解析失败打错误）随手就能写。
- **想更省**：`reply_to<uart_write>()` 直接用裸写函数，连 elog 那层都不要。

> 本节示例由 `tests/ecli_manual_examples.cpp` 的 `check_console_skeleton()` 编译验证
> （测试用 `feed()` 喂字节，断言了回复原样、日志带前缀、`-V` 出版本行、未知命令带命令表、级别过滤生效）。
> 想手玩同款：[沙盒命令台](../SANDBOX-命令台上手指南.md) 第 9 节就是"把命令台换成串口"。

---

## 5. 坑与 FAQ

### 5.1 空结构体不行 —— 无参命令用 `no_args` 的 `skip` 占位

**现象**：编译报 `static assertion failed: E_FMT_DERIVE 的声明体是空的：结构体至少要有一个字段…`。

**原因**：`E_FMT_DERIVE` 从声明体里数字段，空声明体解析不出任何东西（与 ecli 无关，是基座的规矩）。

**怎么办**：放一个 `skip` 占位字段 —— 它不进命令行、不出现在帮助里：

```cpp
E_FMT_DERIVE(struct no_args {
  [[efmt::arg(skip)]] int unused = 0;
}, Cli);
```

### 5.2 裸 `const char*` 在一行文本里会读到行缓冲外面

**现象**：argv 那条路用 `const char*` 一直好好的；换成串口的一行文本后，字符串尾巴上多出垃圾字符。

**原因**：token 是 `string_view`。**argv 里每个 token 自带结尾 `0`**，所以 `const char*` 指向它就是合法 C 字符串；
而一行文本的 token 指向 `line_reader` 的内部缓冲（或你的 scratch），那里**没有结尾 `0`** —— 当 C 字符串用就会读出界。

**怎么办**：一行文本这条路上用**带长度的类型**：

| 写法 | 结果 |
|---|---|
| `etl::string<N>` / `std::string` / `char[N]` | 拷贝，自带长度与结尾 `0`（**推荐**）|
| `std::string_view` / `etl::string_view` | 零拷贝但**只能用 `.data()` + `.size()`**，别当 C 字符串 |
| `const char*` | 只适合 argv 那条路 |

```cpp
line_reader<64> rx;
feed_one_line(rx, "hello\n");
type_args a{};
char scratch[ECLI_MAX_LINE];
parse(rx.line(), a, scratch, sizeof(scratch));
// a.first.data() 落在 rx.line() 的缓冲里面 —— 所以拿长度（a.first.size()）用，别用 std::strlen
```

（同一件事在 [sandbox/main.cpp](../../sandbox/main.cpp) 的 `args_args` 上有一段现成注释。）

### 5.3 `skip` 的两个作用，以及它**仍然能被模式段写**

**现象 1**：某个字段类型不支持命令行取值（比如嵌套结构体），编译报"这个字段的类型不能做命令行取值"。
**怎么办 1**：标 `skip` —— 它不进选项表，也不做取值类型检查。

**现象 2**：以为是"只读字段"，结果 `net set :ssid` 照样把它写进去了。
**原因**：**捕获注入走的是另一条路**（`assign_capture`），它**不跳 `skip`** —— 因为 `skip` 正是"只由模式喂"的写法。
**怎么办 2**：想彻底不让外部写，就别让命令名里有同名捕获段。

### 5.4 字段类型名里不能有顶层逗号

**现象**：编译报 `E_FMT_DERIVE 第一个参数只能是【声明本身】，不能有顶层逗号：一行多字段（int x, y;）请拆成一行一个…`。

**原因**：宏的第一个参数按**顶层逗号**切分；`template<A, B>` 里的逗号会把声明切断，`int x, y;` 同理。

**怎么办**：

```cpp
using int2 = etl::vector<int, 2>;              // 类型名带逗号：先 typedef
E_FMT_DERIVE(struct A { [[efmt::arg(long = "id")]] int2 ids; }, Cli);

E_FMT_DERIVE(struct B {                         // 一行一个字段
  int x;
  int y;
}, Cli);

E_FMT_DERIVE_ENUM(enum class mode { fast, slow });   // 枚举用 ENUM 那个宏
```

### 5.5 帮助被截断了：`...(truncated)`

**现象**：命令多、帮助长时，`help` 的输出尾巴上是 `...(truncated)`。

**原因**：命令表的帮助与报错文本走的是**栈**上的 `ECLI_REPLY_BUFFER`（默认 384 B）；
装不下时库**如实追加标记**而不是静默丢（`write_help` / `write_error` 本体是 snprintf 语义，不受这个限制）。

**怎么办**：`-DECLI_REPLY_BUFFER=768`（[沙盒](../../sandbox/main.cpp) 就是这么干的），或者精简 `help = "…"` 的文案。
用 `send_help / send_error`（3.9 的"产文本 + 送达"）时，返回值 = snprintf 语义的完整长度 ——
`返回值 > ECLI_REPLY_BUFFER` 一眼就知道截断了，差多少都能算出来。

### 5.6 `help` / `-h` / `--help` / `?` / `-V` / `--version` 是保留词

**现象**：命令表里写了一条 `{"help", …}`，但敲 `help` 出来的是命令列表，自己的处理函数永远接不到。

**原因**：`dispatch` 先认这几个词。名字占用了就轮不到你的命令（同理，某个**字段**占了 `-h` / `--help` / `-V` / `--version` 时，它归字段）。

**怎么办**：换名字（`manual` / `usage` 之类）；要靠 `-h` 显示某条命令的用法时用 `help <命令…>` 或 `<命令> -h`。

### 5.7 段数不够：`net set` 会落到更短的那条命令上

**现象**：写了 `net set :ssid`，敲 `net set`（少一段）得到的却是 `too_many_args` 或 `unknown_command`。

**原因**：模式段要**吃满**才算命中。`net set :ssid` 需要 3 个 token；不够时：

- 表里若有更短的前缀（`net`）→ 命中它，多出来的 token 由参数解析报 `too_many_args`；
- 表里没有 → `unknown_command`。

**怎么办**：给半截输入补一条 `{"net set", "…", command_of<no_args, …>()}` 做友好提示（`no_args` 见 5.1）。

### 5.8 编译期报错长什么样（都是"一句话说清"的设计）

| 你写错了什么 | 编译器给你的话（真实诊断，去掉前缀）|
|---|---|
| 类型没标 `Cli` | `这个类型不能做命令行参数：缺 Cli 能力标签。写成 E_FMT_DERIVE(struct args { ... }, Cli) 就能解析了` |
| 空声明体 | `E_FMT_DERIVE 的声明体是空的：结构体至少要有一个字段、枚举至少要有一个取值。` |
| 声明里有顶层逗号 | `E_FMT_DERIVE 第一个参数只能是【声明本身】，不能有顶层逗号：…` |
| 字段既没有 `long/short` 也没有 `pos` | `[[efmt::arg(...)]]：字段既没有 long/short 也没有 pos —— 它在命令行里没有身份。不想让它进命令行就标 [[efmt::arg(skip)]]` |
| `pos` 与 `long/short` 同时标 | `pos 与 long/short 不能同时标（位置参数与命名选项二选一）` |
| 位置参数是 `bool` | `位置参数不能是 bool —— bool 是 --flag 语义，位置参数得能收一个取值` |
| 字段类型不支持且没标 `skip` | `这个字段的类型不能做命令行取值：支持 bool / 整数 / 浮点 / 枚举 / 字符串 / string_view / 字符指针 / 字符数组 / 可重复容器；…` |
| `count` 标在非整数上 | `count 只能标在整数成员上（-vvv 那种计数开关）` |
| `count` 标在位置参数上 | `count 是命名开关，不能标在位置参数上` |
| `trailing` 标在标量上 | `trailing 只能标在【可重复的位置参数（容器）】上：它把余下的 token 全收走` |
| 位置参数序号重复 / 可重复位置参数不在最后 | `位置参数序号有冲突：序号必须唯一，且可重复的位置参数（容器）必须是最后一个` |
| 两个字段抢同一个选项名 | `选项名撞车：long / alias / short / short_alias 必须两两不同` |
| `needs` / `conflicts` / `unless` 引用了不存在的字段 | `needs / conflicts / unless 的取值必须是本类型里真实存在的字段名或选项名（写错就是这句）` |
| 临时 sink 交给 `reply_to_sink` | `use of deleted function 'ecli::reply ecli::reply_to_sink(e_log::sink&&)'` |

（这些反例都在 `tests/ecli_compile_fail_*.cpp` 里，`run_check.ps1` 会一条条跑：**必须编不过，且诊断里有这句话**。）

### 5.9 `parse(line, a)` 便利版的三个限制

**现象**：用了只吃一行的便利版，然后发现：拿不到 `error_info`；把结构体存起来之后字符串字段变成乱码。

**原因**：它自带**函数内栈缓冲**（约 `ECLI_MAX_TOKENS×16 + ECLI_MAX_LINE` 字节）：

- 故意**不返回 `error_info`** —— 出错 token 出函数就悬垂了；
- 带引号 / 转义的 token 落在那个栈缓冲里，**出函数就悬垂**（没引号没转义的 token 指向你传的 `line`，那个还活着）。

**怎么办**：要 `error_info`、或者结构体里留着 `const char*` / `string_view` 字段，
就用带 `scratch` 的重载：

```cpp
char scratch[ECLI_MAX_LINE];
error_info info{};
parse(line, a, scratch, sizeof(scratch), &info);   // scratch 活得比 a 久
```

### 5.10 其它常见现象

| 现象 | 原因 | 怎么办 |
|---|---|---|
| `too_many_tokens` | 一条命令超过 `ECLI_MAX_TOKENS`（默认 16）个 token，或一行超 `ECLI_MAX_LINE` | 调大宏，或把命令拆短 |
| `too_many_values` | 定长容器（`etl::vector<T, N>`）塞满了 | 调大 N，或改用不设上限的容器 |
| 一行文本里 `"abc` 报 `bad_quote` | 引号没闭合 | 补齐引号；注意 `\` 转义 |
| 关系约束只报了一条 | 校验按声明顺序，**一次只报第一个问题** | 改完再跑一次 |
| `--opt` 后面跟 `-x` 报 `missing_value` | 取值位置看起来像选项 | 该字段标 `hyphen`（允许取值以 `-` 开头），或写成 `--opt=-x` |
| 枚举 `--mode turbo` 报 `invalid_value` | 枚举取值名**大小写敏感**（与 json 一致），也不认别名 | 用注册的取值名或数字 |
| `etl::string<N>` 装不下 | CLI 先问容量再写（`etl::string::assign` 本身会静默截断）| 调大 N —— 这里是**报错**，不是截断 |

---

## 6. 怎么自测 / 怎么进一步验证

### 6.1 手敲：编译并跑本手册的示例

```bash
cd <仓库根>
g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I. tests/ecli_manual_examples.cpp -o tests/out/ecli_manual_examples.exe
./tests/out/ecli_manual_examples.exe        # → 225 checks, 0 failures
```

改手册里的任何代码块时，**同步改 `tests/ecli_manual_examples.cpp` 里对应的小节**（分段注释就是手册的小节号），
不然这一步会红 —— 这条规矩来自 [EFMT 手册的示例测试](../../tests/efmt_manual_examples.cpp)。

### 6.2 仓库的全量检查

```powershell
.\tests\run_check.ps1            # 宿主 / 嵌入式 / ETL 三套配置都跑；含 ecli 的各个 pass 与编译失败反例
.\tests\run_check.ps1 -Size      # 额外的 Cortex-M4 体积读数（ecli 的那几行出自 ecli_size_probe.cpp）
```

ecli 相关的检查文件（想看边界语义就读它们，比文档权威）：

| 文件 | 钉住什么 |
|---|---|
| [tests/ecli_cli_check.cpp](../../tests/ecli_cli_check.cpp) | 词法 / 取值 / 错误码 / 出错位置 / 帮助文本 / 行装配器 |
| [tests/ecli_cli_extra_check.cpp](../../tests/ecli_cli_extra_check.cpp) | 别名 / `count` / `delim` / `trailing` / `hyphen` / optional / 关系约束 |
| [tests/ecli_cli_etl_check.cpp](../../tests/ecli_cli_etl_check.cpp) | ETL 类型（`etl::string` 装不下、`etl::vector` 塞满、`etl::string_view` 零拷贝）|
| [tests/ecli_command_check.cpp](../../tests/ecli_command_check.cpp) | 命令表 / 子命令最长前缀 / 内置 help / 回复通道 / 错误路由 |
| [tests/ecli_pattern_check.cpp](../../tests/ecli_pattern_check.cpp) | 模式段（`:param` / `*rest` / 特异性 / 裁剪版）|
| [tests/ecli_elog_check.cpp](../../tests/ecli_elog_check.cpp) | 回复接 elog 的 sink（原样字节、一次绑定）|
| [tests/ecli_size_probe.cpp](../../tests/ecli_size_probe.cpp) | 体积读数（`-Size` 用）|
| [tests/ecli_manual_examples.cpp](../../tests/ecli_manual_examples.cpp) | **本手册里的每一段代码** |

> [`ecli_manual_examples.cpp`](../../tests/ecli_manual_examples.cpp) **已经挂进 `run_check.ps1`**：
> ecli 段里那条 pass 名是 `ecli manual examples (docs stay honest)`（和 `efmt_manual_examples.cpp`
> 的 `manual examples`、eserde / matchit 手册各自的 pass 一个模式）。
> **给本手册加内容时，记得同步在示例文件里加一份可编译版本**，否则这条 pass 证明不了你新写的那段。

### 6.3 手玩：沙盒命令台

```bash
cmake -S sandbox -B build -G Ninja && cmake --build build && ./build/sandbox
```

敲 `help` / `args -v -o a.bin --level 7 --tag net in.txt` / `net set mynet` / `level 5` / `log warn`，
看着命令表、两段式子命令、模式段捕获、日志分级一起动起来 —— 见
[沙盒命令台上手指南](../SANDBOX-命令台上手指南.md) 与 [`sandbox/README.md`](../../sandbox/README.md)。

### 6.4 接着往下读

| 文档 | 什么时候看 |
|---|---|
| [EFMT 使用手册 5.10 / 5.11](../EFMT-使用手册.md) | 想要同一份内容的"总手册口径"（含与 json / cbor 的对照）|
| [ECLI-与clap的差距清单.md](../ECLI-与clap的差距清单.md) | 想知道"clap 有而我们没有"的是哪些、为什么 |
| [ECLI-命令行解析-方案.md](../ECLI-命令行解析-方案.md) | 想知道为什么是 token 表而不是 Source 抽象、下一步做什么 |
| [matchit/README.md](../../matchit/README.md) | 想直接用 matchit 的 `match` 表达式（命令名的模式段就是它在做匹配）|
| [ecli/cli.hpp](../../ecli/cli.hpp) / [ecli/command.hpp](../../ecli/command.hpp) | 想确认某个语义的权威实现（头文件注释写得比本文细）|
