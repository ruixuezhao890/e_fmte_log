# schema 从声明原文编译期解析，而不是靠外部代码生成

C++17 没有反射，而 eserde 需要字段名、类型名、字段标签、枚举取值。决定：把**整段类型声明文本**作为 `E_FMT_DERIVE` 的首参，宏用 `#decl` 取到字符串，在编译期解析成 schema（`efmt/core/format_derive.hpp:2373` → `eserde/serde.hpp:87` 的 `parse_derived_schema<EFMT_DERIVE_MAX_FIELDS>(decl_text<T>())`）。枚举因为体内的逗号都是顶层逗号，另开 `E_FMT_DERIVE_ENUM` 入口。

## Considered Options

- **手写字段名列表**（`E_FMT_FORMATTER_FIELDS` 路线）：字段名要抄第二遍，改名时会静默漂移；而且拿不到 `[[efmt::arg(...)]]` 标签。
- **外部代码生成器**：违背"无脚本、无生成器"的接入前提，嵌入式工程里没人愿意养一条生成链。
- **聚合体结构化绑定探查**（BOOST.PFR 式）：能拿到字段，但拿不到字段名，也拿不到标签。

## Consequences

- 首参必须是"声明本身"，且**不能有顶层逗号**——`E_FMT_DERIVE(enum class S { idle, busy })` 会被预处理器切成三段；`std::pair<int, int>` 这类要先用 `using` 起别名。
- 字段数上限由 `EFMT_DERIVE_MAX_FIELDS` 定（默认 16，允许 1..32）；超了或声明不合法，只能给 `static_assert` 文本。
- `EFMT_DERIVE_ENABLE_SCHEMA=0` 会让 eserde 直接 `#error`；不用序列化的固件应少复制一个目录，而不是关这个宏。
