/**
 ******************************************************************************
 * @file           : serde.hpp
 * @brief          : efmt 的编译期反射 / 能力基座（可选层，efmt 本体不认识它）
 * @attention      : 与 efmt 的关系等同于 elog：只单向依赖 efmt，不往 efmt 里塞东西。
 *                   它做三件事，全部编译期、零运行时、零依赖（只用 <string_view> 等）：
 *                     1. 能力标签查询：E_FMT_DERIVE(decl, Debug, Serialize) 里的标签
 *                        → has_cap_v<T, Serialize>
 *                     2. schema 查询：字段名 / 类型名（文本）/ 标签 / 枚举取值
 *                        → field_count / field_name / tag / find_field / find_by_tag
 *                     3. 按索引访问成员：field_at<I> / visit_fields（写序列化时用）
 *                   不 include 本文件时，efmt 的体积与行为一字不变。
 *                   序列化本体（JSON / 二进制 / CLI）由后续文件各自实现，
 *                   它们只 include 本文件；efmt 与 elog 都不认识它们。
 * @date           : 26-9-30
 ******************************************************************************
 */

#ifndef ESERDE_SERDE_HPP
#define ESERDE_SERDE_HPP

#include <middleware/efmt/core/format.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

// schema 原料（efmt_derive_decl）是 E_FMT_DERIVE 宏生成的；关掉它 eserde 就无从解析。
#if !EFMT_DERIVE_ENABLE_SCHEMA
#error "eserde 需要 EFMT_DERIVE_ENABLE_SCHEMA=1；若这份固件不用序列化，请不要 include eserde"
#endif

namespace eserde {

// ---------------------------------------------------------------------------
// 能力标签
// ---------------------------------------------------------------------------
// 写在 E_FMT_DERIVE(struct person { ... }, Debug, Serialize) 的标签位上。
// efmt 只把它们原样登记（caps_list<...>），不认识含义；含义在这里定义。
// Debug 由 efmt 提供（打印能力，也是默认能力），这里转出来方便 bare name 写：
//   using namespace e_fmt::detail 之外，用户只要 using namespace eserde 就能写 Debug。
struct Serialize {};
struct Deserialize {};
using ::e_fmt::Debug;

namespace detail {

using ::e_fmt::detail::caps_list;
using ::e_fmt::detail::field_view_t;
using ::e_fmt::detail::tag_t;
using ::e_fmt::detail::type_tag;

// 能力清单：没登记过就是空清单
template <typename T, typename = void> struct caps_of {
  using type = caps_list<>;
};

template <typename T>
struct caps_of<T, std::void_t<decltype(efmt_derive_caps(type_tag<T>{}))>> {
  using type = decltype(efmt_derive_caps(type_tag<T>{}));
};

template <typename Cap, typename List> struct contains : std::false_type {};

template <typename Cap, typename... Caps>
struct contains<Cap, caps_list<Caps...>>
    : std::bool_constant<(false || ... || std::is_same<Cap, Caps>::value)> {};

// 是否被 E_FMT_DERIVE / E_FMT_DERIVE_ENUM 注册过（能拿到声明原文即视为注册）
template <typename T, typename = void> struct registered : std::false_type {};

template <typename T>
struct registered<T, std::void_t<decltype(efmt_derive_decl(type_tag<T>{}))>> : std::true_type {};

template <typename T>
constexpr std::string_view decl_text() {
  static_assert(registered<T>::value,
                "这个类型没有注册过：schema/能力都读不到。"
                "结构体请用 E_FMT_DERIVE(struct X { ... }, 能力...)，枚举请用 "
                "E_FMT_DERIVE_ENUM(enum class X { ... })");
  return ::e_fmt::detail::derive_detail::trim(
      efmt_derive_decl(type_tag<T>{}));   // 去掉宏参数里带进来的缩进/换行
}

// schema 只在编译期构造一次（static constexpr ⇒ 必须是常量表达式 ⇒ 不会跑到运行时）
template <typename T> struct schema_holder {
  static constexpr auto value =
      ::e_fmt::detail::parse_derived_schema<EFMT_DERIVE_MAX_FIELDS>(decl_text<T>());
};

template <typename T, typename Visitor, std::size_t... I>
void visit_impl(T &obj, Visitor &vis, std::index_sequence<I...>) {
  static constexpr auto s = schema_holder<std::remove_cv_t<T>>::value;
  static_assert(!s.is_enum, "visit_fields 只用于结构体；枚举请查 schema 或 E_FMT_FORMATTER_ENUM");
  (static_cast<void>(vis(
       s.item(I).name,
       std::get<I>(::e_fmt::detail::aggregate_access<sizeof...(I)>::tie(obj)))),
   ...);
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 能力查询
// ---------------------------------------------------------------------------
// 注册过的类型默认就有 Debug（打印是 efmt 的默认行为），其余能力要看标签位写了没。
template <typename T> inline constexpr bool is_registered_v = detail::registered<T>::value;

template <typename T, typename Cap>
inline constexpr bool has_cap_v =
    detail::contains<Cap, typename detail::caps_of<T>::type>::value ||
    (std::is_same<Cap, ::e_fmt::Debug>::value && detail::registered<T>::value);

// ---------------------------------------------------------------------------
// schema 查询（全部 constexpr，可用于 static_assert）
// ---------------------------------------------------------------------------
template <typename T> constexpr std::size_t field_count() {
  return detail::schema_holder<T>::value.count;
}

template <typename T> constexpr bool is_enum() {
  return detail::schema_holder<T>::value.is_enum;
}

// 结构体字段名 / 枚举取值名（下标从 0 开始，按声明顺序）
template <typename T> constexpr std::string_view field_name(std::size_t index) {
  return detail::schema_holder<T>::value.item(index).name;
}

// 字段类型名（声明原文，纯文本：etl::string<12>、const char * 都会原样给出）
template <typename T> constexpr std::string_view field_type_name(std::size_t index) {
  return detail::schema_holder<T>::value.item(index).type_name;
}

// 枚举取值的数值；结构体字段上调用无意义
template <typename T> constexpr long long enum_value(std::size_t index) {
  return detail::schema_holder<T>::value.item(index).value;
}

template <typename T> constexpr std::size_t tag_count(std::size_t index) {
  return detail::schema_holder<T>::value.tags(index).count;
}

template <typename T> constexpr detail::tag_t tag(std::size_t index, std::size_t k) {
  return detail::schema_holder<T>::value.tags(index).items[k];
}

template <typename T> constexpr bool has_tag(std::size_t index, std::string_view name) {
  const auto list = detail::schema_holder<T>::value.tags(index);
  for (std::size_t k = 0; k < list.count; ++k) {
    if (list.items[k].name == name) return true;
  }
  return false;
}

// 按名字反查下标；找不到 = eserde::npos
inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

template <typename T> constexpr std::size_t find_field(std::string_view name) {
  return detail::schema_holder<T>::value.find(name);
}

// 按标签反查（[[efmt::arg(short, long)]] etl::string<12> name; → find_by_tag<T>("short") == 0）
template <typename T> constexpr std::size_t find_by_tag(std::string_view name) {
  return detail::schema_holder<T>::value.find_by_tag(name);
}

// ---------------------------------------------------------------------------
// 字段的"格式键名"：格式名就是标签名（json / cbor / ...）
// ---------------------------------------------------------------------------
// 默认用字段名；标了 [[efmt::arg(json = "别名")]] 就用别名。
// 各格式只传自己的名字进来，键名策略因此不用每个格式写一遍。
// 关掉 EFMT_DERIVE_ENABLE_TAGS 时标签读不到 → 一律退回字段名（不报错）。
template <typename T>
constexpr std::string_view field_key(std::size_t index, std::string_view format) {
  const std::size_t n = tag_count<T>(index);
  for (std::size_t k = 0; k < n; ++k) {
    const auto t = tag<T>(index, k);
    if (t.name == format && t.has_value) return t.value;
  }
  return field_name<T>(index);
}

// [[efmt::arg(json = "skip")]]：这个字段不进该格式，也不从该格式读
template <typename T>
constexpr bool field_skipped(std::size_t index, std::string_view format) {
  const std::size_t n = tag_count<T>(index);
  for (std::size_t k = 0; k < n; ++k) {
    const auto t = tag<T>(index, k);
    if (t.name == format && t.has_value && t.value == std::string_view("skip")) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 按索引访问成员（写序列化/反序列化时用）
// ---------------------------------------------------------------------------
// 字段数来自 schema（不是探测出来的），所以含数组成员的结构体也不会错位。
template <std::size_t I, typename T>
decltype(auto) field_at(T &&obj) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  constexpr std::size_t count = detail::schema_holder<U>::value.count;
  static_assert(count > 0, "这个类型没有字段（schema 为空）");
  static_assert(I < count, "字段下标越界");
  return std::get<I>(
      ::e_fmt::detail::aggregate_access<(count > 0 ? count : 1)>::tie(obj));
}

// 依次访问每个字段：vis(std::string_view 名字, 字段引用)
//   eserde::visit_fields(p, [](std::string_view name, const auto &v) { ... });
template <typename T, typename Visitor>
void visit_fields(T &&obj, Visitor &&vis) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  constexpr std::size_t count = detail::schema_holder<U>::value.count;
  static_assert(count > 0, "这个类型没有字段（schema 为空）");
  detail::visit_impl(obj, vis, std::make_index_sequence<count>{});
}

}  // namespace eserde

#endif  // ESERDE_SERDE_HPP
