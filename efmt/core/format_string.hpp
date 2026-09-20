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

} // namespace detail

// ============================================================================
// 格式字符串字面量包装
// ============================================================================
// 生成一个轻量空对象：既能隐式转成 std::string_view（任何接受格式串的接口都能用），
// 又保留字面量用于编译期校验。
#define E_FMT_STR(s)                                                          \
  [] {                                                                        \
    struct checked_format_string {                                            \
      static constexpr const char *data() { return s; }                        \
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
