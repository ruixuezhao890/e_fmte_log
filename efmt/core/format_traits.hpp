/**
 ******************************************************************************
 * @file           : format_traits.hpp
 * @author         : ruixuezhao
 * @brief          : Formatters for custom types with adapter pattern
 * @attention      : Reorganized to avoid circular dependency
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_TRAITS_HPP
#define FORMAT_TRAITS_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <type_traits>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <string_view>

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

#if EFMT_ENABLE_STREAM_FALLBACK
#include <sstream>
#endif

namespace e_fmt::detail {
// Type dispatch and extension points for efmt.
// A type can participate through:
// 1. built-in specializations
// 2. default_formatter specializations/macros
// 3. fallback operator<< support

// ============================================================================
// 前向声明
// ============================================================================
template <typename T>
struct formatter;

// ============================================================================
// 默认格式化器 - 必须在 has_formatter 之前定义
// ============================================================================
template <typename T, typename = void>
struct default_formatter {
  static void format(format_context &ctx, const format_specs &,
                     const T &value) {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "obj@%p",
                       static_cast<const void *>(&value));
    ctx.write_chars(buffer, static_cast<size_t>(len));
  }
};

// ============================================================================
// 内置类型列表（这些类型在 formatter.hpp 中有 formatter 特化）
// ============================================================================
template <typename T> struct is_builtin_type : std::false_type {};
template <> struct is_builtin_type<int> : std::true_type {};
template <> struct is_builtin_type<unsigned int> : std::true_type {};
template <> struct is_builtin_type<long> : std::true_type {};
template <> struct is_builtin_type<unsigned long> : std::true_type {};
template <> struct is_builtin_type<long long> : std::true_type {};
template <> struct is_builtin_type<unsigned long long> : std::true_type {};
template <> struct is_builtin_type<float> : std::true_type {};
template <> struct is_builtin_type<double> : std::true_type {};
template <> struct is_builtin_type<bool> : std::true_type {};
template <> struct is_builtin_type<char> : std::true_type {};
template <> struct is_builtin_type<const char*> : std::true_type {};
#if EFMT_ENABLE_DYNAMIC_STRING
template <> struct is_builtin_type<std::string> : std::true_type {};
#endif
template <> struct is_builtin_type<std::string_view> : std::true_type {};

// ============================================================================
// has_formatter - 检测是否有可用的格式化器（formatter 特化或 default_formatter 特化）
// ============================================================================
template <typename T, typename = void>
struct has_formatter : std::false_type {};

// 内置类型有 formatter 特化
template <typename T>
struct has_formatter<T, std::enable_if_t<is_builtin_type<std::decay_t<T>>::value>>
    : std::true_type {};

// 检测是否有 default_formatter 特化（由 E_FMT_FORMATTER_FN 等宏创建）
template <typename T>
struct has_formatter<T, std::void_t<decltype(
    default_formatter<std::decay_t<T>>::format(
        std::declval<format_context &>(),
        std::declval<const format_specs &>(),
        std::declval<const std::decay_t<T> &>())
), std::enable_if_t<!is_builtin_type<std::decay_t<T>>::value>>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_formatter_v = has_formatter<T>::value;

// ============================================================================
// formatter 主模板定义 - 委托给 default_formatter
// ============================================================================
template <typename T>
struct formatter {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    default_formatter<T>::format(ctx, specs, value);
  }
};

// ============================================================================
// 成员描述符
// ============================================================================
// Low-level building block used by the E_FMT_FORMATTER_1/2/3 helper macros.
template <typename ClassType, typename MemberType, MemberType ClassType::*MemberPtr>
struct member_descriptor {
  using class_type = ClassType;
  using member_type = MemberType;
  static constexpr MemberType ClassType::*ptr = MemberPtr;
  const char *name;

  constexpr member_descriptor(const char *n) : name(n) {}

  constexpr const MemberType &get(const ClassType &obj) const {
    return obj.*ptr;
  }
};

// ============================================================================
// 结构化格式化器辅助
// ============================================================================
template <typename... Descriptors>
struct member_descriptor_list {
  std::tuple<Descriptors...> descriptors;

  constexpr member_descriptor_list(Descriptors... descs)
      : descriptors(std::make_tuple(descs...)) {}

  template <typename T, size_t... Indices>
  void format_members(format_context &ctx, const T &value,
                      std::index_sequence<Indices...>) const {
    bool first = true;
    (([&](const auto &desc) {
      if (!first) {
        ctx.write_str(", ");
      }
      first = false;

      ctx.write_str(desc.name);
      ctx.write_char('=');

      const auto &member_value = desc.get(value);
      format_member_value(ctx, member_value);
     }(std::get<Indices>(descriptors))),
     ...);
  }

  template <typename MemberType>
  static void format_member_value(format_context &ctx,
                                 const MemberType &value) {
    format_specs default_specs;
    formatter<MemberType>::format(ctx, default_specs, value);
  }
};

// ============================================================================
// 宏定义
// ============================================================================

// 1. 使用 lambda 的格式化函数
// User-facing macros that make custom type support cheap to add.
#define E_FMT_FORMATTER_FN(Type, Fn) \
  namespace e_fmt::detail { \
  template <> \
  struct default_formatter<Type> { \
    static void format(format_context &e_fmt_ctx, const format_specs &e_fmt_specs, \
                       const Type &e_fmt_value) { \
      Fn(e_fmt_ctx, e_fmt_specs, e_fmt_value); \
    } \
  }; \
  }

// 2. 固定成员数量的格式化
#define E_FMT_FORMATTER_1(Type, T1, M1, N1) \
  namespace e_fmt::detail { \
  template <> \
  struct default_formatter<Type> { \
    static void format(format_context &e_fmt_ctx, const format_specs &e_fmt_specs, \
                       const Type &e_fmt_value) { \
      (void)e_fmt_specs; \
      member_descriptor_list<member_descriptor<Type, T1, &Type::M1>> \
        list(member_descriptor<Type, T1, &Type::M1>(N1)); \
      e_fmt_ctx.write_char('{'); \
      list.format_members(e_fmt_ctx, e_fmt_value, std::make_index_sequence<1>{}); \
      e_fmt_ctx.write_char('}'); \
    } \
  }; \
  }

#define E_FMT_FORMATTER_2(Type, T1, M1, N1, T2, M2, N2) \
  namespace e_fmt::detail { \
  template <> \
  struct default_formatter<Type> { \
    static void format(format_context &e_fmt_ctx, const format_specs &e_fmt_specs, \
                       const Type &e_fmt_value) { \
      (void)e_fmt_specs; \
      member_descriptor_list<\
        member_descriptor<Type, T1, &Type::M1>,\
        member_descriptor<Type, T2, &Type::M2>\
      > list(\
        member_descriptor<Type, T1, &Type::M1>(N1),\
        member_descriptor<Type, T2, &Type::M2>(N2)\
      );\
      e_fmt_ctx.write_char('{');\
      list.format_members(e_fmt_ctx, e_fmt_value, std::make_index_sequence<2>{});\
      e_fmt_ctx.write_char('}');\
    }\
  };\
  }

#define E_FMT_FORMATTER_3(Type, T1, M1, N1, T2, M2, N2, T3, M3, N3) \
  namespace e_fmt::detail { \
  template <> \
  struct default_formatter<Type> { \
    static void format(format_context &e_fmt_ctx, const format_specs &e_fmt_specs, \
                       const Type &e_fmt_value) { \
      (void)e_fmt_specs; \
      member_descriptor_list<\
        member_descriptor<Type, T1, &Type::M1>,\
        member_descriptor<Type, T2, &Type::M2>,\
        member_descriptor<Type, T3, &Type::M3>\
      > list(\
        member_descriptor<Type, T1, &Type::M1>(N1),\
        member_descriptor<Type, T2, &Type::M2>(N2),\
        member_descriptor<Type, T3, &Type::M3>(N3)\
      );\
      e_fmt_ctx.write_char('{');\
      list.format_members(e_fmt_ctx, e_fmt_value, std::make_index_sequence<3>{});\
      e_fmt_ctx.write_char('}');\
    }\
  };\
  }

// ============================================================================
// 流式输出适配器
// ============================================================================
#if EFMT_ENABLE_STREAM_FALLBACK
template <typename T>
struct stream_formatter {
  // Stream fallback is convenient but typically heavier than a dedicated
  // formatter, so projects can override it for hot paths.
  static void format(format_context &ctx, const format_specs &,
                     const T &value) {
    std::ostringstream oss;
    oss << value;
    std::string str = oss.str();
    ctx.write_str(str);
  }
};

// 检测是否有 operator<<
template <typename T, typename = void>
struct has_stream_formatter : std::false_type {};

template <typename T>
struct has_stream_formatter<T, std::void_t<decltype(
    std::declval<std::ostringstream &>() << std::declval<const T &>())>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_stream_formatter_v = has_stream_formatter<T>::value;

// ============================================================================
// has_formatter_no_default - 检测是否有 formatter 特化（不包括 default_formatter）
// ============================================================================
template <typename T, typename = void>
struct has_formatter_no_default : std::false_type {};

// 仅检测 formatter 特化，不是 default_formatter
template <typename T>
struct has_formatter_no_default<T, std::enable_if_t<is_builtin_type<std::decay_t<T>>::value>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_formatter_no_default_v = has_formatter_no_default<T>::value;

// 如果类型有 operator<< 且没有 formatter 特化，使用流式格式化器
template <typename T>
struct default_formatter<T, std::enable_if_t<
    has_stream_formatter_v<T> &&
    !has_formatter_no_default_v<T>>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    stream_formatter<T>::format(ctx, specs, value);
  }
};
#endif

} // namespace e_fmt::detail

// ============================================================================
// 将 formatter 带入 e_fmt 命名空间，方便用户使用
// ============================================================================
namespace e_fmt {

template <typename T>
using formatter = detail::formatter<T>;

} // namespace e_fmt

#endif // FORMAT_TRAITS_HPP
