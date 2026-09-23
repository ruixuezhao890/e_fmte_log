/**
 ******************************************************************************
 * @file           : format.hpp
 * @author         : ruixuezhao
 * @brief          : Main entry point for format library
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_HPP
#define FORMAT_HPP

#include <middleware/efmt/core/format_args.hpp>
#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <middleware/efmt/core/format_parser.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <middleware/efmt/core/format_string.hpp>
#include <middleware/efmt/core/format_style.hpp>
#include <middleware/efmt/core/format_traits.hpp>
#include <middleware/efmt/core/formatter.hpp>

#include <cstring>
#include <string_view>

#if EFMT_ENABLE_STREAM_API
#include <iostream>
#endif

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

namespace e_fmt {

namespace detail {
struct text_style;
}

#if EFMT_ENABLE_STREAM_API
inline void apply_style(std::ostream &os, const detail::text_style &style);
inline void reset_style(std::ostream &os);
#endif

// These declarations must precede the styled-output implementations below.
// The implementations are templates, so unqualified lookup only considers
// functions visible at their definition point.
template <typename... Args>
size_t format_to(char *buffer, size_t size, std::string_view fmt_str,
                 Args &&...args);

// 已打包参数的非模板入口：编译期快路径（format_checked_to）在它定义之前
// 展开，模板体里的非依赖调用需要这条前置声明才能做普通查找。
inline size_t format_to(char *buffer, size_t size, std::string_view fmt_str,
                        const detail::format_args &args);

#if EFMT_ENABLE_DYNAMIC_STRING
template <typename... Args>
std::string format(std::string_view fmt_str, Args &&...args);

template <typename... Args>
void format_to(std::string &out, std::string_view fmt_str, Args &&...args);
#endif

namespace detail {

// 编译期校验：E_FMT_STR / E_FMT_DECLARE_STR 生成的格式串必须与实参个数一致
template <typename Holder, typename... Args>
constexpr void check_format_string() {
  static_assert(count_format_args(Holder::data()) == sizeof...(Args),
                "Number of arguments does not match format string");
}

// ============================================================================
// 格式化执行器
// ============================================================================
// Walks the format string exactly once: literal runs are handed to the context
// as they are found and every replacement field is parsed and formatted on the
// spot. There is no intermediate representation, no component limit and no
// error channel - a malformed format string comes out verbatim and a missing
// argument shows up as "{?}".
class format_executor {
public:
  format_executor(std::string_view fmt_str, const format_args &args)
      : fmt_str_(fmt_str), args_(args) {}

  // 写入缓冲区，返回完整输出所需的长度（snprintf 语义：返回值 >= size 表示被
  // 截断，实际写入 min(返回值, size-1) 个字符）。buffer 为空时只统计长度。
  size_t execute(char *buffer, size_t size) {
    format_context ctx(buffer, size);
    if (!run(ctx)) {
      // 格式串本身有语法错误：原样输出，至少不丢信息
      ctx.reset();
      ctx.write_str(fmt_str_);
    }
    return ctx.pos();
  }

private:
  std::string_view fmt_str_;
  const format_args &args_;

  // 单遍扫描；返回 false 表示括号结构不合法
  bool run(format_context &ctx) {
    if (fmt_str_.empty()) {
      return true;
    }

    const char *p = fmt_str_.data();
    const char *const end = p + fmt_str_.size();
    const char *text = p;
    size_t next_arg_id = 0;

    while (p < end) {
      if (*p == '{') {
        if (p + 1 < end && p[1] == '{') {  // "{{" -> '{'
          ctx.write_chars(text, static_cast<size_t>(p - text));
          ctx.write_char('{');
          p += 2;
          text = p;
          continue;
        }
        ctx.write_chars(text, static_cast<size_t>(p - text));
        if (!format_field(ctx, p, end, next_arg_id)) {
          return false;
        }
        text = p;
        continue;
      }

      if (*p == '}') {
        if (p + 1 < end && p[1] == '}') {  // "}}" -> '}'
          ctx.write_chars(text, static_cast<size_t>(p - text));
          ctx.write_char('}');
          p += 2;
          text = p;
          continue;
        }
        return false;  // 单独的 '}'
      }

      ++p;
    }

    ctx.write_chars(text, static_cast<size_t>(p - text));
    return true;
  }

  // p 指向 '{'，成功后越过对应的 '}'
  bool format_field(format_context &ctx, const char *&p, const char *end,
                    size_t &next_arg_id) {
    const char *cursor = p + 1;
    size_t arg_id = next_arg_id;
    if (cursor < end && is_digit(*cursor)) {
      arg_id = parse_index(cursor, end);
    } else {
      ++next_arg_id;
    }

    format_specs specs;
    if (cursor < end && *cursor == ':') {
      const char *const spec_begin = ++cursor;
      while (cursor < end && *cursor != '}') {
        ++cursor;
      }
      if (cursor >= end) {
        return false;
      }
      specs = parse_format_spec(spec_begin, cursor);
    }

    if (cursor >= end || *cursor != '}') {
      return false;
    }
    p = cursor + 1;

    const format_arg &arg = args_.get(arg_id);
    if (arg.type == arg_type::none) {
      // 实参少于占位符：留下可见记号，不中断其余输出
      ctx.write_str("{?}");
      return true;
    }

    if (arg.formatter) {
      arg.formatter(ctx, specs, arg);
    }
    return true;
  }
};

// ============================================================================
// 参数打包
// ============================================================================
template <typename... Args>
inline format_args pack_format_args(Args &&...args) {
  static_assert(sizeof...(Args) <= max_format_args,
                "Too many format arguments (see max_format_args)");
  format_args packed;
  size_t index = 0;
  (packed.set(index++, make_format_arg(static_cast<Args &&>(args))), ...);
  return packed;
}

// ============================================================================
// 编译期格式串快路径执行器
// ============================================================================
// 所有 E_FMT_STR 调用共用这一份非模板代码：字段位置/规范已在编译期算好，
// 运行期只做 字面量写入 + 参数分发，不再扫描括号、不再解析格式规范。
// 返回值与 snprintf 语义一致（同运行期 format_to：完整长度 + NUL 终止）。
inline size_t execute_checked_fields(char *buffer, size_t size, const char *fmt,
                                     const checked_field *fields, size_t field_count,
                                     size_t trailing, const format_args &args) {
  format_context ctx(buffer, size);
  const char *p = fmt;
  for (size_t i = 0; i < field_count; ++i) {
    ctx.write_chars(p, fields[i].literal_len);
    p += fields[i].literal_len;
    const format_arg &arg = args.get(fields[i].arg_id);
    if (arg.type == arg_type::none) {
      ctx.write_str("{?}");
    } else if (arg.formatter) {
      arg.formatter(ctx, fields[i].specs, arg);
    }
    p += fields[i].field_span;
  }
  ctx.write_chars(p, trailing);
  const size_t needed = ctx.pos();
  if (size > 0) {
    buffer[(needed < size) ? needed : size - 1] = '\0';
  }
  return needed;
}

// checked 格式串入口：plan 有效走快路径，否则退回运行期路径（含转义展开）
template <typename Plan>
size_t format_checked_to(char *buffer, size_t size, const char *fmt, const Plan &plan,
                         const format_args &packed) {
  if (!plan.valid) {
    return format_to(buffer, size, std::string_view(fmt, plan.text_len), packed);
  }
  return execute_checked_fields(buffer, size, fmt, plan.fields, plan.field_count,
                                plan.trailing, packed);
}

// ============================================================================
// 动态字符串输出
// ============================================================================
// Almost every format() result fits in a small stack buffer, so the common case
// is written once and copied into the string; only longer output re-runs into a
// correctly sized string (the first pass already returned the exact length).
// 只有 std::string 路径需要它，嵌入式（EFMT_ENABLE_DYNAMIC_STRING=0）整块裁掉，
// 连带不再需要 <string> 的完整定义。
#if EFMT_ENABLE_DYNAMIC_STRING
template <typename Executor>
std::string format_string(Executor &executor) {
  char local[EFMT_STRING_BUFFER_SIZE];
  const size_t needed = executor.execute(local, sizeof(local));
  if (needed < sizeof(local)) {
    return std::string(local, needed);
  }

  std::string out;
  out.resize(needed);
  executor.execute(out.data(), out.size());
  return out;
}

// checked 格式串返回 std::string 的助手（与 format_string 同款两遍策略）
template <typename Plan>
std::string format_checked_string(const char *fmt, const Plan &plan,
                                  const format_args &packed) {
  char local[EFMT_STRING_BUFFER_SIZE];
  const size_t needed = format_checked_to(local, sizeof(local), fmt, plan, packed);
  if (needed < sizeof(local)) {
    return std::string(local, needed);
  }

  std::string out;
  out.resize(needed);
  format_checked_to(out.data(), out.size(), fmt, plan, packed);
  return out;
}
#endif  // EFMT_ENABLE_DYNAMIC_STRING

// 样式输出与打印实现
inline void apply_style_to_buffer(char *buffer, size_t &size,
                                  const text_style &style) {
  size = style_builder::build(style, buffer, 64);
}

template <typename... Args>
void print_styled_impl_no_stream(const text_style &style, std::string_view fmt_str,
                                 Args &&...args) {
  char format_buffer[EFMT_PRINT_BUFFER_SIZE];
  const size_t needed = format_to(format_buffer, sizeof(format_buffer), fmt_str,
                                  static_cast<Args &&>(args)...);

#if !EFMT_ENABLE_ANSI_STYLES
  (void)style;  // 关闭 ANSI 时样式参数不参与任何输出
#else
  // 关闭 ANSI 时整块样式缓冲都不存在（嵌入式少 64 B 栈）
  char style_buffer[64];
  size_t style_len = 0;
  if (!style.is_empty()) {
    apply_style_to_buffer(style_buffer, style_len, style);
    if (style_len > 0) {
      internal_write(style_buffer, style_len);
    }
  }
#endif

  internal_write(format_buffer,
                 (needed < sizeof(format_buffer)) ? needed
                                                  : sizeof(format_buffer) - 1);

#if EFMT_ENABLE_ANSI_STYLES
  if (style_len > 0) {
    internal_write(style_builder::reset(), style_builder::reset_length());
  }
#endif
}

template <typename... Args>
void println_styled_impl_no_stream(const text_style &style, std::string_view fmt_str,
                                   Args &&...args) {
  print_styled_impl_no_stream(style, fmt_str, static_cast<Args &&>(args)...);
  internal_write("\n", 1);
}

#if EFMT_ENABLE_STREAM_API
template <typename... Args>
void print_styled_impl(std::ostream &os, const text_style &style,
                       std::string_view fmt_str, Args &&...args) {
  apply_style(os, style);

#if EFMT_ENABLE_DYNAMIC_STRING
  std::string result = format(fmt_str, static_cast<Args &&>(args)...);
  os << result;
#else
  char buffer[EFMT_PRINT_BUFFER_SIZE];
  format_to(buffer, sizeof(buffer), fmt_str, static_cast<Args &&>(args)...);
  os << buffer;
#endif

  reset_style(os);
}

template <typename... Args>
void println_styled_impl(std::ostream &os, const text_style &style,
                         std::string_view fmt_str, Args &&...args) {
  print_styled_impl(os, style, fmt_str, static_cast<Args &&>(args)...);
  os << '\n';
}
#endif

inline text_style with_color(color c) { return text_style(c); }

inline text_style with_colors(color fg, bg_color bg) {
  return text_style(fg, bg);
}

inline text_style with_style(style s) {
  text_style ts;
  ts.set_style(s);
  return ts;
}

inline text_style operator|(const text_style &lhs, style rhs) {
  text_style result = lhs;
  result.set_style(rhs);
  return result;
}

inline text_style operator|(color lhs, style rhs) {
  text_style result(lhs);
  result.set_style(rhs);
  return result;
}

} // namespace detail

// ============================================================================
// 核心接口（已打包参数）
// ============================================================================
inline size_t format_to(char *buffer, size_t size, std::string_view fmt_str,
                        const detail::format_args &args) {
  detail::format_executor executor(fmt_str, args);
  const size_t needed = executor.execute(buffer, size);

  if (size > 0) {
    buffer[(needed < size) ? needed : size - 1] = '\0';
  }

  return needed;
}

#if EFMT_ENABLE_DYNAMIC_STRING
inline void format_to(std::string &out, std::string_view fmt_str,
                      const detail::format_args &args) {
  detail::format_executor executor(fmt_str, args);
  out = detail::format_string(executor);
}

inline std::string format(std::string_view fmt_str,
                          const detail::format_args &args) {
  detail::format_executor executor(fmt_str, args);
  return detail::format_string(executor);
}
#endif

inline size_t formatted_size(std::string_view fmt_str,
                             const detail::format_args &args) {
  detail::format_executor executor(fmt_str, args);
  return executor.execute(nullptr, 0);
}

// ============================================================================
// 核心接口（可变参数）
// ============================================================================
// One overload per entry point accepts everything string-like: const char*,
// char arrays, std::string and std::string_view.
#if EFMT_ENABLE_DYNAMIC_STRING
template <typename... Args>
std::string format(std::string_view fmt_str, Args &&...args) {
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return format(fmt_str, static_cast<const detail::format_args &>(packed));
}
#endif

template <typename... Args>
size_t format_to(char *buffer, size_t size, std::string_view fmt_str,
                 Args &&...args) {
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return format_to(buffer, size, fmt_str,
                   static_cast<const detail::format_args &>(packed));
}

#if EFMT_ENABLE_DYNAMIC_STRING
template <typename... Args>
void format_to(std::string &out, std::string_view fmt_str, Args &&...args) {
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  format_to(out, fmt_str, static_cast<const detail::format_args &>(packed));
}
#endif

template <typename... Args>
size_t formatted_size(std::string_view fmt_str, Args &&...args) {
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return formatted_size(fmt_str, static_cast<const detail::format_args &>(packed));
}

// ============================================================================
// 编译期校验接口（E_FMT_STR / E_FMT_DECLARE_STR）
// ============================================================================
// 除了运行参数个数检查，还走编译期预解析快路径：plan.valid 时运行期不再
// 扫描括号/解析格式规范；含转义（{{ / }}）的串自动退回运行期路径，语义一致。
#if EFMT_ENABLE_DYNAMIC_STRING
template <typename Holder, typename... Args,
          typename = std::enable_if_t<detail::is_checked_format_string_v<Holder>>>
std::string format(Holder, Args &&...args) {
  detail::check_format_string<Holder, Args...>();
  static constexpr auto plan = Holder::plan();
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return detail::format_checked_string(Holder::data(), plan, packed);
}
#endif

template <typename Holder, typename... Args,
          typename = std::enable_if_t<detail::is_checked_format_string_v<Holder>>>
size_t format_to(char *buffer, size_t size, Holder, Args &&...args) {
  detail::check_format_string<Holder, Args...>();
  static constexpr auto plan = Holder::plan();
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return detail::format_checked_to(buffer, size, Holder::data(), plan, packed);
}

#if EFMT_ENABLE_DYNAMIC_STRING
template <typename Holder, typename... Args,
          typename = std::enable_if_t<detail::is_checked_format_string_v<Holder>>>
void format_to(std::string &out, Holder, Args &&...args) {
  detail::check_format_string<Holder, Args...>();
  static constexpr auto plan = Holder::plan();
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  out = detail::format_checked_string(Holder::data(), plan, packed);
}
#endif

template <typename Holder, typename... Args,
          typename = std::enable_if_t<detail::is_checked_format_string_v<Holder>>>
size_t formatted_size(Holder, Args &&...args) {
  detail::check_format_string<Holder, Args...>();
  static constexpr auto plan = Holder::plan();
  auto packed = detail::pack_format_args(static_cast<Args &&>(args)...);
  return detail::format_checked_to(nullptr, 0, Holder::data(), plan, packed);
}

// ============================================================================
// 打印接口
// ============================================================================
template <typename... Args>
void print_styled(const detail::text_style &style, std::string_view fmt_str,
                  Args &&...args) {
  detail::print_styled_impl_no_stream(style, fmt_str,
                                      static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_styled(const detail::text_style &style, std::string_view fmt_str,
                    Args &&...args) {
  detail::println_styled_impl_no_stream(style, fmt_str,
                                        static_cast<Args &&>(args)...);
}

#if EFMT_ENABLE_STREAM_API
inline void apply_style(std::ostream &os, const detail::text_style &style) {
  if (style.is_empty()) {
    return;
  }

  char buffer[64];
  size_t len = detail::style_builder::build(style, buffer, sizeof(buffer));
  if (len > 0) {
    os.write(buffer, static_cast<std::streamsize>(len));
  }
}

inline void reset_style(std::ostream &os) {
  os.write(detail::style_builder::reset(),
           static_cast<std::streamsize>(detail::style_builder::reset_length()));
}

template <typename... Args>
void print_styled_to(std::ostream &os, const detail::text_style &style,
                     std::string_view fmt_str, Args &&...args) {
  detail::print_styled_impl(os, style, fmt_str, static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_styled_to(std::ostream &os, const detail::text_style &style,
                       std::string_view fmt_str, Args &&...args) {
  detail::println_styled_impl(os, style, fmt_str, static_cast<Args &&>(args)...);
}
#endif

template <typename... Args>
void print_error(std::string_view fmt_str, Args &&...args) {
  print_styled(detail::styles::error(), fmt_str, static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_error(std::string_view fmt_str, Args &&...args) {
  println_styled(detail::styles::error(), fmt_str,
                 static_cast<Args &&>(args)...);
}

template <typename... Args>
void print_warning(std::string_view fmt_str, Args &&...args) {
  print_styled(detail::styles::warning(), fmt_str,
               static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_warning(std::string_view fmt_str, Args &&...args) {
  println_styled(detail::styles::warning(), fmt_str,
                 static_cast<Args &&>(args)...);
}

template <typename... Args>
void print_info(std::string_view fmt_str, Args &&...args) {
  print_styled(detail::styles::info(), fmt_str, static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_info(std::string_view fmt_str, Args &&...args) {
  println_styled(detail::styles::info(), fmt_str, static_cast<Args &&>(args)...);
}

template <typename... Args>
void print_success(std::string_view fmt_str, Args &&...args) {
  print_styled(detail::styles::success(), fmt_str,
               static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_success(std::string_view fmt_str, Args &&...args) {
  println_styled(detail::styles::success(), fmt_str,
                 static_cast<Args &&>(args)...);
}

template <typename... Args>
void print_debug(std::string_view fmt_str, Args &&...args) {
  print_styled(detail::styles::debug(), fmt_str, static_cast<Args &&>(args)...);
}

template <typename... Args>
void println_debug(std::string_view fmt_str, Args &&...args) {
  println_styled(detail::styles::debug(), fmt_str,
                 static_cast<Args &&>(args)...);
}

using detail::bg_color;
using detail::color;
using detail::style;
using detail::text_style;
using detail::with_color;
using detail::with_colors;
using detail::with_style;

} // namespace e_fmt

#endif // FORMAT_HPP
