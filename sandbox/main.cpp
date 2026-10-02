/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : efmt / elog 的沙盒 —— 想测什么功能就往这里加
 * @attention      : 三种跑法（详见文件末尾 ⑧ 的注释块）：
 *                     ./sandbox            亲手敲命令的命令台（默认就是这个；quit / exit 退出）
 *                     ./sandbox --check    只跑自动自检（run_check.ps1 用的就是这条）
 *                     ./sandbox status -v  一次性：按 argv 跑一条命令（宿主工具那条路）
 *                   include 根由 CMakeLists.txt 接好（和 tests/run_check.ps1 一致）：
 *                     <repo>/tests/include → <middleware/efmt/...> <middleware/etl/...>
 *                     <repo>               → <elog/elog.hpp>
 ******************************************************************************
 */

#define EFMT_ENABLE_ANSI_STYLES 0   // 关掉颜色转义，CLion 运行窗口更干净

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <elog/elog.hpp>
#include <eserde/serde.hpp>       // 可选基座：能力标签查询 + schema（efmt 本体不认识它）
#include <eserde/json.hpp>        // JSON 序列化 / 反序列化（构建在基座上）
#include <eserde/cbor.hpp>        // CBOR 二进制（RFC 8949 子集，写出的字节标准解码器能读）
#include <ecli/cli.hpp>           // 命令行解析（声明即推导；argv 与"一行文本"同一条路）
#include <ecli/command.hpp>       // 命令表：多命令 / 子命令（"wifi set" 按最长前缀匹配）

// ETL：sandbox 显式依赖（CMakeLists.txt 的 ETL_ROOT），elog 已把 ETL 常用类型
// 接进格式化（etl::string / etl::vector / etl::optional / etl::pair / etl::variant...）
#include <middleware/etl/string.h>
#include <middleware/etl/vector.h>
#include <middleware/etl/optional.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace eserde;
using namespace ecli;

// ============================================================================
// 自定义类型：四种注册写法
// ============================================================================
struct imu {                            // ① AUTO：零声明，位置式
  float ax, ay, az;
};
E_FMT_FORMATTER_AUTO(imu);              // → imu(1.5, 2.5, 3.5)

struct point {                          // ② 只列字段名
  int x, y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);    // → {x=3, y=4}

struct cfg {                            // ③ 类型内一行（同样是聚合体：按声明顺序绑定）
  int baud = 115200;
  bool verbose = false;
  E_FMT_FIELDS(baud, verbose)           // → { baud = 115200, verbose = false }
};

E_FMT_DERIVE(struct frame {             // ④ 声明即推导：结构体 / 枚举通用
  point p;                              //    字段名一个都不用手写
  unsigned ts;
});

E_FMT_DERIVE_ENUM(enum class state { idle, sampling, fault });   // 枚举走专用入口（枚举体的逗号是顶层逗号）


E_FMT_DERIVE(struct person {              // ⑤ 声明即推导 + 字段标签 + 能力标签
  int age;                                //    名字照样一个都不用写
  float weight;
  float high;
  [[efmt::arg(short, long)]]              //    字段标签：efmt 只解析，eserde 之类上层来查
  std::string name;
  state state;
}, Debug, Serialize, Deserialize);        //    能力标签：原样登记，efmt 本体只认 Debug
                                          //    写要 Serialize、读要 Deserialize（缺了编译期报错）


// ⑥ 命令行参数：声明即推导解析器（对标 Rust clap 的 #[derive(Parser)]）。
//    字段名一个都不用写，标签就是 clap 的 #[arg(...)]：short / long / pos / help / required / skip。
//    解析器不认识 argv —— 它只认识 token 表，所以串口 / 蓝牙 / 键盘的"一行文本"走同一个入口。
E_FMT_DERIVE(struct cli_args {
  [[efmt::arg(short, long, help = "verbose output")]]              bool verbose = false;
  [[efmt::arg(short = "o", long = "output", help = "dump file")]]  const char *out = nullptr;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]]        int level = 3;
  [[efmt::arg(long = "tag", help = "repeatable")]]                 std::vector<std::string> tags;
  [[efmt::arg(pos = "1", help = "input file")]]                    std::string input;
}, Cli);

// ⑦ 命令表：一个命令 = {名字, 帮助, 处理函数}，名字带空格就是子命令。
//    处理函数签名统一 void(const Args&, ecli::reply)：参数照旧由 E_FMT_DERIVE 推导，
//    reply 决定"回给谁"（串口问的回串口 —— 所以同一个命令表能吃 argv、串口、蓝牙）。
E_FMT_DERIVE(struct status_cmd_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
}, Cli);

// ssid 不写选项 —— 它由命令名模式 "wifi set :ssid" 捕获（matchit 的 extractor），
// 所以标 skip（不进选项表），值由 ecli 注入到这个同名字段。
E_FMT_DERIVE(struct wifi_set_args {
  [[efmt::arg(skip)]]                                          etl::string<16> ssid;
  [[efmt::arg(short = "p", long = "pass", help = "password")]] etl::string<16> pass;
}, Cli);

static void status_cmd(const status_cmd_args &a, ecli::reply out) {
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void wifi_set_cmd(const wifi_set_args &a, ecli::reply out) {
  char buf[80];
  // 口令只报"设了没"，不回显 —— 顺手演示"处理函数自己决定回什么"
  const std::size_t n = format_to(buf, sizeof(buf), "ssid -> {} (password: {})\n", a.ssid,
                                  a.pass.empty() ? "none" : "set");
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
}

E_FMT_DERIVE(struct echo_cmd_args {
  [[efmt::arg(long = "upper", help = "uppercase the text")]] bool upper = false;
  [[efmt::arg(pos = "1", help = "text to echo")]]            etl::string<32> text;
}, Cli);

static void echo_cmd(const echo_cmd_args &a, ecli::reply out) {
  etl::string<32> t = a.text;
  if (a.upper) {
    for (std::size_t i = 0; i < t.size(); ++i) {
      const char c = t[i];
      if (c >= 'a' && c <= 'z') t[i] = static_cast<char>(c - 'a' + 'A');
    }
  }
  char buf[64];
  const std::size_t n = format_to(buf, sizeof(buf), "{}\n", t);
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
}

static constexpr ecli::command kCommands[] = {
    {"status", "show link status", ecli::command_of<status_cmd_args, status_cmd>()},
    {"wifi set :ssid", "set ssid", ecli::command_of<wifi_set_args, wifi_set_cmd>()},   // :ssid 捕获
    {"echo", "echo text back", ecli::command_of<echo_cmd_args, echo_cmd>()},
};

// ============================================================================
// 小工具
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

template <typename... Args>
static std::string text(std::string_view fmt_str, const Args &...args) {
  char buf[256];
  std::size_t n = format_to(buf, sizeof(buf), fmt_str, args...);
  if (n >= sizeof(buf)) {
    n = sizeof(buf) - 1;
  }
  return std::string(buf, n);
}

static void check(const char *what, const std::string &actual, const char *wanted) {
  ++g_checks;
  if (actual == wanted) {
    std::printf("  OK   %-22s %s\n", what, actual.c_str());
    return;
  }
  ++g_failures;
  std::printf("  FAIL %-22s actual=[%s] wanted=[%s]\n", what, actual.c_str(), wanted);
}

static void section(const char *title) { std::printf("\n--- %s ---\n", title); }

// ============================================================================
// ⑧ 验收用的命令台：亲手敲命令（默认不进，run_check 跑这个程序时不会卡在等输入）
// ============================================================================
// 【怎么跑】
//   编译（和 run_check.ps1 用同一组开关；CLion 里直接 Run `sandbox` 目标也行）：
//     g++ -std=c++17 -O2 -Wall -Wextra -I tests/include -I . -DEFMT_DERIVE_SHOW_TYPE=0 sandbox/main.cpp -o sandbox.exe
//   ① 直接跑 = 进命令台（本文档的主角）：
//     ./sandbox.exe                 ← CLion 里按 Run、或命令行直接敲，都会看到提示符 "> "
//     只有 --check 才跑自动自检（run_check.ps1 用这条，免得测试卡在等人输入）：
//     ./sandbox.exe --check
//     --repl 与直接跑等价（管道喂命令脚本时写出来更清楚）：cat cmds.txt | ./sandbox.exe --repl
//   ② 一次性模式（宿主工具那条路：argv 直接分发，方便写脚本）：
//     ./sandbox.exe status -v
//     ./sandbox.exe wifi set -s mynet -p secret
//
// 【提示符下输入什么】一行一条命令、回车执行；参数用空格分开，写 --opt=value 或 --opt value 都行
//     status                        → 显示链路状态
//     status -v                     → 带细节（-v 短选项 = --verbose）
//     wifi set mynet                → 子命令 + 命令名模式 "wifi set :ssid"：mynet 被捕获进 ssid
//     wifi set mynet -p pw          → 模式捕获 + 普通选项混着用
//     echo --upper hello            → 开关 + 位置参数
//     echo "hello world"            → 引号包住空格（算一个 token）
//     help                          → 列出命令表
//     help wifi set                 → 看某个命令的用法（等价于 wifi set -h）
//     quit   或   exit              → 退出（Windows 下 Ctrl+Z 再回车 = EOF，一样能退）
//   故意敲错也能看到"报错 + usage"，这正是要验收的部分：
//     nope                → unknown_command（顺便把命令表列出来）
//     wifi set            → missing_required（-s 是必填）
//     status --wat        → unknown_option
//     echo "abc           → bad_quote（引号没闭合）
//     wifi set -s a -s b  → 同一个选项给两次 = 最后一次生效（last wins）
//     -V  或  --version   → version_requested（版本行由 sandbox 自己打出来）
//
// 【为什么这条路和真机是同一条】stdin 的字节是【一个字节一个字节】喂进 ecli::line_reader 的，
//   跟串口 / 蓝牙收到字节、攒够一行再解析完全一样；回话走 reply 通道（这里接到 stdout，
//   真机上换成 ecli::reply_to<uart_write>() 就回串口）。解析器不认识 argv —— 两条路同一份代码。
static constexpr const char *kSandboxVersion = "0.1.0-sandbox";
static char g_repl_reply[512];

// 一次性：argv → 分发（宿主工具用法）
static int run_oneshot(int argc, char **argv) {
  buffer_reply out{g_repl_reply, sizeof(g_repl_reply), 0};
  const ecli::error e = ecli::dispatch(kCommands, argc, argv, out.as_reply());
  if (e == ecli::error::version_requested) {
    char v[64];
    ecli::write_version("sandbox", kSandboxVersion, v, sizeof(v));
    std::printf("%s", v);
  } else if (out.used != 0) {
    std::printf("%s", g_repl_reply);
  }
  // help / version 不是失败（和 clap 一样：打帮助/版本后退 0）
  const bool ok = e == ecli::error::ok || e == ecli::error::help_requested ||
                  e == ecli::error::version_requested;
  return ok ? 0 : 1;
}

// 交互：把 stdin 的字节流当串口喂（逐字节），一行凑齐就分发；输出回 stdout
static int run_repl() {
  ecli::line_reader<128> source;   // 每个输入源一个行缓冲 —— 这里只有一个源
  char scratch[ECLI_MAX_LINE];
  std::printf("sandbox 命令台 —— 一行一条命令、回车执行；quit / exit（或 Ctrl+Z 回车）退出\n\n");
  {
    buffer_reply out{g_repl_reply, sizeof(g_repl_reply), 0};   // 开局先把命令表列出来
    ecli::write_command_list(kCommands, out.as_reply());
    std::printf("%s\n", g_repl_reply);
  }
  std::printf("> ");
  std::fflush(stdout);
  // 顺手容错：PowerShell / 文件管道会在最前面塞 UTF-8 BOM（EF BB BF），手动敲不会有
  static constexpr unsigned char kBom[3] = {0xEF, 0xBB, 0xBF};
  int bom = 0;
  for (int ch = std::getchar(); ch != EOF; ch = std::getchar()) {
    if (bom < 3 && static_cast<unsigned char>(ch) == kBom[bom]) {
      ++bom;
      continue;
    }
    bom = 3;   // 一旦不是 BOM，后面就再也不检查
    if (!source.put(static_cast<char>(ch))) continue;   // 还没凑够一行
    const std::string_view cmd = source.line();
    if (cmd == "quit" || cmd == "exit") break;
    buffer_reply out{g_repl_reply, sizeof(g_repl_reply), 0};
    const ecli::error e = ecli::dispatch(kCommands, cmd, scratch, sizeof(scratch), out.as_reply());
    if (e == ecli::error::version_requested) {
      char v[64];
      ecli::write_version("sandbox", kSandboxVersion, v, sizeof(v));
      std::printf("%s", v);
    } else if (out.used != 0) {
      std::printf("%s", g_repl_reply);
    }
    std::printf("  [%s]\n> ", ecli::error_name(e));
    std::fflush(stdout);
    source.clear();
  }
  std::printf("\n");
  return 0;
}

int main(int argc, char **argv) {
  // 模式选择（默认就是给你敲命令的那个）：
  //   什么都不给            → 进命令台（交互，等你输入；EOF / quit / exit 退出）
  //   --repl                → 同上（显式写出来，管道喂脚本时可读性更好）
  //   --check               → 只跑下面的自动自检（run_check.ps1 用这条，不会卡住）
  //   其它任何参数          → 当成一条命令一次性执行：./sandbox status -v
  bool want_check = false;
  if (argc > 1) {
    const std::string_view mode(argv[1]);
    if (mode == "--repl") return run_repl();
    if (mode == "--check") {
      want_check = true;   // 继续往下走 = 自动自检
    } else {
      return run_oneshot(argc, argv);
    }
  } else {
    return run_repl();     // 直接跑（CLion / 命令行）→ 命令台
  }
  (void)want_check;

  person p ={
    .age = 18,
    .weight = 1.0,
    .high = 1.0,
    .name = "xiaoming",
    .state = state::idle
  };

  println_info("person info {}",p);
  print_info("person info {:#}", p);

  // 基座：声明原文 → 编译期数据（能力标签 / schema / 字段标签）
  static_assert(eserde::has_cap_v<person, Serialize>, "person 带了 Serialize 能力标签");
  static_assert(eserde::has_cap_v<person, Deserialize>, "person 带了 Deserialize 能力标签");
  static_assert(eserde::find_by_tag<person>("short") == 3, "name 字段带 short 标签");
  println_info("schema: {} 个字段；字段 3 的类型名 = {}，标签数 = {}",
               eserde::field_count<person>(),
               eserde::field_type_name<person>(3),
               eserde::tag_count<person>(3));
  eserde::visit_fields(p, [](std::string_view field, const auto &) {
    std::printf("  field %.*s\n", static_cast<int>(field.size()), field.data());
  });

  // JSON：写→读一圈（eserde::json，零第三方、不抛异常）
  char json_buf[256];
  std::size_t json_len = eserde::json::write_to(json_buf, sizeof(json_buf), p);
  const bool json_fits = json_len < sizeof(json_buf);
  if (!json_fits) json_len = sizeof(json_buf) - 1;
  println_info("json ({} B): {}", json_len, std::string_view(json_buf, json_len));

  person q{};
  const eserde::json::error je =
      eserde::json::read_from(std::string_view(json_buf, json_len), q);
  if (je == eserde::json::error::ok && json_fits) {
    println_info("json round-trip: {}", q);
  } else {
    println_info("json round-trip skipped: {}", eserde::json::error_name(je));
  }

  // CBOR：同一个对象走二进制（同一套语义：snprintf 语义 / 失败不动原对象 / 错误码）
  unsigned char cbor_buf[128];
  std::size_t cbor_len = eserde::cbor::write_to(cbor_buf, sizeof(cbor_buf), p);
  const bool cbor_fits = cbor_len < sizeof(cbor_buf);
  if (!cbor_fits) {
    cbor_len = sizeof(cbor_buf) - 1;
  }
  println_info("cbor ({} B；同样内容 json 是 {} B)", cbor_len, json_len);

  person r{};
  const eserde::cbor::error ce = eserde::cbor::read_from(cbor_buf, cbor_len, r);
  if (ce == eserde::cbor::error::ok && cbor_fits) {
    const std::string want = text("{}", p);
    check("cbor round-trip", text("{}", r), want.c_str());
  } else {
    println_info("cbor round-trip skipped: {}", eserde::cbor::error_name(ce));
  }

  // elog：第一个创建的 logger 自动成为默认 logger，之后的 ELOG_* 宏都走它。
  // 创建必须发生在第一次 ELOG_* 之前，否则默认 logger 为空、日志被静默丢弃。
  if (!e_log::create_logger("etl-demo", e_log::stdout_sink(), e_log::level::debug)) {
    std::printf("  create_logger failed\n");
  }

  const etl::vector<int,8> v ={1,2,3,4,5,6,7,8};
  ELOG_INFO("vector print:{}",v);
  ELOG_INFO("person info {}",p);


  // ============================================================================
  // elog × ETL：嵌入式类型直接打
  // ============================================================================
  section("elog × ETL 类型");
  etl::string<32> dev = "imu01";
  etl::vector<int, 8> raw{1, 2, 3};
  etl::vector<etl::string<16>, 4> names;
  names.push_back("a");
  names.push_back("bc");
  etl::optional<float> temp = 36.5f;
  etl::optional<int> absent;

  check("etl::string", text("{}", dev), "imu01");
  check("etl::string 宽度/截断", text("[{:>10.3}]", dev), "[       imu]");
  check("etl::vector", text("{}", raw), "[1, 2, 3]");
  check("嵌套 etl::vector<etl::string>", text("{}", names), "[a, bc]");
  check("etl::optional 有值", text("{}", temp), "36.5");
  check("etl::optional 空", text("{}", absent), "nullopt");

  // elog 全链路：ETL 类型直接进日志（logger 已在 main 开头创建）。
  // elog 对用户默认打开容器格式（EFMT_ENABLE_CONTAINER_FORMAT=1），
  // MCU 上打 etl::vector 无需任何配置。
  ELOG_INFO("dev={} raw={} temp={}", dev, raw, temp);

  // ============================================================================
  // ecli：命令行解析（宿主 argv 与设备端"一行文本"共用同一个解析器）
  // ============================================================================
  section("ecli 命令行解析");
  {
    cli_args a{};
    const char *argv[] = {"sandbox", "-vo", "dump.bin", "--level=7", "--tag", "net", "input.txt"};
    ecli::error_info info{};
    const ecli::error e = ecli::parse(7, argv, a, &info);
    check("argv 解析", ecli::error_name(e), "ok");
    check("短选项聚簇 + 粘连取值", a.verbose ? "true" : "false", "true");
    check("取值与可重复项", text("{}|{}|{}", a.out, a.level, a.tags[0]), "dump.bin|7|net");
    check("位置参数", a.input, "input.txt");

    // 设备端：串口 / 蓝牙 / 键盘读进来的就是"一行文本"，同一个解析器、同一套标签
    cli_args b{};
    char line_scratch[ECLI_MAX_LINE];
    const ecli::error e2 =
        ecli::parse("--level 9 \"in put.txt\"", b, line_scratch, sizeof(line_scratch), &info);
    check("一行文本解析", ecli::error_name(e2), "ok");
    check("引号包住的空格算一个 token", b.input, "in put.txt");

    // 帮助 / 报错：snprintf 语义写进缓冲区 —— 谁问的就回给谁（串口问的回串口）
    std::printf("%s\n", ecli::help_string<cli_args>("sandbox", "efmt sandbox CLI").c_str());
    cli_args c{};
    const ecli::error e3 = ecli::parse("--level=abc", c, line_scratch, sizeof(line_scratch), &info);
    std::printf("%s\n", ecli::error_string<cli_args>("sandbox", e3, info).c_str());
  }

  // ============================================================================
  // ecli 命令表：多命令 / 子命令 / 帮助 / 报错，全部回给"发起命令的那一路"
  // ============================================================================
  section("ecli 命令表（多命令 / 子命令）");
  {
    char reply[256];
    char scratch[ECLI_MAX_LINE];
    const char *lines[] = {"status -v", "wifi set mynet -p pw", "wifi set", "help wifi set", "nope"};
    for (const char *line : lines) {
      reply[0] = '\0';
      buffer_reply b{reply, sizeof(reply), 0};
      const ecli::error e = ecli::dispatch(kCommands, line, scratch, sizeof(scratch), b.as_reply());
      std::printf("  $ %-18s [%s]\n", line, ecli::error_name(e));
      if (reply[0] != '\0') {
        std::printf("      %s", reply);            // 命令自己回的话（多行 help 原样打）
      }
    }
    reply[0] = '\0';
    buffer_reply b{reply, sizeof(reply), 0};
    (void)ecli::dispatch(kCommands, "status -v", scratch, sizeof(scratch), b.as_reply());
    check("子命令分发 -v", std::string(reply), "link: up (detail)\n");
    reply[0] = '\0';
    buffer_reply b2{reply, sizeof(reply), 0};
    (void)ecli::dispatch(kCommands, "wifi set office -p pw", scratch, sizeof(scratch), b2.as_reply());
    check("子命令 + 命令名模式捕获", std::string(reply), "ssid -> office (password: set)\n");
    reply[0] = '\0';
    buffer_reply b3{reply, sizeof(reply), 0};
    const ecli::error missing =
        ecli::dispatch(kCommands, "wifi set home -p", scratch, sizeof(scratch), b3.as_reply());
    check("取值缺失会报错并给用法",
          missing == ecli::error::missing_value &&
                  std::string(reply).find("usage: wifi set :ssid") != std::string::npos
              ? "ok"
              : "bad",
          "ok");

    // 模式段不够（:ssid 要一个 token，"wifi set" 只有两段）→ 不命中该命令；
    // 表里也没有更短的同名命令，于是 unknown_command（想更友好就补一条表项）
    reply[0] = '\0';
    buffer_reply b4{reply, sizeof(reply), 0};
    check("半截命令（段不够）= unknown_command",
          ecli::dispatch(kCommands, "wifi set", scratch, sizeof(scratch), b4.as_reply()) ==
                  ecli::error::unknown_command
              ? "ok"
              : "bad",
          "ok");
  }

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
