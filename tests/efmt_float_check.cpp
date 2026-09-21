/**
 ******************************************************************************
 * @file           : efmt_float_check.cpp
 * @brief          : 自带浮点引擎与 libc printf 的差分对拍
 * @attention      : 用 -DEFMT_USE_LIBC_PRINTF=0 编译，让 efmt 走自带实现，
 *                   然后拿同样的 value + 规范去和 snprintf 的结果逐字节比对。
 *                   覆盖：随机位模式、极端值、次正规数、进位、半偶舍入、宽度/补零。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if EFMT_USE_LIBC_PRINTF
#error "this check must be built with -DEFMT_USE_LIBC_PRINTF=0"
#endif

using namespace e_fmt;

static size_t g_checks = 0;
static size_t g_failures = 0;

// 宿主 libc 的已知缺陷
// ---------------------------------------------------------------------------
// Windows UCRT 的 %g/%G 在"只差一个填充字符"时会漏掉那一个字符：
//   snprintf("%21.25g", 6.8459595401584845e+19) -> 20 字符（标准应为 21）
// glibc / newlib 都按标准补齐，efmt 也按标准补齐，所以对拍时把参考值修回来。
static bool host_g_width_bug() {
  static const bool bugged = [] {
    char buffer[64];
    const int written =
        std::snprintf(buffer, sizeof(buffer), "%21.25g", 6.8459595401584845e+19);
    return written == 20;
  }();
  return bugged;
}

// 需要时按 printf 规则补上那一个字符（'-' 补后面，'0' 补在符号后，其余补前面）
static void fix_host_general_width(char *expected, const char *spec, bool enabled) {
  if (!enabled) {
    return;
  }
  const size_t spec_len = std::strlen(spec);
  const char type = (spec_len > 0) ? spec[spec_len - 1] : '\0';
  if (type != 'g' && type != 'G') {
    return;
  }

  // 从规范里解析 flags 与 width（flags 形如 "-+ 0#"）
  size_t pos = 0;
  bool left_align = false;
  bool zero_fill = false;
  while (pos < spec_len && (spec[pos] == '-' || spec[pos] == '+' ||
                            spec[pos] == ' ' || spec[pos] == '0' || spec[pos] == '#')) {
    if (spec[pos] == '-') left_align = true;
    if (spec[pos] == '0') zero_fill = true;
    ++pos;
  }
  int width = 0;
  while (pos < spec_len && spec[pos] >= '0' && spec[pos] <= '9') {
    width = width * 10 + (spec[pos] - '0');
    ++pos;
  }

  const size_t len = std::strlen(expected);
  if (width <= 0 || static_cast<size_t>(width) != len + 1) {
    return;  // 只有"差一个字符"这一种情形受该缺陷影响
  }

  char fixed[4096];
  if (left_align) {
    std::snprintf(fixed, sizeof(fixed), "%s ", expected);
  } else if (zero_fill) {
    size_t sign = (expected[0] == '-' || expected[0] == '+' || expected[0] == ' ') ? 1u : 0u;
    std::memcpy(fixed, expected, sign);
    fixed[sign] = '0';
    std::strcpy(fixed + sign + 1, expected + sign);
  } else {
    std::snprintf(fixed, sizeof(fixed), " %s", expected);
  }
  std::strcpy(expected, fixed);
}

// ---------------------------------------------------------------------------
// 对拍一个 (value, spec)
// ---------------------------------------------------------------------------
// spec 用 printf 的写法（不含 '%' / '{:'），例如 "10.2f"、".3e"、"+#g"。
static void compare(double value, const char *spec) {
  char expected[4096];
  char spec_printf[32];
  // efmt 的 "{}"（无规范）等价于 printf 的 "%g"
  std::snprintf(spec_printf, sizeof(spec_printf), spec[0] == '\0' ? "%%g" : "%%%s", spec);
  int expected_len = std::snprintf(expected, sizeof(expected), spec_printf, value);
  fix_host_general_width(expected, spec, host_g_width_bug());
  expected_len = static_cast<int>(std::strlen(expected));
  const bool expected_truncated = (expected_len < 0) || (expected_len >= static_cast<int>(sizeof(expected)));

  // printf 的 '-' 是左对齐，efmt 的 '-' 是"仅负数带符号"，这里做等价翻译
  char efmt_spec[40];
  {
    size_t src = 0;
    size_t dst = 0;
    if (spec[0] == '-') {
      efmt_spec[dst++] = '<';
      src = 1;
    }
    for (; spec[src] != '\0' && dst + 1 < sizeof(efmt_spec); ++src) {
      efmt_spec[dst++] = spec[src];
    }
    efmt_spec[dst] = '\0';
  }

  char fmt[40];
  if (spec[0] == '\0') {
    std::snprintf(fmt, sizeof(fmt), "{}");
  } else {
    std::snprintf(fmt, sizeof(fmt), "{:%s}", efmt_spec);
  }

  char actual[4096];
  const size_t needed = e_fmt::format_to(actual, sizeof(actual), fmt, value);
  const bool actual_truncated = needed >= sizeof(actual);

  ++g_checks;
  if (expected_truncated || actual_truncated) {
    return;  // 超长规范不比对（两边缓冲策略不同，没有可比性）
  }
  if (std::strcmp(expected, actual) == 0) {
    return;
  }

  ++g_failures;
  if (g_failures <= 20) {
    char bits_text[32];
    uint64_t raw_bits = 0;
    std::memcpy(&raw_bits, &value, sizeof(raw_bits));
    std::snprintf(bits_text, sizeof(bits_text), "%016llx",
                  static_cast<unsigned long long>(raw_bits));
    std::printf("FAIL spec=%%%s value=%.17g bits=%s\n     printf=[%s]\n     efmt  =[%s]\n",
                spec, value, bits_text, expected, actual);
  }
}

// ---------------------------------------------------------------------------
// 随机数（xorshift，固定种子便于复现）
// ---------------------------------------------------------------------------
static uint64_t g_state = 0x243F6A8885A308D3ull;

static uint64_t next_random() {
  g_state ^= g_state << 13;
  g_state ^= g_state >> 7;
  g_state ^= g_state << 17;
  return g_state;
}

static double random_double() {
  const uint64_t bits = next_random();
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// ---------------------------------------------------------------------------
// 定点：极端值 + 常见精度
// ---------------------------------------------------------------------------
static void check_edge_values() {
  const double values[] = {
      0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, 3.5, 0.125, 0.375, 0.0625, 0.05, 0.15,
      0.25, 0.35, 0.1, 0.2, 0.3, 0.7, 1.0 / 3.0, 2.0 / 3.0, 1e-1, 1e-2, 1e-3,
      1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-15, 1e-16, 1e-17, 1e-20, 1e-30, 1e-50,
      1e-100, 1e-200, 1e-300, 1e-307, 1e-308, 2.2250738585072014e-308,
      4.9406564584124654e-324, 9.8813129168249309e-324, 1e15, 1e16, 1e17, 1e18,
      1e19, 1e20, 1e21, 1e22, 1e23, 1e30, 1e50, 1e100, 1e200, 1e300, 1e308,
      1.7976931348623157e308, 9007199254740992.0, 9007199254740993.0,
      9999999999999999.0, 123456.789, 1234567.891, 99999.99999, 0.9999999999999999,
      9.999999999999999e22, 1.0000000000000002, 2.675, 1.005, 0.615, 1e-322,
      3.141592653589793, 2.718281828459045, 6.02214076e23, 1.602176634e-19};
  const char *specs[] = {"f",     ".0f",  ".1f",  ".2f",   ".3f",   ".6f",
                          ".9f",  ".15f", ".17f", ".20f",  "e",     ".0e",
                          ".1e",  ".2e",  ".6e",  ".15e",  ".17e",  "g",
                          ".0g",  ".1g",  ".3g",  ".6g",   ".10g",  ".17g",
                          "E",    ".3E",  "G",    ".4G",   "+.2f",  " .2f",
                          "+.3e", "+g",   "#.0f", "#g",    "#.10g", "010.3f",
                          "-12.4f", "12.4f", "020.6e", "20.6g"};
  for (double v : values) {
    for (double s : {v, -v}) {
      for (const char *spec : specs) {
        compare(s, spec);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 随机位模式
// ---------------------------------------------------------------------------
static void check_random_values(size_t count) {
  const char *specs[] = {"",     ".0f", ".2f",  ".6f",   "f",     ".3e",
                          "e",    ".6e", "g",    ".3g",   ".10g",  "12.4f",
                          "020.8g", "+.5f", "#.0f", "#.6g", " .4e",  "30.17g"};
  const size_t spec_count = sizeof(specs) / sizeof(specs[0]);
  for (size_t i = 0; i < count; ++i) {
    const double value = random_double();
    if (value != value) {
      continue;  // NaN 单独测
    }
    compare(value, specs[i % spec_count]);
  }
}

// ---------------------------------------------------------------------------
// 随机规范（精度 0..26，随机宽度/对齐/标志）
// ---------------------------------------------------------------------------
static void check_random_specs(size_t count) {
  const char types[] = {'f', 'F', 'e', 'E', 'g', 'G'};
  for (size_t i = 0; i < count; ++i) {
    const double value = random_double();
    if (value != value) {
      continue;
    }
    const unsigned precision = static_cast<unsigned>(next_random() % 27);
    const unsigned width = static_cast<unsigned>(next_random() % 24);
    char spec[32];
    size_t pos = 0;
    const unsigned flags = static_cast<unsigned>(next_random() % 8);
    if (flags & 1u) spec[pos++] = '-';
    if (flags & 2u) spec[pos++] = '+';
    if ((flags & 4u) && !(flags & 1u)) spec[pos++] = '0';
    if (width > 0) pos += static_cast<size_t>(std::snprintf(spec + pos, sizeof(spec) - pos, "%u", width));
    spec[pos++] = '.';
    pos += static_cast<size_t>(std::snprintf(spec + pos, sizeof(spec) - pos, "%u", precision));
    spec[pos++] = types[next_random() % 6u];
    spec[pos] = '\0';
    compare(value, spec);
  }
}

// ---------------------------------------------------------------------------
// 特殊值：inf / nan（printf 语义：inf/inf/NAN，符号跟随符号位与 '+'）
// ---------------------------------------------------------------------------
static void check_specials() {
  const double inf = 1e308 * 10.0;
  ++g_checks;
  if (format("{:.2f}", inf) != "inf" || format("{:.2f}", -inf) != "-inf" ||
      format("{:F}", inf) != "INF" || format("{:+.2e}", inf) != "+inf" ||
      format("{:}", -inf) != "-inf") {
    ++g_failures;
    std::printf("FAIL special: inf formats -> [%s][%s][%s]\n",
                format("{:.2f}", inf).c_str(), format("{:F}", inf).c_str(),
                format("{:+.2e}", inf).c_str());
  }

  const double nan_value = inf - inf;
  const std::string nan_lower = format("{}", nan_value);
  const std::string nan_upper = format("{:G}", nan_value);
  ++g_checks;
  if (nan_lower.find("nan") == std::string::npos ||
      nan_upper.find("NAN") == std::string::npos) {
    ++g_failures;
    std::printf("FAIL special: nan formats -> [%s][%s]\n", nan_lower.c_str(),
                nan_upper.c_str());
  }
}

// ---------------------------------------------------------------------------
// 一个值在"降精度"路径下也不能越界（EFMT_FLOAT_DIGIT_GROUPS 之外）
// ---------------------------------------------------------------------------
static void check_capacity_fallback() {
  char buffer[1024];
  const size_t needed = format_to(buffer, sizeof(buffer), "{:.900f}", 0.1);
  ++g_checks;
  if (needed == 0 || needed >= sizeof(buffer) || buffer[0] != '0') {
    ++g_failures;
    std::printf("FAIL capacity fallback: needed=%zu head=[%.16s]\n", needed, buffer);
  }
}

// 迭代次数可用 -DEFMT_FLOAT_CHECK_ITERS=... 放大（默认值保证脚本里跑得够快）
#ifndef EFMT_FLOAT_CHECK_ITERS
#define EFMT_FLOAT_CHECK_ITERS 120000
#endif

// ---------------------------------------------------------------------------
// 排版金标准：宽度/补零/对齐按 C 标准（glibc、newlib 行为），不用本机 printf 对拍
// ---------------------------------------------------------------------------
static void check_layout_golden() {
  struct golden {
    const char *fmt;
    double value;
    const char *expected;
  };
  const golden cases[] = {
      // %g 宽度只差一个字符（UCRT 会漏，标准必须补齐）
      {"{:21.25g}", 6.8459595401584845e+19, " 68459595401584844800"},
      {"{:22.25g}", 6.8459595401584845e+19, "  68459595401584844800"},
      {"{:<20.23g}", 2.9988438396621056e+18, "2998843839662105600 "},
      {"{:019.21g}", -94992445471134000.0, "-094992445471134000"},
      {"{:021.21g}", -94992445471134000.0, "-00094992445471134000"},
      // 定点的补零位置（符号紧贴数字、零在符号之后）
      {"{:019.2f}", -3.5, "-000000000000003.50"},
      {"{:>19.2f}", -3.5, "              -3.50"},
      {"{:<19.2f}", -3.5, "-3.50              "},
      {"{:^19.2f}", -3.5, "       -3.50       "},
      {"{:+019.2f}", 3.5, "+000000000000003.50"},
      {"{: 019.2f}", 3.5, " 000000000000003.50"},
      {"{:*>10.2f}", 3.5, "******3.50"},
      // 科学计数的指数格式（至少两位、带符号）
      {"{:.0e}", 1.0, "1e+00"},
      {"{:.0e}", 0.5, "5e-01"},
      {"{:e}", 1.0, "1.000000e+00"},
      {"{:E}", 1.0, "1.000000E+00"},
      {"{:.2e}", 1234.5678, "1.23e+03"},
      {"{:.2e}", 9999999.0, "1.00e+07"},
      {"{:g}", 100000.0, "100000"},
      {"{:.3g}", 100000.0, "1e+05"},
      {"{:.1g}", 0.5, "0.5"},
      {"{:g}", 0.5, "0.5"},
      {"{:#g}", 0.5, "0.500000"},
      {"{:.3g}", 0.0001234, "0.000123"},
      {"{:.3g}", 0.00001234, "1.23e-05"},
      // '#' 保留小数点
      {"{:#.0f}", 3.0, "3."},
      {"{:.0f}", 3.0, "3"},
      // 半偶舍入（tie → 取偶）
      {"{:.0f}", 0.5, "0"},
      {"{:.0f}", 1.5, "2"},
      {"{:.0f}", 2.5, "2"},
      {"{:.0f}", 3.5, "4"},
      {"{:.1f}", 0.25, "0.2"},
      {"{:.1f}", 0.75, "0.8"},
      {"{:.3f}", 0.0625, "0.062"},
      {"{:.2f}", 0.125, "0.12"},
      {"{:.2f}", 0.375, "0.38"},
      // 极端值
      {"{:.0f}", 1e308, "100000000000000001097906362944045541740492309677311846336810682903157585404911491537163328978494688899061249669721172515611590283743140088328307009198146046031271664502933027185697489699588559043338384466165001178426897626212945177628091195786707458122783970171784415105291802893207873272974885715430223118336"},
      {"{}", 4.9406564584124654e-324, "4.94066e-324"},
      {"{:.20f}", 0.1, "0.10000000000000000555"},
      {"{:.17g}", 0.1, "0.10000000000000001"},
      {"{:.30f}", 1.0 / 3.0, "0.333333333333333314829616256247"},
  };

  for (const golden &c : cases) {
    ++g_checks;
    const std::string actual = format(c.fmt, c.value);
    if (actual != c.expected) {
      ++g_failures;
      std::printf("FAIL golden %s -> [%s] expected [%s]\n", c.fmt,
                  actual.c_str(), c.expected);
    }
  }
}

int main() {
  check_layout_golden();
  check_edge_values();
  check_random_values(EFMT_FLOAT_CHECK_ITERS);
  check_random_specs(EFMT_FLOAT_CHECK_ITERS);
  check_specials();
  check_capacity_fallback();

  std::printf("%zu float comparisons, %zu failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
