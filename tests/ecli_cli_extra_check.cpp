/**
 ******************************************************************************
 * @file           : ecli_cli_extra_check.cpp
 * @brief          : ecli 第一批 / 第二批能力的检查
 *                   第一批：别名 / count(-vvv) / delim(值分隔) / trailing / hyphen / optional / -V
 *                   第二批：requires / conflicts / unless / group(互斥) / group_any(至少一个)
 * @attention      : 只用标准库类型，宿主与嵌入式两种配置都能编。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <ecli/cli.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(__has_include)
#if __has_include(<optional>)
#include <optional>
#define ECLI_TEST_HAS_OPTIONAL 1
#endif
#endif

using namespace e_fmt;
using namespace ecli;

// ---------------------------------------------------------------------------
// 第一批的被测类型
// ---------------------------------------------------------------------------
E_FMT_DERIVE(struct extra_args {
  [[efmt::arg(short, long, count, help = "verbosity (-vvv)")]]                  int verbose = 0;
  [[efmt::arg(short = "o", long = "output", alias = "outfile", help = "file")]] const char *output = nullptr;
  [[efmt::arg(short = "f", short_alias = "F", long = "file", help = "file")]]   const char *file = nullptr;
  [[efmt::arg(long = "tag", delim = ",", help = "a,b,c")]]                      std::vector<std::string> tags;
  [[efmt::arg(long = "raw", hyphen, help = "value may start with -")]]          const char *raw = nullptr;
  [[efmt::arg(long = "level")]]                                                 int level = -1;
#if defined(ECLI_TEST_HAS_OPTIONAL)
  [[efmt::arg(long = "opt")]]     std::optional<int> opt;          // 给了才是 Some
  [[efmt::arg(long = "opt2")]]    std::optional<std::string> opt2;
#endif
  [[efmt::arg(pos = "1", trailing, help = "rest of the line")]]                 std::vector<std::string> rest;
}, Cli);

static_assert(option_count<extra_args>() >= 8);
static_assert(option<extra_args>(0).count, "count 标签生效");
static_assert(!option<extra_args>(0).takes_value, "count 是开关，不收值");
static_assert(option<extra_args>(1).long_alias == "outfile");
static_assert(option<extra_args>(2).short_alias == 'F');
static_assert(option<extra_args>(3).delim == ',');
static_assert(option<extra_args>(4).hyphen);
static_assert(option<extra_args>(option_count<extra_args>() - 1).trailing, "最后一项是 trailing 位置参数");

// ---------------------------------------------------------------------------
// 第二批的被测类型
// ---------------------------------------------------------------------------
E_FMT_DERIVE(struct rel_args {
  [[efmt::arg(long = "a", help = "base value")]]                         int a = 0;
  [[efmt::arg(long = "b", needs = "a", help = "needs --a")]]             int b = 0;
  [[efmt::arg(long = "x", group = "net", help = "mutex x")]]             bool x = false;
  [[efmt::arg(long = "y", group = "net", help = "mutex y")]]             bool y = false;
  [[efmt::arg(long = "p", group_any = "src", help = "one of src")]]      bool p = false;
  [[efmt::arg(long = "q", group_any = "src", help = "one of src")]]      bool q = false;
  [[efmt::arg(long = "cfg", unless = "automatic", help = "unless auto")]] int cfg = 0;   // 用【选项名】引用
  [[efmt::arg(long = "automatic", help = "auto mode")]]                  bool auto_mode = false;
  [[efmt::arg(long = "k", conflicts = "m", help = "not with --m")]]      bool k = false;   // 字段名引用
  [[efmt::arg(long = "m", help = "not with --k")]]                       bool m = false;
}, Cli);

static_assert(option_count<rel_args>() == 10);
static_assert(option<rel_args>(1).requires_mask == (1u << 0), "needs = \"a\" 编译期化成位掩码");
static_assert(option<rel_args>(2).conflicts_mask == (1u << 3), "group = \"net\" 展开成互斥掩码");
static_assert(option<rel_args>(4).unless_mask == (1u << 5), "group_any 展开成 unless 掩码");
static_assert(option<rel_args>(6).unless_mask == (1u << 7), "unless = \"automatic\"");

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

template <typename T>
static error run_line(const char *line, T &out, error_info *info = nullptr) {
  static char scratch[ECLI_MAX_LINE];
  return parse(line, out, scratch, sizeof(scratch), info);
}

// ---------------------------------------------------------------------------
// 第一批
// ---------------------------------------------------------------------------
static void check_batch1() {
  {
    extra_args a{};
    check("count: -vvv = 3", run_line("-vvv", a) == error::ok && a.verbose == 3,
          std::to_string(a.verbose));
    extra_args b{};
    check("count: 长选项重复", run_line("--verbose --verbose --verbose --verbose", b) == error::ok &&
                                   b.verbose == 4,
          std::to_string(b.verbose));
    extra_args c{};
    check("count: 聚簇 + 长选项混着来",
          run_line("-vv --verbose", c) == error::ok && c.verbose == 3, std::to_string(c.verbose));
  }
  {
    extra_args a{};
    check("alias: 长别名", run_line("--outfile x.bin", a) == error::ok && a.output != nullptr &&
                               std::string(a.output) == "x.bin");
    extra_args b{};
    check("alias: 正式长名照常", run_line("--output y.bin", b) == error::ok && b.output != nullptr);
    extra_args c{};
    check("alias: 短别名", run_line("-F f.txt", c) == error::ok && c.file != nullptr &&
                               std::string(c.file) == "f.txt");
  }
  {
    extra_args a{};
    check("delim: 一个取值切成多项",
          run_line("--tag=a,b,c", a) == error::ok && a.tags.size() == 3 && a.tags[2] == "c",
          std::to_string(a.tags.size()));
    extra_args b{};
    check("delim: 与重复给混用",
          run_line("--tag=a,b --tag c", b) == error::ok && b.tags.size() == 3, std::to_string(b.tags.size()));
    extra_args c{};
    check("delim: 空项如实保留", run_line("--tag=a,,b", c) == error::ok && c.tags.size() == 3 &&
                                     c.tags[1].empty());
  }
  {
    extra_args a{};
    check("hyphen: 取值可以以 - 开头", run_line("--raw -weird-value", a) == error::ok &&
                                          a.raw != nullptr && std::string(a.raw) == "-weird-value");
    extra_args b{};
    check("没有 hyphen 时仍然拒绝", run_line("--output -x", b) == error::missing_value);
  }
  {
    extra_args a{};
    check("trailing: 余下的 token 全归它（连 -x 也算值）",
          run_line("in.txt -x --not-an-option", a) == error::ok && a.rest.size() == 3 &&
              a.rest[1] == "-x" && a.rest[2] == "--not-an-option",
          std::to_string(a.rest.size()));
  }
#if defined(ECLI_TEST_HAS_OPTIONAL)
  {
    extra_args a{};
    check("optional: 没给就是空", run_line("", a) == error::ok && !a.opt.has_value());
    extra_args b{};
    check("optional: 给了就是 Some", run_line("--opt 7", b) == error::ok && b.opt.has_value() &&
                                         *b.opt == 7);
    extra_args c{};
    check("optional<std::string>", run_line("--opt2 bob", c) == error::ok && c.opt2.has_value() &&
                                        *c.opt2 == "bob");
    extra_args d{};
    check("optional: 取值不合法照常报错", run_line("--opt abc", d) == error::invalid_value);
  }
#endif
  {
    extra_args a{};
    check("-V 请求版本", run_line("-V", a) == error::version_requested);
    extra_args b{};
    check("--version 请求版本", run_line("--version", b) == error::version_requested);
    char buf[64];
    write_version("app", "1.2.3", buf, sizeof(buf));
    check("write_version 文本", std::string(buf) == "app 1.2.3\n", buf);
  }
  {
    // 别名不出现在帮助里（clap 的 alias 语义），正式名照常出现
    char buf[1024];
    write_help<extra_args>("app", "batch1", buf, sizeof(buf));
    const std::string help(buf);
    check("帮助里有正式长名", contains(help, "--output"));
    check("帮助里没有别名", !contains(help, "outfile"), help);
    check("帮助里有 count 的 --verbose", contains(help, "--verbose"));
  }
}

// ---------------------------------------------------------------------------
// 第二批
// ---------------------------------------------------------------------------
static void check_batch2() {
  {
    rel_args a{};
    a.a = 1;
    error_info info{};
    check("requires: 缺依赖报错", run_line("--b 5", a, &info) == error::missing_dependency);
    check("requires: 指向依赖项", info.other_index == 0, std::to_string(info.other_index));
    check("requires: 失败不动原对象", a.a == 1 && a.b == 0);
  }
  {
    rel_args a{};
    // 注意：rel_args 还带着 cfg(group_any) 那两条全局约束，所以这里一并给上
    check("requires: 依赖给齐就过",
          run_line("--a 1 --b 5 --automatic --p", a) == error::ok && a.b == 5);
  }
  {
    rel_args a{};
    error_info info{};
    check("conflicts: 互斥同时给", run_line("--k --m --automatic --p", a, &info) == error::conflict);
    check("conflicts: 指向另一项", info.other_index != npos && info.other_index != info.option_index);
  }
  {
    rel_args a{};
    check("conflicts: 只给一个没问题", run_line("--k --automatic --p", a) == error::ok && a.k);
  }
  {
    rel_args a{};
    check("group: 同组互斥", run_line("--x --y --automatic --p", a) == error::conflict);
  }
  {
    rel_args a{};
    check("group: 同组只给一个", run_line("--x --automatic --p", a) == error::ok && a.x);
  }
  {
    rel_args a{};
    check("group_any: 一个都不给就报必填", run_line("--automatic", a) == error::missing_required);
  }
  {
    rel_args a{};
    check("group_any: 给一个就够", run_line("--q --automatic", a) == error::ok && a.q);
  }
  {
    rel_args a{};
    check("unless: 没给 --automatic 时 --cfg 必填", run_line("--p", a) == error::missing_required);
    rel_args b{};
    check("unless: 给了 --automatic 就不必填", run_line("--p --automatic", b) == error::ok &&
                                                   !b.cfg);
    rel_args c{};
    check("unless: 或自己给了", run_line("--p --cfg 3", c) == error::ok && c.cfg == 3);
  }
  {
    // 报错文本要看得懂
    rel_args a{};
    error_info info{};
    const error e = run_line("--b 5", a, &info);
    char buf[512];
    write_error<rel_args>("app", e, info, buf, sizeof(buf));
    check("requires 报错文本", contains(buf, "'--b' requires '--a'"), buf);

    rel_args b{};
    error_info info2{};
    const error e2 = run_line("--x --y", b, &info2);
    write_error<rel_args>("app", e2, info2, buf, sizeof(buf));
    check("conflicts 报错文本", contains(buf, "conflicts with"), buf);

    rel_args c{};
    error_info info3{};
    const error e3 = run_line("--automatic", c, &info3);
    write_error<rel_args>("app", e3, info3, buf, sizeof(buf));
    check("missing_required 报错文本", contains(buf, "missing required option"), buf);
  }
}

int main() {
  check_batch1();
  check_batch2();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
