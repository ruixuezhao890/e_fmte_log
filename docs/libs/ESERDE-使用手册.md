# Eserde 使用手册（编译期反射基座 + JSON + CBOR）

> 面向第一次接触 eserde 的人：从"复制粘贴就能跑"到"知道每个开关花多少 Flash"。
> 本手册里的每一段代码都在 [tests/eserde_manual_examples.cpp](../../tests/eserde_manual_examples.cpp) 里编译并跑断言（宿主 81 checks / 嵌入式配置 80 checks，0 failures）——文档与实现脱节时，那个文件先红。
> 全量手册（efmt / elog / eserde / ecli 一起讲）见 [EFMT-使用手册.md](../EFMT-使用手册.md) 第 5.4、5.6–5.9 节与附录 B；本手册不重复那里的内容，只讲"怎么写代码"。
> 想先在命令台里敲着看效果 → [沙盒命令台上手指南](../SANDBOX-命令台上手指南.md)。
>
> 文档索引 → [docs/README.md](../README.md) · 同层另外两个库 → [libs/README.md](README.md)

---

## 目录

1. [30 秒：这是什么](#1-30-秒这是什么)
2. [第一个能跑的例子](#2-第一个能跑的例子)
3. [逐块讲清楚](#3-逐块讲清楚)
4. [完整可抄示例：设备配置的保存与读回](#4-完整可抄示例设备配置的保存与读回)
5. [坑与 FAQ](#5-坑与-faq)
6. [怎么自测 / 怎么进一步验证](#6-怎么自测--怎么进一步验证)

---

## 1. 30 秒：这是什么

三句话：

1. **eserde 是 efmt 的编译期反射基座**：它把 `E_FMT_DERIVE` 里的声明原文变成编译期可查的数据（能力标签 / 字段名 / 类型名 / 字段标签 / 枚举取值），自己不产生任何运行时行为。
2. **JSON 与 CBOR 都构建在它上面**：`eserde/json.hpp`、`eserde/cbor.hpp` 各是一个文件、一个开关——**不 include 就一个字节都不编进去**，efmt 本体也不认识它们。
3. **你要写的只有两件事**：类型上写能力标签（`, Serialize, Deserialize`），字段上写格式标签（`json = "别名"` / `json = "skip"`）。剩下的键名、遍历、递归、错误码都由库处理。

前置条件：

| 需要什么 | 说明 |
|---|---|
| C++17 | 与 efmt 一致，无构建系统依赖（header-only） |
| include 路径 | `-I<仓库根>`（为了 `<eserde/...>`）+ efmt 的路径（为了 `<middleware/efmt/core/format.hpp>`）|
| `EFMT_DERIVE_ENABLE_SCHEMA=1` | 默认就是 1。设成 0 时 `eserde/serde.hpp` 直接 `#error`（没有 schema 原料，eserde 无从解析）|
| 零第三方 / 零异常 / 零动态分配 | JSON 与 CBOR 本体都满足；唯一例外是宿主的 `eserde::json::to_string`（返回 `std::string`，靠 `EFMT_ENABLE_DYNAMIC_STRING`）|

文件分工：

| 文件 | 内容 | 不要它就 |
|---|---|---|
| `eserde/serde.hpp` | 基座：能力查询 + schema 查询 + 字段访问 | 不 include |
| `eserde/traits.hpp` | 取值形状判定与取值搬运助手，**JSON / CBOR 共用** | 不 include（它被两个格式头自己拉进来） |
| `eserde/json.hpp` | JSON 文本读写 | 不 include |
| `eserde/cbor.hpp` | CBOR 二进制读写 | 不 include |
| `eserde/eserde.hpp` | 汇总头：按 `ESERDE_ENABLE_JSON` / `ESERDE_ENABLE_CBOR` 拉格式 | 不 include（单格式头永远可以单独 include，那才是最精确的开关）|

---

## 2. 第一个能跑的例子

```cpp
#include <middleware/efmt/core/format.hpp>
#include <eserde/json.hpp>

#include <cstdio>

using namespace e_fmt;
using namespace eserde;

// 声明即推导：字段名 / 能力标签都在这一行里，后面不用再写第二遍
E_FMT_DERIVE(struct reading {
  int id;
  float value;
}, Debug, Serialize, Deserialize);

int main() {
  const reading r{7, 12.5f};
  char buf[64];
  const std::size_t need = json::write_to(buf, sizeof(buf), r);
  std::printf("%.*s\n", static_cast<int>(need), buf);
  return 0;
}
```

编译、运行（命令与 [tests/run_check.ps1](../../tests/run_check.ps1) 里用的是同一套参数）：

```powershell
cd /d E:\01_Workspace\00_Active_projects\00_code\01_cpp_code\etl_fmt
g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I. tests/eserde_manual_examples.cpp -o tests/out/eserde_manual_examples.exe
.\tests\out\eserde_manual_examples.exe
```

输出：

```text
{"id":7,"value":12.5}
```

读完这一段要知道的三件事：

- `Serialize` 管写、`Deserialize` 管读，**两个方向各自独立**：只写不读的类型写 `Serialize` 就够。
- `write_to` 是 `snprintf` 语义：**返回的是"需要的长度"，不是"写进去的长度"**；返回值 > `size` 就是被截断了。
- 它**不抛异常**：读失败返回错误码（见 3.3）。

---

## 3. 逐块讲清楚

本节示例由 tests/eserde_manual_examples.cpp 编译并断言。

### 3.1 声明你的类型：`E_FMT_DERIVE` 与能力标签

```cpp
E_FMT_DERIVE(struct person {
  int age;
  std::string name;
}, Debug, Serialize, Deserialize);
```

声明之后的参数**全是能力标签**——efmt 只把它们原样登记（`caps_list<...>`），含义由 eserde 定义：

| 标签 | 谁定义 | 含义 | 不写会怎样 |
|---|---|---|---|
| `Debug` | efmt | 可打印（`format("{}", x)`）。**注册过的类型默认就有** | 不用写也有 |
| `Serialize` | eserde | 允许 `write_to` / `to_string` | 写它的编译期报错（见 3.6）|
| `Deserialize` | eserde | 允许 `read_from` | 读它的编译期报错（见 3.6）|
| 其它任何类型名 | 你自己 | efmt 不认识、eserde 也不认识，**原样登记**，谁定义谁查 | 没影响 |

自定义标签就是"一个随便什么类型"——`ecli` 的 `Cli` 就是这么干的（见 [ECLI-命令行解析-方案.md](../ECLI-命令行解析-方案.md)）：

```cpp
struct Toml {};   // 自定义能力：只为了让 has_cap_v<T, Toml> 有东西可查

E_FMT_DERIVE(struct person {
  int age;
  std::string name;
}, Debug, Serialize, Deserialize, Toml);

static_assert(eserde::has_cap_v<person, Serialize>, "带 Serialize");
static_assert(eserde::has_cap_v<person, Toml>, "自定义标签照样查得到");
static_assert(eserde::has_cap_v<person, Debug>, "注册过的类型默认有 Debug");
static_assert(!eserde::has_cap_v<int, Serialize>, "标量没有能力标签");
```

枚举走另一个入口（枚举体的逗号是顶层逗号，切不开——原因见 [EFMT-使用手册.md 5.4](../EFMT-使用手册.md)）：

```cpp
E_FMT_DERIVE_ENUM(enum class mode {
  fast,
  slow = 5,
  err = -1
});

static_assert(eserde::has_cap_v<mode, Debug>, "枚举注册过 → 有 Debug");
static_assert(!eserde::has_cap_v<mode, Serialize>, "枚举侧没有能力标签");
```

**枚举不需要 `Serialize` / `Deserialize`**：标量、枚举、容器都是"基础类型"，天然可（反）序列化，能力门禁只管结构体。

两条写法约束（都是预处理器/宏的硬限制，不是设计选择）：

| 约束 | 现象 | 怎么办 |
|---|---|---|
| 第一个参数不能有顶层逗号 | `E_FMT_DERIVE(struct s { std::pair<int, int> p; }, ...)` 报"第一个参数只能是【声明本身】" | 先 `using pair2_t = std::pair<int, int>;`，声明里写 `pair2_t p;` |
| 宏必须写在类型所在的作用域（文件作用域），不能写在函数里 | 报 `a function-definition is not allowed here` | 把类型和宏一起挪到文件作用域 |

### 3.2 字段标签：格式名就是标签名

字段标签写在 `[[efmt::arg(...)]]` 里，**标签名就是格式名**——`json` 只被 JSON 读，`cbor` 只被 CBOR 读，别人看不见：

```cpp
E_FMT_DERIVE(struct person {
  int age;
  [[efmt::arg(short, long, json = "user_name", cbor = "un")]] std::string name;
  [[efmt::arg(json = "skip", cbor = "skip")]] int internal;
}, Debug, Serialize, Deserialize);
```

| 写法 | 效果 | 谁读它 |
|---|---|---|
| `json = "user_name"` | JSON 的键名用 `user_name`（不是 `name`） | JSON 写 + 读 |
| `json = "skip"` | 这个字段不进 JSON，也不从 JSON 读 | JSON 写 + 读 |
| `cbor = "un"` | CBOR 的键名用 `un` | CBOR 写 + 读 |
| `cbor = "skip"` | 不进 CBOR，也不从 CBOR 读 | CBOR 写 + 读 |
| `short` / `long` / `help = "..."` | 与序列化无关的标签（ecli 用它生成命令行） | ecli 等上层 |

同一条策略由基座提供，各格式只传自己的名字进来（`eserde::field_key<T>(i, "json")` / `eserde::field_skipped<T>(i, "json")`），所以**加一个格式不用给每个字段再写一遍策略**：

```cpp
static_assert(eserde::field_key<person>(1, "json") == "user_name", "json 别名");
static_assert(eserde::field_key<person>(1, "cbor") == "un", "同一个字段两套键名");
static_assert(eserde::field_key<person>(1, "toml") == "name", "别的格式看不见 → 退回字段名");
static_assert(eserde::field_skipped<person>(2, "json"), "json = skip");
```

三个容易忽略的点：

- **别名只改键名，不改字段名**：`read_from` 认别名（`user_name`）也认字段名（`name`），两个都能读进来。
- **`skip` 是按格式各算各的**：只想让 JSON 跳过，就只写 `json = "skip"`——CBOR 里它照常出现（见 3.8 的例子）。
- **别名写错格式名等于没写**：`json = "x"` 对 CBOR 毫无影响，CBOR 仍用字段名 `name`。加格式零成本，代价只是"别写错格式名"。

### 3.3 JSON：`write_to` / `read_from` / `to_string`

```cpp
#include <eserde/json.hpp>

// 写：snprintf 语义
char buf[192];
const std::size_t need = json::write_to(buf, sizeof(buf), cfg);   // 返回【所需】长度
if (need > sizeof(buf)) { /* 被截断了，这份内容不完整，别用 */ }

// 只量长度不写（buf == nullptr 或 size == 0）
const std::size_t only_len = json::write_to(nullptr, 0, cfg);

// 读：失败返回错误码，目标对象一根毫毛都不动
device_cfg cfg2{};
const json::error e = json::read_from(text, cfg2);
if (e != json::error::ok) {
  std::printf("读失败：%s\n", json::error_name(e));
}
```

| 接口 | 签名要点 | 语义 |
|---|---|---|
| `json::write_to` | `(char *buf, std::size_t size, const T &v) -> std::size_t` | 返回**所需**长度；`buf == nullptr` / `size == 0` 时只计数不写；**不写字符串结束符** |
| `json::read_from` | `(std::string_view text, T &v) -> json::error` | 在副本上解析，**全部成功才赋回**；要求 `T` 可拷贝构造 |
| `json::to_string` | `(const T &v) -> std::string` | 宿主便利版，只有 `EFMT_ENABLE_DYNAMIC_STRING=1` 时才有 |
| `json::error_name` | `(error) -> const char *` | 给日志用；不会返回 `nullptr` |

**推荐的缓冲区写法**（自己留一格并补结束符——`write_to` 不替你写）：

```cpp
char buf[192];
const std::size_t need = json::write_to(buf, sizeof(buf) - 1, cfg);   // 留一格给结束符
if (need >= sizeof(buf)) {
  return false;                      // 截断：别把半截 JSON 交出去
}
buf[need] = '\0';                    // 现在 buf 是一串 C 字符串
```

不想手动补结束符就用"先量长度"那招（本手册的断言辅助就是这么写的）：

```cpp
template <typename T> std::string json_text_of(const T &v) {
  const std::size_t need = json::write_to(nullptr, 0, v);
  std::string out(need, '\0');
  json::write_to(&out[0], need, v);       // need 已经算准了，正好填满
  return out;
}
```

错误码全表（JSON 与 CBOR 同名同形，方便上层写通用错误处理）：

| 错误码 | 什么时候出现 | 例子 |
|---|---|---|
| `ok` | 成功 | — |
| `syntax` | 输入本身坏了 | `{"age":}`、`{"age":1`（没闭合）、`{"age":1} x`（尾巴有垃圾）|
| `type_mismatch` | 类型对不上 | 往 `int` 里喂字符串、往 `int` 里喂 `1.5`、往 `bool` 里喂 `2` |
| `truncated` | 目标装不下 | `signed char` 收 300、`etl::string<8>` 收 9 个字符、键名超过 `ESERDE_JSON_MAX_KEY` |
| `too_deep` | 嵌套超过 `ESERDE_JSON_MAX_DEPTH` | 十层 `[[[[…]]]]` |
| `unsupported` | 这个类型不参与（反）序列化，或超出 CBOR 子集 | `std::optional` 字段、CBOR 的 tag / 不定长 |

读的语义（逐条钉在 [tests/eserde_json_check.cpp](../../tests/eserde_json_check.cpp)）：

- **没写的字段 / 写成 `null` 的字段保持原值**（配置合并语义）
- **认不出的键跳过它的值**（前向兼容：老固件读新配置不会炸）
- **失败不动原对象**：先解析到副本，成功才赋回
- **数值宽容但明确**：`1.0` 进 `int` 收下（变成 `1`），`1.5` 进 `int` 是 `type_mismatch`

值映射表与更多细节见 [EFMT-使用手册.md 5.7](../EFMT-使用手册.md)。

### 3.4 CBOR：同一个基座上的二进制

```cpp
#include <eserde/cbor.hpp>

unsigned char bin[128];
const std::size_t bytes = cbor::write_to(bin, sizeof(bin), cfg);   // 返回所需【字节数】
if (bytes > sizeof(bin)) { /* 截断：字节流不完整，别拿去解码 */ }

device_cfg back{};
const cbor::error e = cbor::read_from(bin, bytes, back);            // 要多传一个长度
if (e != cbor::error::ok) { std::printf("%s\n", cbor::error_name(e)); }
```

除接口类型与下表两处，语义与 JSON 完全一致：`snprintf` 语义、失败不动原对象、错误码同名同形、字段标签、能力门禁。

| | JSON | CBOR |
|---|---|---|
| 写接口 | `write_to(char *, size, obj)` | `write_to(unsigned char *, size, obj)` |
| 读接口 | `read_from(std::string_view, obj)` | `read_from(const unsigned char *, size, obj)` |
| 枚举 | 写**取值名**字符串（slow）| 写**底层整数**（`0x05`）；读端整数与取值名都收 |
| 浮点 NaN / Inf | 只能写成 `null`（JSON 没有这两个值）| **原样传**（`0xFA` / `0xFB`）|

**写出来的字节是合法 CBOR（RFC 8949）**，标准解码器能直接读——这就是选 CBOR 而不是自研格式的理由。证据在 [tests/eserde_cbor_check.cpp](../../tests/eserde_cbor_check.cpp)：期望值取自 RFC 8949 附录 A，写出的要逐字节相等，标准编码器写出的要能读回来：

```text
1000   → 19 03 e8            1.5f     → fa 3f c0 00 00
-1000  → 39 03 e7            1.5      → fb 3f f8 00 00 00 00 00 00
"水"   → 63 e6 b0 b4         枚举 slow → 05
[1,2,3]→ 83 01 02 03
```

子集边界（写端只写这些，读端只收这些 + 无损处的宽容）：

| CBOR 特性 | 这里怎么做 |
|---|---|
| 整数 major 0/1 | 写最短编码；读收 1/2/4/8 字节的全部合法长度（非最短编码也收）|
| 文本串 major 3 | 双向；map 的键也是文本串（直接指向输入缓冲：不拷贝、无长度上限）|
| 数组 / map major 4/5 | **只写定长**：要能问出元素个数 → 用有 `size()` 的容器；`std::forward_list` 这类编译期报错 |
| 浮点 | 写 `0xFA`(float) / `0xFB`(double)；读端额外收 `0xF9`(half) |
| bool / null | `0xF4` / `0xF5` / `0xF6`；`null` 与 JSON 一样表示"保持原值" |
| 字节串 major 2 | 不写；读端只在**跳过未知键**时略过，出现在字段位置上报 `type_mismatch` |
| tag / bignum / 不定长 / undefined | **不支持** → `unsupported`（不会静默读错）|
| map 键排序（RFC 8949 §4.2 确定性编码）| 不做：按声明顺序写，合法但非 canonical |

**字节数对比**（本仓库实测，同一个结构体）：

| 数据 | JSON | CBOR |
|---|---|---|
| 本手册的 `person`（12 字段，含字符串 / 数组 / 枚举 / uint64）| 173 字节 | **99 字节（57%）** |
| 本手册的 `device_cfg`（4 字段，1 个 skip）| 47 字节 | **25 字节（53%）** |
| [EFMT-使用手册.md 5.8](../EFMT-使用手册.md) 的 10 字段结构体 | 156 字节 | 94 字节（60%）|

数字小的原因很直白：整数不再是十进制文本、键只写一次、没有转义与空白、浮点是定宽二进制。

### 3.5 支持的类型

```cpp
E_FMT_DERIVE(struct shapes {
  unsigned long long big;
  double d;
  bool flag;
  level lv;                 // 注册过的枚举
  char tag[8];              // char[N] → 字符串
  int raw[3];               // 其它 C 数组 → 数组
  std::string text;         // 字符串
  std::vector<int> vec;     // 可增长容器
  arr2_t pair2;             // std::array<int,2>：先 typedef 消掉逗号
  point nested;             // 嵌套结构体（它自己也要标能力）
}, Debug, Serialize, Deserialize);
```

写出来（`json::write_to` 的真实输出）：

```text
{"big":18446744073709551615,"d":2.5,"flag":true,"lv":"high","tag":"imu0",
 "raw":[1,2,3],"text":"hello","vec":[1,2],"pair2":[4,5],"nested":{"x":6,"y":7}}
```

判定方式**不看具体类型，只看成员函数**——`data()/size()` 认得字符串，`begin()/end()/clear()/push_back()` 认得容器，所以 `std` 与 ETL 走的是同一套代码：

| 形状 | 写 | 读 | 说明 |
|---|---|---|---|
| 整数 / `bool` / `float` / `double` | ✓ | ✓ | `unsigned long long` 全范围；`bool` 只收 `true/false/0/1` |
| `char[N]` | ✓ | ✓ | 读的时候带长度检查：装不下连结束符 → `truncated` 并清空 |
| `const char *` / `char *` | ✓ | ✗ | `nullptr` 写 `null`；读**编译期报错**（没有可写存储）|
| `std::string` / `etl::string<N>` | ✓ | ✓ | 定长容器装不下 → `truncated`（不静默截断）|
| `std::string_view` / `etl::string_view` | ✓ | ✗ | 读**编译期报错**（同上）|
| 注册过的枚举 | ✓ | ✓ | JSON 写取值名（没列出的取值写底层整数）；CBOR 写底层整数，读端两种都收 |
| 注册过的结构体 | ✓ | ✓ | 递归；**每个嵌套成员自己也要有对应能力** |
| `std::vector` / `etl::vector<T,N>` | ✓ | ✓ | 满 `max_size()` → `truncated` |
| `std::array` / C 数组 | ✓ | ✓ | 读时元素多了 → `truncated` |
| 其它裸指针 | ✓（写 `null`）| ✗ | JSON / CBOR 里都没有指针 |
| `std::optional` / `etl::optional` | ✗ | ✗ | 不支持：报"不认识这个类型"。用 `bool has_x` + `x` 代替 |
| `std::map` / `std::set` / `std::pair` | ✗ | ✗ | 不支持：`pair` 既不是字符串也不是容器 |
| 位域 / 静态成员 / 成员函数 | — | — | 不参与（efmt 推导自动跳过，见 5.4）|

ETL 类型的边界（实测，本机 `tests/include/middleware/etl` junction）：

| 类型 | 结果 |
|---|---|
| `etl::string<N>` 作成员 | ✓ 与 `std::string` 完全同一套代码；喂超长 → `truncated`，且**原值保持不动** |
| `etl::vector<T,N>` 作成员 | ✓ 容量不够 → `truncated` |
| `etl::array<T,N>` 作成员 | ✗ **编译不过**（efmt 推导打印把它当聚合体逐成员分解，报 `N names provided for structured binding`）；单独 `format("{}", etl::array)` 是好的。换成 `etl::vector` / `std::array` / C 数组 |
| `etl::optional<T>` 作成员 | ✗ 与 `std::optional` 同 |
| ETL 头文件没装 | 本手册的 ETL 那一节自动不编（`__has_include` 判定）；其余功能不受影响 |

### 3.6 能力门禁：缺标签就编译不过

门禁是**编译期**的，不是运行期检查：

```cpp
E_FMT_DERIVE(struct unmarked { int x; });   // 只有默认的 Debug

unmarked u{};
char buf[64];
json::write_to(buf, sizeof(buf), u);        // ← 编译期报错
```

报错原文（GCC，省略前后文）：

```text
error: static assertion failed: 这个类型不能序列化：请在声明处写 E_FMT_DERIVE(struct X { ... }, Debug,
Serialize)。不想进 JSON 的字段可以标 [[efmt::arg(json = "skip")]]
```

怎么读：

| 报错关键词 | 含义 | 怎么办 |
|---|---|---|
| `这个类型不能序列化` | 写方向的类型（含嵌套成员）没有 `Serialize` | 在**那个类型**的声明处补 `Serialize` |
| `这个类型不能反序列化` | 读方向缺 `Deserialize` | 补 `Deserialize`，或那条字段标 `json = "skip"` |
| `不认识这个类型` | 类型既不是标量/字符串/容器，也没注册 | 用 `E_FMT_DERIVE` 注册；`optional` / `map` 改用别的写法 |
| `能序列化但不能反序列化` | `string_view` / `const char *` 这类没有可写缓冲 | 换成 `std::string` / `etl::string<N>`，或标 `skip` |

三条要点：

- **检查覆盖每一个被遍历到的结构体，包括嵌套成员**——里层没标，照样报错（报错点指在里层的类型上）。
- **标量、枚举、容器不需要标签**：它们是基础类型，`has_cap_v<mode, Serialize>` 是 `false` 但照样能写——枚举因此不用改 `E_FMT_DERIVE_ENUM`。
- 两个方向独立：只写不读的类型，`E_FMT_DERIVE(struct X {...}, Debug, Serialize)` 就够。

反例测试（run_check.ps1 会编译它们并要求**编译失败**）：

- [tests/eserde_compile_fail_caps.cpp](../../tests/eserde_compile_fail_caps.cpp)：没写 `Serialize` 就想 `write_to` → 诊断里必须出现"这个类型不能序列化"
- [tests/eserde_compile_fail_caps_read.cpp](../../tests/eserde_compile_fail_caps_read.cpp)：只写 `Serialize` 就想 `read_from` → 必须出现"这个类型不能反序列化"

### 3.7 基座 API 自己用（写第三个格式 / 写校验工具时用）

全部 `constexpr`，能直接写进 `static_assert`，运行时零开销：

| 接口 | 作用 |
|---|---|
| `is_registered_v<T>` | 类型是否用 `E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM` 注册过 |
| `has_cap_v<T, Cap>` | 能力标签查询；注册过的类型默认有 `Debug` |
| `field_count<T>()` | 字段数（枚举 = 取值个数）|
| `field_name<T>(i)` | 字段名 / 枚举取值名（按声明顺序，下标从 0 开始）|
| `field_type_name<T>(i)` | 字段类型名（**声明原文，纯文本**：`etl::string<12>`、`arr2_t` 都原样给出；不能拿来推导 C++ 类型）|
| `enum_value<T>(i)` | 枚举取值的数值 |
| `is_enum<T>()` | 是枚举还是结构体 |
| `tag_count<T>(i)` / `tag<T>(i, k)` / `has_tag<T>(i, "short")` | 字段标签（`[[efmt::arg(...)]]`）|
| `find_field<T>("name")` / `find_by_tag<T>("short")` | 反查字段下标；找不到 = `eserde::npos` |
| `field_key<T>(i, "json")` / `field_skipped<T>(i, "json")` | 字段的"格式键名" / 是否跳过——**写新格式就靠这两个** |
| `visit_fields(obj, vis)` | 按声明顺序遍历：`vis(std::string_view 名字, const auto &值)` |
| `field_at<I>(obj)` | 按索引读写成员（反序列化写回用；`const` 对象只读）|

```cpp
static_assert(eserde::field_count<person>() == 12, "字段数");
static_assert(eserde::field_name<person>(0) == "age", "字段名");
static_assert(eserde::field_type_name<person>(8) == "arr2_t", "typedef 后就是别名原文");
static_assert(eserde::find_field<person>("salary") == 2, "按名字反查下标");
static_assert(eserde::find_field<person>("nope") == eserde::npos, "查不到 = npos");

person p{};
p.age = 18;
std::string visited;
eserde::visit_fields(p, [&](std::string_view name, const auto &) {   // 顺序 = 声明顺序
  if (!visited.empty()) visited += ',';
  visited += std::string(name);
});
eserde::field_at<0>(p) = 30;          // 按索引写回
const person &cp = p;
const int first = eserde::field_at<0>(cp);   // const 对象只读
```

`visit_fields` 只给**名字与值**，不给下标；要下标就 `find_field<T>(名字)` 反查（上层格式就是这么按标签筛字段的）：

```cpp
eserde::visit_fields(p, [&](std::string_view name, const auto &) {
  const std::size_t i = eserde::find_field<person>(name);
  if (eserde::has_tag<person>(i, "short")) { /* 只看带 short 标签的字段 */ }
});
```

### 3.8 自己接一种新格式：几十行写一个 TOML-ish

**加格式不用改库**——基座只提供"字段名 / 键名 / 值"，往哪种字节里写是你的自由。下面这个完整实现（就是本节示例，逐字编译并断言过）把结构体写成 TOML 风味的一行一个键：

```cpp
namespace toml {

constexpr std::string_view kFormat = "toml";

// 一个 snprintf 语义的 sink：写不下就只计数
struct sink {
  char *buf;
  std::size_t cap;
  std::size_t n = 0;

  void put(char c) {
    if (n < cap) buf[n] = c;
    ++n;
  }
  void put(std::string_view s) {
    for (char c : s) put(c);
  }
};

// sep：顶层字段一行一个；嵌套内联表用 ", " 分隔
template <typename T> void write_body(sink &w, const T &obj, std::string_view sep);

// 标量：类型分支怎么写随你，这里只示范"库不参与"
template <typename T> void write_scalar(sink &w, const T &v) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_array_v<D>) {                       // char[N]
    w.put('"');
    w.put(std::string_view(v));
    w.put('"');
  } else if constexpr (std::is_convertible_v<const D &, std::string_view>) {
    w.put('"');                                              // std::string / const char*
    w.put(std::string_view(v));
    w.put('"');
  } else if constexpr (std::is_same_v<D, bool>) {
    w.put(v ? "true" : "false");
  } else if constexpr (std::is_enum_v<D>) {                  // 枚举名问 schema
    const long long key = static_cast<long long>(static_cast<std::underlying_type_t<D>>(v));
    for (std::size_t i = 0; i < eserde::field_count<D>(); ++i) {
      if (eserde::enum_value<D>(i) == key) {
        w.put(eserde::field_name<D>(i));
        return;
      }
    }
    w.put("?");                                              // 没列出的取值
  } else if constexpr (eserde::has_cap_v<D, eserde::Serialize>) {
    w.put("{ ");                                             // 嵌套结构体 → 内联表
    write_body(w, v, ", ");
    w.put(" }");
  } else {
    char tmp[32];                                            // 整数 / 浮点：交给 efmt
    const std::size_t n = e_fmt::format_to(tmp, sizeof(tmp), "{}", v);
    w.put(std::string_view(tmp, n));
  }
}

template <typename T> void write_body(sink &w, const T &obj, std::string_view sep) {
  static_assert(eserde::has_cap_v<T, eserde::Serialize>,
                "toml：类型要写 E_FMT_DERIVE(..., Serialize) 才能被这个格式写出来");
  bool first = true;
  eserde::visit_fields(obj, [&](std::string_view name, const auto &value) {
    const std::size_t i = eserde::find_field<T>(name);
    // skip 必须先判：field_key 遇到值标签 "skip" 会把 "skip" 当键名返回
    if (eserde::field_skipped<T>(i, kFormat)) return;
    if (!first) w.put(sep);
    first = false;
    w.put(eserde::field_key<T>(i, kFormat));                  // toml = "别名"，没标就是字段名
    w.put(" = ");
    write_scalar(w, value);
  });
}

template <typename T> std::size_t write_to(char *buf, std::size_t size, const T &value) {
  sink w{buf, size, 0};
  write_body(w, value, "\n");
  w.put('\n');
  return w.n;
}

}  // namespace toml
```

用起来（类型上多写一套 `toml` 标签即可，其它格式完全不受影响）：

```cpp
E_FMT_DERIVE(struct toml_cfg {
  [[efmt::arg(toml = "boot_ms")]] unsigned boot_delay_ms;
  float gain;
  bool verbose;
  point origin;
  [[efmt::arg(toml = "skip")]] int internal;
}, Debug, Serialize, Deserialize);

toml_cfg c{};
c.boot_delay_ms = 250; c.gain = 1.5f; c.verbose = true;
c.origin = point{1, 2}; c.internal = 999;

char buf[192];
const std::size_t need = toml::write_to(buf, sizeof(buf), c);
```

真实输出：

```text
boot_ms = 250
gain = 1.5
verbose = true
origin = { x = 1, y = 2 }
```

两点值得记住：

- **`toml` 标签对 JSON / CBOR 毫无影响**：同一个对象 `json::write_to` 出来是
  `{"boot_delay_ms":250,"gain":1.5,"verbose":true,"origin":{"x":1,"y":2},"internal":999}`
  ——别名不算数、`skip` 也不算数（它只对 `toml` 生效）。
- **`field_key` 会把 skip 这个字符串当键名返回**（`skip` 也是一个"值标签"），所以顺序必须是**先 `field_skipped` 再 `field_key`**。

### 3.9 裁剪开关与代价

| 宏 | 默认 | 作用 | 关掉/改小的后果 |
|---|---|---|---|
| `ESERDE_ENABLE_JSON` | 1 | 汇总头 `eserde/eserde.hpp` 是否拉 JSON | 只想用 CBOR 时设 0（单格式头不受它影响）|
| `ESERDE_ENABLE_CBOR` | 0 | 汇总头是否拉 CBOR | 缺省不替你决定：用 CBOR 就显式 `-DESERDE_ENABLE_CBOR=1` |
| `ESERDE_JSON_MAX_KEY` | 64 | JSON 读端的键名 / 枚举名缓冲 | 键名或取值名更长 → `truncated`；调大 = 每层对象多占这么多栈 |
| `ESERDE_JSON_MAX_DEPTH` | 8 | JSON 嵌套深度上限 | 更深 → `too_deep`；调大 = 栈开销跟着涨 |
| `ESERDE_CBOR_MAX_DEPTH` | 8 | CBOR 嵌套深度上限 | 同上 |
| `EFMT_DERIVE_ENABLE_SCHEMA` | 1 | 生成 schema 原料 | 设 0 → `eserde/serde.hpp` 直接 `#error`（eserde 用不了）|
| `EFMT_DERIVE_ENABLE_CAPS` | 1 | 登记能力标签 | 设 0 → 能力清单恒空，`has_cap_v<T, Serialize>` 恒 `false`，**序列化门禁会把每个结构体都拒掉**（别关）|
| `EFMT_DERIVE_ENABLE_TAGS` | 1 | 解析 `[[efmt::arg(...)]]` | 设 0 → 别名失效（退回字段名）、`skip` 失效（字段照进照出），字段名/类型名/能力查询照常 |

**实测体积**（Cortex-M4，`arm-none-eabi-g++ -Os`，整程序 + `--gc-sections`，`-DEFMT_ENABLE_HOSTED=0`）：

| 配置 | text | data | bss | 相对上一行 |
|---|---|---|---|---|
| 只用 efmt 打印（完全不 include eserde）| 7840 | 136 | 48 | — |
| + `json.hpp`（一次 write + 一次 read）| 20496 | 140 | 56 | **+12.7 KB** |
| + `cbor.hpp`（一次 write + 一次 read）| 16560 | 140 | 56 | **+8.7 KB** |
| JSON 与 CBOR 都上 | 23740 | 140 | 56 | CBOR 在 JSON 之上再 **+3.2 KB** |

这不是"库的固定开销"，而是这些代码路径**被实例化出来**的代价——模板只在被调用时才生成，只写不读、只读不写都比这个数字小。复现命令模板：

```powershell
arm-none-eabi-g++ -Os -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti `
  -mcpu=cortex-m4 -mthumb --specs=nano.specs --specs=nosys.specs -DEFMT_ENABLE_HOSTED=0 `
  -Itests/include -I. -Wl,--gc-sections <你的源文件> -o out.elf
arm-none-eabi-size out.elf
```

（上面每行末尾的续行符按 PowerShell 的写法——反引号。）

**实测栈**（宿主 x64 `g++ -O2 -fstack-usage`，口径同 [EFMT-使用手册.md 8.3](../EFMT-使用手册.md)）：

| 函数 | 栈 |
|---|---|
| `eserde::json::detail::read_object<T>` | 272 B / 每层嵌套（里面含 `ESERDE_JSON_MAX_KEY` = 64 B 的键缓冲）|
| `eserde::cbor::detail::read_object<T>` | 288 B / 每层嵌套（没有键缓冲——键直接指向输入缓冲）|

```powershell
g++ -std=c++17 -O2 -Itests/include -I. -fstack-usage -c <你的源文件> -o out.o   # 结果看 out.su
```

**标签开关的代价**：同一份带 `[[efmt::arg(short, long, json = "n")]]` 的结构体，`-DEFMT_DERIVE_ENABLE_TAGS=1` 与 `=0` 两次编译的 text/data/bss 完全一样（3516 / 108 / 48）——标签解析是纯编译期的事，代价在编译时间，不在 Flash。

---

## 4. 完整可抄示例：设备配置的保存与读回

一个真实场景：设备配置结构体 → 存成 JSON 文件 → 重启后读回来；同一份配置再来一遍 CBOR 往返。

```cpp
#include <middleware/efmt/core/format.hpp>
#include <eserde/json.hpp>
#include <eserde/cbor.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace e_fmt;
using namespace eserde;

// 两个格式都要：能力标签两个都写；键名与跳过策略按格式各写一套
E_FMT_DERIVE(struct device_cfg {
  [[efmt::arg(json = "boot_delay_ms", cbor = "bd")]] unsigned boot_delay_ms;
  float gain;
  bool verbose;
  [[efmt::arg(json = "skip", cbor = "skip")]] int internal;
}, Debug, Serialize, Deserialize);

// 保存：先量所需长度 → 判断缓冲够不够 → 自己补结束符 → 写文件
static bool save_json(const char *path, const device_cfg &cfg) {
  char buf[192];
  const std::size_t need = eserde::json::write_to(buf, sizeof(buf) - 1, cfg);
  if (need >= sizeof(buf)) return false;          // 截断：别写半截文件出去
  buf[need] = '\0';
  std::FILE *f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  const std::size_t wrote = std::fwrite(buf, 1, need, f);
  std::fclose(f);
  return wrote == need;
}

// 读回：整份读进缓冲 → read_from → 看错误码（失败时 cfg 一根毫毛都不动）
static eserde::json::error load_json(const char *path, device_cfg &cfg) {
  char buf[192];
  std::FILE *f = std::fopen(path, "rb");
  if (f == nullptr) return eserde::json::error::syntax;   // 打不开当作语法错上报
  const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  return eserde::json::read_from(std::string_view(buf, n), cfg);
}

int main() {
  device_cfg cfg{};
  cfg.boot_delay_ms = 250;
  cfg.gain = 1.5f;
  cfg.verbose = true;
  cfg.internal = 999;                     // 标了 skip：不落盘

  const char *path = "device.cfg.json";
  if (!save_json(path, cfg)) {
    std::printf("保存失败\n");
    return 1;
  }

  device_cfg back{};                      // 新对象：没写的字段就是默认值
  const eserde::json::error e = load_json(path, back);
  if (e != eserde::json::error::ok) {
    std::printf("读回失败：%s\n", eserde::json::error_name(e));
    return 1;
  }
  std::printf("boot_delay_ms=%u gain=%.1f verbose=%d internal=%d\n",
              back.boot_delay_ms, static_cast<double>(back.gain), back.verbose ? 1 : 0,
              back.internal);

  // 同一份配置的 CBOR 往返：一个缓冲 + 一个长度，没有文件
  unsigned char bin[128];
  const std::size_t bytes = eserde::cbor::write_to(bin, sizeof(bin), cfg);
  if (bytes > sizeof(bin)) {
    std::printf("CBOR 缓冲不够（要 %zu 字节）\n", bytes);
    return 1;
  }
  device_cfg from_cbor{};
  if (eserde::cbor::read_from(bin, bytes, from_cbor) != eserde::cbor::error::ok) {
    std::printf("CBOR 读回失败\n");
    return 1;
  }
  std::printf("CBOR %zu 字节（JSON 47 字节）\n", bytes);
  return 0;
}
```

真实输出（`save_json` 里写的文件内容是 `{"boot_delay_ms":250,"gain":1.5,"verbose":true}`，47 字节）：

```text
boot_delay_ms=250 gain=1.5 verbose=1 internal=0
CBOR 25 字节（JSON 47 字节）
```

四个真实数字，抄的时候照着对：JSON 47 字节 / CBOR 25 字节；`internal` 标了 `skip`，所以文件里没有它，读回来保持默认 `0`。

---
## 5. 坑与 FAQ

**1. `write_to` 之后缓冲区里没有字符串结束符**
→ 现象：`printf("%s", buf)` 打出后面的垃圾。
→ 原因：`write_to` 按"字节数"工作，不替你补结束符（这样 `need` 才能精确等于写入长度，宿主 `to_string` 靠它一次分配到位）。
→ 怎么办：按 3.3 的推荐写法——`write_to(buf, sizeof(buf) - 1, v)` 留一格，再 `buf[need] = '\0'`；或者用 `std::string_view(buf, need)`。

**2. 缓冲区不够时会怎样**
→ 现象：JSON 只写了一半，`need > size`。
→ 原因：`snprintf` 语义：**照写能写的部分，返回完整所需长度**。
→ 怎么办：`if (need > size)` 就当失败处理。**截断的 JSON / CBOR 不要拿去读**——JSON 会报 `syntax`，CBOR 字节流不完整同样是 `syntax`。

**3. `read_from` 失败后对象被改了一半？**
→ 不会。它先在 `value` 的副本上解析，全部成功才赋回（所以要求 `T` 可拷贝构造）。
→ 连带好处：`null` 与"没写的字段"都保持原值，天然是"配置合并"语义。
→ 别踩：**不要**为了省一次拷贝就自己赋回中间结果。

**4. JSON 里的 `NaN` / `Inf` 变成 `null`**
→ 原因：JSON 没有这两种值，写端如实退化（读回来是"保持原值"，不是变成 0）。
→ 怎么办：需要原样保留就换 CBOR（`0xFA` / `0xFB` 原样传）。

**5. `etl::string<8>` 喂 9 个字符，静默截断了吗？**
→ 没有。JSON 读端满了就直接报 `truncated` 并**清空**这个字符串；因为整次解析失败，外层对象也保持原样。
→ 原因：定长容器的溢出行为不保证，所以库先问 `max_size()`，满了就不推。
→ 怎么办：容量按最坏情况留够，或者把 `truncated` 上报给用户。

**6. 键名长一点就 `truncated`**
→ 原因：JSON 读端把键名解码到栈缓冲 `ESERDE_JSON_MAX_KEY`（默认 64）里再匹配，超了如实报错（不悄悄截断成另一个键）。
→ 怎么办：`-DESERDE_JSON_MAX_KEY=96`，键名尽量短。CBOR 没有这个限制（键直接指向输入缓冲）。

**7. 嵌套深一点就 `too_deep`**
→ 原因：`ESERDE_JSON_MAX_DEPTH` / `ESERDE_CBOR_MAX_DEPTH` 默认 8，防止畸形输入把栈吃穿。
→ 怎么办：按真实数据结构调大，并顺手算一下栈（见 3.9 的实测表）。

**8. 结构体里塞了 `std::optional` / `std::map`，编译不过**
→ 现象：`eserde::json 不认识这个类型`。
→ 原因：库里没有这两个形状的分支（`pair` 既不是字符串也不是容器）。
→ 怎么办：`optional` 拆成 `bool has_x; X x;`（`has_x` 标 `json = "skip"` 也行）；`map` 换成两个平行数组，或自己写一个 `E_FMT_DERIVE` 的 `{ key, value }` 数组。

**9. `etl::array<T,N>` 作成员编译不过**
→ 现象：`N names provided for structured binding`（报在 efmt 的 `format_derive.hpp`）。
→ 原因：efmt 的推导打印把 `etl::array` 当聚合体逐成员分解了（eserde 本身认得它，但类型声明阶段就失败了）。
→ 怎么办：换成 `etl::vector<T,N>` / `std::array<T,N>` / C 数组。

**10. `E_FMT_DERIVE` 写在函数里报错**
→ 现象：`a function-definition is not allowed here before '{' token`。
→ 原因：宏会在同一作用域生成函数（打印器 / schema / 能力登记），函数里放不下。
→ 怎么办：类型与宏一起挪到文件作用域（或命名空间里）。

**11. 类型名里带逗号（`std::pair<int, int>`、`std::array<int, 2>`）**
→ 现象：`E_FMT_DERIVE 第一个参数只能是【声明本身】，不能有顶层逗号`。
→ 原因：预处理器按顶层逗号切宏参数。
→ 怎么办：先 `using arr2_t = std::array<int, 2>;` 再在声明里用别名（`field_type_name` 给出的也就是这个别名原文）。

**12. 嵌套成员忘了标能力**
→ 现象：里层类型上报"这个类型不能序列化"。
→ 原因：门禁覆盖每一个被遍历到的结构体。
→ 怎么办：给里层类型也补上 `Serialize` / `Deserialize`。

**13. 关掉 `EFMT_DERIVE_ENABLE_TAGS` 之后别名和 `skip` 一起失效**
→ 现象：JSON 里出现字段名而不是别名，标了 `skip` 的字段也进去了。
→ 原因：标签读不到时 `field_key` 一律退回字段名，`field_skipped` 恒 `false`（这是有意的降级，不报错）。
→ 怎么办：序列化用到了标签，就别关这个开关。

**14. 关掉 `EFMT_DERIVE_ENABLE_CAPS` 之后什么都写不出去**
→ 现象：每个结构体都报"这个类型不能序列化"。
→ 原因：能力清单恒空 → `has_cap_v<T, Serialize>` 恒 `false` → 门禁全拒。
→ 怎么办：用了 eserde 就必须保持 `EFMT_DERIVE_ENABLE_CAPS=1`（它与 `SCHEMA=1` 都是 eserde 的硬前提）。

**15. 嵌入式下栈和缓冲要自己算**
→ 现象：深层嵌套配置一读就 HardFault，或者日志里 JSON 总被截断。
→ 原因：读端是递归下降，每层嵌套一帧栈；写端不需要额外大缓冲，但需要你自己给够。
→ 怎么办：按 3.9 的实测（JSON 272 B / 层、CBOR 288 B / 层）留栈；写缓冲按最坏情况的数据估，`write_to` 的返回值就是准确值——**先量长度再决定缓冲大小**是最省事的办法。

---

## 6. 怎么自测 / 怎么进一步验证

### 6.1 跑本手册的示例（最直接的一条）

```powershell
cd /d E:\01_Workspace\00_Active_projects\00_code\01_cpp_code\etl_fmt
g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I. tests/eserde_manual_examples.cpp -o tests/out/eserde_manual_examples.exe
.\tests\out\eserde_manual_examples.exe
```

真实输出（本手册写作时跑的）：

```text
手册 3.4：同一个 person，JSON 173 字节 / CBOR 99 字节（57%）
手册 4：JSON=[{"boot_delay_ms":250,"gain":1.5,"verbose":true}] 47 字节，CBOR 25 字节
81 checks, 0 failures
```

同一份源码也能按嵌入式配置编（少了 `to_string` 那一条）：

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -DEFMT_ENABLE_HOSTED=0 -Itests/include -I. tests/eserde_manual_examples.cpp -o tests/out/eserde_manual_examples_embedded.exe
.\tests\out\eserde_manual_examples_embedded.exe      # 80 checks, 0 failures
```

### 6.2 跑全量检查

```powershell
.\tests\run_check.ps1            # 宿主 C++17/C++20 + 嵌入式 + 最小裁剪 + 反例 + elog + eserde + ecli
```

里面与 eserde 有关的步骤：

| 步骤 | 编译配置 | 证明什么 |
|---|---|---|
| `eserde schema / caps / tags` | 宿主 | 基座：能力查询 / schema / 标签 / 字段访问 |
| `eserde (embedded configuration)` | `-DEFMT_ENABLE_HOSTED=0` | 基座不依赖宿主特性 |
| `eserde with tag parsing trimmed off` | `-DEFMT_DERIVE_ENABLE_TAGS=0` | 关标签解析后基座仍可用 |
| `eserde::json (serialize / parse)` | 宿主 + 嵌入式 | JSON 行为与错误码 |
| `eserde::json with ETL types` | 装了 ETL 才跑 | `etl::string` / `etl::vector` 同一套代码 |
| `eserde::cbor (RFC 8949 golden bytes / roundtrip)` | 宿主 + 嵌入式 | 写出的字节合法、标准编码器的字节读得回来 |
| `eserde::cbor with ETL types` | 装了 ETL 才跑 | 同上，ETL 容器 |
| `serialize a type without the Serialize capability` | 反例 | 必须**编译失败**，且诊断含"这个类型不能序列化" |
| `deserialize a type without the Deserialize capability` | 反例 | 必须**编译失败**，且诊断含"这个类型不能反序列化" |
| `eserde manual examples (docs stay honest)` | 宿主（装了 ETL 才跑） | **本手册里的每段示例**都还能编、行为还对得上 —— 文档腐烂时这条先红 |

> 这条 pass 编的就是本手册的示例文件 `tests/eserde_manual_examples.cpp`（6.1 里那两条命令和它参数一致）；
> 给手册加内容时记得同步加一份可编译版本，否则这条 pass 证明不了新写的那段。

### 6.3 哪个文件管什么

| 文件 | 管什么 |
|---|---|
| [tests/eserde_manual_examples.cpp](../../tests/eserde_manual_examples.cpp) | **本手册**的每一段代码（81 checks）|
| [tests/eserde_schema_check.cpp](../../tests/eserde_schema_check.cpp) | 基座：能力 / schema / 标签 / `visit_fields` / `field_at` |
| [tests/eserde_json_check.cpp](../../tests/eserde_json_check.cpp) | JSON 全行为：序列化文本、往返、错误码、失败不动对象 |
| [tests/eserde_json_etl_check.cpp](../../tests/eserde_json_etl_check.cpp) | JSON × ETL 类型（含 `truncated` 语义）|
| [tests/eserde_cbor_check.cpp](../../tests/eserde_cbor_check.cpp) | CBOR：RFC 8949 黄金字节、往返、错误码 |
| [tests/eserde_cbor_etl_check.cpp](../../tests/eserde_cbor_etl_check.cpp) | CBOR × ETL 类型 |
| [tests/eserde_no_tags_check.cpp](../../tests/eserde_no_tags_check.cpp) | `-DEFMT_DERIVE_ENABLE_TAGS=0` 的裁剪行为 |
| [tests/eserde_compile_fail_caps.cpp](../../tests/eserde_compile_fail_caps.cpp) | 反例：缺 `Serialize` |
| [tests/eserde_compile_fail_caps_read.cpp](../../tests/eserde_compile_fail_caps_read.cpp) | 反例：缺 `Deserialize` |

### 6.4 自己量一遍（体积 / 栈）

命令模板见 3.9——把 `<你的源文件>` 换成你自己的业务代码，同一个结构体只要换 include 就能量出"加 JSON / 加 CBOR"的差值。

### 6.5 相关文档

- 全量手册（含 `E_FMT_DERIVE` 写法边界、`eserde` 各节、宏总表与体积实测）→ [EFMT-使用手册.md](../EFMT-使用手册.md)
- 命令台里敲着玩（含 JSON/CBOR 的可运行例子）→ [SANDBOX-命令台上手指南.md](../SANDBOX-命令台上手指南.md)
- 沙盒工程的 include 拓扑与宏开关 → [sandbox/README.md](../../sandbox/README.md)
- 同一层的另一个"格式"（也读同一份 schema，吃 `short` / `long` 这类标签）→ [ECLI-命令行解析-方案.md](../ECLI-命令行解析-方案.md)、[ECLI-与clap的差距清单.md](../ECLI-与clap的差距清单.md)
- 第三方冻结副本 matchit（eserde 不依赖它，ecli 的命令名模式段用）→ [matchit/README.md](../../matchit/README.md)