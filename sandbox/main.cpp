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

#include <middleware/efmt/core/format.hpp>
#include <elog/elog.hpp>            // 日志；顺带把 ETL 类型接进格式化（etl::string 当文本打）
#include <eserde/json.hpp>
#include <eserde/cbor.hpp>
#include <ecli/command.hpp>         // 命令表：多命令 / 子命令 / 命令名模式段
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
  int age;
  std::string name;
  state state;
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
  [[efmt::arg(pos = "1", required, help = "文本，比如 你好 / hello")]] etl::string<16> text;
}, Cli);

E_FMT_DERIVE(struct echo_args {
  [[efmt::arg(long = "upper", help = "转成大写")]]           bool upper = false;
  [[efmt::arg(pos = "1", required, help = "要回显的文本")]]  etl::string<24> text;
}, Cli);

E_FMT_DERIVE(struct log_args {
  [[efmt::arg(pos = "1", help = "trace/debug/info/warn/error/off")]] etl::string<8> level;
}, Cli);

E_FMT_DERIVE(struct level_args {
  [[efmt::arg(skip)]] int n = -1;   // 数字由命令名模式 "level :n" 捕获，不用写标签
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
// 3. 命令实现：格式化用 efmt（format_to），答复走 reply，日志走 ELOG_*
// ============================================================================
// 一行串起"efmt 格式化 → 回给提问的那一路"
template <typename... A>
static void replyf(ecli::reply out, std::string_view fmt, const A &...args) {
  char buf[384];
  const std::size_t n = e_fmt::format_to(buf, sizeof(buf), fmt, args...);   // snprintf 语义：返回完整长度
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
  if (n >= sizeof(buf)) out.put_lit("...(太长，截断了)\n");   // 不静默截断，跟库里的写法一致
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

// ============================================================================
// 4. 命令表：名字带空格就是子命令
// ============================================================================
static constexpr ecli::command kCommands[] = {
    {"num", "数字格式化", command_of<num_args, run_num>()},
    {"fmt", "文本排版", command_of<fmt_args, run_fmt>()},
    {"me", "自定义类型三种写法", command_of<no_args, run_me>()},
    {"json", "JSON 写→读", command_of<no_args, run_json>()},
    {"cbor", "CBOR 写→读", command_of<no_args, run_cbor>()},
    {"log", "日志（可换级别）", command_of<log_args, run_log>()},
    {"level :n", "matchit 分支", command_of<level_args, run_level>()},
    {"echo", "回显文本", command_of<echo_args, run_echo>()},
    {"args", "完整选项集", command_of<args_args, run_args>()},
};

static constexpr const char *kVersion = "0.1.0-sandbox";

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
  const ecli::reply out = ecli::elog_stdout_reply();   // 答复出口 = elog 的 stdout sink

  out.put_lit("\nsandbox —— 敲一条命令，看一段输出（help 看全部，quit 退出）\n"
              "  num 42         数字：十进制 / 0x / 0b / 补零 / 对齐\n"
              "  fmt 你好        排版：左中右对齐 / 截断\n"
              "  me             自定义类型的三种注册写法\n"
              "  json / cbor    同一个人，两种格式各走一圈\n"
              "  level 5        matchit 的分支（0 / 1..9 / 其它）\n"
              "  log warn       日志：换级别，看哪几行被过滤\n"
              "  echo --upper hi    开关 + 位置参数\n"
              "  args -v -o a.bin --level 7 in.txt    完整选项集（还能 --tag a --tag b）\n\n"
              "故意敲错也有东西看：num（缺参数）、num abc（值不对）、nope（未知命令）\n");
  ecli::write_command_list(kCommands, out);

  skip_bom();
  for (;;) {
    out.put_lit("\n> ");
    std::fflush(stdout);
    if (!read_line(source)) break;
    const std::string_view line = source.line();
    if (line == "quit" || line == "exit") break;

    const ecli::error e = ecli::dispatch(kCommands, line, scratch, sizeof(scratch), out);
    if (e == ecli::error::version_requested) out.put(ecli::version_string("sandbox", kVersion));
    source.clear();
  }
  out.put_lit("\n");
  return 0;
}

// 宿主工具那条路：argv 直接分发（同一个命令表、同一个解析器）
static int run_once(int argc, char **argv) {
  const ecli::reply out = ecli::elog_stdout_reply();
  const ecli::error e = ecli::dispatch(kCommands, argc, argv, out);
  if (e == ecli::error::version_requested) out.put(ecli::version_string("sandbox", kVersion));
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
      "echo --upper hi", "args -v -o a.bin --level 7 --tag net in.txt",
      "help", "help args", "nope",          // nope = 未知命令，也要有回复而不是崩
  };
  char scratch[ECLI_MAX_LINE];
  char reply_buf[ECLI_REPLY_BUFFER];
  int bad = 0;
  for (const char *line : kScript) {
    buffer_reply b{reply_buf, sizeof(reply_buf), 0};
    const ecli::error e = ecli::dispatch(kCommands, line, scratch, sizeof(scratch), b.as_reply());
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
    std::printf("create_logger failed\n");   // 日志都建不起来时，只能退回 printf
    return 1;
  }

  if (argc > 1) {
    const std::string_view mode(argv[1]);
    if (mode == "--check") return run_smoke();
    if (mode == "--repl") return run_console();
    return run_once(argc, argv);
  }
  return run_console();   // 默认：亲手敲
}
