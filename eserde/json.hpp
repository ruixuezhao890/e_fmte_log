/**
 ******************************************************************************
 * @file           : json.hpp
 * @brief          : eserde 基座上的 JSON 序列化 / 反序列化（纯 C++17，无第三方库）
 * @attention      : 依赖方向：json.hpp → serde.hpp → efmt。efmt 与 elog 都不认识它。
 *                   按嵌入式的规矩来：
 *                     * 写：write_to(buf, size, obj) 是 snprintf 语义（返回所需长度，
 *                       空间不够就截断并如实返回）；宿主的 to_string 可选
 *                     * 读：read_from(text, obj) 先解析到临时对象、成功才赋回，
 *                       失败返回错误码（不抛异常、不半途改坏已有对象）
 *                   零第三方、零异常、零动态分配（宿主 to_string 除外）。
 *                   类型识别不看具体类型，只看成员函数 —— std::string / std::vector
 *                   与 etl::string / etl::vector 同一套代码通吃。
 * @date           : 26-9-30
 ******************************************************************************
 */

#ifndef ESERDE_JSON_HPP
#define ESERDE_JSON_HPP

#include <eserde/serde.hpp>

#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

// JSON 键名 / 枚举名缓冲（反序列化时先解码到这块栈缓冲再匹配）
#ifndef ESERDE_JSON_MAX_KEY
#define ESERDE_JSON_MAX_KEY 64
#endif

// 对象 / 数组最大嵌套深度（畸形输入不会把栈吃穿）
#ifndef ESERDE_JSON_MAX_DEPTH
#define ESERDE_JSON_MAX_DEPTH 8
#endif

namespace eserde::json {

// ---------------------------------------------------------------------------
// 错误码（不抛异常）
// ---------------------------------------------------------------------------
enum class error {
  ok = 0,
  syntax,         // JSON 语法错
  type_mismatch,  // 类型对不上（往 int 里喂字符串……）
  truncated,      // 目标装不下（固定长度目标比数据短、整数溢出目标类型）
  too_deep,       // 嵌套超过 ESERDE_JSON_MAX_DEPTH
  unsupported,    // 这个类型不参与（反）序列化
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

// 反斜杠的 ASCII 码。JSON 转义要写很多反斜杠，这里统一用 kBackslash，
// 免得源码里到处是成串的转义字符（写错一个就静默改了语义）。
constexpr char kBackslash = 92;

// ---------------------------------------------------------------------------
// 类型判定：只认成员函数，std 与 ETL 通用
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

template <typename T, typename = void> struct has_max_size : std::false_type {};

template <typename T>
struct has_max_size<T, std::void_t<decltype(std::declval<T &>().max_size())>> : std::true_type {};

// char 指针（const char* / char*）：按 JSON 字符串处理
template <typename T> struct is_char_pointer : std::false_type {};

template <typename T>
struct is_char_pointer<T *>
    : std::bool_constant<std::is_same<std::remove_cv_t<T>, char>::value> {};

template <typename T>
inline constexpr bool is_object_v = ::eserde::is_registered_v<T> && !std::is_enum<T>::value;

template <typename T>
inline constexpr bool is_registered_enum_v =
    ::eserde::is_registered_v<T> && std::is_enum<T>::value;

// ---------------------------------------------------------------------------
// 写出：snprintf 语义（n = 所需总长度；超出 cap 就只计数不写）
// ---------------------------------------------------------------------------
struct writer {
  char *buf = nullptr;
  std::size_t cap = 0;
  std::size_t n = 0;

  void put(char c) {
    if (n < cap) buf[n] = c;
    ++n;
  }

  void put(std::string_view s) {
    if (n + s.size() <= cap) {
      for (std::size_t k = 0; k < s.size(); ++k) buf[n + k] = s[k];
      n += s.size();
      return;
    }
    for (char c : s) put(c);
  }

  void put_lit(const char *s) {          // 短字面量：不用 strlen，逐个吐字符
    while (*s != 0) put(*s++);
  }

  void put_hex4(unsigned v) {
    constexpr char kHex[] = "0123456789abcdef";
    put(kHex[(v >> 12) & 0xFu]);
    put(kHex[(v >> 8) & 0xFu]);
    put(kHex[(v >> 4) & 0xFu]);
    put(kHex[v & 0xFu]);
  }

  void put_unsigned(unsigned long long v) {
    char tmp[24];
    std::size_t k = 0;
    do {
      tmp[k++] = static_cast<char>('0' + static_cast<int>(v % 10));
      v /= 10;
    } while (v != 0);
    while (k > 0) put(tmp[--k]);
  }

  void put_signed(long long v) {
    if (v < 0) {
      put('-');
      put_unsigned(0ULL - static_cast<unsigned long long>(v));
    } else {
      put_unsigned(static_cast<unsigned long long>(v));
    }
  }

  // NaN / Inf：JSON 没有这两种值 → 写 null（读回来是"保持原值"）
  template <typename F> void put_real(F v) {
    if (!(v == v) || v > std::numeric_limits<F>::max() || v < -std::numeric_limits<F>::max()) {
      put_lit("null");
      return;
    }
    char tmp[32];
    const std::size_t need = ::e_fmt::format_to(tmp, sizeof(tmp), "{}", v);
    if (need >= sizeof(tmp)) {
      put_lit("null");
      return;
    }
    put(std::string_view(tmp, need));
  }

  void put_escaped(std::string_view s) {
    put('"');
    for (char c : s) {
      switch (c) {
        case '"': put(kBackslash); put('"'); break;
        case kBackslash: put(kBackslash); put(kBackslash); break;
        case '\n': put(kBackslash); put('n'); break;
        case '\r': put(kBackslash); put('r'); break;
        case '\t': put(kBackslash); put('t'); break;
        case '\b': put(kBackslash); put('b'); break;
        case '\f': put(kBackslash); put('f'); break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            put(kBackslash); put('u'); put('0'); put('0');
            put_hex4(static_cast<unsigned>(static_cast<unsigned char>(c)));
          } else {
            put(c);   // UTF-8 字节原样透传
          }
      }
    }
    put('"');
  }
};

// ---------------------------------------------------------------------------
// 读入：递归下降；字符串流式解码，不占大缓冲
// ---------------------------------------------------------------------------
struct reader {
  std::string_view s;
  std::size_t i = 0;
  int depth = 0;

  void ws() {
    while (i < s.size()) {
      const char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
      break;
    }
  }

  bool peek(char c) {
    ws();
    return i < s.size() && s[i] == c;
  }

  bool eat(char c) {
    if (!peek(c)) return false;
    ++i;
    return true;
  }

  bool word(std::string_view w) {
    ws();
    if (s.substr(i, w.size()) != w) return false;
    i += w.size();
    return true;
  }

  bool hex4(unsigned &out) {
    if (i + 4 > s.size()) return false;
    unsigned v = 0;
    for (std::size_t k = 0; k < 4; ++k) {
      const char c = s[i + k];
      unsigned d = 0;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return false;
      v = (v << 4) | d;
    }
    i += 4;
    out = v;
    return true;
  }
};

inline bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// 码点 → UTF-8
template <typename Sink> void put_utf8(unsigned cp, Sink &&sink) {
  if (cp < 0x80) {
    sink(static_cast<char>(cp));
  } else if (cp < 0x800) {
    sink(static_cast<char>(0xC0 | (cp >> 6)));
    sink(static_cast<char>(0x80 | (cp & 0x3Fu)));
  } else if (cp < 0x10000) {
    sink(static_cast<char>(0xE0 | (cp >> 12)));
    sink(static_cast<char>(0x80 | ((cp >> 6) & 0x3Fu)));
    sink(static_cast<char>(0x80 | (cp & 0x3Fu)));
  } else {
    sink(static_cast<char>(0xF0 | (cp >> 18)));
    sink(static_cast<char>(0x80 | ((cp >> 12) & 0x3Fu)));
    sink(static_cast<char>(0x80 | ((cp >> 6) & 0x3Fu)));
    sink(static_cast<char>(0x80 | (cp & 0x3Fu)));
  }
}

// 解出 JSON 字符串，逐个字符喂给 sink（转义、\uXXXX、代理对都在这里处理）
template <typename Sink> error parse_string(reader &r, Sink &&sink) {
  if (!r.eat('"')) return error::type_mismatch;
  while (r.i < r.s.size()) {
    const char c = r.s[r.i++];
    if (c == '"') return error::ok;
    if (c != kBackslash) {
      sink(c);
      continue;
    }
    if (r.i >= r.s.size()) return error::syntax;
    const char esc = r.s[r.i++];
    switch (esc) {
      case '"': sink('"'); break;
      case kBackslash: sink(kBackslash); break;
      case '/': sink('/'); break;
      case 'b': sink('\b'); break;
      case 'f': sink('\f'); break;
      case 'n': sink('\n'); break;
      case 'r': sink('\r'); break;
      case 't': sink('\t'); break;
      case 'u': {
        unsigned cp = 0;
        if (!r.hex4(cp)) return error::syntax;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {          // 高代理：试着合成一对
          unsigned low = 0;
          if (r.i + 1 < r.s.size() && r.s[r.i] == kBackslash && r.s[r.i + 1] == 'u') {
            r.i += 2;
            if (!r.hex4(low)) return error::syntax;
            if (low >= 0xDC00u && low <= 0xDFFFu) {
              cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
            } else {
              cp = 0xFFFDu;                            // 配对失败 → 替换字符
            }
          } else {
            cp = 0xFFFDu;
          }
        }
        put_utf8(cp, sink);
        break;
      }
      default: return error::syntax;
    }
  }
  return error::syntax;   // 没闭合
}

// 数字：整数部分给出"无符号幅度 mag + 符号 neg"（unsigned long long 的最大值也能过），
// 出现小数点/指数则 is_real = true 并给 dval。溢出/装不下由调用方按目标类型判。
inline error parse_number(reader &r, unsigned long long &mag, bool &neg, double &dval,
                          bool &is_real) {
  is_real = false;
  neg = false;
  mag = 0;
  dval = 0;
  r.ws();
  if (r.i < r.s.size() && r.s[r.i] == '-') { neg = true; ++r.i; }
  // 值缺失（已经到 } ] , 或结尾）→ 语法错；其它字符 → 类型对不上
  if (r.i >= r.s.size() || r.s[r.i] == '}' || r.s[r.i] == ']' || r.s[r.i] == ',') {
    return error::syntax;
  }
  if (r.s[r.i] < '0' || r.s[r.i] > '9') return error::type_mismatch;

  unsigned long long iv = 0;
  bool overflow = false;
  while (r.i < r.s.size() && r.s[r.i] >= '0' && r.s[r.i] <= '9') {
    const unsigned d = static_cast<unsigned>(r.s[r.i++] - '0');
    if (iv > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) overflow = true;
    else iv = iv * 10ULL + d;
  }

  double dv = static_cast<double>(iv);
  if (r.i < r.s.size() && r.s[r.i] == '.') {
    is_real = true;
    ++r.i;
    if (r.i >= r.s.size() || r.s[r.i] < '0' || r.s[r.i] > '9') return error::syntax;
    double scale = 0.1;
    while (r.i < r.s.size() && r.s[r.i] >= '0' && r.s[r.i] <= '9') {
      dv += static_cast<double>(r.s[r.i++] - '0') * scale;
      scale *= 0.1;
    }
  }
  if (r.i < r.s.size() && (r.s[r.i] == 'e' || r.s[r.i] == 'E')) {
    is_real = true;
    ++r.i;
    bool exp_neg = false;
    if (r.i < r.s.size() && (r.s[r.i] == '+' || r.s[r.i] == '-')) {
      exp_neg = (r.s[r.i] == '-');
      ++r.i;
    }
    if (r.i >= r.s.size() || r.s[r.i] < '0' || r.s[r.i] > '9') return error::syntax;
    long exp = 0;
    while (r.i < r.s.size() && r.s[r.i] >= '0' && r.s[r.i] <= '9') {
      if (exp < 100000) exp = exp * 10 + (r.s[r.i] - '0');
      ++r.i;
    }
    double f = 1.0;
    for (long k = 0; k < exp; ++k) f *= 10.0;
    dv = exp_neg ? dv / f : dv * f;
  }
  if (overflow) return error::truncated;
  mag = iv;
  dval = neg ? -dv : dv;
  return error::ok;
}

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

template <typename D> constexpr D integer_value(unsigned long long mag, bool neg) {
  return neg ? static_cast<D>(0ULL - mag) : static_cast<D>(mag);
}

inline bool skip_to_delimiter(reader &r) {
  const std::size_t begin = r.i;
  while (r.i < r.s.size()) {
    const char d = r.s[r.i];
    if (d == ',' || d == '}' || d == ']' || is_ws(d)) break;
    ++r.i;
  }
  return r.i != begin;
}

error skip_value(reader &r);

template <typename T> error read_value(reader &r, T &out);

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------
// 字段的 JSON 键名：默认字段名；标了 [[efmt::arg(json = "别名")]] 用别名
template <typename T> constexpr std::string_view json_key(std::size_t index) {
  const std::size_t n = ::eserde::tag_count<T>(index);
  for (std::size_t k = 0; k < n; ++k) {
    const auto t = ::eserde::tag<T>(index, k);
    if (t.name == std::string_view("json") && t.has_value) return t.value;
  }
  return ::eserde::field_name<T>(index);
}

// [[efmt::arg(json = "skip")]]：这个字段不进 JSON，也不从 JSON 读
template <typename T> constexpr bool field_skipped(std::size_t index) {
  const std::size_t n = ::eserde::tag_count<T>(index);
  for (std::size_t k = 0; k < n; ++k) {
    const auto t = ::eserde::tag<T>(index, k);
    if (t.name == std::string_view("json") && t.has_value &&
        t.value == std::string_view("skip")) {
      return true;
    }
  }
  return false;
}

template <typename T, std::size_t... I>
void write_object(writer &w, const T &obj, std::index_sequence<I...>);

template <typename E> void write_enum_value(writer &w, E v) {
  using U = std::underlying_type_t<E>;
  const long long key = static_cast<long long>(static_cast<U>(v));
  const std::size_t n = ::eserde::field_count<E>();
  for (std::size_t i = 0; i < n; ++i) {
    if (::eserde::enum_value<E>(i) == key) {
      w.put_escaped(::eserde::field_name<E>(i));
      return;
    }
  }
  w.put_signed(key);   // 没列出的取值：写底层整数（与打印行为一致）
}

template <typename T> void write_value(writer &w, const T &v) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;

  if constexpr (is_string_like_v<D>) {
    w.put_escaped(std::string_view(v.data(), v.size()));
  } else if constexpr (is_char_pointer<D>::value) {
    if (v == nullptr) w.put_lit("null");
    else w.put_escaped(std::string_view(v));
  } else if constexpr (std::is_array_v<D>) {
    using elem = std::remove_extent_t<D>;
    if constexpr (std::is_same_v<std::remove_cv_t<elem>, char>) {
      constexpr std::size_t kCount = std::extent_v<D>;
      std::size_t len = 0;
      while (len < kCount && v[len] != 0) ++len;
      w.put_escaped(std::string_view(v, len));
    } else {
      constexpr std::size_t kCount = std::extent_v<D>;
      w.put('[');
      for (std::size_t k = 0; k < kCount; ++k) {
        if (k != 0) w.put(',');
        write_value(w, v[k]);
      }
      w.put(']');
    }
  } else if constexpr (std::is_pointer_v<D>) {
    w.put_lit("null");   // 其它裸指针：JSON 里没有指针，写 null
  } else if constexpr (std::is_same_v<D, bool>) {
    w.put_lit(v ? "true" : "false");
  } else if constexpr (std::is_integral_v<D>) {
    if constexpr (std::is_signed_v<D>) w.put_signed(static_cast<long long>(v));
    else w.put_unsigned(static_cast<unsigned long long>(v));
  } else if constexpr (std::is_floating_point_v<D>) {
    w.put_real(v);
  } else if constexpr (is_registered_enum_v<D>) {
    write_enum_value(w, v);
  } else if constexpr (is_object_v<D>) {
    w.put('{');
    constexpr std::size_t kCount = ::eserde::field_count<D>();
    write_object(w, v, std::make_index_sequence<kCount>{});
    w.put('}');
  } else if constexpr (has_range<D>::value) {
    w.put('[');
    bool first = true;
    for (const auto &item : v) {
      if (!first) w.put(',');
      first = false;
      write_value(w, item);
    }
    w.put(']');
  } else {
    static_assert(sizeof(D) == 0,
                  "eserde::json 不认识这个类型：结构体请用 E_FMT_DERIVE 注册，"
                  "不想进 JSON 的字段可以标 [[efmt::arg(json = \"skip\")]]");
  }
}

template <std::size_t I, typename T> void write_one_field(writer &w, const T &obj, bool &first) {
  if (field_skipped<T>(I)) return;
  if (!first) w.put(',');
  first = false;
  w.put_escaped(json_key<T>(I));
  w.put(':');
  write_value(w, ::eserde::field_at<I>(obj));
}

template <typename T, std::size_t... I>
void write_object(writer &w, const T &obj, std::index_sequence<I...>) {
  bool first = true;
  (write_one_field<I>(w, obj, first), ...);
}

// ---------------------------------------------------------------------------
// 反序列化
// ---------------------------------------------------------------------------
inline error skip_value(reader &r) {
  if (++r.depth > ESERDE_JSON_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  r.ws();
  if (r.i >= r.s.size()) {
    --r.depth;
    return error::syntax;
  }

  error e = error::ok;
  const char c = r.s[r.i];
  if (c == '"') {
    e = parse_string(r, [](char) {});
  } else if (c == '{' || c == '[') {
    const char close = (c == '{') ? '}' : ']';
    ++r.i;
    if (r.peek(close)) {
      ++r.i;
      --r.depth;
      return error::ok;
    }
    while (true) {
      if (c == '{') {
        e = parse_string(r, [](char) {});
        if (e != error::ok) break;
        if (!r.eat(':')) {
          e = error::syntax;
          break;
        }
      }
      e = skip_value(r);
      if (e != error::ok) break;
      if (r.eat(',')) continue;
      if (r.eat(close)) break;
      e = error::syntax;
      break;
    }
  } else if (!skip_to_delimiter(r)) {
    e = error::syntax;
  }
  --r.depth;
  return e;
}

// 字符串 → 可写字符串目标（流式 append；装不下 → truncated 并清空，不留半截）
template <typename T> error read_into_string(reader &r, T &out) {
  out.clear();
  bool overflow = false;
  const error e = parse_string(r, [&](char c) {
    if (overflow) return;
    if constexpr (has_max_size<T>::value) {
      if (out.size() >= out.max_size()) {   // 满了就先别推（定长容器的溢出行为不保证）
        overflow = true;
        return;
      }
    }
    out.push_back(c);
  });
  if (e != error::ok || overflow) {
    out.clear();
    return e != error::ok ? e : error::truncated;
  }
  return error::ok;
}

template <std::size_t N> error read_into_char_array(reader &r, char (&out)[N]) {
  std::size_t k = 0;
  bool overflow = false;
  const error e = parse_string(r, [&](char c) {
    if (k + 1 < N) out[k++] = c;
    else overflow = true;
  });
  if (e != error::ok) return e;
  if (overflow) {
    out[0] = 0;
    return error::truncated;
  }
  out[k] = 0;
  return error::ok;
}

template <typename C, typename V> bool push_checked(C &c, V &&v) {
  if constexpr (has_max_size<C>::value) {
    if (c.size() >= c.max_size()) return false;
  }
  c.push_back(std::forward<V>(v));
  return true;
}

// 可增长容器：[...]
template <typename T> error read_growable_range(reader &r, T &out) {
  if (++r.depth > ESERDE_JSON_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  if (!r.eat('[')) {
    --r.depth;
    return error::type_mismatch;
  }
  out.clear();
  if (r.peek(']')) {
    ++r.i;
    --r.depth;
    return error::ok;
  }
  error e = error::ok;
  while (true) {
    typename T::value_type item{};
    e = read_value(r, item);
    if (e != error::ok) break;
    if (!push_checked(out, std::move(item))) {
      e = error::truncated;
      break;
    }
    if (r.eat(',')) continue;
    if (r.eat(']')) break;
    e = error::syntax;
    break;
  }
  --r.depth;
  return e;
}

// 固定长度序列（std::array 这类：有 size() 没有 push_back）：按位填，多的报 truncated
template <typename T> error read_fixed_range(reader &r, T &out) {
  if (++r.depth > ESERDE_JSON_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  if (!r.eat('[')) {
    --r.depth;
    return error::type_mismatch;
  }
  const std::size_t cap = static_cast<std::size_t>(out.size());
  std::size_t k = 0;
  if (r.peek(']')) {
    ++r.i;
    --r.depth;
    return error::ok;
  }
  error e = error::ok;
  while (true) {
    if (k >= cap) {
      e = error::truncated;
      break;
    }
    e = read_value(r, out[k]);
    if (e != error::ok) break;
    ++k;
    if (r.eat(',')) continue;
    if (r.eat(']')) break;
    e = error::syntax;
    break;
  }
  --r.depth;
  return e;
}

// C 数组（非 char）：[a, b, c]
template <typename E, std::size_t N> error read_c_array(reader &r, E (&out)[N]) {
  if (++r.depth > ESERDE_JSON_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  if (!r.eat('[')) {
    --r.depth;
    return error::type_mismatch;
  }
  std::size_t k = 0;
  if (r.peek(']')) {
    ++r.i;
    --r.depth;
    return error::ok;
  }
  error e = error::ok;
  while (true) {
    if (k >= N) {
      e = error::truncated;
      break;
    }
    e = read_value(r, out[k]);
    if (e != error::ok) break;
    ++k;
    if (r.eat(',')) continue;
    if (r.eat(']')) break;
    e = error::syntax;
    break;
  }
  --r.depth;
  return e;
}

template <typename E> error read_enum_value(reader &r, E &out) {
  r.ws();
  if (r.i < r.s.size() && r.s[r.i] == '"') {
    char buf[ESERDE_JSON_MAX_KEY];
    std::size_t len = 0;
    bool overflow = false;
    const error e = parse_string(r, [&](char c) {
      if (len < sizeof(buf)) buf[len++] = c;
      else overflow = true;
    });
    if (e != error::ok) return e;
    if (overflow) return error::truncated;
    const std::string_view name(buf, len);
    const std::size_t n = ::eserde::field_count<E>();
    for (std::size_t i = 0; i < n; ++i) {
      if (::eserde::field_name<E>(i) == name) {
        out = static_cast<E>(static_cast<std::underlying_type_t<E>>(::eserde::enum_value<E>(i)));
        return error::ok;
      }
    }
    return error::type_mismatch;
  }
  unsigned long long mag = 0;
  bool neg = false;
  double dv = 0;
  bool is_real = false;
  const error e = parse_number(r, mag, neg, dv, is_real);
  if (e != error::ok) return e;
  if (is_real) return error::type_mismatch;
  using U = std::underlying_type_t<E>;
  if (!integer_fits<U>(mag, neg)) return error::truncated;
  out = static_cast<E>(integer_value<U>(mag, neg));
  return error::ok;
}

// 字段名匹配：字段名本身，或 [[efmt::arg(json = "别名")]] 的别名
template <typename T, std::size_t I> bool name_matches(std::string_view key) {
  if (field_skipped<T>(I)) return false;
  const std::string_view name = ::eserde::field_name<T>(I);
  if (key == name) return true;
  const std::string_view alias = json_key<T>(I);
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
  if (++r.depth > ESERDE_JSON_MAX_DEPTH) {
    --r.depth;
    return error::too_deep;
  }
  if (!r.eat('{')) {
    --r.depth;
    return error::type_mismatch;
  }
  if (r.peek('}')) {
    ++r.i;
    --r.depth;
    return error::ok;
  }
  error e = error::ok;
  while (true) {
    char keybuf[ESERDE_JSON_MAX_KEY];
    std::size_t keylen = 0;
    bool overflow = false;
    e = parse_string(r, [&](char c) {
      if (keylen < sizeof(keybuf)) keybuf[keylen++] = c;
      else overflow = true;
    });
    if (e != error::ok) break;
    if (overflow) {
      e = error::truncated;
      break;
    }
    if (!r.eat(':')) {
      e = error::syntax;
      break;
    }
    bool handled = false;
    e = read_field(std::string_view(keybuf, keylen), r, out, handled,
                   std::make_index_sequence<::eserde::field_count<T>()>{});
    if (e != error::ok) break;
    if (!handled) {   // 不认识的键：跳过它的值（前向兼容）
      e = skip_value(r);
      if (e != error::ok) break;
    }
    if (r.eat(',')) continue;
    if (r.eat('}')) break;
    e = error::syntax;
    break;
  }
  --r.depth;
  return e;
}

template <typename T> error read_value(reader &r, T &out) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;

  r.ws();
  if (r.word("null")) {
    if constexpr (std::is_pointer_v<D>) out = nullptr;
    return error::ok;   // null：其余类型保持原值
  }

  if constexpr (is_writable_string<D>::value) {
    return read_into_string(r, out);
  } else if constexpr (is_string_like_v<D>) {
    static_assert(sizeof(D) == 0,
                  "这个类型能序列化但不能反序列化（比如 std::string_view / etl::string_view："
                  "没有可写缓冲）。要读就换成 std::string / etl::string<N>；"
                  "确实不用读，就标 [[efmt::arg(json = \"skip\")]]");
    return error::unsupported;
  } else if constexpr (is_char_pointer<D>::value) {
    static_assert(sizeof(D) == 0,
                  "const char* / char* 字段只能序列化：反序列化没有可写的存储。"
                  "要能读进来请用 std::string / etl::string<N>（或标 json = \"skip\" 跳过）");
    return error::unsupported;
  } else if constexpr (std::is_array_v<D>) {
    using elem = std::remove_extent_t<D>;
    if constexpr (std::is_same_v<std::remove_cv_t<elem>, char>) {
      return read_into_char_array(r, out);   // char[N]：当字符串读（带长度检查）
    } else {
      return read_c_array(r, out);           // 其它数组：[a, b, c]
    }
  } else if constexpr (std::is_same_v<D, bool>) {
    if (r.word("true")) {
      out = true;
      return error::ok;
    }
    if (r.word("false")) {
      out = false;
      return error::ok;
    }
    unsigned long long mag = 0;
    bool neg = false;
    double dv = 0;
    bool is_real = false;
    const error e = parse_number(r, mag, neg, dv, is_real);
    if (e != error::ok) return e;
    if (is_real || neg || mag > 1ULL) return error::type_mismatch;   // bool 只收 true/false/0/1
    out = (mag != 0);
    return error::ok;
  } else if constexpr (std::is_integral_v<D>) {
    unsigned long long mag = 0;
    bool neg = false;
    double dv = 0;
    bool is_real = false;
    const error e = parse_number(r, mag, neg, dv, is_real);
    if (e != error::ok) return e;
    if (is_real) {
      // 1.0 这种"整数值的实数"收下；1.5 直接类型错
      if (dv > 9.0e18 || dv < -9.0e18) return error::type_mismatch;
      const double rounded = static_cast<double>(static_cast<long long>(dv));
      if (dv != rounded) return error::type_mismatch;
      mag = static_cast<unsigned long long>(dv < 0 ? -dv : dv);
      neg = dv < 0;
    }
    if (!integer_fits<D>(mag, neg)) return error::truncated;
    out = integer_value<D>(mag, neg);
    return error::ok;
  } else if constexpr (std::is_floating_point_v<D>) {
    unsigned long long mag = 0;
    bool neg = false;
    double dv = 0;
    bool is_real = false;
    const error e = parse_number(r, mag, neg, dv, is_real);
    if (e != error::ok) return e;
    out = static_cast<D>(is_real ? dv
                                 : (neg ? -static_cast<double>(mag)
                                        : static_cast<double>(mag)));
    return error::ok;
  } else if constexpr (is_registered_enum_v<D>) {
    return read_enum_value(r, out);
  } else if constexpr (is_object_v<D>) {
    return read_object(r, out);
  } else if constexpr (is_growable_range<D>::value) {
    return read_growable_range(r, out);
  } else if constexpr (has_range<D>::value) {
    return read_fixed_range(r, out);
  } else {
    static_assert(sizeof(D) == 0,
                  "eserde::json 不认识这个类型（反序列化）。结构体请用 E_FMT_DERIVE 注册；"
                  "不想读的字段可以标 [[efmt::arg(json = \"skip\")]]");
    return error::unsupported;
  }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------
// 序列化：snprintf 语义 —— 返回【所需】长度；返回值 > size 表示被截断。
// buf == nullptr 或 size == 0 时只量长度不写（照着 formatted_size 的用法）。
template <typename T> std::size_t write_to(char *buf, std::size_t size, const T &value) {
  detail::writer w{buf, size, 0};
  detail::write_value(w, value);
  return w.n;
}

#if EFMT_ENABLE_DYNAMIC_STRING
// 宿主便利版（嵌入式没有 std::string，所以挂在 EFMT_ENABLE_DYNAMIC_STRING 上）
template <typename T> std::string to_string(const T &value) {
  char stack[EFMT_STRING_BUFFER_SIZE];
  const std::size_t need = write_to(stack, sizeof(stack), value);
  if (need < sizeof(stack)) return std::string(stack, need);
  std::string out;
  out.resize(need);
  write_to(&out[0], need, value);
  return out;
}
#endif

// 反序列化：在 value 的副本上解析，全部成功才赋回 ——
//   * JSON 里没写的字段 / 写成 null 的字段，保持 value 原来的值（配置合并语义）
//   * 中途失败不会动 value 一根毫毛（不做"改一半"的破坏性写入）
// 要求 T 可拷贝构造（嵌入式结构体都是）。返回错误码，不抛异常。
template <typename T> error read_from(std::string_view text, T &value) {
  detail::reader r{text, 0, 0};
  T tmp = value;
  const error e = detail::read_value(r, tmp);
  if (e != error::ok) return e;
  r.ws();
  if (r.i != text.size()) return error::syntax;   // 后面还有垃圾
  value = std::move(tmp);
  return error::ok;
}

}  // namespace eserde::json

#endif  // ESERDE_JSON_HPP
