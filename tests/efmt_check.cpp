/**
 ******************************************************************************
 * @file           : efmt_check.cpp
 * @brief          : Behaviour + regression checks for the efmt core library
 * @attention      : Self-contained (no test framework). Build, from the repo
 *                   root, with a compiler whose include path can see both
 *                   <middleware/efmt/...> and <middleware/etl/...>:
 *                     g++ -std=c++17 -O2 -Wall -Wextra -I tests/include \
 *                         tests/efmt_check.cpp -o tests/out/efmt_check.exe
 *                   (tests/run_check.ps1 sets this up on Windows/MinGW.)
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace e_fmt;

// ============================================================================
// 极简断言框架
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void report(bool ok, const char *expr, const char *file, int line,
                   const std::string &detail) {
  ++g_checks;
  if (ok) {
    return;
  }
  ++g_failures;
  std::printf("FAIL %s:%d  %s\n     %s\n", file, line, expr, detail.c_str());
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__, "")
#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    const auto a_ = (actual);                                                   \
    const auto e_ = (expected);                                                 \
    std::ostringstream os_;                                                     \
    os_ << "actual=[" << a_ << "] expected=[" << e_ << "]";                      \
    report(a_ == e_, #actual " == " #expected, __FILE__, __LINE__, os_.str());   \
  } while (0)

// ============================================================================
// 被测类型
// ============================================================================
struct point {
  int x;
  int y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);

struct rgb_color {
  int r;
  int g;
  int b;
};
E_FMT_FORMATTER_FN(rgb_color, [](format_context &ctx, const format_specs &,
                                 const rgb_color &c) {
  char buffer[16];
  int len = std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", c.r, c.g, c.b);
  ctx.write_chars(buffer, static_cast<size_t>(len));
});

struct temperature {
  double celsius;
};
E_FMT_FORMATTER_FIELDS(temperature, celsius);

// 自定义格式化器内再次调用 format：验证自定义参数不依赖共享状态
struct nested {
  int value;
};
E_FMT_FORMATTER_FN(nested, [](format_context &ctx, const format_specs &,
                              const nested &n) {
  // 格式化器内部再次调用 format（递归格式化的参数不能互相踩）
  // 限定命名空间：宏把 lambda 放在 default_formatter<T> 里，未限定的 format 会先找到成员函数
  const std::string inner = e_fmt::format("[{}]", n.value * 2);
  ctx.write_str(inner);
});

// 逐字复制测试输入，用来验证长输出路径
struct passthrough {
  std::string text;
};
E_FMT_FORMATTER_FN(passthrough, [](format_context &ctx, const format_specs &,
                                   const passthrough &p) {
  ctx.write_str(p.text);
});

// 命名空间作用域的编译期格式串（推荐用法：声明一次，多处复用）
E_FMT_DECLARE_STR(ns_scope_fmt, "<{}:{}>");

struct stream_only {
  int value;
};
inline std::ostream &operator<<(std::ostream &os, const stream_only &s) {
  return os << "stream(" << s.value << ")";
}

// ============================================================================
// 用例
// ============================================================================
static void test_basic() {
  CHECK_EQ(format("Hello, {}!", "World"), std::string("Hello, World!"));
  CHECK_EQ(format("Position: x={}, y={}", 10, 20), std::string("Position: x=10, y=20"));
  CHECK_EQ(format("PI = {:.2f}", 3.14159), std::string("PI = 3.14"));
  CHECK_EQ(format("{}", true), std::string("1"));
  CHECK_EQ(format("{:s}", true), std::string("true"));
  CHECK_EQ(format("{:s}", false), std::string("false"));
  CHECK_EQ(format("{}", 'A'), std::string("A"));
  CHECK_EQ(format("{:c}", 65), std::string("A"));
  CHECK_EQ(format("{}", (const char *)nullptr), std::string("(null)"));

  // 小整型不再被隐式转换成 char
  CHECK_EQ(format("{}", static_cast<unsigned char>(255)), std::string("255"));
  CHECK_EQ(format("{}", static_cast<signed char>(-8)), std::string("-8"));
  CHECK_EQ(format("{}", static_cast<short>(-1234)), std::string("-1234"));

  // 64 位边界
  CHECK_EQ(format("{}", ~0ULL), std::string("18446744073709551615"));
  CHECK_EQ(format("{}", static_cast<long long>(-9223372036854775807LL - 1)),
           std::string("-9223372036854775808"));
  CHECK_EQ(format("{:x}", 4294967295U), std::string("ffffffff"));

  // 转义
  CHECK_EQ(format("{{}}"), std::string("{}"));
  CHECK_EQ(format("{{{}}}", 7), std::string("{7}"));
  CHECK_EQ(format("100%% done"), std::string("100%% done"));
}

static void test_specs() {
  // 填充与对齐
  CHECK_EQ(format("|{:<10}|", "text"), std::string("|text      |"));
  CHECK_EQ(format("|{:>10}|", "text"), std::string("|      text|"));
  CHECK_EQ(format("|{:^10}|", "text"), std::string("|   text   |"));
  CHECK_EQ(format("|{:*>10}|", "text"), std::string("|******text|"));
  CHECK_EQ(format("|{:0<10}|", "text"), std::string("|text000000|"));
  CHECK_EQ(format("|{:#^10}|", "text"), std::string("|###text###|"));
  CHECK_EQ(format("|{:>6}|", 42), std::string("|    42|"));
  CHECK_EQ(format("|{:<6}|", 42), std::string("|42    |"));

  // 零填充（'0' 选项）与显式对齐互斥
  CHECK_EQ(format("{:08}", 42), std::string("00000042"));
  CHECK_EQ(format("{:08}", -42), std::string("-0000042"));
  CHECK_EQ(format("{:08x}", 255), std::string("000000ff"));
  CHECK_EQ(format("{:>8}", 42), std::string("      42"));
  CHECK_EQ(format("{:#08x}", 255), std::string("0x0000ff"));
  CHECK_EQ(format("{:#08x}", -255), std::string("-0x000ff"));
  CHECK_EQ(format("{:*^11}", "ab"), std::string("****ab*****"));
  CHECK_EQ(format("{:08.5}", 42), std::string("00000042"));

  // 符号
  CHECK_EQ(format("{:+}", 42), std::string("+42"));
  CHECK_EQ(format("{:+}", -42), std::string("-42"));
  CHECK_EQ(format("{: }", 42), std::string(" 42"));
  CHECK_EQ(format("{: }", -42), std::string("-42"));

  // 宽度 / 精度
  CHECK_EQ(format("{:.3}", 3.14159), std::string("3.14"));
  CHECK_EQ(format("{:.5}", "hello world"), std::string("hello"));
  CHECK_EQ(format("{:10.2f}", 3.14159), std::string("      3.14"));
  CHECK_EQ(format("{:.5}", 42), std::string("00042"));
  CHECK_EQ(format("{:.0}", 0.0), std::string("0"));

  // 进制
  CHECK_EQ(format("{:d}", 255), std::string("255"));
  CHECK_EQ(format("{:x}", 255), std::string("ff"));
  CHECK_EQ(format("{:X}", 255), std::string("FF"));
  CHECK_EQ(format("{:#x}", 255), std::string("0xff"));
  CHECK_EQ(format("{:#X}", 255), std::string("0XFF"));
  CHECK_EQ(format("{:o}", 255), std::string("377"));
  CHECK_EQ(format("{:#o}", 255), std::string("0377"));
  CHECK_EQ(format("{:b}", 255), std::string("11111111"));
  CHECK_EQ(format("{:#b}", 5), std::string("0b101"));

  // 浮点
  CHECK_EQ(format("{:f}", 3.14159265359), std::string("3.141593"));
  CHECK_EQ(format("{:e}", 3.14159265359), std::string("3.141593e+00"));
  CHECK_EQ(format("{:E}", 3.14159265359), std::string("3.141593E+00"));
  CHECK_EQ(format("{:g}", 3.14159265359), std::string("3.14159"));
  CHECK_EQ(format("{:G}", 3.14159265359), std::string("3.14159"));
  CHECK_EQ(format("{:+}", 2.5), std::string("+2.5"));
  CHECK_EQ(format("{:+.2f}", 2.5), std::string("+2.50"));
  CHECK_EQ(format("{:08.2f}", -3.5), std::string("-0003.50"));
  CHECK(format("{}", 1.0 / 0.0).find("inf") != std::string::npos);
  CHECK(format("{}", -1.0 / 0.0).find("-inf") != std::string::npos);
  CHECK(format("{}", 0.0 / 0.0).find("nan") != std::string::npos);

  // 大精度不再越界（旧实现会写坏 64 字节静态缓冲）
  const std::string wide = format("{:.200}", 7);
  CHECK_EQ(wide.size(), static_cast<size_t>(200));
  CHECK_EQ(wide.substr(198), std::string("07"));
  const std::string wide_f = format("{:.120f}", 1.0);
  CHECK(wide_f.size() <= 128);
  CHECK_EQ(format("{}", 42), std::string("42"));  // 仍然可用
}

static void test_argument_selection() {
  CHECK_EQ(format("{1} {0}", "a", "b"), std::string("b a"));
  CHECK_EQ(format("{0} {0} {0}", 5), std::string("5 5 5"));
  // 动态宽度（{0:{1}}）不支持：解析失败时原样输出
  CHECK_EQ(format("{0:{1}}", 42, 6), std::string("{0:{1}}"));
  CHECK_EQ(format("{} and {}", 1), std::string("1 and {?}"));
}

static void test_no_error_channel() {
  // 没有错误码：坏格式串原样输出，缺失参数写 "{?}"，其余照常
  CHECK_EQ(format("{"), std::string("{"));
  CHECK_EQ(format("a}b"), std::string("a}b"));
  CHECK_EQ(format("{{"), std::string("{"));
  CHECK_EQ(format("{} {}", 1), std::string("1 {?}"));
  CHECK_EQ(format("{}", 1, 2), std::string("1"));   // 多余实参被忽略
}

static void test_buffer_api() {
  char buffer[32];
  const size_t written = format_to(buffer, sizeof(buffer), "Value: {}", 123);
  CHECK_EQ(written, static_cast<size_t>(10));
  CHECK_EQ(std::string(buffer), std::string("Value: 123"));
  CHECK_EQ(formatted_size("Value: {}", 123), static_cast<size_t>(10));

  // snprintf 语义：返回值是所需长度，>= size 即截断，缓冲区末位留给 '\0'
  char small[8];
  CHECK_EQ(format_to(small, sizeof(small), "{}!", "0123456789"),
           static_cast<size_t>(11));
  CHECK_EQ(std::string(small), std::string("0123456"));

  char exact[12];
  CHECK_EQ(format_to(exact, sizeof(exact), "{}!", "0123456789"),
           static_cast<size_t>(11));
  CHECK_EQ(std::string(exact), std::string("0123456789!"));

  char no_room[1] = {'x'};
  CHECK_EQ(format_to(no_room, 0, "{}", 42), static_cast<size_t>(2));

  // 精确长度（旧实现按每个参数估算 64 字节，长参数会被静默截断）
  const std::string long_text(500, 'x');
  CHECK_EQ(format("{}", long_text).size(), static_cast<size_t>(500));
  CHECK_EQ(formatted_size("{}", long_text), static_cast<size_t>(500));
  CHECK_EQ(formatted_size("Hello {}", "World"), static_cast<size_t>(11));
  CHECK_EQ(formatted_size("{:.2f}", 3.14159), static_cast<size_t>(4));

  // 超过栈缓冲区的内容：第一遍就拿到精确长度，再按长度重写一遍
  const std::string huge(1000, 'y');
  CHECK_EQ(format("[{}]{}", "tag", huge).size(), static_cast<size_t>(1005));
  char big_buffer[2048];
  CHECK_EQ(format_to(big_buffer, sizeof(big_buffer), "[{}]{}", "tag", huge),
           static_cast<size_t>(1005));
  CHECK_EQ(formatted_size("{}", huge), static_cast<size_t>(1000));

  std::string out = "prefix";
  format_to(out, "{}:{}", "a", 1);
  CHECK_EQ(out, std::string("a:1"));
}

static void test_string_views() {
  // 格式串与参数都可以不是 NUL 结尾
  const char raw[] = {'x', '=', '{', '}', ' ', 'y', '=', '{', '}', '?'};
  const std::string_view fmt(raw, 9);
  CHECK_EQ(format(fmt, 1, 2), std::string("x=1 y=2"));

  const char text[] = {'a', 'b', 'c', 'd'};
  CHECK_EQ(format("[{}]", std::string_view(text, 4)), std::string("[abcd]"));
  CHECK_EQ(format("[{:.2}]", std::string_view(text, 4)), std::string("[ab]"));
}

static void test_containers() {
  std::vector<int> nums = {1, 2, 3};
  CHECK_EQ(format("{}", nums), std::string("[1, 2, 3]"));

  std::vector<std::string> words = {"hello", "world"};
  CHECK_EQ(format("{}", words), std::string("[hello, world]"));

  std::vector<std::vector<int>> matrix = {{1, 2}, {3, 4}};
  CHECK_EQ(format("{}", matrix), std::string("[[1, 2], [3, 4]]"));

  std::tuple<int, double, std::string> t = {42, 3.14, "hello"};
  CHECK_EQ(format("{}", t), std::string("(42, 3.14, hello)"));

  std::pair<std::string, int> p = {"answer", 42};
  CHECK_EQ(format("{}", p), std::string("(answer: 42)"));
}

static void test_custom_types() {
  CHECK_EQ(format("{}", point{10, 20}), std::string("{x=10, y=20}"));
  CHECK_EQ(format("{}", rgb_color{255, 0, 0}), std::string("#ff0000"));
  CHECK_EQ(format("{}", temperature{21.5}), std::string("{celsius=21.5}"));
  CHECK_EQ(format("{} {}", point{1, 2}, point{3, 4}),
           std::string("{x=1, y=2} {x=3, y=4}"));
  CHECK_EQ(format("n={}", nested{21}), std::string("n=[42]"));
  CHECK_EQ(format("{}", stream_only{7}), std::string("stream(7)"));

  // 自定义参数与内建参数混排，且数量超过参数槽轮转周期
  CHECK_EQ(format("{}{}{}{}{}{}{}{}{}{}", point{1, 1}, rgb_color{0, 0, 0}, point{2, 2},
                  point{3, 3}, rgb_color{1, 1, 1}, point{4, 4}, point{5, 5}, point{6, 6},
                  point{7, 7}, point{8, 8}),
           std::string("{x=1, y=1}#000000{x=2, y=2}{x=3, y=3}#010101{x=4, y=4}"
                       "{x=5, y=5}{x=6, y=6}{x=7, y=7}{x=8, y=8}"));
}

static void test_compile_time_strings() {
  CHECK_EQ(format(E_FMT_STR("inline {} {}"), 1, 2), std::string("inline 1 2"));

  E_FMT_DECLARE_STR(coord_fmt, "({}, {})");
  CHECK_EQ(format(coord_fmt, 3, 4), std::string("(3, 4)"));
  CHECK_EQ(formatted_size(coord_fmt, 3, 4), static_cast<size_t>(6));

  CHECK_EQ(format(ns_scope_fmt, "k", 7), std::string("<k:7>"));
  CHECK_EQ(formatted_size(ns_scope_fmt, "k", 7), static_cast<size_t>(5));

  E_FMT_DECLARE_STR_2(legacy_fmt, "x={}, y={}");
  CHECK_EQ(format(legacy_fmt, 10, 20), std::string("x=10, y=20"));

  E_FMT_DECLARE_FMT(other_fmt, "{}!");
  CHECK_EQ(format(other_fmt, "hi"), std::string("hi!"));

  char buffer[16];
  CHECK_EQ(format_to(buffer, sizeof(buffer), E_FMT_STR("{}"), 9),
           static_cast<size_t>(1));
  CHECK_EQ(std::string(buffer), std::string("9"));

  std::string out;
  format_to(out, E_FMT_STR("<{}>"), 1);
  CHECK_EQ(out, std::string("<1>"));

  CHECK_EQ(formatted_size(E_FMT_STR("ab{}"), 1), static_cast<size_t>(3));
  CHECK_EQ(format_to(buffer, sizeof(buffer), E_FMT_STR("{}"), "ok"),
           static_cast<size_t>(2));

  // E_FMT_STR 也能隐式转成 std::string_view，任何接口都能用
  const std::string_view holder_view = E_FMT_STR("sv");
  CHECK_EQ(std::string(holder_view), std::string("sv"));
}

static void test_size_matches_output() {
  // 计数扫描与写入扫描必须产出一致的结果（长度精确的前提）
  CHECK_EQ(formatted_size("{}", 3.14159), format("{}", 3.14159).size());
  CHECK_EQ(formatted_size("{:.2f}", 3.14159), format("{:.2f}", 3.14159).size());
  CHECK_EQ(formatted_size("{:>20}", 42), format("{:>20}", 42).size());
  CHECK_EQ(formatted_size("{:<20}", "x"), format("{:<20}", "x").size());
  CHECK_EQ(formatted_size("{:^21}", "x"), format("{:^21}", "x").size());
  CHECK_EQ(formatted_size("{:08x}", 255), format("{:08x}", 255).size());
  CHECK_EQ(formatted_size("{:+.3e}", -12345.678), format("{:+.3e}", -12345.678).size());
  CHECK_EQ(formatted_size("a{{b}}c{}", "d"), format("a{{b}}c{}", "d").size());
  CHECK_EQ(formatted_size("{}", "0123456789"), format("{}", "0123456789").size());
  CHECK_EQ(formatted_size("{}", std::string(400, 'q')), format("{}", std::string(400, 'q')).size());
  CHECK_EQ(formatted_size("{}{}", point{1, 2}, std::vector<int>{1, 2, 3}),
           format("{}{}", point{1, 2}, std::vector<int>{1, 2, 3}).size());
  CHECK_EQ(formatted_size("no fields"), format("no fields").size());
  CHECK_EQ(formatted_size(""), format("").size());
  CHECK_EQ(formatted_size("{}", stream_only{1}), format("{}", stream_only{1}).size());

  // 计数用的预扫描不能改变最终缓冲区内容
  char first[64];
  char second[64];
  format_to(first, sizeof(first), "{}|{}", 42, 1.5);
  (void)formatted_size("{}|{}", 42, 1.5);
  format_to(second, sizeof(second), "{}|{}", 42, 1.5);
  CHECK_EQ(std::string(first), std::string(second));
}

static void test_print_api() {
  char sink[128] = {};
  set_buffer_output(sink, sizeof(sink));
  print_info("value={}", 42);
  CHECK_EQ(std::string(sink).find("value=42") != std::string::npos, true);
  CHECK_EQ(get_buffer_output_pos() > 0, true);

  reset_buffer_output_pos();
  println_info(E_FMT_STR("checked {}"), 1);
  CHECK_EQ(std::string(sink).find("checked 1") != std::string::npos, true);

  reset_buffer_output_pos();
  char long_sink[600];
  set_buffer_output(long_sink, sizeof(long_sink));
  const std::string big(400, 'z');
  println_info("{}", big);
  CHECK_EQ(std::string(long_sink).find("zzz") != std::string::npos, true);

  reset_output_handler();
  reset_buffer_output_pos();
}

static void test_style_builder() {
  char buffer[64];
  const size_t len = detail::style_builder::build(detail::styles::error(), buffer, sizeof(buffer));
  CHECK_EQ(std::string(buffer, len), std::string("\033[31;1m"));
  CHECK_EQ(std::string(detail::style_builder::reset()), std::string("\033[0m"));

  char single[16];
  size_t single_len = 0;
  detail::style_builder::build_color(single, single_len, detail::color::bright_white);
  CHECK_EQ(std::string(single, single_len), std::string("\033[97m"));

  detail::text_style combined(detail::color::white, detail::bg_color::blue);
  combined.set_style(detail::style::bold);
  const size_t combined_len = detail::style_builder::build(combined, buffer, sizeof(buffer));
  CHECK_EQ(std::string(buffer, combined_len), std::string("\033[37;44;1m"));
}

#define RUN(fn) do { std::printf(">> " #fn "\n"); std::fflush(stdout); fn(); } while (0)

int main() {
  RUN(test_basic);
  RUN(test_specs);
  RUN(test_argument_selection);
  RUN(test_no_error_channel);
  RUN(test_buffer_api);
  RUN(test_string_views);
  RUN(test_containers);
  RUN(test_custom_types);
  RUN(test_compile_time_strings);
  RUN(test_size_matches_output);
  RUN(test_print_api);
  RUN(test_style_builder);

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
