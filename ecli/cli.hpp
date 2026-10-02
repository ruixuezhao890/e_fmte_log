/**
 ******************************************************************************
 * @file           : cli.hpp
 * @author         : ruixuezhao
 * @brief          : 命令行解析（对标 Rust clap 的 derive 用法）：声明即推导解析器
 * @attention      : 依赖方向：cli.hpp → eserde/traits.hpp → serde.hpp → efmt。
 *                   efmt / elog / eserde 都不认识它；不 include 就是零开销。
 *
 *                   它做三件事，全部构建在既有基座上（schema 标签 + 取值搬运助手）：
 *                     1. 词法：argv 或【任意一行文本】（串口 / 蓝牙 / 键盘读进来的）→ token 表
 *                     2. 取值：token → 字段（bool 开关 / 整数 / 浮点 / 枚举 / 字符串 / 可重复容器）
 *                     3. 表意：usage / help / 报错文本（snprintf 语义，写进调用方的缓冲区）
 *
 *                   与 json / cbor 的关系：那两者是"字节 ↔ 对象"，这里是"命令行 ↔ 对象"，
 *                   共用同一个 schema 与同一套取值助手，所以字段标签依然是 [[efmt::arg(...)]]。
 *
 *                   【多输入源】解析器不认识 argv，也不认识串口：它只认识 token 表。
 *                   所以串口 / 蓝牙 / 键盘这些来源在调用方那一层汇合：
 *                     每源一个 line_reader（字节 → 一行）→ tokenize（一行 → token）→ parse
 *                   每源必须有各自的行缓冲（否则两路输入会串词）；解析与执行不重入，
 *                   由调用方在主循环里串起来（库不提供线程原语、不碰 HAL）。
 *
 *                   用法（字段名一个字都不用写）：
 *                     E_FMT_DERIVE(struct args {
 *                       [[efmt::arg(short, long, help = "verbose output")]]      bool verbose = false;
 *                       [[efmt::arg(short = "o", long = "output", help = "file")]] const char *out = nullptr;
 *                       [[efmt::arg(short = "l", long = "level", help = "0..9")]]  int level = 3;
 *                       [[efmt::arg(pos = "1", help = "input file")]]             etl::string<64> input;
 *                     }, Cli);
 *
 *                     args a{};
 *                     if (ecli::parse(argc, argv, a) != ecli::error::ok) { ... }
 *
 *                   零第三方、零异常、零动态分配（宿主的 *_string 便利版除外）。
 * @date           : 26-10-02
 ******************************************************************************
 */

#ifndef ECLI_CLI_HPP
#define ECLI_CLI_HPP

#include <eserde/traits.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

// 一行命令行最多几个 token。超了报 too_many_tokens，绝不静默丢参数。
#ifndef ECLI_MAX_TOKENS
#define ECLI_MAX_TOKENS 16
#endif

// "去引号 + 反转义"的缓冲大小（只有带引号 / 转义的 token 才占它；原样 token 直接指原文）。
// 装不下报 too_many_tokens。
#ifndef ECLI_MAX_LINE
#define ECLI_MAX_LINE 192
#endif

// usage / help 文本开关：0 = 整段裁掉（省 Flash），-h / --help 也不再特殊处理。
#ifndef ECLI_ENABLE_HELP
#define ECLI_ENABLE_HELP 1
#endif

namespace ecli {

// ---------------------------------------------------------------------------
// 能力标签
// ---------------------------------------------------------------------------
// 写在 E_FMT_DERIVE(struct args { ... }, Cli) 的能力位上：没有它 = 编译期报错。
// 与 eserde 的 Serialize / Deserialize 一个套路（efmt 只原样登记，含义在这一层定义）。
struct Cli {};

// ---------------------------------------------------------------------------
// 错误码（不抛异常；与 json / cbor 一样配 error_name，方便统一打印）
// ---------------------------------------------------------------------------
enum class error {
  ok = 0,
  help_requested,    // -h / --help：不是失败，调用方打印帮助后正常退出
  unknown_option,    // --foo / -x：没有这个选项
  missing_value,     // --out 后面没有取值
  invalid_value,     // 取值转换失败（不是数字 / 装不下目标类型 / 枚举名认不出）
  value_too_long,    // 字符串目标装不下（不静默截断）
  missing_required,  // 必填项（required）没给
  too_many_args,     // 位置参数多给了
  too_many_values,   // 可重复选项把定长容器塞满了
  too_many_tokens,   // 词法缓冲装不下（token 数超 ECLI_MAX_TOKENS，或去引号后超 ECLI_MAX_LINE）
  bad_quote,         // 一行文本里引号没闭合
  unknown_command,   // 命令表里没有这个命令（ecli/command.hpp 用）
};

constexpr const char *error_name(error e) {
  switch (e) {
    case error::ok: return "ok";
    case error::help_requested: return "help_requested";
    case error::unknown_option: return "unknown_option";
    case error::missing_value: return "missing_value";
    case error::invalid_value: return "invalid_value";
    case error::value_too_long: return "value_too_long";
    case error::missing_required: return "missing_required";
    case error::too_many_args: return "too_many_args";
    case error::too_many_values: return "too_many_values";
    case error::too_many_tokens: return "too_many_tokens";
    case error::bad_quote: return "bad_quote";
    case error::unknown_command: return "unknown_command";
  }
  return "?";
}

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

// ---------------------------------------------------------------------------
// token 表：解析器唯一认识的输入
// ---------------------------------------------------------------------------
// argv 与"一行文本"最后都变成它。里面的 string_view 一律指向【调用方的存储】：
//   * from_argv 指 argv 本体（进程活多久就活多久）
//   * tokenize  指传进来的 scratch
// 所以它们的寿命必须盖过 args 结构体里 const char* / string_view 字段的使用期。
struct token_list {
  std::string_view items[ECLI_MAX_TOKENS];
  std::size_t count = 0;
  bool overflow = false;    // token 数超 ECLI_MAX_TOKENS，或文本装不下 scratch
  bool bad_quote = false;   // 引号没闭合
};

// ---------------------------------------------------------------------------
// 出错位置：给 write_error 用的上下文
// ---------------------------------------------------------------------------
struct error_info {
  std::string_view token{};         // 出问题的 token（选项名或取值）
  std::size_t option_index = npos;  // 相关字段在 schema 里的下标；npos = 没有
  std::size_t index = 0;            // token 在命令行里的下标（0 起）
};

// ---------------------------------------------------------------------------
// 一个字段在命令行里长什么样（编译期从 schema 标签算出来，运行时零解析）
// ---------------------------------------------------------------------------
struct option_view {
  std::string_view field{};      // 字段名（帮助里显示、报错里定位）
  std::string_view long_name{};  // 长选项名（不含 --）；空 = 没有
  std::string_view help{};       // help = "…" 的文本
  char short_name = 0;           // 短选项字符；0 = 没有
  std::size_t position = 0;      // 位置参数序号（1 起）；0 = 不是位置参数
  bool required = false;
  bool skip = false;             // [[efmt::arg(skip)]]：不进命令行
  bool takes_value = true;       // false = bool 开关
  bool repeatable = false;       // 容器目标：同名字段可重复给
};

namespace detail {

// 形状判定 / 取值搬运助手：与 json / cbor 共用，定义在 traits.hpp
using ::eserde::detail::has_max_size;
using ::eserde::detail::integer_fits;
using ::eserde::detail::integer_value;
using ::eserde::detail::is_char_pointer;
using ::eserde::detail::is_growable_range;
using ::eserde::detail::is_string_like_v;
using ::eserde::detail::is_writable_string;
using ::eserde::detail::push_checked;

// ---------------------------------------------------------------------------
// 文本输出：snprintf 语义（返回值 = 完整长度；能写就写，永远 NUL 结尾）
// ---------------------------------------------------------------------------
// 与 json::writer 同一套语义，但只有文本一种形态，所以不带十六进制等辅助。
struct text_out {
  char *buf = nullptr;
  std::size_t cap = 0;
  std::size_t n = 0;

  void put(char c) {
    // 写成 "cap != 0 && n < cap - 1" 而不是 "n + 1 < cap"：语义相同（永远给 NUL 留一格），
    // 但 GCC 的 -Wstringop-overflow 在内联链上能看懂这一版，不会误报"写进 0 大小区域"。
    if (cap != 0 && n < cap - 1) buf[n] = c;
    ++n;
  }

  void put(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) put(s[i]);
  }

  void put_lit(const char *s) {
    while (*s != 0) put(*s++);
  }

  void spaces(std::size_t k) {
    while (k-- != 0) put(' ');
  }

  std::size_t finish() {
    if (cap != 0) buf[n < cap ? n : cap - 1] = '\0';
    return n;
  }
};

// ---------------------------------------------------------------------------
// 字符 / 取值助手
// ---------------------------------------------------------------------------
constexpr bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

constexpr unsigned digit_value(char c) {
  if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
  return 16;   // 非法数字（>= 任何进制）
}

inline bool equals_ci(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (to_lower(a[i]) != to_lower(b[i])) return false;
  }
  return true;
}

// 整数：十进制 / 0x 十六进制 / 0b 二进制；允许前导 +/-。
// 溢出与"吃不满整个 token"都老实返回 false —— 由调用方按目标类型报 invalid_value。
// 溢出判定【不用除法】：arm-none-eabi 上 64 位除法会拉进 __udivmoddi4（720 B Flash）。
// 每个进制的"最大乘数 + 余量"都是编译期常量，两次比较就是精确判定。
constexpr unsigned long long kU64Max = ~0ULL;
constexpr unsigned long long kDecVmax = kU64Max / 10;
constexpr unsigned long long kDecVrem = kU64Max % 10;
constexpr unsigned long long kHexVmax = kU64Max / 16;
constexpr unsigned long long kHexVrem = kU64Max % 16;
constexpr unsigned long long kBinVmax = kU64Max / 2;
constexpr unsigned long long kBinVrem = kU64Max % 2;

inline bool parse_integer(std::string_view t, unsigned long long &mag, bool &neg) {
  mag = 0;
  neg = false;
  std::size_t i = 0;
  if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
    neg = (t[i] == '-');
    ++i;
  }
  unsigned base = 10;
  unsigned long long vmax = kDecVmax;
  unsigned long long vrem = kDecVrem;
  if (i + 1 < t.size() && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
    base = 16;
    vmax = kHexVmax;
    vrem = kHexVrem;
    i += 2;
  } else if (i + 1 < t.size() && t[i] == '0' && (t[i + 1] == 'b' || t[i + 1] == 'B')) {
    base = 2;
    vmax = kBinVmax;
    vrem = kBinVrem;
    i += 2;
  }
  if (i >= t.size()) return false;
  unsigned long long v = 0;
  for (; i < t.size(); ++i) {
    const unsigned d = digit_value(t[i]);
    if (d >= base) return false;
    if (v > vmax || (v == vmax && d > vrem)) return false;   // 溢出：报错，不绕回
    v = v * base + d;
  }
  mag = v;
  return true;
}

// 浮点：十进制 + 可选小数 + 可选指数（不收 inf / nan：与整数一样只认字面量）
inline bool parse_float(std::string_view t, double &out) {
  std::size_t i = 0;
  bool neg = false;
  if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
    neg = (t[i] == '-');
    ++i;
  }
  double value = 0.0;
  bool any = false;
  while (i < t.size() && is_digit(t[i])) {
    value = value * 10.0 + static_cast<double>(t[i] - '0');
    ++i;
    any = true;
  }
  if (i < t.size() && t[i] == '.') {
    ++i;
    double scale = 0.1;
    while (i < t.size() && is_digit(t[i])) {
      value += static_cast<double>(t[i] - '0') * scale;
      scale *= 0.1;
      ++i;
      any = true;
    }
  }
  if (!any) return false;
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
    ++i;
    bool exp_neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
      exp_neg = (t[i] == '-');
      ++i;
    }
    if (i >= t.size() || !is_digit(t[i])) return false;
    long e = 0;
    for (; i < t.size() && is_digit(t[i]); ++i) {
      if (e < 100000) e = e * 10 + (t[i] - '0');
    }
    double f = 1.0;
    for (long k = 0; k < e; ++k) f *= 10.0;
    value = exp_neg ? value / f : value * f;
  }
  if (i != t.size()) return false;   // 必须吃满整个 token
  out = neg ? -value : value;
  return true;
}

// bool 文本：true/false、1/0、on/off、yes/no（大小写不敏感）
inline bool parse_bool(std::string_view t, bool &out) {
  if (equals_ci(t, "true") || equals_ci(t, "on") || equals_ci(t, "yes") || t == "1") {
    out = true;
    return true;
  }
  if (equals_ci(t, "false") || equals_ci(t, "off") || equals_ci(t, "no") || t == "0") {
    out = false;
    return true;
  }
  return false;
}

// 枚举：先按 schema 的取值名（大小写敏感，与 json 一致），认不出再收数字
template <typename E>
inline bool parse_enum(std::string_view t, E &out) {
  const std::size_t n = ::eserde::field_count<E>();
  for (std::size_t i = 0; i < n; ++i) {
    if (::eserde::field_name<E>(i) == t) {
      out = static_cast<E>(::eserde::enum_value<E>(i));
      return true;
    }
  }
  unsigned long long mag = 0;
  bool neg = false;
  if (!parse_integer(t, mag, neg)) return false;
  using U = std::underlying_type_t<E>;
  if (!integer_fits<U>(mag, neg)) return false;
  out = static_cast<E>(integer_value<U>(mag, neg));
  return true;
}

// ---------------------------------------------------------------------------
// 字段标签读取（编译期可从 schema 查到 [[efmt::arg(...)]]）
// ---------------------------------------------------------------------------
template <typename T>
constexpr std::string_view tag_value(std::size_t index, std::string_view name) {
  const std::size_t n = ::eserde::tag_count<T>(index);
  for (std::size_t k = 0; k < n; ++k) {
    const auto t = ::eserde::tag<T>(index, k);
    if (t.name == name && t.has_value) return t.value;
  }
  return std::string_view{};
}

template <typename T, std::size_t I>
constexpr bool field_skipped() {
  return ::eserde::has_tag<T>(I, "skip");
}

// 成员类型：从 schema 的下标直接拿到 C++ 类型（类型名文本只是文档，不参与推导）
template <typename T, std::size_t I>
using member_t = std::remove_cv_t<std::remove_reference_t<
    decltype(::eserde::field_at<I>(std::declval<T &>()))>>;

template <typename M>
inline constexpr bool is_bool_v = std::is_same<M, bool>::value;

template <typename M>
inline constexpr bool is_char_array_v =
    std::is_array<M>::value && std::is_same<std::remove_extent_t<M>, char>::value;

// 可重复：可增长容器，但要排掉字符串（它也能 push_back(char)）与字符指针
template <typename M>
inline constexpr bool is_repeatable_v = is_growable_range<M>::value &&
                                        !is_writable_string<M>::value &&
                                        !is_char_pointer<M>::value && !is_char_array_v<M>;

// 能不能直接吃一个取值
template <typename M>
inline constexpr bool is_value_type_v =
    std::is_arithmetic<M>::value || ::eserde::detail::is_registered_enum_v<M> ||
    is_string_like_v<M> || is_char_pointer<M>::value || is_char_array_v<M>;

// 位置序号：pos = "2" → 2；裸 pos → 按声明顺序数第几个 pos 字段
constexpr std::size_t parse_position(std::string_view s) {
  if (s.empty()) return 0;
  std::size_t v = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (!is_digit(s[i])) return 0;
    v = v * 10 + static_cast<std::size_t>(s[i] - '0');
  }
  return v;
}

// [[efmt::arg(...)]] → option_view；写法不对就在这里编译报错（不猜、不静默）
template <typename T, std::size_t I>
constexpr option_view make_option() {
  using M = member_t<T, I>;
  constexpr bool skipped = field_skipped<T, I>();
  constexpr bool has_long = ::eserde::has_tag<T>(I, "long");
  constexpr bool has_short = ::eserde::has_tag<T>(I, "short");
  constexpr bool has_pos = ::eserde::has_tag<T>(I, "pos");
  constexpr std::string_view long_alias = tag_value<T>(I, "long");
  constexpr std::string_view short_alias = tag_value<T>(I, "short");
  constexpr std::string_view pos_alias = tag_value<T>(I, "pos");

  option_view v{};
  v.field = ::eserde::field_name<T>(I);
  v.help = tag_value<T>(I, "help");
  v.skip = skipped;
  v.required = ::eserde::has_tag<T>(I, "required");
  v.takes_value = !is_bool_v<M>;
  v.repeatable = is_repeatable_v<M>;
  if (skipped) return v;   // 标了 skip：不进命令行，也不做取值类型检查

  // 注意：static_assert 在实例化时就检查，拦不住上面那句运行时早返回 ——
  // 所以标了 skip 的字段要显式放过（否则一个不支持类型的"非选项字段"会被误杀）。
  static_assert(skipped || has_long || has_short || has_pos,
                "[[efmt::arg(...)]]：字段既没有 long/short 也没有 pos —— 它在命令行里没有身份。"
                "不想让它进命令行就标 [[efmt::arg(skip)]]");
  static_assert(skipped || !(has_pos && (has_long || has_short)),
                "[[efmt::arg(...)]]：pos 与 long/short 不能同时标（位置参数与命名选项二选一）");
  static_assert(skipped || !(has_pos && is_bool_v<M>),
                "[[efmt::arg(...)]]：位置参数不能是 bool —— bool 是 --flag 语义，位置参数得能收一个取值");
  static_assert(skipped || is_value_type_v<M> || is_repeatable_v<M>,
                "这个字段的类型不能做命令行取值：支持 bool / 整数 / 浮点 / 枚举 / 字符串 / "
                "string_view / 字符指针 / 字符数组 / 可重复容器；不想让它进命令行就标 [[efmt::arg(skip)]]");
  static_assert(skipped || short_alias.size() <= 1,
                "[[efmt::arg(short = \"x\")]]：short 只能给一个字符（不给取值则用字段名首字母）");
  static_assert(!has_pos || pos_alias.empty() || parse_position(pos_alias) != 0,
                "[[efmt::arg(pos = \"2\")]]：pos 的取值只能是 1 起的位置序号");

  if (has_long) v.long_name = long_alias.empty() ? v.field : long_alias;
  if (has_short) {
    v.short_name = short_alias.empty() ? v.field[0] : short_alias[0];
  }
  if (has_pos) {
    if (pos_alias.empty()) {
      // 裸 pos：按声明顺序数第几个 pos 字段（显式序号与裸序号混用时以声明顺序为准）
      std::size_t k = 1;
      for (std::size_t j = 0; j < I; ++j) {
        if (::eserde::has_tag<T>(j, "pos")) ++k;
      }
      v.position = k;
    } else {
      v.position = parse_position(pos_alias);
    }
  }
  return v;
}

template <typename T, std::size_t... I>
constexpr std::array<option_view, sizeof...(I)> make_options(std::index_sequence<I...>) {
  return {make_option<T, I>()...};
}

// 位置参数序号必须唯一，且可重复的位置参数必须是最后一个
// （否则它会把后面位置参数的值全收走）
template <typename T, std::size_t... I>
constexpr bool positions_ok(std::index_sequence<I...>) {
  const std::array<option_view, sizeof...(I)> t = make_options<T>(std::index_sequence<I...>{});
  for (std::size_t a = 0; a < t.size(); ++a) {
    if (t[a].skip || t[a].position == 0) continue;
    for (std::size_t b = a + 1; b < t.size(); ++b) {
      if (t[b].skip || t[b].position == 0) continue;
      if (t[b].position == t[a].position) return false;
    }
    if (!t[a].repeatable) continue;
    for (std::size_t b = 0; b < t.size(); ++b) {
      if (b != a && !t[b].skip && t[b].position > t[a].position) return false;
    }
  }
  return true;
}

template <typename T> struct options_holder {
  using seq = std::make_index_sequence<::eserde::field_count<T>()>;
  static constexpr std::size_t count = ::eserde::field_count<T>();
  static constexpr std::array<option_view, count> value = make_options<T>(seq{});
  static_assert(positions_ok<T>(seq{}),
                "位置参数序号有冲突：序号必须唯一，且可重复的位置参数（容器）必须是最后一个");
};

// 选项查找（线性扫 —— 参数类型字段 ≤ EFMT_DERIVE_MAX_FIELDS，够用且零额外表）
inline std::size_t find_long(const option_view *t, std::size_t n, std::string_view name) {
  for (std::size_t i = 0; i < n; ++i) {
    if (!t[i].skip && !t[i].long_name.empty() && t[i].long_name == name) return i;
  }
  return npos;
}

inline std::size_t find_short(const option_view *t, std::size_t n, char c) {
  for (std::size_t i = 0; i < n; ++i) {
    if (!t[i].skip && t[i].short_name == c) return i;
  }
  return npos;
}

inline std::size_t find_position(const option_view *t, std::size_t n, std::size_t pos) {
  for (std::size_t i = 0; i < n; ++i) {
    if (!t[i].skip && t[i].position == pos) return i;
  }
  return npos;
}

// 下一个 token 看起来像选项吗？"-" 单个与负数（-5 / -.5）不算
inline bool looks_like_option(std::string_view t) {
  if (t.size() < 2 || t[0] != '-') return false;
  if (t[1] == '-') return true;
  return !(is_digit(t[1]) || t[1] == '.');
}

// ---------------------------------------------------------------------------
// 写字段：token → 字段类型（按真实 C++ 类型分派，与 json 的取值助手同源）
// ---------------------------------------------------------------------------
// 字符串目标：先问容量再写 —— etl::string 的 assign 满了是【静默截断】，
// 所以这里必须自己挡住（不静默改用户的输入）。
template <typename S>
inline error assign_string(S &slot, std::string_view text) {
  if constexpr (has_max_size<S>::value) {
    if (text.size() > slot.max_size()) return error::value_too_long;
  }
  slot.assign(text.data(), text.size());
  return error::ok;
}

template <std::size_t N>
inline error assign_char_array(char (&out)[N], std::string_view text) {
  if (text.size() + 1 > N) return error::value_too_long;
  for (std::size_t i = 0; i < text.size(); ++i) out[i] = text[i];
  out[text.size()] = '\0';
  return error::ok;
}

// 容器元素：与 assign_field 的标量分支同一套转换
template <typename E>
inline error assign_element(E &slot, std::string_view text) {
  if constexpr (std::is_same<E, bool>::value) {
    bool b = false;
    if (!parse_bool(text, b)) return error::invalid_value;
    slot = b;
    return error::ok;
  } else if constexpr (std::is_integral<E>::value) {
    unsigned long long mag = 0;
    bool neg = false;
    if (!parse_integer(text, mag, neg) || !integer_fits<E>(mag, neg)) return error::invalid_value;
    slot = integer_value<E>(mag, neg);
    return error::ok;
  } else if constexpr (std::is_floating_point<E>::value) {
    double d = 0.0;
    if (!parse_float(text, d)) return error::invalid_value;
    slot = static_cast<E>(d);
    return error::ok;
  } else if constexpr (::eserde::detail::is_registered_enum_v<E>) {
    if (!parse_enum(text, slot)) return error::invalid_value;
    return error::ok;
  } else if constexpr (is_writable_string<E>::value) {
    return assign_string(slot, text);
  } else if constexpr (std::is_same<E, std::string_view>::value) {
    slot = text;
    return error::ok;
  } else {
    static_assert(sizeof(E) == 0,
                  "容器元素类型不能做命令行取值：支持整数 / 浮点 / bool / 枚举 / 字符串");
    return error::invalid_value;
  }
}

template <std::size_t I, typename T>
error assign_field(T &obj, std::string_view text, bool has_text, bool flag_on) {
  if constexpr (field_skipped<T, I>()) {
    (void)obj;
    (void)text;
    (void)has_text;
    (void)flag_on;
    return error::unknown_option;   // 永远不会被匹配到；这里只是跳过类型检查
  } else {
    using M = member_t<T, I>;
    auto &slot = ::eserde::field_at<I>(obj);
    if constexpr (std::is_same<M, bool>::value) {
      if (!has_text) {
        slot = flag_on;
        return error::ok;
      }
      bool b = false;
      if (!parse_bool(text, b)) return error::invalid_value;
      slot = b;
      return error::ok;
    } else if constexpr (std::is_integral<M>::value) {
      unsigned long long mag = 0;
      bool neg = false;
      if (!parse_integer(text, mag, neg) || !integer_fits<M>(mag, neg)) return error::invalid_value;
      slot = integer_value<M>(mag, neg);
      return error::ok;
    } else if constexpr (std::is_floating_point<M>::value) {
      double d = 0.0;
      if (!parse_float(text, d)) return error::invalid_value;
      slot = static_cast<M>(d);
      return error::ok;
    } else if constexpr (::eserde::detail::is_registered_enum_v<M>) {
      if (!parse_enum(text, slot)) return error::invalid_value;
      return error::ok;
    } else if constexpr (is_char_pointer<M>::value) {
      slot = text.data();   // 零拷贝：指向 argv / scratch（见 token_list 的寿命说明）
      return error::ok;
    } else if constexpr (is_char_array_v<M>) {
      return assign_char_array(slot, text);
    } else if constexpr (is_writable_string<M>::value) {
      return assign_string(slot, text);
    } else if constexpr (is_string_like_v<M>) {
      // string_view 类：零拷贝指向 token
      if constexpr (std::is_constructible<M, const char *, std::size_t>::value) {
        slot = M(text.data(), text.size());
      } else {
        slot = M(text);
      }
      return error::ok;
    } else if constexpr (is_repeatable_v<M>) {
      using E = std::remove_cv_t<std::remove_reference_t<decltype(*slot.begin())>>;
      E item{};
      const error e = assign_element(item, text);
      if (e != error::ok) return e;
      if (!push_checked(slot, static_cast<E &&>(item))) return error::too_many_values;
      return error::ok;
    } else {
      static_assert(sizeof(M) == 0,
                    "这个字段的类型不能做命令行取值（见 make_option 的同类报错）");
      return error::invalid_value;
    }
  }
}

// 按 schema 下标写第 index 个字段：编译期展开成链，运行期只走一次比较
template <std::size_t I, typename T>
error assign_rec(T &obj, std::size_t index, std::string_view text, bool has_text, bool flag_on) {
  if (index == I) return assign_field<I>(obj, text, has_text, flag_on);
  if constexpr (I + 1 < ::eserde::field_count<T>()) {
    return assign_rec<I + 1>(obj, index, text, has_text, flag_on);
  } else {
    return error::unknown_option;
  }
}

template <typename T>
error assign_at(T &obj, std::size_t index, std::string_view text, bool has_text, bool flag_on) {
  return assign_rec<0>(obj, index, text, has_text, flag_on);
}

// ---------------------------------------------------------------------------
// 主解析循环
// ---------------------------------------------------------------------------
template <typename T>
error run(const token_list &tokens, T &obj, error_info &info) {
  constexpr std::size_t n = options_holder<T>::count;
  const option_view *opts = options_holder<T>::value.data();

  unsigned long long seen = 0;
  bool no_more_options = false;
  std::size_t next_pos = 1;

  for (std::size_t i = 0; i < tokens.count; ++i) {
    const std::string_view tok = tokens.items[i];
    info.index = i;
    info.token = tok;
    info.option_index = npos;

    if (!no_more_options && tok == "--") {
      no_more_options = true;
      continue;
    }

    const bool is_long = !no_more_options && tok.size() > 2 && tok[0] == '-' && tok[1] == '-';
    const bool is_short = !no_more_options && tok.size() > 1 && tok[0] == '-' && tok[1] != '-';

    if (is_long) {
      std::string_view name = tok.substr(2);
      std::string_view value{};
      bool has_value = false;
      const std::size_t eq = name.find('=');
      if (eq != std::string_view::npos) {
        value = name.substr(eq + 1);
        has_value = true;
        name = name.substr(0, eq);
      }

      std::size_t idx = find_long(opts, n, name);
      bool flag_on = true;
      if (idx == npos && name.size() > 3 && name.compare(0, 3, "no-") == 0) {
        const std::size_t neg = find_long(opts, n, name.substr(3));
        if (neg != npos && !opts[neg].takes_value) {
          idx = neg;
          flag_on = false;
        }
      }
      if (idx == npos) {
#if ECLI_ENABLE_HELP
        if (name == "help" && find_long(opts, n, "help") == npos) return error::help_requested;
#endif
        return error::unknown_option;
      }
      info.option_index = idx;

      if (opts[idx].takes_value) {
        if (!has_value) {
          if (i + 1 >= tokens.count || looks_like_option(tokens.items[i + 1])) {
            info.token = tok;
            return error::missing_value;
          }
          ++i;
          value = tokens.items[i];
          info.index = i;
        }
        info.token = value;
        const error e = assign_at(obj, idx, value, true, true);
        if (e != error::ok) return e;
      } else {
        const error e = assign_at(obj, idx, value, has_value, flag_on);
        if (e != error::ok) return e;
      }
      seen |= (1ull << idx);
      continue;
    }

    if (is_short) {
      std::size_t k = 1;
      while (k < tok.size()) {
        const char c = tok[k];
#if ECLI_ENABLE_HELP
        if (c == 'h' && find_short(opts, n, 'h') == npos) return error::help_requested;
#endif
        const std::size_t idx = find_short(opts, n, c);
        if (idx == npos) {
          info.token = tok;
          return error::unknown_option;
        }
        info.option_index = idx;

        if (opts[idx].takes_value) {
          std::string_view rest = tok.substr(k + 1);
          const bool had_eq = !rest.empty() && rest.front() == '=';
          if (had_eq) rest = rest.substr(1);
          if (rest.empty() && !had_eq) {
            if (i + 1 >= tokens.count || looks_like_option(tokens.items[i + 1])) {
              info.token = tok;
              return error::missing_value;
            }
            ++i;
            rest = tokens.items[i];
            info.index = i;
          }
          info.token = rest;
          const error e = assign_at(obj, idx, rest, true, true);
          if (e != error::ok) return e;
          seen |= (1ull << idx);
          break;   // 取值吃掉了这个 token 的余下部分
        }

        const error e = assign_at(obj, idx, std::string_view{}, false, true);
        if (e != error::ok) {
          info.token = tok;
          return e;
        }
        seen |= (1ull << idx);
        ++k;
      }
      continue;
    }

    // 位置参数
    const std::size_t idx = find_position(opts, n, next_pos);
    if (idx == npos) return error::too_many_args;
    info.option_index = idx;
    const error e = assign_at(obj, idx, tok, true, true);
    if (e != error::ok) return e;
    seen |= (1ull << idx);
    if (!opts[idx].repeatable) ++next_pos;   // 可重复的那个把余下的位置参数全收走
  }

  // 必填检查（用位图判别"给没给"，与字段的默认值无关）
  for (std::size_t k = 0; k < n; ++k) {
    if (opts[k].skip || !opts[k].required) continue;
    if ((seen & (1ull << k)) == 0) {
      info.option_index = k;
      info.token = std::string_view{};
      return error::missing_required;
    }
  }
  return error::ok;
}

// 选项显示名："--level" / "-l" / "<input>"
inline std::string_view option_label(const option_view &v, char *tmp, std::size_t cap) {
  std::size_t w = 0;
  const auto put = [&](char c) { if (w + 1 < cap) tmp[w] = c; ++w; };
  if (!v.long_name.empty()) {
    put('-');
    put('-');
    for (std::size_t i = 0; i < v.long_name.size(); ++i) put(v.long_name[i]);
  } else if (v.short_name != 0) {
    put('-');
    put(v.short_name);
  } else {
    put('<');
    for (std::size_t i = 0; i < v.field.size(); ++i) put(v.field[i]);
    put('>');
  }
  if (cap != 0) tmp[w < cap ? w : cap - 1] = '\0';
  return std::string_view(tmp, w < cap ? w : (cap == 0 ? 0 : cap - 1));
}

// 位置参数占位符：<name> / [name] / <name>...
inline void put_positional(text_out &out, const option_view &v) {
  const char open = v.required ? '<' : '[';
  const char close = v.required ? '>' : ']';
  out.put(open);
  out.put(v.field);
  out.put(close);
  if (v.repeatable) out.put_lit("...");
}

inline void put_usage_tail(text_out &out, const option_view *opts, std::size_t n) {
  bool has_named = false;
  for (std::size_t i = 0; i < n; ++i) {
    if (!opts[i].skip && (opts[i].short_name != 0 || !opts[i].long_name.empty())) has_named = true;
  }
  if (has_named) out.put_lit(" [options]");
  std::size_t max_pos = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (!opts[i].skip && opts[i].position > max_pos) max_pos = opts[i].position;
  }
  for (std::size_t p = 1; p <= max_pos; ++p) {
    for (std::size_t i = 0; i < n; ++i) {
      if (opts[i].skip || opts[i].position != p) continue;
      out.put(' ');
      put_positional(out, opts[i]);
    }
  }
}

// 左栏的排版宽度：短选项 + 长选项 + "<value>"
inline std::size_t left_column_len(const option_view &v) {
  std::size_t w = 2;   // 前置缩进
  if (v.short_name != 0) w += 2;
  if (v.short_name != 0 && !v.long_name.empty()) w += 2;
  if (!v.long_name.empty()) w += 2 + v.long_name.size();
  const bool positional = (v.short_name == 0 && v.long_name.empty());
  if (positional) w += v.field.size() + 2 + (v.repeatable ? 3 : 0);
  if (v.takes_value && !positional) w += 8;   // " <value>"（位置参数的占位符本身就是取值）
  return w;
}

inline void put_left_column(text_out &out, const option_view &v) {
  out.put_lit("  ");
  if (v.short_name != 0) {
    out.put('-');
    out.put(v.short_name);
    if (!v.long_name.empty()) out.put_lit(", ");
  }
  if (!v.long_name.empty()) {
    out.put_lit("--");
    out.put(v.long_name);
  }
  const bool positional = (v.short_name == 0 && v.long_name.empty());
  if (positional) put_positional(out, v);   // 位置参数："<input>"
  if (v.takes_value && !positional) out.put_lit(" <value>");
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 词法
// ---------------------------------------------------------------------------
// argv → token 表（零拷贝：跳过 argv[0]，其余直接指向 argv）
inline token_list from_argv(int argc, const char *const *argv) {
  token_list out{};
  for (int i = 1; i < argc; ++i) {
    if (out.count == ECLI_MAX_TOKENS) {
      out.overflow = true;
      break;
    }
    out.items[out.count++] = std::string_view(argv[i]);
  }
  return out;
}

// 一行文本 → token 表（串口 / 蓝牙 / 键盘 / 测试都能用）
//   * 空白切分；单引号与双引号都能包住空格；反斜杠转义（\n \t \r \0，其余取字符本身）
//   * token 指向 scratch（调用方给的缓冲），所以它的寿命要盖过解析结果的使用期
inline token_list tokenize(std::string_view line, char *scratch, std::size_t cap) {
  token_list out{};
  std::size_t w = 0;
  std::size_t i = 0;
  bool room = true;
  const auto push = [&](char c) {
    if (w < cap) scratch[w++] = c;
    else room = false;
  };
  while (i < line.size()) {
    while (i < line.size() && detail::is_space(line[i])) ++i;
    if (i >= line.size()) break;
    if (out.count == ECLI_MAX_TOKENS) {
      out.overflow = true;
      return out;
    }
    const std::size_t begin = w;
    const std::size_t tok_begin = i;
    char quote = 0;
    bool transformed = false;   // 引号 / 转义动过 → 得去 scratch；原样 → 直接指原文
    while (i < line.size()) {
      const char c = line[i];
      if (c == '\\' && i + 1 < line.size()) {
        transformed = true;
        ++i;
        const char e = line[i];
        switch (e) {
          case 'n': push('\n'); break;
          case 't': push('\t'); break;
          case 'r': push('\r'); break;
          case '0': push('\0'); break;
          default: push(e); break;   // \\ \" \' 空格 等：就是它本身
        }
        ++i;
        continue;
      }
      if (quote != 0) {
        transformed = true;
        if (c == quote) {
          quote = 0;
          ++i;
          continue;
        }
        push(c);
        ++i;
        continue;
      }
      if (detail::is_space(c)) break;
      if (c == '"' || c == '\'') {
        transformed = true;
        quote = c;
        ++i;
        continue;
      }
      push(c);
      ++i;
    }
    if (quote != 0) {
      out.bad_quote = true;
      return out;
    }
    if (transformed) {
      if (!room) {
        out.overflow = true;
        return out;
      }
      out.items[out.count++] = std::string_view(scratch + begin, w - begin);
    } else {
      w = begin;   // 没用上 scratch：这个 token 直接指原文（零拷贝）
      out.items[out.count++] = line.substr(tok_begin, i - tok_begin);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// 行装配器：字节流（串口 / 蓝牙 / 键盘）→ 一行文本
// ---------------------------------------------------------------------------
// 每个输入源一个实例 —— 串口正敲着 "se"、蓝牙发来整行 "status" 时，
// 共用一块行缓冲就会串词，所以行缓冲天然是"每源一个"（几十字节）。
// 只认 \r \n 与退格；ANSI 转义序列、历史、行编辑都是终端的事，这里不管。
template <std::size_t MaxLine = ECLI_MAX_LINE> class line_reader {
 public:
  // 喂一个字节；返回 true = 攒够一行（line() 可取）。空行不算命令，直接返回 false。
  // put 返回 true 后请立刻取走 line()；再喂字节会自动开始新的一行（也可以显式 clear()）。
  bool put(char c) {
    if (ready_) {
      if (c == '\n' || c == '\r') return false;   // 上一行的收尾字节：吃掉
      clear();
    }
    if (c == '\n' || c == '\r') {
      if (c == '\n' && last_cr_) {   // \r\n 只算一次
        last_cr_ = false;
        return false;
      }
      last_cr_ = (c == '\r');
      if (len_ == 0 && !overflow_) return false;   // 空行：不产生命令
      ready_ = true;
      return true;
    }
    last_cr_ = false;
    if (c == '\b' || c == 0x7F) {   // 退格 / DEL
      if (len_ > 0) --len_;
      return false;
    }
    if (len_ + 1 < MaxLine) buf_[len_++] = c;
    else overflow_ = true;           // 超长：置位并继续吃，等到换行再报
    return false;
  }

  std::string_view line() const { return std::string_view(buf_, len_); }
  bool overflow() const { return overflow_; }
  void clear() {
    len_ = 0;
    overflow_ = false;
    ready_ = false;
    last_cr_ = false;
  }

 private:
  char buf_[MaxLine];
  std::size_t len_ = 0;
  bool overflow_ = false;
  bool ready_ = false;
  bool last_cr_ = false;
};

// ---------------------------------------------------------------------------
// 规格查询（帮助文本与自检用；全部 constexpr）
// ---------------------------------------------------------------------------
template <typename T> inline constexpr bool is_cli_args_v = ::eserde::has_cap_v<T, Cli>;

template <typename T> constexpr std::size_t option_count() {
  return detail::options_holder<T>::count;
}

template <typename T> constexpr option_view option(std::size_t index) {
  return index < detail::options_holder<T>::count ? detail::options_holder<T>::value[index]
                                                  : option_view{};
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
// 语义（与 json::read_from 一致）：在副本上解析，全部成功才赋回 —— 失败不动原对象。
// 没给的字段保持 out 原来的值（也就是结构体的默认成员初始化值）。
template <typename T>
error parse(const token_list &tokens, T &out, error_info *info = nullptr) {
  static_assert(is_cli_args_v<T>,
                "这个类型不能做命令行参数：缺 Cli 能力标签。"
                "写成 E_FMT_DERIVE(struct args { ... }, Cli) 就能解析了");
  error_info local{};
  if (tokens.overflow) {
    if (info != nullptr) *info = local;
    return error::too_many_tokens;
  }
  if (tokens.bad_quote) {
    if (info != nullptr) *info = local;
    return error::bad_quote;
  }
  T tmp = out;
  const error e = detail::run(tokens, tmp, local);
  if (info != nullptr) *info = local;
  if (e != error::ok) return e;
  out = static_cast<T &&>(tmp);
  return error::ok;
}

// 一行文本 + 调用方给的 scratch（串口 / 蓝牙 / 键盘这条路上用这个）：
//   * 没引号、没转义的 token 直接指向 line（零拷贝）——它们跟 line 一样长寿
//   * 有引号 / 转义的 token 落在 scratch 里 —— 所以 scratch 必须活得比 out 久
//   * info.token 同上：想查看出错 token，就让 scratch 活得够久（或用 argv 那版）
template <typename T>
error parse(std::string_view line, T &out, char *scratch, std::size_t cap,
            error_info *info = nullptr) {
  const token_list tokens = tokenize(line, scratch, cap);
  return parse(tokens, out, info);
}

// 便利版：自带栈缓冲（约 ECLI_MAX_TOKENS×16 + ECLI_MAX_LINE 字节栈占用）。
// 因为缓冲是局部的，这里【故意】不给 error_info —— 出错 token 出函数就悬垂了。
// 结构体里有 const char* / string_view 字段时请改用上面那版（自己管的 scratch）。
template <typename T>
error parse(std::string_view line, T &out) {
  char scratch[ECLI_MAX_LINE];
  return parse(line, out, scratch, sizeof(scratch), nullptr);
}

// argv（宿主工具的主入口）
template <typename T>
error parse(int argc, const char *const *argv, T &out, error_info *info = nullptr) {
  return parse(from_argv(argc, argv), out, info);
}

// ---------------------------------------------------------------------------
// 帮助 / 用法 / 报错（snprintf 语义；buf = nullptr 时只量长度）
// ---------------------------------------------------------------------------
template <typename T>
std::size_t write_usage(std::string_view app, char *buf, std::size_t cap) {
#if ECLI_ENABLE_HELP
  detail::text_out out{buf, cap, 0};
  out.put_lit("usage: ");
  out.put(app);
  detail::put_usage_tail(out, detail::options_holder<T>::value.data(),
                         detail::options_holder<T>::count);
  return out.finish();
#else
  (void)app;
  (void)buf;
  (void)cap;
  return 0;
#endif
}

// 帮助：about 可空；选项列的对齐宽度按最长的一项算
template <typename T>
std::size_t write_help(std::string_view app, std::string_view about, char *buf, std::size_t cap) {
#if ECLI_ENABLE_HELP
  constexpr std::size_t n = detail::options_holder<T>::count;
  const option_view *opts = detail::options_holder<T>::value.data();
  detail::text_out out{buf, cap, 0};
  if (!about.empty()) {
    out.put(about);
    out.put_lit("\n\n");
  }
  std::size_t width = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (opts[i].skip) continue;
    const std::size_t w = detail::left_column_len(opts[i]);
    if (w > width) width = w;
  }
  {
    constexpr std::size_t k_help_column = 12;   // "  -h, --help"
    if (k_help_column > width) width = k_help_column;
  }
  out.put_lit("usage: ");
  out.put(app);
  detail::put_usage_tail(out, opts, n);
  out.put_lit("\n\noptions:\n");
  for (std::size_t i = 0; i < n; ++i) {
    if (opts[i].skip) continue;
    detail::put_left_column(out, opts[i]);
    if (!opts[i].help.empty()) {
      const std::size_t used = detail::left_column_len(opts[i]);
      out.spaces(width >= used ? width - used + 2 : 2);
      out.put(opts[i].help);
    }
    out.put('\n');
  }
  out.put_lit("  -h, --help");
  out.spaces(width > 12 ? width - 12 + 2 : 2);
  out.put_lit("show this help\n");
  return out.finish();
#else
  (void)app;
  (void)about;
  (void)buf;
  (void)cap;
  return 0;
#endif
}

// 报错：错误码 + 出错位置 + usage（调用方把这段文本丢给对应的输入源即可）
template <typename T>
std::size_t write_error(std::string_view app, error e, const error_info &info, char *buf,
                        std::size_t cap) {
#if ECLI_ENABLE_HELP
  constexpr std::size_t n = detail::options_holder<T>::count;
  const option_view *opts = detail::options_holder<T>::value.data();
  char label_buf[48];
  std::string_view label{};
  if (info.option_index < n) {
    label = detail::option_label(opts[info.option_index], label_buf, sizeof(label_buf));
  }
  detail::text_out out{buf, cap, 0};
  out.put_lit("error: ");
  switch (e) {
    case error::ok:
      break;
    case error::help_requested:
      out.put_lit("help requested");
      break;
    case error::unknown_option:
      out.put_lit("unknown option '");
      out.put(info.token);
      out.put_lit("'");
      break;
    case error::missing_value:
      out.put_lit("'");
      out.put(label);
      out.put_lit("' needs a value");
      break;
    case error::invalid_value:
      out.put_lit("invalid value '");
      out.put(info.token);
      out.put_lit("' for '");
      out.put(label);
      out.put_lit("'");
      break;
    case error::value_too_long:
      out.put_lit("value '");
      out.put(info.token);
      out.put_lit("' is too long for '");
      out.put(label);
      out.put_lit("'");
      break;
    case error::missing_required:
      out.put_lit("missing required option '");
      out.put(label);
      out.put_lit("'");
      break;
    case error::too_many_args:
      out.put_lit("too many arguments: '");
      out.put(info.token);
      out.put_lit("'");
      break;
    case error::too_many_values:
      out.put_lit("too many values for '");
      out.put(label);
      out.put_lit("'");
      break;
    case error::too_many_tokens:
      out.put_lit("command line too long (ECLI_MAX_TOKENS / ECLI_MAX_LINE)");
      break;
    case error::bad_quote:
      out.put_lit("unterminated quote");
      break;
    case error::unknown_command:
      out.put_lit("unknown command '");
      out.put(info.token);
      out.put_lit("'");
      break;
  }
  out.put_lit("\n\n");
  out.put_lit("usage: ");
  out.put(app);
  detail::put_usage_tail(out, opts, n);
  return out.finish();
#else
  (void)app;
  (void)e;
  (void)info;
  (void)buf;
  (void)cap;
  return 0;
#endif
}

#if EFMT_ENABLE_DYNAMIC_STRING
// 宿主便利版：直接拿 std::string（嵌入式没有 std::string，所以按开关裁剪）
template <typename T>
std::string usage_string(std::string_view app) {
  const std::size_t need = write_usage<T>(app, nullptr, 0);
  std::string out(need + 1, '\0');
  write_usage<T>(app, &out[0], need + 1);
  out.resize(need);
  return out;
}

template <typename T>
std::string help_string(std::string_view app, std::string_view about = {}) {
  const std::size_t need = write_help<T>(app, about, nullptr, 0);
  std::string out(need + 1, '\0');
  write_help<T>(app, about, &out[0], need + 1);
  out.resize(need);
  return out;
}

template <typename T>
std::string error_string(std::string_view app, error e, const error_info &info) {
  const std::size_t need = write_error<T>(app, e, info, nullptr, 0);
  std::string out(need + 1, '\0');
  write_error<T>(app, e, info, &out[0], need + 1);
  out.resize(need);
  return out;
}
#endif

}  // namespace ecli

#endif  // ECLI_CLI_HPP
