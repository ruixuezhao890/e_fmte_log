# matchit —— Rust `match` 表达式的 C++ 移植（第三方，冻结副本）

- 上游：<https://github.com/BowenFu/matchit.cpp> · Apache-2.0 · 单头文件 · C++17/20
- 本目录**只**是 vendored 副本 + 最小改动（见 [PATCHES.md](PATCHES.md)），不是我们的代码。
- 为什么引进它：ecli 的命令表要支持**命令名模式段**（`"wifi set :ssid"` / `"log *rest"`），
  段的判定交给 matchit 的 **extractor 协议**（`app` + `some` + 通配 `_`），
  我们不自己写一套模式解释器。

## 用在哪

```
ecli/command.hpp
  segment_extractor          // 段模式对象：":name" / "*name" → 任意 token；字面量 → 必须相等
  match_segment()            // match(token)( pattern | app(extractor, some(_)) = true, pattern | _ = false )
  match_command_pattern()    // 逐段走命令名，收集捕获（:name 一个 token / *rest 余下全部）
```

关掉 `ECLI_ENABLE_PATTERN_COMMANDS=0` 时 **不 include 本文件**，命令名只认字面量段。

## 我们踩过的坑（写在前面，省得再踩）

1. **别用 `Id<T>` 接捕获值**：它存的是**指针**（绑定 match 表达式里的临时值），
   表达式一结束就悬垂 —— 第一版拿到的就是垃圾内存。判定用 matchit、取值直接用我们手上的
   token（`match_segment` 就是这么写的）。
2. **`-fno-exceptions` 编不过**：上游 6 处 `throw`，见 PATCHES.md 改动 1。
3. **ARM 上有个上游自检编不过**：`int32_t` 不是 `int` 的平台，见 PATCHES.md 改动 2。

## 代价（Cortex-M4 `-Os` 整程序，`run_check.ps1 -Size` 实测）

| 配置 | .text |
|---|---|
| 命令表 2 条（`ECLI_ENABLE_PATTERN_COMMANDS=0`，不含 matchit） | 9924 B |
| 命令表 2 条（模式匹配开，含 matchit） | **10580 B** |
| 单条 `:param` 命令（含 matchit） | 7184 B |

即 matchit 本体的边际开销约 **0.6 KB**（其余是捕获注入与 params 管线）。
