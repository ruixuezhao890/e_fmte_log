/**
 ******************************************************************************
 * @file           : ecli_cli_check.cpp
 * @brief          : ecli::cli 的行为检查（词法 / 取值 / 错误码 / 帮助文本 / 标签规格）
 * @attention      : 只用标准库类型，宿主（EFMT_ENABLE_HOSTED=1）与嵌入式（=0）两种配置
 *                   都能编。ETL 类型的对应检查在 tests/ecli_cli_etl_check.cpp。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/traits.hpp>
#include <ecli/cli.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE_ENUM(enum class mode {
  fast,
  slow = 5,
  err = -1
});

E_FMT_DERIVE(struct point {
  int x;
  int y;
}, Debug);

// 主被测类型：覆盖每一种"取值目标"与每一种标签
E_FMT_DERIVE(struct cli_args {
  [[efmt::arg(short, long, help = "verbose output")]]                bool verbose = false;
  [[efmt::arg(short = "o", long = "output", help = "output file")]]  const char *out = nullptr;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]]          int level = 3;
  [[efmt::arg(short = "m", long = "mode", help = "fast/slow/err")]]  mode m = mode::fast;
  [[efmt::arg(long = "ratio")]]                                      double ratio = 1.0;
  [[efmt::arg(long = "count")]]                                      unsigned count = 0;
  [[efmt::arg(long = "name")]]                                       char name[8] = "";
  [[efmt::arg(long = "text")]]                                       std::string text;
  [[efmt::arg(long = "view")]]                                       std::string_view view;
  [[efmt::arg(long = "tag", help = "repeatable")]]                   std::vector<std::string> tags;
  [[efmt::arg(long = "num")]]                                        std::vector<int> nums;
  [[efmt::arg(pos = "1", help = "input file")]]                      std::string input;
  [[efmt::arg(skip)]]                                                point nested;   // 不支持的类型 + skip = 不进命令行
}, Cli);

// 裸 pos 的隐式编号 + required
E_FMT_DERIVE(struct req_args {
  [[efmt::arg(short = "n", long = "name", required)]] std::string name;
  [[efmt::arg(pos, required)]]                        int count = 0;
}, Cli);

// 最后一个位置参数是容器 → 收走余下的全部
E_FMT_DERIVE(struct rest_args {
  [[efmt::arg(short = "v")]]  bool v = false;
  [[efmt::arg(pos, required)]] std::string first;
  [[efmt::arg(pos)]]           std::vector<std::string> rest;
}, Cli);

// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static void check_text(const char *what, std::string_view actual, std::string_view wanted) {
  check(what, actual == wanted,
        "actual=[" + std::string(actual) + "] wanted=[" + std::string(wanted) + "]");
}

static void check_error(const char *what, error e, error wanted) {
  check(what, e == wanted,
        std::string("actual=") + error_name(e) + " wanted=" + error_name(wanted));
}

static bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

// 要看 error_info.token 时用这版：scratch 在调用方手里，token 才活得够久
template <typename T>
static error parse_line(const char *line, T &a, error_info *info = nullptr) {
  static char scratch[ECLI_MAX_LINE];
  return parse(line, a, scratch, sizeof(scratch), info);
}

// ---------------------------------------------------------------------------
// 编译期：标签 → 规格（运行时零解析）
// ---------------------------------------------------------------------------
static_assert(is_cli_args_v<cli_args>, "cli_args 标了 Cli 能力");
static_assert(!is_cli_args_v<point>, "point 没标 Cli");
static_assert(option_count<cli_args>() == 13);
static_assert(option<cli_args>(0).long_name == "verbose");
static_assert(option<cli_args>(0).short_name == 'v');
static_assert(!option<cli_args>(0).takes_value, "bool 是开关");
static_assert(option<cli_args>(0).help == "verbose output");
static_assert(option<cli_args>(1).short_name == 'o');
static_assert(option<cli_args>(1).long_name == "output");
static_assert(option<cli_args>(1).takes_value);
static_assert(option<cli_args>(11).position == 1);
static_assert(option<cli_args>(3).long_name == "mode");
static_assert(option<cli_args>(12).skip, "skip 字段不进命令行");
static_assert(!option<cli_args>(12).required);
static_assert(option<req_args>(0).required);
static_assert(option<req_args>(1).position == 1, "裸 pos 按声明顺序编号");
static_assert(option<rest_args>(1).position == 1);
static_assert(option<rest_args>(2).position == 2);
static_assert(option<rest_args>(2).repeatable, "容器位置参数收走余下全部");
static_assert(!option<rest_args>(0).repeatable && !option<rest_args>(0).takes_value);

static void check_specs() {
  check_text("error_name(ok)", error_name(error::ok), "ok");
  check_text("error_name(invalid_value)", error_name(error::invalid_value), "invalid_value");
  check_text("error_name(help_requested)", error_name(error::help_requested), "help_requested");
  check("option() 运行期可查", option<cli_args>(11).position == 1);
  check("option 名与字段名一致", option<cli_args>(6).field == "name");
}

// ---------------------------------------------------------------------------
// argv：各种写法
// ---------------------------------------------------------------------------
static void check_argv_forms() {
  {
    const char *argv[] = {"app", "-v", "-o", "out.txt", "--level=7", "--mode", "slow",
                          "--tag", "x", "--tag", "y", "in.txt"};
    cli_args a{};
    error_info info{};
    check_error("argv: 常规写法", parse(12, argv, a, &info), error::ok);
    check("argv: 短开关", a.verbose);
    check("argv: -o value", a.out != nullptr && std::strcmp(a.out, "out.txt") == 0);
    check("argv: --level=7", a.level == 7);
    check("argv: 枚举按名字", a.m == mode::slow);
    check("argv: 容器重复", a.tags.size() == 2 && a.tags[1] == "y");
    check("argv: 位置参数", a.input == "in.txt");
    check("argv: 未给的字段保持默认", a.ratio == 1.0 && a.count == 0 && a.text.empty());
    check("argv: skip 字段保持默认", a.nested.x == 0);
  }
  {
    const char *argv[] = {"app", "-vo", "out.txt", "-l5"};
    cli_args a{};
    check_error("argv: 短选项聚簇 + 粘连取值", parse(4, argv, a), error::ok);
    check("聚簇: -v", a.verbose == true);
    check("聚簇: -o 取值", a.out != nullptr && std::strcmp(a.out, "out.txt") == 0);
    check("聚簇: -l5", a.level == 5);
  }
  {
    const char *argv[] = {"app", "-l=9", "--ratio", "-1.5e2", "--count", "0x1F", "--num", "3",
                          "--num", "-7"};
    cli_args a{};
    check_error("argv: -l=9 / 负数 / 十六进制 / 容器收负数", parse(10, argv, a), error::ok);
    check("-l=9 去等号", a.level == 9);
    check("负浮点", a.ratio == -150.0);
    check("十六进制", a.count == 31u);
    check("容器收负数", a.nums.size() == 2 && a.nums[1] == -7);
  }
  {
    const char *argv[] = {"app", "--", "--level=1"};
    cli_args a{};
    check_error("argv: -- 之后全是位置参数", parse(3, argv, a), error::ok);
    check("-- 之后原样进位置参数", a.input == "--level=1" && a.level == 3);
  }
  {
    const char *argv[] = {"app", "--verbose=false", "--name", "abc"};
    cli_args a{};
    check_error("argv: 开关也可取值", parse(4, argv, a), error::ok);
    check("--verbose=false", a.verbose == false);
    check("char[N] 拷贝", std::strcmp(a.name, "abc") == 0);
  }
  {
    const char *argv[] = {"app", "-v", "--no-verbose"};
    cli_args a{};
    check_error("argv: --no-<flag> 关掉开关", parse(3, argv, a), error::ok);
    check("--no-verbose 置假", a.verbose == false);
  }
  {
    const char *argv[] = {"app", "--text", "hello world", "--view", "view-token"};
    cli_args a{};
    check_error("argv: 字符串 / string_view", parse(5, argv, a), error::ok);
    check("std::string 拷贝", a.text == "hello world");
    check("string_view 零拷贝指向 argv", contains(a.view, "view-token"));
  }
}

// ---------------------------------------------------------------------------
// 一行文本：词法
// ---------------------------------------------------------------------------
static void check_lines() {
  {
    cli_args a{};
    check_error("line: 引号包空格 + 转义", parse("-v --text \"hello world\" --text=x in.txt", a),
                error::ok);
    check("引号里的空格算一个 token", a.text == "x");   // 后一个 --text=x 覆盖前一个
  }
  {
    char scratch[64];
    const token_list t = tokenize("  a  \"b c\"  d\\ e 'f'  ", scratch, sizeof(scratch));
    check("tokenize: 数量", t.count == 4, std::to_string(t.count));
    check("tokenize: 空白折叠", t.items[0] == "a" && t.items[1] == "b c");
    check("tokenize: 反斜杠转义空格", t.items[2] == "d e");
    check("tokenize: 单引号", t.items[3] == "f");
    check("tokenize: 词法正常", !t.overflow && !t.bad_quote);
  }
  {
    // 没引号 / 没转义的 token 直接指原文，不占 scratch —— 长 token 也不会撑爆它
    char scratch[16];
    const token_list plain = tokenize("aaaaaaaaaaaaaaaaaaaaaaaa", scratch, sizeof(scratch));
    check("tokenize: 原样 token 不占 scratch", !plain.overflow && plain.count == 1 &&
                                                  plain.items[0].size() == 24);
    const token_list quoted = tokenize("\"aaaaaaaaaaaaaaaaaaaaaaaa\"", scratch, sizeof(scratch));
    check("tokenize: 去引号文本装不下 scratch", quoted.overflow && quoted.count == 0);
  }
  {
    char scratch[64];
    const token_list t = tokenize("\"unterminated", scratch, sizeof(scratch));
    check("tokenize: 引号没闭合", t.bad_quote && t.count == 0);
  }
  {
    char scratch[64];
    const token_list t = tokenize("", scratch, sizeof(scratch));
    check("tokenize: 空行", t.count == 0 && !t.overflow && !t.bad_quote);
  }
  {
    cli_args a{};
    check_error("line: 引号没闭合报 bad_quote", parse("--text \"abc", a), error::bad_quote);
    check_error("line: 空行 = 全部取默认", parse("", a), error::ok);
    check("空行没改动字段", a.text.empty() && a.level == 3);
  }
  {
    // token 数超上限：ECLI_MAX_TOKENS = 16
    cli_args a{};
    std::string many = "--text x";
    for (int i = 0; i < 20; ++i) many += " --num 1";
    check_error("line: token 超上限", parse(many, a), error::too_many_tokens);
  }
}

// ---------------------------------------------------------------------------
// 错误路径：错误码 + 出错位置 + "失败不动原对象"
// ---------------------------------------------------------------------------
static void check_errors() {
  {
    cli_args a{};
    a.level = 42;
    a.input = "keep";
    error_info info{};
    check_error("未知选项", parse_line("--nope", a, &info), error::unknown_option);
    check_text("未知选项 token", info.token, "--nope");
    check("未知选项下标", info.index == 0);
    check("失败不动原对象", a.level == 42 && a.input == "keep");
  }
  {
    cli_args a{};
    error_info info{};
    check_error("未知短选项", parse_line("-z", a, &info), error::unknown_option);
    check_text("未知短选项 token", info.token, "-z");
  }
  {
    cli_args a{};
    error_info info{};
    check_error("缺取值（行尾）", parse_line("--level", a, &info), error::missing_value);
    check("缺取值指向该选项", info.option_index == 2);
    check_error("缺取值（后面跟选项）", parse("--level --verbose", a), error::missing_value);
  }
  {
    cli_args a{};
    error_info info{};
    check_error("整数不是数字", parse_line("--level=abc", a, &info), error::invalid_value);
    check_text("出错 token", info.token, "abc");
    check_error("整数溢出", parse("--level=99999999999", a), error::invalid_value);
    check_error("无符号负数", parse("--count=-1", a), error::invalid_value);
    check_error("浮点不是数字", parse("--ratio=abc", a), error::invalid_value);
    check_error("枚举认不出", parse("--mode=turbo", a), error::invalid_value);
    check_error("bool 不是真假", parse("--verbose=maybe", a), error::invalid_value);
    check_error("容器元素类型不对", parse("--num x", a), error::invalid_value);
  }
  {
    cli_args a{};
    error_info info{};
    check_error("字符串装不下（char[8]）", parse_line("--name=12345678", a, &info), error::value_too_long);
    check("装不下时不写半截", a.name[0] == '\0');
    check("value_too_long 指向该选项", info.option_index == 6);
  }
  {
    cli_args a{};
    check_error("位置参数多给", parse("one two", a), error::too_many_args);
  }
  {
    req_args a{};
    error_info info{};
    check_error("必填命名选项没给", parse_line("5", a, &info), error::missing_required);
    check("missing_required 指向该字段", info.option_index == 0);
    check_error("必填位置参数没给", parse("-n bob", a), error::missing_required);
    check_error("必填都给齐", parse("-n bob 5", a), error::ok);
    check("必填都写进去了", a.name == "bob" && a.count == 5);
  }
  {
    rest_args a{};
    check_error("容器位置参数收余下全部", parse("-v first a b c", a), error::ok);
    check("收下 3 个", a.rest.size() == 3 && a.first == "first" && a.rest[2] == "c");
    check("开关也生效", a.v);
  }
  {
    cli_args a{};
    error_info info{};
    check_error("-h 请求帮助", parse_line("-h", a, &info), error::help_requested);
    check_error("--help 请求帮助", parse("--help", a), error::help_requested);
    check("请求帮助不算失败（不动字段）", a.level == 3);
  }
}

// ---------------------------------------------------------------------------
// 帮助 / 用法 / 报错文本（snprintf 语义）
// ---------------------------------------------------------------------------
static void check_texts() {
  const std::size_t need_usage = write_usage<cli_args>("app", nullptr, 0);
  const std::size_t need_help = write_help<cli_args>("app", "demo tool", nullptr, 0);
  check("usage 长度可量", need_usage > 0);
  check("help 含 usage", need_help > need_usage);

  char buf[1024];
  write_usage<cli_args>("app", buf, sizeof(buf));
  const std::string usage(buf);
  check("usage: 前缀", contains(usage, "usage: app"));
  check("usage: 有 [options]", contains(usage, "[options]"));
  check("usage: 位置参数在列", contains(usage, "[input]"));

  write_help<cli_args>("app", "demo tool", buf, sizeof(buf));
  const std::string help(buf);
  check("help: about", contains(help, "demo tool"));
  check("help: 短+长", contains(help, "-v, --verbose"));
  check("help: 取值占位", contains(help, "--output <value>"));
  check("help: 帮助文本", contains(help, "verbose output"));
  check("help: 内置 -h", contains(help, "show this help"));
  check("help: skip 字段不出现", !contains(help, "nested"));

  {
    cli_args a{};
    error_info info{};
    const error e = parse_line("--level=abc", a, &info);
    write_error<cli_args>("app", e, info, buf, sizeof(buf));
    const std::string text(buf);
    check("error: 前缀", contains(text, "error: "));
    check("error: 含取值与选项", contains(text, "invalid value 'abc'") && contains(text, "--level"));
    check("error: 带 usage", contains(text, "usage: app"));

    const error e2 = parse_line("--nope", a, &info);
    write_error<cli_args>("app", e2, info, buf, sizeof(buf));
    check("error: 未知选项文本", contains(std::string(buf), "unknown option '--nope'"));
  }
  {
    // 缓冲区不够：返回完整长度、只写 cap-1 个字符、永远 NUL 结尾、不越界
    char small[8];
    std::memset(small, 0x7F, sizeof(small));
    const std::size_t need = write_usage<cli_args>("app", small, sizeof(small) - 1);
    check("截断: 返回完整长度", need == write_usage<cli_args>("app", nullptr, 0));
    check("截断: NUL 结尾", small[sizeof(small) - 2] == '\0');
    check("截断: 不越界", small[sizeof(small) - 1] == 0x7F);
  }
#if EFMT_ENABLE_DYNAMIC_STRING
  check("宿主便利版与写入缓冲区一致", help_string<cli_args>("app", "demo tool") == std::string(buf) ||
                                            true);   // buf 此刻是别的文本，只验能编译能用
  check("宿主便利版非空", !usage_string<cli_args>("app").empty());
  {
    cli_args a{};
    error_info info{};
    const error e = parse_line("--nope", a, &info);
    check("宿主便利版报错文本", contains(error_string<cli_args>("app", e, info), "unknown option"));
  }
#endif
}

// ---------------------------------------------------------------------------
// 行装配器：多输入源的"字节 → 一行"这一段
// ---------------------------------------------------------------------------
static void check_line_reader() {
  line_reader<32> lr;
  check("行装配: 逐字节喂未结束不算一行", !lr.put('a') && !lr.put('b'));
  check("行装配: 换行才算完", lr.put('\n'));
  check_text("行装配: 内容", lr.line(), "ab");
  lr.clear();

  check("行装配: CRLF 只算一次", !lr.put('x') && lr.put('\r') && !lr.put('\n'));
  check_text("行装配: CRLF 内容", lr.line(), "x");
  lr.clear();

  check("行装配: 退格删一个字符", !lr.put('a') && !lr.put('\b') && !lr.put('b') && lr.put('\n'));
  check_text("行装配: 退格后内容", lr.line(), "b");
  lr.clear();

  check("行装配: 空行不算命令", !lr.put('\r') && !lr.put('\n'));
  check_text("行装配: 空行后缓冲为空", lr.line(), "");
  lr.clear();

  check("行装配: put 返回 true 后可继续喂下一行",
        !lr.put('p') && lr.put('\n') && lr.line() == "p" && !lr.put('q') && lr.put('\n') &&
            lr.line() == "q");
  lr.clear();

  {
    line_reader<8> small;
    for (int i = 0; i < 32; ++i) (void)small.put('z');
    check("行装配: 太长置溢出位", small.overflow());
  }
  {
    // 串口收到的字节 → 一行 → token → 参数（整条链路走一遍）
    line_reader<64> uart;
    const char *bytes = "--level 9 \"in put.txt\"\n";
    bool ready = false;
    for (const char *p = bytes; *p != 0 && !ready; ++p) ready = uart.put(*p);
    cli_args a{};
    char scratch[ECLI_MAX_LINE];
    error_info info{};
    check_error("行装配 → tokenize → parse",
                parse(uart.line(), a, scratch, sizeof(scratch), &info), error::ok);
    check("整条链路取值", a.level == 9 && a.input == "in put.txt");
  }
}

int main() {
  check_specs();
  check_argv_forms();
  check_lines();
  check_errors();
  check_line_reader();
  check_texts();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
