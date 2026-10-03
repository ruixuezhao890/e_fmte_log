# 领域文档

工程技能探索代码库时应如何消费本仓库的领域文档。

## 探索之前先读这些

- 仓库根目录的 **`GLOSSARY.md`**，或
- 根目录存在 **`GLOSSARY-MAP.md`** 时：它指向每个上下文各一份 `GLOSSARY.md`，读与当前主题相关的那些。
- **`docs/adr/`**：读涉及你即将动手区域的 ADR。多上下文仓库还要看 `src/<context>/docs/adr/` 里的上下文级决策。

这些文件不存在时**静默继续**：不要提示缺失，也不要建议提前创建。`/domain-modeling` 技能（经 `/grill-with-docs`、`/improve-codebase-architecture` 到达）会在术语或决策真正敲定时按需创建。

## 文件布局

单上下文仓库（本仓库）：

```
/
├── GLOSSARY.md
├── docs/adr/
│   ├── 0001-....md
│   └── 0002-....md
└── efmt/ · elog/ · eserde/ · ecli/ · matchit/ · sandbox/
```

多上下文仓库（根目录存在 `GLOSSARY-MAP.md`）：

```
/
├── GLOSSARY-MAP.md
├── docs/adr/                ← 系统级决策
└── src/
    └── <context>/
        ├── GLOSSARY.md
        └── docs/adr/        ← 上下文级决策
```

## 使用术语表里的词汇

输出中出现的领域概念（issue 标题、重构提案、假设、测试名）要用 `GLOSSARY.md` 定义的词，不要漂移到它明确回避的同义词。

需要的概念还不在术语表里，这是一个信号：要么你在发明项目不用的语言（重新考虑），要么确实有空缺（记下来交给 `/domain-modeling`）。

## 冲突的 ADR 要显式指出

如果你的输出与既有 ADR 矛盾，明确写出来而不是默默覆盖：

> _与 ADR-0007（event-sourced orders）矛盾，但值得重开，因为……_
