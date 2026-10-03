/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : efmt / elog / ecli 的手玩沙盒 —— 敲一条命令，看一段输出
 * @attention      : 三种跑法
 *                     ./sandbox               进命令台（默认）：一行一条命令、回车执行
 *                     ./sandbox num 42        直接跑一条 —— 宿主 argv 那条路，同一个命令表
 *                     ./sandbox --check       run_check.ps1 的冒烟跑（不进交互、不会等人）
 *
 *                   输出规矩（本仓库）：文本格式化一律 efmt，文本输出一律 elog。
 *                     * 命令的答复 / 界面文字 → reply 通道（原样字节，谁问的回给谁）
 *                     * 日志                  → ELOG_*（带级别和来源；敲 `log warn` 现场看过滤）
 *                     * 交互提示符            → 原样字节（elog 每行必加前缀和换行，做不了提示符）
 *
 *                   include 根（CMakeLists.txt 与 tests/run_check.ps1 一致）：
 *                     <repo>/tests/include → <middleware/efmt/...> <middleware/etl/...>
 *                     <repo>               → <elog/elog.hpp> <ecli/...> <matchit/...>
 ******************************************************************************
 */

#define EFMT_ENABLE_ANSI_STYLES 0   // 关掉颜色转义，终端 / CLion 输出干净
// 命令表 11 条、帮助又是中文，默认 384 B 的列表缓冲会被截断 —— 桌面上不抠这点栈
#define ECLI_REPLY_BUFFER 768

#include <middleware/efmt/core/format.hpp>
#include <elog/elog.hpp>            // 日志；顺带把 ETL 类型接进格式化（etl::string 当文本打）
#include <eserde/json.hpp>
#include <eserde/cbor.hpp>
#include <ecli/command.hpp>         // 旧风格：命令表 / 子命令 / 命令名模式段
#include <ecli/cli.hpp>             // 新风格：嵌套 struct 子命令（parse / help_string / error_string）
#include <ecli/elog_reply.hpp>      // 答复的出口 = elog 的 sink（可选层）
#include <matchit/matchit.h>        // 第三方（matchit/ 冻结副本）：这里直接用它的 match 表达式

#include <middleware/etl/string.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace eserde;
using namespace ecli;

// ============================================================================
// 1. 自定义类型：三种注册写法，各一行
// ============================================================================
struct imu {                      // ① AUTO：零声明，按字段声明顺序打
  float x, y, z;
};
E_FMT_FORMATTER_AUTO(imu);

struct point {                    // ② FIELDS：只列字段名
  int x, y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);

E_FMT_DERIVE_ENUM(enum class state { idle, sampling, fault });

E_FMT_DERIVE(struct person {      // ③ 声明即推导：字段名一个都不用写
  int age=18;
  std::string name="hello";
  state state=state::idle;
}, Debug, Serialize, Deserialize);   //    能力标签：写要 Serialize、读要 Deserialize

static const person kPerson{18, "xiaoming", state::idle};

// ============================================================================
// 2. 每条命令的参数类型：照样是 E_FMT_DERIVE，字段名一个都不用写
// ============================================================================
// 无参命令共用一个占位类型：E_FMT_DERIVE 至少要有一个字段，
// 而 skip = 不进命令行（也不会出现在帮助里）。
E_FMT_DERIVE(struct no_args {
  [[efmt::arg(skip)]] int unused = 0;
}, Cli);

E_FMT_DERIVE(struct num_args {
  [[efmt::arg(pos = "1", required, help = "整数，比如 42 / 0x2a / -7")]] int n = 0;
}, Cli);

E_FMT_DERIVE(struct fmt_args {
  [[efmt::arg(pos = "1", required, help = "文本，比如 你好 / hello")]]

    etl::string<16> text;
}, Cli);

E_FMT_DERIVE_ENUM(enum class test {
  hex,
  h
});

E_FMT_DERIVE(struct echo_args {
  [[efmt::arg(long = "upper", help = "转成大写")]]           bool upper = false;
  [[efmt::arg(pos = "1", required, help = "要回显的文本")]]  etl::string<24> text;
}, Cli);

E_FMT_DERIVE(struct log_args {
  [[efmt::arg(pos = "1", help = "trace/debug/info/warn/error/off")]] etl::string<8> level;
}, Cli);

E_FMT_DERIVE(struct level_args {
  [[efmt::arg(skip)]] int n = -1;   // 数字由命令名模式 "level :n" 捕获，不用写标签
}, Cli,Debug);

// 两段式子命令 "net set :ssid"：ssid 同样由模式段捕获进这个 skip 字段
E_FMT_DERIVE(struct net_set_args {
  [[efmt::arg(skip)]] etl::string<16> ssid;
}, Cli);

E_FMT_DERIVE(struct args_args {     // 宿主工具那种完整选项集
  [[efmt::arg(short, long, help = "详细输出")]]                  bool verbose = false;
  // 注意别用裸 const char*：它零拷贝指向输入缓冲（argv 里每个 token 自带结尾 0，
  // 而"一行文本"里的 token 只是视图）—— 定长字符串会老实报"太长"，不会读出界
  [[efmt::arg(short = "o", long = "out", help = "输出文件")]]     etl::string<24> out;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]]       int level = 3;
  [[efmt::arg(long = "tag", help = "可重复：--tag a --tag b")]]   std::vector<std::string> tags;
  [[efmt::arg(pos = "1", help = "输入文件")]]                     etl::string<24> input;
}, Cli);

// ============================================================================
// 2.5 新风格：嵌套 struct 子命令（和 clap derive 同构）
//    注册表 = 零字段 struct，里面每条「嵌套 struct」就是一条子命令；
//    命令名 = struct 名首字母转小写（Add → add）；帮助写在嵌套 struct 头部
// ============================================================================
E_FMT_DERIVE(struct Commands {
  [[efmt::arg(help = "add a file")]] struct Add {
    [[efmt::arg(short = "f", long = "file", required, help = "file name")]] etl::string<16> file;
  };
  [[efmt::arg(help = "delete a file")]] struct Del {
    [[efmt::arg(short = "f", long = "file", required, help = "file name")]] etl::string<16> file;
  };
  [[efmt::arg(help = "find by tag")]] struct Find {
    [[efmt::arg(short = "t", long = "tag", help = "tag")]] etl::string<16> tag;
  };
}, Subcommand, Debug);

// 解析结构体：顶层选项 + 一个 command 槽；variant 首备选 = 注册表 =「没给子命令」
using CmdArgs = std::variant<Commands, Commands::Add, Commands::Del, Commands::Find>;
E_FMT_DERIVE(struct sub_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
  [[efmt::arg(command)]] CmdArgs cmd;  // 必须是变体（首备选=注册表=未选哨兵）；写注册表名 Commands 编译不过（get 无匹配）
}, Parser, Debug);

// ============================================================================
// 3. 命令实现：格式化用 efmt（format_to），答复走 reply，日志走 ELOG_*
// ============================================================================
// 一行串起"efmt 格式化 → 回给提问的那一路"
template <typename... A>
static void replyf(ecli::reply out, std::string_view fmt, const A &...args) {
  char buf[384];
  const std::size_t n = e_fmt::format_to(buf, sizeof(buf), fmt, args...);   // snprintf 语义：返回完整长度
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
  if (n >= sizeof(buf)) out.put_lit("...(truncated)\n");   // 不静默截断，跟库里的写法一致
}

static void run_num(const num_args &a, ecli::reply out) {
  replyf(out,
         "十进制   {0}\n十六进制 {0:#x}\n二进制   {0:#b}\n补零     {0:08}\n"
         "左对齐   [{0:<8}]\n右对齐   [{0:>8}]\n居中     [{0:^8}]\n",
         a.n);
}

static void run_fmt(const fmt_args &a, ecli::reply out) {
  replyf(out,
         "原样     [{}]\n左对齐   [{:<10}]\n右对齐   [{:>10}]\n居中     [{:^10}]\n"
         "截断     [{:.3}]（精度按字节，一个汉字 3 字节）\n",
         a.text, a.text, a.text, a.text, a.text);
}

static void run_me(const no_args &, ecli::reply out) {
  replyf(out,
         "imu     {}     （AUTO：零声明）\n"
         "point   {}       （FIELDS：只列字段名）\n"
         "person  {}     （DERIVE：声明即推导）\n"
         "同一个人换成 {{:#}} 就是多行：\n{:#}\n",
         imu{1.5f, 2.5f, 3.5f}, point{3, 4}, kPerson, kPerson);
}

static void run_json(const no_args &, ecli::reply out) {
  char buf[192];
  const std::size_t n = eserde::json::write_to(buf, sizeof(buf), kPerson);
  if (n >= sizeof(buf)) {
    out.put_lit("json 缓冲区不够大\n");
    return;
  }
  person back{};
  const eserde::json::error e = eserde::json::read_from(std::string_view(buf, n), back);
  replyf(out, "写：{}\n读回来：{}\n", std::string_view(buf, n),
         e == eserde::json::error::ok ? "ok" : eserde::json::error_name(e));
}

static void run_cbor(const no_args &, ecli::reply out) {
  unsigned char buf[128];
  const std::size_t n = eserde::cbor::write_to(buf, sizeof(buf), kPerson);
  if (n >= sizeof(buf)) {
    out.put_lit("cbor 缓冲区不够大\n");
    return;
  }
  person back{};
  const eserde::cbor::error e = eserde::cbor::read_from(buf, n, back);
  char jbuf[192];
  const std::size_t jn = eserde::json::write_to(jbuf, sizeof(jbuf), kPerson);
  replyf(out, "cbor {} B（同样内容 json {} B）\n读回来：{}\n", n, jn < sizeof(jbuf) ? jn : 0,
         e == eserde::cbor::error::ok ? "ok" : eserde::cbor::error_name(e));
}

static void run_log(const log_args &a, ecli::reply out) {
  e_log::logger *const lg = e_log::default_logger();
  if (lg == nullptr) {
    out.put_lit("没有默认 logger\n");
    return;
  }

  static const struct { const char *name; e_log::level value; } kLevels[] = {
      {"trace", e_log::level::trace}, {"debug", e_log::level::debug},
      {"info", e_log::level::info},   {"warn", e_log::level::warn},
      {"error", e_log::level::error}, {"off", e_log::level::off},
  };

  if (!a.level.empty()) {
    const e_log::level *found = nullptr;
    for (const auto &lv : kLevels) {
      if (a.level == lv.name) found = &lv.value;
    }
    if (found == nullptr) {
      replyf(out, "不认识的级别 '{}'：可选 trace / debug / info / warn / error / off\n", a.level);
      return;
    }
    lg->set_level(*found);
  }

  replyf(out, "当前级别 {}：下面五行只有 >= 它的会出来（试试 log warn / log off / log trace）\n",
         e_log::to_string(lg->current_level()));
  ELOG_TRACE("trace 行");
  ELOG_DEBUG("debug 行");
  ELOG_INFO("info 行");
  ELOG_WARN("warn 行");
  ELOG_ERROR("error 行");
}

// 命令名模式 "level :n" 把数字捕获进 n，处理函数里直接用 matchit 的 match 表达式分支
static void run_level(const level_args &a, ecli::reply out) {
  using namespace matchit;
  const char *msg = match(a.n)(
      // clang-format off
      pattern | 0                    = "0：关\n",
      pattern | and_(_ >= 1, _ <= 9) = "1..9：开\n",
      pattern | _                    = "超出 0..9（试试 level 0 / level 5 / level 99）\n"
      // clang-format on
  );
  out.put_lit(msg);
}

// 光敲 net = 概览；敲 net set xxx 会被【更长的那条】命令接走（最长前缀优先）
static void run_net(const no_args &, ecli::reply out) {
  out.put_lit("net: 概览 —— 子命令就是「名字带空格」的另一种写法\n"
              "  试 net set mynet（两段式子命令，mynet 被模式段 :ssid 捕获）\n");
}

static void run_net_set(const net_set_args &a, ecli::reply out) {
  replyf(out, "ssid -> {}（这段数字/名字是命令名模式 :ssid 捕获的）\n", a.ssid);
}

static void run_echo(const echo_args &a, ecli::reply out) {
  etl::string<24> t = a.text;
  if (a.upper) {
    for (std::size_t i = 0; i < t.size(); ++i) {
      const char c = t[i];
      if (c >= 'a' && c <= 'z') t[i] = static_cast<char>(c - 'a' + 'A');
    }
  }
  replyf(out, "{}\n", t);
}

static void run_args(const args_args &a, ecli::reply out) {
  replyf(out, "verbose={}  out={}  level={}  input={}  tags={}\n", a.verbose,
         a.out.empty() ? "(没给)" : a.out.c_str(), a.level,
         a.input.empty() ? "(没给)" : a.input.c_str(), a.tags);
}

// 新风格消费：cmd 是 variant，一臂一条子命令 —— matchit 的 match 和 clap 的 match 同构。
// 注意分支 lambda 必须可【无参】调用（matchit 的约束）；字段在分支内用 std::get
// 取 —— 能走到这个分支，variant 里就是这个类型。别用 Id<T> 接捕获值（会悬垂，
// 见 matchit/README.md 的坑 1）
static void run_sub(const sub_args &a, ecli::reply out) {
  using namespace matchit;
  const int hit = match(a.cmd)(
      pattern | as<Commands::Add>(_) = [&] {
        replyf(out, "add: file = {}\n", std::get<Commands::Add>(a.cmd).file);
        return 1;
      },
      pattern | as<Commands::Del>(_) = [&] {
        replyf(out, "del: file = {}\n", std::get<Commands::Del>(a.cmd).file);
        return 2;
      },
      pattern | as<Commands::Find>(_) = [&] {
        replyf(out, "find: tag = {}\n", std::get<Commands::Find>(a.cmd).tag);
        return 3;
      },
      pattern | _ = [&] {
        out.put_lit("（没给子命令）\n");
        return 0;
      });
  (void)hit;
}

// ============================================================================
// 4. 命令表（旧风格）：名字带空格就是子命令
//    注意：add / del / find 不进这张表 —— 它们由 2.5 的新风格 Parser 接管，
//    分发见 run_line()：新风格优先，落空回落到这张表（两套并存）
// ============================================================================
static constexpr ecli::command kCommands[] = {
    {"num", "数字格式化", command_of<num_args, run_num>(), help_of<num_args>()},
    {"fmt", "文本排版", command_of<fmt_args, run_fmt>(), help_of<fmt_args>()},
    {"me", "自定义类型三种写法", command_of<no_args, run_me>(), help_of<no_args>()},
    {"json", "JSON 写→读", command_of<no_args, run_json>(), help_of<no_args>()},
    {"cbor", "CBOR 写→读", command_of<no_args, run_cbor>(), help_of<no_args>()},
    {"log", "日志（可换级别）", command_of<log_args, run_log>(), help_of<log_args>()},
    {"level :n", "matchit 分支", command_of<level_args, run_level>(), help_of<level_args>()},
    {"net", "网络概览（子命令的兜底）", command_of<no_args, run_net>(), help_of<no_args>()},
    {"net set :ssid", "设置 SSID", command_of<net_set_args, run_net_set>(), help_of<net_set_args>()},
    {"echo", "回显文本", command_of<echo_args, run_echo>(), help_of<echo_args>()},
    {"args", "完整选项集", command_of<args_args, run_args>(), help_of<args_args>()},
};

static constexpr const char *kVersion = "0.1.0-sandbox";

// ============================================================================
// 5. 分发：新风格优先，落空回落到旧命令表
//    "help" 归旧表（列全部命令）；-h / --help / -V / --version 归新风格子命令层
// ============================================================================
static ecli::error run_line(std::string_view line, char *scratch, const ecli::reply out) {
  if (line == "help" || line.compare(0, 5, "help ") == 0) {
    const ecli::error e = ecli::dispatch(kCommands, line, scratch, ECLI_MAX_LINE, out);
    if (e == ecli::error::help_requested || e == ecli::error::ok) {
      out.put_lit("\nadd / del / find —— 新风格：嵌套 struct 子命令（add -h 看它自己的帮助）\n");
    }
    return e;
  }
  sub_args a{};
  ecli::error_info info{};
  const ecli::error es = ecli::parse(line, a, scratch, ECLI_MAX_LINE, &info);
  if (es == ecli::error::ok) {
    if (a.cmd.index() == 0) {
      out.put_lit("（没给子命令，试试 add -f a.txt / del -f b.txt / find -t net）\n");
      return ecli::error::ok;
    }
    run_sub(a, out);
    return ecli::error::ok;
  }
  if (es == ecli::error::help_requested || es == ecli::error::version_requested) {
    if (es == ecli::error::help_requested) {
      out.put(ecli::help_string<sub_args>("sandbox", "嵌套 struct 子命令"));
    } else {
      out.put(ecli::version_string("sandbox", kVersion));
    }
    return es;
  }
  if (es != ecli::error::unknown_command) {
    out.put(ecli::error_string<sub_args>("sandbox", es, info));
    return es;
  }
  return ecli::dispatch(kCommands, line, scratch, ECLI_MAX_LINE, out);
}

// argv 那条路：同一套分流（token 直接指向 argv，零拷贝）
static ecli::error run_line_argv(int argc, char **argv, const ecli::reply out) {
  if (argc > 1 && std::string_view(argv[1]) == "help") {
    const ecli::error e = ecli::dispatch(kCommands, argc, argv, out);
    if (e == ecli::error::help_requested || e == ecli::error::ok) {
      out.put_lit("\nadd / del / find —— 新风格：嵌套 struct 子命令（add -h 看它自己的帮助）\n");
    }
    return e;
  }
  sub_args a{};
  ecli::error_info info{};
  const ecli::error es = ecli::parse(argc, argv, a, &info);
  if (es == ecli::error::ok) {
    if (a.cmd.index() == 0) {
      out.put_lit("（没给子命令，试试 add -f a.txt / del -f b.txt / find -t net）\n");
      return ecli::error::ok;
    }
    run_sub(a, out);
    return ecli::error::ok;
  }
  if (es == ecli::error::help_requested || es == ecli::error::version_requested) {
    if (es == ecli::error::help_requested) {
      out.put(ecli::help_string<sub_args>("sandbox", "嵌套 struct 子命令"));
    } else {
      out.put(ecli::version_string("sandbox", kVersion));
    }
    return es;
  }
  if (es != ecli::error::unknown_command) {
    out.put(ecli::error_string<sub_args>("sandbox", es, info));
    return es;
  }
  return ecli::dispatch(kCommands, argc, argv, out);
}

// ============================================================================
// 5. 命令台：一行一条命令，回车执行（不 include 就是零开销，这里就是全部实现）
// ============================================================================
// 逐字节读一行 —— 串口 / 蓝牙 / 键盘 / 管道都是这个形状（解析器不认识 argv，只认识 token 表）
static bool read_line(ecli::line_reader<128> &source) {
  for (int ch = std::getchar(); ch != EOF; ch = std::getchar()) {
    if (source.put(static_cast<char>(ch))) return true;
  }
  return false;
}

// PowerShell / 文件管道会在最前面塞 UTF-8 BOM（EF BB BF）；手动敲不会有
static void skip_bom() {
  const int c0 = std::getchar();
  if (c0 == 0xEF) {
    if (std::getchar() == 0xBB && std::getchar() == 0xBF) return;
  } else if (c0 != EOF) {
    std::ungetc(c0, stdin);
  }
}

static int run_console() {
  char scratch[ECLI_MAX_LINE];
  ecli::line_reader<128> source;
  // 答复出口 = 日志已经绑好的那条通道（main 里 create_logger 绑的）—— 不再绑第二次
  const ecli::reply out = ecli::reply_to_default_logger();

  out.put_lit("\nsandbox —— 敲一条命令，看一段输出（help 看全部，quit 退出）\n"
              "  num 42         数字：十进制 / 0x / 0b / 补零 / 对齐\n"
              "  fmt 你好        排版：左中右对齐 / 截断\n"
              "  me             自定义类型的三种注册写法\n"
              "  json / cbor    同一个人，两种格式各走一圈\n"
              "  level 5        matchit 的分支（0 / 1..9 / 其它）\n"
              "  net set mynet  旧风格：两段式子命令（net 单独敲 = 概览，最长前缀优先）\n"
              "  add -f a.txt   新风格：嵌套 struct 子命令（add / del / find）\n"
              "  --verbose add -f a.txt   新风格：顶层选项 + 子命令（matchit 消费）\n"
              "  log warn       日志：换级别，看哪几行被过滤\n"
              "  echo --upper hi    开关 + 位置参数\n"
              "  args -v -o a.bin --level 7 in.txt    完整选项集（还能 --tag a --tag b）\n\n"
              "故意敲错也有东西看：num（缺参数）、num abc（值不对）、net set（子命令少一段）、nope（未知命令）\n");
  ecli::write_command_list(kCommands, out);

  skip_bom();
  for (;;) {
    out.put_lit("\n> ");
    std::fflush(stdout);
    if (!read_line(source)) break;
    const std::string_view line = source.line();
    if (line == "quit" || line == "exit") break;

    const ecli::error e = run_line(line, scratch, out);   // 新风格优先，落空走旧表
    (void)e;   // 命令台循环：每条命令的回复已经写进 out，返回值只给 run_once 用
    source.clear();
  }
  out.put_lit("\n");
  return 0;
}

// 宿主工具那条路：argv 直接分发（同一个命令表、同一个解析器）
static int run_once(int argc, char **argv) {
  // 同上：argv 这条路也复用那条通道，没有第二次绑定
  const ecli::reply out = ecli::reply_to_default_logger();
  const ecli::error e = run_line_argv(argc, argv, out);
  // help / version 不是失败（和 clap 一样：打完帮助/版本退 0）
  return e == ecli::error::ok || e == ecli::error::help_requested ||
                 e == ecli::error::version_requested
             ? 0
             : 1;
}

// run_check.ps1 用这条：把命令表当脚本跑一遍，只看有没有异常，输出丢掉
static int run_smoke() {
  // 刻意不放 log 命令：它会往 stdout 刷五行日志，冒烟跑只留最后那行总结
  static const char *const kScript[] = {
      "num 42", "num 0x2a", "fmt 你好", "me", "json",
      "cbor",   "level 0",  "level 5",  "level 99",
      "net", "net set mynet",
      "echo --upper hi", "args -v -o a.bin --level 7 --tag net in.txt",
      "add -f a.txt", "del -f b.txt", "find -t net", "add -h",   // 新风格
      "help", "help args", "nope",          // nope = 未知命令，也要有回复而不是崩
  };
  char scratch[ECLI_MAX_LINE];
  char reply_buf[ECLI_REPLY_BUFFER];
  int bad = 0;
  for (const char *line : kScript) {
    buffer_reply b{reply_buf, sizeof(reply_buf), 0};
    const ecli::error e = run_line(line, scratch, b.as_reply());    // 新风格优先，落空走旧表
    // unknown_command 是有意的（最后一条），help/version 也不是失败
    if (e != ecli::error::ok && e != ecli::error::help_requested &&
        e != ecli::error::unknown_command && e != ecli::error::version_requested) {
      ++bad;
      ELOG_ERROR("冒烟失败：{} -> {}", line, ecli::error_name(e));
    }
  }
  ELOG_INFO("冒烟：{} 条命令，{} 条异常", sizeof(kScript) / sizeof(kScript[0]), bad);
  return bad == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  // logger 先建：默认 logger 是 ELOG_* 的去处，必须在第一次打日志之前建好
  e_log::logger *const log =
      e_log::create_logger("sandbox", e_log::stdout_sink(), e_log::level::debug);
  if (log == nullptr) {
    std::printf("create_logger failed\n"); // 日志都建不起来时，只能退回 printf
    return 1;
  }
  const level_args s;
  const person p;
  fmt_args d{"nihao shijie"};
  test t = test::hex;
  ELOG_INFO(" {:#}",s);
  ELOG_INFO(" {:#}",p);
  ELOG_INFO(" {:#}",d);
  ELOG_INFO(" {:#}",t);
  if (argc > 1) {
    const std::string_view mode(argv[1]);
    if (mode == "--check") return run_smoke();
    if (mode == "--repl") return run_console();
    return run_once(argc, argv);
  }
  return run_console();   // 默认：亲手敲
}
