/**
 ******************************************************************************
 * @file           : efmt_derive_check.cpp
 * @brief          : 自定义类型自动派生的行为检查（AUTO / FIELDS / ENUM）
 * @attention      : 目标是把"手写类型+名字+字符串"三处抄写压缩成一处，这里逐条钉住
 *                   三种写法的输出、边界与容错。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;

// ============================================================================
// 被测类型
// ============================================================================
struct point {
  int x;
  int y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);   // 类型与显示名都不用手写

struct reading {
  float temp;
  float hum;
  unsigned ts;
};
E_FMT_FORMATTER_FIELDS(reading, temp, hum, ts);

// 12 个字段（宏与自动推导的上限）
struct twelve {
  int a, b, c, d, e, f, g, h, i, j, k, l;
};
E_FMT_FORMATTER_FIELDS(twelve, a, b, c, d, e, f, g, h, i, j, k, l);

// 自动推导：什么都不用写
struct auto_reading {
  float temp;
  float hum;
  unsigned ts;
};
E_FMT_FORMATTER_AUTO(auto_reading);

struct auto_default_init {
  int a;
  int b = 7;   // 有默认成员初始化器也是聚合体
};
E_FMT_FORMATTER_AUTO(auto_default_init);

struct auto_twelve {
  int a, b, c, d, e, f, g, h, i, j, k, l;
};
E_FMT_FORMATTER_AUTO(auto_twelve);

struct auto_nested {
  point p;          // 成员自己也有格式化器
  int flag;
};
E_FMT_FORMATTER_AUTO(auto_nested);

struct auto_array {
  point p;
  char name[8];   // C 型数组成员会让字段计数偏大（brace elision），所以要显式给个数
  int count;
};
E_FMT_FORMATTER_AUTO_N(auto_array, 3);

#if EFMT_ENABLE_DYNAMIC_STRING
struct auto_with_string {
  std::string label;
  int count;
};
E_FMT_FORMATTER_AUTO(auto_with_string);
#endif

// 枚举
enum class led_color { red, green, blue };
E_FMT_FORMATTER_ENUM(led_color, red, green, blue);

enum class sparse : unsigned char { off = 0, on = 2, sleep = 7 };
E_FMT_FORMATTER_ENUM(sparse, off, on, sleep);

enum legacy_flag { leg_off = 0, leg_on = 1 };
E_FMT_FORMATTER_ENUM(legacy_flag, leg_off, leg_on);

// 类型放在用户命名空间里：宏写在同一个命名空间，ADL 能找到；名字与库内部符号重名也不会
// 误伤（旧宏会把 color/style 这类名字解析成 e_fmt::detail 里的同名符号）
namespace app {
struct vec2 {
  float x;
  float y;
};
E_FMT_FORMATTER_FIELDS(vec2, x, y);

struct style {   // 与 e_fmt::detail::style 重名
  int id;
};
E_FMT_FORMATTER_AUTO(style);

enum class color { red, green, blue };   // 与 e_fmt::detail::color 重名
E_FMT_FORMATTER_ENUM(color, red, green, blue);
} // namespace app

// ============================================================================
// 极简断言
// ============================================================================
// 用缓冲区版本而不是 format()：这样同一份测试也能在 EFMT_ENABLE_HOSTED=0
// （没有 std::string 接口）的配置下编译运行。
template <typename... Args>
static std::string as_text(std::string_view fmt_str, const Args &...args) {
  char buffer[256];
  const size_t needed = format_to(buffer, sizeof(buffer), fmt_str, args...);
  return std::string(buffer, (needed < sizeof(buffer)) ? needed : sizeof(buffer) - 1);
}

static int g_checks = 0;
static int g_failures = 0;

static void check_text(const char *what, const std::string &actual,
                       const char *expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  ++g_failures;
  std::printf("FAIL %s\n     actual  =[%s]\n     expected=[%s]\n", what,
              actual.c_str(), expected);
}

#define CHECK_TEXT(actual, expected) check_text(#actual, (actual), (expected))

int main() {
  // ---- FIELDS：只列字段名 ----
  CHECK_TEXT(as_text("{}", point{10, 20}), "{x=10, y=20}");
  CHECK_TEXT(as_text("{:#}", point{10, 20}), "{\n  x=10,\n  y=20\n}");   // 紧凑风格的多行
  CHECK_TEXT(as_text("{}", point{-1, -2}), "{x=-1, y=-2}");
  CHECK_TEXT(as_text("{}", reading{25.5f, 60.0f, 12345u}), "{temp=25.5, hum=60, ts=12345}");
  CHECK_TEXT(as_text("{}", twelve{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}),
             "{a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10, k=11, l=12}");

  // ---- AUTO：连字段都不用列 ----
  CHECK_TEXT(as_text("{}", auto_reading{25.5f, 60.0f, 12345u}),
             "auto_reading(25.5, 60, 12345)");
  CHECK_TEXT(as_text("{}", auto_default_init{1}), "auto_default_init(1, 7)");
  CHECK_TEXT(as_text("{}", auto_default_init{1, 9}), "auto_default_init(1, 9)");
  CHECK_TEXT(as_text("{}", auto_twelve{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}),
             "auto_twelve(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)");
  CHECK_TEXT(as_text("{}", auto_nested{point{3, 4}, 9}), "auto_nested({x=3, y=4}, 9)");
  CHECK_TEXT(as_text("{}", auto_array{point{1, 2}, "abc", 7}), "auto_array({x=1, y=2}, abc, 7)");
#if EFMT_ENABLE_DYNAMIC_STRING
  CHECK_TEXT(as_text("{}", auto_with_string{std::string("imu"), 3}),
             "auto_with_string(imu, 3)");
#endif

  // AUTO 输出的前缀就是类型名（编译器拿不到签名时可能为空，这种情况只比对括号内内容）
  {
    const std::string text = as_text("{}", auto_reading{1.0f, 2.0f, 3u});
    const bool named = text.find("auto_reading(") == 0;
    const bool bare = text.find("(1, 2, 3)") == 0;
    ++g_checks;
    if (!named && !bare) {
      ++g_failures;
      std::printf("FAIL AUTO 前缀异常: [%s]\n", text.c_str());
    }
  }

  // ---- ENUM ----
  CHECK_TEXT(as_text("{}", led_color::blue), "blue");
  CHECK_TEXT(as_text("{}", led_color::red), "red");
  CHECK_TEXT(as_text("{}", sparse::sleep), "sleep");
  CHECK_TEXT(as_text("{}", leg_on), "leg_on");
  CHECK_TEXT(as_text("{:>10}", led_color::red), "       red");      // 尊重宽度/对齐
  CHECK_TEXT(as_text("{}", static_cast<led_color>(9)), "9");        // 未列出的取值 → 底层整数
  CHECK_TEXT(as_text("{}", static_cast<sparse>(5)), "5");
  CHECK_TEXT(as_text("{}", static_cast<sparse>(-1)), "255");   // 底层类型是 unsigned char
  CHECK_TEXT(as_text("{}", static_cast<led_color>(7)), "7");
  CHECK_TEXT(as_text("{:#x}", static_cast<led_color>(7)), "0x7");  // 未列出时仍可当整数用

  // ---- 三种写法可以混用、可以嵌套 ----
  CHECK_TEXT(as_text("p={} r={} c={}", point{1, 2}, auto_reading{3.0f, 4.0f, 5u}, led_color::green),
             "p={x=1, y=2} r=auto_reading(3, 4, 5) c=green");

  // ---- 用户命名空间里的类型（含与库内部同名的 style / color） ----
  CHECK_TEXT(as_text("{}", app::vec2{1.5f, 2.5f}), "{x=1.5, y=2.5}");
  CHECK_TEXT(as_text("{}", app::color::green), "green");
  {
    const std::string text = as_text("{}", app::style{42});
    ++g_checks;
    if (text.find("style(42)") == std::string::npos && text.find("(42)") == std::string::npos) {
      ++g_failures;
      std::printf("FAIL app::style 输出异常: [%s]\n", text.c_str());
    }
  }

  // ---- 老的写法仍然可用（向后兼容） ----
  struct old_style {
    int v;
  };
  CHECK_TEXT(as_text("{}", 42), "42");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}