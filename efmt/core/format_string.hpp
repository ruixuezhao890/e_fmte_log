/**
 ******************************************************************************
 * @file           : format_string.hpp
 * @author         : ruixuezhao
 * @brief          : Compile-time format string validation
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_STRING_HPP
#define FORMAT_STRING_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_parser.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace e_fmt {
// Compile-time format string helpers.
//
// 用法：把格式串交给 E_FMT_STR（或 E_FMT_DECLARE_STR），参数个数就会在编译期校验：
//
//   e_fmt::format(E_FMT_STR("x={}, y={}"), x, y);   // OK
//   e_fmt::format(E_FMT_STR("x={}, y={}"), x);      // 编译期报错
//
// 原理：宏在 lambda 里把字面量重新暴露成 static constexpr 函数，字面量因此仍然
// 是常量表达式，占位符个数可以在 static_assert 里算出来。普通 const char* 格式串
// 依旧照常可用，只是没有编译期校验。

namespace detail {

// 编译期统计替换字段数量；括号不匹配返回 (size_t)-1
constexpr size_t count_format_args(const char *str, size_t size) {
  size_t count = 0;
  for (size_t i = 0; i < size; ++i) {
    const char c = str[i];
    if (c == '{') {
      if (i + 1 < size && str[i + 1] == '{') {  // 转义 "{{"
        ++i;
        continue;
      }
      ++count;
      for (++i; i < size && str[i] != '}'; ++i) {
      }
      if (i >= size) {
        return static_cast<size_t>(-1);  // 未闭合的 '{'
      }
    } else if (c == '}') {
      if (i + 1 < size && str[i + 1] == '}') {  // 转义 "}}"
        ++i;
        continue;
      }
      return static_cast<size_t>(-1);  // 多余的 '}'
    }
  }
  return count;
}

// 以 NUL 结尾的格式串版本
constexpr size_t count_format_args(const char *str) {
  size_t size = 0;
  while (str[size] != '\0') {
    ++size;
  }
  return count_format_args(str, size);
}

// 数组版本（字面量直接调用时用得上）
template <size_t N>
constexpr size_t count_format_args(const char (&str)[N]) {
  return count_format_args(str, N - 1);
}

// 编译期格式串标记：E_FMT_STR 生成的类型都有一个 static constexpr data()
template <typename T, typename = void>
struct is_checked_format_string : std::false_type {};

template <typename T>
struct is_checked_format_string<T, std::void_t<decltype(T::data())>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_checked_format_string_v =
    is_checked_format_string<T>::value;

// ============================================================================
// 编译期格式串预解析（E_FMT_STR 快路径）
// ============================================================================
// E_FMT_STR / E_FMT_DECLARE_STR 这类【编译期已知】的格式串，把字段位置、参数
// 索引和 format_specs 在编译期全部算好；运行期执行器只做 字面量 memcpy + 参数
// 格式化，不再逐字段扫描、不再逐字段解析格式规范，也不再用运行时计数分配参数
// 下标。含 {{ / }} 转义的串退回运行期路径（语义完全一致，见 format.hpp 的
// format_checked_to）。快路径循环是共享的一份非模板函数，不会按调用点膨胀代码。
struct checked_field {
  std::uint32_t literal_len = 0;  // 本字段前的字面量长度
  std::uint32_t field_span = 0;   // 原始文本里 {...} 的长度（含大括号）
  std::uint8_t arg_id = 0;
  format_specs specs;             // 已在编译期解析好的格式规范
};

// N 至少为 1：零字段串（纯字面量）不会触发 [0] 零长数组（MSVC 拒绝）
template <std::size_t N>
struct checked_plan_t {
  checked_field fields[N];
  std::size_t text_len = 0;    // 格式串总长
  std::size_t trailing = 0;    // 最后一个字段后的字面量长度
  std::size_t field_count = 0;
  bool valid = false;          // false = 含转义/括号不合法，调用方退回运行期路径
};

template <std::size_t N>
using nonempty_checked_plan_t = checked_plan_t<(N > 0 ? N : 1)>;

constexpr std::size_t constexpr_strlen(const char *s) {
  std::size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

constexpr bool contains_escapes(const char *s, std::size_t n) {
  for (std::size_t i = 0; i + 1 < n; ++i) {
    if ((s[i] == '{' && s[i + 1] == '{') || (s[i] == '}' && s[i + 1] == '}')) {
      return true;
    }
  }
  return false;
}

// 编译期把格式串拆成 字面量段 + 字段表。与运行期 format_field 的语义逐项对齐：
//   * {<数字>} 显式索引不推进顺序计数；无数字的顺序参数自动 +1
//   * {:spec} 的 spec 用同一个 parse_format_spec 解析（constexpr 版本）
//   * 只有无转义且括号合法的串才 valid=true
template <std::size_t N>
constexpr checked_plan_t<N> make_checked_plan(const char *s, std::size_t len) {
  checked_plan_t<N> plan{};
  plan.text_len = len;
  if (contains_escapes(s, len)) {
    return plan;  // valid 保持 false
  }
  std::size_t prev = 0;
  std::size_t next_id = 0;
  std::size_t field = 0;
  std::size_t i = 0;
  while (i < len) {
    if (s[i] != '{') {
      ++i;
      continue;
    }
    // 到这里 '{' 一定是字段（"{{" 已被 contains_escapes 排除）
    if (field >= N) {
      plan.valid = false;
      return plan;
    }
    const std::size_t open = i;
    ++i;
    std::size_t arg_id = next_id;
    if (i < len && is_digit(s[i])) {
      const char *q = s + i;
      arg_id = parse_index(q, s + len);
      i = static_cast<std::size_t>(q - s);
    } else {
      ++next_id;
    }
    if (i < len && s[i] == ':') {
      const std::size_t spec_begin = ++i;
      while (i < len && s[i] != '}') {
        ++i;
      }
      if (i >= len) {
        plan.valid = false;
        return plan;
      }
      plan.fields[field].specs = parse_format_spec(s + spec_begin, s + i);
    }
    if (i >= len || s[i] != '}') {
      plan.valid = false;
      return plan;
    }
    ++i;
    plan.fields[field].literal_len = static_cast<std::uint32_t>(open - prev);
    plan.fields[field].field_span = static_cast<std::uint32_t>(i - open);
    plan.fields[field].arg_id = static_cast<std::uint8_t>(arg_id);
    ++field;
    prev = i;
  }
  plan.trailing = len - prev;
  plan.field_count = field;
  plan.valid = true;
  return plan;
}

} // namespace detail

// ============================================================================
// 格式字符串字面量包装
// ============================================================================
// 生成一个轻量空对象：既能隐式转成 std::string_view（任何接受格式串的接口都能用），
// 又保留字面量用于编译期校验与预解析。plan() 返回编译期拆好的字段表：
// 无转义且括号合法时运行期零扫描、零规范解析（见 format.hpp 的 checked 快路径）；
// 含 {{ / }} 转义时 plan.valid == false，接口自动退回运行期路径，语义不变。
#define E_FMT_STR(s)                                                          \
  [] {                                                                        \
    struct checked_format_string {                                            \
      static constexpr const char *data() { return s; }                        \
      static constexpr auto plan() {                                          \
        return e_fmt::detail::make_checked_plan<                               \
            (e_fmt::detail::count_format_args(s) > 0                           \
                 ? e_fmt::detail::count_format_args(s)                         \
                 : 1)>(s, e_fmt::detail::constexpr_strlen(s));                 \
      }                                                                        \
      constexpr operator std::string_view() const {                            \
        return std::string_view(data());                                       \
      }                                                                        \
    };                                                                         \
    return checked_format_string{};                                            \
  }()

// 声明一个具名的编译期格式串（参数个数无需手写）
#define E_FMT_DECLARE_STR(name, s) constexpr auto name = E_FMT_STR(s)

// 兼容旧写法：E_FMT_DECLARE_STR_N / E_FMT_COMPILE_TIME_STRING / E_FMT_DECLARE_FMT
// 现在的参数个数由格式串本身推导，_N 后缀不再影响行为。
#define E_FMT_DECLARE_STR_1(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_DECLARE_STR_2(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_DECLARE_STR_3(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_DECLARE_STR_4(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_DECLARE_STR_5(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_DECLARE_FMT(name, s) E_FMT_DECLARE_STR(name, s)
#define E_FMT_COMPILE_TIME_STRING(s) E_FMT_STR(s)

} // namespace e_fmt

#endif // FORMAT_STRING_HPP
