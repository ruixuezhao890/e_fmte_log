/**
 ******************************************************************************
 * @file           : ecli_pattern_check.cpp
 * @brief          : 命令名【模式段】的检查（matchit 版匹配）
 *                   字面量 / :参数捕获 / *余下捕获 / 特异性排序 / help 前缀命中 / 裁剪开关
 * @attention      : 宿主与嵌入式两种配置都能编；再用 -DECLI_ENABLE_PATTERN_COMMANDS=0
 *                   跑一遍"只认字面量段"的老行为（见文件末尾的 #if 分支）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string.h>
#include <ecli/command.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE(struct status_args {
  [[efmt::arg(short = "v", long = "verbose")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct wifi_args {
  [[efmt::arg(skip)]]                       etl::string<16> ssid;   // 由 "wifi set :ssid" 注入
  [[efmt::arg(short = "p", long = "pass")]] etl::string<16> pass;
}, Cli);

E_FMT_DERIVE(struct log_args {
  [[efmt::arg(skip)]]          std::vector<std::string> rest;       // 由 "log *rest" 逐个 push
  [[efmt::arg(long = "tag")]]  std::string tag;
}, Cli);

E_FMT_DERIVE(struct set_args {
  [[efmt::arg(skip)]] int level = -1;                               // :level 注入（int）
}, Cli);

E_FMT_DERIVE(struct scalar_rest_args {
  [[efmt::arg(skip)]] std::string one;                              // *rest 注入到标量：应【不】注入
}, Cli);

static std::string g_trace;

static void wifi_summary_run(const status_args &, reply out) {
  g_trace = "wifi-summary";
  out.put_lit("1 ap\n");
}
static void wifi_set_run(const wifi_args &a, reply out) {
  g_trace = "set:" + std::string(a.ssid.c_str()) + "/" + std::string(a.pass.c_str());
  out.put_lit("ok\n");
}
static void log_run(const log_args &a, const params &p, reply out) {   // 三参数形式：看原始捕获
  g_trace = "log:";
  for (std::size_t i = 0; i < a.rest.size(); ++i) {
    g_trace += a.rest[i];
    g_trace += "|";
  }
  g_trace += "caps=" + std::to_string(p.size()) + ":";
  for (std::size_t i = 0; i < p.size(); ++i) {
    g_trace += std::string(p.name_at(i)) + "(" + std::to_string(p.rest_count(i)) + ")";
  }
  out.put_lit("done\n");
}
static void set_run(const set_args &a, reply) { g_trace = "level:" + std::to_string(a.level); }
static void scalar_rest_run(const scalar_rest_args &a, const params &p, reply) {
  g_trace = "one:[" + a.one + "] caps=" + std::to_string(p.size());
}
static void sensor_read_run(const status_args &, reply out) {
  g_trace = "sensor-read";
  out.put_lit("r\n");
}
static void sensor_rest_run(const status_args &, reply out) {
  g_trace = "sensor-rest";
  out.put_lit("w\n");
}

static constexpr command kTable[] = {
    {"wifi", "wifi summary", command_of<status_args, wifi_summary_run>(), help_of<status_args>()},
    {"wifi set :ssid", "set ssid", command_of<wifi_args, wifi_set_run>(), help_of<wifi_args>()},
    {"log *rest", "log lines", command_of<log_args, log_run>(), help_of<log_args>()},
    {"set :level", "set level", command_of<set_args, set_run>(), help_of<set_args>()},
    {"one *rest", "scalar rest", command_of<scalar_rest_args, scalar_rest_run>(), help_of<scalar_rest_args>()},
    {"sensor read", "read sensor", command_of<status_args, sensor_read_run>(), help_of<status_args>()},
    {"sensor *rest", "sensor rest", command_of<status_args, sensor_rest_run>(), help_of<status_args>()},
};

static_assert(kTable[1].name == "wifi set :ssid");

// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

[[maybe_unused]] static bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

static char g_reply[512];

template <std::size_t N>
static error run_line(const command (&table)[N], const char *line, std::string &text) {
  static char scratch[ECLI_MAX_LINE];
  buffer_reply b{g_reply, sizeof(g_reply), 0};
  const error e = dispatch(table, line, scratch, sizeof(scratch), b.as_reply());
  const std::size_t n = b.used < sizeof(g_reply) ? b.used : sizeof(g_reply) - 1;
  text.assign(g_reply, n);
  return e;
}

static void check_patterns_on() {
#if ECLI_ENABLE_PATTERN_COMMANDS
  std::string text;
  g_trace.clear();

  check(":参数 捕获 + 注入同名字段",
        run_line(kTable, "wifi set home -p pw", text) == error::ok && g_trace == "set:home/pw",
        g_trace);
  check(":参数 与选项各归各位",
        run_line(kTable, "wifi set office", text) == error::ok && g_trace == "set:office/", g_trace);
  check("字面量命令照旧", run_line(kTable, "wifi", text) == error::ok && g_trace == "wifi-summary",
        g_trace);

  g_trace.clear();
  check("*rest 注入容器（逐个 push）+ 三参数处理函数看得到捕获",
        run_line(kTable, "log a b c", text) == error::ok &&
            g_trace == "log:a|b|c|caps=1:rest(3)",
        g_trace);
  check("*rest 收 0 个也命中", run_line(kTable, "log", text) == error::ok &&
                                   g_trace == "log:caps=1:rest(0)",
        g_trace);

  check(":参数 注入到 int 字段", run_line(kTable, "set 5", text) == error::ok &&
                                    g_trace == "level:5",
        g_trace);
  check(":参数 类型不对会报错", run_line(kTable, "set abc", text) == error::invalid_value);
  check("类型不对的报错带 usage", contains(text, "usage: set :level"), text.substr(0, 60));

  check("*rest 注入标量：不注入但 params 里有",
        run_line(kTable, "one x y", text) == error::ok && g_trace == "one:[] caps=1", g_trace);

  // 特异性：具体命令赢过 catch-all
  check("sensor read 赢过 sensor *rest",
        run_line(kTable, "sensor read", text) == error::ok && g_trace == "sensor-read", g_trace);
  check("sensor 其它 token 落给 catch-all",
        run_line(kTable, "sensor xyz", text) == error::ok && g_trace == "sensor-rest", g_trace);

  // help 用前缀命中（"help wifi set" 能摸到 "wifi set :ssid"）
  check("help <命令> 前缀命中模式命令",
        run_line(kTable, "help wifi set", text) == error::help_requested &&
            contains(text, "usage: wifi set :ssid"),
        text);
  check("help <命令> 不执行处理函数", g_trace == "sensor-rest", g_trace);
  check("help <未知> 报 unknown_command", run_line(kTable, "help nope", text) == error::unknown_command);

  // 段数不够：:ssid 模式要 3 个 token，"wifi set" 只够到更短的 "wifi"
  // → 多余的 token 由参数解析报 too_many_args（想给友好提示就再写一条 {"wifi set", ...} 表项）
  g_trace.clear();
  check("段数不够时落到更短的前缀命令上（多出的 token 报错）",
        run_line(kTable, "wifi set", text) == error::too_many_args && g_trace.empty(), g_trace);
  check("报错文本带该命令的 usage", contains(text, "usage: wifi"), text.substr(0, 60));
#else
  std::string text;
  g_trace.clear();
  // 裁剪版：:name / *name 当字面量处理
  check("裁剪版：模式段按字面量匹配",
        run_line(kTable, "wifi set :ssid", text) == error::ok && g_trace == "set:/", g_trace);
  g_trace.clear();
  check("裁剪版：普通 token 不命中模式段（落到 wifi，多出的 token 报错）",
        run_line(kTable, "wifi set home", text) == error::too_many_args && g_trace.empty(), g_trace);
#endif
}

int main() {
  check_patterns_on();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
