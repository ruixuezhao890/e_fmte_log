# 沙盒命令台：新手 10 分钟上手

> 面向第一次打开本仓库的人。目标：**10 分钟内亲手敲出第一条命令、看懂每段输出，并知道"我想加一条自己的命令"该改哪 4 个地方。**
> 沙盒的全部代码只有一个文件：[`sandbox/main.cpp`](../sandbox/main.cpp)（约 380 行），却是
> **efmt / elog / eserde / ecli / matchit 五层的可运行总览**。
> 库本身的系统用法看 [EFMT-使用手册.md](EFMT-使用手册.md)；本文只讲"跑起来 → 敲起来 → 改起来"。

---

## 0. 一句话：这个沙盒是什么

一个普通 C++17 控制台程序：敲一行、回车、看一段输出。

```
> num 42
十进制   42
十六进制 0x2a
二进制   0b101010
补零     00000042
左对齐   [42      ]
右对齐   [      42]
居中     [   42   ]
```

**同一个命令表，三种吃法**：键盘 / 管道喂进来的一行文本、宿主 argv（`./sandbox num 42`）、
脚本冒烟（`--check`）。所以你在沙盒里敲通的东西，搬到串口 / 蓝牙上就是同一份代码（见第 9 节）。

| 命令 | 演示的是哪一层能力 |
|---|---|
| `num` / `fmt` | efmt：格式化与格式规范（进制、补零、对齐、截断） |
| `me` | efmt：自定义类型三种注册写法（AUTO / FIELDS / DERIVE） |
| `json` / `cbor` | eserde：序列化写→读一圈（同一个 `person`，两种格式） |
| `log` | elog：分级日志、运行期换级别、`文件:行 函数` 前缀 |
| `level` | matchit：处理函数里直接用 `match` 表达式 + 命令名模式段 `:n` |
| `net` / `net set` | ecli 命令表：两段式子命令、最长前缀优先、捕获注入 |
| `echo` / `args` | ecli 命令行解析：开关 / 短选项 / 取值 / 可重复 / 位置参数 |
| `help` / `-V` | ecli 内置帮助与版本 |

---

## 1. 跑起来（三条路，任选一条）

### 路线 A：CLion（最省事）

1. CLion → **File → Open** → 选 `sandbox` 目录（里面有 `CMakeLists.txt`，CLion 自己 configure）；
2. 右上角目标选 `sandbox` → **Run**。默认就是进命令台。

沙盒**只差一个外部依赖 ETL**（写日志的 elog 要用）。CMake 会自己按这个顺序找，找不到就在 configure
期报错（不会拖到编译）：

1. `-DETL_ROOT=<路径>` （CLion：**Settings → CMake → CMake options** 填这个）
2. 环境变量 `EFMT_ETL_INCLUDE=<路径>`
3. 相邻目录 `../etl-master/include`、`../etl/include`、`third_party/etl/include`

路径写 `etl/` 的**父目录**（如 `etl-master/include`），或**直接指 `etl/` 本身**，都行；没装就去
<https://github.com/ETLCPP/etl> 拉一份。
`middleware/efmt` 与 `middleware/etl` 这两层 include 形状由 CMake 在**构建目录**里自建（junction / symlink），
不用手工搭、也不用管 `tests/include`（那是 `run_check.ps1` 的地盘）。

### 路线 B：命令行（CMake + Ninja，照抄即可）

```bash
cd <仓库根>
cmake -S sandbox -B build -G Ninja                       # ETL 没在相邻目录时补：-DETL_ROOT=<...>/etl-master/include
cmake --build build
./build/sandbox                                          # Windows: .\build\sandbox.exe
```

前提：C++17 编译器 + CMake ≥ 3.20（实测 GCC 15.1 / MSVC 均可）。**clone 下来就能编**，没有别的前置。

### 路线 C：先跑仓库自带检查（连 sandbox 冒烟一起）

ETL 不在 `tests/include/middleware/etl` 时，用 `-EtlInclude` 指给它（注意这里指 `etl/` **本身**）：

```powershell
pwsh tests/run_check.ps1 -EtlInclude <...>/etl-master/include/etl   # 全量：宿主 + 嵌入式 + ETL + 编译期反例
pwsh tests/run_check.ps1                                            # ETL 已在位时不用带参数
```

> ⚠️ `run_check.ps1` 会在 `tests/include/` 下建两个 junction（`.gitignore` 忽略，只在本机存在）。
> 要删只能用 `cmd /c rmdir <路径>`（**不带** `/s`；带 `/s` 会跟着进目标目录，把内容删掉）。

---

## 2. 30 秒：进命令台先敲这三条

开局自己会把命令表和几条例子打出来（下面是真实输出）：

```
sandbox —— 敲一条命令，看一段输出（help 看全部，quit 退出）
  num 42         数字：十进制 / 0x / 0b / 补零 / 对齐
  fmt 你好        排版：左中右对齐 / 截断
  me             自定义类型的三种注册写法
  json / cbor    同一个人，两种格式各走一圈
  level 5        matchit 的分支（0 / 1..9 / 其它）
  net set mynet  两段式子命令（net 单独敲 = 概览，最长前缀优先）
  log warn       日志：换级别，看哪几行被过滤
  echo --upper hi    开关 + 位置参数
  args -v -o a.bin --level 7 in.txt    完整选项集（还能 --tag a --tag b）

故意敲错也有东西看：num（缺参数）、num abc（值不对）、net set（子命令少一段）、nope（未知命令）
commands:
  num             数字格式化
  fmt             文本排版
  ...
  help [command]  show this help

> 
```

| 敲这个 | 你会看到 |
|---|---|
| `help` | 命令表（就是上面那份 `commands:` 列表）|
| `num 42` | 一个整数的七种看法：十进制 / 十六进制 / 二进制 / 补零 / 左中右对齐 |
| `log warn` | 日志换级别：下面五行里只有 `>= warn` 的出来 |
| `quit` / `exit` / Ctrl+Z 回车 | 退出 |

---

## 3. 命令台里敲什么（全表 + 真实输出）

```
> fmt 你好
原样     [你好]
左对齐   [你好    ]
右对齐   [    你好]
居中     [  你好  ]
截断     [你]（精度按字节，一个汉字 3 字节）
```

```
> me
imu     imu(1.5, 2.5, 3.5)     （AUTO：零声明）
point   {x=3, y=4}       （FIELDS：只列字段名）
person  { age = 18, name = xiaoming, state = idle }     （DERIVE：声明即推导）
同一个人换成 {:#} 就是多行：
{
  age = 18,
  name = xiaoming,
  state = idle
}
```

```
> json
写：{"age":18,"name":"xiaoming","state":"idle"}
读回来：ok

> cbor
cbor 27 B（同样内容 json 43 B）
读回来：ok
```

```
> level 5
1..9：开

> net
net: 概览 —— 子命令就是「名字带空格」的另一种写法
  试 net set mynet（两段式子命令，mynet 被模式段 :ssid 捕获）

> net set mynet
ssid -> mynet（这段数字/名字是命令名模式 :ssid 捕获的）

> echo --upper hi
HI

> args -v -o a.bin --level 7 --tag net in.txt
verbose=1  out=a.bin  level=7  input=in.txt  tags=[net]

> log warn
当前级别 warn：下面五行只有 >= 它的会出来（试试 log warn / log off / log trace）
[warn] [main.cpp:205 run_log] warn 行
[error] [main.cpp:206 run_log] error 行
```

| 敲这个 | 看什么 |
|---|---|
| `num 0x2a` / `num -7` | 同一个整数换几种进制与对齐（取值支持 `0x` / `0b` / 负号）|
| `log trace` / `log off` | 换级别再敲一次 `log`：被过滤的行**直接消失**（不输出也不报错）|
| `level 0` / `level 99` | 同一个 `match` 表达式的另外两个分支 |
| `help args` / `args -h` | 某条命令的 usage + 选项表（同一份内容，两个入口）|
| `-V` | 版本行（`sandbox 0.1.0-sandbox`），版本号由宿主自己给，库不猜 |

---

## 4. 故意敲错：报错长这样

```
> num
error: missing required option '<n>'

usage: num <n>

> num abc
error: invalid value 'abc' for '<n>'

usage: num <n>

> net set
error: too many arguments: 'set'

usage: net

> nope
error: unknown command 'nope'

commands:
  num             数字格式化
  ...
```

四条分别对应：**缺必填 / 值类型不对 / 子命令少一段（被 `net` 兜底接走）/ 未知命令**。
报错都自带 usage；`ecli::dispatch` 还会把错误码**返回**给调用方（沙盒的 `--check` 就是靠它判成败）。
库里的解析器**从不 exit、从不抛异常**。

---

## 5. 不进交互的两种跑法

### argv（宿主工具那条路）

```bash
./sandbox num 0x2a                       # 同一个命令表、同一个解析器
./sandbox help args                      # 打帮助 → 退出码 0
./sandbox nope                           # 未知命令 → 退出码 1
```

```
$ ./sandbox help args
完整选项集

usage: args [options] [input]

options:
  -v, --verbose        详细输出
  -o, --out <value>    输出文件
  -l, --level <value>  0..9
  --tag <value>        可重复：--tag a --tag b
  [input]              输入文件
  -h, --help           show this help
```

### 管道 / 脚本

stdin 的字节是**逐字节**喂进 `ecli::line_reader` 的 —— 跟串口 / 蓝牙收到字节、攒够一行再解析
完全是同一条路径，所以管道脚本能直接当"串口模拟器"用：

```bash
printf 'num 42\nnet set mynet\nquit\n' | ./sandbox --repl
```

```powershell
Get-Content cmds.txt | .\sandbox.exe --repl    # PowerShell / 文件管道前面的 UTF-8 BOM 会被自动跳过
.\sandbox.exe --check                          # 冒烟：16 条命令跑一遍，只报"几条异常"（run_check.ps1 用这条）
```

---

## 6. 想加一条自己的命令：只改 4 个地方

`main.cpp` 分成五段，加命令只碰前三段 + 重编译：

```cpp
// ① 参数类型：还是 E_FMT_DERIVE，字段名一个都不用写（对标 clap 的 #[derive(Parser)]）
E_FMT_DERIVE(struct blink_args {
  [[efmt::arg(pos = "1", required, help = "闪烁次数")]] int times = 1;
  [[efmt::arg(short, long, help = "快点闪")]]           bool fast = false;
}, Cli);

// ② 处理函数：答复走 reply（谁问的回给谁），格式化走 efmt 的 format_to
static void run_blink(const blink_args &a, ecli::reply out) {
  replyf(out, "blink {} 次，fast={}\n", a.times, a.fast);   // replyf 是沙盒里的 4 行小助手
}

// ③ 命令表里加一行（名字带空格 = 子命令，自动变成两级）
static constexpr ecli::command kCommands[] = {
    // ...原有那些...
    {"blink", "闪烁 LED", command_of<blink_args, run_blink>()},
};
```

④ 重新 build，敲 `blink 3 --fast`，再敲 `blink -h` 看自动生成的帮助。

四个"抄现成"的坑：

- **无参命令别用空结构体**：`E_FMT_DERIVE` 至少要有一个字段 —— 抄 `no_args`（一个 `[[efmt::arg(skip)]]` 占位字段）；
- **由命令名模式喂值的字段要标 `skip`**：否则它会同时进选项表（见下面 `net_set_args`）；
- **别用裸 `const char*` 接"一行文本"里的值**：argv 里每个 token 自带结尾 `0`，但一行的 token 只是视图；
  定长字符串用 `etl::string<N>`（装不下如实报 `value_too_long`，不静默截断）；
- **帮助是中文、命令又多时**要调大 `ECLI_REPLY_BUFFER`（默认 384 B，沙盒里设成 768）——
  超了会如实追加 `...(truncated)`，不静默丢。

### 子命令与命令名模式段（沙盒里两条活的例子）

```cpp
{"net",           "网络概览（子命令的兜底）", command_of<no_args, run_net>()},
{"net set :ssid", "设置 SSID",               command_of<net_set_args, run_net_set>()},
{"level :n",      "matchit 分支",            command_of<level_args, run_level>()},
```

- 名字带空格 = 子命令；匹配**最长前缀优先** → `net set mynet` 归 `net set :ssid`，裸 `net` 归 `net`；
- `:ssid` 吃掉一个 token，并**按名字注入**参数结构体的同名字段（所以那个字段标 `skip`）；
- `*rest` = 余下全收（注入容器字段，只能放末尾），例如 `"log *rest"`；
- 段数不够时（`net set`）会落到更短的那条并报 `too many arguments` —— 想让半截输入也有友好提示，
  就在表里再补一条 `{"net set", ...}`；
- `help` / `-h` / `--help` / `?` 是**保留词**，命令表里别用。

---

## 7. 顺手看一眼 matchit

`level` 这条命令抓到数字后，处理函数里直接用 matchit 的分支表达式：

```cpp
static void run_level(const level_args &a, ecli::reply out) {
  using namespace matchit;
  const char *msg = match(a.n)(
      pattern | 0                    = "0：关\n",
      pattern | and_(_ >= 1, _ <= 9) = "1..9：开\n",
      pattern | _                    = "超出 0..9（试试 level 0 / level 5 / level 99）\n");
  out.put_lit(msg);
}
```

`matchit/` 是第三方**冻结副本**（Apache-2.0，改动见 [matchit/PATCHES.md](../matchit/PATCHES.md)）：
我们只把"一个 token 与一个段模式"的判定交给它的 extractor，不自己写模式解释器。
不想带这层依赖就 `-DECLI_ENABLE_PATTERN_COMMANDS=0`（`:name` / `*name` 当字面量，省约 0.6 KB）。

---

## 8. 三条输出通道，和"一次绑定，多处使用"

本仓库的规矩：**文本格式化一律 efmt，文本输出一律 elog**。沙盒里分成三条：

| 内容 | 走哪条 | 为什么 |
|---|---|---|
| 日志行（`log` 那五行、`--check` 的总结）| `ELOG_INFO` / `ELOG_WARN` / `ELOG_ERROR` | 要级别、要 `文件:行 函数` 前缀、要能运行期过滤 |
| 命令答复 / 界面文字（`num` 的输出、`help`、报错）| `ecli::reply`（沙盒用 `reply_to_default_logger()`）| **原样字节**：usage / help 是多行整块文本，日志语义会加前缀、补换行、超 384 B 整行丢弃 |
| 交互提示符 `"> "` | 原样字节 | elog 每行必加前缀和换行，做不了提示符 |

沙盒里**通道只绑一次**，其余地方全是取用：

```cpp
// main()：全局唯一一次"绑定"（sink = 你的 (data, size) 写函数）
e_log::logger *const log = e_log::create_logger("sandbox", e_log::stdout_sink(), e_log::level::debug);
// 命令台 / argv 两条路：复用上面那条通道，不再绑第二次
const ecli::reply out = ecli::reply_to_default_logger();
```

换物理出口（串口 / 蓝牙 / 屏幕）只改 `create_logger` 那**一个**参数，日志与命令答复一起跟着走；
绑成 `multi_sink` 时连"扇出到多路"也一并白拿。完整约定见手册 **13.9**。

---

## 9. 搬到真机：把命令台换成串口

好消息：**命令表、参数类型、处理函数一行都不用改** —— 它们不认识 argv，只认识 token。

1. **输入**：把 `std::getchar()` 换成"串口收到一个字节"，同一份 `ecli::line_reader<128>` 攒够一行就走；
2. **输出**：把 `create_logger` 的 sink 换成 `e_log::make_sink(&uart_write)`，`reply_to_default_logger()` 自动跟着走；
3. **想更省**：`ecli::reply_to<uart_write>()` 直接用裸写函数（连 elog 那层都不要）。

```cpp
static bool uart_write(const char *data, std::size_t size, void *) {
  HAL_UART_Transmit(&huart1, (const uint8_t *)data, (uint16_t)size, 100);
  return true;                       // 与 efmt 输出处理器同一形状
}

void boot() {
  e_log::logger *app = e_log::create_logger("app", e_log::make_sink(&uart_write), e_log::level::debug);
  // ...命令表照抄沙盒...
}

// 串口中断 / 任务里：
char scratch[ECLI_MAX_LINE];
ecli::line_reader<128> rx;
// 每收到一个字节：
if (rx.put(static_cast<char>(c))) {                    // true = 一行到齐
  ecli::dispatch(kCommands, rx.line(), scratch, sizeof(scratch),
                 ecli::reply_to_default_logger());     // 谁问的回给谁
  rx.clear();
}
```

---

## 10. 新手最容易踩的 8 个点（都实测过）

1. **bool 打出来是 `1`/`0`**，不是 `true`/`false`（嵌入式省 Flash 的取舍）。
2. `E_FMT_DERIVE` 默认**不带类型名**：`{ p = {x=3, y=4}, ts = 9 }`；要 `frame { ... }` 就 `-DEFMT_DERIVE_SHOW_TYPE=1`。
3. **同一类型不能重复注册**（会重定义 `efmt_derive_format`）；AUTO / FIELDS / DERIVE 三种写法可以混用。
4. **字段类型名里不能有顶层逗号**：`etl::vector<int, 8>` 先 `using` 一个别名，或改用 `std::vector<int>`。
5. `elog` 直接调 `logger->info(...)` 时位置信息是 `<unknown>:0 <unknown>`；要 `文件:行 函数` 前缀就用 `ELOG_INFO(...)` 宏。
6. `ECLI_REPLY_BUFFER` 是**栈**上的一块（默认 384 B）：命令多、帮助长就要调大，否则帮助尾部会 `...(truncated)`。
7. `multi_sink` 按指针保存 `user_data`：用它建的 logger 不能活得比这个 `multi_sink` 对象长。
8. 被级别过滤掉的消息**直接返回**：不输出、也不报错（`set_level` 之后就是这个行为）。

更细的沙盒笔记（include 拓扑、宏开关总表）在 [`sandbox/README.md`](../sandbox/README.md)。

---

## 11. 相关文档

| 文档 | 什么时候看它 |
|---|---|
| [EFMT-使用手册.md](EFMT-使用手册.md) | 完整手册（18 章）：格式化 / 日志 / JSON·CBOR / 命令行 / 裁剪宏 / Flash 实测 |
| 手册 5.10 / 5.11 / 13.9 | ecli 解析器、命令表与模式段、输出通道"一次绑定"的正式说明 |
| [ECLI-命令行解析-方案.md](ECLI-命令行解析-方案.md) | ecli 的拍板结论、交付物与边界 |
| [ECLI-与clap的差距清单.md](ECLI-与clap的差距清单.md) | 和 Rust clap 一条条对照（已对齐 / 差距 / 故意不做）|
| [`sandbox/README.md`](../sandbox/README.md) | 沙盒的 include 拓扑、宏开关总表、实测坑 |
| [`matchit/README.md`](../matchit/README.md) · [`PATCHES.md`](../matchit/PATCHES.md) | 第三方冻结副本与我们对它做的最小改动 |
| [backup/](backup/) | 每一步大改前的 git bundle + `*-回滚说明.md` |
