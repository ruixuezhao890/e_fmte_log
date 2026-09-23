# EFmt 使用手册（嵌入式 C++ 格式化库）

> 面向第一次接触 EFmt 的嵌入式开发者：从"复制粘贴就能跑"到"知道每个宏花多少 Flash"。
> 版本：v1.8（elog 支持 ETL 类型） · 适用 C++17 及以上 · 头文件库，无构建系统依赖

---

## 目录

1. [这是什么，30 秒上手](#1-这是什么30-秒上手)
2. [第一个工程：从零到串口打印](#2-第一个工程从零到串口打印)
3. [四个核心接口](#3-四个核心接口)
4. [格式规范语法详解](#4-格式规范语法详解)
5. [类型支持与自定义类型](#5-类型支持与自定义类型)
6. [输出与打印](#6-输出与打印)
7. [嵌入式配置手册（宏总表）](#7-嵌入式配置手册宏总表)
8. [Flash / RAM / 栈占用实测](#8-flash--ram--栈占用实测)
9. [浮点格式化](#9-浮点格式化)
10. [编译期检查](#10-编译期检查)
11. [常见坑与 FAQ](#11-常见坑与-faq)
12. [从 printf / snprintf 迁移对照表](#12-从-printf--snprintf-迁移对照表)
13. [与 elog 一起用](#13-与-elog-一起用)
14. [API 速查](#14-api-速查)
15. [验证与自测](#15-验证与自测)
16. [附录 A：格式规范完整语法](#附录-a格式规范完整语法)
17. [附录 B：宏速查与代价](#附录-b宏速查与代价)
18. [附录 C：版本历史](#附录-c版本历史)

---

## 1. 这是什么，30 秒上手

EFmt 是一个**只有头文件**的 C++17 格式化库（类似 Python 的 f-string 或 C++20 的 `std::format`），
为资源受限的嵌入式环境设计：不抛异常、不用堆、不依赖 libc 的 printf，输出可以导到 UART / RTT /
ITM / SD 卡 / 你自己的缓冲区。

```cpp
#include <middleware/efmt/core/format.hpp>
using namespace e_fmt;

println_info("boot ok, version {}", 1.4);          // 打印（走全局输出处理器）
char line[64];
format_to(line, sizeof(line), "x={} y={}", 10, 20); // 写进缓冲区，可用栈上内存
std::string s = format("PI={:.2f}", 3.14159);       // 宿主环境：返回 std::string
```

三句话理解它：

1. **格式串里的 `{}` 会被后面的实参依次替换**，类型安全、可带格式规范（`{:.2f}`、`{:#06x}`）。
2. **格式化不会"失败"**：参数不够就输出 `{?}`，格式串写错就原样输出，缓冲区不够就按 `snprintf`
   语义截断并由返回值告诉你。
3. **参数个数写错在编译期就报错**（用 `E_FMT_STR` 包住格式串），不会拖到现场才发现。

---

## 2. 第一个工程：从零到串口打印

### 2.1 把库放进工程

EFmt 是纯头文件，只有一条要求：**编译器要能找到 `middleware/efmt/` 这个目录结构**。

```
你的工程/
├── middleware/
│   ├── efmt/core/*.hpp      ← 本库（把仓库里的 efmt 目录改名/软链成 middleware/efmt）
│   └── etl/                 ← 只有用 elog 时才需要
└── src/main.cpp
```

编译时加一个 include 根目录即可（`middleware` 的上一层）：

```bash
# GCC / arm-none-eabi-gcc / ESP-IDF
g++ -std=c++17 -I path/to/你的工程 -O2 src/main.cpp -o app
arm-none-eabi-g++ -std=c++17 -Os -mcpu=cortex-m4 -mthumb -I . src/main.cpp -o app.elf

# Keil / IAR：把 include 根目录加进 "Include Paths" 即可（无需其他配置）
```

> 只需要 `-std=c++17`（或更新）。EFmt 不用异常、不用 RTTI、不用堆，所以
> `-fno-exceptions -fno-rtti` 可以放心开；裸机工程常用 `--specs=nano.specs` 也没问题
> （浮点不再依赖 libc，见 [第 9 章](#9-浮点格式化)）。

### 2.2 第一次打印（STM32 HAL 为例）

```cpp
#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart1;

// 1) 告诉 EFmt 输出到哪里：一个 (数据指针, 长度) 的回调就够了
static void uart_sink(const char* data, size_t size) {
    HAL_UART_Transmit(&huart1, (uint8_t*)data, (uint16_t)size, 1000);
}

int main() {
    // 2) 注册一次，之后所有 print/println 都走它
    e_fmt::set_output_handler(uart_sink);

    // 3) 开始打印
    e_fmt::println_info("System boot");
    e_fmt::println_info("Firmware {} build {}", "1.8.0", 20260322);
    e_fmt::println_warning("battery {}%", 18);
    e_fmt::println_error("sensor 0x{:02X} timeout after {} ms", 0x1A, 250);
    e_fmt::println_info("temp={:.1f}C hum={:.1f}%", 25.5f, 60.0f);

    for (;;) { }
}
```

串口上会看到：

```
System boot
Firmware 1.8.0 build 20260322
battery 18%
sensor 0x1A timeout after 250 ms
temp=25.5C hum=60.0%
```

> 裸机上 `println_info` 的彩色/样式默认是**关闭**的（不会往串口发 `ESC[34m` 这种转义序列）；
> 桌面/树莓派上默认开启颜色。见 [7.2](#72-裁剪宏逐个说明)。

### 2.3 三个必须知道的约束（新手最容易踩）

| 约束 | 说明 | 怎么绕 |
|------|------|--------|
| `print/println` 有栈缓冲上限 | 默认 256 字节，超长会被截断 | 要完整内容用 `format_to(buf, size, ...)`；或调大 `EFMT_PRINT_BUFFER_SIZE` |
| 一次最多 8 个参数（嵌入式默认） | 超出会编译报错（不是运行时） | 拆成两条日志，或调大 `EFMT_MAX_FORMAT_ARGS` |
| 嵌入式默认没有 `std::string` | `format()` 返回 `std::string` 的重载不参与编译 | 用 `format_to` 写进字符数组；或把 `EFMT_ENABLE_DYNAMIC_STRING` 打开 |

---

## 3. 四个核心接口

格式串参数统一接受 `const char*`、字符数组、`std::string`、`std::string_view` 以及
`E_FMT_STR("...")`；**不需要以 `'\0'` 结尾**，`string_view` 按长度解析。

### 3.1 `format_to`：写进缓冲区（嵌入式主力）

```cpp
char buffer[64];
size_t needed = format_to(buffer, sizeof(buffer), "value={}", 1234567890);

// 返回值遵循 snprintf 语义：返回"完整输出需要的长度"
if (needed >= sizeof(buffer)) {
    // 被截断：buffer 里只有前 63 个字符 + '\0'
} else {
    // 完整写入：buffer 里有 needed 个字符 + '\0'
}
```

写进 `std::string`（宿主环境）：

```cpp
std::string out;
format_to(out, "count={}", 42);   // 长度精确，不截断
```

### 3.2 `format`：返回 `std::string`（需要 `EFMT_ENABLE_DYNAMIC_STRING`）

```cpp
std::string s = format("({}, {})", x, y);
```

长度精确、**不会截断**：先写 256 字节栈缓冲（`EFMT_STRING_BUFFER_SIZE`），装不下才按第一遍
算出的精确长度重新分配。嵌入式默认不启用这个接口。

### 3.3 `formatted_size`：只要长度

```cpp
size_t n = formatted_size("id={} name={}", 42, "abc");   // 精确长度，不含 '\0'
char* p = (char*)malloc(n + 1);                           // 需要多大就分配多大
```

在嵌入式里它更有用的场景是**判断这行日志放不放得下**：

```cpp
if (formatted_size("imu {} {} {}", a, b, c) < sizeof(line)) {
    format_to(line, sizeof(line), "imu {} {} {}", a, b, c);
}
```

### 3.4 `print` / `println` 系列：直接出串口

```cpp
// 五个带样式的便捷接口（前缀区分用途，行为一致）
println_error("...");    // 红（宿主）/ 无样式（嵌入式）
println_warning("...");  // 黄 / 无样式
println_info("...");     // 蓝 / 无样式
println_success("...");  // 绿 / 无样式
println_debug("...");    // 灰 / 无样式

// 自定义样式
print_styled(with_color(color::bright_cyan) | style::bold, "highlight {}
", 42);
```

`print` 不换行，`println` 自动补 `'\n'`。

---

## 4. 格式规范语法详解

一个替换字段的形状：

```
{ [索引] [: 格式规范] }
格式规范 = [[填充]对齐] [符号] [#] [0] [宽度] [.精度] [类型]
```

例子：`{|>+#010.3f}` = 用 `|` 填充、右对齐、总是带符号、替代形式、补零、宽 10、精度 3、定点。

### 4.1 对齐与填充

```cpp
format("{:<10}", "text");     // "text      "   左对齐
format("{:>10}", "text");     // "      text"   右对齐（数字默认）
format("{:^10}", "text");     // "   text   "   居中
format("{:*>10}", "text");    // "******text"   自定义填充字符
format("{:0<10}", "text");    // "text000000"
format("{:#^10}", "text");    // "###text###"
format("{:>10.3}", "abcdef"); // "       abc"   填充 + 精度（截断）
```

> 字符串默认**左对齐**，数字默认**右对齐** —— 和 `std::format` 一致。

### 4.2 符号模式

```cpp
format("{:+}", 42);      // "+42"
format("{:+}", -42);     // "-42"
format("{: }", 42);      // " 42"     正数留一个空格（对齐正负号）
format("{: }", -42);     // "-42"
format("{:+}", 42u);     // "+42"
```

### 4.3 宽度、精度、补零

```cpp
format("{:10}", 42);         // "        42"
format("{:08}", 42);         // "00000042"   '0' 选项 = 补零（无显式对齐时生效）
format("{:08.3f}", 3.14159); // "0003.142"
format("{:+.08.3f}", 3.14159); // "+003.142" 符号紧贴数字，零在中间
format("{:.3}", 3.14159);    // "3.14"      浮点：有效数字
format("{:.3}", "abcdef");   // "abc"       字符串：截断
format("{:.0f}", 2.5);       // "2"         半偶舍入（round-half-even）
```

### 4.4 整数：进制与替代形式

| 写法 | 255 的输出 | 说明 |
|------|-----------|------|
| `{}` / `{:d}` | `255` | 十进制 |
| `{:#x}` | `0xff` | 十六进制（小写）带前缀 |
| `{:X}` | `FF` | 十六进制（大写） |
| `{:#X}` | `0XFF` | 大写 + 前缀 |
| `{:o}` / `{:#o}` | `377` / `0377` | 八进制 |
| `{:b}` / `{:#b}` | `11111111` / `0b11111111` | 二进制（调试寄存器很好用） |
| `{:08x}` | `000000ff` | 定宽寄存器打印 |
| `{:.6d}` | `000255` | 整数精度 = 最少位数 |

整数支持全部 64 位范围（`format("{}", UINT64_MAX)` 输出 `18446744073709551615`），
小整数类型（`uint8_t`、`short`、`char` 除外）都走整型通道，不会意外变成字符。

### 4.5 浮点：f / e / g

```cpp
double pi = 3.14159265358979;
format("{}",    pi);        // "3.14159"          默认：6 位有效数字（同 printf 的 %g）
format("{:f}",  pi);        // "3.141593"         定点，默认 6 位小数
format("{:.2f}", pi);       // "3.14"
format("{:e}",  pi);        // "3.141593e+00"     科学计数
format("{:.2e}", pi);       // "3.14e+00"
format("{:g}",  100000.0);  // "100000"           通用：自动选定点/科学
format("{:.3g}", 100000.0); // "1e+05"
format("{:.0f}", 1e308);    // 309 位整数（长度精确，不截断）
```

浮点行为**与 printf 逐位一致**（含 round-half-even 舍入、`inf`/`nan`/`-0.0`、`%g` 的
去尾零规则）。细节与验证方法见 [第 9 章](#9-浮点格式化)。

### 4.6 字符、布尔、指针

```cpp
format("{}", 'A');      // "A"         char
format("{:c}", 65);     // "A"         整数按字符输出
format("{}", true);     // "1"         默认布尔输出 1/0
format("{:s}", true);   // "true"      {:s} 输出 true/false
format("{:s}", false);  // "false"
int x; format("{}", &x);      // "0x7ffd1234abcd"   指针（小写十六进制）
format("{}", (void*)nullptr); // "(nil)"
```

### 4.7 转义花括号

要输出字面量的 `{` 或 `}`，写两遍：

```cpp
format("{{}}");          // "{}"
format("{{{}}}", 42);    // "{42}"
format("{{{{value}}}}"); // "{{value}}"
```
---

## 5. 类型支持与自定义类型

### 5.1 内置类型一览

| 类型 | 支持 | 备注 |
|------|------|------|
| `int` / `long` / `long long` | ✓ | 64 位全范围 |
| `unsigned` 各变体 | ✓ | 不会把 `UINT64_MAX` 当成负数 |
| `short` / `int8_t` / `uint16_t` 等 | ✓ | 统一走整型通道 |
| `float` / `double` | ✓ | 见[第 9 章](#9-浮点格式化) |
| `bool` | ✓ | `{}` → `1/0`，`{:s}` → `true/false` |
| `char` | ✓ | 作为字符输出 |
| `const char*` / `char[]` | ✓ | 带长度，不会 `strlen` 越界 |
| `std::string` / `std::string_view` | ✓ | `std::string` 需 `EFMT_ENABLE_DYNAMIC_STRING` |
| 任意指针 | ✓ | `0x...` 十六进制；空指针 `(nil)` |
| `std::vector` 等容器 | 宿主默认 ✓ / 嵌入式默认 ✗ | 见 5.5；**elog 用户默认 ✓**（见 13.8） |
| `std::tuple` / `std::pair` | 同上 | `(a, b)` / `(k: v)` |
| `etl::string` / `etl::string_view` / `etl::optional` 等 ETL 类型 | 仅 elog | 见 [13.8 ETL 类型支持](#138-etl-类型支持elog-自带无需配置) |

字符串参数是**按长度**传递的，所以空字符串、内嵌 `'\0'` 的缓冲区都安全：

```cpp
char raw[8] = {'a', '\0', 'b'};
format("[{}]", std::string_view(raw, 3));   // "[a\0b]" 里的 3 个字符原样输出
```

### 5.2 自定义类型：四种写法的选择

**方式 1：结构化成成员打印（最省事）** —— 输出 `{x=10, y=20}`

> 嫌下面这种写法要抄"类型 + 名字 + 字符串"三遍？直接用 [5.4 自动派生](#54-自动派生不用再抄类型和名字推荐)：
> `E_FMT_FORMATTER_FIELDS(point, x, y)` 一行搞定，不用写类型也不用写字符串。

```cpp
struct Point { int x, y; };
E_FMT_FORMATTER_FIELDS(Point, x, y);

struct Reading { float temp; float hum; uint32_t ts; };
E_FMT_FORMATTER_FIELDS(Reading, temp, hum, ts);

struct Flag { bool on; };
E_FMT_FORMATTER_FIELDS(Flag, on);

Point p{10, 20};
println_info("{}", p);        // "{x=10, y=20}"
```

**方式 2：自定义输出逻辑（最灵活）** —— 一个 lambda 里想写什么写什么

```cpp
struct Rgb { uint8_t r, g, b; };
E_FMT_FORMATTER_FN(Rgb, [](format_context& ctx, const format_specs& specs, const Rgb& c) {
    // 要支持宽度/对齐：把完整内容拼好，再一次交给 write_aligned
    // （别先 write_char('#') 再 write_aligned(...)，那样 '#' 不参与对齐）
    char text[7];
    text[0] = '#';
    for (int i = 0; i < 3; ++i) {
        // 注意：宏参数里的逗号会拆参数，所以这里不用 {c.r, c.g, c.b} 这种花括号列表
        const uint8_t v = (i == 0) ? c.r : ((i == 1) ? c.g : c.b);
        text[1 + i * 2] = "0123456789abcdef"[v >> 4];
        text[2 + i * 2] = "0123456789abcdef"[v & 0xF];
    }
    ctx.write_aligned(std::string_view(text, 7), specs);
});

Rgb red{255, 0, 0};
println_info("led={}", red);  // "led=#ff0000"
println_info("led={:>10}", red);  // "led=   #ff0000"
```

**方式 3：特化 formatter（库内部风格，零宏）**

```cpp
namespace e_fmt::detail {
template <> struct formatter<MyType> {
    static void format(format_context& ctx, const format_specs& specs, const MyType& v) {
        ctx.write_str(v.to_string());       // 或 ctx.write_aligned(...) 支持宽度
    }
};
}  // namespace e_fmt::detail
```

**方式 4：什么都不写，靠 `operator<<`（仅宿主）**

```cpp
struct Vec { float x, y; };
inline std::ostream& operator<<(std::ostream& os, const Vec& v) { return os << v.x << "," << v.y; }
format("{}", Vec{1.5f, 2.5f});   // 走 <sstream>；嵌入式默认关闭（EFMT_ENABLE_STREAM_FALLBACK）
```

> ⚠️ 方式 4 会拉进 `<sstream>`（Flash 杀手），**嵌入式不要用**，请用方式 1/2/3。
> 没有任何格式化器的类型会打印成 `obj@0x20000123`（类型 + 地址），不会静默输出空白。

**两个容易误解的点**：

1. **方式 1（`E_FMT_FORMATTER_FIELDS`）会忽略外层的格式规范**：成员一律按默认格式输出，
   所以 `format("{:s}", flag)` 里的 `{:s}` 不会传到成员上（bool 成员仍然是 `{on=1}`）。
   需要控制成员格式就用方式 2/3 自己写。
2. **宽度/对齐要自己接**：写了 `ctx.write_char('#') …` 再 `write_aligned(...)` 的话，
   只有后半段参与对齐。正确做法是先组成完整字符串再交给 `write_aligned`。

### 5.3 格式化器里还能再调用 format

格式化器内部可以递归调用 `format`（参数不依赖全局状态）：

```cpp
E_FMT_FORMATTER_FN(Packet, [](format_context& ctx, const format_specs&, const Packet& p) {
    ctx.write_str(e_fmt::format("[{}|{}]", p.id, p.len));   // 注意写 e_fmt::format
});
```

### 5.4 声明即推导：`E_FMT_DERIVE`（对标 Rust 的 `#[derive(Debug)]`）

**结构体和枚举都只写声明** —— 字段名、取值名一个字都不用写：

```cpp
E_FMT_DERIVE(struct imu {
  float ax, ay, az;
});

E_FMT_DERIVE(enum class state {
  idle,
  busy = 5,
  fault
});

println_info("{}", imu{1.5f, 2.5f, 3.5f});   // { ax = 1.5, ay = 2.5, az = 3.5 }
println_info("{}", state::busy);             // busy
```

嵌套、数组、位域、默认值、静态成员、成员函数、函数指针、命名空间，全都只用写声明：

```cpp
E_FMT_DERIVE(struct frame {
  unsigned seq;
  imu sample;                // 内层也推导了 → 递归带名字
  raw_pair pair;             // 内层没推导 → 位置式 (4, 5)，不用你操心
  float cal[3];              // 数组 → [1.5, 2.5, 3.5]
  char tag[8];               // char 数组 → 字符串 imu0
  unsigned flags : 3;        // 位域 ✓（GCC 下 std::tie 绑位域会打 0，这里是直接传引用，正确）
  int gain = 2;              // 默认成员初始化 ✓
  void (*callback)(int);     // 函数指针 → 十六进制地址 / (nil)
  static const int kMax = 8; // 静态成员：自动跳过
  int getSeq() const;        // 成员函数：自动跳过
});
```

**它是怎么做到的**（全在编译期，运行时零开销）：

1. 宏在声明后面追加一个 `extern` 声明，用 `decltype` 抓住刚声明的类型（不占存储、类型名不用重复）；
2. 用 `#__VA_ARGS__` 把声明文本在**编译期解析**出字段名/枚举名；
3. 在同一作用域生成一个 ADL 自由函数，把名字表挂到类型上（宏声明了类型 ⇒ ADL 必然找得到，命名空间里的类型也不会出错）；
4. 取值用结构化绑定后**直接**交给格式化器；
5. 解析出的字段数就是结构化绑定的数量 —— **对不上就编译报错**，绝不静默输出错名字。

**边界（超出这些写法会明确报错，并提示改用下面的兜底写法）**

| 写法 | 支持 | 说明 |
|------|------|------|
| 结构体 / 枚举 / 嵌套 / 数组 / 位域 / 默认值 / 静态成员 / 成员函数 / 函数指针 / 命名空间 | ✓ | 见上面的例子 |
| 枚举取值：自增、`= 5`、`= 0x10`、负数 | ✓ | 未列出的取值打印底层整数 |
| 枚举取值是**非字面量**（`A = 1 << 3`、`B = A + 1`） | ✗ | 报错 → 用 `E_FMT_FIELDS(取值名, ...)` 显式列出，或手写 formatter |
| 声明里出现 `#if` / `#include` / 宏调用 | ✗ | 整个声明是宏的参数 → 用 `E_FMT_FIELDS(字段, ...)` 写在类型内部 |
| 模板结构体 | ✗ | 同上 |
| 基类 / 私有成员 | ✗ | 结构化绑定本身就不支持 |
| 超过 `EFMT_DERIVE_MAX_FIELDS`（默认 16） | ✗ | 调大宏或改用其它写法 |

**兜底写法一：类型里写一行 `E_FMT_FIELDS`**（已有类型、需要 `#if`、模板结构体、超过上限时用它）

```cpp
struct cfg {
  int retry;
  bool verbose;
  E_FMT_FIELDS(retry, verbose);      // 只列名字；类型/字符串/成员指针全自动
};

struct with_platform_field {
  int base;
#ifdef STM32
  int extra;
  E_FMT_FIELDS(base, extra);         // 声明里有 #if，E_FMT_DERIVE 做不到，这里可以
#else
  E_FMT_FIELDS(base);
#endif
};

template <typename T>                 // 模板结构体也可以（E_FMT_DERIVE 做不到）
struct box {
  T value;
  int tag;
  E_FMT_FIELDS(value, tag);
};
```

两种写法可以互相嵌套（`E_FMT_FIELDS` 的类型作为 `E_FMT_DERIVE` 类型的成员，反之亦然），
输出风格一致。

**兜底写法二：**`E_FMT_FORMATTER_FN` / `E_FMT_FORMATTER_AUTO` 等仍可用。`E_FMT_FORMATTER_1/2/3`（抄三遍写法）已删除，请改用 `E_FMT_FORMATTER_FIELDS` 或 `E_FMT_DERIVE`。

**输出与体积开关**

| 宏 | 默认 | 作用 |
|----|------|------|
| `EFMT_DERIVE_SHOW_TYPE` | 0 | 输出带不带类型名：`{ ax = 1.5 }`（默认，省 Flash）vs `imu { ax = 1.5 }`（开它 **+1.35 KB Flash**，Cortex-M4 实测） |
| `EFMT_DERIVE_MAX_FIELDS` | 16 | 单类型字段/取值上限 |
| `EFMT_DERIVE_MAX_ARRAY_ITEMS` | 8 | 数组成员最多打几个元素，超出 `...` |
| `EFMT_DERIVE_STRICT` | 1 | 成员没有格式化器 → 编译报错（Rust 行为）；设 0 退回 `obj@地址` |
### 5.5 容器与 tuple（默认只在宿主可用）

```cpp
std::vector<int> v{1, 2, 3};
format("{}", v);                                // "[1, 2, 3]"
std::map<std::string, int> m{{"a", 1}};
format("{}", m);                                // "{a: 1}"
format("{}", std::make_tuple(1, 2.5, "x"));     // "(1, 2.5, x)"
format("{}", std::make_pair("k", 7));           // "(k: 7)"
```

独立使用 efmt 时嵌入式默认**关闭**（省 Flash，也避免 MCU 日志里出现容器）；
需要时 `-DEFMT_ENABLE_CONTAINER_FORMAT=1`。**elog 用户默认已打开**（见
[13.8](#138-etl-类型支持elog-自带无需配置)），MCU 上直接能打。

关闭后的行为：普通容器（`std::vector` / `etl::vector` 等）参数退化为 `obj@0x...`
地址兜底，不会静默打错内容；聚合数组（`std::array` / `etl::array`）在
`EFMT_DERIVE_STRICT=1` 下直接编译报错（提示找不到格式化器）。

> 实测（Cortex-M4 `-Os`）：容器宏开着但不用容器 = **零 Flash 开销**（模板惰性
> 实例化）；真的打一个 `etl::vector<int,8>` 才 +260 B。因此 elog 默认打开它没有
> 隐性成本。

---

## 6. 输出与打印

> `println_*` 是无状态的快捷打印。**要分级、要过滤、要上线关日志**的日志直接用 elog
> （[第 13 章](#13-与-elog-一起用)）；elog 的 `stdout_sink()` 走的就是这里的全局输出处理器，
> 两条路可以输出到同一条通道。

### 6.1 输出处理器：一个函数指针搞定

```cpp
using output_fn = void(*)(const char* data, size_t size);

set_output_handler(fn);     // 设置
get_output_handler();       // 读取
reset_output_handler();     // 恢复默认（宿主 = stdout，嵌入式 = 丢弃）
```

常见外设的接法：

```cpp
// UART（STM32 HAL）：注意 HAL 形参是 uint8_t*
static void uart_sink(const char* d, size_t n) { HAL_UART_Transmit(&huart1, (uint8_t*)d, (uint16_t)n, 100); }

// Segger RTT
static void rtt_sink(const char* d, size_t n) { SEGGER_RTT_Write(0, d, (unsigned)n); }

// ITM（Cortex-M 调试口，不需要串口线）
static void itm_sink(const char* d, size_t n) { for (size_t i = 0; i < n; ++i) ITM_SendChar(d[i]); }

// FatFS 写 SD 卡
static void sd_sink(const char* d, size_t n) { UINT w; f_write(&log_file, d, (UINT)n, &w); }

// DMA 环形缓冲：只做拷贝，不阻塞（ISR 友好）
static void ring_sink(const char* d, size_t n) { ring_push_isr(d, n); }
```

### 6.2 输出到内存缓冲区（测试 / 屏显）

```cpp
char buf[256];
set_buffer_output(buf, sizeof(buf));
println_info("hello {}", 42);
size_t used = get_buffer_output_pos();   // 已写入的字节数（不含 '\0'）
buf[used] = '\0';                         // EFmt 不代写结束符
...
reset_buffer_output_pos();               // 清空重来（不清零内容）
reset_output_handler();                  // 恢复默认输出
```

> 缓冲区满了会**丢弃**多余字节（`pos` 不再增长），不会越界。

### 6.3 丢弃输出 / 临时改道

```cpp
set_output_handler(null_output_handler);          // 全丢（发布版关日志）
{
    e_fmt::detail::output_handler_scope scope(my_sink);   // 作用域内改道，退出自动还原
    println_info("only here");
}
```

### 6.4 颜色与样式（宿主）

```cpp
println_error("Error: {}", code);                          // 红+粗
println_warning("Warn");                                   // 黄
println_success("Done");                                   // 绿+粗
println_info("Info");                                      // 蓝
println_debug("Debug");                                    // 灰暗

print_styled(with_color(color::bright_cyan), "cyan\n");
print_styled(with_colors(color::white, bg_color::blue), "white on blue\n");
print_styled(with_color(color::yellow) | style::bold | style::underline, "styled\n");
```

可用 `color::`/`bg_color::` 共 16 色（含 `bright_*`），`style::` 有
`bold dim italic underline blink reverse hidden strikethrough`。

嵌入式（`EFMT_ENABLE_HOSTED=0`）默认 `EFMT_ENABLE_ANSI_STYLES=0`：**样式接口照常可用，
但不产生任何字节** —— 串口里不会混入 `ESC[34m`。手动关闭也是同样效果：

```cpp
#define EFMT_ENABLE_ANSI_STYLES 0      // 必须在包含头文件之前
#include <middleware/efmt/core/format.hpp>
```

### 6.5 线程 / 中断安全

| 操作 | 安全性 |
|------|--------|
| `format` / `format_to` / `formatted_size` | **可重入**，无共享可变状态，多线程/中断里都能用 |
| `print` / `println` 系列 | 共享全局输出处理器，多线程同时打印需要自己加锁 |
| `set_output_handler` | 建议初始化阶段设置一次，运行期不要再改 |

中断里打日志的建议：中断只把数据丢进环形缓冲（用 `format_to` 格式化到局部数组，
再 `ring_push_isr`），主循环负责真正发串口 —— 避免在 ISR 里做长时间阻塞的 UART 发送。

---

## 7. 嵌入式配置手册（宏总表）

### 7.1 平台总开关

```cpp
#define EFMT_ENABLE_HOSTED 0     // 0 = 嵌入式，1 = 宿主；也可 -DEFMT_ENABLE_HOSTED=0
```

不写时按下面的规则自动判定：

| 条件 | 判定 |
|------|------|
| `__STDC_HOSTED__ == 0`（标准 freestanding） | 嵌入式 |
| `__AVR__` / `__MSP430__` / `ARDUINO` / `__MBED__` | 嵌入式 |
| `__arm__` / `__thumb__` 且不是 Linux/Unix（即 Cortex-M 裸机/RTOS） | 嵌入式 |
| 其余（Windows / Linux / **ESP-IDF**） | 宿主 |

> **ESP32 注意**：ESP-IDF 默认按宿主处理（有完整 libc，`std::string` 可用）。
> 想给 ESP32 极限压缩，显式加 `-DEFMT_ENABLE_HOSTED=0`。
> 32 位 ARM Linux（树莓派）现在也正确地按宿主处理了。

### 7.2 裁剪宏逐个说明

| 宏 | 宿主默认 | 嵌入式默认 | 作用 | 关掉的代价 |
|----|---------|-----------|------|-----------|
| `EFMT_ENABLE_HOSTED` | 1 | 自动 | 总开关 | — |
| `EFMT_ENABLE_DYNAMIC_STRING` | `HOSTED` | 0 | `std::string` 重载、`format()` | 只能用 `format_to` 写字符数组 |
| `EFMT_ENABLE_STREAM_API` | `HOSTED` | 0 | `std::ostream` 接口 | 不能用 `std::cout` |
| `EFMT_ENABLE_STREAM_FALLBACK` | `STREAM_API && HOSTED` | 0 | 用 `operator<<` 兜底格式化 | 没有 formatter 的类型打印成 `obj@0x...` |
| `EFMT_ENABLE_ANSI_STYLES` | `HOSTED` | 0 | 输出 ANSI 颜色 | `println_error` 等变成纯文本（接口不变） |
| `EFMT_ENABLE_STDIO` | `HOSTED` | 0 | 默认输出到 stdout | 必须自己 `set_output_handler` |
| `EFMT_ENABLE_FLOAT` | 1 | 1 | 浮点通道 | **写浮点参数直接编译报错**（省 ~3.4 KB） |
| `EFMT_USE_LIBC_PRINTF` | 1 | 0 | 浮点是否借用 libc 的 `snprintf` | 用自带引擎（无堆、newlib-nano 也能用） |
| `EFMT_ENABLE_CONTAINER_FORMAT` | `HOSTED` | 0 | 容器/tuple/pair 格式化 | 关闭后打容器退化为 `obj@0x...`（聚合数组在 STRICT 下编译报错）；**elog 默认已开** |
| `EFMT_MAX_FORMAT_ARGS` | 16 | 8 | 单次调用参数上限（每个约 24 B 栈） | 超出的调用编译报错 |
| `EFMT_PRINT_BUFFER_SIZE` | 256 | 256 | `print/println` 栈缓冲 | 单行超长被截断 |
| `EFMT_STRING_BUFFER_SIZE` | 256 | — | `format()` 的一次性栈缓冲 | 超长会二次分配（只影响宿主） |
| `EFMT_FLOAT_BIGNUM_LIMBS` | 48 | 48 | 浮点定点大整数容量（32 位 limb） | 精度上限下降 |
| `EFMT_FLOAT_DIGIT_GROUPS` | 48 | 48 | 浮点一次能产生的数字组（每组 9 位） | 输出位数上限下降 |

### 7.3 三套推荐配置（照抄即可）

**A. 典型 STM32（有浮点、有日志、栈 2 KB+）**

```bash
arm-none-eabi-g++ -std=c++17 -Os -mcpu=cortex-m4 -mthumb \
  --specs=nano.specs --specs=nosys.specs \
  -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
  -I middleware_root src/main.cpp -Wl,--gc-sections -o app.elf
```

不用写任何 EFmt 宏：Cortex-M 自动判定为嵌入式，浮点走自带引擎、无堆依赖。

**B. 极小固件（不打印浮点、参数 ≤ 4、单行 ≤ 128 B）**

```bash
-DEFMT_ENABLE_HOSTED=0 -DEFMT_ENABLE_FLOAT=0 -DEFMT_MAX_FORMAT_ARGS=4 \
-DEFMT_PRINT_BUFFER_SIZE=128 -DEFMT_ENABLE_CONTAINER_FORMAT=0
```

**C. 桌面 / 树莓派 / ESP-IDF（要 `std::string`、要颜色、要容器）**

```bash
# 什么都不用定义，默认就是宿主模式
g++ -std=c++17 -O2 -I middleware_root src/main.cpp -o app
```

### 7.4 编译期自检（建议加进工程）

```cpp
// 确保配置符合预期，配置漂了立刻编译失败
static_assert(EFMT_MAX_FORMAT_ARGS >= 8, "日志最多会用 8 个参数");
#if !EFMT_ENABLE_HOSTED
static_assert(!EFMT_ENABLE_ANSI_STYLES, "嵌入式不要往串口发转义序列");
#endif
```

---

## 8. Flash / RAM / 栈占用实测

### 8.1 怎么量的（可复现）

```powershell
.\tests\run_check.ps1 -Size      # 需要 arm-none-eabi-g++ / xtensa-esp32-elf-g++
```

同口径：`-Os`、`-ffunction-sections -fdata-sections`、`-Wl,--gc-sections`、
`-fno-exceptions -fno-rtti`、newlib-nano。数字是**整个可执行文件**（含 libc 里被拉进来的部分），
所以能反映真实固件增量。

### 8.2 Cortex-M4（newlib-nano，`-Os`）实测

**同一份测试源码**的优化前后对比（这是唯一公平的比法）：

| 场景（同一份 .cpp） | .text | .data | .bss | 拉进堆/printf |
|------|-------|-------|------|--------------|
| 只格式化整数/字符串（优化前） | 3592 B | 108 | 564 | 否 |
| 只格式化整数/字符串（优化后） | **3288 B** | 108 | 564 | 否 |
| 再加一处 `{:.2f}`（优化前，走 libc） | 7133 B | 112 | 584 | **是（malloc/free/sbrk）** |
| 再加一处 `{:.2f}`（优化后，自带引擎） | **6552 B** | 108 | 564 | **否** |

也就是说：整数/字符串路径 **-304 B**；一处浮点带来的增量从 +3541 B 降到 +3264 B，
并且**不再需要堆**（newlib 的 dtoa 会 `malloc`）。

当前出厂配置（`run_check.ps1 -Size` 直接输出，源码为库自带的测试程序）：

| 配置 | Cortex-M4 | ESP32（xtensa） |
|------|-----------|----------------|
| 默认（含自带浮点，完整功能测试） | 8340 B text / 108 data / 48 bss | 9359 B / 20 / 56 |
| 最小裁剪（无浮点、4 参数、无容器） | **3960 B** | **5150 B** |
| derive 样例（`E_FMT_DERIVE` × 3，见 8.2b） | 8376 B / 108 / 44 | 8719 B / 20 / 52 |

要点：

* 嵌入式默认配置不再需要 libc 的浮点 printf —— 优化前仅仅一处 `{:.2f}` 就会把
  `_malloc_r / _free_r / _sbrk` 拉进固件（newlib 的 dtoa 会动态分配），现在这条链没有了。
* 完全关掉浮点（`-DEFMT_ENABLE_FLOAT=0`）时最小裁剪配置在 Cortex-M4 上是 **3960 B**
  （另一份更小的测试程序，见 `-Size` 输出）。
* 默认/最小样例里的数字含 `E_FMT_STR` 快路径执行器（样例自带一行编译期校验调用，约 +80 B，
  opt-in：代码里不用 `E_FMT_STR` 就不链入）。不用 checked 串的同一份默认样例是 **8260 B**。

### 8.2b 自定义类型推导的代价（`E_FMT_DERIVE` vs 手写宏）

同一份输出任务、Cortex-M4 `-Os`：

| 版本 | .text | 对比 |
|------|-------|------|
| 完全不格式化（基线） | 452 B | — |
| 老的结构化宏 `E_FMT_FORMATTER_3`（抄三遍写法，**已删除**；数字为历史测量） | 6548 B | 基准 |
| **`E_FMT_DERIVE(struct imu { … });`（默认）** | **6692 B** | **+144 B** |
| `E_FMT_DERIVE` + `EFMT_DERIVE_SHOW_TYPE=1`（带类型名） | 8044 B | +1496 B |
| 嵌套两个类型：老宏 7728 B / `E_FMT_DERIVE` 8080 B | | +352 B |

结论：**推导的代价是每个类型约 40~90 B**（字段越多越接近上限；换来字段名与类型名零手写、
漏字段编译报错），而"输出带类型名"这一项单独就要 1.35 KB —— 所以默认关
（`EFMT_DERIVE_SHOW_TYPE=0`）。解析本身是**编译期**的，运行时零开销，也不占 RAM。

代价下降来自**静态名字表按字段数缩放**：旧实现每个 `E_FMT_DERIVE` / `E_FMT_FIELDS` 实例
都放一张固定 400 B 的表（16 条 × string_view + value，用不到也占）；现在表长 = 实际条目数
（`FIELDS` 用宏参数个数，`E_FMT_DERIVE` 先按上限解析拿 count、再按 count 精确定位）。
同一份三类型样例（3 字段结构体 ×2 + 枚举 ×1）实测 Cortex-M4 `-Os`：8632 → **8220 B**（-412 B）；
`-Size` 的 derive 行即是这份样例（含整数/浮点新路径后的完整数字）。

### 8.3 栈占用实测（`-fstack-usage`，GCC x64 宿主口径，比值可参考）

| 路径 | 优化前 | 优化后 |
|------|-------|-------|
| 日志 TU 内最大的 `format_to` 帧（嵌入式，参数上限 8） | 576 B | **384 B** |
| elog 一次 `info("...", 2 个参数)` 整帧（嵌入式） | 960 B | **896 B** |
| 浮点格式化（自带引擎，`{:.2f}`） | — | 608 B（`format_fixed`）+ 416 B（分派） |

浮点路径的栈占用可以用宏压小（见 9.4），或者干脆关掉浮点。

### 8.4 速度实测（GCC x64 `-O2`，`tests/run_check.ps1 -Bench`）

| 用例 | 优化前 | 优化后（libc 浮点） | 优化后（自带浮点） |
|------|-------|-------------------|------------------|
| 日志式记录（`[{}] [{}:{} {}] {}`，char 缓冲） | 72.9 ns | 80.3 ns* | 71.1 ns |
| `format("{:.2f}|{:<12}|{:#06x}")` → string | 324.4 ns | 318.9 ns | **184.2 ns** |
| `format("x={}, y={}, z={}")` → string | 115.3 ns | 109.0 ns | **94.2 ns** |
| `formatted_size("Value: {} of {}")` | 37.4 ns | 33.6 ns | **28.0 ns** |
| 长文本（200 字符）→ string | 368.2 ns | 375.5 ns | **243.8 ns** |
| 同一混合规范 + `E_FMT_STR`（编译期预解析） | — | 276.5 ns | **130.2 ns** |
| 同一三字段 + `E_FMT_STR`（编译期预解析） | — | 86.4 ns | 88.8 ns |
| elog 一行日志（2 个参数，嵌入式配置） | 197.3 ns | 112.7 ns | **113.3 ns** |
| elog 一行日志（3 个参数含 `{:.1f}`） | 511.5 ns | 430.6 ns | **171.7 ns** |

\* 该行是 10 万次量级的抖动范围（±10%），重复运行会在 71–80 ns 之间；有意义的对比是同一
二进制内的相对差异：**自带浮点引擎比 libc 的 `snprintf` 快 40%+**，elog 单遍直写快 23%。

elog 的逐项实测（含"被过滤级别"的零成本证据）见 [13.6](#136-elog-性能实测gcc-x64--o2可复现)。

### 8.4b 嵌入式侧相对周期（QEMU `-icount`，Cortex-M4，可复现）

QEMU 里的 CPU 周期计数（x64 上面向 MCU 的 ns 数没有参考价值，M4 指令成本分布不同）。
`tests/run_check.ps1 -QemuBench` 在 QEMU `mps2-an386` + `-icount` + SysTick 计时
（实测 1 tick ≈ 570 条 guest 指令，同一环境标定），**确定性可复现**，适合相对对比；
不代表真板时序（无缓存/流水线/访存延迟）。

| 用例 | ticks/op | ≈指令/op |
|---|---:|---:|
| `int {}` | 0.50 | ~285 |
| 混合规范 `{:<12}\|{:>8.2f}\|{:#06x}` | 2.52 | ~1436 |
| 三字段 `x={}, y={}, z={}` | 1.57 | ~895 |
| 混合规范 + `E_FMT_STR` | 1.71 | ~975（-32%）|
| 三字段 + `E_FMT_STR` | 1.22 | ~695（-22%）|
| `{:.2f}` | 1.36 | ~775 |
| `{:e}` | 1.95 | ~1112 |
| `{:g}` | 2.00 | ~1140 |
| 200 字符长文本 | 5.57 | ~3175 |
| `E_FMT_DERIVE` 结构体 | 0.98 | ~559 |
| `E_FMT_DERIVE` 枚举 | 0.30 | ~171 |

结论：**`E_FMT_STR` 编译期预解析在 M4 上与宿主一致快 22~32%**；单次整数格式化约
285 条指令级；浮点 `{:.2f}` 约 775 条指令级。真要真板绝对数，用 STM32CubeIDE 的
cycle-accurate 仿真或真机 DWT 复核。

---

## 9. 浮点格式化

### 9.1 为什么不用 libc 的 `%f`

在 `-Os` 的 Cortex-M4 + newlib-nano 上实测：

| 做法 | 代价 |
|------|------|
| libc `snprintf("%.*f")` | +3541 B Flash，并且链入 `_malloc_r/_free_r/_sbrk`（printf 的 dtoa 会分配内存） |
| newlib-nano + 没加 `-u _printf_float` | `%f` 直接**打印空白**（新手最常见的"浮点打不出来"原因） |
| EFmt 自带引擎 | +3264 B Flash（含整数路径共用的除法辅助），**不需要堆**，newlib-nano 下直接可用 |

### 9.2 默认走哪条路

| 构建 | 默认浮点实现 | 想换怎么办 |
|------|-------------|-----------|
| 嵌入式（`EFMT_ENABLE_HOSTED=0`） | 自带引擎 | `-DEFMT_USE_LIBC_PRINTF=1` 换回 libc |
| 宿主（Windows/Linux/ESP-IDF） | libc `snprintf` | `-DEFMT_USE_LIBC_PRINTF=0` 用自带引擎 |

两条路的输出**逐位一致**（这正是 `tests/efmt_float_check.cpp` 在验证的：随机位模式 + 各种
规范组合，共 24.6 万次比对，0 失败）。

### 9.3 自带引擎的覆盖范围

* 任意 `double`（含次正规数、`DBL_MAX`、`-0.0`、`inf`、`nan`）都能精确输出；
* `%f` 精度 ≤ 600、`%e`/`%g` 精度 ≤ 400 与 printf 逐位一致；
* 输出总长度上限由 `EFMT_FLOAT_DIGIT_GROUPS` 决定（默认 48 组 = 423 位数字）。

超出容量时**自动退到最后能算准的精度并按原精度补零**：不越界、不错位，但那一串数字会与
printf 不同（只有 `{:.900f}` 这种极端请求才会碰到）。要"任意精度都精确"：

```bash
-DEFMT_FLOAT_BIGNUM_LIMBS=80 -DEFMT_FLOAT_DIGIT_GROUPS=120   # 栈多花约 400 B，覆盖全部 double
```

### 9.4 精度与栈的取舍

| `EFMT_FLOAT_BIGNUM_LIMBS` | 定点容量 | 覆盖的 `%f` 精度 | 栈 |
|---------------------------|---------|------------------|----|
| 24 | 768 bit | ≤ 300 | ~96 B |
| **48（默认）** | 1536 bit | ≤ 600 | ~192 B |
| 80 | 2560 bit | 全部（含次正规数任意精度） | ~320 B |

### 9.5 与 C++20 `std::format` 的差异（提前知道不踩坑）

| 写法 | EFmt | `std::format` |
|------|------|----------------|
| `format("{}", 3.14)` | `3.14`（`%g` 风格，6 位有效数字） | `3.14`（最短往返表示） |
| `format("{}", 100000000.0)` | `1e+08` | `1e+08` |
| `format("{}", 0.1)` | `0.1` | `0.1` |
| `format("{:d}", 3.0)` | `3`（按定点，与 printf 一致） | 编译期报错（不允许） |
| 参数个数不匹配 | 需要 `E_FMT_STR` 才在编译期报错 | 一律编译期报错 |

EFmt 的选择是**对齐 printf**，因为嵌入式开发者最熟悉 printf 的输出，也方便和已有日志对账。
---

## 10. 编译期检查

### 10.1 `E_FMT_STR`：让编译器检查参数个数

普通字符串字面量没法在编译期数占位符；用 `E_FMT_STR` 包一下就行：

```cpp
format(E_FMT_STR("Hello {}"), "World");     // ✓
format(E_FMT_STR("x={}, y={}"), 10, 20);    // ✓
format(E_FMT_STR("x={}, y={}"), 10);        // ✗ 编译失败
// error: static assertion failed: Number of arguments does not match format string
```

`E_FMT_STR` 生成的对象可以隐式转成 `std::string_view`，所以**任何接受格式串的接口都能用**
（`format` / `format_to` / `formatted_size` / `println_*`）。

### 10.1b `E_FMT_STR` 顺带提速：编译期预解析格式规范

`E_FMT_STR` 不止数参数个数，还会在**编译期**把格式串拆好：字段位置、参数索引、
每个字段的格式规范（同一套 `parse_format_spec` 的 constexpr 版）全部预先算好。
运行期只剩 字面量 memcpy + 参数格式化，不再扫括号、不再逐字段解析规范。
宿主实测（GCC x64 `-O2`，与 [8.4](#84-速度实测gcc-x64--o2可复现) 同口径）：

| 用例 | 运行期路径 | `E_FMT_STR` 快路径 |
|------|-----------|-------------------|
| 混合规范 `{:<12}\|{:>8.2f}\|{:#06x}`（自带浮点） | 177.5 ns | **130.2 ns**（-27%） |
| 三字段 `x={}, y={}, z={}` | 106.4 ns | **88.8 ns**（-17%） |

注意：

* 含 `{{ / }}` 转义的格式串自动**退回运行期路径**——输出逐字节一致，只是不省这部分时间；
* 快路径只在你真的写 `E_FMT_STR` / `E_FMT_DECLARE_STR` 时生效；普通 `const char*` 格式串
  照旧走运行期路径，行为完全不变；
* 嵌入式体积：快路径执行器是**一份共享的非模板函数**，不会按调用点复制；只有代码里
  用了 `E_FMT_STR` 才会被链接进固件（Cortex-M4 `-Os` 实测约 +80 B），不用则零开销。

### 10.2 `E_FMT_DECLARE_STR`：声明一次，多处复用

```cpp
E_FMT_DECLARE_STR(coord_fmt, "({}, {})");   // 命名空间作用域，参数个数自动推导

for (const auto& p : points) {
    println_info(coord_fmt, p.x, p.y);      // 也适用于 print/println 系列
}
```

旧写法 `E_FMT_DECLARE_STR_1..5`、`E_FMT_DECLARE_FMT`、`E_FMT_COMPILE_TIME_STRING` 仍然可用，
但 `_N` 后缀不再表示参数个数（个数以格式串为准）。

### 10.3 其他会在编译期拦住的错误

| 情况 | 报错信息关键字 |
|------|---------------|
| 参数个数不匹配（用了 `E_FMT_STR`） | `Number of arguments does not match format string` |
| 参数超过 `EFMT_MAX_FORMAT_ARGS` | `Too many format arguments (see max_format_args)` |
| 关掉浮点后仍传浮点 | `EFMT_ENABLE_FLOAT=0: 本配置不编译浮点格式化…` |
| 关掉容器后仍传容器 | 退化为 `obj@0x...`（聚合数组如 `std::array` / `etl::array` 在 `EFMT_DERIVE_STRICT=1` 下编译报错） |
| 类型没有格式化器 | `Type T does not have a formatter defined…`（宿主下会退化成 `obj@0x...`） |

### 10.4 运行期不会崩

```cpp
format("{}, {}", 1);            // "1, {?}"      参数不够 -> 缺的地方是 {?}
format("{}", 1, 2);             // "1"           多余参数忽略
format("{abc", 1);              // "{abc"        结构不合法 -> 格式串原样输出
format("a}b", 1);               // "a}b"
format("{:9000}", 1);           // 9000 个字符，长度精确
```

---

## 11. 常见坑与 FAQ

**Q1：浮点打出来是空的 / 只有整数部分？**
你用 libc 的 `%f` 且链的是 newlib-nano。三个办法：加 `-u _printf_float`（+3.5 KB 和无堆保证），
或者用 EFmt 的嵌入式默认配置（自带引擎，推荐），或 `-DEFMT_ENABLE_FLOAT=0` 索性不打印浮点。

**Q2：串口里出现 `ESC[34m` 这种乱码？**
你在宿主模式（或老版本）下打开了 ANSI。嵌入式配置现已默认关闭；宿主手动关：
`-DEFMT_ENABLE_ANSI_STYLES=0`。v1.4 修掉了"关了还在发转义序列"的 bug。

**Q3：日志只打印了半行就断了？**
`print/println` 用的是 256 字节栈缓冲（`EFMT_PRINT_BUFFER_SIZE`）。要么调大它，要么用
`format_to(buf, sizeof(buf), ...)` 自己管理缓冲并检查返回值。

**Q4：程序跑着跑着 HardFault，和格式化有关吗？**
最常见的是**栈不够**：一次 `format_to` 大约要 `24 B × 参数个数`（默认 8 个 = 192 B）+
调用者缓冲；带浮点再叠加约 600 B。要么加大任务栈（FreeRTOS `configMINIMAL_STACK_SIZE`），
要么降 `EFMT_MAX_FORMAT_ARGS`、关浮点。用 `-fstack-usage` + `tests/run_check.ps1 -Size` 可以量。

**Q5：`format("{}", some_struct)` 打出了 `obj@0x20000040`？**
该类型没有格式化器，走了兜底输出。用 `E_FMT_FORMATTER_FIELDS` 或 `E_FMT_FORMATTER_FN` 补上。

**Q6：`std::string s = format(...)` 在嵌入式编译不过？**
嵌入式默认 `EFMT_ENABLE_DYNAMIC_STRING=0`（无 `std::string`）。用 `format_to(char*, size)`，
或显式开 `-DEFMT_ENABLE_DYNAMIC_STRING=1`（前提是你的工具链有完整 `std::string`）。

**Q7：`format("{}", std::vector<int>{1,2,3})` 打出了 `obj@0x...`？**
独立使用 efmt 时嵌入式默认关容器格式化：关闭后容器参数退化为地址兜底（`EFMT_DERIVE_STRICT=1`
下聚合数组如 `std::array` / `etl::array` 会直接编译报错）。`-DEFMT_ENABLE_CONTAINER_FORMAT=1`
打开即可——实测不用容器零开销，用容器才 +260 B（Cortex-M4 `-Os`）。**elog 用户默认已打开**，无需任何配置。

**Q8：小数四舍五入和我想的不一样？**
printf 与 EFmt 都用 **round-half-even**（银行家舍入）：`{:.0f}` 对 `0.5` 是 `0`、对 `2.5` 是 `2`。
需要商业舍入请自己 `+0.5` 后取整。

**Q9：`{:.2f}` 打印 2.675 得到 2.67？**
不是 bug：`2.675` 的二进制值略小于 2.675，printf 也是 `2.67`。

**Q10：能打印中文吗？**
能，按字节原样输出（UTF-8 源码即可）：`println_info("温度 {:.1f}C", t);`。
注意字段宽度是按**字节**算的，一个汉字占 3 字节。

**Q11：中断里能打印吗？**
格式化本身可重入、无锁，但 `println_*` 会直接调你的 UART 回调（可能阻塞）。
建议 ISR 里只 `format_to` 到局部数组 + 入环形缓冲，主循环负责发送。

**Q12：多任务同时打印会乱序吗？**
会。全局输出处理器是共享的，需要自己加锁或每个任务一个 sink。

**Q13：`{:#b}` 打印 32 位寄存器怎么对齐？**
`format("{:#034b}", reg)` → `0b` + 32 位。或 `"{:08X}"` 更紧凑。

**Q14：参数超过 8 个怎么办？**
拆成两条日志，或 `-DEFMT_MAX_FORMAT_ARGS=16`（每次调用多占 192 B 栈）。

**Q15：格式串里的 `{}` 想原样输出？**
写 `{{}}`。

**Q16：能在格式化器里递归调用 `format` 吗？**
能，但要写全名 `e_fmt::format(...)`（宏里是成员函数上下文，不加限定会递归到成员）。

**Q17：为什么 `format("{}", 3.14)` 是 6 位有效数字？**
对齐 printf 的 `%g`。要更多位数写 `{:.10g}` 或 `{:.10f}`。

**Q18：`format("{:d}", 3.14)` 会怎样？**
按定点输出 `3`（与 printf 的 `%.0f` 类似）。这是有意为之的历史行为。

**Q19：编译报 `underlying type mismatch` / 找不到 `middleware/efmt/...`？**
include 根目录要指向 `middleware` 的**上一层**，保证 `middleware/efmt/core/format.hpp` 能被找到。

**Q20：要不要 `-fno-exceptions`？**
要。本库不抛异常，加上可以再省一点体积，行为不变。

**Q21：`E_FMT_FORMATTER_FN` 里为什么不能写 `{c.r, c.g, c.b}`？**
宏是按逗号切参数的，花括号**不保护**逗号（只有圆括号保护）。把花括号列表换成表达式、
或者用一层圆括号包起来即可：

```cpp
// ✗ 宏会认为传了 4 个参数
E_FMT_FORMATTER_FN(Rgb, [](format_context& ctx, const format_specs&, const Rgb& c) {
    const uint8_t ch[3] = {c.r, c.g, c.b};
});
// ✓ 一个一个取，或用圆括号包住
E_FMT_FORMATTER_FN(Rgb, [](format_context& ctx, const format_specs&, const Rgb& c) {
    const uint8_t r = c.r, g = c.g, b = c.b;
});
```

**Q22：能打印 `enum class` 吗？**
用 `E_FMT_FORMATTER_ENUM(MyEnum, A, B, C);` 一行（见 5.4），输出 `A`/`B`/`C`，
没列出的取值打印底层整数。不加这个宏的话，`enum class` 会打印成 `obj@0x...`，
无作用域枚举会打印数字。

**Q23：`E_FMT_FORMATTER_AUTO` 报 `N names provided for structured binding … decomposes into M elements`？**
该类型里有 C 型数组成员，字段数被花括号省略（brace elision）算大了。两种改法：
`E_FMT_FORMATTER_AUTO_N(Type, M)`（M 用报错里给出的那个数字），或改用
`E_FMT_FORMATTER_FIELDS(Type, 字段…)`。

**Q25：`E_FMT_DERIVE` 报"解析不出这段声明里的字段/取值"？**
说明声明里有解析器认不出的形态。常见两种：
① 枚举取值是非字面量（`A = 1 << 3`、`B = A + 1`）→ 改用 `E_FMT_FIELDS(取值名, ...)`；
② 声明里出现了 `#if`/`#include`/宏调用，或字段数超过 `EFMT_DERIVE_MAX_FIELDS`（默认 16）→
改用类型内一行 `E_FMT_FIELDS(字段, ...)`。错误信息里会给出这两条出路。

**Q26：`E_FMT_DERIVE` 打出来没有类型名？**
默认关（`EFMT_DERIVE_SHOW_TYPE=0`），输出 `{ ax = 1.5 }`，比带类型名省 **1.35 KB Flash**
（Cortex-M4 实测）。想要 Rust 那种 `imu { ax = 1.5 }` 就设 `EFMT_DERIVE_SHOW_TYPE=1`。

**Q27：为什么位域也能打对？`std::tie` 不是有坑吗？**
有坑：GCC 下 `std::tie` 绑位域会拿到**未初始化的临时量**（实测打印 0）。EFmt 的推导路径
**不经过 `std::tie`**，而是把结构化绑定的变量直接按引用交给格式化器 —— 实测 `-O0/-O2` 都正确。

**Q28：成员类型没有格式化器时？**
编译报错并点名那个类型（Rust 里相当于"这个类型没实现 Debug"）：

```
error: static assertion failed: 成员类型没有格式化器：给它加 E_FMT_DERIVE(...)，
       或写一个 formatter<>（Rust 里相当于这个类型没实现 Debug）
```

给它加 `E_FMT_DERIVE`，或写 `E_FMT_FORMATTER_FN`；实在想退回旧的"打印地址"行为就设
`EFMT_DERIVE_STRICT=0`。

**Q29：老宏 `E_FMT_FORMATTER_FIELDS` 写错了作用域会怎样？**
**编译报错**（v1.7 起），错误信息直接告诉你怎么改：

```
error: static assertion failed: 这个类型有字段但找不到格式化器。若你用过
E_FMT_FORMATTER_FIELDS / E_FMT_FORMATTER_ENUM / E_FMT_FORMATTER_AUTO，
请把宏写在【类型所在的命名空间】里；推荐改用 E_FMT_DERIVE(...) 或类型内一行
E_FMT_FIELDS(字段, ...)。确实想打印地址就定义 EFMT_DERIVE_STRICT=0。
```

早期版本在同样的情况下会**静默**退化成 `obj@地址`（这次已修）。正确/错误写法对照：

```cpp
namespace app {
struct cfg { int retry; bool verbose; };
E_FMT_FORMATTER_FIELDS(cfg, retry, verbose);      // ✓ 宏在类型所在命名空间里
}  // namespace app

E_FMT_FORMATTER_FIELDS(app::cfg, retry, verbose); // ✗ 宏在全局 → 编译报错
```

想要"写在哪里都行"，用推荐写法：`E_FMT_DERIVE(struct cfg {...})` 或类型内一行
`E_FMT_FIELDS(retry, verbose)`（这两种不依赖 ADL，没有作用域要求）。

**Q24：自动派生的宏为什么不能写在别的命名空间里？**
库靠 ADL（实参相关查找）找 `efmt_derive_format`，而 ADL 只搜索"实参类型所属的命名空间"。
把宏写在类型所在的命名空间（类型在全局就写在全局）即可。

---

## 12. 从 printf / snprintf 迁移对照表

| 你现在写的 | 换成 |
|-----------|------|
| `printf("x=%d\n", x)` | `println_info("x={}", x)` |
| `printf("%08X", r)` | `println_info("{:08X}", r)` |
| `printf("%.2f", v)` | `println_info("{:.2f}", v)` |
| `printf("%-10s|%10s|\n", a, b)` | `println_info("{:<10}|{:>10}|", a, b)` |
| `snprintf(buf, n, "id=%u", id)` | `format_to(buf, n, "id={}", id)` |
| `if (snprintf(...) >= n) 截断了` | `if (format_to(...) >= n) 截断了`（同一语义） |
| `printf("%s", std::string(...).c_str())` | `println_info("{}", sv)`（直接传 `string_view`） |
| `printf("{}", ...)` 拼字符串 + `strcat` | `format_to(buf, n, "{}{}", a, b)` |
| `printf("%zu", size)` | `println_info("{}", size)`（类型自动） |
| 自定义类型 + `operator<<` + `ostringstream` | `E_FMT_FORMATTER_FN` / `E_FMT_FORMATTER_FIELDS` |

同样的迁移也适用于 `ELOG_*`：

```cpp
// 之前
printf("[%s:%d] temp=%.1f\n", __FILE__, __LINE__, t);
// 之后
ELOG_INFO("temp={:.1f}", t);      // 自动带上文件/行号/函数
```

---

## 13. 与 elog 一起用

`elog` 是同一仓库里的日志库，构建在 EFmt 之上（`elog/elog.hpp`），依赖 ETL
（`middleware/etl`），提供分级、多 sink、级别过滤。

### 13.1 `println_*` 还是 elog：怎么选

两者**共用同一套引擎**：格式化都走 `format_to`，颜色都取 `detail::styles::info()` 这类样式，
不是两套实现。差别在职责——`println_*` 是"无状态的一次性彩色打印"，elog 是"有状态的日志系统"：

| 能力 | efmt `println_info` 等 | elog `ELOG_INFO` |
|------|------------------------|------------------|
| 运行期关掉低级别（release 关 debug） | ✗ 没有，调了就打 | ✓ `set_level` + `level::off` |
| 输出带 `[级别] [文件:行 函数]` | ✗ 只上颜色 | ✓ 宏自动带源位置 |
| 多个命名 logger、各自独立 sink | ✗ 全局一条输出道 | ✓ 注册表 + 每 logger 独立 sink |
| trace / critical 级别 | ✗ 家族里没有 | ✓ 六档齐全 |
| 换行 | ✓ | ✓ |

怎么选：

* **调试初期 / 一次性打印**：`println_info`、`println_debug` 一行搞定，零概念；
* **要上线、要分级、要能关日志**：用 elog，`ELOG_*` 走默认 logger，`ELOG_LOGGER_*` 指定
  logger，还能 `set_level` 在运行期切换。

两者可以**输出到同一条通道**：`e_log::stdout_sink()` 内部用的就是 efmt 的全局输出处理器
（`get_output_handler()`），同一条串口上混用不打架。

### 13.2 从初始化到上线：完整用法

日志系统在**启动早期**初始化一次，之后业务代码只碰宏：

```cpp
#include <elog/elog.hpp>

// 业务代码只暴露这 4 个宏（再包一层，将来换库只改一处）
#define APP_LOG_DEBUG(...) ELOG_DEBUG(__VA_ARGS__)
#define APP_LOG_INFO(...)  ELOG_INFO(__VA_ARGS__)
#define APP_LOG_WARN(...)  ELOG_WARN(__VA_ARGS__)
#define APP_LOG_ERROR(...) ELOG_ERROR(__VA_ARGS__)

static bool uart_sink(const char* data, std::size_t size, void* /*user*/) {
    HAL_UART_Transmit(&huart1, (uint8_t*)data, (uint16_t)size, 100);
    return true;                 // false = 写失败（当前只有 multi_sink 会统计）
}

void logging_init() {
    // 第一个创建的 logger 自动成为默认 logger
    e_log::logger* app = e_log::create_logger(
        "app", e_log::make_sink(&uart_sink), e_log::level::debug);
    if (!app) {
        return;                  // 重名 / 注册表满 / 名字非法
    }

    // 双输出：串口 + efmt 全局输出处理器（前提：已 set_output_handler 接过 println_* 的通道）
    // 注意 multi_sink 必须活得比用它建的 logger 长，放静态区。
    static e_log::multi_sink dual;
    (void)dual.add_sink(e_log::make_sink(&uart_sink));
    (void)dual.add_sink(e_log::make_efmt_sink(e_fmt::get_output_handler()));
    e_log::logger* bus = e_log::create_logger("bus", dual.output_sink(), e_log::level::info);

    e_log::set_default_logger("app");   // 显式指定默认（可省略）
}

void app_loop() {
    APP_LOG_INFO("boot {} {}", "ok", 42);
    APP_LOG_WARN("battery low: {}%", 15);
    APP_LOG_ERROR("sensor {} failed", 3);
}
```

要点：

* **上线前关日志**：`app->set_level(e_log::level::warn)` 或 `level::off`。被过滤的级别在**格式化
  之前**短路，实测约 1 ns/行（见 13.6），发布固件可以留着日志代码不拆；
* **单行放不下**：`ELOG_MAX_RECORD_SIZE` 整行缓冲溢出时**整行丢弃**（不输出半行）；栈紧的 MCU
  调小它换栈（见 13.5）；
* **颜色**：嵌入式默认 `ELOG_ENABLE_COLOR=0`（跟随 `EFMT_ENABLE_ANSI_STYLES`），串口不会出现
  `ESC[34m`；
* **多 sink**：`multi_sink::add_sink` 返回 `bool`，失败说明 sink 无效或槽位满（最多 4 个）。

### 13.3 最小可运行例子

```cpp
#include <elog/elog.hpp>

static bool uart_sink(const char* data, std::size_t size, void* /*user*/) {
    HAL_UART_Transmit(&huart1, (uint8_t*)data, (uint16_t)size, 100);
    return true;                     // 返回 false 表示写失败
}

int main() {
    e_log::logger* logger = e_log::create_logger("app", e_log::make_sink(&uart_sink), e_log::level::info);
    if (!logger) {
        return -1;                    // 重名 / 注册表满 / 名字非法
    }
    logger->info("boot {} {}", "ok", 42);
    ELOG_WARN("battery low: {}%", 15);          // 用默认 logger + 自动文件/行号
    ELOG_LOGGER_ERROR(*logger, "sensor {}", 3); // 指定 logger
    return 0;
}
```

输出形如：

```
[info] [main.cpp:12 main] boot ok 42
```

### 13.4 关键接口

| 接口 | 说明 |
|------|------|
| `e_log::create_logger(name, sink, level)` | 建日志器，返回 `logger*`，失败为 `nullptr`；名字 ≤ 31 字符、最多 8 个、不可重名 |
| `e_log::get(name)` / `default_logger()` | 查找 / 取默认；找不到或未设为 `nullptr` |
| `logger::set_level(level)` / `should_log(level)` | 运行期过滤（trace→critical→off） |
| `logger::info(...)` / `warn` / `error` … | 直接打；放不下的整行静默丢弃 |
| `e_log::set_default_logger(...)` | 设默认 logger，失败返回 `false` |
| `e_log::multi_sink` | 一个 logger 挂多个 sink（最多 4 个），`add_sink(...)` 失败返回 `false` |
| `ELOG_INFO(...)` 等宏 | 自动带 `__FILE__/__LINE__/__func__` |

### 13.5 elog 的裁剪宏

| 宏 | 默认 | 说明 |
|----|------|------|
| `ELOG_MAX_LOGGERS` | 8 | 日志器槽位（每个约 64 B 静态 RAM） |
| `ELOG_MAX_RECORD_SIZE` | 384 | 单行日志栈缓冲（前缀 + 消息）；放不下的一行整体丢弃 |
| `ELOG_ENABLE_COLOR` | `EFMT_ENABLE_ANSI_STYLES` | 是否给日志上色（嵌入式默认关） |

```cpp
// 内存紧的 MCU：4 个日志器、单行 128 字节
-DELOG_MAX_LOGGERS=4 -DELOG_MAX_RECORD_SIZE=128
```

### 13.6 elog 性能实测（GCC x64 `-O2`，可复现）

`tests/run_check.ps1 -Bench` 现在含 elog 基准（`tests/elog_bench.cpp`），同一份源码编
libc 浮点 / 自带浮点两轮，当前实测：

| 用例 | libc 浮点 | 自带浮点 |
|------|----------|---------|
| 一行 2 参数（`info("boot {} {}", "ok", 42)`，含前缀 + 换行） | 112.7 ns | **113.3 ns** |
| 一行含 `{:.1f}`（3 参数） | 430.6 ns | **171.7 ns** |
| 级别被过滤（`level::error` 下打 `debug`） | 1.0 ns | **0.6 ns** |
| 对照：裸 `format_to` 同一条消息 | **35.8 ns** | 40.0 ns |

要点：

* **一行完整日志 ≈ 113 ns**（x64 `-O2`）：前缀是固定字面量，交给 `E_FMT_STR` 编译期预解析
  （不再逐行扫描/解析格式规范），消息是用户运行时串照常运行期路径——两段顺序写进同一块
  记录缓冲，无二次复制；
* **浮点用自带引擎快约 2.5 倍**（172 vs 431 ns）——嵌入式默认自带引擎的原因之一，另一个是
  不拉进 newlib 的 `malloc/free`（见 9.1）；
* **被过滤的日志几乎零成本**（0.6~1.0 ns）：`should_log` 在格式化之前短路——发布版调高 logger
  级别后，日志代码可以原样留着；
* efmt 本体核心格式化同样快（`format("x={} y={} z={}")` 约 100 ns，见 8.4 同一轮实测）。

---

### 13.7 v1.4 对 elog 的优化（为什么它变快了）

旧实现每写一行日志要做两遍完整格式化：先把消息格式化到 `payload[257]`，再把
`"[{}] [{}:{} {}] {}"` 连 payload 一起格式化进 `record[385]`（payload 被当普通字符串又抄一遍）。
现在前缀和消息**顺序写入同一块 record 缓冲**，一遍就完：

* 少一块 257 B 栈缓冲、少一次完整重排；
* 嵌入式下日志帧从 960 B 降到 896 B（`-fstack-usage` 实测）；
* 一行 2 参数日志 197 → 152 ns（x64 `-O2` 实测）；
* 参数改为按 `const&` 转发，不再复制实参（传 `std::string` / 自定义类型时省一次拷贝）。

### 13.8 ETL 类型支持（elog 自带，无需配置）

elog 依赖 ETL（`middleware/etl`），也顺带把 ETL 常用类型接进了格式化体系。
**只要包含 `<elog/elog.hpp>` 就能直接用，不需要任何宏**：

```cpp
#include <elog/elog.hpp>

etl::string<32> name = "imu";
etl::vector<int, 8> raw{1, 2, 3};          // ETL 静态容器：无堆
ELOG_INFO("data: str={} vec={}", name, raw);
// [info] [main.cpp:23 main] data: str=imu vec=[1, 2, 3]
```

| ETL 类型 | 输出 | 说明 |
|---|---|---|
| `etl::string<N>` / `etl::istring` / `etl::string_view` | 文本 | 宽度/精度/对齐等格式规范照常生效 |
| `etl::vector` / `etl::list` / `etl::deque` / `etl::set` / `etl::multiset` / `etl::forward_list` / `etl::array` / `etl::span` / `etl::circular_buffer` | `[a, b, c]` | 通用迭代器通道自动覆盖 |
| `etl::map` / `etl::multimap` / `etl::flat_map` / `etl::unordered_map` 等关联容器 | `{k: v, ...}` | 自动识别 |
| `etl::optional<T>` | 有值打值，空打 `nullopt` | |
| `etl::pair<A,B>` | `(a: b)` | 与 `std::pair` 风格一致 |
| `etl::variant<Ts...>` | 当前活跃值 | C++17（`etl::visit`） |

要点：

* **容器默认打开**：elog 在包含 efmt 前把 `EFMT_ENABLE_CONTAINER_FORMAT` 默认置 1
  （Cortex-M4 `-Os` 实测：不用容器零 Flash 开销，用容器 +260 B 左右），所以 MCU 上打
  `etl::vector` / `etl::map` 开箱即用；仍可用 `-DEFMT_ENABLE_CONTAINER_FORMAT=0` 关掉。
* **`etl::queue` / `etl::stack` / `etl::priority_queue` / `etl::bitset`** 是容器
  **适配器/位集**，没有迭代器接口——与 `std::queue` 等一样不支持（打印退化为 `obj@0x...`）。
* 实现是 elog 层对 `e_fmt::formatter` 的偏特化，**不修改 efmt 核心**；efmt 独立使用
  （不包含 elog）时 ETL 类型不在支持列表里。

---

## 14. API 速查

### 14.1 格式化

```cpp
// 缓冲区（snprintf 语义：返回所需长度，>= size 表示被截断）
template <typename... Args>
size_t format_to(char* buffer, size_t size, std::string_view fmt, Args&&... args);

// std::string（需 EFMT_ENABLE_DYNAMIC_STRING）
template <typename... Args> std::string format(std::string_view fmt, Args&&... args);
template <typename... Args> void format_to(std::string& out, std::string_view fmt, Args&&... args);

// 只要长度（精确，可直接用于分配）
template <typename... Args> size_t formatted_size(std::string_view fmt, Args&&... args);
```

带 `E_FMT_STR` / `E_FMT_DECLARE_STR` 的版本是同样的签名，只多一次编译期参数个数校验。

### 14.2 打印

```cpp
void print_error(std::string_view fmt, Args&&...);      void println_error(...);
void print_warning(...);                                void println_warning(...);
void print_info(...);                                   void println_info(...);
void print_success(...);                                void println_success(...);
void print_debug(...);                                  void println_debug(...);

void print_styled(const text_style&, std::string_view fmt, Args&&...);
void println_styled(const text_style&, std::string_view fmt, Args&&...);
```

### 14.3 输出管理（`format_output.hpp`）

```cpp
using output_fn = void(*)(const char* data, size_t size);
void       set_output_handler(output_fn);
output_fn  get_output_handler();
void       reset_output_handler();

void       set_buffer_output(char* buf, size_t size);
size_t     get_buffer_output_pos();
void       reset_buffer_output_pos();
void       buffer_output_handler(const char* data, size_t size);   // 手动当 sink 用
void       null_output_handler(const char* data, size_t size);     // 丢弃
void       stdout_output_handler(const char* data, size_t size);   // 需 EFMT_ENABLE_STDIO
void       stderr_output_handler(const char* data, size_t size);
void       file_output_handler(FILE*, const char* data, size_t size);

namespace detail { class output_handler_scope { ... }; }  // RAII 临时改道
```

### 14.4 自定义类型与样式

```cpp
// 老写法 E_FMT_FORMATTER_1/2/3 已删除（抄"类型+成员名+显示名"三遍）；要"只列字段名"用下面的 FORMATTER_FIELDS
E_FMT_FORMATTER_FN(Type, lambda)                 // 完全自定义输出（最灵活）

E_FMT_DERIVE(struct imu { float ax, ay, az; });   // 声明即推导（推荐）
E_FMT_DERIVE(enum class state { idle, busy = 5, fault });

E_FMT_FIELDS(m1, m2, ...)                   // 类型内一行：只列名字（#if/模板/超上限时用）
E_FMT_FORMATTER_FIELDS(Type, m1, m2, ...)   // 只列字段名（≤12），名字自动转字符串
E_FMT_FORMATTER_AUTO(Type)                  // 简单聚合体：字段全自动
E_FMT_FORMATTER_AUTO_N(Type, count)         // 有 C 型数组成员时显式给字段个数
E_FMT_FORMATTER_ENUM(Type, V1, V2, ...)     // 枚举取值 → 名字（≤12）

E_FMT_STR("literal")            E_FMT_DECLARE_STR(name, "literal")

with_color(color)   with_colors(fg, bg)   with_style(style)   style_a | style_b
detail::styles::error() / warning() / info() / success() / debug() / muted() / highlight()
**输出样式：{} 单行 / {:#} 多行缩进**（`E_FMT_DERIVE`、`E_FMT_FIELDS`、`E_FMT_FORMATTER_FIELDS` 都支持，读取格式规格里现成的 `#` = 替代形式位）：

    E_FMT_DERIVE(struct person { int age; float weight; std::string name; });
    printf("%s\n", format("{}", person{18, 1.0f, "xiaoming"}).c_str());
    // { age = 18, weight = 1, name = xiaoming }

    printf("%s\n", format("{:#}", person{18, 1.0f, "xiaoming"}).c_str());
    // {
    //   age = 18,
    //   weight = 1,
    //   name = xiaoming
    // }

- 嵌套成员固定单行，多行只作用于顶层（缩进不乱）。
- 老宏 `E_FMT_FORMATTER_FN` 仍是单行（保持历史输出）。
- 开关：`EFMT_DERIVE_STYLE_MULTILINE`（默认 1；固件想省那 ~15 B 字符串就 -D=0，`{:#}` 自动退化为单行）。

```

---

## 15. 验证与自测

### 15.1 一键跑全部检查

```powershell
.\tests\run_check.ps1                    # 宿主 C++17/C++20 + 嵌入式 + 最小裁剪 + 反例 + elog
.\tests\run_check.ps1 -Bench             # 额外跑微基准（libc 浮点 / 自带浮点各一轮）
.\tests\run_check.ps1 -Size              # 额外交叉编译量 Flash/RAM（需要 arm-none-eabi-g++ 等）
.\tests\run_check.ps1 -Qemu              # 额外在 QEMU（mps2-an386，Cortex-M4）里真实运行嵌入式行为检查
.\tests\run_check.ps1 -QemuBench         # 额外输出嵌入式侧相对周期（-icount + SysTick）
.\tests\run_check.ps1 -EtlInclude D:\etl\include   # 重新指向 ETL 头文件
```

**`-Qemu`**：把嵌入式配置的行为检查交叉编译成可启动的 `.elf`（self-contained，无 newlib），
在 QEMU 的 Cortex-M4（`mps2-an386`）上真实执行：20 项断言（整数/uint64/十六进制/浮点自带引擎/
截断/`E_FMT_STR`/8 参数/derive/输出回调），经 UART 输出，`ALL PASS (20)` 为绿。
需要 `qemu-system-arm` 与 `arm-none-eabi-g++`（缺任一即跳过提示）。
链接注意（踩过的坑）：必须链接 **thumb/v7e-m 的 libgcc**——脚本里用
`arm-none-eabi-g++ -print-libgcc-file-name` 定位；若用 `-nostdlib` 手写 `-lgcc`，
GCC 驱动丢失 multilib 的 `-L`，会链到 A32（ARM）libgcc，Thumb 代码调其 64 位除法
会指令流错乱（实测 `42` 被格式化成了 `80` 并最终 HardFault-Lockup）。

当前基线（全绿）：宿主 **149 项 × 2 标准**、浮点对拍 **24.6 万次比对 0 失败**、
嵌入式配置 / 最小裁剪配置 / 五个编译期反例 / elog 集成（三种配置）/ 无流无 ANSI 配置 /
QEMU（Cortex-M4）20 项真 ARM 断言。

### 15.2 浮点对拍自己跑一遍（可选，想加大样本时）

```powershell
g++ -std=c++17 -O2 -Itests/include -DEFMT_USE_LIBC_PRINTF=0 tests/efmt_float_check.cpp -o floatchk.exe
.\floatchk.exe                                   # 24.6 万次比对
g++ -std=c++17 -O2 -Itests/include -DEFMT_USE_LIBC_PRINTF=0 -DEFMT_FLOAT_CHECK_ITERS=4000000 \
    tests/efmt_float_check.cpp -o floatbig.exe
.\floatbig.exe                                   # 800 万次比对（约 40 秒）
```

### 15.3 加自己的检查

检查代码是**零框架**的：把 `tests/efmt_check.cpp` 里的 `CHECK` / `CHECK_EQ` 宏复制到你的
文件，然后在 `run_check.ps1` 里加一段 `Invoke-EfmtBuild` + `Invoke-EfmtRun` 即可。

### 15.4 上板自检清单

- [ ] `set_output_handler` 在最开始调用，串口能收到 `println_info("boot")`
- [ ] 确认没有 `ESC[` 开头的乱码（说明 ANSI 已关闭）
- [ ] 打印一次浮点（`{:.2f}`）确认有值，不是空白
- [ ] 日志最长的一行的字节数 < `EFMT_PRINT_BUFFER_SIZE`（默认 256）
- [ ] `-fstack-usage` 或加大任务栈后，压力场景跑 24 小时无 HardFault

---

## 附录 A：格式规范完整语法

```
替换字段 := "{" [ 索引 ] [ ":" 格式规范 ] "}"
格式规范 := [ [ 填充字符 ] 对齐 ] [ 符号 ] [ "#" ] [ "0" ] [ 宽度 ] [ "." 精度 ] [ 类型 ]

填充字符 := 任意字符（当它后面紧跟对齐符号时才算填充）
对齐     := "<" 左 | ">" 右 | "^" 居中
符号     := "+" 总带符号 | "-" 仅负数（默认） | " " 正数留空格
"#"      := 替代形式：整数加 0x/0X/0b/0 前缀；浮点保留小数点与尾零
"0"      := 补零（无显式对齐时生效），补在符号之后
宽度     := 十进制数（超长数字串按饱和处理，不会溢出）
精度     := 十进制数；整数=最少位数，字符串=最大长度，浮点=小数位/有效位数
类型     := "d" "o" "x" "X" "b" "B"    整数进制
            "f" "F" "e" "E" "g" "G"    浮点
            "c"                        按字符（整数）
            "s"                        字符串 / bool 的 true|false
            "p"                        指针（当前与默认一致）
```

解析规则要点：

* 规范是**一次线性扫描**，不认识的字符会被忽略 → 写法有误时退化成默认格式，而不是整条失败；
* 没有显式对齐时，**数字右对齐、字符串左对齐**；
* `'0'` 与显式对齐同时出现时，显式对齐优先（与 printf 一致）；
* 宽度/精度没有上限（不会因为位数太长而溢出或写坏内存）。

## 附录 B：宏速查与代价

| 宏 | 默认（宿主 / 嵌入式） | 影响 | 典型用法 |
|----|---------------------|------|---------|
| `EFMT_ENABLE_HOSTED` | 自动 | 总开关 | `-DEFMT_ENABLE_HOSTED=0` 强制嵌入式 |
| `EFMT_DERIVE_SHOW_TYPE` | 0 | 推导输出是否带类型名 | 开它 +1.35 KB Flash（Cortex-M4） |
| `EFMT_DERIVE_STYLE_MULTILINE` | 1 | `{:#}` 多行缩进是否编进去 | 关它省 ~15 B 字符串，`{:#}` 退化为单行 |
| `EFMT_DERIVE_MAX_FIELDS` | 16 | 单类型字段/取值上限 | 更大结构体时调大 |
| `EFMT_DERIVE_MAX_ARRAY_ITEMS` | 8 | 数组成员最多打几个 | 缓冲区想全打就调大 |
| `EFMT_DERIVE_STRICT` | 1 | 成员缺格式化器即编译错误 | 设 0 退回 `obj@地址` |
| `EFMT_MAX_FORMAT_ARGS` | 16 / 8 | 每次调用栈 = 24 B × N | `=4` 省 96 B 栈 |
| `EFMT_PRINT_BUFFER_SIZE` | 256 / 256 | `print/println` 单行上限 | `=128` 省 128 B 栈 |
| `EFMT_STRING_BUFFER_SIZE` | 256 | `format()` 是否需要二次分配 | 宿主调优 |
| `EFMT_ENABLE_FLOAT` | 1 / 1 | 关掉省 ~3.4 KB，浮点参数编译报错 | 不打印浮点就关 |
| `EFMT_USE_LIBC_PRINTF` | 1 / 0 | 浮点实现二选一 | 嵌入式保持 0 |
| `EFMT_FLOAT_BIGNUM_LIMBS` | 48 / 48 | 浮点精度上限与栈 | 精度需求高时调 80 |
| `EFMT_FLOAT_DIGIT_GROUPS` | 48 / 48 | 浮点输出位数上限与栈 | 极端精度时调 120 |
| `EFMT_ENABLE_CONTAINER_FORMAT` | `HOSTED` / 0 | 容器/tuple 格式化 | efmt 独立用需就开；**elog 默认 1**（MCU 也可打 ETL 容器） |
| `EFMT_ENABLE_DYNAMIC_STRING` | `HOSTED` / 0 | `std::string` 重载 | ESP-IDF 上可开 |
| `EFMT_ENABLE_STREAM_API` | `HOSTED` / 0 | `ostream` 接口 | 桌面调试 |
| `EFMT_ENABLE_STREAM_FALLBACK` | `STREAM_API && HOSTED` / 0 | `operator<<` 兜底 | 嵌入式别开 |
| `EFMT_ENABLE_ANSI_STYLES` | `HOSTED` / 0 | ANSI 颜色字节 | 嵌入式保持 0 |
| `EFMT_ENABLE_STDIO` | `HOSTED` / 0 | 默认输出到 stdout | 嵌入式保持 0 |
| `ELOG_MAX_LOGGERS` | 8 | 静态 RAM ≈ 64 B × N | 内存紧时 `=4` |
| `ELOG_MAX_RECORD_SIZE` | 384 | 单行日志栈缓冲 | `=128` 省 256 B 栈 |
| `ELOG_ENABLE_COLOR` | 跟随 ANSI | 日志着色 | 嵌入式保持 0 |

## 附录 C：版本历史

- **v1.0** 初版：基本格式化、格式规范、ANSI 颜色、容器、自定义类型
- **v1.1** 嵌入式增强：无异常、自定义输出处理器、UART/RTT/SD 支持、截断检测
- **v1.2** 解析性能与接口一致性：单遍扫描执行、长度精确、规范解析重写、`E_FMT_STR` 编译期校验
- **v1.3** 去掉错误码：`format_to` 改为 snprintf 语义、只依赖 C++17 标准库
- **v1.4** 嵌入式专项优化
  - **自带浮点引擎**：不依赖 libc 的 `printf`、不用堆，newlib-nano 下也能正确打印浮点；
    与 printf 逐位一致（24.6 万次随机对拍验证），比 libc 快 40%+；Cortex-M4 上一处
    `{:.2f}` 从 7133 B 降到 6552 B，并去掉 `_malloc_r/_free_r/_sbrk` 依赖
  - **修掉 ANSI 开关失效**：`EFMT_ENABLE_ANSI_STYLES=0` 或嵌入式配置下，`println_*`
    不再往串口写 `ESC[34m`（此前只有 elog 遵守该宏）
  - **可裁剪化**：`EFMT_MAX_FORMAT_ARGS`（嵌入式默认 8）、`EFMT_ENABLE_FLOAT`、
    `EFMT_ENABLE_CONTAINER_FORMAT`、`EFMT_FLOAT_BIGNUM_LIMBS/DIGIT_GROUPS`；
    平台判定改用 `__STDC_HOSTED__` + 裸机特征，32 位 ARM Linux 不再被误判为嵌入式
  - **性能**：填充改 `memset`、浮点格式规范解析路径去掉 `snprintf`；
    典型用例 `format("{:.2f}|{:<12}|{:#06x}")` 324 → 184 ns
  - **elog 单遍直写**：日志不再"先格式化 payload 再整体重排"，省一块 257 B 栈、
    一行 2 参数日志 197 → 152 ns，栈帧 960 → 896 B；参数按 `const&` 转发
  - **文档**：本手册（新手向）+ `efmt/core/README.md` 精简为索引
  - **测试**：新增浮点差分对拍（`tests/efmt_float_check.cpp`）、最小裁剪配置
    （`tests/efmt_tiny_build.cpp`）、浮点关闭反例；`run_check.ps1` 增加 `-Size`
    交叉编译体积报告

---

- **v1.8** elog 支持 ETL 类型（本版）
  - elog 层新增 ETL 类型格式化：`etl::string<N>` / `etl::istring` / `etl::string_view`
    → 文本（格式规范全支持）、`etl::optional` → 值或 `nullopt`、`etl::pair` → `(a: b)`、
    `etl::variant` → 当前活跃值（C++17）；ETL 容器（vector/map/list/deque/set/span/array/
    circular_buffer/...）走通用迭代器通道自动覆盖，无需特化
  - elog 默认打开容器格式化（`EFMT_ENABLE_CONTAINER_FORMAT=1`，可 `-D...=0` 关）：
    Cortex-M4 `-Os` 实测不用容器零开销、用 `etl::vector<int,8>` +260 B
  - 修掉便捷 API 按值传参：`logger->trace/debug/info/warn/error/critical` 与同名自由函数
    统一改 `const Args&...` 转发（此前 `etl::istring` 等不可拷贝类型会编译失败，字符串参数被白拷贝）
  - 修正容器关闭后的行为口径：非聚合容器打印 `obj@0x...`（不是编译报错）；聚合数组
    （`std::array` / `etl::array`）在 `EFMT_DERIVE_STRICT=1` 下仍编译报错
  - 测试：`tests/elog_integration.cpp` 增加 ETL 断言，宿主 + 嵌入式（`EFMT_ENABLE_HOSTED=0`）
    两配置跑；`run_check.ps1` 新增嵌入式 elog pass

- **v1.7** 修掉"宏写错作用域静默失效"
  - 老宏（`E_FMT_FORMATTER_FIELDS` / `_ENUM` / `_AUTO` / `_AUTO_N`）写在类型命名空间
    之外时，v1.5 会**静默**退化成 `obj@地址`
  - 现在由库侧在编译期拦住：有字段却找不到格式化器的聚合体 → `static_assert` 并给出
    两条出路（挪到类型所在命名空间 / 改用 `E_FMT_DERIVE(...)` 或 `E_FMT_FIELDS(...)`）；
    想保留旧的地址输出就定义 `EFMT_DERIVE_STRICT=0`
  - 新增反例测试 `tests/efmt_compile_fail_derive_scope.cpp`
- **v1.6** `E_FMT_DERIVE`：声明即推导（真正的 Rust `#[derive(Debug)]` 体验）
  - **一个宏管结构体和枚举**，字段名/取值名一个字都不用写：
    `E_FMT_DERIVE(struct imu { float ax, ay, az; });` → `{ ax = 1.5, ay = 2.5, az = 3.5 }`、
    `E_FMT_DERIVE(enum class state { idle, busy = 5 });` → `busy`
  - 机制全部纯 C++17：`extern` + `decltype` 抓类型、`#__VA_ARGS__` 编译期解析声明文本、
    同作用域 ADL 自由函数挂名字、结构化绑定取值 —— **无脚本、无第三方库、无编译器扩展**
    （宿主 GCC 15.1 / arm-none-eabi 10.3 / xtensa 三条工具链实测通过）
  - 取值路径**不经过 `std::tie`**：GCC 下 tie 绑位域会拿到未初始化临时量（实测打印 0），
    改为直接传引用后位域正确且无拷贝；这也把推导开销从 +1360 B 压到 **+144 B**（Cortex-M4 -Os，
    对比老的结构化宏；嵌套两类型 +352 B）
  - 解析出的字段数 == 结构化绑定数量：对不上、成员缺格式化器、枚举非字面量初始值
    → 一律**编译报错**并给出可操作的出路，绝不静默输出错名字
  - 开关：`EFMT_DERIVE_SHOW_TYPE`（默认 0，省 Flash）、`EFMT_DERIVE_MAX_FIELDS`（16）、
    `EFMT_DERIVE_MAX_ARRAY_ITEMS`（8）、`EFMT_DERIVE_STRICT`（1）
  - 兜底：**类型内一行** `E_FMT_FIELDS(retry, verbose)`（不需要 ADL/特化，因此命名空间、
    限定名、遮蔽问题一概不存在），支持 **`#if` 平台分支字段** 与 **模板结构体**，
    可与 `E_FMT_DERIVE` 互相嵌套
  - 测试：`tests/efmt_derive_auto_check.cpp`（27 项 × 宿主/嵌入式）、
    `tests/efmt_compile_fail_derive.cpp`（成员缺格式化器必须编译失败）
- **v1.5** 自定义类型自动派生（对标 Rust 的 `#[derive(Debug)]`）
  - 新增 `format_derive.hpp`：`E_FMT_FORMATTER_FIELDS`（只列字段名，类型与显示名自动）、
    `E_FMT_FORMATTER_AUTO` / `_AUTO_N`（纯聚合体零声明，输出 `Type(a, b, c)`）、
    `E_FMT_FORMATTER_ENUM`（枚举取值转名字，未列出时打印底层整数）
  - 宏改为展开成自由函数（ADL 定制点）：不再进 `e_fmt::detail` 命名空间，
    用户类型名不会被库内部同名符号（`detail::color`/`detail::style` 等）静默遮蔽
  - 枚举现在能正确走到格式化器：此前无作用域枚举会被 `ostream` 的 `operator<<(int)`
    抢走（隐式转 int），`E_FMT_FORMATTER_ENUM`/自定义 `formatter<Enum>` 都轮不到
  - `E_FMT_FORMATTER_AUTO` 用不上时给的是可操作的编译错误（提示改用 `_FIELDS`/`_AUTO_N`）
  - 测试：`tests/efmt_derive_check.cpp`（宿主 + 嵌入式两种配置）、
    `tests/efmt_compile_fail_auto.cpp`（非聚合体必须编译失败）

## 许可证

请参阅项目根目录的 LICENSE 文件（若仓库中还没有，添加时请与本库使用的第三方代码保持一致）。

## 反馈

发现文档与实现不一致，先跑一遍 `tests/run_check.ps1` —— 它是本手册所有行为断言的来源，
也是最快的复现路径。
