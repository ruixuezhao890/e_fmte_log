/**
 ******************************************************************************
 * @file           : format_context.hpp
 * @author         : ruixuezhao
 * @brief          : Format context for output buffering
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_CONTEXT_HPP
#define FORMAT_CONTEXT_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <cstring>
#include <string_view>

namespace e_fmt::detail {

// 格式化上下文 - 管理输出缓冲区
// Shared output context used by all formatter implementations.
// It centralizes safe buffer writes and alignment.
//
// 位置语义与 snprintf 一致：pos() 始终是"完整输出需要的长度"。缓冲区装不下时
// 多余的字符被丢弃，但位置继续累加，因此调用方用 返回值 >= 缓冲区大小 就能判断
// 截断，用 min(返回值, 大小) 得到实际写入的字符数。
// buffer == nullptr / size == 0 时就是纯计数模式（formatted_size 用）。
class format_context {
public:
  format_context(char *buffer, size_t size)
      : buffer_(buffer), size_(size), pos_(0) {}

  // 计数模式：只统计长度，不写入任何内容
  static format_context counting() { return format_context(nullptr, 0); }

  // 写入单个字符
  void write_char(char c) {
    if (pos_ < size_) {
      buffer_[pos_] = c;
    }
    ++pos_;
  }

  // 写入多个字符
  void write_chars(const char *str, size_t len) {
    if (len == 0) {
      return;
    }
    if (pos_ < size_) {
      const size_t space = size_ - pos_;
      std::memcpy(buffer_ + pos_, str, (len <= space) ? len : space);
    }
    pos_ += len;
  }

  // 写入字符串视图
  void write_str(std::string_view sv) { write_chars(sv.data(), sv.size()); }

  // 填充字符（用于对齐）
  void write_fill(char fill, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      write_char(fill);
    }
  }

  // 应用对齐
  // Alignment is implemented once here so individual formatter<T> only needs
  // to produce the raw textual representation of a value.
  void write_aligned(std::string_view content, const format_specs &specs) {
    size_t content_len = content.size();
    size_t width = (specs.width > 0) ? static_cast<size_t>(specs.width) : 0;

    if (width <= content_len) {
      write_str(content);
      return;
    }

    size_t fill_count = width - content_len;
    size_t left_fill = 0;
    size_t right_fill = 0;

    switch (specs.alignment) {
    case align::left:
      right_fill = fill_count;
      break;
    case align::right:
      left_fill = fill_count;
      break;
    case align::center:
      left_fill = fill_count / 2;
      right_fill = fill_count - left_fill;
      break;
    default:
      // 默认右对齐
      left_fill = fill_count;
      break;
    }

    write_fill(specs.fill, left_fill);
    write_str(content);
    write_fill(specs.fill, right_fill);
  }

  // 写入 "前缀 + 补零 + 数字"，并套用宽度/填充/对齐
  // Numbers are emitted in three pieces so the sign stays glued to the digits
  // and the zeros land between them: both the '0' option and an integer
  // precision need that, and emitting piece-wise keeps an arbitrarily large
  // precision from needing a temporary buffer.
  void write_number(const char *prefix, size_t prefix_len, size_t zeros,
                    const char *digits, size_t digits_len,
                    const format_specs &specs) {
    size_t width = (specs.width > 0) ? static_cast<size_t>(specs.width) : 0;
    size_t body = prefix_len + zeros + digits_len;
    size_t pad = (width > body) ? width - body : 0;
    char fill = specs.fill;

    // The '0' option only applies when no explicit alignment was given
    // (std::format ignores it otherwise).
    if (specs.zero && specs.alignment == align::none) {
      zeros += pad;
      pad = 0;
      fill = '0';
    }

    size_t left = 0;
    size_t right = 0;
    switch (specs.alignment) {
    case align::left:
      right = pad;
      break;
    case align::center:
      left = pad / 2;
      right = pad - left;
      break;
    default:
      left = pad;
      break;
    }

    write_fill(fill, left);
    write_chars(prefix, prefix_len);
    write_fill('0', zeros);
    write_chars(digits, digits_len);
    write_fill(fill, right);
  }

  // 完整输出需要的长度（即使缓冲区装不下也会算准）
  [[nodiscard]] size_t pos() const { return pos_; }

  // 重置位置
  void reset(size_t pos = 0) { pos_ = pos; }

  // 获取缓冲区指针
  [[nodiscard]] char *buffer() { return buffer_; }

private:
  char *buffer_;
  size_t size_;
  size_t pos_;
};

} // namespace e_fmt::detail

#endif // FORMAT_CONTEXT_HPP
