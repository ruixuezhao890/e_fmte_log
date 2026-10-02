# matchit/matchit.h 的本地改动清单

本目录是 [matchit.cpp](https://github.com/BowenFu/matchit.cpp)（Apache-2.0）的**冻结副本 + 最小改动**。

| 项 | 值 |
|---|---|
| 上游 | `BowenFu/matchit.cpp` |
| 冻结提交 | `0ba73cc433897b3d87c7ee41870a8e5c73ff54b9`（2026-01-22） |
| 上游文件 | `include/matchit.h`，82476 B，sha256 前缀 `3E6BA8A4BAA4E6D3` |
| 本地文件 | `matchit/matchit.h`，84013 B（含补丁与横幅） |
| 许可证 | Apache-2.0（见同目录 `LICENSE`，原样复制） |

## 改动 1：无异常构建可用（6 处 `throw`）

上游在"逻辑上不该发生"的地方抛 `std::logic_error`；我们全库是**无异常**风格
（`-fno-exceptions`），所以改成可配置的失败宏，**有异常时行为与上游完全一致**。

在文件头部（版权注释之后）新增：

```cpp
MATCHIT_USE_EXCEPTIONS   // 自动探测：有异常 1，-fno-exceptions 下 0（可自行覆盖）
MATCHIT_FAIL()           // 默认 (assert(false && "..."), std::abort())
MATCHIT_NO_MATCH()       // 默认 assert(false && "matchit: no patterns got matched!")
#include <cassert> / <cstdlib>
```

| # | 上游位置 | 上游写法 | 现在 |
|---|---|---|---|
| 1 | `IdBlockBase<Type>` 的 `std::monostate` 分支 | `throw std::logic_error("invalid state!");` | `MATCHIT_FAIL();` |
| 2 | `IdBlockBase<Type&>` 空指针 | `throw std::logic_error("Trying to dereference a nullptr!");` | `MATCHIT_FAIL();` |
| 3 | `IdBlockBase<Type&>` monostate | `throw std::logic_error("Invalid state!");` | `MATCHIT_FAIL();` |
| 4 | `IdBlockBase<Type&&>` 空指针 | 同 #2 | `MATCHIT_FAIL();` |
| 5 | `IdBlockBase<Type&&>` monostate | 同 #3 | `MATCHIT_FAIL();` |
| 6 | `match(...)` 的"一条都没匹配上" | `throw std::logic_error{"Error: no patterns got matched!"};` | `#if MATCHIT_USE_EXCEPTIONS` 保留原 throw；`#else` 走 `MATCHIT_NO_MATCH(); return result;` |

语义提示：改动 6 在无异常构建下，NDEBUG 时会**返回返回值类型的默认值**（不再抛）。
我们自己的用法一律带 `pattern | _` 兜底，所以这条路径不会走到；
要别的行为（写日志 / 复位）就先自己定义 `MATCHIT_NO_MATCH()`。

## 改动 2：一处可移植性 bug（ARM 上编不过）

```cpp
// 上游（include/matchit.h 内的一段自检）
constexpr auto y = 1;
static_assert(std::holds_alternative<int32_t const *>(
    std::variant<std::monostate, const int32_t *>{&y}));
```

在 `int32_t` 不是 `int` 的平台上（例如 arm-none-eabi：`int32_t` 是 `long int`），
`&y` 是 `const int*`，与 `const int32_t*` 对不上 → 直接编译失败。改成：

```cpp
constexpr auto y = int32_t{1};
```

语义不变，只是让 `y` 的类型就是 `int32_t`。

## 重新同步上游的步骤

1. 取上游新的 `include/matchit.h`；
2. 按上表重放改动 1（插横幅 + 6 处替换）与改动 2；
3. 更新本文件的"冻结提交 / sha256"两栏；
4. 跑 `tests/run_check.ps1`（ecli 的模式命令测试会覆盖我们用到的部分：
   `app` / `some` / 通配 `_` / 字符串比较）。

> 我们只用到 matchit 的一小块：**把"一个 token 和一个段模式"的判定交给它**
> （见 `ecli/command.hpp` 的 `segment_extractor` + `match_segment`）。
> 上游其它部分（`Id` 绑定、`ds` 析构、范围模式、guard……）没有用上，但都留在文件里，
> 你要用随时可用 —— 这也是"最小适配、保持可 diff"的意思。
