/**
 ******************************************************************************
 * @file           : ecli_manual_examples.cpp
 * @brief          : 手册里的示例代码必须有可编译版本（防止文档腐烂）
 * @attention      : 对应文档 docs/libs/ECLI-使用手册.md。手册里出现的每个代码块都在这里
 *                   编译并跑断言；分段注释「// ---- 手册 x.y ----」与手册小节一一对应。
 *                   手册里的片段若与实现脱节，这里先红。
 *                   只用标准库 + ETL 类型，宿主配置下编译运行（g++ -std=c++17 -O2 -Wall -Wextra）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/optional.h>
#include <middleware/etl/string.h>
#include <middleware/etl/string_view.h>
#include <middleware/etl/vector.h>

#include <ecli/cli.hpp>
#include <ecli/command.hpp>
#include <ecli/elog_reply.hpp>

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace ecli;

// ============================================================================
// 断言辅助（照抄 tests/ecli_command_check.cpp 的写法，不引任何测试框架）
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

static void check_text(const char *what, std::string_view actual, std::string_view wanted) {
  check(what, actual == wanted,
        "actual=[" + std::string(actual) + "] wanted=[" + std::string(wanted) + "]");
}

static bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

// 收回复用的缓冲（真机上就是 UART / RTT 的写函数）
static std::string g_reply_text;
static void raw_write(const char *data, std::size_t size) { g_reply_text.append(data, size); }

// elog 的 sink 形状：bool(const char*, size_t, void*)
struct sink_state {
  std::string text;
};
static bool elog_collect(const char *data, std::size_t size, void *user_data) {
  static_cast<sink_state *>(user_data)->text.append(data, size);
  return true;
}
static bool elog_into_reply_text(const char *data, std::size_t size, void *) {
  g_reply_text.append(data, size);
  return true;
}

// ============================================================================
// ---- 手册 2 第一个能跑的例子 ----
// ============================================================================
E_FMT_DERIVE(struct args {
  [[efmt::arg(short, long, help = "verbose output")]]       bool verbose = false;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]] int level = 3;
  [[efmt::arg(pos = "1", help = "input file")]]             etl::string<32> input;
}, Cli);

// 手册里这段直接写在 main 里（std::printf + return）；测试里抽成函数、把输出收进 out，
// 其余逐字相同。
static int first_app(int argc, const char *const *argv, std::string &out) {
  args a{};
  error_info info{};
  const error e = parse(argc, argv, a, &info);

  if (e == error::help_requested) {   // -h / --help：不是失败，打帮助后正常退出
    char text[512];
    write_help<args>("app", "我的第一个命令行工具", text, sizeof(text));
    out = text;
    return 0;
  }
  if (e != error::ok) {               // 真错：写进缓冲区，怎么输出由你决定
    char text[512];
    write_error<args>("app", e, info, text, sizeof(text));
    out = text;
    return 1;
  }
  out = "verbose=" + std::to_string(a.verbose ? 1 : 0) + " level=" + std::to_string(a.level) +
        " input=" + a.input.c_str() + "\n";
  return 0;
}

// 手册 2 里那份【完整可复制运行】的 main：原样编译一遍（[[maybe_unused]]：测试只验它能编过，
// 运行走上面那个把输出收进 out 的版本）。手册里就是把函数名换成 main、printf 之前那两行
// out = text; 换成 std::printf("%s", text);
[[maybe_unused]] static int first_app_printf(int argc, char **argv) {
  args a{};
  error_info info{};
  const error e = parse(argc, argv, a, &info);

  if (e == error::help_requested) {
    char text[512];
    write_help<args>("app", "我的第一个命令行工具", text, sizeof(text));
    std::printf("%s", text);
    return 0;
  }
  if (e != error::ok) {
    char text[512];
    write_error<args>("app", e, info, text, sizeof(text));
    std::printf("%s", text);
    return 1;
  }
  std::printf("verbose=%d level=%d input=%s\n", a.verbose ? 1 : 0, a.level, a.input.c_str());
  return 0;
}

static void check_first_example() {
  std::string out;
  {
    const char *argv[] = {"app", "-v", "--level", "7", "in.txt"};
    check("手册 2：argv 解析成功", first_app(5, argv, out) == 0, out);
    check_text("手册 2：取值正确", out, "verbose=1 level=7 input=in.txt\n");
  }
  {
    const char *argv[] = {"app", "-h"};
    check("手册 2：-h 走帮助分支（退 0）", first_app(2, argv, out) == 0, out);
    check("手册 2：帮助里有 usage", contains(out, "usage: app"), out);
  }
  {
    const char *argv[] = {"app", "--level=abc"};
    check("手册 2：坏取值退 1", first_app(2, argv, out) == 1, out);
    check("手册 2：报错文本带取值与选项",
          contains(out, "invalid value 'abc'") && contains(out, "--level"), out);
  }
}

// ============================================================================
// ---- 手册 3.1 声明即推导 ----
// ============================================================================
// 只标 Debug 的类型：不是命令行参数（反例见 tests/ecli_compile_fail_caps.cpp）
E_FMT_DERIVE(struct plain {
  int x = 0;
}, Debug);

static_assert(is_cli_args_v<args>, "标了 Cli 才能解析");
static_assert(!is_cli_args_v<plain>, "没标 Cli 的类型不是命令行参数");
static_assert(option_count<args>() == 3);
static_assert(option<args>(0).long_name == "verbose");
static_assert(option<args>(0).short_name == 'v');
static_assert(!option<args>(0).takes_value, "bool 是开关，不收取值");
static_assert(option<args>(2).position == 1, "pos = \"1\" 是第 1 个位置参数");

static void check_derivation() {
  check_text("手册 3.1：field 就是字段名", option<args>(1).field, "level");
  check_text("手册 3.1：help 原样登记", option<args>(0).help, "verbose output");
  check("手册 3.1：命名选项的 position 是 0", option<args>(0).position == 0);
  check("手册 3.1：越界下标给空规格，不崩", !option<args>(99).required);
}

// ============================================================================
// ---- 手册 3.2 字段标签全表 ----
// ============================================================================
E_FMT_DERIVE(struct tag_args {
  [[efmt::arg(short, long, count, help = "计数开关：-vvv = 3")]]   int verbose = 0;
  [[efmt::arg(short = "o", long = "output", help = "输出文件")]]   etl::string<16> out;
  [[efmt::arg(long = "name", alias = "moniker")]]                 etl::string<16> name;
  [[efmt::arg(short = "f", short_alias = "F", long = "file")]]    etl::string<16> file;
  [[efmt::arg(long = "tag", delim = ",", help = "a,b,c")]]        std::vector<std::string> tags;
  [[efmt::arg(long = "raw", hyphen, help = "取值可以以 - 开头")]]  etl::string<16> raw;
  [[efmt::arg(long = "mode", required, help = "必填项")]]          etl::string<8> mode;
  [[efmt::arg(pos = "1", help = "位置参数")]]                      etl::string<16> input;
  [[efmt::arg(skip)]]                                             int internal = 7;   // 不进命令行
}, Cli);

// trailing 只能标在【可重复的位置参数】上，而且它一拿到第一个 token 就把余下的全收走
E_FMT_DERIVE(struct trail_args {
  [[efmt::arg(pos = "1", trailing, help = "余下 token 全收")]] std::vector<std::string> rest;
}, Cli);

static_assert(option_count<tag_args>() == 9);
static_assert(option<tag_args>(0).count, "count 标签生效");
static_assert(!option<tag_args>(0).takes_value, "count 是开关，不收值");
static_assert(option<tag_args>(2).long_alias == "moniker");
static_assert(option<tag_args>(3).short_alias == 'F');
static_assert(option<tag_args>(4).delim == ',');
static_assert(option<tag_args>(5).hyphen);
static_assert(option<tag_args>(6).required);
static_assert(option<tag_args>(7).position == 1);
static_assert(option<tag_args>(8).skip, "skip 字段不进命令行");
static_assert(option<trail_args>(0).trailing && option<trail_args>(0).repeatable);

// needs / conflicts / unless / group / group_any（名字写字段名或长选项名都认）
E_FMT_DERIVE(struct rel_args {
  [[efmt::arg(long = "a", help = "基座")]]                             int a = 0;
  [[efmt::arg(long = "b", needs = "a", help = "依赖 --a")]]            int b = 0;
  [[efmt::arg(long = "x", group = "net")]]                             bool x = false;
  [[efmt::arg(long = "y", group = "net")]]                             bool y = false;
  [[efmt::arg(long = "p", group_any = "src")]]                         bool p = false;
  [[efmt::arg(long = "q", group_any = "src")]]                         bool q = false;
  [[efmt::arg(long = "cfg", unless = "automatic", help = "除非 auto")]] int cfg = 0;
  [[efmt::arg(long = "automatic")]]                                    bool auto_mode = false;
  [[efmt::arg(long = "k", conflicts = "m", help = "别和 --m 一起给")]]  bool k = false;
  [[efmt::arg(long = "m")]]                                            bool m = false;
}, Cli);

static_assert(option<rel_args>(1).requires_mask == (1u << 0), "needs = \"a\" 化成位掩码");
static_assert(option<rel_args>(2).conflicts_mask == (1u << 3), "group = \"net\" 展开成互斥掩码");
static_assert(option<rel_args>(4).unless_mask == (1u << 5), "group_any 展开成 unless 掩码");

static char g_scratch[ECLI_MAX_LINE];

template <typename T>
static error parse_line(const char *line, T &out, error_info *info = nullptr) {
  return parse(line, out, g_scratch, sizeof(g_scratch), info);
}

static void check_tags() {
  {
    tag_args a{};
    check_error("手册 3.2：count -vvv = 3", parse_line("-vvv --mode m", a), error::ok);
    check("手册 3.2：count 累加", a.verbose == 3, std::to_string(a.verbose));
  }
  {
    tag_args a{};
    check_error("手册 3.2：隐藏长别名命中",
                parse_line("--moniker x --mode m --output o", a), error::ok);
    check_text("手册 3.2：别名写进同一字段", a.name.c_str(), "x");
    check_text("手册 3.2：正式长名照常", a.out.c_str(), "o");
  }
  {
    tag_args a{};
    check_error("手册 3.2：隐藏短别名", parse_line("-F f.txt --mode m", a), error::ok);
    check_text("手册 3.2：短别名写进 file", a.file.c_str(), "f.txt");
  }
  {
    tag_args a{};
    check_error("手册 3.2：delim 一个取值切三项", parse_line("--mode m --tag=a,b,c", a), error::ok);
    check("手册 3.2：delim 结果", a.tags.size() == 3 && a.tags[2] == "c",
          std::to_string(a.tags.size()));
  }
  {
    tag_args a{};
    check_error("手册 3.2：hyphen 放行 - 开头的取值", parse_line("--mode m --raw -weird", a),
                error::ok);
    check_text("手册 3.2：hyphen 取值", a.raw.c_str(), "-weird");
    tag_args b{};
    check_error("手册 3.2：没有 hyphen 就拒绝", parse_line("--mode m --output -x", b),
                error::missing_value);
  }
  {
    trail_args a{};
    check_error("手册 3.2：trailing 收走余下 token（含 -x）",
                parse_line("in.txt -x --not-an-option", a), error::ok);
    check("手册 3.2：trailing 结果",
          a.rest.size() == 3 && a.rest[0] == "in.txt" && a.rest[2] == "--not-an-option",
          std::to_string(a.rest.size()));
  }
  {
    tag_args a{};
    check_error("手册 3.2：required 没给报错", parse_line("in.txt", a), error::missing_required);
    check("手册 3.2：失败不动原对象", a.internal == 7 && a.tags.empty());
    check_error("手册 3.2：skip 字段不可解析", parse_line("--mode m --internal 5", a),
                error::unknown_option);
  }
  {
    char buf[1024];
    write_help<tag_args>("app", "tags", buf, sizeof(buf));
    const std::string help(buf);
    check("手册 3.2：帮助里有正式名",
          contains(help, "--output") && contains(help, "-v, --verbose"), help);
    check("手册 3.2：帮助里没有别名",
          !contains(help, "moniker") && !contains(help, "  -F"), help.substr(0, 80));
    check("手册 3.2：帮助里没有 skip 字段", !contains(help, "internal"), help.substr(0, 80));
  }
  {
    rel_args a{};
    error_info info{};
    check_error("手册 3.2：needs 缺依赖", parse_line("--b 5", a, &info), error::missing_dependency);
    check("手册 3.2：needs 指向依赖项", info.other_index == 0, std::to_string(info.other_index));
    check_error("手册 3.2：依赖给齐就过", parse_line("--a 1 --b 5 --automatic --p", a), error::ok);
  }
  {
    rel_args a{};
    check_error("手册 3.2：conflicts 同时给", parse_line("--k --m --automatic --p", a),
                error::conflict);
    check_error("手册 3.2：group 同组互斥", parse_line("--x --y --automatic --p", a),
                error::conflict);
  }
  {
    rel_args a{};
    check_error("手册 3.2：group_any 一个都不给", parse_line("--automatic", a),
                error::missing_required);
    check_error("手册 3.2：group_any 给一个就够", parse_line("--q --automatic", a), error::ok);
  }
  {
    rel_args a{};
    check_error("手册 3.2：unless 没给自动项就必填", parse_line("--p", a), error::missing_required);
    check_error("手册 3.2：unless 给了自动项就不必填", parse_line("--p --automatic", a), error::ok);
    check_error("手册 3.2：unless 自己给了也行", parse_line("--p --cfg 3", a), error::ok);
  }
  {
    rel_args a{};
    error_info info{};
    char buf[512];
    const error e = parse_line("--b 5", a, &info);
    write_error<rel_args>("app", e, info, buf, sizeof(buf));
    check("手册 3.2：关系报错文本", contains(buf, "'--b' requires '--a'"), buf);
  }
}

// ============================================================================
// ---- 手册 3.3 取值类型全表 ----
// ============================================================================
E_FMT_DERIVE_ENUM(enum class mode {
  fast,
  slow = 5,
  err = -1
});

// 类型名里的顶层逗号会把 E_FMT_DERIVE 的第一个参数切断 —— 先 typedef 消掉
using int2 = etl::vector<int, 2>;

E_FMT_DERIVE(struct type_args {
  [[efmt::arg(long = "flag")]]  bool flag = false;               // 开关
  [[efmt::arg(long = "dec")]]   int dec = 0;                     // 十进制 / 0x / 0b / 负数
  [[efmt::arg(long = "hex")]]   unsigned hex = 0;
  [[efmt::arg(long = "mode")]]  mode m = mode::fast;             // 枚举名或数字
  [[efmt::arg(long = "ratio")]] double ratio = 0.0;              // 1.5 / -1.5e2
  [[efmt::arg(long = "name")]]  char name[8] = "";               // 定长字符数组（拷贝）
  [[efmt::arg(long = "text")]]  std::string text;                // 动态字符串
  [[efmt::arg(long = "fixed")]] etl::string<8> fixed;            // 定长字符串（装不下报错）
  [[efmt::arg(long = "view")]]  std::string_view view;           // 零拷贝别名
  [[efmt::arg(long = "opt")]]   std::optional<int> opt;          // 给了才是 Some
  [[efmt::arg(long = "eopt")]]  etl::optional<int> eopt;
  [[efmt::arg(long = "tag")]]   std::vector<std::string> tags;   // 可重复
  [[efmt::arg(long = "id")]]    int2 ids;                        // etl::vector<int, 2>
  [[efmt::arg(pos = "1")]]      etl::string_view first;          // 位置参数也能零拷贝
}, Cli);

static void check_value_types() {
  {
    type_args a{};
    const char *argv[] = {"app",  "--flag", "--dec", "0x10", "--hex",  "0b101",
                          "--mode", "slow",  "--ratio", "-1.5e2", "in"};
    check_error("手册 3.3：argv 各类型", parse(11, argv, a), error::ok);
    check("手册 3.3：bool 开关", a.flag);
    check("手册 3.3：0x 十六进制", a.dec == 16, std::to_string(a.dec));
    check("手册 3.3：0b 二进制", a.hex == 5u, std::to_string(a.hex));
    check("手册 3.3：枚举按名字", a.m == mode::slow);
    check("手册 3.3：负指数浮点", a.ratio == -150.0);
    check("手册 3.3：位置参数零拷贝", a.first == "in");
  }
  {
    type_args a{};
    check_error("手册 3.3：char[N] / etl::string / 可重复容器 / etl::vector",
                parse_line("--name abc --fixed fix --tag x --tag y --id 3 --id 4", a), error::ok);
    check("手册 3.3：char[N] 拷贝", std::strcmp(a.name, "abc") == 0, a.name);
    check_text("手册 3.3：etl::string", a.fixed.c_str(), "fix");
    check("手册 3.3：可重复容器", a.tags.size() == 2 && a.tags[1] == "y");
    check("手册 3.3：etl::vector<int, 2>", a.ids.size() == 2 && a.ids[1] == 4);
  }
  {
    type_args a{};
    check_error("手册 3.3：bool 也能收取值", parse_line("--flag=false", a), error::ok);
    check("手册 3.3：--flag=false 置假", !a.flag);
    type_args b{};
    check_error("手册 3.3：--no-<flag> 关掉开关", parse_line("--no-flag", b), error::ok);
    check("手册 3.3：--no-flag 置假", !b.flag);
  }
  {
    type_args a{};
    check_error("手册 3.3：枚举也收数字", parse_line("--mode 5", a), error::ok);
    check("手册 3.3：枚举数字", a.m == mode::slow, std::to_string(static_cast<int>(a.m)));
    type_args b{};
    check_error("手册 3.3：枚举认不出就报错", parse_line("--mode turbo", b), error::invalid_value);
    check_error("手册 3.3：整数溢出", parse_line("--dec 99999999999", b), error::invalid_value);
    check_error("手册 3.3：负数进无符号", parse_line("--hex=-1", b), error::invalid_value);
  }
  {
    type_args a{};
    check_error("手册 3.3：char[8] 装不下（不静默截断）", parse_line("--name 12345678", a),
                error::value_too_long);
    check("手册 3.3：装不下时不写半截", a.name[0] == '\0');
    check_error("手册 3.3：etl::string<8> 装不下", parse_line("--fixed 123456789", a),
                error::value_too_long);
    check_error("手册 3.3：定长容器塞满", parse_line("--id 1 --id 2 --id 3", a),
                error::too_many_values);
  }
  {
    type_args a{};
    check("手册 3.3：optional 没给就是空", !a.opt.has_value() && !a.eopt.has_value());
    check_error("手册 3.3：optional 给了就是 Some", parse_line("--opt 7 --eopt 9", a), error::ok);
    check("手册 3.3：optional 取值",
          a.opt.has_value() && *a.opt == 7 && a.eopt.has_value() && *a.eopt == 9);
    type_args b{};
    check_error("手册 3.3：optional 取值不合法照常报错", parse_line("--opt abc", b),
                error::invalid_value);
  }
  {
    type_args a{};
    check_error("手册 3.3：string_view 零拷贝", parse_line("--view tok", a), error::ok);
    check_text("手册 3.3：string_view 内容", a.view, "tok");
  }
}

// ============================================================================
// ---- 手册 3.4 命令行语法（argv 与一行文本）----
// ============================================================================
static void check_syntax() {
  {
    const char *argv[] = {"app", "-vl", "7", "in.txt"};
    args a{};
    check_error("手册 3.4：短选项聚簇 -vl 7", parse(4, argv, a), error::ok);
    check("手册 3.4：聚簇结果", a.verbose && a.level == 7);
  }
  {
    const char *argv[] = {"app", "-l=9", "in.txt"};
    args a{};
    check_error("手册 3.4：-l=9 去等号", parse(3, argv, a), error::ok);
    check("手册 3.4：-l=9 取值", a.level == 9, std::to_string(a.level));
  }
  {
    const char *argv[] = {"app", "-l7", "in.txt"};
    args a{};
    check_error("手册 3.4：-l7 粘连取值", parse(3, argv, a), error::ok);
    check("手册 3.4：-l7 取值", a.level == 7);
  }
  {
    const char *argv[] = {"app", "--level", "-3", "in.txt"};
    args a{};
    check_error("手册 3.4：负数不当选项", parse(4, argv, a), error::ok);
    check("手册 3.4：负数值进字段", a.level == -3, std::to_string(a.level));
  }
  {
    const char *argv[] = {"app", "--", "--level=1"};
    args a{};
    check_error("手册 3.4：-- 之后全是位置参数", parse(3, argv, a), error::ok);
    check_text("手册 3.4：-- 之后原样进位置参数", a.input.c_str(), "--level=1");
    check("手册 3.4：-- 之后的选项没被解析", a.level == 3);
  }
  {
    args a{};
    check_error("手册 3.4：一行文本：引号包空格", parse_line("-v \"in put.txt\"", a), error::ok);
    check_text("手册 3.4：引号里的空格算一个 token", a.input.c_str(), "in put.txt");
    args b{};
    check_error("手册 3.4：一行文本：反斜杠转义空格", parse_line("in\\ put.txt", b), error::ok);
    check_text("手册 3.4：转义结果", b.input.c_str(), "in put.txt");
    args c{};
    check_error("手册 3.4：一行文本：引号没闭合", parse_line("-v \"abc", c), error::bad_quote);
  }
}

// ============================================================================
// ---- 手册 3.5 帮助 / 版本 / 报错 ----
// ============================================================================
static void check_texts() {
  char buf[1024];

  const std::size_t need_usage = write_usage<args>("app", nullptr, 0);   // buf = nullptr：只量长度
  const std::size_t need_help = write_help<args>("app", "我的工具", nullptr, 0);
  check("手册 3.5：usage 长度可量", need_usage > 0);
  check("手册 3.5：help 比 usage 长", need_help > need_usage);

  write_usage<args>("app", buf, sizeof(buf));
  const std::string usage(buf);
  check("手册 3.5：usage 前缀", contains(usage, "usage: app"), usage);
  check("手册 3.5：usage 有 [options]", contains(usage, "[options]"), usage);
  check("手册 3.5：usage 列出位置参数", contains(usage, "[input]"), usage);

  write_help<args>("app", "我的工具", buf, sizeof(buf));
  const std::string help(buf);
  check("手册 3.5：help 带 about", contains(help, "我的工具"));
  check("手册 3.5：help 短 + 长", contains(help, "-v, --verbose"), help);
  check("手册 3.5：help 取值占位", contains(help, "--level <value>"), help);
  check("手册 3.5：help 内置 -h",
        contains(help, "-h, --help") && contains(help, "show this help"), help);

  {
    args a{};
    error_info info{};
    const error e = parse_line("--level=abc", a, &info);
    write_error<args>("app", e, info, buf, sizeof(buf));
    const std::string text(buf);
    check("手册 3.5：报错前缀", contains(text, "error: "), text);
    check("手册 3.5：报错带取值与选项",
          contains(text, "invalid value 'abc'") && contains(text, "--level"), text);
    check("手册 3.5：报错带 usage", contains(text, "usage: app"), text);
  }
  {
    args a{};
    error_info info{};
    check_error("手册 3.5：-h = help_requested", parse_line("-h", a, &info), error::help_requested);
    check_error("手册 3.5：--help = help_requested", parse_line("--help", a, &info),
                error::help_requested);
    check_error("手册 3.5：-V = version_requested", parse_line("-V", a, &info),
                error::version_requested);
    check_error("手册 3.5：--version = version_requested", parse_line("--version", a, &info),
                error::version_requested);
    check("手册 3.5：请求帮助不动字段", a.level == 3 && a.input.empty());
  }
  {
    write_version("app", "1.2.3", buf, sizeof(buf));
    check_text("手册 3.5：版本行", buf, "app 1.2.3\n");
  }
  {
    // snprintf 语义：缓冲区不够只写 cap-1 个字符、返回完整长度、永远 NUL 结尾
    char small[8];
    std::memset(small, 0x7F, sizeof(small));
    const std::size_t need = write_usage<args>("app", small, sizeof(small) - 1);
    check("手册 3.5：截断返回完整长度", need == need_usage);
    check("手册 3.5：截断仍 NUL 结尾", small[sizeof(small) - 2] == '\0');
    check("手册 3.5：截断不越界", small[sizeof(small) - 1] == 0x7F);
  }
#if EFMT_ENABLE_DYNAMIC_STRING
  {
    args a{};
    error_info info{};
    const error e = parse_line("--nope", a, &info);
    check("手册 3.5：宿主便利版 usage", contains(usage_string<args>("app"), "usage: app"));
    check("手册 3.5：宿主便利版 help",
          contains(help_string<args>("app", "我的工具"), "-v, --verbose"));
    check("手册 3.5：宿主便利版 error",
          contains(error_string<args>("app", e, info), "unknown option"));
    check_text("手册 3.5：宿主便利版 version", version_string("app", "1.2.3"), "app 1.2.3\n");
  }
#endif
}

// ============================================================================
// ---- 手册 3.6 三个入口：argv / 一行文本 / token 表 ----
// ============================================================================
static void check_entries() {
  {   // 入口一：argv（零拷贝指向 argv）
    const char *argv[] = {"app", "--level", "9", "in.txt"};
    const token_list tokens = from_argv(4, argv);
    args a{};
    check_error("手册 3.6：from_argv + parse(token_list)", parse(tokens, a), error::ok);
    check("手册 3.6：token 表入口取值", a.level == 9 && a.input == "in.txt");
  }
  {   // 入口二：一行文本 + 调用方给的 scratch
    char scratch[ECLI_MAX_LINE];
    const token_list tokens = tokenize("-v --level=8 \"in put.txt\"", scratch, sizeof(scratch));
    check("手册 3.6：tokenize 数量", tokens.count == 3, std::to_string(tokens.count));
    check("手册 3.6：词法正常", !tokens.overflow && !tokens.bad_quote);
    args a{};
    error_info info{};
    check_error("手册 3.6：一行文本入口", parse(tokens, a, &info), error::ok);
    check("手册 3.6：一行文本取值", a.verbose && a.level == 8 && a.input == "in put.txt");
  }
  {   // 入口三：一行文本的便利版（自带栈缓冲：不给 error_info，也别留引号里的值）
    args a{};
    check_error("手册 3.6：一行文本便利版", parse("--level 6 in.txt", a), error::ok);
    check("手册 3.6：便利版取值", a.level == 6 && a.input == "in.txt");
  }
  {   // 行装配器：字节 → 一行（串口 / 蓝牙 / 键盘的形状）
    line_reader<128> rx;
    const char *bytes = "status -v\n";
    bool ready = false;
    for (const char *p = bytes; *p != 0 && !ready; ++p) ready = rx.put(*p);
    check("手册 3.6：换行才算一行", ready);
    check_text("手册 3.6：行内容", rx.line(), "status -v");
    rx.clear();
    check("手册 3.6：空行不算命令", !rx.put('\r') && !rx.put('\n'));
  }
  {
    line_reader<8> small;
    for (int i = 0; i < 32; ++i) (void)small.put('z');
    check("手册 3.6：超长置 overflow()", small.overflow());
  }
  {
    // token 数超上限（ECLI_MAX_TOKENS = 16）
    args a{};
    std::string many = "--level 1";
    for (int i = 0; i < 20; ++i) many += " in";
    check_error("手册 3.6：token 超上限", parse_line(many.c_str(), a), error::too_many_tokens);
  }
}

// ============================================================================
// ---- 手册 3.7 命令表 ----
// ============================================================================
E_FMT_DERIVE(struct status_args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct wifi_args {
  [[efmt::arg(short = "s", long = "ssid", required, help = "network name")]] etl::string<16> ssid;
  [[efmt::arg(short = "p", long = "pass", help = "password")]]               etl::string<16> pass;
}, Cli);

static std::string g_calls;   // 记录处理函数被调用的轨迹

static void status_run(const status_args &a, reply out) {
  g_calls += a.verbose ? "[status -v]" : "[status]";
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void wifi_summary_run(const status_args &, reply out) {
  g_calls += "[wifi]";
  out.put_lit("wifi: 1 ap\n");
}

static void wifi_set_run(const wifi_args &a, reply out) {
  g_calls += "[wifi set " + std::string(a.ssid.c_str()) + "/" + std::string(a.pass.c_str()) + "]";
  out.put_lit("ok\n");
}

static constexpr command kCommands[] = {
    {"status", "show link status", command_of<status_args, status_run>(), help_of<status_args>()},
    {"wifi", "wifi summary", command_of<status_args, wifi_summary_run>(), help_of<status_args>()},
    {"wifi set", "set ssid / password", command_of<wifi_args, wifi_set_run>(), help_of<wifi_args>()},
};

static_assert(kCommands[2].name == "wifi set", "命令表是 constexpr：编译期就能查");

// 跑一行命令：回复收进 text
template <std::size_t N>
static error run_line(const command (&table)[N], const char *line, std::string &text) {
  char scratch[ECLI_MAX_LINE];
  char buf[1024];
  buffer_reply b{buf, sizeof(buf), 0};
  const error e = dispatch(table, line, scratch, sizeof(scratch), b.as_reply());
  const std::size_t n = b.used < sizeof(buf) ? b.used : sizeof(buf) - 1;
  text.assign(buf, n);
  return e;
}

static void check_command_table() {
  std::string text;

  g_calls.clear();
  check_error("手册 3.7：单命令", run_line(kCommands, "status", text), error::ok);
  check_text("手册 3.7：处理函数回话", text, "link: up\n");
  check("手册 3.7：处理函数被调用", g_calls == "[status]", g_calls);

  g_calls.clear();
  check_error("手册 3.7：子命令 + 选项", run_line(kCommands, "wifi set -s mynet -p secret", text),
              error::ok);
  check("手册 3.7：子命令参数", g_calls == "[wifi set mynet/secret]", g_calls);

  g_calls.clear();
  check_error("手册 3.7：最长前缀命中短命令", run_line(kCommands, "wifi", text), error::ok);
  check("手册 3.7：wifi 走概览", g_calls == "[wifi]" && text == "wifi: 1 ap\n", g_calls + text);

  g_calls.clear();
  check_error("手册 3.7：最长前缀命中长命令", run_line(kCommands, "wifi set -s n1", text),
              error::ok);
  check("手册 3.7：wifi set 走子命令", g_calls == "[wifi set n1/]", g_calls);

  check_error("手册 3.7：未知命令", run_line(kCommands, "nope", text), error::unknown_command);
  check("手册 3.7：未知命令带命令表",
        contains(text, "unknown command 'nope'") && contains(text, "commands:"), text);

  g_calls.clear();
  check_error("手册 3.7：必填项缺失", run_line(kCommands, "wifi set -p only", text),
              error::missing_required);
  check("手册 3.7：必填报错带 usage",
        contains(text, "missing required option '--ssid'") && contains(text, "usage: wifi set"),
        text);
  check("手册 3.7：报错不执行处理函数", g_calls.empty(), g_calls);
}

static void check_builtin_help() {
  std::string text;

  check_error("手册 3.7：空行 = 列命令", run_line(kCommands, "", text), error::help_requested);
  check("手册 3.7：列命令含表头与子命令",
        contains(text, "commands:") && contains(text, "wifi set"), text);
  check("手册 3.7：列命令含内置 help", contains(text, "help [command]"), text);

  for (const char *word : {"help", "-h", "--help", "?"}) {
    check_error("手册 3.7：内置帮助词", run_line(kCommands, word, text), error::help_requested);
  }
  check_error("手册 3.7：-V 请求版本", run_line(kCommands, "-V", text), error::version_requested);
  check_error("手册 3.7：--version 请求版本", run_line(kCommands, "--version", text),
              error::version_requested);

  check_error("手册 3.7：help <子命令>", run_line(kCommands, "help wifi set", text),
              error::help_requested);
  check("手册 3.7：给出子命令 usage 与选项表",
        contains(text, "usage: wifi set") && contains(text, "-s, --ssid") &&
            contains(text, "network name"),
        text);

  g_calls.clear();
  check_error("手册 3.7：命令自己的 -h", run_line(kCommands, "wifi set -h", text),
              error::help_requested);
  check("手册 3.7：-h 不执行处理函数", g_calls.empty(), g_calls);

  check_error("手册 3.7：help 未知命令", run_line(kCommands, "help nope", text),
              error::unknown_command);

  {   // write_command_list 也能单独调（自己选回哪条通道）
    char buf[512];
    buffer_reply b{buf, sizeof(buf), 0};
    const std::size_t need = write_command_list(kCommands, b.as_reply());
    check("手册 3.7：write_command_list 长度", need > 0 && need == b.used,
          std::to_string(need) + "/" + std::to_string(b.used));
    check("手册 3.7：write_command_list 内容",
          contains(buf, "commands:") && contains(buf, "status"), buf);
  }

  {   // token 表入口：同一个命令表
    char scratch2[ECLI_MAX_LINE];
    const token_list tokens = tokenize("status -v", scratch2, sizeof(scratch2));
    g_calls.clear();
    g_reply_text.clear();
    check_error("手册 3.7：token 表分发",
                dispatch(kCommands, tokens, reply_to<raw_write>()), error::ok);
    check("手册 3.7：token 表分发结果",
          g_calls == "[status -v]" && g_reply_text == "link: up (detail)\n", g_calls + g_reply_text);
  }
  {   // argv 入口：同一个命令表
    const char *argv[] = {"app", "wifi", "set", "-s", "n1", "-p", "p1"};
    char buf[256];
    buffer_reply b{buf, sizeof(buf), 0};
    g_calls.clear();
    check_error("手册 3.7：argv 分发", dispatch(kCommands, 7, argv, b.as_reply()), error::ok);
    check("手册 3.7：argv 分到子命令", g_calls == "[wifi set n1/p1]", g_calls);
  }
}

// ============================================================================
// ---- 手册 3.8 命令名模式段（:name / *rest）----
// ============================================================================
// 无参命令的占位类型：E_FMT_DERIVE 至少要有一个字段，skip 让它不进命令行
E_FMT_DERIVE(struct no_args {
  [[efmt::arg(skip)]] int unused = 0;
}, Cli);

E_FMT_DERIVE(struct set_args {
  [[efmt::arg(skip)]] int n = -1;              // 由 "level :n" 捕获注入（捕获名 = 字段名）
}, Cli);

E_FMT_DERIVE(struct log_args {
  [[efmt::arg(skip)]]         std::vector<std::string> rest;   // 由 "log *rest" 逐个 push
  [[efmt::arg(long = "tag")]] etl::string<16> tag;
}, Cli);

E_FMT_DERIVE(struct ssid_args {
  [[efmt::arg(skip)]] etl::string<16> ssid;    // 由 "net set :ssid" 捕获注入
}, Cli);

static std::string g_trace;

static void level_run(const set_args &a, reply out) {
  g_trace = "level:" + std::to_string(a.n);
  out.put_lit("ok\n");
}

// 想看原始捕获值就用三参数形式
static void log_run(const log_args &a, const params &p, reply out) {
  g_trace = "log:";
  for (const std::string &s : a.rest) g_trace += s + "|";
  g_trace += "caps=" + std::to_string(p.size()) + ":";
  for (std::size_t i = 0; i < p.size(); ++i) {
    g_trace += std::string(p.name_at(i)) + "(" + std::to_string(p.rest_count(i)) + ")";
  }
  g_trace += " get=" + std::string(p.get("rest", "(无)"));
  out.put_lit("done\n");
}

static void net_run(const no_args &, reply out) {
  g_trace = "net";
  out.put_lit("net: 概览\n");
}

static void net_set_run(const ssid_args &a, reply out) {
  g_trace = "ssid:" + std::string(a.ssid.c_str());
  out.put_lit("ok\n");
}

static constexpr command kPatternCommands[] = {
    {"level :n", "设置等级", command_of<set_args, level_run>(), help_of<set_args>()},
    {"log *rest", "记录一行", command_of<log_args, log_run>(), help_of<log_args>()},
    {"net", "网络概览（兜底）", command_of<no_args, net_run>(), help_of<no_args>()},
    {"net set :ssid", "设置 SSID", command_of<ssid_args, net_set_run>(), help_of<ssid_args>()},
};

// ---- 手册 3.8 附：params 的每个取值口都钉一遍 ----
static std::string g_params_dump;

static void probe_rest_run(const log_args &, const params &p, reply out) {
  g_params_dump = "count=" + std::to_string(p.count) + " size=" + std::to_string(p.size()) +
                  " capacity=" + std::to_string(params::capacity) +
                  " overflow=" + std::to_string(p.overflow ? 1 : 0) +
                  " spec=" + std::to_string(p.spec) + " name0=" + std::string(p.name_at(0)) +
                  " value0=" + std::string(p.value_at(0)) +
                  " is_rest0=" + std::to_string(p.is_rest(0) ? 1 : 0) +
                  " rest_count0=" + std::to_string(p.rest_count(0)) +
                  " rest_at00=" + std::string(p.rest_at(0, 0)) +
                  " has_rest=" + std::to_string(p.has("rest") ? 1 : 0) +
                  " has_tag=" + std::to_string(p.has("tag") ? 1 : 0) +
                  " get=" + std::string(p.get("rest", "(none)")) +
                  " fallback=" + std::string(p.get("nope", "(none)"));
  out.put_lit("probed\n");
}

static void probe_one_run(const set_args &a, const params &p, reply out) {
  g_params_dump = "is_rest0=" + std::to_string(p.is_rest(0) ? 1 : 0) +
                  " value0=" + std::string(p.value_at(0)) +
                  " rest_count0=" + std::to_string(p.rest_count(0)) +
                  " rest_at00=" + std::string(p.rest_at(0, 0)) +
                  " name0=" + std::string(p.name_at(0)) + " n=" + std::to_string(a.n);
  out.put_lit("probed\n");
}

// 两条命令各自独立的前缀：否则 "probe :n" 会以更高特异性抢走 "probe *rest" 的输入
static constexpr command kProbeCommands[] = {
    {"prest *rest", "参数探针（余下）", command_of<log_args, probe_rest_run>(), help_of<log_args>()},
    {"pnum :n", "参数探针（单值）", command_of<set_args, probe_one_run>(), help_of<set_args>()},
};

static void check_params_api() {
  std::string text;
  check_error("手册 3.8：params 探针（*rest）", run_line(kProbeCommands, "prest a b", text),
              error::ok);
  check_text("手册 3.8：*rest 的 params 取值口", g_params_dump,
             "count=1 size=1 capacity=8 overflow=0 spec=1 name0=rest value0=a is_rest0=1 "
             "rest_count0=2 rest_at00=a has_rest=1 has_tag=0 get=a fallback=(none)");

  check_error("手册 3.8：params 探针（:n）", run_line(kProbeCommands, "pnum 5", text), error::ok);
  check_text("手册 3.8：:name 的 params 取值口", g_params_dump,
             "is_rest0=0 value0=5 rest_count0=0 rest_at00= name0=n n=5");
}

static void check_pattern_commands() {
  std::string text;

  check_error("手册 3.8：:参数捕获 + 注入同名字段", run_line(kPatternCommands, "level 5", text),
              error::ok);
  check("手册 3.8：注入到 int 字段", g_trace == "level:5", g_trace);
  check_error("手册 3.8：:参数类型不对会报错", run_line(kPatternCommands, "level abc", text),
              error::invalid_value);
  check("手册 3.8：类型不对的报错带 usage", contains(text, "usage: level :n"), text.substr(0, 60));

  check_error("手册 3.8：*rest 注入容器", run_line(kPatternCommands, "log a b c", text), error::ok);
  check("手册 3.8：*rest 逐个 push + params 可查",
        g_trace == "log:a|b|c|caps=1:rest(3) get=a", g_trace);
  check_error("手册 3.8：*rest 收 0 个也命中", run_line(kPatternCommands, "log", text), error::ok);
  check("手册 3.8：*rest 空也算命中", g_trace == "log:caps=1:rest(0) get=", g_trace);

  check_error("手册 3.8：两段式子命令", run_line(kPatternCommands, "net set home", text),
              error::ok);
  check("手册 3.8：:ssid 捕获", g_trace == "ssid:home", g_trace);
  check_error("手册 3.8：裸 net 走兜底", run_line(kPatternCommands, "net", text), error::ok);
  check("手册 3.8：兜底命令", g_trace == "net", g_trace);

  g_trace.clear();
  check_error("手册 3.8：段数不够 → 落到更短的那条（余下 token 报错）",
              run_line(kPatternCommands, "net set", text), error::too_many_args);
  check("手册 3.8：段数不够时不执行处理函数", g_trace.empty(), g_trace);
  check("手册 3.8：报错带该命令的 usage", contains(text, "usage: net"), text.substr(0, 60));

  check_error("手册 3.8：help 前缀命中模式命令",
              run_line(kPatternCommands, "help net set", text), error::help_requested);
  check("手册 3.8：help 给出模式命令的 usage", contains(text, "usage: net set :ssid"), text);
  check_error("手册 3.8：help 未知命令", run_line(kPatternCommands, "help nope", text),
              error::unknown_command);
}

// ============================================================================
// ---- 手册 3.9 回复通道全表 ----
// ============================================================================
static void check_reply_channels() {
  char scratch[ECLI_MAX_LINE];

  {   // 1) reply_to<写函数>()：库直接调你的 (data, size)
    g_reply_text.clear();
    check_error("手册 3.9：reply_to<写函数>",
                dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to<raw_write>()),
                error::ok);
    check_text("手册 3.9：写函数收到原样字节", g_reply_text, "link: up\n");
  }
  {   // 2) buffer_reply：写进定长缓冲（snprintf 语义，永远 NUL 结尾）
    char buf[512];
    buffer_reply b{buf, sizeof(buf), 0};
    check_error("手册 3.9：buffer_reply",
                dispatch(kCommands, "status -v", scratch, sizeof(scratch), b.as_reply()),
                error::ok);
    check_text("手册 3.9：缓冲内容", buf, "link: up (detail)\n");
    check("手册 3.9：used = 完整长度", b.used == std::strlen(buf), std::to_string(b.used));
  }
  {   // 3) 空 reply{}：命令照跑，输出丢弃，不崩
    g_calls.clear();
    check_error("手册 3.9：空 reply 丢弃输出",
                dispatch(kCommands, "status", scratch, sizeof(scratch), reply{}), error::ok);
    check("手册 3.9：空 reply 仍执行处理函数", g_calls == "[status]", g_calls);
    check("手册 3.9：空 reply 的 valid()", !reply{}.valid());
  }
#if EFMT_ENABLE_STDIO
  check("手册 3.9：stdout_reply 有效", stdout_reply().valid());
#endif
#if EFMT_ENABLE_DYNAMIC_STRING
  {   // 4) string_reply(s)：追加到 std::string（宿主）
    std::string out;
    check_error("手册 3.9：string_reply",
                dispatch(kCommands, "wifi", scratch, sizeof(scratch), string_reply(out)),
                error::ok);
    check_text("手册 3.9：string_reply 内容", out, "wifi: 1 ap\n");
  }
#endif
  {   // 5) 一次绑定，多处使用：日志与命令回复复用同一条通道
    e_log::logger *lg = e_log::create_logger("manual-one-bind", e_log::make_sink(&elog_into_reply_text),
                                             e_log::level::debug);
    check("手册 3.9：create_logger 成功", lg != nullptr);
    if (lg != nullptr) {
      g_reply_text.clear();
      ELOG_LOGGER_INFO(*lg, "日志走这条通道 {}", 42);
      const std::size_t after_log = g_reply_text.size();
      check("手册 3.9：日志那一路带级别前缀", contains(g_reply_text, "[info]"), g_reply_text);

      check_error("手册 3.9：reply_to_logger 复用同一条通道",
                  dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to_logger(*lg)),
                  error::ok);
      check_text("手册 3.9：回复原样（无前缀、不补换行）", g_reply_text.substr(after_log),
                 "link: up\n");

      const std::size_t after_reply = g_reply_text.size();
      const reply via_default = reply_to_default_logger();
      check("手册 3.9：reply_to_default_logger 有效", via_default.valid());
      check("手册 3.9：默认 logger 就是第一个建的",
            dispatch(kCommands, "status", scratch, sizeof(scratch), via_default) == error::ok &&
                g_reply_text.substr(after_reply) == "link: up\n",
            g_reply_text.substr(after_reply));
    }
  }
  {   // 6) reply_to_sink(裸 sink)
    sink_state state;
    const e_log::sink out = e_log::make_sink(&elog_collect, &state);
    check_error("手册 3.9：reply_to_sink",
                dispatch(kCommands, "status", scratch, sizeof(scratch), reply_to_sink(out)),
                error::ok);
    check_text("手册 3.9：sink 收到原样字节", state.text, "link: up\n");
    check("手册 3.9：elog_stdout_reply 有效", elog_stdout_reply().valid());
  }
}

// ============================================================================
// ---- 手册 3.10 裁剪宏与代价 ----
// ============================================================================
// 下面断言的是【默认配置】的值；真改了宏（-DECLI_MAX_TOKENS=…）就得跟着改这里。
static_assert(ECLI_MAX_TOKENS == 16, "一条命令行最多几个 token");
static_assert(ECLI_MAX_LINE == 192, "去引号缓冲 / line_reader 行宽");
static_assert(ECLI_ENABLE_HELP == 1, "usage/help 文本开关");
static_assert(ECLI_REPLY_BUFFER == 384, "命令表帮助/报错的栈缓冲");
static_assert(ECLI_ENABLE_PATTERN_COMMANDS == 1, "命令名模式段开关");
static_assert(ECLI_MAX_CAPTURES == 8, "一次命令最多记几个捕获");
static_assert(params::capacity == ECLI_MAX_CAPTURES, "params 的容量就是这个宏");

// ============================================================================
// ---- 手册 4 串口命令行骨架（完整可抄）----
// ============================================================================
// 假的串口：写函数收字节（真机上换成 HAL_UART_Transmit）
static std::string g_uart_tx;
static bool uart_write(const char *data, std::size_t size, void *) {
  g_uart_tx.append(data, size);
  return true;
}

E_FMT_DERIVE(struct dev_status_args {
  [[efmt::arg(short, long, help = "细节")]] bool verbose = false;
}, Cli);

E_FMT_DERIVE(struct dev_ssid_args {
  [[efmt::arg(skip)]] etl::string<16> ssid;   // 由 "net set :ssid" 捕获
}, Cli);

E_FMT_DERIVE(struct dev_level_args {
  [[efmt::arg(skip)]] int n = -1;             // 由 "level :n" 捕获
}, Cli);

E_FMT_DERIVE(struct dev_log_args {
  [[efmt::arg(pos = "1", help = "trace/debug/info/warn/error/off")]] etl::string<8> level;
}, Cli);

static void run_dev_status(const dev_status_args &a, reply out) {
  out.put_lit(a.verbose ? "link: up (detail)\n" : "link: up\n");
}

static void run_dev_ssid(const dev_ssid_args &a, reply out) {
  ELOG_INFO("ssid 已切到 {}", a.ssid);   // 日志：带级别与来源前缀
  out.put_lit("ssid ok\n");              // 回复：原样字节
}

static void run_dev_level(const dev_level_args &a, reply out) {
  ELOG_INFO("level -> {}", a.n);
  out.put_lit("level ok\n");
}

static void run_dev_log(const dev_log_args &a, reply out) {
  e_log::logger *const lg = e_log::default_logger();
  if (lg == nullptr) {
    out.put_lit("no logger\n");
    return;
  }

  static const struct {
    const char *name;
    e_log::level value;
  } kLevels[] = {
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
      out.put_lit("不认识的级别：trace/debug/info/warn/error/off\n");
      return;
    }
    lg->set_level(*found);
  }

  ELOG_DEBUG("debug 行：级别比它高就不会出来");
  ELOG_WARN("warn 行：级别 <= warn 就出来（现在是 {}）", e_log::to_string(lg->current_level()));
  out.put_lit("ok\n");
}

static constexpr command kConsole[] = {
    {"status", "链路状态", command_of<dev_status_args, run_dev_status>(), help_of<dev_status_args>()},
    {"net set :ssid", "设置 SSID", command_of<dev_ssid_args, run_dev_ssid>(), help_of<dev_ssid_args>()},
    {"level :n", "设置等级", command_of<dev_level_args, run_dev_level>(), help_of<dev_level_args>()},
    {"log", "查看 / 切换日志级别", command_of<dev_log_args, run_dev_log>(), help_of<dev_log_args>()},
};

static constexpr const char *kVersion = "0.1.0";

// 每个输入源一个行缓冲；解析与执行不重入，都在主循环里串着来
static line_reader<128> g_rx;
static char g_line_scratch[ECLI_MAX_LINE];
static error g_last_error = error::ok;

static void console_boot() {
  // ★ 唯一的绑定点：通道在这里定义一次，日志与命令回复都复用它
  e_log::logger *app =
      e_log::create_logger("console", e_log::make_sink(&uart_write), e_log::level::debug);
  if (app != nullptr) {
    e_log::set_default_logger(*app);   // ELOG_* 与 reply_to_default_logger() 都走它
  }
}

static void on_uart_byte(char c) {
  if (!g_rx.put(c)) return;                       // 还没凑够一行
  const std::string_view line = g_rx.line();
  const reply out = reply_to_default_logger();    // 谁问的回给谁（这里只有一条串口）
  g_last_error = dispatch(kConsole, line, g_line_scratch, sizeof(g_line_scratch), out);
  if (g_last_error == error::version_requested) {
    char v[64];
    const std::size_t n = write_version("console", kVersion, v, sizeof(v));
    out.put(std::string_view(v, n < sizeof(v) ? n : sizeof(v) - 1));
  }
  g_rx.clear();
}

static void feed(const char *bytes) {
  for (const char *p = bytes; *p != 0; ++p) on_uart_byte(*p);
}

static void check_console_skeleton() {
  console_boot();
  check("手册 4：默认 logger 已绑好", e_log::default_logger() != nullptr);

  g_uart_tx.clear();
  feed("status -v\n");
  check_text("手册 4：回复原样进串口（无日志前缀）", g_uart_tx, "link: up (detail)\n");
  check_error("手册 4：返回 ok", g_last_error, error::ok);

  g_uart_tx.clear();
  feed("net set home\n");
  check("手册 4：模式段捕获 + 日志与回复同走一条通道",
        contains(g_uart_tx, "[info]") && contains(g_uart_tx, "ssid 已切到 home") &&
            g_uart_tx.size() >= 8 && g_uart_tx.compare(g_uart_tx.size() - 8, 8, "ssid ok\n") == 0,
        g_uart_tx);

  g_uart_tx.clear();
  feed("help\n");
  check("手册 4：help 列命令表",
        contains(g_uart_tx, "commands:") && contains(g_uart_tx, "net set"), g_uart_tx.substr(0, 60));

  g_uart_tx.clear();
  feed("-V\n");
  check("手册 4：-V 打版本行", contains(g_uart_tx, "console 0.1.0"), g_uart_tx);

  g_uart_tx.clear();
  feed("nope\n");
  check_error("手册 4：未知命令返回错误码", g_last_error, error::unknown_command);
  check("手册 4：未知命令有回复", contains(g_uart_tx, "unknown command 'nope'"), g_uart_tx);

  g_uart_tx.clear();
  feed("log warn\n");
  check("手册 4：warn 行出来、debug 行被过滤",
        contains(g_uart_tx, "[warn]") && contains(g_uart_tx, "warn 行") &&
            !contains(g_uart_tx, "debug 行"),
        g_uart_tx);
  check("手册 4：回复仍是原样字节",
        g_uart_tx.size() >= 3 && g_uart_tx.compare(g_uart_tx.size() - 3, 3, "ok\n") == 0,
        g_uart_tx);
}

// ============================================================================
// ---- 手册 5 坑与 FAQ ----
// ============================================================================
// 按字节喂到出一行为止
static void feed_one_line(line_reader<64> &rx, const char *bytes) {
  for (const char *p = bytes; *p != 0; ++p) {
    if (rx.put(*p)) return;
  }
}

static void check_pitfalls() {
  {   // 无参命令：E_FMT_DERIVE 至少要一个字段，用 skip 占位
    check("手册 5：no_args 的 skip 字段不进命令行", option<no_args>(0).skip);
    std::string text;
    check_error("手册 5：no_args 的命令能分发", run_line(kPatternCommands, "net", text), error::ok);
    check_error("手册 5：--unused 不是选项", run_line(kPatternCommands, "net --unused 1", text),
                error::unknown_option);
  }
  {   // 一行文本里的 string_view：指向行缓冲，那里没有结尾 0
    line_reader<64> rx;
    feed_one_line(rx, "hello\n");
    type_args a{};
    char scratch[ECLI_MAX_LINE];
    check_error("手册 5：一行文本里的 string_view",
                parse(rx.line(), a, scratch, sizeof(scratch)), error::ok);
    check("手册 5：token 指进行缓冲（没有结尾 0，别当 C 字符串用）",
          a.first.data() >= rx.line().data() && a.first.data() < rx.line().data() + rx.line().size());
    check_text("手册 5：长度是对的，拿长度用就没事",
               std::string_view(a.first.data(), a.first.size()), "hello");
  }
  {   // 帮助比 ECLI_REPLY_BUFFER 长：如实追加 "...(truncated)"
    static constexpr std::string_view kLongAbout =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    static constexpr command big[] = {{"big", kLongAbout, command_of<status_args, status_run>(), help_of<status_args>()}};
    std::string text;
    check_error("手册 5：超长帮助仍返回 help_requested", run_line(big, "big -h", text),
                error::help_requested);
    check("手册 5：装不下时有 truncated 标记", contains(text, "truncated"), text.substr(0, 40));
  }
  {   // help 是保留词：命令表里叫 help 的那条永远接不到命令
    static constexpr command shadow[] = {
        {"help", "这条永远接不到", command_of<status_args, status_run>(), help_of<status_args>()},
    };
    std::string text;
    g_calls.clear();
    check_error("手册 5：help 保留词被内置帮助截走", run_line(shadow, "help", text),
                error::help_requested);
    check("手册 5：内置帮助照常列命令表", contains(text, "commands:"), text);
    check("手册 5：处理函数没被调用", g_calls.empty(), g_calls);
  }
  {   // 段数不够：表里没有裸 "net" 兜底时，"net set" 只够到 "net set :ssid" 的前两段
    static constexpr command only_pattern[] = {
        {"net set :ssid", "设置 SSID", command_of<ssid_args, net_set_run>(), help_of<ssid_args>()},
    };
    std::string text;
    g_trace.clear();
    check_error("手册 5：段数不够 → unknown_command（没有更短的前缀可落）",
                run_line(only_pattern, "net set", text), error::unknown_command);
    check("手册 5：没执行处理函数", g_trace.empty(), g_trace);
  }
}

// ============================================================================
int main() {
  check_first_example();
  check_derivation();
  check_tags();
  check_value_types();
  check_syntax();
  check_texts();
  check_entries();
  check_command_table();
  check_builtin_help();
  check_pattern_commands();
  check_params_api();
  check_reply_channels();
  check_console_skeleton();
  check_pitfalls();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
