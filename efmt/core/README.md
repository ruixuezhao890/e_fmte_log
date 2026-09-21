# EFmt — 嵌入式 C++ 格式化库

EFmt 是一个**只有头文件**的 C++17 格式化库（`{}` 占位符、类似 Python f-string / C++20 `std::format`），
面向资源受限的嵌入式环境：**无异常、无错误码、不用堆、不依赖 libc 的 printf**，
输出可路由到 UART / RTT / ITM / SD 卡 / 任意缓冲区。

```cpp
#include <middleware/efmt/core/format.hpp>
using namespace e_fmt;

set_output_handler(uart_sink);                     // 你的 (data, size) 回调
println_info("boot {}, temp={:.1f}C", "1.4.0", 25.5f);

char line[64];
if (format_to(line, sizeof(line), "x={} y={}", 10, 20) < sizeof(line)) {
    // 完整写入（snprintf 语义：返回值 >= size 表示被截断）
}
```

---

## 文档

| 文档 | 内容 |
|------|------|
| **[docs/EFMT-使用手册.md](../../docs/EFMT-使用手册.md)** | **完整使用手册（新手向）**：上手指南、格式规范、嵌入式配置、体积/栈/速度实测、FAQ、printf 迁移、elog 集成、API 速查 |
| 本文件 | 目录结构、快速索引、版本要点 |
| [tests/run_check.ps1](../../tests/run_check.ps1) | 一键验证（行为检查 + 浮点对拍 + 交叉编译体积报告） |

---

## 目录结构

```
efmt/core/            格式化库本体（头文件，13 个模块）
  format_base.hpp     平台开关与宏、基础类型别名
  format_args.hpp     类型擦除的参数表
  format_context.hpp  输出上下文（缓冲区/计数/对齐）
  format_parser.hpp   格式规范解析
  format_specs.hpp    解析结果
  format_string.hpp   E_FMT_STR 等编译期格式串
  format_style.hpp    ANSI 颜色与样式
  format_traits.hpp   类型分派与扩展点
  formatter.hpp       内置类型格式化器
  format_float.hpp    自带浮点引擎（不依赖 libc printf）
  format_derive.hpp   自定义类型自动派生（AUTO / FIELDS / ENUM）
  format_range.hpp    容器/tuple（宿主默认开，嵌入式默认关）
  format_output.hpp   输出处理器（UART/RTT/缓冲/丢弃）
  format.hpp          入口：format / format_to / formatted_size / print*
elog/elog.hpp         分级日志库（构建在 efmt 之上，需要 ETL）
tests/                零框架行为检查 + 浮点差分对拍 + 基准 + 反例
docs/                 使用手册
```

---

## 平台与裁剪

不写任何宏时：**Cortex-M / AVR / MSP430 / Arduino / Mbed 自动判定为嵌入式**（无 `std::string`、
无流、无 stdio、无 ANSI、8 个参数上限、浮点走自带引擎）；Windows / Linux / **ESP-IDF** 走宿主模式。

常用开关（完整表见使用手册的「附录 B：宏速查与代价」）：

| 宏 | 嵌入式默认 | 作用 |
|----|-----------|------|
| `EFMT_ENABLE_FLOAT=0` | 1 | 完全不编译浮点（省约 3.4 KB） |
| `EFMT_USE_LIBC_PRINTF` | 0 | 1 = 浮点改用 libc `snprintf` |
| `EFMT_MAX_FORMAT_ARGS` | 8 | 单次调用参数上限（每个约 24 B 栈） |
| `EFMT_ENABLE_CONTAINER_FORMAT` | 0 | 容器/tuple 格式化 |
| `EFMT_ENABLE_ANSI_STYLES` | 0 | ANSI 颜色字节 |
| `EFMT_PRINT_BUFFER_SIZE` | 256 | `print/println` 单行栈缓冲 |
| `EFMT_FLOAT_BIGNUM_LIMBS` / `EFMT_FLOAT_DIGIT_GROUPS` | 48 / 48 | 浮点精度与位数上限、栈占用 |

---

## v1.4 嵌入式专项优化（本版要点）

* **自带浮点引擎**（`format_float.hpp`）：不依赖 libc 的 `printf`、不用堆，与 printf **逐位一致**
  （24.6 万次随机位模式差分对拍，0 失败）。Cortex-M4 `-Os` 实测：一处 `{:.2f}` 由 7133 B →
  **6552 B**，且不再链入 `_malloc_r / _free_r / _sbrk`；速度比 libc 快 40%+。
* **修掉 ANSI 开关失效**：`EFMT_ENABLE_ANSI_STYLES=0`（含嵌入式默认）下 `println_*` 不再往
  串口写 `ESC[34m`（此前只有 elog 遵守该宏）。嵌入式下同时省掉 64 B 样式栈缓冲。
* **可裁剪化**：新增 `EFMT_MAX_FORMAT_ARGS`、`EFMT_ENABLE_FLOAT`、
  `EFMT_ENABLE_CONTAINER_FORMAT`、`EFMT_FLOAT_BIGNUM_LIMBS/DIGIT_GROUPS`；
  参数表从 384 B 降到 192 B，容器/流回退在嵌入式下默认不编译。
* **平台判定更准**：改用 `__STDC_HOSTED__` + 裸机特征宏，32 位 ARM Linux 不再被误判为嵌入式。
* **速度**：填充改 `memset`；`format("{:.2f}|{:<12}|{:#06x}")` 324 → **184 ns**。
* **elog 单遍直写**：日志不再"格式化 payload 再整体重排"，省一块 257 B 栈（帧 960 → 896 B），
  一行 2 参数日志 197 → **152 ns**；参数按 `const&` 转发不复制。
* **测试**：新增浮点差分对拍、最小裁剪配置、浮点关闭反例；`run_check.ps1` 支持 `-Size`
  交叉编译体积报告（arm-none-eabi / xtensa）。

---

## v1.5 自定义类型自动派生

不用再抄"成员类型 + 成员名 + 显示字符串"三遍：

```cpp
struct point { int x, y; };
E_FMT_FORMATTER_FIELDS(point, x, y);   // 输出 {x=10, y=20}；类型由 &point::x 推导，名字由 #x 生成

struct reading { float temp; float hum; unsigned ts; };
E_FMT_FORMATTER_AUTO(reading);         // 输出 reading(25.5, 60, 12345)，改结构体不用同步

enum class color { red, green, blue };
E_FMT_FORMATTER_ENUM(color, red, green, blue);   // 输出 red / green / blue
```

* 宏展开成自由函数（ADL 定制点），**不进 e_fmt 命名空间**：用户类型名与库内部同名符号
  （`detail::color`/`detail::style` …）不再互相误伤
* 枚举现在能真正走到格式化器：以前无作用域枚举会被 ostream 的 `operator<<(int)` 抢走
* `AUTO` 只支持简单聚合体；含 C 型数组时用 `E_FMT_FORMATTER_AUTO_N(Type, 字段数)`
* 老的 `E_FMT_FORMATTER_1/2/3` / `_FN` 照常可用

## 验证

```powershell
.\tests\run_check.ps1           # 宿主 C++17/C++20 + 浮点对拍 + 嵌入式 + 最小裁剪 + 反例 + elog
.\tests\run_check.ps1 -Bench    # 额外跑微基准（两种浮点实现各一轮）
.\tests\run_check.ps1 -Size     # 额外量 Cortex-M / ESP32 的 Flash 与 RAM
```

当前基线：行为检查 143 项 × 2 个标准全通过、自动派生 27 项（宿主 + 嵌入式）、
浮点对拍 24.6 万次 0 失败、三个编译期反例按预期失败。

---

## 致谢与许可

请参阅项目根目录的 LICENSE 文件。第三方 ETL 头文件通过 `tests/include/middleware/etl`
junction 引用，不属于本仓库。
