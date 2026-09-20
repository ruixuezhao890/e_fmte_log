# EFmt - 嵌入式 C++ 格式化库使用手册

> 一个轻量级、零依赖的 C++17 格式化库，专为嵌入式系统设计

---

## 目录

1. [简介](#简介)
2. [快速开始](#快速开始)
3. [基本用法](#基本用法)
4. [格式规范语法](#格式规范语法)
5. [类型支持](#类型支持)
6. [自定义类型格式化](#自定义类型格式化)
7. [失败与边界](#失败与边界)
8. [自定义输出（嵌入式友好）](#自定义输出嵌入式友好)
9. [颜色与样式输出](#颜色与样式输出)
10. [容器格式化](#容器格式化)
11. [编译期验证](#编译期验证)
12. [API 参考](#api-参考)
13. [最佳实践](#最佳实践)
14. [注意事项](#注意事项)
15. [完整示例](#完整示例)

---

## 简介

**EFmt** (Embedded Format) 是一个类似 Python f-string 和 C++20 `std::format` 的类型安全字符串格式化库，专为资源受限的嵌入式环境设计。

### 主要特性

- **零外部依赖** - 仅依赖标准库和 ETL
- **类型安全** - 编译期类型检查，避免运行时错误
- **无异常、无错误码** - 格式化不会失败，缓冲区不够用返回值判断（snprintf 语义）
- **零外部依赖** - 只用 C++17 标准库，不依赖 ETL/eresult
- **自定义输出** - 支持 UART、RTT、SD 卡等任意输出方式
- **可变参数模板** - 支持任意数量参数
- **丰富的格式化选项** - 对齐、填充、精度、进制转换等
- **ANSI 颜色支持** - 终端彩色输出（可禁用）
- **容器格式化** - 自动格式化 STL 容器
- **自定义类型支持** - 简单扩展机制
- **C++17 兼容** - 无需 C++20
- **单遍解析执行** - 没有中间组件表，格式串多长、字段多少都不额外占内存
- **长度精确** - `formatted_size()` 与 `format()` 不做估算，长参数不会被截断
- **编译期校验参数个数** - `E_FMT_STR("x={}")` 写错实参个数直接编译失败

---

## 快速开始

### 包含头文件

```cpp
#include <middleware/efmt/core/format.hpp>
using namespace e_fmt;
```

### 第一个示例

```cpp
// 基本用法
std::string result = format("Hello, {}!", "World");
// 结果: "Hello, World!"

// 多个参数
std::string coords = format("Position: x={}, y={}", 10, 20);
// 结果: "Position: x=10, y=20"

// 带格式说明符
std::string pi = format("PI = {:.2f}", 3.14159);
// 结果: "PI = 3.14"
```

### 嵌入式环境快速配置

```cpp
#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

// 定义你的 UART 输出函数
void uart_send(const char* data, size_t size) {
    // 发送到 UART
    UART_Transmit((uint8_t*)data, size);
}

int main() {
    // 设置输出到 UART
    e_fmt::set_output_handler(uart_send);

    // 现在所有打印都会输出到 UART
    e_fmt::println_info("System started!");
    e_fmt::println_error("Error code: {}", 0x1234);

    return 0;
}
```

---

## 基本用法

### 1. format() - 返回格式化字符串

```cpp
// 格式化到 std::string
std::string s1 = format("The answer is {}", 42);
std::string s2 = format("Name: {}, Age: {}", "Alice", 25);
std::string s3 = format("PI: {:.2f}", 3.14159);  // "PI: 3.14"
```

### 2. format_to() - 格式化到缓冲区

```cpp
// 格式化到字符数组
char buffer[128];
size_t written = format_to(buffer, sizeof(buffer), "Value: {}", 123);

// 格式化到 std::string
std::string out;
format_to(out, "Count: {}", 42);
```

### 3. formatted_size() - 计算所需大小

```cpp
// 计算格式化结果的大小（不含空终止符）
size_t size = formatted_size("Hello {}", "World");
// 返回: 11 ("Hello World" 的长度)
```

---

## 格式规范语法

格式规范由替换字段 `{}` 内的可选内容组成：

```
格式: {[索引][:格式规范]}
```

### 基本格式规范

```
格式规范: [[填充]对齐][符号][#][0][宽度][.精度][类型]
```

### 1. 对齐与填充

```cpp
// 左对齐 (默认右对齐)
format("|{:<10}|", "text");   // "|text      |"

// 右对齐
format("|{:>10}|", "text");   // "|      text|"

// 居中对齐
format("|{:^10}|", "text");   // "|   text   |"

// 自定义填充字符
format("|{:*>10}|", "text");   // "|******text|"
format("|{:0<10}|", "text");   // "|text000000|"
format("|{:#^10}|", "text");   // "|###text###|"
```

### 2. 符号模式

```cpp
// 总是显示符号
format("{:+}", 42);    // "+42"
format("{:+}", -42);   // "-42"

// 负数显示符号，正数显示空格
format("{: }", 42);    // " 42"
format("{: }", -42);   // "-42"

// 仅负数显示符号（默认）
format("{}", 42);      // "42"
format("{}", -42);     // "-42"
```

### 3. 宽度与精度

```cpp
// 宽度
format("{:10}", 42);      // "        42"
format("{:10}", "test");  // "test      "

// 精度
format("{:.3}", 3.14159);      // "3.14"
format("{:.5}", "hello");      // "hello"
format("{:.2}", 3.14159);      // "3.14"

// 宽度 + 精度
format("{:10.2f}", 3.14159);   // "      3.14"
```

### 4. 数值类型

#### 整数类型

```cpp
// 十进制 (默认)
format("{}", 255);           // "255"
format("{:d}", 255);         // "255"

// 十六进制
format("{:x}", 255);         // "ff"
format("{:X}", 255);         // "FF"
format("{:#x}", 255);        // "0xff"
format("{:#X}", 255);        // "0XFF"

// 八进制
format("{:o}", 255);         // "377"
format("{:#o}", 255);        // "0377"

// 二进制
format("{:b}", 255);         // "11111111"
format("{:B}", 255);         // "11111111"
format("{:#b}", 255);        // "0b11111111"
```

#### 浮点类型

```cpp
double pi = 3.14159265359;

// 固定小数点
format("{:f}", pi);          // "3.141593"
format("{:.2f}", pi);        // "3.14"
format("{:F}", pi);          // "3.141593"

// 科学计数法
format("{:e}", pi);          // "3.141593e+00"
format("{:E}", pi);          // "3.141593E+00"

// 通用格式 (自动选择)
format("{:g}", pi);          // "3.14159"
format("{:G}", pi);          // "3.14159"
```

### 5. 字符与布尔值

```cpp
// 字符格式化
format("{}", 'A');           // "A"
format("{:c}", 65);          // "A"

// 布尔值
format("{}", true);          // "1"
format("{:d}", true);        // "1"
format("{:s}", true);        // "true"
format("{:s}", false);       // "false"
```

---

## 类型支持

### 内置类型

| 类型 | 支持情况 | 示例 |
|------|----------|------|
| `int`, `long`, `long long` | ✓ | `format("{}", 42)` |
| `unsigned` 变体 | ✓ | `format("{}", 42u)` |
| `float`, `double` | ✓ | `format("{}", 3.14)` |
| `bool` | ✓ | `format("{}", true)` |
| `char` | ✓ | `format("{}", 'A')` |
| `const char*` | ✓ | `format("{}", "text")` |
| `std::string` | ✓ | `format("{}", str)` |
| `std::string_view` | ✓ | `format("{}", sv)` |
| 指针类型 | ✓ | `format("{}", ptr)` |

### 指针格式化

```cpp
int x = 42;
format("{}", &x);       // "0x7ffc1234"
format("{}", nullptr);  // "(nil)"
```

---

## 自定义类型格式化

EFmt 提供多种方式为自定义类型添加格式化支持：

### 方式 1: 使用宏（最简单）

```cpp
// 定义自定义类型
struct Point {
    int x, y;
};

// 使用宏定义格式化（1个成员）
E_FMT_FORMATTER_1(Point, int, x, "x");
// 输出: {x=10}

// 使用宏定义格式化（2个成员）
E_FMT_FORMATTER_2(Point, int, x, "x", int, y, "y");
// 输出: {x=10, y=20}

// 使用宏定义格式化（3个成员）
struct Person {
    std::string name;
    int age;
    double height;
};

E_FMT_FORMATTER_3(Person,
    std::string, name, "name",
    int, age, "age",
    double, height, "height");
// 输出: {name="Alice", age=25, height=1.75}
```

### 方式 2: 使用 Lambda 函数

```cpp
struct Color {
    uint8_t r, g, b;
};

E_FMT_FORMATTER_FN(Color, [](format_context& ctx, const format_specs& specs, const Color& c) {
    char buffer[16];
    int len = snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", c.r, c.g, c.b);
    ctx.write_chars(buffer, len);
});

// 使用
Color red{255, 0, 0};
format("Color: {}", red);  // "Color: #ff0000"
```

### 方式 3: 特化 formatter 模板

```cpp
namespace e_fmt::detail {

template <>
struct formatter<MyType> {
    static void format(format_context& ctx, const format_specs& specs, const MyType& value) {
        // 自定义格式化逻辑
        ctx.write_str("MyType: ");
        ctx.write_str(value.to_string());
    }
};

} // namespace e_fmt::detail
```

### 方式 4: 使用流式输出适配器

如果你的类型已经定义了 `operator<<`，EFmt 会自动使用它：

```cpp
struct MyType {
    int value;
};

// 定义流式输出
std::ostream& operator<<(std::ostream& os, const MyType& t) {
    return os << "MyType(" << t.value << ")";
}

// 自动支持格式化
MyType t{42};
format("{}", t);  // "MyType(42)"
```

---

## 失败与边界

EFmt 没有异常，也没有错误码——格式化不会"失败"，只可能被截断或降级。所有接口都是
无错误返回；编译器能发现的错误就交给编译器。

| 情况 | 行为 |
|------|------|
| 实参少于占位符 | 该位置输出 `{?}`，其余内容照常格式化 |
| 实参多于占位符 | 多余实参被忽略 |
| 括号结构不合法（`{`、`}`、`{abc`） | 整个格式串原样输出 |
| 参数个数不匹配（编译期可知） | `E_FMT_STR` / `E_FMT_DECLARE_STR` + `static_assert`：编译失败 |
| 输出缓冲区装不下 | 按需截断，返回值遵循 `snprintf` 语义 |

### 缓冲区返回值（snprintf 语义）

`format_to(buffer, size, ...)` 返回**完整输出所需的长度**，不是写入长度：

- 返回值 `< size`：完整写入，缓冲区里有 `返回值` 个字符 + `'\0'`
- 返回值 `>= size`：被截断，缓冲区里是前 `size - 1` 个字符 + `'\0'`

```cpp
char buffer[16];
const size_t needed = format_to(buffer, sizeof(buffer), "value={}", 1234567890);
if (needed >= sizeof(buffer)) {
    // 太长：buffer 里只保留了开头 15 个字符
}

// 先问需要多大空间（精确值，可直接用来分配）
char *text = new char[formatted_size("value={}", 1234567890) + 1];
```

需要完整内容又不想自己算长度，用返回 `std::string` 的 `format()`：长度精确、不会截断。

> v1.3 起 `err_code`、`format_result`、`error_message`、`make_unexpected` 以及全部
> `*_with_error` 接口已移除：14 个错误码里有 10 个在实现中根本无法产生，唯一真实
> 的运行期状态是"缓冲区不够"，用返回值判断即可；参数个数错误在编译期就被拦住。

---

## 自定义输出（嵌入式友好）

EFmt 支持**自定义输出处理器**，让你可以在嵌入式环境中灵活控制输出目的地。

### 输出处理器类型

```cpp
// 输出处理器函数类型
using output_fn = void(*)(const char* data, size_t size);
```

### 基本用法

```cpp
#include <middleware/efmt/core/format_output.hpp>

// 定义你的输出函数
void my_uart_handler(const char* data, size_t size) {
    // 发送到 UART
    UART_Send((const uint8_t*)data, size);
}

int main() {
    // 设置输出处理器
    e_fmt::set_output_handler(my_uart_handler);

    // 现在所有打印都会输出到 UART
    e_fmt::println_info("System started");
    e_fmt::println_error("Error: {}", 0x1234);

    return 0;
}
```

### 内置输出处理器

```cpp
// 缓冲区输出
char log_buffer[512];
e_fmt::set_buffer_output(log_buffer, sizeof(log_buffer));
e_fmt::println_info("This goes to buffer");

// 获取写入位置
size_t written = e_fmt::get_buffer_output_pos();

// 恢复默认输出
e_fmt::reset_output_handler();
```

### 常见嵌入式场景

#### 1. UART 输出（STM32 HAL）

```cpp
void uart_output_handler(const char* data, size_t size) {
    HAL_UART_Transmit(&huart1, (uint8_t*)data, size, 1000);
}
e_fmt::set_output_handler(uart_output_handler);
```

#### 2. Segger RTT 输出

```cpp
extern "C" {
    int SEGGER_RTT_Write(int BufferIndex, const char* pBuffer, unsigned int NumBytes);
}

void rtt_output_handler(const char* data, size_t size) {
    SEGGER_RTT_Write(0, data, size);
}
e_fmt::set_output_handler(rtt_output_handler);
```

#### 3. ITM 输出（ARM Cortex-M 调试）

```cpp
void itm_output_handler(const char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        ITM_SendChar(data[i]);
    }
}
e_fmt::set_output_handler(itm_output_handler);
```

#### 4. SD 卡日志（FatFS）

```cpp
extern FIL log_file;

void sd_log_handler(const char* data, size_t size) {
    UINT written;
    f_write(&log_file, data, size, &written);
    f_sync(&log_file);
}
e_fmt::set_output_handler(sd_log_handler);
```

### 输出处理器 API

| 函数 | 说明 |
|------|------|
| `set_output_handler(fn)` | 设置全局输出处理器 |
| `get_output_handler()` | 获取当前输出处理器 |
| `reset_output_handler()` | 恢复默认输出处理器 |
| `set_buffer_output(buf, size)` | 设置缓冲区输出 |
| `get_buffer_output_pos()` | 获取缓冲区写入位置 |
| `reset_buffer_output_pos()` | 重置缓冲区位置 |

### RAII 风格的作用域管理

```cpp
{
    // 临时使用自定义处理器
    e_fmt::detail::output_handler_scope scope(custom_handler);

    e_fmt::println_info("This uses custom handler");

    // 作用域结束时自动恢复
}
e_fmt::println_info("Back to previous handler");
```

---

## 颜色与样式输出

EFmt 支持 ANSI 颜色代码，用于终端彩色输出（可在嵌入式环境中禁用）。

### 预定义样式

```cpp
// 错误信息（红色加粗）
println_error("Error: {} not found!", "file.txt");

// 警告信息（黄色）
println_warning("Warning: {} is deprecated!", "function");

// 信息（蓝色）
println_info("Processing {} files...", 42);

// 成功（绿色加粗）
println_success("Done! Processed {} items.", 100);

// 调试信息（灰色暗淡）
println_debug("Debug: value = {}", x);
```

### 自定义样式

```cpp
// 创建自定义颜色样式
text_style style = with_color(color::bright_cyan);
print_styled(style, "Cyan text\n");

// 前景色 + 背景色
text_style style2 = with_colors(color::white, bg_color::blue);
print_styled(style2, "White on blue\n");

// 组合样式（颜色 + 粗体 + 下划线）
text_style style3 = with_color(color::yellow) | style::bold | style::underline;
print_styled(style3, "Bold underlined yellow\n");
```

### 可用颜色

#### 前景色 (color)
```cpp
color::black, color::red, color::green, color::yellow,
color::blue, color::magenta, color::cyan, color::white,
color::bright_black, color::bright_red, color::bright_green,
color::bright_yellow, color::bright_blue, color::bright_magenta,
color::bright_cyan, color::bright_white
```

#### 背景色 (bg_color)
```cpp
bg_color::black, bg_color::red, bg_color::green, bg_color::yellow,
bg_color::blue, bg_color::magenta, bg_color::cyan, bg_color::white,
// ... 以及 bright_ 变体
```

#### 文本样式 (style)
```cpp
style::bold, style::dim, style::italic, style::underline,
style::blink, style::reverse, style::hidden, style::strikethrough
```

### 禁用颜色（提高性能）

如果不需要颜色样式，可以在编译时禁用（样式序列只是不再输出，接口保持不变）：

```cpp
#define EFMT_ENABLE_ANSI_STYLES 0
#include <middleware/efmt/core/format.hpp>
```

或在编译选项中添加：`-DEFMT_ENABLE_ANSI_STYLES=0`；嵌入式构建
（`EFMT_ENABLE_HOSTED=0`）默认就是关闭的。

---

## 容器格式化

EFmt 自动支持 STL 容器的格式化：

### 序列容器

```cpp
std::vector<int> nums = {1, 2, 3, 4, 5};
format("{}", nums);  // "[1, 2, 3, 4, 5]"

std::list<std::string> words = {"hello", "world"};
format("{}", words);  // "[hello, world]"

std::array<int, 3> arr = {10, 20, 30};
format("{}", arr);  // "[10, 20, 30]"
```

### 关联容器

```cpp
std::map<std::string, int> scores = {{"Alice", 90}, {"Bob", 85}};
format("{}", scores);  // "{Alice: 90, Bob: 85}"

std::unordered_map<int, std::string> id_map = {{1, "one"}, {2, "two"}};
format("{}", id_map);  // "{1: one, 2: two}"
```

### Tuple 和 Pair

```cpp
std::tuple<int, double, std::string> t = {42, 3.14, "hello"};
format("{}", t);  // "(42, 3.14, hello)"

std::pair<std::string, int> p = {"answer", 42};
format("{}", p);  // "(answer: 42)"
```

### 嵌套容器

```cpp
std::vector<std::vector<int>> matrix = {{1, 2}, {3, 4}};
format("{}", matrix);  // "[[1, 2], [3, 4]]"

std::map<std::string, std::vector<int>> data = {
    {"even", {2, 4, 6}},
    {"odd", {1, 3, 5}}
};
format("{}", data);  // "{even: [2, 4, 6], odd: [1, 3, 5]}"
```

---

## 编译期验证

EFmt 支持编译期格式字符串验证，确保参数数量匹配。参数个数由格式串自动推导，
不需要手写。

### 使用编译期字符串

```cpp
// 方式 1: 就地包装（推荐）
format(E_FMT_STR("Hello {}"), "World");        // ✓ OK
format(E_FMT_STR("x={}, y={}"), 10, 20);       // ✓ OK

// 方式 2: 声明成具名常量，便于复用
E_FMT_DECLARE_STR(coord_fmt, "({}, {})");      // 参数个数自动推导
for (const auto& p : points) {
    println_info(coord_fmt, p.x, p.y);         // 也适用于 print/println 系列
}
```

`E_FMT_STR` / `E_FMT_DECLARE_STR` 生成的对象可以隐式转换成 `std::string_view`，
因此任何接受格式串的接口都能直接使用；普通字符串字面量、`std::string`、
`std::string_view` 也照常可用，只是没有编译期校验。

### 参数数量错误（编译期报错）

```cpp
// 2 个占位符，只给 1 个实参：编译期报错
format(E_FMT_STR("x={}, y={}"), 10);
// error: static assertion failed: Number of arguments does not match format string

format(E_FMT_STR("x={}, y={}"), 10, 20, 30);   // 同样编译期报错
```

> 旧写法 `E_FMT_DECLARE_STR_1..5` 仍然可用，`_N` 后缀不再代表参数个数
> （个数以格式串为准，写错会被编译器发现）。

---

## API 参考

### 核心函数

所有接口都接受任意"字符串样"格式串：`const char*`、字符数组、
`std::string`、`std::string_view`、以及 `E_FMT_STR(...)`。
格式串不需要以 `'\0'` 结尾（`std::string_view` 按长度解析）。

#### `format()`
```cpp
template <typename... Args>
std::string format(std::string_view fmt_str, Args&&... args);
```
返回格式化后的字符串。长度精确，**不会截断**任何参数。

#### `format_to()`
```cpp
template <typename... Args>
size_t format_to(char* buffer, size_t size, std::string_view fmt_str, Args&&... args);

template <typename... Args>
void format_to(std::string& out, std::string_view fmt_str, Args&&... args);
```
格式化到缓冲区或字符串。缓冲区版本返回**所需长度**（snprintf 语义）：返回值
`>= size` 说明被截断，缓冲区里保留 `size - 1` 个字符 + `\0`。

#### `formatted_size()`
```cpp
template <typename... Args>
size_t formatted_size(std::string_view fmt_str, Args&&... args);
```
计算格式化结果的精确大小（不含空终止符）——数字、浮点、字符串、自定义类型
都按实际输出长度计算。

### 输出函数

无流（嵌入式）路径先把结果写进 `EFMT_PRINT_BUFFER_SIZE`（默认 256）
字节的栈缓冲区；需要完整输出请使用 `format*` 接口。

#### 预定义样式打印
```cpp
void print_error(std::string_view fmt_str, Args&&... args);
void println_error(std::string_view fmt_str, Args&&... args);
void print_warning(std::string_view fmt_str, Args&&... args);
void println_warning(std::string_view fmt_str, Args&&... args);
void print_info(std::string_view fmt_str, Args&&... args);
void println_info(std::string_view fmt_str, Args&&... args);
void print_success(std::string_view fmt_str, Args&&... args);
void println_success(std::string_view fmt_str, Args&&... args);
void print_debug(std::string_view fmt_str, Args&&... args);
void println_debug(std::string_view fmt_str, Args&&... args);
```

#### 通用样式打印
```cpp
void print_styled(const text_style& style, std::string_view fmt_str, Args&&... args);
void println_styled(const text_style& style, std::string_view fmt_str, Args&&... args);
```

### 可调配置宏

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `max_format_args` | 16 | 单次调用最多参数个数（超出的实参会在编译期报错） |
| `EFMT_PRINT_BUFFER_SIZE` | 256 | `print/println` 的栈缓冲区 |
| `EFMT_STRING_BUFFER_SIZE` | 256 | `format()` 返回 `std::string` 时先用的一次性栈缓冲区 |
| `EFMT_ENABLE_HOSTED` | 自动 | 总开关：0 = 嵌入式（无 `std::string`/流/stdio/ANSI） |
| `EFMT_ENABLE_DYNAMIC_STRING` | 跟随 hosted | 是否提供 `std::string` 重载 |
| `EFMT_ENABLE_STREAM_API` | 跟随 hosted | 是否提供 `std::ostream` 接口 |
| `EFMT_ENABLE_ANSI_STYLES` | 跟随 hosted | 是否输出 ANSI 颜色 |
| `EFMT_ENABLE_STDIO` | 跟随 hosted | 默认输出到 stdout |

---

## 最佳实践

### 1. 使用字符串字面量

```cpp
// 推荐
format("Value: {}", value);

// 不必要
std::string fmt = "Value: {}";
format(fmt, value);
```

### 2. 复用格式字符串

```cpp
// 声明一次，多次使用（参数个数自动推导）
E_FMT_DECLARE_STR(coord_fmt, "({}, {})");

for (const auto& p : points) {
    println_info(coord_fmt, p.x, p.y);
}
```

### 3. 让编译器校验格式串

唯一可靠的一致性检查发生在编译期：用 `E_FMT_STR` 包住格式串字面量，
占位符个数与实参个数不一致就直接编译失败。运行期不做校验——格式串不合法时
原样输出，实参不足时输出 `{?}`，都不会崩也不会静默截断。

```cpp
format(E_FMT_STR("x={}, y={}"), x, y);   // OK
format(E_FMT_STR("x={}, y={}"), x);      // 编译失败
```

### 4. 合理使用精度

```cpp
// 浮点数精度
format("{:.2f}", price);     // 货币: 2位小数
format("{:.6f}", pi);        // 科学计算: 6位小数

// 字符串截断
format("{:.10}", long_text); // 只显示前10个字符
```

### 5. 对齐表格输出

```cpp
// 创建整齐的列输出
for (const auto& item : items) {
    println_info("| {:<20} | {:>10.2f} |", item.name, item.price);
}
```

### 6. 嵌入式环境优化

```cpp
// 关闭宿主环境特性：无 std::string / 流 / stdio / ANSI 颜色
#define EFMT_ENABLE_HOSTED 0

// 使用缓冲区输出避免动态内存
char output_buffer[256];
e_fmt::set_buffer_output(output_buffer, sizeof(output_buffer));

// 缓冲区够不够看返回值（snprintf 语义），不需要异常/错误码
char buffer[64];
if (format_to(buffer, sizeof(buffer), fmt, args...) >= sizeof(buffer)) {
    // 被截断
}
```

---

## 性能与实现要点

- **格式串只扫一遍**：`format_to`（缓冲区）边扫描边解析字段边输出，文本段整体
  `memcpy`，字段解析不进中间结构（旧实现先建 token 流再建组件表，每个字段还要回退重扫）
- **规范解析是一次线性扫描**：`[[fill]align][sign][#][0][width][.precision][type]`，
  数字串溢出安全，宽度/精度不再受固定缓冲限制
- **整数按位生成**：从缓冲区尾部向前写数字，不再"先逆序再翻转"，前缀/补零/对齐
  分片输出，符号永远紧贴数字
- **参数打包零初始化开销**：`format_args` 只写用到的槽位（编译期限制默认 16 个参数）
- **字符串参数带长度**：不调用 `strlen`，空 `string_view`、内嵌 `'\0'` 都是安全的
- **动态字符串一条快路径**：先写 256 字节栈缓冲（绝大多数输出），装不下才精确计数
  + 二次写入

---

## 注意事项

### 1. 转义花括号

要输出字面量 `{` 或 `}`，使用双花括号：

```cpp
format("{{escaped}}");     // 输出: "{escaped}"
format("{{{{value}}}}");   // 输出: "{{value}}"
format("{{{}}}", 42);      // 输出: "{42}"
```

### 2. 缓冲区大小

使用 `format_to` 到字符数组时，确保缓冲区足够大：

```cpp
char buffer[256];  // 分配足够空间
size_t written = format_to(buffer, sizeof(buffer), "Value: {}", x);
```

### 3. 线程安全

格式化本身没有共享可变状态（自定义类型参数直接指向调用方的对象，不再使用
`thread_local` 暂存），因此 `format*` 系列可以在多线程下并发调用。

全局输出处理器（`set_output_handler` / `set_buffer_output`）是进程级状态，
`print/println` 系列共享它——多线程写同一个 sink 时需自行加锁。

### 4. 内存使用

- 最大参数数量: 16（`max_format_args`）
- `format()` 的栈缓冲区: 256 字节（`EFMT_STRING_BUFFER_SIZE`）
- `print/println` 的栈缓冲区: 256 字节（`EFMT_PRINT_BUFFER_SIZE`）
- 格式串长度、占位符个数、宽度/精度都不再有固定上限，也不再为每个字段预留固定容量

### 5. 输出长度

- `formatted_size()` 返回精确长度，可直接用来分配缓冲区
- `format_to` 的缓冲区版本返回所需长度：`返回值 >= size` 即被截断
- `format()` 返回 `std::string`：先写 256 字节栈缓冲，超长时按第一遍算出的
  精确长度重新分配再写一遍

---

## 完整示例

### 桌面环境示例

```cpp
#include <middleware/efmt/core/format.hpp>

#include <map>
#include <string>
#include <vector>

using namespace e_fmt;

struct Point {
    int x, y;
};

E_FMT_FORMATTER_2(Point, int, x, "x", int, y, "y");

int main() {
    // 基本格式化
    println_info("Hello, {}!", "World");

    // 格式规范
    println_info("| {:<10} |", "left");
    println_info("| {:>10} |", "right");
    println_info("| {:^10} |", "center");
    println_info("| {:0>10} |", "padded");

    // 数值格式化
    println_info("Hex: {:#x}", 255);
    println_info("Binary: {:b}", 255);
    println_info("Float: {:.2f}", 3.14159);
    println_info("Scientific: {:e}", 1234567.89);

    // 自定义类型
    Point p{10, 20};
    println_info("Point: {}", p);

    // 容器
    std::vector<int> nums = {1, 2, 3, 4, 5};
    println_info("Numbers: {}", nums);

    std::map<std::string, int> scores = {{"Alice", 90}, {"Bob", 85}};
    println_info("Scores: {}", scores);

    // 返回字符串（精确长度，可安全拼接/比较）
    std::string line = format("({}, {})", p.x, p.y);

    // 彩色输出
    println_error("Error occurred!");
    println_warning("Check your input");
    println_info("Processing data...");
    println_success("Operation completed!");

    return 0;
}
```

### 嵌入式环境示例

```cpp
#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

// UART 输出函数
void uart_send(const char* data, size_t size) {
    // 假设的 UART 发送函数
    for (size_t i = 0; i < size; ++i) {
        UART_SendByte(data[i]);
    }
}

// 自定义类型
struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

E_FMT_FORMATTER_3(SensorData,
    float, temperature, "temp",
    float, humidity, "hum",
    uint32_t, timestamp, "time");

int main() {
    // 设置输出到 UART
    e_fmt::set_output_handler(uart_send);

    // 系统启动
    e_fmt::println_success("System initialized!");
    e_fmt::println_info("Firmware version: {}", "1.0.0");

    // 读取传感器
    SensorData sensor{25.5f, 60.0f, 1234567890};
    e_fmt::println_info("Sensor: {}", sensor);

    // 截断检查（snprintf 语义）
    char line_buffer[16];
    if (e_fmt::format_to(line_buffer, sizeof(line_buffer), "Value: {:.2f}", 3.14159) >=
        sizeof(line_buffer)) {
        e_fmt::println_error("line too long");
    } else {
        e_fmt::println_info("{}", line_buffer);
    }

    return 0;
}
```

---

## 版本历史

- **v1.0** - 初始版本
  - 基本格式化功能
  - 格式规范支持
  - ANSI 颜色支持
  - 容器格式化
  - 自定义类型支持

- **v1.1** - 嵌入式增强
  - 无异常（错误码在 v1.3 移除）
  - 自定义输出处理器
  - UART/RTT/SD卡 等输出支持
  - 缓冲区截断检测

- **v1.2** - 解析性能与接口一致性
  - 格式串改为单遍扫描执行：去掉中间 token/token 流与组件表，一次遍历直接输出
  - `formatted_size()` 与 `format()` 长度精确：不再按"每参数 64 字节"估算，
    长字符串/自定义类型不再被静默截断
  - `format()` 默认单遍写入栈缓冲区，超长才二次写入；参数打包不再逐个清零
  - 格式规范解析重写：支持自定义填充 `{:*>10}` 与 `0` 选项（`{:08}`→`00000042`），
    十进制/十六进制/八进制/二进制/字符类型全部生效
  - 修复：64 位无符号数被当成负数、`unsigned char` 等小整型走错通道、
    大精度（如 `{:.200}`）越界写坏静态缓冲、`{:+.200f}` 栈溢出
  - 编译期校验改用 `E_FMT_STR` / `E_FMT_DECLARE_STR`：参数个数自动推导，
    不再需要 `E_FMT_DECLARE_STR_1..5` 与 `basic_format_string<N, Count>`
  - 接口统一为 `std::string_view`：`const char*`/`std::string`/`std::string_view`/字符数组
    共用一套重载，格式串不需要 `'\0'` 结尾
  - 字符串参数携带长度：不再 `strlen`，支持空 `string_view` 与内嵌 `'\0'`
  - 自定义类型参数不再依赖 `thread_local` 包装器数组（嵌套/多参数不再互相干扰）
  - 样式转义序列不再用 `snprintf`，`EFMT_ENABLE_STDIO=0` 的嵌入式构建可直接编译

- **v1.3** - 去掉错误码
  - 删除 `err_code` / `format_result` / `error_message` / `make_unexpected` / `error_context`
    与全部 `*_with_error` 接口（14 个错误码里 10 个从未被产生过）
  - `format_to` 改为 snprintf 语义：返回所需长度，`>= size` 即为截断；
    删除 `format_output`，不再需要 expected 包装
  - efmt 只依赖 C++17 标准库：不再包含 `<middleware/etl/expected.h>`，
    连带解决了 `expected.h → eresult → elog → format.hpp` 的循环包含（先包含
    format.hpp 的编译单元此前编不过）
  - 副产品：`formatted_size`/长字符串路径少一次计数扫描，`format()` 更快
  - elog：改用返回值判断截断，移除 `map_efmt_error` 与 `errc::formatting_failed`

---

## 许可证

请参阅项目根目录的 LICENSE 文件。

---

## 贡献

欢迎提交问题和拉取请求！
