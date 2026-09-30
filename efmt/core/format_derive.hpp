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

namespace e_fmt {

// E_FMT_DERIVE(decl, ...) 里的能力标签写在这层：Debug = 打印（efmt 本体自带，也是默认
// 能力）。Serialize 之类由上层（eserde）自己定义，efmt 不认识它们 —— 宏只把标签原样登记。
struct Debug {};

}  // namespace e_fmt

namespace e_fmt::detail {

// 能力标签清单：E_FMT_DERIVE(decl, Debug, Serialize) → caps_list<Debug, Serialize>。
// 空清单 = "只登记了类型、没写能力"。纯类型登记，不产生任何代码或数据。
template <typename... Caps> struct caps_list {};

// ADL 挂钩的标签类型：宏写不出类型名，但能把类型塞进模板参数，
// 上层靠 efmt_derive_xxx(type_tag<T>{}) 的 ADL 找到宏生成的函数。
template <typename T> struct type_tag {};

// ============================================================================
// 为什么需要它
// ============================================================================
// 旧写法要把"成员类型 + 成员名 + 显示名"抄三遍：
//     （老写法 E_FMT_FORMATTER_2 已删除；需要"只列字段名"时用 E_FMT_FORMATTER_FIELDS）
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
// N 的默认值已在 format_traits.hpp 的声明处给出（arm-none-eabi-g++ 10.x 只认第一条声明上的默认值）
template <typename T, std::size_t N>
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
  template <typename T> static auto tie(T &value) {
    auto &[f0] = value;
    return std::tie(f0);
  }
};

template <> struct aggregate_access<2> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1] = value;
    return std::tie(f0, f1);
  }
};

template <> struct aggregate_access<3> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2] = value;
    return std::tie(f0, f1, f2);
  }
};

template <> struct aggregate_access<4> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3] = value;
    return std::tie(f0, f1, f2, f3);
  }
};

template <> struct aggregate_access<5> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4] = value;
    return std::tie(f0, f1, f2, f3, f4);
  }
};

template <> struct aggregate_access<6> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5] = value;
    return std::tie(f0, f1, f2, f3, f4, f5);
  }
};

template <> struct aggregate_access<7> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6);
  }
};

template <> struct aggregate_access<8> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7);
  }
};

template <> struct aggregate_access<9> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8);
  }
};

template <> struct aggregate_access<10> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9);
  }
};

template <> struct aggregate_access<11> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10);
  }
};

template <> struct aggregate_access<13> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12);
  }
};

template <> struct aggregate_access<14> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13);
  }
};

template <> struct aggregate_access<15> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14);
  }
};

template <> struct aggregate_access<16> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15] = value;
    return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15);
  }
};

template <> struct aggregate_access<12> {
  template <typename T> static auto tie(T &value) {
    auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = value;
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
  EFMT_DETAIL_ARG_COUNT_(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define EFMT_DETAIL_ARG_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, \
                               _13, _14, _15, _16, N, ...) \
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
  list.format_full(ctx, value, std::make_index_sequence<1>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<2>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<3>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<4>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<5>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<6>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<7>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<8>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<9>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<10>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<11>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
  list.format_full(ctx, value, std::make_index_sequence<12>{}, \
                   ::e_fmt::detail::make_descriptor_style(specs)); \
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
// 用法（字段名/取值名一个字都不用写；结构体与枚举各一个入口）：
//
//   E_FMT_DERIVE(struct imu { float ax; float ay; float az; }, Debug, Serialize);
//   println_info("{}", imu{1.5f, 2.5f, 3.5f});      // { ax = 1.5, ay = 2.5, az = 3.5 }
//   // 第一个参数是声明本身：里面不能有顶层逗号（预处理器按顶层逗号切参数），字段一行一个；
//   // 其后的 Debug / Serialize 是能力标签，efmt 原样登记（Debug 由 efmt 提供），
//   // 上层 eserde 之类靠 efmt_derive_caps / efmt_derive_decl 这两个 ADL 钩子查。
//
//   E_FMT_DERIVE_ENUM(enum class state { idle, busy = 5, fault });
//   println_info("{}", state::busy);                 // busy
//   // 枚举体的逗号是顶层逗号（圆括号才保护逗号，花括号不算），所以走"整段声明"的专用入口。
//
// 字段标签（可选，写在字段声明的前面或后面，编译期解析）：
//   [[efmt::arg(short, long)]] etl::string<12> name;   → 字段 name 带标签 short / long
//   int level [[efmt::arg(help = "0..9")]];            → 带值标签
//
// 机制（全部纯 C++17：无脚本、无第三方库、无编译器扩展）：
//   1) 宏尾追加 extern <你的声明> 唯一名;  → 用 decltype(唯一名) 抓到类型（不占存储、不重复类型名）
//   2) 用 #__VA_ARGS__ 在编译期解析出字段名/枚举名（常量表达式，运行时零开销）
//   3) 在同一作用域生成 ADL 自由函数（宏声明了类型 ⇒ ADL 必然找得到），把名字挂上去
//   4) 取值用结构化绑定后【直接】交给格式化器，不经过 std::tie —— GCC 下 std::tie 绑位域会拿到
//      未初始化的临时量（实测打印 0），直接按引用传既正确又无拷贝
//   5) 解析出的字段数 == 结构化绑定数量：对不上就编译报错，绝不静默输出错名字

// ---------------------------------------------------------------------------
// 开关统一在 format_base.hpp：
//   EFMT_DERIVE_SHOW_TYPE（默认 0）/ EFMT_DERIVE_MAX_FIELDS（16）/
//   EFMT_DERIVE_MAX_ARRAY_ITEMS（8）/ EFMT_DERIVE_STRICT（1）
// ---------------------------------------------------------------------------
static_assert(EFMT_DERIVE_MAX_FIELDS >= 1 && EFMT_DERIVE_MAX_FIELDS <= 32,
              "EFMT_DERIVE_MAX_FIELDS 必须在 1..32 之间");

// ---------------------------------------------------------------------------
// 解析结果
// ---------------------------------------------------------------------------
// 名字表按实际条目数缩放（[N] 数组正好 N 条）：E_FMT_DERIVE / E_FMT_FIELDS 的
// 静态 rodata 从固定的 ~400B（16 条 × string_view + value）降到 ~(16+8)·N+24B，
// 典型 2~8 字段类型省 70%~80%。N 由调用方编译期确定：FIELDS 用宏参数个数，
// E_FMT_DERIVE 先按上限解析一遍拿 count、再按 count 重解析一遍。
template <std::size_t N>
struct derived_names_t {
  std::string_view items[N];
  long long values[N];   // 枚举取值（结构体不用，保留以共用一套打印器）
  std::size_t count = 0;
  bool is_enum = false;
  bool valid = false;                // 是否解析成功
  bool parsed = false;               // 结构层面成立（大括号配平、属性位置合法）
  bool unknown_initializer = false;  // 枚举里有认不出的初始值（非字面量）
};

// ---------------------------------------------------------------------------
// 属性（[[...]]）与标签：E_FMT_DERIVE 的"基座"原料
// ---------------------------------------------------------------------------
// 属性是标准 C++ 语法，编译器忽略不认识的（GCC 会告 -Wattributes，宏里已局部静音），
// 但 #decl 字符串化后原文还在 —— 解析器就从这里读标签，上层（eserde 等）再按标签
// 生成序列化/CLI 代码：
//   [[efmt::arg(short, long)]]                    etl::string<12> name;
//   [[efmt::arg(long, help = "monthly salary")]]  float salary;
// 认不出的写法（标签不是标识符、标签超过上限、属性夹在声明中间）置 valid = false，
// 由上层 static_assert 报出来，绝不静默吞掉。
struct tag_t {
  std::string_view name{};
  std::string_view value{};
  bool has_value = false;
};

template <std::size_t MaxTags>
struct tag_list_t {
  tag_t items[MaxTags]{};
  std::size_t count = 0;
  bool valid = true;
};

// 单个字段 / 枚举取值的视图：只持 string_view，不进打印表 ⇒ 不用就不占 Flash
struct field_view_t {
  std::string_view name{};
  std::string_view type_name{};
  std::string_view attrs_head{};
  std::string_view attrs_tail{};
  long long value = 0;      // 枚举取值（结构体字段不用）
  bool explicit_value = false;
  bool has_attrs = false;
  bool valid = true;
};

// 类型 schema：只持有声明原文 + 条目数，字段/标签按需在编译期重新扫描
template <std::size_t MaxN>
struct derived_schema_t {
  std::string_view decl{};
  std::size_t count = 0;
  bool valid = false;
  bool parsed = false;
  bool is_enum = false;

  constexpr field_view_t field(std::size_t index) const;
  constexpr field_view_t enum_item(std::size_t index) const;
  constexpr field_view_t item(std::size_t index) const;      // 统一入口（按 is_enum 分派）
  constexpr std::size_t find(std::string_view name) const;   // 按名字反查（npos = 没有）
  constexpr std::size_t find_by_tag(std::string_view tag) const;  // 按标签反查
  constexpr tag_list_t<EFMT_DERIVE_MAX_TAGS> tags(std::size_t index) const;
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

// 声明符里的字段名：最后一个标识符，遇到 [ = 停。
// 单个 ':' 是位域（int flags : 3）要停；"::" 是限定名的一部分
// （std::string label 的字段名是 label，不是 std），必须跳过继续读。
constexpr std::string_view declarator_name(std::string_view frag) {
  std::string_view last{};
  std::size_t i = 0;
  while (i < frag.size()) {
    const char c = frag[i];
    if (c == '[' || c == '=') break;
    if (c == ':') {
      if (i + 1 < frag.size() && frag[i + 1] == ':') {
        i += 2;
        continue;
      }
      break;
    }
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

// ---------------------------------------------------------------------------
// 属性（[[...]]）扫描：标签解析的第一步
// ---------------------------------------------------------------------------
struct attr_span_t {
  bool found = false;
  std::size_t begin = 0;
  std::size_t end = 0;   // 配对 ]] 之后的位置
};

// s[p] 起是不是一段 [[...]]（段内可再嵌套 [[ ]]）
constexpr attr_span_t attr_span_at(std::string_view s, std::size_t p) {
  attr_span_t out{};
  if (p + 1 >= s.size() || s[p] != '[' || s[p + 1] != '[') return out;
  std::size_t i = p + 2;
  int depth = 1;
  while (i < s.size()) {
    if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '[') { ++depth; i += 2; continue; }
    if (s[i] == ']' && i + 1 < s.size() && s[i + 1] == ']') {
      if (--depth == 0) { out.found = true; out.begin = p; out.end = i + 2; return out; }
      i += 2; continue;
    }
    ++i;
  }
  return out;
}

// 语句里的属性布局：只接受"前导段 + 尾部段"。属性夹在声明中间会改写字段名解析，
// 宁可报错也不猜。rest 是去掉属性后的声明文本（仍是连续区间）。
struct attr_layout_t {
  std::string_view rest{};
  std::string_view head{};
  std::string_view tail{};
  bool has_head = false;
  bool has_tail = false;
  bool ok = true;
};

constexpr attr_layout_t split_attributes(std::string_view stmt) {
  constexpr std::size_t kMaxSegs = 4;
  attr_layout_t out{};
  const std::string_view s = trim(stmt);
  out.rest = s;
  if (s.find("[[") == std::string_view::npos) return out;

  attr_span_t segs[kMaxSegs]{};
  std::size_t seg_count = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t open = s.find("[[", i);
    if (open == std::string_view::npos) break;
    const attr_span_t seg = attr_span_at(s, open);
    if (!seg.found || seg_count >= kMaxSegs) { out.ok = false; return out; }
    segs[seg_count++] = seg;
    i = seg.end;
  }

  std::size_t head_end = 0;          // 前导连续段
  std::size_t k = 0;
  if (segs[0].begin == 0) {
    head_end = segs[0].end;
    k = 1;
    while (k < seg_count && trim(s.substr(head_end, segs[k].begin - head_end)).empty()) {
      head_end = segs[k].end;
      ++k;
    }
  }
  std::size_t tail_begin = s.size();  // 尾部连续段
  std::size_t j = seg_count;
  while (j > 0 && trim(s.substr(segs[j - 1].end, tail_begin - segs[j - 1].end)).empty()) {
    tail_begin = segs[j - 1].begin;
    --j;
  }
  if (j > k || head_end > tail_begin) { out.ok = false; return out; }   // 属性夹在中间

  if (head_end > 0) { out.has_head = true; out.head = s.substr(0, head_end); }
  if (tail_begin < s.size()) { out.has_tail = true; out.tail = s.substr(tail_begin); }
  out.rest = s.substr(head_end, tail_begin - head_end);
  return out;
}

#if EFMT_DERIVE_ENABLE_TAGS
// [[efmt::arg(a, b = "v")]] → 标签表；认不出的写法置 valid = false（上层 static_assert）
template <std::size_t MaxTags>
constexpr void push_tag_items(std::string_view args, tag_list_t<MaxTags> &out) {
  int depth = 0;
  bool in_str = false;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= args.size(); ++i) {
    const char c = (i < args.size()) ? args[i] : ',';
    if (in_str) {
      if (c == '\\') { ++i; continue; }
      if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(' || c == '<' || c == '{' || c == '[') { ++depth; continue; }
    if (c == ')' || c == '>' || c == '}' || c == ']') { --depth; continue; }
    if (c != ',' || depth != 0) continue;

    const std::string_view item = trim(args.substr(begin, i - begin));
    begin = i + 1;
    if (item.empty()) continue;

    const std::size_t eq = item.find('=');
    const std::string_view n = trim(eq == std::string_view::npos ? item : item.substr(0, eq));
    bool name_ok = !n.empty() && is_alpha(n[0]);
    for (std::size_t k = 0; name_ok && k < n.size(); ++k) name_ok = is_ident(n[k]);
    if (!name_ok) { out.valid = false; continue; }   // 标签名必须是标识符

    tag_t tag{};
    tag.name = n;
    if (eq != std::string_view::npos) {
      std::string_view v = trim(item.substr(eq + 1));
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
      tag.value = v;
      tag.has_value = true;
    }
    if (out.count < MaxTags) out.items[out.count++] = tag;
    else out.valid = false;                          // 超过 EFMT_DERIVE_MAX_TAGS
  }
}

// 扫 attrs 文本里的每段 [[...]]，只认 efmt::arg(...)，其余属性原样跳过
template <std::size_t MaxTags>
constexpr void collect_tags(std::string_view attrs, tag_list_t<MaxTags> &out) {
  std::size_t i = 0;
  while (i < attrs.size()) {
    const std::size_t open = attrs.find("[[", i);
    if (open == std::string_view::npos) return;
    const attr_span_t seg = attr_span_at(attrs, open);
    if (!seg.found) { out.valid = false; return; }
    i = seg.end;
    const std::string_view body = attrs.substr(open + 2, (seg.end - 2) - (open + 2));

    std::size_t p = 0;
    while (p < body.size() && is_space(body[p])) ++p;
    const std::size_t nb = p;
    while (p < body.size() && (is_ident(body[p]) || body[p] == ':')) ++p;
    if (body.substr(nb, p - nb) != "efmt::arg") continue;   // 别的属性不管
    if (p >= body.size() || body[p] != '(') { out.valid = false; continue; }

    const std::size_t ab = p + 1;
    std::size_t q = ab;
    int depth = 1;
    bool in_str = false;
    while (q < body.size() && depth > 0) {
      const char c = body[q];
      if (in_str) {
        if (c == '\\') { q += 2; continue; }
        if (c == '"') in_str = false;
        ++q; continue;
      }
      if (c == '"') { in_str = true; ++q; continue; }
      if (c == '(' || c == '<' || c == '{' || c == '[') { ++depth; ++q; continue; }
      if (c == ')' || c == '>' || c == '}' || c == ']') {
        --depth;
        if (depth == 0) break;
        ++q; continue;
      }
      ++q;
    }
    push_tag_items(body.substr(ab, q - ab), out);
  }
}
#else
// 裁剪：不解析标签，属性只当注释跳过
template <std::size_t MaxTags>
constexpr void collect_tags(std::string_view, tag_list_t<MaxTags> &) {}
#endif

// ---------------------------------------------------------------------------
// 逐条取字段 / 枚举取值（schema 视图用；判断逻辑与打印用解析器完全一致）
// ---------------------------------------------------------------------------
// 不是数据字段的语句：静态成员 / 类型别名 / 访问修饰符 / 友元 / 模板
constexpr bool is_skipped_statement(std::string_view stmt) {
  return has_word(stmt, "static") || has_word(stmt, "using") || has_word(stmt, "typedef") ||
         has_word(stmt, "friend") || has_word(stmt, "template") ||
         has_word(stmt, "public") || has_word(stmt, "private") ||
         has_word(stmt, "protected");
}

// 声明符之前的部分就是类型名（原样保留 * & 与模板参数，只去空白）
constexpr std::string_view declarator_type_name(std::string_view part, std::string_view name) {
  const std::size_t at = static_cast<std::size_t>(name.data() - part.data());
  return trim(part.substr(0, at));
}

// 第 index 个字段（结构体）：名字 / 类型名 / 标签原文
constexpr field_view_t struct_field_at(std::string_view body, std::size_t index) {
  field_view_t out{};
  int depth = 0;
  std::size_t stmt_begin = 0;
  std::size_t seen = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ';';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ';' || depth != 0) continue;

    const std::string_view raw = trim(body.substr(stmt_begin, i - stmt_begin));
    stmt_begin = i + 1;
    if (raw.empty()) continue;
    const attr_layout_t lay = split_attributes(raw);
    if (!lay.ok) { out.valid = false; return out; }
    const std::string_view stmt = trim(lay.rest);
    if (stmt.empty() || is_skipped_statement(stmt)) continue;

    if (stmt.find('(') != std::string_view::npos) {              // 成员函数 / 函数指针
      const std::string_view fn = function_pointer_name(stmt);
      if (fn.empty()) continue;                                  // 成员函数不算字段
      if (seen != index) { ++seen; continue; }
      const std::size_t star = stmt.find("(*");
      out.name = fn;
      out.type_name = trim(stmt.substr(0, star == std::string_view::npos ? 0 : star));
      out.attrs_head = lay.head;
      out.attrs_tail = lay.tail;
      out.has_attrs = lay.has_head || lay.has_tail;
      return out;
    }

    int inner = 0;
    std::size_t part_begin = 0;
    for (std::size_t j = 0; j <= stmt.size(); ++j) {
      const char c2 = (j < stmt.size()) ? stmt[j] : ',';
      if (c2 == '(' || c2 == '[' || c2 == '{' || c2 == '<') { ++inner; continue; }
      if (c2 == ')' || c2 == ']' || c2 == '}' || c2 == '>') { --inner; continue; }
      if (c2 != ',' || inner != 0) continue;
      const std::string_view part = trim(stmt.substr(part_begin, j - part_begin));
      part_begin = j + 1;
      const std::string_view name = declarator_name(part);
      if (name.empty()) continue;
      if (seen != index) { ++seen; continue; }
      out.name = name;
      out.type_name = declarator_type_name(part, name);
      out.attrs_head = lay.head;
      out.attrs_tail = lay.tail;
      out.has_attrs = lay.has_head || lay.has_tail;
      return out;
    }
  }
  out.valid = false;   // 下标越界
  return out;
}

// 第 index 个枚举取值：名字 / 数值 / 标签原文
constexpr field_view_t enum_item_at(std::string_view body, std::size_t index) {
  field_view_t out{};
  long long next_value = 0;
  int depth = 0;
  std::size_t begin = 0;
  std::size_t seen = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ',';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ',' || depth != 0) continue;

    const std::string_view raw = trim(body.substr(begin, i - begin));
    begin = i + 1;
    if (raw.empty()) continue;
    const attr_layout_t lay = split_attributes(raw);
    if (!lay.ok) { out.valid = false; return out; }
    const std::string_view item = trim(lay.rest);
    if (item.empty()) continue;

    const std::size_t eq = item.find('=');
    const std::string_view name_part = trim(eq == std::string_view::npos ? item : item.substr(0, eq));
    std::size_t len = 0;
    while (len < name_part.size() && is_ident(name_part[len])) ++len;
    if (len == 0) continue;

    long long value = next_value;
    bool explicit_value = false;
    if (eq != std::string_view::npos) {
      bool literal_ok = false;
      const long long parsed = parse_integer_literal(trim(item.substr(eq + 1)), literal_ok);
      if (!literal_ok) continue;      // 认不出的初值：parse_enum_body 会置 unknown_initializer
      value = parsed;
      explicit_value = true;
    }
    next_value = value + 1;

    if (seen != index) { ++seen; continue; }
    out.name = name_part.substr(0, len);
    out.value = value;
    out.explicit_value = explicit_value;
    out.attrs_head = lay.head;
    out.attrs_tail = lay.tail;
    out.has_attrs = lay.has_head || lay.has_tail;
    return out;
  }
  out.valid = false;
  return out;
}

// 结构体体：按 ; 切语句（深度 0），跳过静态成员/别名/访问修饰符/函数，再按 , 切声明符
template <std::size_t MaxN>
constexpr derived_names_t<MaxN> parse_struct_body(std::string_view body) {
  derived_names_t<MaxN> out{};
  int depth = 0;
  bool ok = true;
  bool parsed_ok = true;
  std::size_t stmt_begin = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ';';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ';' || depth != 0) continue;

    const std::string_view raw = trim(body.substr(stmt_begin, i - stmt_begin));
    stmt_begin = i + 1;
    if (raw.empty()) continue;
    // [[...]] 先摘掉：属性里可能有 '(' 和 ','，不摘会被当成成员函数/多声明符
    const attr_layout_t lay = split_attributes(raw);
    if (!lay.ok) { parsed_ok = false; continue; }
    const std::string_view stmt = trim(lay.rest);
    if (stmt.empty()) continue;
    if (is_skipped_statement(stmt)) {
      continue;   // 静态成员 / 类型别名 / 访问修饰符：不是数据字段
    }
    if (stmt.find('(') != std::string_view::npos) {
      const std::string_view fn = function_pointer_name(stmt);   // 函数指针成员要算字段
      if (!fn.empty()) {
        if (out.count < MaxN) out.items[out.count++] = fn;
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
        if (out.count < MaxN) out.items[out.count++] = name;
        else ok = false;
      }
      part_begin = j + 1;
    }
  }
  out.parsed = parsed_ok;
  out.valid = parsed_ok && ok && out.count > 0;
  return out;
}

// 枚举体：按 , 切项，首标识符为名字，可选的 = 整数字面量
template <std::size_t MaxN>
constexpr derived_names_t<MaxN> parse_enum_body(std::string_view body) {
  derived_names_t<MaxN> out{};
  out.is_enum = true;
  long long next_value = 0;
  int depth = 0;
  bool ok = true;
  bool parsed_ok = true;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const char c = (i < body.size()) ? body[i] : ',';
    if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
    if (c == ')' || c == ']' || c == '}') { --depth; continue; }
    if (c != ',' || depth != 0) continue;

    const std::string_view raw = trim(body.substr(begin, i - begin));
    begin = i + 1;
    if (raw.empty()) continue;
    // [[...]] 先摘掉：不摘的话取值名的首字符是 '[' → 整条被当垃圾跳过、静默丢字段
    const attr_layout_t lay = split_attributes(raw);
    if (!lay.ok) { parsed_ok = false; continue; }
    const std::string_view item = trim(lay.rest);
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
    if (out.count < MaxN) {
      out.items[out.count] = name_part.substr(0, len);
      out.values[out.count] = value;
      ++out.count;
    } else {
      ok = false;
    }
    next_value = value + 1;
  }
  out.parsed = parsed_ok;
  out.valid = parsed_ok && ok && out.count > 0 && !out.unknown_initializer;
  return out;
}

template <std::size_t MaxN>
constexpr derived_names_t<MaxN> parse_derived_declaration(std::string_view text) {
  const std::size_t open = text.find('{');
  if (open == std::string_view::npos) return derived_names_t<MaxN>{};
  int depth = 0;
  std::size_t close = std::string_view::npos;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    else if (text[i] == '}') { if (--depth == 0) { close = i; break; } }
  }
  if (close == std::string_view::npos) return derived_names_t<MaxN>{};

  const std::string_view head = trim(text.substr(0, open));
  const std::string_view body = text.substr(open + 1, close - open - 1);
  return (head.size() >= 5 && head.substr(0, 5) == "enum ")
             ? parse_enum_body<MaxN>(body)
             : parse_struct_body<MaxN>(body);
}

}  // namespace derive_detail

// ---------------------------------------------------------------------------
// schema 视图：声明原文 + 懒查询（字段名/类型名/标签都在用的时候才扫）
// ---------------------------------------------------------------------------
template <std::size_t MaxN>
constexpr derived_schema_t<MaxN> parse_derived_schema(std::string_view text) {
  derived_schema_t<MaxN> out{};
  out.decl = text;
  const std::size_t open = text.find('{');
  if (open == std::string_view::npos) return out;
  const std::string_view head = derive_detail::trim(text.substr(0, open));
  out.is_enum = head.size() >= 5 && head.substr(0, 5) == "enum ";

  const derived_names_t<MaxN> names = derive_detail::parse_derived_declaration<MaxN>(text);
  out.count = names.count;
  out.parsed = names.parsed;
  out.valid = names.valid;
  return out;
}

template <std::size_t MaxN>
constexpr field_view_t derived_schema_t<MaxN>::field(std::size_t index) const {
  const std::size_t open = decl.find('{');
  if (open == std::string_view::npos || index >= count) { field_view_t bad{}; bad.valid = false; return bad; }
  int depth = 0;
  std::size_t close = std::string_view::npos;
  for (std::size_t i = open; i < decl.size(); ++i) {
    if (decl[i] == '{') ++depth;
    else if (decl[i] == '}') { if (--depth == 0) { close = i; break; } }
  }
  if (close == std::string_view::npos) { field_view_t bad{}; bad.valid = false; return bad; }
  return derive_detail::struct_field_at(decl.substr(open + 1, close - open - 1), index);
}

template <std::size_t MaxN>
constexpr field_view_t derived_schema_t<MaxN>::enum_item(std::size_t index) const {
  const std::size_t open = decl.find('{');
  if (open == std::string_view::npos || index >= count) { field_view_t bad{}; bad.valid = false; return bad; }
  int depth = 0;
  std::size_t close = std::string_view::npos;
  for (std::size_t i = open; i < decl.size(); ++i) {
    if (decl[i] == '{') ++depth;
    else if (decl[i] == '}') { if (--depth == 0) { close = i; break; } }
  }
  if (close == std::string_view::npos) { field_view_t bad{}; bad.valid = false; return bad; }
  return derive_detail::enum_item_at(decl.substr(open + 1, close - open - 1), index);
}

template <std::size_t MaxN>
constexpr field_view_t derived_schema_t<MaxN>::item(std::size_t index) const {
  return is_enum ? enum_item(index) : field(index);
}

template <std::size_t MaxN>
constexpr std::size_t derived_schema_t<MaxN>::find(std::string_view name) const {
  for (std::size_t i = 0; i < count; ++i) {
    if (item(i).name == name) return i;
  }
  return static_cast<std::size_t>(-1);
}

template <std::size_t MaxN>
constexpr std::size_t derived_schema_t<MaxN>::find_by_tag(std::string_view tag) const {
  for (std::size_t i = 0; i < count; ++i) {
    const tag_list_t<EFMT_DERIVE_MAX_TAGS> list = tags(i);
    for (std::size_t k = 0; k < list.count; ++k) {
      if (list.items[k].name == tag) return i;
    }
  }
  return static_cast<std::size_t>(-1);
}

template <std::size_t MaxN>
constexpr tag_list_t<EFMT_DERIVE_MAX_TAGS> derived_schema_t<MaxN>::tags(std::size_t index) const {
  tag_list_t<EFMT_DERIVE_MAX_TAGS> out{};
  if (index >= count) { out.valid = false; return out; }
  const field_view_t view = item(index);
  if (!view.valid) { out.valid = false; return out; }
  if (!view.has_attrs) return out;
  derive_detail::collect_tags(view.attrs_head, out);
  derive_detail::collect_tags(view.attrs_tail, out);
  return out;
}

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
// Names 是 derived_names_t<N>（N 随类型变化），这里模板化避免写死 16 条
// Names == nullptr 时只输出值（位置式）
template <typename Names, typename T>
void write_field(format_context &ctx, const Names *names, std::size_t index,
                 const T &value, const derive_style &style) {
  if (index != 0) {
    ctx.write_str(style.sep);
  }
  if (names != nullptr) {
    ctx.write_str(names->items[index]);
    ctx.write_str(style.name_sep);
  }
  derive_write_value(ctx, value);
}

template <typename Names>
inline void open_bracket(format_context &ctx, const Names *names,
                            const derive_style &style) {
  ctx.write_str(names != nullptr ? style.open : "(");
}

template <typename Names>
inline void close_bracket(format_context &ctx, const Names *names,
                             const derive_style &style) {
  ctx.write_str(names != nullptr ? style.close : ")");
}

// 按字段数特化的打印器：每个绑定直接交给格式化器（不经过 std::tie）
template <std::size_t N> struct derived_printer;

template <> struct derived_printer<1> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<2> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<3> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<4> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<5> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<6> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<7> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<8> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<9> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<10> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<11> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<12> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    write_field(ctx, names, 11, m11, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<13> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    write_field(ctx, names, 11, m11, style);
    write_field(ctx, names, 12, m12, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<14> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    write_field(ctx, names, 11, m11, style);
    write_field(ctx, names, 12, m12, style);
    write_field(ctx, names, 13, m13, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<15> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    write_field(ctx, names, 11, m11, style);
    write_field(ctx, names, 12, m12, style);
    write_field(ctx, names, 13, m13, style);
    write_field(ctx, names, 14, m14, style);
    close_bracket(ctx, names, style);
  }
};

template <> struct derived_printer<16> {
  template <typename T, typename Names = derived_names_t<1>>
  static void run(const T &value, format_context &ctx, const Names *names,
                             const derive_style &style) {
    auto &[m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15] = value;
    open_bracket(ctx, names, style);
    write_field(ctx, names, 0, m0, style);
    write_field(ctx, names, 1, m1, style);
    write_field(ctx, names, 2, m2, style);
    write_field(ctx, names, 3, m3, style);
    write_field(ctx, names, 4, m4, style);
    write_field(ctx, names, 5, m5, style);
    write_field(ctx, names, 6, m6, style);
    write_field(ctx, names, 7, m7, style);
    write_field(ctx, names, 8, m8, style);
    write_field(ctx, names, 9, m9, style);
    write_field(ctx, names, 10, m10, style);
    write_field(ctx, names, 11, m11, style);
    write_field(ctx, names, 12, m12, style);
    write_field(ctx, names, 13, m13, style);
    write_field(ctx, names, 14, m14, style);
    write_field(ctx, names, 15, m15, style);
    close_bracket(ctx, names, style);
  }
};

// 枚举：取值 → 名字；未列出的取值 → 底层整数
template <typename T, typename Names>
void derive_write_enum(const T &value, format_context &ctx, const format_specs &specs,
                       const Names &names) {
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
                    const derived_names_t<N> &names) {
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
    derived_printer<N>::run(value, ctx, &names, make_derive_style(specs));
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
    derived_printer<count>::run(
        value, ctx, static_cast<const derived_names_t<count> *>(nullptr),
        derive_style{});
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
    efmt_derive_format(value, ctx, format_specs{});   // 嵌套固定默认样式：{:#} 只作用于顶层   // E_FMT_DERIVE 推导过的类型
  } else if constexpr (has_field_names_v<D>) {
    format_via_field_names(ctx, format_specs{}, value);   // 类型内一行 E_FMT_FIELDS(...)
  } else if constexpr (std::is_aggregate_v<D> && (aggregate_field_count<D>() > 0)) {
    derive_write_positional(value, ctx);     // 内层没声明 → 位置式递归
  } else {
#if EFMT_DERIVE_STRICT
    // 白名单式检查（只认内置 + 显式 formatter<> 特化）会误杀走 default_formatter
    // 偏特化的容器/流式类型：它们有自己的通道，无需特化。这里改成"漏注册检测"——
    // 只拦【带字段的聚合体】和【未注册的枚举】，与 default_formatter 主模板
    // （format_traits.hpp）里的检查同构。
    static_assert(!(std::is_aggregate<D>::value && aggregate_field_count<D>() > 0) &&
                      !std::is_enum<D>::value,
                  "成员类型没有格式化器：带字段的聚合体多半是忘记注册或宏写错了作用域"
                  "（请用 E_FMT_DERIVE(...) 或类型内 E_FMT_FIELDS(...)），枚举请用 "
                  "E_FMT_FORMATTER_ENUM。容器类型走通用迭代器通道，无需特化。"
                  "也可以定义 EFMT_DERIVE_STRICT=0 退回打印地址。");
#endif
    formatter<D>::format(ctx, specs, value);
  }
}

// ---------------------------------------------------------------------------
// 类型内一行：E_FMT_FIELDS(字段, ...)
// ---------------------------------------------------------------------------
// 解析 "retry, verbose" 这样的名字清单；N = 宏参数个数，表正好 N 条
// （E_FMT_FIELDS 的宏参数个数编译期已知，直接按它开户，不用两遍解析）
template <std::size_t MaxN>
constexpr derived_names_t<MaxN> parse_name_list(std::string_view text) {
  derived_names_t<MaxN> out{};
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
    if (out.count < MaxN) out.items[out.count++] = item;
  }
  out.valid = out.count > 0;
  return out;
}

template <typename T>
void format_via_field_names(format_context &ctx, const format_specs &specs,
                            const T &value) {
  using names_type = decltype(T::efmt_field_names());
  static constexpr names_type names = T::efmt_field_names();
  static_assert(names.valid,
                "E_FMT_FIELDS(...) 里没写字段名，或字段数超过 EFMT_DERIVE_MAX_FIELDS");
  if constexpr (EFMT_DERIVE_SHOW_TYPE != 0) {
    const std::string_view type = type_name<T>();
    if (!type.empty()) {
      ctx.write_str(type);
      ctx.write_char(' ');
    }
  }
  derived_printer<names.count>::run(value, ctx, &names, make_derive_style(specs));
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

// 属性是标准 C++ 语法，但 GCC/Clang 对不认识的带命名空间属性会告 -Wattributes
// （efmt::arg 正是这类）。宏是重新发出这段声明的唯一地方，所以在这里局部静音，
// 用户不用改自己的编译开关；不认识这些 pragma 的编译器（MSVC）直接跳过。
#if defined(__GNUC__) || defined(__clang__)
#define EFMT_DERIVE_DETAIL_ATTR_PUSH                                                \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wattributes\"")
#define EFMT_DERIVE_DETAIL_ATTR_POP _Pragma("GCC diagnostic pop")
#else
#define EFMT_DERIVE_DETAIL_ATTR_PUSH
#define EFMT_DERIVE_DETAIL_ATTR_POP
#endif

// C++17 下"省略可变参数"（E_FMT_DERIVE(struct X { int a; }) 不写能力标签）只给
// -Wpedantic 警告，C++20 才合法；调用点就在宏里，包一层 pragma 让 -Wpedantic -Werror
// 的工程也能过。老 GCC（< 11）不认识这个告警名，那时不加 pragma（免得冒出未知告警）。
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 11)
#define EFMT_DERIVE_DETAIL_VARIADIC_PUSH                                            \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wc++20-extensions\"")
#define EFMT_DERIVE_DETAIL_VARIADIC_POP _Pragma("GCC diagnostic pop")
#else
#define EFMT_DERIVE_DETAIL_VARIADIC_PUSH
#define EFMT_DERIVE_DETAIL_VARIADIC_POP
#endif

// 生成格式化函数（结构体/枚举共用）：解析两遍，第一遍按上限拿字段数并做完校验，
// 第二遍按真实条目数重建静态表（正好 N 条，省 Flash）。
#define EFMT_DERIVE_DETAIL_FORMATTER(id, ...)                                        \
  inline void efmt_derive_format(                                                     \
      const decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id)) &value,            \
      ::e_fmt::detail::format_context &ctx,                                           \
      const ::e_fmt::detail::format_specs &specs) {                                   \
    using efmt_derive_type = decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id));  \
    static constexpr auto efmt_derive_tmp_ =                                          \
        ::e_fmt::detail::derive_detail::parse_derived_declaration<                    \
            EFMT_DERIVE_MAX_FIELDS>(#__VA_ARGS__);                                    \
    static_assert(!efmt_derive_tmp_.unknown_initializer,                              \
        "枚举里有非字面量的初始值（例如 A = 1 << 3）：E_FMT_DERIVE 的自动模式只认"     \
        "整数字面量。请改用 E_FMT_FIELDS(取值名, ...) 写在枚举内部显式列出");          \
    static_assert(efmt_derive_tmp_.valid,                                             \
        "E_FMT_DERIVE 解析不出这段声明里的字段/取值：请检查写法，或改用 "              \
        "E_FMT_FIELDS(字段, ...) 写在类型内部显式列出");                              \
    constexpr std::size_t efmt_derive_count = efmt_derive_tmp_.count;                 \
    static constexpr auto efmt_derive_names_ =                                        \
        ::e_fmt::detail::derive_detail::parse_derived_declaration<                    \
            (efmt_derive_count > 0 ? efmt_derive_count : 1)>(#__VA_ARGS__);           \
    ::e_fmt::detail::format_derived<efmt_derive_type, efmt_derive_count>(             \
        value, ctx, specs, efmt_derive_names_);                                       \
  }

// 结构体 / 类 / 联合体：第一个参数是声明本身，后面全是能力标签（原样给上层查）。
// decl 里不能有顶层逗号 —— 预处理器按顶层逗号切参数，切断了就拼不回声明，
// 所以第一道 static_assert 专门把这种情况报成看得懂的话。
#define EFMT_DERIVE_DETAIL_UNIQUE(id, decl, ...)                                     \
  static_assert(                                                                     \
      ::e_fmt::detail::derive_detail::parse_derived_declaration<                     \
          EFMT_DERIVE_MAX_FIELDS>(#decl).parsed,                                     \
      "E_FMT_DERIVE 第一个参数只能是【声明本身】，不能有顶层逗号：一行多字段"        \
      "（int x, y;）请拆成一行一个；枚举请用 E_FMT_DERIVE_ENUM(...)；"               \
      "std::pair<int, int> 这类类型名里带逗号的先用 typedef 消掉；"                  \
      "属性只能写在字段声明的前面或后面");                                           \
  EFMT_DERIVE_DETAIL_ATTR_PUSH                                                       \
  extern decl EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id);                          \
  EFMT_DERIVE_DETAIL_ATTR_POP                                                        \
  EFMT_DERIVE_DETAIL_FORMATTER(id, decl)                                             \
  /* ---- 基座钩子：efmt 只登记，不解释标签（上层 eserde 等来查）---- */             \
  EFMT_DERIVE_DETAIL_SCHEMA_HOOK(id, decl)                                           \
  EFMT_DERIVE_DETAIL_CAPS_HOOK(id, __VA_ARGS__)

// 枚举：整段声明一起进来。枚举体的逗号是【顶层】的（预处理器只认圆括号保护），
// 所以枚举走不了"第一个参数 = 声明"那条路 —— 整段收进来，逗号反而全都保住了。
//   E_FMT_DERIVE_ENUM(enum class state { idle, busy = 5, fault });
//   E_FMT_DERIVE_ENUM(enum class code : unsigned char { ok = 0, warn = 0x10 });
// 能力标签（Debug/Serialize）暂时只在结构体侧支持，枚举侧先登记空清单。
#define E_FMT_DERIVE_ENUM(...) EFMT_DERIVE_ENUM_DETAIL(EFMT_DERIVE_DETAIL_ID, __VA_ARGS__)
#define EFMT_DERIVE_ENUM_DETAIL(id, ...)                                             \
  EFMT_DERIVE_DETAIL_ATTR_PUSH                                                       \
  extern __VA_ARGS__ EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id);                   \
  EFMT_DERIVE_DETAIL_ATTR_POP                                                        \
  EFMT_DERIVE_DETAIL_FORMATTER(id, __VA_ARGS__)                                      \
  EFMT_DERIVE_DETAIL_SCHEMA_HOOK(id, __VA_ARGS__)                                    \
  EFMT_DERIVE_DETAIL_CAPS_HOOK(id, )

// schema 原料：声明原文。constexpr 函数不被用到就不会生成，字符串也不进 rodata。
#if EFMT_DERIVE_ENABLE_SCHEMA
#define EFMT_DERIVE_DETAIL_SCHEMA_HOOK(id, ...)                                      \
  constexpr ::std::string_view efmt_derive_decl(                                     \
      ::e_fmt::detail::type_tag<decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id))>) {  \
    return #__VA_ARGS__;                                                             \
  }
#else
#define EFMT_DERIVE_DETAIL_SCHEMA_HOOK(id, ...)
#endif

// 能力标签登记（ADL，靠 type_tag<T> 的关联命名空间找到）：只声明不定义，
// 上层只用 decltype 查能力，不产生任何代码或数据。
#if EFMT_DERIVE_ENABLE_CAPS
#define EFMT_DERIVE_DETAIL_CAPS_HOOK(id, ...)                                        \
  constexpr ::e_fmt::detail::caps_list<__VA_ARGS__> efmt_derive_caps(                \
      ::e_fmt::detail::type_tag<decltype(EFMT_DERIVE_DETAIL_CAT(efmt_derive_reg_, id))>) {  \
    return {};                                                                       \
  }
#else
#define EFMT_DERIVE_DETAIL_CAPS_HOOK(id, ...)
#endif

// 用法：
//   E_FMT_DERIVE(struct imu { float ax; float ay; }, Debug, Serialize);   // 结构体
//   E_FMT_DERIVE_ENUM(enum class state { idle, busy = 5, fault });        // 枚举
#define E_FMT_DERIVE(...)                                                           \
  EFMT_DERIVE_DETAIL_VARIADIC_PUSH                                                  \
  EFMT_DERIVE_DETAIL_UNIQUE(EFMT_DERIVE_DETAIL_ID, __VA_ARGS__)                     \
  EFMT_DERIVE_DETAIL_VARIADIC_POP

// 类型内一行：只能列字段名，类型/字符串/成员指针全自动。
// 用在 E_FMT_DERIVE 覆盖不到的场合：声明里有 #if、模板结构体、字段数超上限、给已有类型补一行。
//   struct cfg { int retry; bool verbose; E_FMT_FIELDS(retry, verbose); };
#define E_FMT_FIELDS(...)                                                          static constexpr ::e_fmt::detail::derived_names_t<EFMT_DETAIL_ARG_COUNT(__VA_ARGS__)> efmt_field_names() {                return ::e_fmt::detail::parse_name_list<EFMT_DETAIL_ARG_COUNT(__VA_ARGS__)>(#__VA_ARGS__);                          }


} // namespace e_fmt::detail

#endif // FORMAT_DERIVE_HPP