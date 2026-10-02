/**
 ******************************************************************************
 * @file           : ecli_command_check.cpp
 * @brief          : ecli::command 的行为检查（命令表 / 子命令最长前缀 / 帮助 / 回复通道 / 错误路由）
 * @attention      : 只用标准库类型，宿主与嵌入式两种配置都能编（string_reply 是宿主专属，
 *                   用 EFMT_ENABLE_DYNAMIC_STRING 圈出来）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <ecli/cli.hpp>
#include <ecli/command.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace e_fmt;
using namespace ecli;

// ---------------------------------------------------------------------------
// 各命令的参数类型：照旧 E_FMT_DERIVE 推导
// ---------------------------------------------------------------------------
E_FMT_DERIVE(struct status_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct wifi_args {
  [[efmt::arg(short = "s", long = "ssid", required, help = "network name")]] std::string ssid;
  [[efmt::arg(short = "p", long = "pass", help = "password")]]               std::string pass;
}, Cli);

E_FMT_DERIVE(struct echo_args {
  [[efmt::arg(long = "upper", help = "uppercase the text")]] bool upper = false;
  [[efmt::arg(pos = "1", help = "text to echo")]]            std::string text;
}, Cli);

// ---------------------------------------------------------------------------
// 处理函数：统一 void(const Args&, reply)
// ---------------------------------------------------------------------------
static std::string g_calls;   // 记录处理函数被调用的顺序与内容

static void status_run(const status_args &a, reply out) {
  g_calls += a.verbose ? "[status -v]" : "[status]";
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void wifi_summary_run(const status_args &a, reply out) {
  g_calls += a.verbose ? "[wifi -v]" : "[wifi]";
  out.put_lit("wifi: 1 ap\n");
}

static void wifi_set_run(const wifi_args &a, reply) {
  g_calls += "[wifi set " + a.ssid + "/" + a.pass + "]";
}

static void echo_run(const echo_args &a, reply out) {
  g_calls += "[echo]";
  std::string text = a.text;
  if (a.upper) {
    for (std::size_t i = 0; i < text.size(); ++i) {
      text[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    }
  }
  text += "\n";
  out.put(text);
}

// ---------------------------------------------------------------------------
// 自定义回复通道：库直接调这个 (data, size) 写函数（嵌入式里换成 UART 写）
// ---------------------------------------------------------------------------
static std::string g_sink;

static void uart_write(const char *data, std::size_t size) { g_sink.append(data, size); }

static reply sink_reply() { return reply_to<uart_write>(); }

// ---------------------------------------------------------------------------
// 命令表：故意同时放 "wifi" 与 "wifi set" —— 用来钉住"最长前缀优先"
// ---------------------------------------------------------------------------
static constexpr command kCommands[] = {
    {"status", "show link status", command_of<status_args, status_run>()},
    {"wifi", "wifi summary", command_of<status_args, wifi_summary_run>()},
    {"wifi set", "set ssid / password", command_of<wifi_args, wifi_set_run>()},
    {"echo", "echo text back", command_of<echo_args, echo_run>()},
};

static_assert(kCommands[2].name == "wifi set");
static_assert(option_count<wifi_args>() == 2);
static_assert(is_cli_args_v<wifi_args>);

// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static void check_error(const char *what, error e, error wanted) {
  check(what, e == wanted,
        std::string("actual=") + error_name(e) + " wanted=" + error_name(wanted));
}

static bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

// 比 ECLI_REPLY_BUFFER（384）还长的 about：用来钉"帮助装不下要如实说 truncated"
static constexpr std::string_view kLongAbout =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
    "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

static char g_reply[1024];

// 跑一行命令：返回错误码，文本通过 text 带出来
template <std::size_t N>
static error run_line(const command (&table)[N], const char *line, std::string &text) {
  char scratch[ECLI_MAX_LINE];
  buffer_reply b{g_reply, sizeof(g_reply), 0};
  const error e = dispatch(table, line, scratch, sizeof(scratch), b.as_reply());
  const std::size_t n = b.used < sizeof(g_reply) ? b.used : sizeof(g_reply) - 1;
  text.assign(g_reply, n);
  return e;
}

// ---------------------------------------------------------------------------
static void check_dispatch() {
  std::string text;

  g_calls.clear();
  check_error("单命令", run_line(kCommands, "status", text), error::ok);
  check("单命令输出", text == "link: up\n", text);
  check("单命令执行了处理函数", g_calls == "[status]", g_calls);

  check_error("短选项", run_line(kCommands, "status -v", text), error::ok);
  check("短选项输出", text == "link: up (detail)\n", text);

  g_calls.clear();
  check_error("两级子命令", run_line(kCommands, "wifi set -s mynet -p secret", text), error::ok);
  check("子命令参数", g_calls == "[wifi set mynet/secret]", g_calls);
  check("子命令不回话就没输出", text.empty(), text);

  g_calls.clear();
  check_error("最长前缀：wifi 命中短的那条", run_line(kCommands, "wifi", text), error::ok);
  check("wifi 走 summary", g_calls == "[wifi]" && text == "wifi: 1 ap\n", g_calls + text);

  g_calls.clear();
  check_error("最长前缀：wifi set 命中长的那条", run_line(kCommands, "wifi set -s n1", text),
              error::ok);
  check("wifi set 走 set 分支", g_calls == "[wifi set n1/]", g_calls);

  g_calls.clear();
  check_error("位置参数 + 开关", run_line(kCommands, "echo --upper hello", text), error::ok);
  check("处理函数能回话", text == "HELLO\n", text);
}

static void check_help_and_errors() {
  std::string text;

  check_error("空行 = 列命令", run_line(kCommands, "", text), error::help_requested);
  check("列命令含表头", contains(text, "commands:"), text);
  check("列命令含子命令名", contains(text, "wifi set"), text);
  check("列命令含 help", contains(text, "help [command]"), text);

  check_error("help 单词", run_line(kCommands, "help", text), error::help_requested);
  check("help 列命令", contains(text, "status") && contains(text, "echo text back"), text);

  check_error("help <子命令>", run_line(kCommands, "help wifi set", text), error::help_requested);
  check("help 子命令给出该命令的用法", contains(text, "usage: wifi set"), text);
  check("help 子命令列出选项", contains(text, "-s, --ssid") && contains(text, "network name"), text);

  check_error("命令自己的 -h", run_line(kCommands, "wifi set -h", text), error::help_requested);
  check("命令 -h 不执行处理函数", g_calls.find("[wifi set") == std::string::npos, g_calls);
  check("命令 -h 给用法", contains(text, "usage: wifi set"), text);

  g_calls.clear();
  check_error("未知命令", run_line(kCommands, "nope", text), error::unknown_command);
  check("未知命令文本", contains(text, "unknown command 'nope'"), text);
  check("未知命令附带命令表", contains(text, "commands:"), text);
  check("未知命令不执行处理函数", g_calls.empty(), g_calls);

  check_error("help 未知命令", run_line(kCommands, "help nope", text), error::unknown_command);

  check_error("必填项缺失", run_line(kCommands, "wifi set -p only", text), error::missing_required);
  check("必填项报错带用法", contains(text, "missing required option '--ssid'") &&
                                contains(text, "usage: wifi set"),
        text);

  check_error("未知选项", run_line(kCommands, "status --wat", text), error::unknown_option);
  check("未知选项文本", contains(text, "unknown option '--wat'"), text);

  check_error("多余的位置参数", run_line(kCommands, "wifi extra", text), error::too_many_args);

  check_error("引号没闭合", run_line(kCommands, "echo \"abc", text), error::bad_quote);
  check("引号没闭合文本", contains(text, "unterminated quote"), text);

  {
    std::string many = "status";
    for (int i = 0; i < 20; ++i) many += " -v";
    check_error("token 超上限", run_line(kCommands, many.c_str(), text), error::too_many_tokens);
  }
}

static void check_reply_channels() {
  std::string text;

  // argv 入口（宿主工具：myapp wifi set -s mynet）
  {
    const char *argv[] = {"app", "wifi", "set", "-s", "n1", "-p", "p1"};
    g_calls.clear();
    buffer_reply b{g_reply, sizeof(g_reply), 0};
    check_error("argv 分发", dispatch(kCommands, 7, argv, b.as_reply()), error::ok);
    check("argv 分发到子命令", g_calls == "[wifi set n1/p1]", g_calls);
  }
  {
    const char *argv[] = {"app", "help", "wiki"};
    buffer_reply b{g_reply, sizeof(g_reply), 0};
    check_error("argv 里的未知命令", dispatch(kCommands, 3, argv, b.as_reply()),
                error::unknown_command);
  }

  // reply_to<写函数>：库直接调你的 (data, size) 写函数
  {
    const error e = dispatch(kCommands, "status", g_reply, sizeof(g_reply), sink_reply());
    check_error("reply_to<sink>", e, error::ok);
    check("sink 收到文本", std::string(g_sink) == "link: up\n", g_sink);
    g_sink.clear();
  }

  // 空 reply：命令照跑，输出丢弃（不崩）
  {
    g_calls.clear();
    char scratch[ECLI_MAX_LINE];
    check_error("空 reply 不崩", dispatch(kCommands, "status", scratch, sizeof(scratch), reply{}),
                error::ok);
    check("空 reply 仍执行", g_calls == "[status]", g_calls);
  }

  // 回执缓冲按 snprintf 语义：装不下只计数，永远 NUL 结尾（截断是回执通道自己的事）
  {
    char small[8];
    char scratch[ECLI_MAX_LINE];
    buffer_reply b{small, sizeof(small), 0};
    const error e = dispatch(kCommands, "help", scratch, sizeof(scratch), b.as_reply());
    check_error("小回执缓冲仍返回 help_requested", e, error::help_requested);
    check("小回执缓冲按 snprintf 语义收尾", std::strlen(small) == 7 && b.used > 7,
          std::string(small) + " used=" + std::to_string(b.used));
  }

  // 帮助文本比 ECLI_REPLY_BUFFER 还长：必须看得见"(truncated)"，不能静默丢
  {
    constexpr command big[] = {{"big", kLongAbout, command_of<echo_args, echo_run>()}};
    std::string text;
    check_error("超长帮助仍返回 help_requested", run_line(big, "big -h", text), error::help_requested);
    check("超长帮助给出 truncated 标记", contains(text, "truncated"), text.substr(0, 80));
  }

#if EFMT_ENABLE_DYNAMIC_STRING
  {
    std::string out;
    char scratch[ECLI_MAX_LINE];
    check_error("string_reply（宿主）", dispatch(kCommands, "status -v", scratch, sizeof(scratch),
                                                 string_reply(out)),
                error::ok);
    check("string_reply 内容", out == "link: up (detail)\n", out);
  }
#endif
}

int main() {
  check_dispatch();
  check_help_and_errors();
  check_reply_channels();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
