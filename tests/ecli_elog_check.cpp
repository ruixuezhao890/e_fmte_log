/**
 ******************************************************************************
 * @file           : ecli_elog_check.cpp
 * @brief          : ecli × elog —— 命令回复接到 elog 的 sink（ecli/elog_reply.hpp）
 * @attention      : 需要 ETL（elog 依赖 etl::array），run_check.ps1 在 $hasEtl 时才跑这一步。
 *                   钉住的重点是【回复 = 原样字节】：不加级别前缀、不补换行、多行整块送达。
 *                   这正是回复走 sink 而不是 logger 的原因（logger 会加前缀、补换行，
 *                   整行超过 ELOG_MAX_RECORD_SIZE 还会整行丢弃 —— 多行 usage 会被吃掉）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <elog/elog.hpp>
#include <ecli/elog_reply.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace ecli;

E_FMT_DERIVE(struct status_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct big_args {
  [[efmt::arg(short = "a", long = "alpha", help = "alpha option")]] bool alpha = false;
  [[efmt::arg(short = "b", long = "beta", help = "beta option")]]   bool beta = false;
  [[efmt::arg(short = "g", long = "gamma", help = "gamma option")]] bool gamma = false;
}, Cli);

static void status_run(const status_args &a, reply out) {
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void big_run(const big_args &, reply out) { out.put_lit("big\n"); }

static constexpr command kCommands[] = {
    {"status", "show link status", command_of<status_args, status_run>()},
    {"big", "multi-line help", command_of<big_args, big_run>()},
};

// ---- elog 的 sink：走它自己的 (data, size, user_data) 回调，user_data 也照传 ----
struct sink_state {
  std::string text;
};

static bool collect(const char *data, std::size_t size, void *user_data) {
  static_cast<sink_state *>(user_data)->text.append(data, size);
  return true;
}

// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

int main() {
  char scratch[ECLI_MAX_LINE];
  sink_state state;
  const e_log::sink out = e_log::make_sink(&collect, &state);

  // 1) 回复原样进 sink：库不碰格式，也没有任何"日志化"加工
  {
    const error e = dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to_sink(out));
    check("reply_to_sink 返回 ok", e == error::ok, error_name(e));
    check("sink 收到原样字节", state.text == "link: up\n", state.text);
    check("原样 = 没有日志前缀", !contains(state.text, "[info]"), state.text);
  }

  // 2) 多行帮助整块送达（logger 会按行加前缀、超长整行丢弃；sink 不会）
  {
    char buf[256];
    buffer_reply b{buf, sizeof(buf), 0};
    const error e1 = dispatch(kCommands, "help big", scratch, sizeof(scratch), b.as_reply());
    const std::string wanted(buf);
    check("help big 返回 help_requested", e1 == error::help_requested, error_name(e1));

    state.text.clear();
    const error e2 = dispatch(kCommands, "help big", scratch, sizeof(scratch), reply_to_sink(out));
    check("帮助经 sink 原样送达", e2 == error::help_requested && state.text == wanted,
          state.text.substr(0, 48));
    check("帮助是多行整块", contains(state.text, "\noptions:\n") && state.text.size() > 32,
          state.text.substr(0, 48));
    check("帮助里没有日志前缀", !contains(state.text, "[info]"), state.text.substr(0, 48));
  }

  // 3) 报错文本同样原样（调用方把它丢回提问的那一路）
  {
    state.text.clear();
    const error e = dispatch(kCommands, "big --nope", scratch, sizeof(scratch), reply_to_sink(out));
    check("未知选项报 unknown_option", e == error::unknown_option, error_name(e));
    check("报错文本原样", contains(state.text, "error: unknown option '--nope'") &&
                              contains(state.text, "usage: big"),
          state.text.substr(0, 48));
  }

  // 4) 空 sink：命令照跑，输出丢弃（不崩）
  {
    const e_log::sink empty{};
    check("空 sink 不崩",
          dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to_sink(empty)) == error::ok);
  }

  // 5) elog_stdout_reply()：宿主便利入口，构造出来就是有效通道（这里不真的往 stdout 写）
  check("elog_stdout_reply 有效", elog_stdout_reply().valid());

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
