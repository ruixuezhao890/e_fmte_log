/**
 ******************************************************************************
 * @file           : cbor.hpp
 * @brief          : eserde 基座上的 CBOR（RFC 8949）子集：二进制序列化 / 反序列化
 * @attention      : 依赖方向：cbor.hpp → traits.hpp → serde.hpp → efmt。efmt 与 elog 都不认识它。
 *                   写出来的字节是合法 CBOR —— Python cbor2 / Node cbor-x / 任何语言的
 *                   解码器都能直接读（这就是选 CBOR 而不是自研格式的理由）。
 *                   子集边界（写端只写这些，读端只收这些 + 无损处的宽容）：
 *                     * 整数 major 0/1：写最短编码，读收 1/2/4/8 字节的全部合法长度
 *                     * 文本串 major 3（UTF-8）；map 的键也是文本串
 *                     * 数组 / map major 4/5：只写定长（definite）—— 要能问出元素个数
 *                     * 浮点 0xFA(float) / 0xFB(double)；读端额外收 0xF9(half)
 *                     * bool 0xF4/0xF5、null 0xF6
 *                   不实现：tag(major 6)、bignum、不定长(ai=31)、undefined、
 *                   map 键排序（RFC 8949 §4.2 确定性编码）→ 输出合法但非 canonical。
 *                   枚举写底层整数（JSON 侧写的是取值名：二进制要的是字节数），读端两种都收。
 *                   零第三方、零异常、零动态分配。
 * @date           : 26-10-02
 ******************************************************************************
 */

#ifndef ESERDE_CBOR_HPP
#define ESERDE_CBOR_HPP

#include <eserde/traits.hpp>

#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

// 对象 / 数组最大嵌套深度（畸形输入不会把栈吃穿）
#ifndef ESERDE_CBOR_MAX_DEPTH
#define ESERDE_CBOR_MAX_DEPTH 8
#endif

namespace eserde::cbor {

// ---------------------------------------------------------------------------
// 错误码（不抛异常）。名字与 json 侧一一对应，方便上层写通用的错误处理。
// ---------------------------------------------------------------------------
enum class error {
  ok = 0,
  syntax,         // CBOR 坏了（头不完整、长度越过缓冲区、28..30 保留位……）
  type_mismatch,  // 类型对不上（往 int 里喂文本串……）
  truncated,      // 目标装不下（定长目标比数据短、整数溢出目标类型）
  too_deep,       // 嵌套超过 ESERDE_CBOR_MAX_DEPTH
  unsupported,    // 子集之外（tag / 不定长）或这个类型不参与（反）序列化
};

constexpr const char *error_name(error e) {
  switch (e) {
    case error::ok: return "ok";
    case error::syntax: return "syntax";
    case error::type_mismatch: return "type_mismatch";
    case error::truncated: return "truncated";
    case error::too_deep: return "too_deep";
    case error::unsupported: return "unsupported";
  }
  return "?";
}

namespace detail {

// 形状判定 / 取值搬运助手：与 json.hpp 共用，定义在 traits.hpp
using ::eserde::detail::has_max_size;
using ::eserde::detail::has_range;
using ::eserde::detail::has_size;
using ::eserde::detail::integer_fits;
using ::eserde::detail::integer_value;
using ::eserde::detail::is_char_pointer;
using ::eserde::detail::is_growable_range;
using ::eserde::detail::is_object_v;
using ::eserde::detail::is_registered_enum_v;
using ::eserde::detail::is_string_like_v;
using ::eserde::detail::is_writable_string;
using ::eserde::detail::push_checked;

// 本格式的名字：字段标签用它取键名（[[efmt::arg(cbor = "别名")]]）
constexpr std::string_view kFormat = "cbor";

// 头字节：高 3 位是 major type
constexpr unsigned kUint = 0;     // 无符号整数
constexpr unsigned kNegInt = 1;   // 负整数（值 = -1 - 参数）
constexpr unsigned kBytes = 2;    // 字节串（本子集不写，读端只跳过）
constexpr unsigned kText = 3;     // 文本串（UTF-8）
constexpr unsigned kArray = 4;
constexpr unsigned kMap = 5;
constexpr unsigned kTag = 6;      // 语义标签：子集之外
constexpr unsigned kSimple = 7;   // 简单值 / 浮点

// major 7 的 additional info
constexpr unsigned kAiFalse = 20;
constexpr unsigned kAiTrue = 21;
constexpr unsigned kAiNull = 22;
constexpr unsigned kAiHalf = 25;
constexpr unsigned kAiFloat = 26;
constexpr unsigned kAiDouble = 27;
constexpr unsigned kAiIndefinite = 31;

// ---------------------------------------------------------------------------
// 写出：snprintf 语义（n = 所需总字节数；超出 cap 就只计数不写）
// ---------------------------------------------------------------------------
struct writer {
  unsigned char *buf = nullptr;
  std::size_t cap = 0;
  std::size_t n = 0;

  void byte(unsigned v) {
    if (n < cap) buf[n] = static_cast<unsigned char>(v);
    ++n;
  }

  // 大端写 k 个字节
  void be(unsigned long long v, int k) {
    for (int j = k - 1; j >= 0; --j) byte(static_cast<unsigned>((v >> (8 * j)) & 0xFFu));
  }

  // major type + 参数：按最短长度编码（RFC 8949 的首选序列化）
  void head(unsigned major, unsigned long long v) {
    const unsigned m = major << 5;
    if (v < 24ULL) { byte(m | static_cast<unsigned>(v)); return; }
    if (v <= 0xFFULL) { byte(m | 24u); be(v, 1); return; }
    if (v <= 0xFFFFULL) { byte(m | 25u); be(v, 2); return; }
    if (v <= 0xFFFFFFFFULL) { byte(m | 26u); be(v, 4); return; }
    byte(m | 27u);
    be(v, 8);
  }

  // 有符号整数：负数走 major 1 的 -1-n（v + 1 不会溢出，所以 INT64_MIN 也安全）
  void put_signed(long long v) {
    if (v >= 0) head(kUint, static_cast<unsigned long long>(v));
    else head(kNegInt, static_cast<unsigned long long>(-(v + 1)));
  }

  void put_unsigned(unsigned long long v) { head(kUint, v); }

  void put_bool(bool v) { byte(v ? 0xF5u : 0xF4u); }

  void put_null() { byte(0xF6u); }

  // 浮点按 C++ 类型定宽写（float → 0xFA，double → 0xFB）；NaN / Inf 原样传，不像 JSON 退化成 null
  void put_float(float v) {
    unsigned bits = 0;
    std::memcpy(&bits, &v, 4);
    byte(0xFAu);
    be(bits, 4);
  }

  void put_double(double v) {
    unsigned long long bits = 0;
    std::memcpy(&bits, &v, 8);
    byte(0xFBu);
    be(bits, 8);
  }

  void put_text_head(std::size_t len) { head(kText, len); }

  void put_raw(const char *p, std::size_t len) {
    for (std::size_t k = 0; k < len; ++k) byte(static_cast<unsigned>(static_cast<unsigned char>(p[k])));
  }
};

// ---------------------------------------------------------------------------
// 读入：按头递归下降；文本串直接指向输入缓冲（不拷贝、不需要键缓冲）
// ---------------------------------------------------------------------------
struct reader {
  const unsigned char *p = nullptr;
  std::size_t n = 0;
  std::size_t i = 0;
  int depth = 0;

  bool byte(unsigned &out) {
    if (i >= n) return false;
    out = p[i++];
    return true;
  }

  bool be(int k, unsigned long long &out) {
    if (i + static_cast<std::size_t>(k) > n) return false;
    unsigned long long v = 0;
    for (int j = 0; j < k; ++j) v = (v << 8) | p[i + static_cast<std::size_t>(j)];
    i += static_cast<std::size_t>(k);
    out = v;
    return true;
  }

  // 读一个数据项的头：major + additional info + 参数。
  // major 7 时参数就是浮点的原始位模式（half/float/double 各自 2/4/8 字节）。
  error head(unsigned &major, unsigned &ai, unsigned long long &arg) {
    arg = 0;
    unsigned ib = 0;
    if (!byte(ib)) return error::syntax;   // 数据提前结束
    major = ib >> 5;
    ai = ib & 0x1Fu;
    if (ai < 24u) { arg = ai; return error::ok; }
    switch (ai) {
      case 24u: return be(1, arg) ? error::ok : error::syntax;
      case 25u: return be(2, arg) ? error::ok : error::syntax;
      case 26u: return be(4, arg) ? error::ok : error::syntax;
      case 27u: return be(8, arg) ? error::ok : error::syntax;
      case kAiIndefinite: return error::unsupported;   // 不定长：子集之外
      default: return error::syntax;                   // 28..30 保留
    }
  }
};

// 半个精度浮点 → float（读端宽容：别人可能写 0xF9）
inline float half_to_float(unsigned h) {
  const unsigned sign = (h & 0x8000u) << 16;
  const unsigned exp = (h >> 10) & 0x1Fu;
  const unsigned man = h & 0x3FFu;
  unsigned bits = 0;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;                                   // ±0
    } else {
      unsigned m = man;
      int k = 0;
      while ((m & 0x400u) == 0) { m <<= 1; ++k; }    // 规格化：值 = (m/2^10) * 2^(-14-k)
      bits = sign | (static_cast<unsigned>(113 - k) << 23) | ((m & 0x3FFu) << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);         // Inf / NaN
  } else {
    bits = sign | ((exp + (127u - 15u)) << 23) | (man << 13);
  }
  float f = 0;
  std::memcpy(&f, &bits, 4);
  return f;
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------
template <typename T> void write_value(writer &w, const T &v);

// 枚举：写底层整数（JSON 侧写取值名；读端两种都收）
template <typename E> void write_enum_value(writer &w, E v) {
  using U = std::underlying_type_t<E>;
  w.put_signed(static_cast<long long>(static_cast<U>(v)));
}

// 可见字段数：跳过的字段不占 map 的条目数（定长头必须先算出来）
template <typename T, std::size_t... I>
constexpr std::size_t visible_fields(std::index_sequence<I...>) {
  return (std::size_t{0} + ... + (::eserde::field_skipped<T>(I, kFormat) ? 0u : 1u));
}

template <std::size_t I, typename T> void write_one_field(writer &w, const T &obj) {
  if (::eserde::field_skipped<T>(I, kFormat)) return;
  const std::string_view key = ::eserde::field_key<T>(I, kFormat);
  w.put_text_head(key.size());
  w.put_raw(key.data(), key.size());
  write_value(w, ::eserde::field_at<I>(obj));
}

template <typename T, std::size_t... I>
void write_fields(writer &w, const T &obj, std::index_sequence<I...>) {
  (write_one_field<I>(w, obj), ...);
}

template <typename T> void write_value(writer &w, const T &v) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;

  if constexpr (is_string_like_v<D>) {
    w.put_text_head(v.size());
    w.put_raw(v.data(), v.size());
  } else if constexpr (is_char_pointer<D>::value) {
    if (v == nullptr) {
      w.put_null();
    } else {
      const std::string_view text(v);
      w.put_text_head(text.size());
      w.put_raw(text.data(), text.size());
    }
  } else if constexpr (std::is_array_v<D>) {
    using elem = std::remove_extent_t<D>;
    constexpr std::size_t kCount = std::extent_v<D>;
    if constexpr (std::is_same_v<std::remove_cv_t<elem>, char>) {
      std::size_t len = 0;
      while (len < kCount && v[len] != 0) ++len;
      w.put_text_head(len);
      w.put_raw(v, len);
    } else {
      w.head(kArray, kCount);
      for (std::size_t k = 0; k < kCount; ++k) write_value(w, v[k]);
    }
  } else if constexpr (std::is_pointer_v<D>) {
    w.put_null();   // 其它裸指针：CBOR 里没有指针
  } else if constexpr (std::is_same_v<D, bool>) {
    w.put_bool(v);
  } else if constexpr (std::is_integral_v<D>) {
    if constexpr (std::is_signed_v<D>) w.put_signed(static_cast<long long>(v));
    else w.put_unsigned(static_cast<unsigned long long>(v));
  } else if constexpr (std::is_floating_point_v<D>) {
    if constexpr (sizeof(D) <= 4) w.put_float(static_cast<float>(v));
    else w.put_double(static_cast<double>(v));
  } else if constexpr (is_registered_enum_v<D>) {
    write_enum_value(w, v);
  } else if constexpr (is_object_v<D>) {
    static_assert(::eserde::has_cap_v<D, ::eserde::Serialize>,
                  "这个类型不能序列化：请在声明处写 E_FMT_DERIVE(struct X { ... }, Debug, "
                  "Serialize)。不想进 CBOR 的字段可以标 [[efmt::arg(cbor = \"skip\")]]");
    constexpr std::size_t kCount = ::eserde::field_count<D>();
    w.head(kMap, visible_fields<D>(std::make_index_sequence<kCount>{}));
    write_fields(w, v, std::make_index_sequence<kCount>{});
  } else if constexpr (has_range<D>::value) {
    static_assert(has_size<D>::value,
                  "CBOR 的数组头要写死元素个数：请用 std::vector / std::array / etl::vector "
                  "这类有 size() 的容器；std::forward_list 这种问不出长度的不在子集里");
    w.head(kArray, static_cast<unsigned long long>(v.size()));
    for (const auto &item : v) write_value(w, item);
  } else {
    static_assert(sizeof(D) == 0,
                  "eserde::cbor 不认识这个类型：结构体请用 E_FMT_DERIVE 注册，"
                  "不想进 CBOR 的字段可以标 [[efmt::arg(cbor = \"skip\")]]");
  }
}

// ---------------------------------------------------------------------------
// 反序列化
// ---------------------------------------------------------------------------
template <typename T> error read_value(reader &r, T &out);

// 跳过一整个数据项（不认识的键：前向兼容）
inline error skip_value(reader &r) {
  if (++r.depth > ESERDE_CBOR_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long arg = 0;
  error e = r.head(major, ai, arg);
  if (e == error::ok) {
    switch (major) {
      case kBytes:
      case kText:
        if (arg > r.n - r.i) e = error::syntax;   // 长度越过缓冲区
        else r.i += static_cast<std::size_t>(arg);
        break;
      case kArray:
        for (unsigned long long k = 0; k < arg && e == error::ok; ++k) e = skip_value(r);
        break;
      case kMap:
        for (unsigned long long k = 0; k < arg && e == error::ok; ++k) {
          e = skip_value(r);                          // 键
          if (e == error::ok) e = skip_value(r);      // 值
        }
        break;
      case kTag:
        e = error::unsupported;                       // 语义标签：子集之外
        break;
      default:
        break;                                        // 整数 / 浮点 / 简单值：头已读完
    }
  }
  --r.depth;
  return e;
}

// 文本串 → 可写字符串（装不下 → truncated 并清空，不留半截）
template <typename T> error read_into_string(reader &r, T &out) {
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long len = 0;
  const error e = r.head(major, ai, len);
  if (e != error::ok) return e;
  if (major != kText) return error::type_mismatch;
  if (len > r.n - r.i) return error::syntax;
  out.clear();
  bool overflow = false;
  for (unsigned long long k = 0; k < len; ++k) {
    if constexpr (has_max_size<T>::value) {
      if (out.size() >= out.max_size()) {   // 满了就先别推（定长容器的溢出行为不保证）
        overflow = true;
        break;
      }
    }
    out.push_back(static_cast<char>(r.p[r.i + k]));
  }
  if (overflow) {
    out.clear();
    return error::truncated;
  }
  r.i += static_cast<std::size_t>(len);
  return error::ok;
}

template <std::size_t N> error read_into_char_array(reader &r, char (&out)[N]) {
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long len = 0;
  const error e = r.head(major, ai, len);
  if (e != error::ok) return e;
  if (major != kText) return error::type_mismatch;
  if (len > r.n - r.i) return error::syntax;
  if (len + 1ULL > static_cast<unsigned long long>(N)) {
    out[0] = 0;
    return error::truncated;
  }
  for (unsigned long long k = 0; k < len; ++k) out[k] = static_cast<char>(r.p[r.i + k]);
  out[len] = 0;
  r.i += static_cast<std::size_t>(len);
  return error::ok;
}

// 整数（major 0/1）→ 幅度 + 符号
inline error read_integer(reader &r, unsigned long long &mag, bool &neg) {
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long arg = 0;
  const error e = r.head(major, ai, arg);
  if (e != error::ok) return e;
  if (major == kUint) {
    mag = arg;
    neg = false;
    return error::ok;
  }
  if (major == kNegInt) {
    if (arg == 0xFFFFFFFFFFFFFFFFULL) return error::truncated;   // -2^64：没有目标类型装得下
    mag = arg + 1ULL;                                           // 值 = -1 - arg
    neg = true;
    return error::ok;
  }
  return error::type_mismatch;
}

template <typename D> error read_integer_into(reader &r, D &out) {
  unsigned long long mag = 0;
  bool neg = false;
  const error e = read_integer(r, mag, neg);
  if (e != error::ok) return e;
  if (!integer_fits<D>(mag, neg)) return error::truncated;
  out = integer_value<D>(mag, neg);
  return error::ok;
}

// 浮点：收 0xF9/0xFA/0xFB 与整数（整数→浮点是无损方向，收；浮点→整数不收，
// CBOR 的整数与浮点是两种类型，不做 JSON 那种 1.0 → int 的宽容）
template <typename D> error read_real(reader &r, D &out) {
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long arg = 0;
  const error e = r.head(major, ai, arg);
  if (e != error::ok) return e;
  if (major == kUint) {
    out = static_cast<D>(arg);
    return error::ok;
  }
  if (major == kNegInt) {
    if (arg == 0xFFFFFFFFFFFFFFFFULL) return error::truncated;
    out = static_cast<D>(-1.0 - static_cast<double>(arg));
    return error::ok;
  }
  if (major != kSimple) return error::type_mismatch;
  if (ai == kAiHalf) {
    out = static_cast<D>(half_to_float(static_cast<unsigned>(arg)));
    return error::ok;
  }
  if (ai == kAiFloat) {
    const unsigned bits = static_cast<unsigned>(arg);
    float f = 0;
    std::memcpy(&f, &bits, 4);
    out = static_cast<D>(f);
    return error::ok;
  }
  if (ai == kAiDouble) {
    double d = 0;
    std::memcpy(&d, &arg, 8);
    out = static_cast<D>(d);
    return error::ok;
  }
  return error::type_mismatch;
}

template <typename D> error read_bool(reader &r, D &out) {
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long arg = 0;
  const error e = r.head(major, ai, arg);
  if (e != error::ok) return e;
  if (major == kSimple && ai == kAiFalse) {
    out = false;
    return error::ok;
  }
  if (major == kSimple && ai == kAiTrue) {
    out = true;
    return error::ok;
  }
  if (major == kUint && arg <= 1ULL) {   // 宽容：0 / 1 也算（与 JSON 侧只收 true/false/0/1 对齐）
    out = (arg != 0);
    return error::ok;
  }
  return error::type_mismatch;
}

// 枚举：整数（本子集写的形态）或取值名字符串（别的编码器写的形态）都收
template <typename E> error read_enum_value(reader &r, E &out) {
  if (r.i < r.n && (r.p[r.i] >> 5) == kText) {
    unsigned major = 0;
    unsigned ai = 0;
    unsigned long long len = 0;
    const error e = r.head(major, ai, len);
    if (e != error::ok) return e;
    if (len > r.n - r.i) return error::syntax;
    const std::string_view name(reinterpret_cast<const char *>(r.p + r.i),
                                static_cast<std::size_t>(len));
    r.i += static_cast<std::size_t>(len);
    const std::size_t count = ::eserde::field_count<E>();
    for (std::size_t k = 0; k < count; ++k) {
      if (::eserde::field_name<E>(k) == name) {
        out = static_cast<E>(static_cast<std::underlying_type_t<E>>(::eserde::enum_value<E>(k)));
        return error::ok;
      }
    }
    return error::type_mismatch;
  }
  unsigned long long mag = 0;
  bool neg = false;
  const error e = read_integer(r, mag, neg);
  if (e != error::ok) return e;
  using U = std::underlying_type_t<E>;
  if (!integer_fits<U>(mag, neg)) return error::truncated;
  out = static_cast<E>(integer_value<U>(mag, neg));
  return error::ok;
}

template <typename T> error read_growable_range(reader &r, T &out) {
  if (++r.depth > ESERDE_CBOR_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long count = 0;
  error e = r.head(major, ai, count);
  if (e != error::ok) {
    --r.depth;
    return e;
  }
  if (major != kArray) {
    --r.depth;
    return error::type_mismatch;
  }
  if (count > r.n - r.i) {   // 每个元素至少 1 字节
    --r.depth;
    return error::syntax;
  }
  out.clear();
  for (unsigned long long k = 0; k < count; ++k) {
    typename T::value_type item{};
    e = read_value(r, item);
    if (e != error::ok) break;
    if (!push_checked(out, std::move(item))) {
      e = error::truncated;
      break;
    }
  }
  --r.depth;
  return e;
}

// 固定长度序列（std::array 这类：有 size() 没有 push_back）
template <typename T> error read_fixed_range(reader &r, T &out) {
  if (++r.depth > ESERDE_CBOR_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long count = 0;
  error e = r.head(major, ai, count);
  if (e != error::ok) {
    --r.depth;
    return e;
  }
  if (major != kArray) {
    --r.depth;
    return error::type_mismatch;
  }
  if (count > r.n - r.i) {
    --r.depth;
    return error::syntax;
  }
  if (count > static_cast<unsigned long long>(out.size())) {
    --r.depth;
    return error::truncated;   // 元素个数写在头里，装不下当场就能判
  }
  for (unsigned long long k = 0; k < count; ++k) {
    e = read_value(r, out[static_cast<std::size_t>(k)]);
    if (e != error::ok) break;
  }
  --r.depth;
  return e;
}

// C 数组（非 char）
template <typename E, std::size_t N> error read_c_array(reader &r, E (&out)[N]) {
  if (++r.depth > ESERDE_CBOR_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long count = 0;
  error e = r.head(major, ai, count);
  if (e != error::ok) {
    --r.depth;
    return e;
  }
  if (major != kArray) {
    --r.depth;
    return error::type_mismatch;
  }
  if (count > r.n - r.i || count > static_cast<unsigned long long>(N)) {
    --r.depth;
    return error::truncated;
  }
  for (unsigned long long k = 0; k < count; ++k) {
    e = read_value(r, out[static_cast<std::size_t>(k)]);
    if (e != error::ok) break;
  }
  --r.depth;
  return e;
}

// 字段名匹配：字段名本身，或 [[efmt::arg(cbor = "别名")]] 的别名
template <typename T, std::size_t I> bool name_matches(std::string_view key) {
  if (::eserde::field_skipped<T>(I, kFormat)) return false;
  const std::string_view name = ::eserde::field_name<T>(I);
  if (key == name) return true;
  const std::string_view alias = ::eserde::field_key<T>(I, kFormat);
  return alias != name && key == alias;
}

template <typename T, std::size_t I>
bool try_field(std::string_view key, reader &r, T &out, bool &handled, error &result) {
  if (!name_matches<T, I>(key)) return false;
  handled = true;
  result = read_value(r, ::eserde::field_at<I>(out));
  return true;   // 命中即短路
}

template <typename T, std::size_t... I>
error read_field(std::string_view key, reader &r, T &out, bool &handled,
                 std::index_sequence<I...>) {
  error result = error::ok;
  (void)((try_field<T, I>(key, r, out, handled, result) || ...));
  return result;
}

template <typename T> error read_object(reader &r, T &out) {
  if (++r.depth > ESERDE_CBOR_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  unsigned major = 0;
  unsigned ai = 0;
  unsigned long long count = 0;
  error e = r.head(major, ai, count);
  if (e != error::ok) {
    --r.depth;
    return e;
  }
  if (major != kMap) {
    --r.depth;
    return error::type_mismatch;
  }
  for (unsigned long long k = 0; k < count; ++k) {
    unsigned key_major = 0;
    unsigned key_ai = 0;
    unsigned long long key_len = 0;
    e = r.head(key_major, key_ai, key_len);
    if (e != error::ok) break;
    if (key_major != kText) {
      e = error::type_mismatch;
      break;
    }
    if (key_len > r.n - r.i) {
      e = error::syntax;
      break;
    }
    // 键直接指向输入缓冲：不拷贝、没有长度上限
    const std::string_view key(reinterpret_cast<const char *>(r.p + r.i),
                               static_cast<std::size_t>(key_len));
    r.i += static_cast<std::size_t>(key_len);
    bool handled = false;
    e = read_field(key, r, out, handled, std::make_index_sequence<::eserde::field_count<T>()>{});
    if (e != error::ok) break;
    if (!handled) {   // 不认识的键：跳过它的值（前向兼容）
      e = skip_value(r);
      if (e != error::ok) break;
    }
  }
  --r.depth;
  return e;
}

template <typename T> error read_value(reader &r, T &out) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;

  if (r.i < r.n && r.p[r.i] == 0xF6u) {   // null：保持原值（配置合并语义）
    ++r.i;
    if constexpr (std::is_pointer_v<D>) out = nullptr;
    return error::ok;
  }

  if constexpr (is_writable_string<D>::value) {
    return read_into_string(r, out);
  } else if constexpr (is_string_like_v<D>) {
    static_assert(sizeof(D) == 0,
                  "这个类型能序列化但不能反序列化（比如 std::string_view / etl::string_view："
                  "没有可写缓冲）。要读就换成 std::string / etl::string<N>；"
                  "确实不用读，就标 [[efmt::arg(cbor = \"skip\")]]");
    return error::unsupported;
  } else if constexpr (is_char_pointer<D>::value) {
    static_assert(sizeof(D) == 0,
                  "const char* / char* 字段只能序列化：反序列化没有可写的存储。"
                  "要能读进来请用 std::string / etl::string<N>（或标 cbor = \"skip\" 跳过）");
    return error::unsupported;
  } else if constexpr (std::is_array_v<D>) {
    using elem = std::remove_extent_t<D>;
    if constexpr (std::is_same_v<std::remove_cv_t<elem>, char>) {
      return read_into_char_array(r, out);   // char[N]：当文本串读（带长度检查）
    } else {
      return read_c_array(r, out);           // 其它 C 数组：定长数组头
    }
  } else if constexpr (std::is_same_v<D, bool>) {
    return read_bool(r, out);
  } else if constexpr (std::is_integral_v<D>) {
    return read_integer_into(r, out);
  } else if constexpr (std::is_floating_point_v<D>) {
    return read_real(r, out);
  } else if constexpr (is_registered_enum_v<D>) {
    return read_enum_value(r, out);
  } else if constexpr (is_object_v<D>) {
    static_assert(::eserde::has_cap_v<D, ::eserde::Deserialize>,
                  "这个类型不能反序列化：请在声明处写 E_FMT_DERIVE(struct X { ... }, Debug, "
                  "Deserialize)。不想读的字段可以标 [[efmt::arg(cbor = \"skip\")]]");
    return read_object(r, out);
  } else if constexpr (is_growable_range<D>::value) {
    return read_growable_range(r, out);
  } else if constexpr (has_range<D>::value) {
    return read_fixed_range(r, out);
  } else {
    static_assert(sizeof(D) == 0,
                  "eserde::cbor 不认识这个类型（反序列化）。结构体请用 E_FMT_DERIVE 注册；"
                  "不想读的字段可以标 [[efmt::arg(cbor = \"skip\")]]");
    return error::unsupported;
  }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------
// 序列化：snprintf 语义 —— 返回【所需】字节数；返回值 > size 表示被截断。
// buf == nullptr 或 size == 0 时只量长度不写。注意：截断后的字节流不完整，别拿去解码。
template <typename T> std::size_t write_to(unsigned char *buf, std::size_t size, const T &value) {
  detail::writer w{buf, size, 0};
  detail::write_value(w, value);
  return w.n;
}

// 反序列化：在 value 的副本上解析，全部成功才赋回 ——
//   * CBOR 里没写的字段 / 写成 null 的字段，保持 value 原来的值（配置合并语义）
//   * 中途失败不会动 value 一根毫毛（不做"改一半"的破坏性写入）
// 缓冲里多出没读完的字节 = syntax（和 JSON 侧一样，不接受尾巴上的垃圾）。
template <typename T> error read_from(const unsigned char *data, std::size_t size, T &value) {
  detail::reader r{data, size, 0, 0};
  T tmp = value;
  const error e = detail::read_value(r, tmp);
  if (e != error::ok) return e;
  if (r.i != size) return error::syntax;
  value = std::move(tmp);
  return error::ok;
}

}  // namespace eserde::cbor

#endif  // ESERDE_CBOR_HPP
