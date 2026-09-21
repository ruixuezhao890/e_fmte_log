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

// 字段数上限（-DEFMT_DERIVE_MAX_FIELDS=N 可调）：E_FMT_DERIVE 解析缓冲、
// 零声明自动推导的探测上限、以及 aggregate_access 的打印表都受它约束
#ifndef EFMT_DERIVE_MAX_FIELDS
#define EFMT_DERIVE_MAX_FIELDS 16
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

template <> struct aggregate_access<13> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12);
  }
};

template <> struct aggregate_access<14> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13);
  }
};

template <> struct aggregate_access<15> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14);
  }
};

template <> struct aggregate_access<16> {
  template <typename T> static auto tie(const T &value) {
    const auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15);
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

// ============================================================================
// E_FMT_DERIVE：声明即推导（对标 Rust 的 #[derive(Debug)]）
// ============================================================================
// 用法（一个宏同时管结构体和枚举，字段名/取值名一个字都不用写）：
//
//   E_FMT_DERIVE(struct imu { float ax, ay, az; });
//   println_info("{}", imu{1.5f, 2.5f, 3.5f});      // { ax = 1.5, ay = 2.5, az = 3.5 }
//
//   E_FMT_DERIVE(enum class state { idle, busy = 5, fault });
//   println_info("{}", state::busy);                 // busy
//
// 机制（全部纯 C++17：无脚本、无第三方库、无编译器扩展）：
//   1) 宏尾追加 extern <你的声明> 唯一名;  → 用 decltype(唯一名) 抓到类型（不占存储、不重复类型名）
//   2) 用 #__VA_ARGS__ 在编译期解析出字段名/枚举名（常量表达式，运行时零开销）
//   3) 在同一作用域生成 ADL 自由函数（宏声明了类型 ⇒ ADL 必然找得到），把名字挂上去
//   4) 取值用结构化绑定后【直接】交给格式化器，不经过 std::tie —— GCC 下 std::tie 绑位域会拿到
//      未初始化的临时量（实测打印 0），直接按引用传既正确又无拷贝
//   5) 解析出的字段数 == 结构化绑定数量：对不上就编译报错，绝不静默输出错名字

// ---------------------------------------------------------------------------
// 开关
// ---------------------------------------------------------------------------
// 输出里是否带类型名：{ ax = 1.5 }（默认）还是 imu { ax = 1.5 }
// 默认关：省 Flash —— 类型名要靠编译器签名宏解析，会多一份字符串与解析代码
#ifndef EFMT_DERIVE_SHOW_TYPE
#define EFMT_DERIVE_SHOW_TYPE 0
#endif

// 单类型最多支持多少字段/枚举取值：与上面的 EFMT_DERIVE_MAX_FIELDS 是同一个宏
static_assert(EFMT_DERIVE_MAX_FIELDS >= 1 && EFMT_DERIVE_MAX_FIELDS <= 32,
              "EFMT_DERIVE_MAX_FIELDS 必须在 1..32 之间");

// 数组成员最多打印几个元素，超出用 ... 省略
#ifndef EFMT_DERIVE_MAX_ARRAY_ITEMS
#define EFMT_DERIVE_MAX_ARRAY_ITEMS 8
#endif

// 严格模式：成员没有格式化器时直接编译报错（Rust 行为）；设为 0 退回旧的 obj@地址 行为
#ifndef EFMT_DERIVE_STRICT
#define EFMT_DERIVE_STRICT 1
#endif

// ---------------------------------------------------------------------------
// 解析结果
// ---------------------------------------------------------------------------
struct derived_names {
  std::string_view items[EFMT_DERIVE_MAX_FIELDS];
  long long values[EFMT_DERIVE_MAX_FIELDS];   // 枚举取值（结构体不用）
  std::size_t count = 0;
  bool is_enum = false;
  bool valid = false;                // 是否解析成功
  bool unknown_initializer = false;  // 枚举里有认不出的初始值（非字面量）
};

namespace derive_detail {

constexpr bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
constexpr bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
constexpr bool is_ident(char c) { return is_alpha(c) || (c >= '0' && c <= '9'); }

constexpr std::string_view trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

constexpr bool has_word(std::string_view s, std::string_view w) {
  for (std::size_t i = 0; i + w.size() <= s.size(); ++i) {
    bool hit = true;
    for (std::size_t k = 0; k < w.size(); ++k) {
      if (s[i + k] != w[k]) { hit = false; break; }
    }
    if (!hit) continue;
    if ((i == 0 || !is_ident(s[i - 1])) &&
        (i + w.size() >= s.size() || !is_ident(s[i + w.size()]))) {
      return true;
    }
  }
  return false;
}

// 声明符里的字段名：最后一个标识符，遇到 [ = : 停
constexpr std::string_view declarator_name(std::string_view frag) {
  std::string_view last{};
  std::size_t i = 0;
  while (i < frag.size()) {
    const char c = frag[i];
    if (c == '[' || c == '=' || c == ':') break;
    if (is_alpha(c)) {
      const std::size_t b = i;
      while (i < frag.size() && is_ident(frag[i])) ++i;
      last = frag.substr(b, i - b);
      continue;
    }
    ++i;
  }
  return last;
}

// 函数指针成员：void (*cb)(int) → cb
constexpr std::string_view function_pointer_name(std::string_view s) {
  const std::size_t star = s.find("(*");
  if (star == std::string_view::npos) return {};
  std::size_t i = star + 2;
  while (i < s.size() && is_space(s[i])) ++i;
  const std::size_t b = i;
  while (i < s.size() && is_ident(s[i])) ++i;
  if (i == b) return {};
  return s.substr(b, i - b);
}

constexpr long long parse_integer_literal(std::string_view s, bool &ok) {
  ok = false;
  std::size_t i = 0;
  bool negative = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) { negative = (s[i] == '-'); ++i; }
  int base = 10;
  if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { base = 16; i += 2; }
  long long value = 0;
  bool any = false;
  for (; i < s.size(); ++i) {
    const char c = s[i];
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else break;
    value = value * base + digit;
    any = true;
  }
  while (i < s.size() && is_space(s[i])) ++i;
  if (!any || i != s.size()) return 0;   // 后面还有东西 → 不是字面量
  ok = true;
  return negative ? -value : value;
}

// 结构体体：按 ; 切语句（深度 0），跳过静态成员/别名/访问修饰符/函数，再按 , 切声明符
constexpr derived_names parse_struct_body(std::string_view body) {
  derived_names out{};
  int depth = 0;
  bool ok = true;
  std::size_t stmt_begin = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ';';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ';' || depth != 0) continue;

    const std::string_view stmt = trim(body.substr(stmt_begin, i - stmt_begin));
    stmt_begin = i + 1;
    if (stmt.empty()) continue;
    if (has_word(stmt, "static") || has_word(stmt, "using") || has_word(stmt, "typedef") ||
        has_word(stmt, "friend") || has_word(stmt, "template") ||
        has_word(stmt, "public") || has_word(stmt, "private") ||
        has_word(stmt, "protected")) {
      continue;   // 静态成员 / 类型别名 / 访问修饰符：不是数据字段
    }
    if (stmt.find('(') != std::string_view::npos) {
      const std::string_view fn = function_pointer_name(stmt);   // 函数指针成员要算字段
      if (!fn.empty()) {
        if (out.count < EFMT_DERIVE_MAX_FIELDS) out.items[out.count++] = fn;
        else ok = false;
      }
      continue;   // 成员函数跳过
    }

    int inner = 0;
    std::size_t part_begin = 0;
    for (std::size_t j = 0; j <= stmt.size(); ++j) {
      const char c2 = (j < stmt.size()) ? stmt[j] : ',';
      if (c2 == '(' || c2 == '[' || c2 == '{' || c2 == '<') { ++inner; continue; }
      if (c2 == ')' || c2 == ']' || c2 == '}' || c2 == '>') { --inner; continue; }
      if (c2 != ',' || inner != 0) continue;
      const std::string_view name = declarator_name(trim(stmt.substr(part_begin, j - part_begin)));
      if (!name.empty()) {
        if (out.count < EFMT_DERIVE_MAX_FIELDS) out.items[out.count++] = name;
        else ok = false;
      }
      part_begin = j + 1;
    }
  }
  out.valid = ok && out.count > 0;
  return out;
}

// 枚举体：按 , 切项，首标识符为名字，可选的 = 整数字面量
constexpr derived_names parse_enum_body(std::string_view body) {
  derived_names out{};
  out.is_enum = true;
  long long next_value = 0;
  int depth = 0;
  bool ok = true;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ',';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ',' || depth != 0) continue;

    const std::string_view item = trim(body.substr(begin, i - begin));
    begin = i + 1;
    if (item.empty()) continue;

    const std::size_t eq = item.find('=');
    const std::string_view name_part = trim(eq == std::string_view::npos ? item : item.substr(0, eq));
    std::size_t len = 0;
    while (len < name_part.size() && is_ident(name_part[len])) ++len;
    if (len == 0) continue;   // 不是标识符开头 → 跳过

    long long value = next_value;
    if (eq != std::string_view::npos) {
      bool literal_ok = false;
      const long long parsed = parse_integer_literal(trim(item.substr(eq + 1)), literal_ok);
      if (!literal_ok) {
        out.unknown_initializer = true;
        continue;   // 交给 static_assert 报错，不猜
      }
      value = parsed;
    }
    if (out.count < EFMT_DERIVE_MAX_FIELDS) {
      out.items[out.count] = name_part.substr(0, len);
      out.values[out.count] = value;
      ++out.count;
    } else {
      ok = false;
    }
    next_value = value + 1;
  }
  out.valid = ok && out.count > 0 && !out.unknown_initializer;
  return out;
}

constexpr derived_names parse_derived_declaration(std::string_view text) {
  const std::size_t open = text.find('{');
  if (open == std::string_view::npos) return derived_names{};
  int depth = 0;
  std::size_t close = std::string_view::npos;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    else if (text[i] == '}') { if (--depth == 0) { close = i; break; } }
  }
  if (close == std::string_view::npos) return derived_names{};

  const std::string_view head = trim(text.substr(0, open));
  const std::string_view body = text.substr(open + 1, close - open - 1);
  return (head.size() >= 5 && head.substr(0, 5) == "enum ")
             ? parse_enum_body(body)
             : parse_struct_body(body);
}

}  // namespace derive_detail

// ---------------------------------------------------------------------------
// 打印
// ---------------------------------------------------------------------------
template <typename T>
void derive_write(format_context &ctx, const format_specs &specs, const T &value);

template <typename T>
void derive_write_value(format_context &ctx, const T &value) {
  format_specs specs;
  derive_write(ctx, specs, value);
}

// 名字 + " = " + 值；names == nullptr 表示位置式（内层没声明格式化器的聚合体）
template <typename T>
void write_field(format_context &ctx, const derived_names *names, std::size_t index,
                 const T &value) {
  if (index != 0) {
    ctx.write_str(", ");
  }
  if (names != nullptr) {
    ctx.write_str(names->items[index]);
    ctx.write_str(" = ");
  }
  derive_write_value(ctx, value);
}

inline void open_bracket(format_context &ctx, const derived_names *names) {
  ctx.write_str(names != nullptr ? "{ " : "(");
}

inline void close_bracket(format_context &ctx, const derived_names *names) {
  ctx.write_str(names != nullptr ? " }" : ")");
}

// 按字段数特化的打印器：每个绑定直接交给格式化器（不经过 std::tie）
template <std::size_t N> struct derived_printer;

template <> struct derived_printer<1> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<2> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<3> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<4> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<5> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<6> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<7> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<8> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<9> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<10> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<11> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<12> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    write_field(ctx, names, 11, m11);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<13> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    write_field(ctx, names, 11, m11);
    write_field(ctx, names, 12, m12);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<14> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    write_field(ctx, names, 11, m11);
    write_field(ctx, names, 12, m12);
    write_field(ctx, names, 13, m13);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<15> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    write_field(ctx, names, 11, m11);
    write_field(ctx, names, 12, m12);
    write_field(ctx, names, 13, m13);
    write_field(ctx, names, 14, m14);
    close_bracket(ctx, names);
  }
};

template <> struct derived_printer<16> {
  template <typename T>
  static void run(const T &value, format_context &ctx, const derived_names *names) {
    const auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15] = value;
    open_bracket(ctx, names);
    write_field(ctx, names, 0, m0);
    write_field(ctx, names, 1, m1);
    write_field(ctx, names, 2, m2);
    write_field(ctx, names, 3, m3);
    write_field(ctx, names, 4, m4);
    write_field(ctx, names, 5, m5);
    write_field(ctx, names, 6, m6);
    write_field(ctx, names, 7, m7);
    write_field(ctx, names, 8, m8);
    write_field(ctx, names, 9, m9);
    write_field(ctx, names, 10, m10);
    write_field(ctx, names, 11, m11);
    write_field(ctx, names, 12, m12);
    write_field(ctx, names, 13, m13);
    write_field(ctx, names, 14, m14);
    write_field(ctx, names, 15, m15);
    close_bracket(ctx, names);
  }
};

// 枚举：取值 → 名字；未列出的取值 → 底层整数
template <typename T>
void derive_write_enum(const T &value, format_context &ctx, const format_specs &specs,
                       const derived_names &names) {
  using underlying = std::underlying_type_t<T>;
  const bool as_unsigned = !std::is_signed<underlying>::value;
  const long long as_signed = static_cast<long long>(static_cast<underlying>(value));
  const unsigned long long as_plain =
      static_cast<unsigned long long>(static_cast<underlying>(value));
  const long long key = as_unsigned ? static_cast<long long>(as_plain) : as_signed;
  for (std::size_t i = 0; i < names.count; ++i) {
    if (names.values[i] == key) {
      ctx.write_aligned(names.items[i], specs);
      return;
    }
  }
  integral_formatter::format_signed(ctx, specs, as_signed);
}

template <typename T, std::size_t N>
void format_derived(const T &value, format_context &ctx, const format_specs &specs,
                    const derived_names &names) {
  if constexpr (std::is_enum<T>::value) {
    derive_write_enum(value, ctx, specs, names);
  } else {
    if constexpr (EFMT_DERIVE_SHOW_TYPE != 0) {
      const std::string_view type = type_name<T>();
      if (!type.empty()) {
        ctx.write_str(type);
        ctx.write_char(' ');
      }
    }
    derived_printer<N>::run(value, ctx, &names);
  }
}

// 位置式打印：内层什么都不用写的聚合体
template <typename T>
void derive_write_positional(const T &value, format_context &ctx) {
  constexpr std::size_t count = aggregate_field_count<T>();
  static_assert(count > 0,
                "这个聚合体推导不出字段数（可能含数组成员、有基类或不是聚合体）。"
                "请给它加 E_FMT_DERIVE(...)，或改用 E_FMT_FIELDS(...)");
  if constexpr (count > 0) {
    if constexpr (EFMT_DERIVE_SHOW_TYPE != 0) {
      const std::string_view type = type_name<T>();
      if (!type.empty()) {
        ctx.write_str(type);
        ctx.write_char(' ');
      }
    }
    derived_printer<count>::run(value, ctx, nullptr);
  }
}

// 递归打印一个成员/元素
template <typename T>
void derive_write(format_context &ctx, const format_specs &specs, const T &value) {
  // 用 remove_cvref 而不是 decay：decay 会把数组变成指针，数组就走不到数组分支
  using D = std::remove_cv_t<std::remove_reference_t<T>>;

  if constexpr (std::is_array_v<D> && std::is_same_v<std::remove_extent_t<D>, char>) {
    std::size_t len = 0;
    while (len < std::extent_v<D> && value[len] != '\0') ++len;
    ctx.write_aligned(std::string_view(value, len), specs);
  } else if constexpr (std::is_array_v<D>) {
    ctx.write_char('[');
    constexpr std::size_t cap = EFMT_DERIVE_MAX_ARRAY_ITEMS;
    const std::size_t total = std::extent_v<D>;
    const std::size_t shown = (total < cap) ? total : cap;
    for (std::size_t i = 0; i < shown; ++i) {
      if (i != 0) ctx.write_str(", ");
      derive_write_value(ctx, value[i]);
    }
    if (total > shown) ctx.write_str(", ...");
    ctx.write_char(']');
  } else if constexpr (std::is_pointer_v<D>) {
    // 裸指针 / 函数指针：按十六进制地址打印（空指针 → (nil)），与库内指针格式一致
    write_hex_address(ctx, specs, reinterpret_cast<const void *>(value), "(nil)");
  } else if constexpr (has_derived_formatter_v<D>) {
    efmt_derive_format(value, ctx, specs);   // E_FMT_DERIVE 推导过的类型
  } else if constexpr (has_field_names_v<D>) {
    format_via_field_names(ctx, specs, value);   // 类型内一行 E_FMT_FIELDS(...)
  } else if constexpr (std::is_aggregate_v<D> && (aggregate_field_count<D>() > 0)) {
    derive_write_positional(value, ctx);     // 内层没声明 → 位置式递归
  } else {
#if EFMT_DERIVE_STRICT
    static_assert(is_builtin_type<D>::value || has_formatter_specialization_v<D>,
                  "成员类型没有格式化器：给它加 E_FMT_DERIVE(...)，或写一个 formatter<> "
                  "（Rust 里相当于这个类型没实现 Debug）。"
                  "也可以定义 EFMT_DERIVE_STRICT=0 退回打印地址。");
#endif
    formatter<D>::format(ctx, specs, value);
  }
}

// ---------------------------------------------------------------------------
// 类型内一行：E_FMT_FIELDS(字段, ...)
// ---------------------------------------------------------------------------
// 解析 "retry, verbose" 这样的名字清单
constexpr derived_names parse_name_list(std::string_view text) {
  derived_names out{};
  int depth = 0;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    const char c = (i < text.size()) ? text[i] : ',';
    if (c == '(' || c == '[' || c == '{' || c == '<') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}' || c == '>') { --depth; continue; }
    if (c != ',' || depth != 0) continue;
    const std::string_view item = derive_detail::trim(text.substr(begin, i - begin));
    begin = i + 1;
    if (item.empty()) continue;
    if (out.count < EFMT_DERIVE_MAX_FIELDS) out.items[out.count++] = item;
  }
  out.valid = out.count > 0;
  return out;
}

template <typename T>
void format_via_field_names(format_context &ctx, const format_specs &specs,
                            const T &value) {
  (void)specs;
  static constexpr derived_names names = T::efmt_field_names();
  static_assert(names.valid,
                "E_FMT_FIELDS(...) 里没写字段名，或字段数超过 EFMT_DERIVE_MAX_FIELDS");
  if constexpr (EFMT_DERIVE_SHOW_TYPE != 0) {
    const std::string_view type = type_name<T>();
    if (!type.empty()) {
      ctx.write_str(type);
      ctx.write_char(' ');
    }
  }
  derived_printer<names.count>::run(value, ctx, &names);
}

// ---------------------------------------------------------------------------
// 宏
// ---------------------------------------------------------------------------
#if defined(__COUNTER__)
#define EFMT_DERIVE_DETAIL_ID __COUNTER__
#else
#define EFMT_DERIVE_DETAIL_ID __LINE__
#endif

#define EFMT_DERIVE_DETAIL_CAT_(a, b) a##b
#define EFMT_DERIVE_DETAIL_CAT(a, b) EFMT_DERIVE_DETAIL_CAT_(a, b)

#define EFMT_DERIVE_DETAIL_UNIQUE(id, ...)                                          \
  extern __VA_ARGS__ EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id);                   \
  inline void efmt_derive_format(                                                     \
      const decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id)) &value,            \
      ::e_fmt::detail::format_context &ctx,                                           \
      const ::e_fmt::detail::format_specs &specs) {                                   \
    static constexpr ::e_fmt::detail::derived_names                                  \
        EFMT_DERIVE_DETAIL_CAT(efmt_derive_names_, id) =                              \
            ::e_fmt::detail::derive_detail::parse_derived_declaration(#__VA_ARGS__);  \
    using efmt_derive_type = decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id));  \
    static_assert(                                                                    \
        !EFMT_DERIVE_DETAIL_CAT(efmt_derive_names_, id).unknown_initializer,          \
        "枚举里有非字面量的初始值（例如 A = 1 << 3）：E_FMT_DERIVE 的自动模式只认"     \
        "整数字面量。请改用 E_FMT_FIELDS(取值名, ...) 写在枚举内部显式列出");          \
    static_assert(                                                                    \
        EFMT_DERIVE_DETAIL_CAT(efmt_derive_names_, id).valid,                         \
        "E_FMT_DERIVE 解析不出这段声明里的字段/取值：请检查写法，或改用 "              \
        "E_FMT_FIELDS(字段, ...) 写在类型内部显式列出");                              \
    constexpr std::size_t efmt_derive_count =                                         \
        EFMT_DERIVE_DETAIL_CAT(efmt_derive_names_, id).count;                         \
    ::e_fmt::detail::format_derived<efmt_derive_type, efmt_derive_count>(             \
        value, ctx, specs, EFMT_DERIVE_DETAIL_CAT(efmt_derive_names_, id));           \
  }

// 用法：E_FMT_DERIVE(struct imu { float ax, ay, az; });
#define E_FMT_DERIVE(...) EFMT_DERIVE_DETAIL_UNIQUE(EFMT_DERIVE_DETAIL_ID, __VA_ARGS__)

// 类型内一行：只能列字段名，类型/字符串/成员指针全自动。
// 用在 E_FMT_DERIVE 覆盖不到的场合：声明里有 #if、模板结构体、字段数超上限、给已有类型补一行。
//   struct cfg { int retry; bool verbose; E_FMT_FIELDS(retry, verbose); };
#define E_FMT_FIELDS(...)                                                          static constexpr ::e_fmt::detail::derived_names efmt_field_names() {                return ::e_fmt::detail::parse_name_list(#__VA_ARGS__);                          }


} // namespace e_fmt::detail

#endif // FORMAT_DERIVE_HPP