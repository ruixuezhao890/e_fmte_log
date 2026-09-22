# EFmt · ELog —— 嵌入式 C++17 格式化与分级日志

纯头文件库：**EFmt** 提供 `{}` 占位符格式化（类似 Python f-string / C++20 `std::format`），
**ELog** 在其上提供分级日志。面向资源受限的嵌入式环境（STM32 / ESP32 / AVR / Mbed ……）：
**无异常、无错误码、不用堆、无 RTTI**，浮点不依赖 libc 的 `printf`，输出可路由到
UART / RTT / ITM / SD 卡 / 任意缓冲区。

- 语言：C++17 及以上，单文件头 + 宏配置，无脚本、无生成器、无第三方依赖（EFmt 本体）
- 许可：MIT（见 [LICENSE](LICENSE)）

## 特性

| | |
|---|---|
| 类型安全 | 格式串里的 `{}` 按类型匹配实参，`E_FMT_STR` 包住格式串可**编译期校验**参数个数/类型 |
| 格式规范 | `{:.2f}` `{:#06x}` `{:<12}` `{:>8}` 等完整规范（Python / std::format 风格） |
| 输出路由 | 一个 `(const char*, size_t)` 回调：UART / RTT / ITM / SD / 缓冲区 / stdout 均可 |
| 自带浮点引擎 | 不依赖 libc printf、不用堆；与 printf **逐位一致**（24.6 万次随机差分对拍 0 失败），比 libc 快 40%+ |
| 可裁剪 | 13 个开关宏；不打印浮点可关掉省 ~3.4 KB Flash；嵌入式默认配置开箱即用 |
| 自定义类型 | 一行 `E_FMT_DERIVE(...)` 声明即推导字段/枚举名（对标 Rust `#[derive(Debug)]`），纯 C++17 实现 |
| 分级日志 | ELog：trace→critical + off，多 logger 独立 sink、运行期 `set_level`，直接格式化 **ETL 类型**（`etl::string`/`vector`/`optional`/`variant` …） |

## 目录结构

```
efmt/                 格式化库本体（13 个头文件，无第三方依赖）
  core/format.hpp     入口：format / format_to / formatted_size / print* / println*
  core/format_output.hpp  输出处理器（UART/RTT/缓冲/…）与 set_output_handler
  core/format_derive.hpp  自定义类型推导（E_FMT_DERIVE / FIELDS / AUTO / ENUM）
  core/其他 *.hpp     模块按需自动包含，一般不用直接碰
elog/elog.hpp         分级日志（构建在 EFmt 之上；需要 ETL）
docs/EFMT-使用手册.md  完整新手手册（18 章）——新用户从这里开始
tests/                零框架行为检查 + 浮点差分对拍 + 基准 + 编译期反例
sandbox/              CLion 试玩工程（打开即跑，19 条自检走查）
```

## 如何加入你的项目

### 1) 复制目录

| 你需要的功能 | 复制 | 额外依赖 |
|---|---|---|
| 只格式化 | `efmt/` | 无 |
| 还要分级日志 | `efmt/` + `elog/` | ETL（见下） |

### 2) 保持 `middleware/` 目录形状

代码里所有 include 都是 `<middleware/efmt/...>` 与 `<middleware/etl/...>` 形式，
所以复制后要形成如下结构（`efmt/` 目录整体改名或软链成 `middleware/efmt` 即可）：

```
你的工程/
├── middleware/
│   ├── efmt/core/*.hpp      ← 本仓库的 efmt/ 目录
│   └── etl/                 ← 仅当使用 elog：ETL 的头文件目录（含 string.h 的那一层）
└── src/main.cpp
```

编译时把 `middleware/` 的**上一层**（即你的工程根）加进 include 路径：

```bash
g++ -std=c++17 -I . -O2 src/main.cpp -o app
arm-none-eabi-g++ -std=c++17 -Os -mcpu=cortex-m4 -mthumb -I . src/main.cpp -o app.elf

# Keil / IAR：把工程根加进 "Include Paths" 即可，无需其他配置
```

> EFmt 不用异常、不用 RTTI、不用堆：`-fno-exceptions -fno-rtti` 可以放心开；
> 裸机工程用 `--specs=nano.specs` 也没问题（浮点不依赖 libc）。

### 3) 最简 CMake（可选）

```cmake
add_executable(app src/main.cpp)
target_compile_features(app PRIVATE cxx_std_17)
target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR})   # middleware/ 的上一层
```

### 4) 使用 elog 时接 ETL

elog 通过 `<middleware/etl/...>` 使用 ETL 类型（`etl::string` / `etl::vector` / …）。
把 ETL 分发包里的头文件目录（包含 `string.h` 的那一层，即 `.../include/etl` 的内容）
放到 `middleware/etl/`，或用软链/junction 指过去（Windows: `cmd /c mklink /J`）。

> 坑：不要把 `etl/` 目录本身加进 include 路径（ETL 自带 `string.h` 等系统同名头，
> 会遮蔽 `<cstring>` 等标准头）。指到它的**父目录**才是 `<etl/...>` 的接法。

### 5) 不用 elog？

那就只复制 `efmt/`，什么都不用接——EFmt 本体零依赖，C++17 编译器即可。

## 30 秒上手

```cpp
#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <cstdio>
using namespace e_fmt;

// 1) 告诉 EFmt 输出到哪里：一个 (data, size) 回调，注册一次全局生效
static void uart_sink(const char* data, size_t size) {
    std::fwrite(data, 1, size, stdout);   // 嵌入式里换成 HAL_UART_Transmit 即可
}

int main() {
    set_output_handler(uart_sink);

    // 2) 打印：println_* / print_*（info/warning/error 三档前缀）
    println_info("boot {} build {}", "1.8.0", 20260322);
    println_warning("battery {}%", 18);
    println_error("sensor 0x{:02X} timeout after {} ms", 0x1A, 250);
    println_info("temp={:.1f}C hum={:.1f}%", 25.5f, 60.0f);

    // 3) 写缓冲区（snprintf 语义：返回值 < size 表示完整写入）
    char line[64];
    size_t n = format_to(line, sizeof(line), "x={} y={:#06x}", 10, 0xAB);

    // 4) 编译期校验：实参个数/类型与格式串不符 -> 直接编译报错
    constexpr auto f = E_FMT_STR("{} / {:04X}", 1, 0x2A);
    println_info("{}", format(f, 42, 0x2A));
    return 0;
}
```

**自定义类型一行推导**（结构体、枚举都行，字段名/取值名自动生成）：

```cpp
E_FMT_DERIVE(struct imu { float ax, ay, az; });
E_FMT_DERIVE(enum class state { idle, busy = 5 });
println_info("{}", imu{1.5f, 2.5f, 3.5f});   // { ax = 1.5, ay = 2.5, az = 3.5 }
println_info("{}", state::busy);            // busy
```

**分级日志**（需要 ETL）：

```cpp
#include <elog/elog.hpp>
static void log_sink(const char* data, size_t size) { std::fwrite(data, 1, size, stderr); }

// 第一个创建的 logger 自动成为全局默认；最多 8 个，名字 ≤ 31 字符
e_log::logger* app = e_log::create_logger("app", e_log::make_sink(&log_sink), e_log::level::debug);
ELOG_INFO("boot ok, temp={:.1f}", 25.5f);        // 默认 logger + 自动带 文件:行 函数
ELOG_LOGGER_WARN(*app, "battery {}%", 18);       // 指定 logger
app->set_level(e_log::level::warn);              // 运行期过滤（trace→critical→off）
```

## 常用配置宏（嵌入式默认值）

不写任何宏时按平台自动判定：**Cortex-M / AVR / MSP430 / Arduino / Mbed 进嵌入式配置**
（无 std::string、无流、无 stdio、无 ANSI、参数上限 8、浮点走自带引擎），
Windows / Linux / **ESP-IDF 走宿主配置**（容器、std::string、ANSI、stdio 自动打开）。
完整总表见手册[附录 B](docs/EFMT-使用手册.md)。

| 宏 | 嵌入式默认 | 作用 |
|---|---|---|
| `EFMT_ENABLE_FLOAT` | 1 | 0 = 完全不编浮点（省 ~3.4 KB） |
| `EFMT_USE_LIBC_PRINTF` | 0 | 1 = 浮点改用 libc snprintf |
| `EFMT_MAX_FORMAT_ARGS` | 8 | 单次调用参数上限（每个约 24 B 栈） |
| `EFMT_ENABLE_CONTAINER_FORMAT` | 0（elog: 1） | 容器/tuple 格式化 |
| `EFMT_ENABLE_ANSI_STYLES` | 0 | ANSI 颜色字节 |
| `EFMT_PRINT_BUFFER_SIZE` | 256 | print/println 单行栈缓冲 |
| `EFMT_DERIVE_STRICT` | 1 | 0 = 允许老宏写错作用域时退化成地址输出 |
| `ELOG_MAX_LOGGERS` / `ELOG_MAX_RECORD_SIZE` | 8 / 384 | logger 槽位数 / 单行日志栈缓冲 |

## 文档

| 文档 | 内容 |
|---|---|
| [docs/EFMT-使用手册.md](docs/EFMT-使用手册.md) | **完整使用手册（新手向，18 章）**：上手指南、格式规范全语法、嵌入式配置、Flash/RAM/栈实测、浮点、FAQ、printf 迁移对照、elog 集成、API 速查 |
| [efmt/core/README.md](efmt/core/README.md) | 模块索引、版本要点（v1.4~v1.7 逐版说明） |
| [sandbox/](sandbox/README.md) | CLion 试玩工程：打开即跑，19 条自检，覆盖主要 API 与 ETL 类型 |
| [tests/](tests/run_check.ps1) | 验证脚本：行为检查、浮点对拍、嵌入式交叉编译体积报告 |

## 验证与自测

```powershell
.\tests\run_check.ps1            # 宿主 C++17/C++20 + 浮点对拍 + 嵌入式 + 最小裁剪 + 编译期反例 + elog
.\tests\run_check.ps1 -Bench     # 额外跑微基准
.\tests\run_check.ps1 -Size      # 额外量 Cortex-M / ESP32 的 Flash 与 RAM
```

当前基线：行为检查 143 项 × 2 标准、E_FMT_DERIVE 22 项 × 2 配置、派生宏 27 项（宿主+嵌入式）、
浮点对拍 24.6 万次 0 失败、四个编译期反例按预期失败 —— 全部通过。

> 需要 ETL 才能跑 elog 相关步骤：没接 ETL 时脚本会跳过并提示（`-EtlInclude` 指定位置）。

## 版本历史

- **v1.8** elog 支持 ETL 类型：`etl::string/optional/pair/variant` + 容器直接格式化，elog 默认开容器格式化；便捷 API 改 `const Args&...` 转发
- **v1.7** 修复老宏写错作用域静默退化成地址输出（`EFMT_DERIVE_STRICT` 编译期拦截）
- **v1.6** `E_FMT_DERIVE` 声明即推导（纯 C++17，对标 Rust `#[derive(Debug)]`），`E_FMT_FIELDS` 类型内兜底
- **v1.5** 自定义类型自动派生：`E_FMT_FORMATTER_FIELDS/AUTO/ENUM`（ADL 定制点）
- **v1.4** 嵌入式专项优化：自带浮点引擎、裁剪开关真正生效、elog 单遍直写、新手手册
- **v1.3** 去掉错误码：`format_to` 改 snprintf 语义，只依赖 C++17 标准库
- **v1.2** 单遍扫描执行、`E_FMT_STR` 编译期校验
- **v1.1** 嵌入式增强：无异常、自定义输出处理器、截断检测
- **v1.0** 初版：基本格式化、格式规范、ANSI 颜色、容器、自定义类型

## License

MIT —— 见 [LICENSE](LICENSE)。ETL 为第三方库（仅在 elog/测试中引用，不属于本仓库）。
