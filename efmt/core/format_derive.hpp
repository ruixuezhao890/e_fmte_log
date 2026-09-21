/**
 ******************************************************************************
 * @file           : format_derive.hpp
 * @author         : ruixuezhao
 * @brief          : 自定义类型的"自动派生"格式化（对标 Rust 的 #[derive(Debug)]）
 * @attention      : 纯模板 + 宏，未使用时不产生任何代码。三种写法按"越自动越省心"排：
 *                     E_FMT_FORMATTER_AUTO(Type)              零声明（字段全自动）
 *                     E_FMT_FORMATTER_FIELDS(Type, a, b, c)   只列字段名（名字自动转字符串）
 *                     E_FMT_FORMATTER_ENUM(Type, A, B, C)     枚举取值自动转名字
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_DERIVE_HPP
#define FORMAT_DERIVE_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_specs.hpp>

#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace e_fmt::detail {

// ============================================================================
// 为什么需要它
// ============================================================================
// 旧写法要把"成员类型 + 成员名 + 显示名"抄三遍：
//     E_FMT_FORMATTER_2(Point, int, x, "x", int, y, "y");
// 抄错任何一处都是编译错误（类型）或输出错（字符串）。
// 这里把三处压到一处：
//     E_FMT_FORMATTER_FIELDS(Point, x, y);   // 类型由 &Point::x 推导，显示名由 #x 生成
// 再进一步，纯聚合体连字段都不用列：
//     E_FMT_FORMATTER_AUTO(Point);           // 输出 Point(10, 20)

// ============================================================================
// 成员指针 → 类型信息
// ============================================================================
// C++17 的 auto 非类型模板参数让我们能从 &Type::member 反推成员类型，
// 于是调用方不需要再写一遍类型。
template <auto MemberPtr> struct member_pointer_traits;

template <typename Class, typename Member, Member Class::*Ptr>
struct member_pointer_traits<Ptr> {
  using class_type = Class;
  using member_type = Member;
};

// 一个字段的描述符：接口与旧 member_descriptor 一致，但类型是推导出来的
template <auto MemberPtr> struct field_descriptor {
  using class_type = typename member_pointer_traits<MemberPtr>::class_type;
  using member_type = typename member_pointer_traits<MemberPtr>::member_type;

  const char *name;

  constexpr explicit field_descriptor(const char *field_name) : name(field_name) {}

  constexpr const member_type &get(const class_type &obj) const {
    return obj.*MemberPtr;
  }
};

// ============================================================================
// 编译期类型名（给 AUTO 用）
// ============================================================================
// 从编译器的签名宏里截出类型名：不需要 RTTI，也不占运行时开销。
// 拿不到时返回空串（那就只输出值，不输出类型名前缀）。
#if defined(_MSC_VER) && !defined(__clang__)
#define EFMT_DETAIL_FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define EFMT_DETAIL_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

template <typename T> constexpr std::string_view type_name() {
#if defined(EFMT_DETAIL_FUNCTION_SIGNATURE)
  const std::string_view signature = EFMT_DETAIL_FUNCTION_SIGNATURE;
#if defined(_MSC_VER) && !defined(__clang__)
  // ... e_fmt::detail::type_name<struct SensorData>(void)
  const std::string_view key = "type_name<";
  const std::size_t begin = signature.find(key);
  if (begin == std::string_view::npos) {
    return {};
  }
  std::size_t from = begin + key.size();
  const std::size_t end = signature.find(">(void)", from);
  if (end == std::string_view::npos) {
    return {};
  }
  const char *prefixes[] = {"struct ", "class ", "enum "};  // MSVC 会带这些前缀
  for (const char *prefix : prefixes) {
    const std::string_view p(prefix);
    if (signature.compare(from, p.size(), p) == 0) {
      from += p.size();
      break;
    }
  }
  return signature.substr(from, end - from);
#else
  // GCC / Clang: ... [with T = SensorData; ...] 或 ... [T = SensorData]
  const std::string_view key = "T = ";
  const std::size_t begin = signature.find(key);
  if (begin == std::string_view::npos) {
    return {};
  }
  const std::size_t from = begin + key.size();
  const std::size_t end = signature.find_first_of(";]", from);
  if (end == std::string_view::npos) {
    return {};
  }
  return signature.substr(from, end - from);
#endif
#else
  (void)sizeof(T);
  return {};
#endif
}

// ============================================================================
// 聚合体字段计数（编译期）
// ============================================================================
// 技巧：用"能转换成任意引用"的探针去初始化聚合体。
//   * T{探针 × N} 合法 ⟺ N <= 字段数（聚合初始化允许只给一部分）
//   * 再多一个就非法 ⟹ 第一个非法的 N 减一正好是字段数
// 只对"简单聚合体"成立：公开成员、无基类、无自定义构造函数。
struct any_field {
  template <typename U> constexpr operator U &() const noexcept;
};

template <typename T, typename Seq, typename = void>
struct aggregate_probe : std::false_type {};

template <typename T, std::size_t... I>
struct aggregate_probe<T, std::index_sequence<I...>,
                       std::void_t<decltype(T{(static_cast<void>(I), any_field{})...})>>
    : std::true_type {};

// 自动推导的字段数上限（-DEFMT_DERIVE_MAX_FIELDS=N 可调）
#ifndef EFMT_DERIVE_MAX_FIELDS
#define EFMT_DERIVE_MAX_FIELDS 12
#endif

// 返回字段数；0 表示"推导不出来"（非聚合体、有基类、字段数超过上限……）
template <typename T, std::size_t N = 1>
constexpr std::size_t aggregate_field_count() {
  if constexpr (N > EFMT_DERIVE_MAX_FIELDS + 1) {
    return 0;
  } else if constexpr (aggregate_probe<T, std::make_index_sequence<N>>::value) {
    return aggregate_field_count<T, N + 1>();
  } else {
    return N - 1;
  }
}

// 按编译期字段数把成员取出来（结构化绑定，N 必须是常量）
template <std::size_t N> struct aggregate_access;

template <> struct aggregate_access<1> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0] = value;
    return std::tie(f0);
  }
};

template <> struct aggregate_access<2> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1] = value;
    return std::tie(f0, f1);
  }
};

template <> struct aggregate_access<3> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2] = value;
    return std::tie(f0, f1, f2);
  }
};

template <> struct aggregate_access<4> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3] = value;
    return std::tie(f0, f1, f2, f3);
  }
};

template <> struct aggregate_access<5> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4] = value;
    return std::tie(f0, f1, f2, f3, f4);
  }
};

template <> struct aggregate_access<6> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5] = value;
    return std::tie(f0, f1, f2, f3, f4, f5);
  }
};

template <> struct aggregate_access<7> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6);
  }
};

template <> struct aggregate_access<8> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7);
  }
};

template <> struct aggregate_access<9> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8);
  }
};

template <> struct aggregate_access<10> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9);
  }
};

template <> struct aggregate_access<11> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10);
  }
};

template <> struct aggregate_access<12> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11);
  }
};

// ============================================================================
// AUTO：零声明，输出 Type(a, b, c)
// ============================================================================
// Count = 0 表示自动推导；显式给出（E_FMT_FORMATTER_AUTO_N）时跳过推导，
// 这是给"含 C 型数组成员"这类推导不可靠的类型准备的逃生口。
template <typename T, std::size_t Count = 0> struct aggregate_formatter {
  static void format(format_context &ctx, const format_specs &specs,
                     const T &value) {
    (void)specs;
    constexpr std::size_t count =
        (Count > 0) ? Count : aggregate_field_count<T>();
    static_assert(Count > 0 || std::is_aggregate<T>::value,
                  "E_FMT_FORMATTER_AUTO 只能用于聚合体（公开成员、无基类、无自定义"
                  "构造函数）；否则请改用 E_FMT_FORMATTER_FIELDS(Type, 字段...)");
    static_assert(count > 0,
                  "自动推导字段失败：字段数超过 EFMT_DERIVE_MAX_FIELDS，或该类型不是"
                  "简单聚合体（含数组成员时推导会偏大）。请改用 "
                  "E_FMT_FORMATTER_FIELDS(Type, 字段...) 或 "
                  "E_FMT_FORMATTER_AUTO_N(Type, 字段个数)");
    if constexpr (count > 0) {
      const std::string_view name = type_name<T>();
      if (!name.empty()) {
        ctx.write_str(name);
      }
      ctx.write_char('(');
      print_values(ctx, aggregate_access<count>::tie(value),
                   std::make_index_sequence<count>{});
      ctx.write_char(')');
    }
  }

private:
  template <typename Tuple, std::size_t... I>
  static void print_values(format_context &ctx, const Tuple &values,
                           std::index_sequence<I...>) {
    bool first = true;
    (([&]() {
      if (!first) {
        ctx.write_str(", ");
      }
      first = false;
      format_specs default_specs;
      formatter<std::decay_t<decltype(std::get<I>(values))>>::format(
          ctx, default_specs, std::get<I>(values));
    }()),
     ...);
  }
};

// ============================================================================
// 宏：字段列表 / 自动推导 / 枚举
// ============================================================================
// 三个宏都展开成一个自由函数 efmt_derive_format(...)（不改动 e_fmt 命名空间）：
//   * 用户类型名在用户作用域里解析，不会被库内部同名符号遮蔽
//   * 可以写在类型所在的命名空间里（ADL 能找到），也可以写在全局
#define EFMT_DETAIL_CONCAT(a, b) EFMT_DETAIL_CONCAT_(a, b)
#define EFMT_DETAIL_CONCAT_(a, b) a##b

#define EFMT_DETAIL_ARG_COUNT(...) \
  EFMT_DETAIL_ARG_COUNT_(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define EFMT_DETAIL_ARG_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, \
                               _12, N, ...) \
  N

// 只列字段名：成员类型由 &Type::字段 推导，显示名由 #字段 生成
#define E_FMT_FORMATTER_FIELDS(Type, ...) \
  EFMT_DETAIL_CONCAT(EFMT_DETAIL_FIELDS_, \
                     EFMT_DETAIL_ARG_COUNT(__VA_ARGS__))(Type, __VA_ARGS__)

// 枚举取值 → 名字（未列出的取值打印底层整数）
#define E_FMT_FORMATTER_ENUM(Type, ...) \
  EFMT_DETAIL_CONCAT(EFMT_DETAIL_ENUM_, \
                     EFMT_DETAIL_ARG_COUNT(__VA_ARGS__))(Type, __VA_ARGS__)

// 零声明：字段全自动（简单聚合体；含 C 型数组成员时推导会偏大，请用下面的 _N 版本）
#define E_FMT_FORMATTER_AUTO(Type) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  ::e_fmt::detail::aggregate_formatter<Type>::format(ctx, specs, value); \
  }

// 手动给字段个数：给"自动推导不可靠"的聚合体用（例如成员里有 char name[8]）
#define E_FMT_FORMATTER_AUTO_N(Type, Count) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  ::e_fmt::detail::aggregate_formatter<Type, Count>::format(ctx, specs, value); \
  }

#define EFMT_DETAIL_FIELDS_1(Type, m1) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<1>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_2(Type, m1, m2) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<2>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_3(Type, m1, m2, m3) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<3>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_4(Type, m1, m2, m3, m4) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<4>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_5(Type, m1, m2, m3, m4, m5) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<5>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_6(Type, m1, m2, m3, m4, m5, m6) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<6>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_7(Type, m1, m2, m3, m4, m5, m6, m7) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<7>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_8(Type, m1, m2, m3, m4, m5, m6, m7, m8) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>, \
        ::e_fmt::detail::field_descriptor<&Type::m8>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7), \
                       ::e_fmt::detail::field_descriptor<&Type::m8>(#m8)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<8>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_9(Type, m1, m2, m3, m4, m5, m6, m7, m8, m9) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>, \
        ::e_fmt::detail::field_descriptor<&Type::m8>, \
        ::e_fmt::detail::field_descriptor<&Type::m9>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7), \
                       ::e_fmt::detail::field_descriptor<&Type::m8>(#m8), \
                       ::e_fmt::detail::field_descriptor<&Type::m9>(#m9)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<9>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_10(Type, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>, \
        ::e_fmt::detail::field_descriptor<&Type::m8>, \
        ::e_fmt::detail::field_descriptor<&Type::m9>, \
        ::e_fmt::detail::field_descriptor<&Type::m10>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7), \
                       ::e_fmt::detail::field_descriptor<&Type::m8>(#m8), \
                       ::e_fmt::detail::field_descriptor<&Type::m9>(#m9), \
                       ::e_fmt::detail::field_descriptor<&Type::m10>(#m10)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<10>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_11(Type, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>, \
        ::e_fmt::detail::field_descriptor<&Type::m8>, \
        ::e_fmt::detail::field_descriptor<&Type::m9>, \
        ::e_fmt::detail::field_descriptor<&Type::m10>, \
        ::e_fmt::detail::field_descriptor<&Type::m11>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7), \
                       ::e_fmt::detail::field_descriptor<&Type::m8>(#m8), \
                       ::e_fmt::detail::field_descriptor<&Type::m9>(#m9), \
                       ::e_fmt::detail::field_descriptor<&Type::m10>(#m10), \
                       ::e_fmt::detail::field_descriptor<&Type::m11>(#m11)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<11>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_FIELDS_12(Type, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  (void)specs; \
  using list_type = ::e_fmt::detail::member_descriptor_list< \
        ::e_fmt::detail::field_descriptor<&Type::m1>, \
        ::e_fmt::detail::field_descriptor<&Type::m2>, \
        ::e_fmt::detail::field_descriptor<&Type::m3>, \
        ::e_fmt::detail::field_descriptor<&Type::m4>, \
        ::e_fmt::detail::field_descriptor<&Type::m5>, \
        ::e_fmt::detail::field_descriptor<&Type::m6>, \
        ::e_fmt::detail::field_descriptor<&Type::m7>, \
        ::e_fmt::detail::field_descriptor<&Type::m8>, \
        ::e_fmt::detail::field_descriptor<&Type::m9>, \
        ::e_fmt::detail::field_descriptor<&Type::m10>, \
        ::e_fmt::detail::field_descriptor<&Type::m11>, \
        ::e_fmt::detail::field_descriptor<&Type::m12>>; \
  const list_type list(::e_fmt::detail::field_descriptor<&Type::m1>(#m1), \
                       ::e_fmt::detail::field_descriptor<&Type::m2>(#m2), \
                       ::e_fmt::detail::field_descriptor<&Type::m3>(#m3), \
                       ::e_fmt::detail::field_descriptor<&Type::m4>(#m4), \
                       ::e_fmt::detail::field_descriptor<&Type::m5>(#m5), \
                       ::e_fmt::detail::field_descriptor<&Type::m6>(#m6), \
                       ::e_fmt::detail::field_descriptor<&Type::m7>(#m7), \
                       ::e_fmt::detail::field_descriptor<&Type::m8>(#m8), \
                       ::e_fmt::detail::field_descriptor<&Type::m9>(#m9), \
                       ::e_fmt::detail::field_descriptor<&Type::m10>(#m10), \
                       ::e_fmt::detail::field_descriptor<&Type::m11>(#m11), \
                       ::e_fmt::detail::field_descriptor<&Type::m12>(#m12)); \
  ctx.write_char('{'); \
  list.format_members(ctx, value, std::make_index_sequence<12>{}); \
  ctx.write_char('}'); \
  }

#define EFMT_DETAIL_ENUM_1(Type, v1) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_2(Type, v1, v2) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_3(Type, v1, v2, v3) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_4(Type, v1, v2, v3, v4) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_5(Type, v1, v2, v3, v4, v5) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_6(Type, v1, v2, v3, v4, v5, v6) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_7(Type, v1, v2, v3, v4, v5, v6, v7) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_8(Type, v1, v2, v3, v4, v5, v6, v7, v8) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  case Type::v8: \
    ctx.write_aligned(#v8, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_9(Type, v1, v2, v3, v4, v5, v6, v7, v8, v9) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  case Type::v8: \
    ctx.write_aligned(#v8, specs); \
    return; \
  case Type::v9: \
    ctx.write_aligned(#v9, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_10(Type, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  case Type::v8: \
    ctx.write_aligned(#v8, specs); \
    return; \
  case Type::v9: \
    ctx.write_aligned(#v9, specs); \
    return; \
  case Type::v10: \
    ctx.write_aligned(#v10, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_11(Type, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  case Type::v8: \
    ctx.write_aligned(#v8, specs); \
    return; \
  case Type::v9: \
    ctx.write_aligned(#v9, specs); \
    return; \
  case Type::v10: \
    ctx.write_aligned(#v10, specs); \
    return; \
  case Type::v11: \
    ctx.write_aligned(#v11, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

#define EFMT_DETAIL_ENUM_12(Type, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12) \
  inline void efmt_derive_format(const Type &value, \
                                 ::e_fmt::detail::format_context &ctx, \
                                 const ::e_fmt::detail::format_specs &specs) { \
  switch (value) { \
  case Type::v1: \
    ctx.write_aligned(#v1, specs); \
    return; \
  case Type::v2: \
    ctx.write_aligned(#v2, specs); \
    return; \
  case Type::v3: \
    ctx.write_aligned(#v3, specs); \
    return; \
  case Type::v4: \
    ctx.write_aligned(#v4, specs); \
    return; \
  case Type::v5: \
    ctx.write_aligned(#v5, specs); \
    return; \
  case Type::v6: \
    ctx.write_aligned(#v6, specs); \
    return; \
  case Type::v7: \
    ctx.write_aligned(#v7, specs); \
    return; \
  case Type::v8: \
    ctx.write_aligned(#v8, specs); \
    return; \
  case Type::v9: \
    ctx.write_aligned(#v9, specs); \
    return; \
  case Type::v10: \
    ctx.write_aligned(#v10, specs); \
    return; \
  case Type::v11: \
    ctx.write_aligned(#v11, specs); \
    return; \
  case Type::v12: \
    ctx.write_aligned(#v12, specs); \
    return; \
  default: \
    break; \
  } \
  /* 没列到的取值：打印底层整数，方便发现漏项 */ \
  ::e_fmt::detail::integral_formatter::format_signed( \
      ctx, specs, \
      static_cast<::e_fmt::int64_t>( \
          static_cast<typename std::underlying_type<Type>::type>(value))); \
  }

} // namespace e_fmt::detail

#endif // FORMAT_DERIVE_HPP