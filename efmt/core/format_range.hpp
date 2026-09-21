/**
 ******************************************************************************
 * @file           : format_range.hpp
 * @author         : ruixuezhao
 * @brief          : Container/Range formatter for iterator-based containers
 * @attention      : C++17 compatible - supports any container with iterators
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_RANGE_HPP
#define FORMAT_RANGE_HPP

#include <middleware/efmt/core/format_base.hpp>

// 容器 / tuple / pair 的自动格式化。嵌入式默认关闭（EFMT_ENABLE_CONTAINER_FORMAT=0）：
// 省 Flash，也不会把 <iterator>/<tuple> 拖进 MCU 工程；需要时用
// -DEFMT_ENABLE_CONTAINER_FORMAT=1 打开。
#if EFMT_ENABLE_CONTAINER_FORMAT

#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <middleware/efmt/core/formatter.hpp>
#include <type_traits>
#include <iterator>
#include <tuple>

namespace e_fmt::detail {
// Automatic formatter support for iterator-based containers and tuples.
// Formatting is recursive, so nested containers work as long as their element
// types are themselves formattable.

// ============================================================================
// 类型特征检测 - 检测容器类型
// ============================================================================

// 检测是否有 iterator 类型定义
template <typename T, typename = void>
struct has_iterator_type : std::false_type {};

template <typename T>
struct has_iterator_type<T, std::void_t<typename T::iterator>> : std::true_type {};

template <typename T>
inline constexpr bool has_iterator_type_v = has_iterator_type<T>::value;

// 检测是否有 const_iterator 类型定义
template <typename T, typename = void>
struct has_const_iterator_type : std::false_type {};

template <typename T>
struct has_const_iterator_type<T, std::void_t<typename T::const_iterator>> : std::true_type {};

template <typename T>
inline constexpr bool has_const_iterator_type_v = has_const_iterator_type<T>::value;

// 检测是否有 begin() 和 end() 方法
template <typename T, typename = void>
struct has_begin_end : std::false_type {};

template <typename T>
struct has_begin_end<T, std::void_t<
    decltype(std::begin(std::declval<T>())),
    decltype(std::end(std::declval<T>()))
>> : std::true_type {};

template <typename T>
inline constexpr bool has_begin_end_v = has_begin_end<T>::value;

// 检测是否是 pair 类型（用于 map）
template <typename T>
struct is_pair : std::false_type {};

template <typename T, typename U>
struct is_pair<std::pair<T, U>> : std::true_type {};

template <typename T>
inline constexpr bool is_pair_v = is_pair<T>::value;

// 检测是否是 tuple
template <typename T>
struct is_tuple : std::false_type {};

template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
inline constexpr bool is_tuple_v = is_tuple<T>::value;

// 检测是否是容器（有迭代器但不是字符串）
template <typename T>
struct is_container {
  static constexpr bool value =
      has_begin_end_v<T> &&
      !is_pair_v<T> &&
      !is_tuple_v<T> &&
      !std::is_same_v<std::decay_t<T>, std::string> &&
      !std::is_same_v<std::decay_t<T>, std::string_view> &&
      !std::is_same_v<std::decay_t<T>, const char*> &&
      !std::is_same_v<std::decay_t<T>, char*>;
};

template <typename T>
inline constexpr bool is_container_v = is_container<T>::value;

// 检测是否是关联容器（map/set）
template <typename T, typename = void>
struct is_map_container : std::false_type {};

// 检测 map 类型（value_type 是 pair）
template <typename T>
struct is_map_container<T, std::enable_if_t<
    is_container_v<T> &&
    is_pair_v<typename T::value_type>,
    void>
> : std::true_type {};

template <typename T>
inline constexpr bool is_map_container_v = is_map_container<T>::value;

// ============================================================================
// 范围格式化器
// ============================================================================

// 序列容器格式化器 - 输出 [a, b, c]
template <typename Container>
class sequence_formatter {
public:
  static void format(format_context &ctx, const format_specs &,
                     const Container &container) {
    ctx.write_char('[');

    bool first = true;
    for (const auto &item : container) {
      if (!first) {
        ctx.write_str(", ");
      }
      first = false;

      // 递归格式化每个元素
      // Reuse the element's own formatter, enabling nested containers.
      format_specs default_specs;
      formatter<std::decay_t<decltype(item)>>::format(ctx, default_specs, item);
    }

    ctx.write_char(']');
  }
};

// 关联容器（map）格式化器 - 输出 {key: value, ...}
template <typename Container>
class map_formatter {
public:
  static void format(format_context &ctx, const format_specs &,
                     const Container &container) {
    ctx.write_char('{');

    bool first = true;
    for (const auto &item : container) {
      if (!first) {
        ctx.write_str(", ");
      }
      first = false;

      // 格式化 key: value
      format_specs default_specs;
      formatter<std::decay_t<decltype(item.first)>>::format(ctx, default_specs, item.first);
      ctx.write_str(": ");
      formatter<std::decay_t<decltype(item.second)>>::format(ctx, default_specs, item.second);
    }

    ctx.write_char('}');
  }
};

// Tuple 格式化器 - 输出 (a, b, c)
template <typename Tuple>
class tuple_formatter {
public:
  static void format(format_context &ctx, const format_specs &,
                     const Tuple &tuple) {
    ctx.write_char('(');
    format_tuple_impl(ctx, tuple, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    ctx.write_char(')');
  }

private:
  template <size_t... Indices>
  static void format_tuple_impl(format_context &ctx, const Tuple &tuple,
                                std::index_sequence<Indices...>) {
    bool first = true;
    ([&](auto index_const) {
      constexpr size_t index = decltype(index_const)::value;
      if (!first) {
        ctx.write_str(", ");
      }
      first = false;

      format_specs default_specs;
      formatter<std::decay_t<decltype(std::get<index>(tuple))>>::format(
          ctx, default_specs, std::get<index>(tuple));
    }(std::integral_constant<size_t, Indices>{}), ...);
  }
};

// ============================================================================
// 默认格式化器特化 - 容器类型
// ============================================================================

// 序列容器（vector, list, deque, set 等）
// Hook container support into the generic default_formatter path.
template <typename T>
struct default_formatter<T, std::enable_if_t<
    is_container_v<T> &&
    !is_map_container_v<T> &&
    !is_tuple_v<T>
>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    sequence_formatter<T>::format(ctx, specs, value);
  }
};

// 关联容器（map, unordered_map）
template <typename T>
struct default_formatter<T, std::enable_if_t<
    is_container_v<T> &&
    is_map_container_v<T>
>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    map_formatter<T>::format(ctx, specs, value);
  }
};

// Tuple 特化
template <typename... Ts>
struct default_formatter<std::tuple<Ts...>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const std::tuple<Ts...> &value) {
    tuple_formatter<std::tuple<Ts...>>::format(ctx, specs, value);
  }
};

// Pair 特化
template <typename T, typename U>
struct default_formatter<std::pair<T, U>> {
  static void format(format_context &ctx, const format_specs &,
                     const std::pair<T, U> &value) {
    ctx.write_char('(');
    format_specs default_specs;

    formatter<T>::format(ctx, default_specs, value.first);
    ctx.write_str(": ");
    formatter<U>::format(ctx, default_specs, value.second);

    ctx.write_char(')');
  }
};

} // namespace e_fmt::detail

#endif // EFMT_ENABLE_CONTAINER_FORMAT

#endif // FORMAT_RANGE_HPP
