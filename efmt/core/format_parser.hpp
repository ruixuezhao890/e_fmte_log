/**
 ******************************************************************************
 * @file           : format_parser.hpp
 * @author         : ruixuezhao
 * @brief          : Format spec parser with error code handling
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_PARSER_HPP
#define FORMAT_PARSER_HPP

#include <middleware/efmt/core/format_specs.hpp>
#include <middleware/efmt/core/format_base.hpp>

namespace e_fmt::detail {
// Parser for the contents after ':' inside a replacement field.
// It produces a format_specs object consumed by formatter<T>.

// 字符分类辅助
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_align(char c) { return c == '<' || c == '>' || c == '^'; }
constexpr bool is_sign(char c) { return c == '+' || c == '-' || c == ' '; }

constexpr bool is_type(char c) {
  return c == 'b' || c == 'B' || c == 'c' || c == 'd' || c == 'e' ||
         c == 'E' || c == 'f' || c == 'F' || c == 'g' || c == 'G' ||
         c == 'o' || c == 'p' || c == 's' || c == 'x' || c == 'X';
}

constexpr align to_align(char c) {
  return c == '<' ? align::left : (c == '>' ? align::right : align::center);
}

constexpr sign to_sign(char c) {
  return c == '+' ? sign::plus : (c == '-' ? sign::minus : sign::space);
}

// 解析宽度/精度：超长数字串饱和处理，避免整数溢出
constexpr int parse_number(const char *&p, const char *end) {
  int value = 0;
  while (p < end && is_digit(*p)) {
    if (value < 100000000) {
      value = value * 10 + (*p - '0');
    }
    ++p;
  }
  return value;
}

constexpr size_t parse_index(const char *&p, const char *end) {
  size_t value = 0;
  while (p < end && is_digit(*p)) {
    if (value < static_cast<size_t>(-1) / 16) {
      value = value * 10 + static_cast<size_t>(*p - '0');
    }
    ++p;
  }
  return value;
}

// ============================================================================
// Format Spec Parser - 格式规范解析器
// ============================================================================
// Grammar: [[fill]align][sign][#][0][width][.precision][type]
// One linear scan over the specifier; characters that belong to no option are
// ignored, so an unknown specifier degrades to the default formatting instead
// of failing the whole call.
constexpr format_specs parse_format_spec(const char *begin, const char *end) {
  format_specs specs;
  const char *p = begin;

  // [fill]align - a fill character is only a fill when an alignment follows
  if (p + 1 < end && is_align(p[1])) {
    specs.fill = p[0];
    specs.alignment = to_align(p[1]);
    p += 2;
  } else if (p < end && is_align(p[0])) {
    specs.alignment = to_align(p[0]);
    ++p;
  }

  // sign
  if (p < end && is_sign(*p)) {
    specs.sign_mode = to_sign(*p);
    ++p;
  }

  // '#'
  if (p < end && *p == '#') {
    specs.alt = true;
    ++p;
  }

  // '0' - zero padding (only meaningful without an explicit alignment)
  if (p < end && *p == '0') {
    specs.zero = true;
    ++p;
  }

  // width
  if (p < end && is_digit(*p)) {
    specs.width = parse_number(p, end);
  }

  // '.' precision
  if (p < end && *p == '.') {
    ++p;
    specs.precision = parse_number(p, end);
  }

  // type
  if (p < end && is_type(*p)) {
    specs.type = static_cast<presentation>(*p);
    ++p;
  }

  return specs;
}

} // namespace e_fmt::detail
#endif // FORMAT_PARSER_HPP
