/**
 ******************************************************************************
 * @file           : traits.hpp
 * @brief          : eserde 的"取值形状"探测 + 各格式共用的取值搬运助手
 * @attention      : 依赖方向：traits.hpp → serde.hpp → efmt。efmt 与 elog 都不认识它。
 *                   这里的每一样都与具体格式无关：格式（json.hpp / cbor.hpp）只管
 *                   "往什么字节里写、从什么字节里读"，"这个类型长什么样"一律问这里 ——
 *                   所以加第二个格式时不用把这些判定抄一遍。
 *                   判定只认成员函数（data/size/clear/push_back/max_size），不认具体类型，
 *                   因此 std 与 ETL 容器走的是同一套代码。
 * @date           : 26-10-02
 ******************************************************************************
 */

#ifndef ESERDE_TRAITS_HPP
#define ESERDE_TRAITS_HPP

#include <eserde/serde.hpp>

#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eserde::detail {

// ---------------------------------------------------------------------------
// 形状判定：只认成员函数，std 与 ETL 通用
// ---------------------------------------------------------------------------
// 字符串：有 data()/size()，元素是 char
template <typename T, typename = void> struct string_of {
  static constexpr bool value = false;
};

template <typename T>
struct string_of<T, std::void_t<decltype(std::declval<const T &>().data()),
                                decltype(std::declval<const T &>().size())>> {
  using char_type =
      std::remove_cv_t<std::remove_pointer_t<decltype(std::declval<const T &>().data())>>;
  static constexpr bool value = std::is_same<char_type, char>::value;
};

template <typename T> inline constexpr bool is_string_like_v = string_of<T>::value;

// 可写字符串：再加 clear() / push_back(char) / assign(const char*, size_t)
// （assign 用来把 etl::vector<char> 这类"长得像字符串的容器"排除在外）
template <typename T, typename = void> struct is_writable_string : std::false_type {};

template <typename T>
struct is_writable_string<
    T, std::void_t<decltype(std::declval<T &>().clear()),
                   decltype(std::declval<T &>().push_back(char{})),
                   decltype(std::declval<T &>().assign(static_cast<const char *>(nullptr),
                                                       std::size_t{}))>>
    : std::bool_constant<string_of<T>::value> {};

// 容器：有 begin()/end()
template <typename T, typename = void> struct has_range : std::false_type {};

template <typename T>
struct has_range<T, std::void_t<decltype(std::declval<T &>().begin()),
                                decltype(std::declval<T &>().end())>> : std::true_type {};

// 可增长容器：再加 clear() / push_back(元素)
template <typename T, typename = void> struct is_growable_range : std::false_type {};

template <typename T>
struct is_growable_range<
    T, std::void_t<decltype(std::declval<T &>().clear()),
                   decltype(std::declval<T &>().push_back(*std::declval<T &>().begin()))>>
    : std::true_type {};

// 能问出元素个数（二进制格式写"定长容器头"要用；json 用不上）
template <typename T, typename = void> struct has_size : std::false_type {};

template <typename T>
struct has_size<T, std::void_t<decltype(std::declval<T &>().size())>> : std::true_type {};

template <typename T, typename = void> struct has_max_size : std::false_type {};

template <typename T>
struct has_max_size<T, std::void_t<decltype(std::declval<T &>().max_size())>> : std::true_type {};

// char 指针（const char* / char*）：按字符串处理
template <typename T> struct is_char_pointer : std::false_type {};

template <typename T>
struct is_char_pointer<T *>
    : std::bool_constant<std::is_same<std::remove_cv_t<T>, char>::value> {};

// 注册过的结构体 / 枚举（注册 = 类型上有 E_FMT_DERIVE / E_FMT_DERIVE_ENUM）
template <typename T>
inline constexpr bool is_object_v = ::eserde::is_registered_v<T> && !std::is_enum<T>::value;

template <typename T>
inline constexpr bool is_registered_enum_v =
    ::eserde::is_registered_v<T> && std::is_enum<T>::value;

// ---------------------------------------------------------------------------
// 取值搬运助手（两个格式共用）
// ---------------------------------------------------------------------------
// 目标类型装得下吗（neg = 有没有负号，mag = 无符号幅度）
template <typename D> constexpr bool integer_fits(unsigned long long mag, bool neg) {
  if constexpr (std::is_same_v<D, bool>) {
    return !neg && mag <= 1ULL;
  } else if constexpr (std::is_signed_v<D>) {
    const unsigned long long lim =
        neg ? static_cast<unsigned long long>(std::numeric_limits<D>::max()) + 1ULL
            : static_cast<unsigned long long>(std::numeric_limits<D>::max());
    return mag <= lim;
  } else {
    return !neg && mag <= static_cast<unsigned long long>(std::numeric_limits<D>::max());
  }
}

// 幅度 + 符号 → 目标整数类型
template <typename D> constexpr D integer_value(unsigned long long mag, bool neg) {
  return neg ? static_cast<D>(0ULL - mag) : static_cast<D>(mag);
}

// 往容器里推一个元素，满了就如实报 false（定长容器的溢出行为不保证，所以先问 max_size）
template <typename C, typename V> bool push_checked(C &c, V &&v) {
  if constexpr (has_max_size<C>::value) {
    if (c.size() >= c.max_size()) return false;
  }
  c.push_back(std::forward<V>(v));
  return true;
}

}  // namespace eserde::detail

#endif  // ESERDE_TRAITS_HPP
