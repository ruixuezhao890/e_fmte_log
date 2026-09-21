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
#include <cstring>
#include <tuple>
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
// 十六进制地址输出
// ============================================================================
// 指针与"无格式化器类型"共用：自己按位生成十六进制，避免为了 %p 把 libc 的
// printf 链进固件（newlib-nano 下那是一大块代码 + 一堆 locale 状态）。
inline void write_hex_address(format_context &ctx, const format_specs &specs,
                              const void *address, const char *null_text) {
  if (address == nullptr) {
    ctx.write_aligned(null_text, specs);
    return;
  }

  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  char buffer[2 + sizeof(uintptr_t) * 2];
  buffer[0] = '0';
  buffer[1] = 'x';

  size_t len = 2;
  bool leading = true;
  for (int shift = static_cast<int>(sizeof(uintptr_t) * 8) - 4; shift >= 0;
       shift -= 4) {
    const unsigned digit =
        static_cast<unsigned>((value >> shift) & static_cast<uintptr_t>(0xF));
    if (digit != 0 || !leading) {
      buffer[len++] = "0123456789abcdef"[digit];
      leading = false;
    }
  }
  if (leading) {
    buffer[len++] = '0';
  }
  ctx.write_aligned(std::string_view(buffer, len), specs);
}

// ============================================================================
// 派生格式化函数（ADL 定制点）
// ============================================================================
// 自动派生的宏（E_FMT_FORMATTER_AUTO / _FIELDS / _ENUM）展开出来的是一个**自由函数**：
//
//   void efmt_derive_format(const MyType&, format_context&, const format_specs&);
//
// 库这边用 ADL 找它。这样做的原因：宏不必再打开 e_fmt::detail 命名空间，用户类型名
// 就不会被库内部同名符号（如 detail::color / detail::style）遮蔽 —— 旧宏那样展开时，
// 一个叫 color 的用户类型会静默给库自己的枚举加格式化器。
namespace derive_probe {
void efmt_derive_format();  // 占位声明：保证名字在解析期可见（不定义，仅参与查找）
} // namespace derive_probe

template <typename T, typename = void>
struct has_derived_formatter : std::false_type {};

template <typename T>
struct has_derived_formatter<
    T, std::void_t<decltype(efmt_derive_format(
           std::declval<const T &>(), std::declval<format_context &>(),
           std::declval<const format_specs &>()))>> : std::true_type {};

template <typename T>
inline constexpr bool has_derived_formatter_v = has_derived_formatter<T>::value;

// ============================================================================
// 类型内一行写法（E_FMT_FIELDS）的探测
// ============================================================================
// E_FMT_FIELDS(x, y, z) 写在类型内部时会生成静态函数 efmt_field_names()。
// 库直接读它：不需要 ADL、不需要特化，命名空间/限定名/遮蔽问题一概不存在，
// 也能用在模板结构体里（这是 E_FMT_DERIVE 覆盖不到的边界）。
template <typename T, typename = void>
struct has_field_names : std::false_type {};

template <typename T>
struct has_field_names<T, std::void_t<decltype(T::efmt_field_names())>> : std::true_type {};

template <typename T>
inline constexpr bool has_field_names_v = has_field_names<T>::value;

// 定义在 format_derive.hpp；这里只要声明（模板体在实例化时已经可见）
template <typename T>
void format_via_field_names(format_context &ctx, const format_specs &specs,
                            const T &value);

// ============================================================================
// 默认格式化器 - 必须在 has_formatter 之前定义
// ============================================================================
// 优先用派生出来的格式化函数；没有再退化成"类型名 + 地址"方便定位（不静默输出空白）。
template <typename T, typename = void>
struct default_formatter {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    if constexpr (has_derived_formatter_v<T>) {
      efmt_derive_format(value, ctx, specs);  // ADL：找到用户侧展开的自由函数
    } else if constexpr (has_field_names_v<T>) {
      format_via_field_names(ctx, specs, value);  // 类型内一行 E_FMT_FIELDS(...)
    } else {
      ctx.write_str("obj@");
      write_hex_address(ctx, specs, static_cast<const void *>(&value), "0x0");
    }
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
  // 只有这个主模板带这个标记：用户可以据此（库内部据它判断）知道 formatter<T>
  // 是否被特化过 —— 枚举的分发需要这个信息（见 formatter.hpp 的枚举入口）。
  using e_fmt_primary_formatter = void;

  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    default_formatter<T>::format(ctx, specs, value);
  }
};

// formatter<T> 是否被特化过（含内置类型与用户特化）
template <typename T, typename = void>
struct has_formatter_specialization : std::true_type {};

template <typename T>
struct has_formatter_specialization<
    T, std::void_t<typename formatter<T>::e_fmt_primary_formatter>>
    : std::false_type {};

template <typename T>
inline constexpr bool has_formatter_specialization_v =
    has_formatter_specialization<T>::value;

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

// 如果类型有 operator<< 且没有 formatter 特化，使用流式格式化器。
// 注意排除"已经有派生格式化函数"的类型：无作用域枚举因为能隐式转成 int，会被
// ostream 的 operator<<(int) 接住，从而抢走 E_FMT_FORMATTER_ENUM 的优先级。
template <typename T>
struct default_formatter<T, std::enable_if_t<
    has_stream_formatter_v<T> &&
    !has_formatter_no_default_v<T> &&
    !has_derived_formatter_v<T>>> {
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
