/**
 ******************************************************************************
 * @file           : formatter.hpp
 * @author         : ruixuezhao
 * @brief          : Formatters for built-in types
 * @attention      : Do not include format_traits.hpp to avoid circular dependency
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMATTER_HPP
#define FORMATTER_HPP

#include "format_traits.hpp"
#include "format_range.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <middleware/efmt/core/format_args.hpp>
#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <type_traits>

namespace e_fmt::detail {

// Concrete formatter implementations for built-in and custom-dispatch types.
// formatter<T> decides "how to render", while format_context decides "where
// and how safely to write".

// ============================================================================
// 整数格式化
// ============================================================================
// One implementation covers every integer type: the caller supplies the
// magnitude plus the sign, so 64-bit unsigned values keep their full range.
class integral_formatter {
public:
  static void format_signed(format_context &ctx, const format_specs &specs,
                            int64_t value) {
    const bool negative = value < 0;
    const uint64_t magnitude =
        negative ? (0ULL - static_cast<uint64_t>(value))
                 : static_cast<uint64_t>(value);
    format_magnitude(ctx, specs, magnitude, negative);
  }

  static void format_unsigned(format_context &ctx, const format_specs &specs,
                              uint64_t value) {
    format_magnitude(ctx, specs, value, false);
  }

private:
  static void format_magnitude(format_context &ctx, const format_specs &specs,
                               uint64_t magnitude, bool negative) {
    // '{:c}'：按字符输出（例如把 65 打印成 'A'）
    if (specs.type == presentation::chr) {
      const char c = static_cast<char>(magnitude);
      ctx.write_aligned(std::string_view(&c, 1), specs);
      return;
    }

    // Binary is the widest representation a 64-bit magnitude can need.
    char digits[64];
    char *const end = digits + sizeof(digits);

    unsigned digit_count;
    switch (specs.type) {
    case presentation::hex_lower:
      digit_count = to_digits<16, false>(end, magnitude);
      break;
    case presentation::hex_upper:
      digit_count = to_digits<16, true>(end, magnitude);
      break;
    case presentation::oct:
      digit_count = to_digits<8, false>(end, magnitude);
      break;
    case presentation::bin:
    case presentation::bin_upper:
      digit_count = to_digits<2, false>(end, magnitude);
      break;
    default:  // dec or none
      digit_count = to_digits<10, false>(end, magnitude);
      break;
    }

    char prefix[4];
    size_t prefix_len = 0;
    if (negative) {
      prefix[prefix_len++] = '-';
    } else if (specs.sign_mode == sign::plus) {
      prefix[prefix_len++] = '+';
    } else if (specs.sign_mode == sign::space) {
      prefix[prefix_len++] = ' ';
    }

    // 替代形式前缀（#）
    if (specs.alt && magnitude != 0) {
      switch (specs.type) {
      case presentation::hex_lower:
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = 'x';
        break;
      case presentation::hex_upper:
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = 'X';
        break;
      case presentation::oct:
        prefix[prefix_len++] = '0';
        break;
      case presentation::bin:
      case presentation::bin_upper:
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = 'b';
        break;
      default:
        break;
      }
    }

    // 精度 = 最少位数
    size_t zeros = 0;
    if (specs.precision > 0 &&
        static_cast<size_t>(specs.precision) > digit_count) {
      zeros = static_cast<size_t>(specs.precision) - digit_count;
    }

    ctx.write_number(prefix, prefix_len, zeros, end - digit_count, digit_count,
                     specs);
  }

  // 从缓冲末尾向前写数字，返回位数（避免先逆序再翻转）
  template <unsigned Base, bool Upper>
  static unsigned to_digits(char *end, uint64_t value) {
    const char *alphabet = Upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char *p = end;
    do {
      *--p = alphabet[value % Base];
      value /= Base;
    } while (value != 0);
    return static_cast<unsigned>(end - p);
  }
};

// ============================================================================
// 浮点格式化
// ============================================================================
class float_formatter {
public:
  static void format(format_context &ctx, const format_specs &specs,
                     double value) {
    // 使用更大的缓冲区来处理可能的符号前缀
    char buffer[128];

    // 确定格式说明符
    const char *fmt_spec = get_format_spec(specs);

    // 处理特殊值
    if (std::isnan(value)) {
      ctx.write_aligned(is_upper(specs) ? "NAN" : "nan", specs);
      return;
    }

    if (std::isinf(value)) {
      const char *inf_str = is_upper(specs) ? "INF" : "inf";
      const size_t inf_len = 3;
      char text[4];
      size_t prefix_len = 0;
      if (value < 0) {
        text[prefix_len++] = '-';
      }
      std::memcpy(text + prefix_len, inf_str, inf_len);
      ctx.write_aligned(std::string_view(text, prefix_len + inf_len), specs);
      return;
    }

    // 使用 snprintf 格式化
    // snprintf keeps the floating-point formatting rules compact and familiar.
    const int precision = (specs.precision >= 0) ? specs.precision : 6;
    const int written = std::snprintf(buffer, sizeof(buffer), fmt_spec, precision, value);
    if (written <= 0) {
      return;
    }

    // snprintf 返回"本应写入"的长度，超长时按实际内容截断
    size_t len = static_cast<size_t>(written);
    if (len >= sizeof(buffer)) {
      len = sizeof(buffer) - 1;
    }

    // 处理正数的符号模式
    size_t prefix_len = 0;
    if (len > 0 && buffer[0] == '-') {
      prefix_len = 1;
    } else if (value >= 0 && specs.sign_mode == sign::plus) {
      std::memmove(buffer + 1, buffer, len);
      buffer[0] = '+';
      ++len;
      prefix_len = 1;
    } else if (value >= 0 && specs.sign_mode == sign::space) {
      std::memmove(buffer + 1, buffer, len);
      buffer[0] = ' ';
      ++len;
      prefix_len = 1;
    }

    ctx.write_number(buffer, prefix_len, 0, buffer + prefix_len,
                     len - prefix_len, specs);
  }

private:
  static bool is_upper(const format_specs &specs) {
    return specs.type == presentation::exp_upper ||
           specs.type == presentation::hex_upper ||
           specs.type == presentation::general_upper ||
           specs.type == presentation::fixed_upper;
  }

  static const char *get_format_spec(const format_specs &specs) {
    switch (specs.type) {
    case presentation::exp_lower:
      return "%.*e";
    case presentation::exp_upper:
      return "%.*E";
    case presentation::fixed_lower:
    case presentation::hex_lower:
      return "%.*f";
    case presentation::fixed_upper:
    case presentation::hex_upper:
      return "%.*F";
    case presentation::general_upper:
      return "%.*G";
    case presentation::general_lower:
    default:
      return "%.*g";
    }
  }
};

// ============================================================================
// 字符串格式化
// ============================================================================
class string_formatter {
public:
  static void format(format_context &ctx, const format_specs &specs,
                     std::string_view sv) {
    // 应用精度
    if (specs.precision >= 0) {
      const size_t max_len = static_cast<size_t>(specs.precision);
      if (sv.size() > max_len) {
        sv = sv.substr(0, max_len);
      }
    }

    // 应用对齐
    ctx.write_aligned(sv, specs);
  }
};

// ============================================================================
// 字符格式化
// ============================================================================
class char_formatter {
public:
  static void format(format_context &ctx, const format_specs &specs, char c) {
    ctx.write_aligned(std::string_view(&c, 1), specs);
  }
};

// ============================================================================
// 布尔格式化
// ============================================================================
class bool_formatter {
public:
  static void format(format_context &ctx, const format_specs &specs, bool value) {
    if (specs.type == presentation::str) {
      ctx.write_aligned(value ? "true" : "false", specs);
    } else {
      integral_formatter::format_signed(ctx, specs, value ? 1 : 0);
    }
  }
};

// ============================================================================
// 指针格式化
// ============================================================================
class pointer_formatter {
public:
  static void format(format_context &ctx, const format_specs &specs,
                     const void *ptr) {
    if (ptr == nullptr) {
      ctx.write_aligned("(nil)", specs);
      return;
    }

    char buffer[20];
    buffer[0] = '0';
    buffer[1] = 'x';

    const char *hex_digits = "0123456789abcdef";
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    int pos = 2;
    bool leading = true;
    for (int shift = (sizeof(uintptr_t) * 8) - 4; shift >= 0; shift -= 4) {
      int digit = (addr >> shift) & 0xF;
      if (digit != 0 || !leading) {
        buffer[pos++] = hex_digits[digit];
        leading = false;
      }
    }

    if (pos == 2) {
      buffer[pos++] = '0';
    }

    ctx.write_aligned(std::string_view(buffer, static_cast<size_t>(pos)), specs);
  }
};

// ============================================================================
// 格式化函数包装器 - 用于 format_args
// ============================================================================
// Bridge functions from the erased format_arg payload back to concrete
// formatter implementations.
inline void format_int64_wrapper(format_context &ctx, const format_specs &specs,
                                 const format_arg &arg) {
  integral_formatter::format_signed(ctx, specs, arg.value.integral);
}

inline void format_uint64_wrapper(format_context &ctx, const format_specs &specs,
                                  const format_arg &arg) {
  integral_formatter::format_unsigned(ctx, specs, arg.value.unsigned_integral);
}

inline void format_float_wrapper(format_context &ctx, const format_specs &specs,
                                 const format_arg &arg) {
  float_formatter::format(ctx, specs, arg.value.floating);
}

inline void format_bool_wrapper(format_context &ctx, const format_specs &specs,
                                const format_arg &arg) {
  bool_formatter::format(ctx, specs, arg.value.boolean);
}

inline void format_string_wrapper(format_context &ctx, const format_specs &specs,
                                  const format_arg &arg) {
  string_formatter::format(
      ctx, specs,
      std::string_view(arg.value.string, static_cast<size_t>(arg.string_size)));
}

inline void format_char_wrapper(format_context &ctx, const format_specs &specs,
                                const format_arg &arg) {
  char_formatter::format(ctx, specs, arg.value.char_value);
}

inline void format_pointer_wrapper(format_context &ctx, const format_specs &specs,
                                   const format_arg &arg) {
  pointer_formatter::format(ctx, specs, arg.value.pointer);
}

// ============================================================================
// 为各种类型设置 format_arg
// ============================================================================
// Front door for the variadic public API: pack original arguments into the
// runtime format_arg representation.
inline format_arg make_format_arg(int value) {
  return format_arg::make_integral(static_cast<int64_t>(value), format_int64_wrapper);
}

inline format_arg make_format_arg(unsigned int value) {
  return format_arg::make_uintegral(static_cast<uint64_t>(value), format_uint64_wrapper);
}

inline format_arg make_format_arg(long value) {
  return format_arg::make_integral(static_cast<int64_t>(value), format_int64_wrapper);
}

inline format_arg make_format_arg(unsigned long value) {
  return format_arg::make_uintegral(static_cast<uint64_t>(value), format_uint64_wrapper);
}

inline format_arg make_format_arg(long long value) {
  return format_arg::make_integral(static_cast<int64_t>(value), format_int64_wrapper);
}

inline format_arg make_format_arg(unsigned long long value) {
  return format_arg::make_uintegral(static_cast<uint64_t>(value), format_uint64_wrapper);
}

// 其余整型（short / signed char / unsigned char / wchar_t ...）统一走整型通道，
// 避免它们被隐式转换成 char 之类的意外类型。
template <typename T,
          typename = std::enable_if_t<std::is_integral_v<T> &&
                                      !std::is_same_v<T, char> &&
                                      !std::is_same_v<T, bool>>>
inline format_arg make_format_arg(T value) {
  if constexpr (std::is_signed_v<T>) {
    return format_arg::make_integral(static_cast<int64_t>(value), format_int64_wrapper);
  } else {
    return format_arg::make_uintegral(static_cast<uint64_t>(value), format_uint64_wrapper);
  }
}

inline format_arg make_format_arg(double value) {
  return format_arg::make_floating(value, format_float_wrapper);
}

inline format_arg make_format_arg(float value) {
  return format_arg::make_floating(static_cast<double>(value), format_float_wrapper);
}

inline format_arg make_format_arg(bool value) {
  return format_arg::make_boolean(value, format_bool_wrapper);
}

inline format_arg make_format_arg(const char *value) {
  return value ? format_arg::make_string(value, std::strlen(value), format_string_wrapper)
               : format_arg::make_string("(null)", 6, format_string_wrapper);
}

inline format_arg make_format_arg(char *value) {
  return make_format_arg(static_cast<const char *>(value));
}

template <size_t N>
inline format_arg make_format_arg(const char (&value)[N]) {
  return format_arg::make_string(value, N - 1, format_string_wrapper);
}

template <size_t N>
inline format_arg make_format_arg(char (&value)[N]) {
  return format_arg::make_string(value, N - 1, format_string_wrapper);
}

#if EFMT_ENABLE_DYNAMIC_STRING
inline format_arg make_format_arg(const std::string &value) {
  return format_arg::make_string(value.data(), value.size(), format_string_wrapper);
}
#endif

inline format_arg make_format_arg(std::string_view value) {
  return format_arg::make_string(value.data(), value.size(), format_string_wrapper);
}

inline format_arg make_format_arg(char value) {
  return format_arg::make_char(value, format_char_wrapper);
}

inline format_arg make_format_arg(const void *value) {
  return format_arg::make_pointer(value, format_pointer_wrapper);
}

template <typename T>
inline format_arg make_format_arg(T *value) {
  return format_arg::make_pointer(static_cast<const void *>(value), format_pointer_wrapper);
}

// ============================================================================
// formatter specializations for built-in types
// These provide direct formatters for built-in types (avoiding default_formatter)
// ============================================================================

// 整数类型
template <>
struct formatter<int> {
  static void format(format_context &ctx, const format_specs &specs, int value) {
    integral_formatter::format_signed(ctx, specs, static_cast<int64_t>(value));
  }
};

template <>
struct formatter<unsigned int> {
  static void format(format_context &ctx, const format_specs &specs, unsigned int value) {
    integral_formatter::format_unsigned(ctx, specs, static_cast<uint64_t>(value));
  }
};

template <>
struct formatter<long> {
  static void format(format_context &ctx, const format_specs &specs, long value) {
    integral_formatter::format_signed(ctx, specs, static_cast<int64_t>(value));
  }
};

template <>
struct formatter<unsigned long> {
  static void format(format_context &ctx, const format_specs &specs, unsigned long value) {
    integral_formatter::format_unsigned(ctx, specs, static_cast<uint64_t>(value));
  }
};

template <>
struct formatter<long long> {
  static void format(format_context &ctx, const format_specs &specs, long long value) {
    integral_formatter::format_signed(ctx, specs, static_cast<int64_t>(value));
  }
};

template <>
struct formatter<unsigned long long> {
  static void format(format_context &ctx, const format_specs &specs, unsigned long long value) {
    integral_formatter::format_unsigned(ctx, specs, static_cast<uint64_t>(value));
  }
};

// 浮点类型
template <>
struct formatter<float> {
  static void format(format_context &ctx, const format_specs &specs, float value) {
    float_formatter::format(ctx, specs, static_cast<double>(value));
  }
};

template <>
struct formatter<double> {
  static void format(format_context &ctx, const format_specs &specs, double value) {
    float_formatter::format(ctx, specs, value);
  }
};

// 字符串类型
template <>
struct formatter<const char*> {
  static void format(format_context &ctx, const format_specs &specs, const char* value) {
    string_formatter::format(ctx, specs, value ? std::string_view(value) : std::string_view("(null)"));
  }
};

#if EFMT_ENABLE_DYNAMIC_STRING
template <>
struct formatter<std::string> {
  static void format(format_context &ctx, const format_specs &specs, const std::string &value) {
    string_formatter::format(ctx, specs, value);
  }
};
#endif

template <>
struct formatter<std::string_view> {
  static void format(format_context &ctx, const format_specs &specs, std::string_view value) {
    string_formatter::format(ctx, specs, value);
  }
};

// 字符类型
template <>
struct formatter<char> {
  static void format(format_context &ctx, const format_specs &specs, char value) {
    char_formatter::format(ctx, specs, value);
  }
};

// 布尔类型
template <>
struct formatter<bool> {
  static void format(format_context &ctx, const format_specs &specs, bool value) {
    bool_formatter::format(ctx, specs, value);
  }
};

// ============================================================================
// 自定义类型支持
// ============================================================================
// 自定义类型的格式化函数
template <typename T>
void format_custom_wrapper(format_context &ctx, const format_specs &specs,
                           const format_arg &arg) {
  formatter<T>::format(ctx, specs, *static_cast<const T *>(arg.value.pointer));
}

// 为自定义类型创建 format_arg
// Custom types are stored as pointers to the caller's object: the object lives
// until the end of the whole format() expression, so no copy (and no side
// storage) is needed, and nested format() calls cannot clobber each other.
template <typename T>
inline std::enable_if_t<
    !std::is_arithmetic_v<std::decay_t<T>> &&
    !std::is_same_v<std::decay_t<T>, bool> &&
    !std::is_same_v<std::decay_t<T>, const char*> &&
    !std::is_same_v<std::decay_t<T>, char> &&
    !std::is_pointer_v<std::remove_reference_t<T>>,
    format_arg>
make_format_arg(T &&value) {
  using value_type = std::decay_t<T>;
  static_assert(has_formatter_v<value_type>
#if EFMT_ENABLE_STREAM_FALLBACK
                || has_stream_formatter_v<value_type>,
#else
                ,
#endif
                "Type T does not have a formatter defined. "
                "Use E_FMT_FORMATTER or E_FMT_FORMATTER_FN to define one, "
                "or define operator<< for the type.");

  format_arg arg;
  arg.type = arg_type::pointer;
  arg.value.pointer = static_cast<const void *>(&value);
  arg.formatter = format_custom_wrapper<value_type>;
  return arg;
}

} // namespace e_fmt::detail

#endif // FORMATTER_HPP
