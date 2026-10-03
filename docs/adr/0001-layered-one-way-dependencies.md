# 分层单向依赖：能力标签的含义由定义它的那一层给

`efmt/` 本体不认识 elog、eserde、ecli；这三层只单向依赖本体，不 include 即零开销。
`E_FMT_DERIVE(声明, Debug, Serialize)` 标签位上的能力标签，本体只把它们原样塞进 `caps_list`（`efmt/core/format_derive.hpp:36-38`），不解释含义——`Debug` 留在本体，`Serialize` / `Deserialize` 由 eserde 定义（`eserde/serde.hpp:43`），`Cli` 由 ecli 定义（`ecli/cli.hpp:79`）。能力查询用 ADL 挂钩 `efmt_derive_caps(type_tag<T>{})` 回到定义层。

## Considered Options

- **把序列化 / 命令行能力直接做进本体**：否定。不用这两个能力的固件也得背上这份代码，"按需复制目录"的接法就没了。
- **让本体持有一张能力白名单**：否定。每加一个格式（toml、二进制协议……）都要改本体，本体会变成所有人的公共瓶颈。

## Consequences

- 本体里的 `Debug` 是"默认能力"；没写标签的类型也带它。
- 能力门禁的报错文本由定义层提供，所以同一个缺失在不同层给出不同提示（serialize 说补 `Serialize`，cli 说补 `Cli`）。
- 查"某类型有没有某能力"必须经由基座（`has_cap_v`），本体自己答不了。
