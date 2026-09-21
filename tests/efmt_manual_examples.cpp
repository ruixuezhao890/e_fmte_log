/**
 ******************************************************************************
 * @file           : efmt_manual_examples.cpp
 * @brief          : 手册里的示例代码必须有可编译版本（防止文档腐烂）
 * @attention      : 手册 docs/EFMT-使用手册.md 中的片段若与实现脱节，这里会先编译失败。
 ******************************************************************************
 */

// 手册第 2 章展示的串口输出没有颜色转义序列，这里同样关掉 ANSI 再比对文本
#define EFMT_ENABLE_ANSI_STYLES 0

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace e_fmt;

// ---- 2.2 第一次打印（用缓冲区代替 UART）-------------------------------------
static char g_uart[512];
static size_t g_uart_pos = 0;
static void uart_sink(const char *data, size_t size) {
  if (g_uart_pos + size >= sizeof(g_uart)) {
    return;
  }
  std::memcpy(g_uart + g_uart_pos, data, size);
  g_uart_pos += size;
  g_uart[g_uart_pos] = '\0';
}

// ---- 5.2 自定义类型：四种写法 -------------------------------------------------
struct Point {
  int x, y;
};
E_FMT_FORMATTER_2(Point, int, x, "x", int, y, "y");

struct Reading {
  float temp;
  float hum;
  uint32_t ts;
};
E_FMT_FORMATTER_3(Reading, float, temp, "t", float, hum, "h", uint32_t, ts, "ts");

struct Flag {
  bool on;
};
E_FMT_FORMATTER_1(Flag, bool, on, "on");

struct Rgb {
  uint8_t r, g, b;
};
E_FMT_FORMATTER_FN(Rgb, [](format_context &ctx, const format_specs &specs, const Rgb &c) {
  char text[7];
  text[0] = '#';
  for (int i = 0; i < 3; ++i) {
    // 注意：宏参数里的逗号会拆参数，所以这里不用 {c.r, c.g, c.b} 这种花括号列表
    const uint8_t v = (i == 0) ? c.r : ((i == 1) ? c.g : c.b);
    text[1 + i * 2] = "0123456789abcdef"[v >> 4];
    text[2 + i * 2] = "0123456789abcdef"[v & 0xF];
  }
  ctx.write_aligned(std::string_view(text, 7), specs);
});

// 方式 3：特化 formatter
struct MyType {
  const char *to_string() const { return "MyType"; }
};
namespace e_fmt::detail {
template <> struct formatter<MyType> {
  static void format(format_context &ctx, const format_specs &, const MyType &v) {
    ctx.write_str(v.to_string());
  }
};
} // namespace e_fmt::detail

// ---- 5.3 格式化器里递归调用 format -------------------------------------------
struct Packet {
  int id;
  int len;
};
E_FMT_FORMATTER_FN(Packet, [](format_context &ctx, const format_specs &, const Packet &p) {
  ctx.write_str(e_fmt::format("[{}|{}]", p.id, p.len));
});

// ---- 10.2 E_FMT_DECLARE_STR --------------------------------------------------
E_FMT_DECLARE_STR(coord_fmt, "({}, {})");

// ---- 7.4 编译期自检 ----------------------------------------------------------
static_assert(EFMT_MAX_FORMAT_ARGS >= 8, "日志最多会用 8 个参数");

// 手册里的例子用"文本相等"来断言，失败时直接打出期望/实际，方便定位文档错误
static int g_failures = 0;

static void expect(const char *what, const std::string &actual, const char *expected) {
  if (actual == expected) {
    return;
  }
  ++g_failures;
  std::printf("FAIL %s\n     actual  =[%s]\n     expected=[%s]\n", what,
              actual.c_str(), expected);
}

static void expect_true(const char *what, bool ok) {
  if (ok) {
    return;
  }
  ++g_failures;
  std::printf("FAIL %s\n", what);
}

#define EXPECT_EQ(actual, expected) expect(#actual, (actual), (expected))
#define EXPECT_TRUE(expr) expect_true(#expr, static_cast<bool>(expr))

int main() {
  int failures = 0;
  char buffer[128];

  // 3.1 format_to
  const size_t needed = format_to(buffer, sizeof(buffer), "value={}", 1234567890);
  failures += (needed != 16) ? 1 : 0;
  char small[8];
  failures += (format_to(small, sizeof(small), "value={}", 1234567890) < sizeof(small)) ? 1 : 0;
  failures += (std::strcmp(small, "value=1") != 0) ? 1 : 0;

  // 3.2 / 3.3
  const std::string s = format("({}, {})", 1, 2);
  failures += (s != "(1, 2)") ? 1 : 0;
  EXPECT_TRUE(formatted_size("id={} name={}", 42, "abc") == 14);

  // 3.4 / 6.2 print 系列 + 缓冲区输出
  char sink[256];
  set_buffer_output(sink, sizeof(sink));
  set_output_handler(uart_sink);
  println_info("System boot");
  println_info("Firmware {} build {}", "1.4.0", 20260322);
  println_warning("battery {}%", 18);
  println_error("sensor 0x{:02X} timeout after {} ms", 0x1A, 250);
  println_info("temp={:.1f}C hum={:.1f}%", 25.5f, 60.0f);
  EXPECT_EQ(std::string(g_uart),
            "System boot\nFirmware 1.4.0 build 20260322\nbattery 18%\n"
            "sensor 0x1A timeout after 250 ms\ntemp=25.5C hum=60.0%\n");
  set_buffer_output(sink, sizeof(sink));  // 4.1 的字符串格式化仍走缓冲区
  reset_output_handler();

  // 4.x 格式规范
  EXPECT_EQ(format("{:<10}", "text"), "text      ");
  EXPECT_EQ(format("{:*>10}", "text"), "******text");
  EXPECT_EQ(format("{:#^10}", "text"), "###text###");
  EXPECT_EQ(format("{:+#010.3f}", 3.14159), "+00003.142");
  EXPECT_EQ(format("{:.0f}", 2.5), "2");
  EXPECT_EQ(format("{:#x}", 255), "0xff");
  EXPECT_EQ(format("{:#b}", 255), "0b11111111");
  EXPECT_EQ(format("{:.6d}", 255), "000255");
  EXPECT_EQ(format("{}", UINT64_MAX), "18446744073709551615");
  EXPECT_EQ(format("{:e}", 3.14159265358979), "3.141593e+00");
  EXPECT_EQ(format("{:.3g}", 100000.0), "1e+05");
  EXPECT_EQ(format("{:c}", 65), "A");
  EXPECT_EQ(format("{:s}", true), "true");
  EXPECT_EQ(format("{{{{{}}}}}", 42), "{{42}}");

  // 5.x 自定义类型
  EXPECT_EQ(format("{}", Point{10, 20}), "{x=10, y=20}");
  EXPECT_EQ(format("{}", Rgb{255, 0, 0}), "#ff0000");
  EXPECT_EQ(format("{:>10}", Rgb{255, 0, 0}), "   #ff0000");
  EXPECT_EQ(format("{}", MyType{}), "MyType");
  EXPECT_EQ(format("{}", Packet{7, 3}), "[7|3]");
  EXPECT_EQ(format("{}", Flag{true}), "{on=1}");   // bool 成员按 {}→ 1/0 输出
  // 方式 1 的宏会忽略外层格式规范：{:s} 不会传到成员上（手册 5.2 有说明）
  EXPECT_EQ(format("{:s}", Flag{true}), "{on=1}");

  // 5.4 容器（宿主默认开启）
  std::vector<int> v{1, 2, 3};
  failures += (format("{}", v) != "[1, 2, 3]") ? 1 : 0;
  std::map<std::string, int> m{{"a", 1}};
  failures += (format("{}", m) != "{a: 1}") ? 1 : 0;
  failures += (format("{}", std::make_tuple(1, 2.5, "x")) != "(1, 2.5, x)") ? 1 : 0;
  failures += (format("{}", std::make_pair(std::string("k"), 7)) != "(k: 7)") ? 1 : 0;

  // 10.1 / 10.2 编译期校验
  failures += (format(E_FMT_STR("Hello {}"), "World") != "Hello World") ? 1 : 0;
  failures += (format(coord_fmt, 3, 4) != "(3, 4)") ? 1 : 0;

  // 10.4 运行期不崩
  EXPECT_EQ(format("{}, {}", 1), "1, {?}");
  EXPECT_EQ(format("{}", 1, 2), "1");
  EXPECT_EQ(format("{abc", 1), "{abc");

  // 5.1 内嵌 '\0' 的字符串视图
  char raw[8] = {'a', '\0', 'b'};
  const std::string_view sv(raw, 3);
  failures += (format("[{}]", sv).size() != 5) ? 1 : 0;

  g_failures += failures;  // 早期写的计数也一并计入
  std::printf("manual examples: %s\n", g_failures == 0 ? "ok" : "FAILED");
  return g_failures == 0 ? 0 : 1;
}
