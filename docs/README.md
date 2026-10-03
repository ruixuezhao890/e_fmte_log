# 文档索引：先看哪一份

> 规则一句话：**新增任何能力，都要在 `docs/` 里留下一份"新手照着做就能跑"的使用文档**（同一批改动里一起提交）。
> 下面的表按"你现在的身份 / 想做的事"排，从上往下选一份就行。

| 你现在的处境 | 看哪一份 |
|---|---|
| 第一次拿到这个仓库，想先跑起来看看效果 | [沙盒命令台：新手 10 分钟上手](SANDBOX-命令台上手指南.md) |
| 要把库用进自己的工程（格式化 / 分级日志 / JSON·CBOR / 命令行）| [EFMT-使用手册.md](EFMT-使用手册.md)（18 章，新手向：第 1–3 章跑通，第 4–13 章查用法，第 14 章以后当字典）|
| 要**动手写代码**：命令行工具 / JSON·CBOR 存取 / 命令名模式匹配 | **[libs/](libs/)：ecli · eserde · matchit 三份独立使用手册**（示例驱动，每段代码都有编译验证）|
| 想知道 ecli 为什么这么设计、后面还打算做什么 | [ECLI-命令行解析-方案.md](ECLI-命令行解析-方案.md) |
| 想知道 ecli 和 Rust clap 到底差在哪 | [ECLI-与clap的差距清单.md](ECLI-与clap的差距清单.md) |
| 想知道 eserde 为什么只有 JSON / CBOR、YAML / XML 值不值得加 | [ESERDE-为什么没有YAML与XML-评估报告.md](ESERDE-为什么没有YAML与XML-评估报告.md) |
| 想把某次改动回滚掉 | [backup/](backup/) 下的 `*-回滚说明.md` + 同名 `.bundle` |

## 三条最短路径

1. **先看效果**（约 3 分钟）：
   ```bash
   cmake -S sandbox -B build -G Ninja && cmake --build build && ./build/sandbox
   ```
   敲 `num 42` → `log warn` → `net set mynet` → `help args` → `quit`。
2. **用进自己的工程**：手册第 2 章（把库放进工程）→ 第 3 章（四个核心接口）→ 第 7 章（裁剪宏）。
3. **加一条自己的命令**：[沙盒指南第 6 节](SANDBOX-命令台上手指南.md)（参数类型 / 处理函数 / 命令表 / 重编译）。

## 文件一览

```
docs/
  README.md                        本索引
  SANDBOX-命令台上手指南.md         沙盒：跑起来 / 敲起来 / 加自己的命令（新手入口）
  EFMT-使用手册.md                  完整手册：efmt / elog / eserde / ecli 全量用法与体积实测
  ECLI-命令行解析-方案.md           ecli 的拍板结论、交付物、边界
  ECLI-与clap的差距清单.md          ecli ↔ clap 逐条对照
  ESERDE-为什么没有YAML与XML-评估报告.md  格式扩展评估：为什么没有 YAML/XML、值不值得加（结论：不值得内置）
  libs/                            三个库的独立使用手册（示例驱动，新手写代码看这里）
    README.md                      三个库的分工、依赖关系、学习路线
    ECLI-使用手册.md               命令行解析 + 命令表 + 回复通道
    ESERDE-使用手册.md             能力基座 + JSON + CBOR
    MATCHIT-使用手册.md            第三方 matchit 的用法与本仓库的接缝
  backup/                          每次大改前的 git bundle + 回滚说明
```

> 另有几份"就地文档"跟着各自目录走，不搬进 `docs/`：
> [`sandbox/README.md`](../sandbox/README.md)（include 拓扑、宏开关总表）、
> [`matchit/README.md`](../matchit/README.md) 与 [`PATCHES.md`](../matchit/PATCHES.md)（第三方冻结副本与改动）。
