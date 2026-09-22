# efmt / elog 沙盒

CLion 里 **File → Open** 选这个 `sandbox` 目录即可（里面有 `CMakeLists.txt`，CLion 会自己 configure 并生成 `cmake-build-*`）。
C++17；`main.cpp` 是一次功能走查，自带 19 条自检（跑完打印 `19/19 checks passed`，全过才返回 0）。

## include 根怎么接的

CMakeLists 里加了三条，前两条和 `tests/run_check.ps1` 用的完全一致，第三条是 ETL 显式依赖：

| 路径 | 提供 |
|---|---|
| `../tests/include` | `<middleware/efmt/...>`、`<middleware/etl/...>` |
| `..` | `<elog/elog.hpp>` |
| `ETL_ROOT`（CMake cache 变量） | `<etl/...>`（ETL 真实头文件**父目录**，即包含 `etl/` 子目录的那一层） |

`tests/include/middleware/efmt` 是指向 `efmt/` 的 junction，`middleware/etl` 指向外部 etl-master 的 `include/etl`。
**别删这两个 junction**；真要删只能用 `cmd /c rmdir <路径>`（不带 `/s`，带 `/s` 会跟着进目标目录把内容删掉）。

**ETL 位置变了**：`cmake -DETL_ROOT=新路径`（CLion：Settings → CMake → CMake options），
或重做 junction（先 `cmd /c rmdir tests\include\middleware\etl`，再 `mklink /J ...`，见 `tests/run_check.ps1`）。
注意 ETL_ROOT 要指到 `etl/` 的**父目录**（如 `etl-master/include`）：ETL 自带 `string.h` 等与系统头同名的头，
把 `etl/` 本身加进 include 路径会遮蔽 `<cstring>` 等系统头（MinGW 实测 include 链崩掉）。

`main.cpp` 末尾有 `elog × ETL 类型` 自检段：`etl::string` / `etl::vector`（含嵌套）/ `etl::optional`
直接格式化，并演示 ETL 类型进 elog 日志（elog 对用户默认打开容器格式，MCU 上打 `etl::vector` 开箱即用）。

## 跑

CLion 右上角选 `sandbox` 目标直接 Run。命令行等价：

```
cmake -S sandbox -B build -G Ninja
cmake --build build
./build/sandbox
```

## 想试的开关

在 CLion 的 **Settings → CMake → CMake options** 里加：

- `-DEFMT_DERIVE_SHOW_TYPE=1` → 推导输出带类型名：`point { x = 3, y = 4 }`（默认不带，Cortex-M4 上省约 1.35 KB Flash）

其它开关直接写在 `main.cpp` 顶部 `#define`（都能用 `-D` 覆盖，定义见 `efmt/core/format_base.hpp`）：

| 宏 | 默认 | 作用 |
|---|---|---|
| `EFMT_ENABLE_FLOAT` | 1 | 0 = 完全不编浮点通道 |
| `EFMT_ENABLE_CONTAINER_FORMAT` | 宿主 1 | 0 = 不格式化 vector/map/tuple |
| `EFMT_ENABLE_DYNAMIC_STRING` | 宿主 1 | 0 = 不支持 std::string 参数 |
| `EFMT_ENABLE_ANSI_STYLES` | 宿主 1 | 颜色转义（`main.cpp` 里已关成 0） |
| `EFMT_DERIVE_STYLE_MULTILINE` | 1 | `{:#}` 多行缩进；关它省 ~15 B，`{:#}` 退化为单行 |
| `ELOG_MAX_RECORD_SIZE` | 384 | 单条日志的记录缓冲 |
| `ELOG_MAX_LOGGERS` | 8 | logger 槽位数 |

## 上手就踩得到的几个点（已实测）

- **bool 打出来是 `1`/`0`**，不是 `true`/`false`（嵌入式省 Flash 的取舍）。
- `E_FMT_DERIVE` 默认**不带类型名**：`{ p = {x=3, y=4}, ts = 9 }`；要 `frame { ... }` 就开上面的开关。
- 四种注册写法可混用但不能对**同一类型**重复注册（会重定义 `efmt_derive_format`）。
- `elog` 直接调 `logger->try_info(...)` 时位置信息是 `<unknown>:0 <unknown>`；要 `文件:行 函数` 前缀就用 `ELOG_INFO(...)` 宏（或 `log_at` 传 `ELOG_SOURCE_LOCATION`）。
- `multi_sink` 按指针保存 `user_data`：用它创建的 logger 不能活得比这个 `multi_sink` 对象长。
- 等级过滤掉的消息仍返回成功（`void_result` 有值），不报错。
