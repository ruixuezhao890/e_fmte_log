/**
 ******************************************************************************
 * @file           : format_style.hpp
 * @author         : ruixuezhao
 * @brief          : ANSI color and style support for terminal output
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_STYLE_HPP
#define FORMAT_STYLE_HPP

#include <cstdint>
#include <middleware/efmt/core/format_base.hpp>

namespace e_fmt::detail {
// ANSI styling helpers used by the print/println family.
// These utilities are orthogonal to string formatting itself: they describe
// how already-formatted text should look when emitted to a terminal.

// ============================================================================
// ANSI Color Code Definitions
// ============================================================================

// Text colors (foreground)
enum class color : uint8_t {
  // Basic colors (30-37)
  black = 30,
  red = 31,
  green = 32,
  yellow = 33,
  blue = 34,
  magenta = 35,
  cyan = 36,
  white = 37,

  // Bright variants (90-97)
  bright_black = 90,
  bright_red = 91,
  bright_green = 92,
  bright_yellow = 93,
  bright_blue = 94,
  bright_magenta = 95,
  bright_cyan = 96,
  bright_white = 97,

  // Default
  none = 0,        // Reset to default
  default_ = 39    // Default foreground color
};

// Background colors (40-47)
enum class bg_color : uint8_t {
  // Basic colors (40-47)
  black = 40,
  red = 41,
  green = 42,
  yellow = 43,
  blue = 44,
  magenta = 45,
  cyan = 46,
  white = 47,

  // Bright variants (100-107)
  bright_black = 100,
  bright_red = 101,
  bright_green = 102,
  bright_yellow = 103,
  bright_blue = 104,
  bright_magenta = 105,
  bright_cyan = 106,
  bright_white = 107,

  // Default
  none = 0,        // Reset to default
  default_ = 49    // Default background color
};

// Text styles
enum class style : uint8_t {
  reset = 0,
  bold = 1,
  dim = 2,
  italic = 3,
  underline = 4,
  blink = 5,
  reverse = 7,
  hidden = 8,
  strikethrough = 9,

  // Not widely supported, but defined
  double_underline = 21,
  no_bold = 22,
  no_dim = 22,
  no_italic = 23,
  no_underline = 24,
  no_blink = 25,
  no_reverse = 27,
  no_hidden = 28,
  no_strikethrough = 29
};

// ============================================================================
// Style Combination Helper
// ============================================================================

// Represents a complete text style (foreground, background, and text styles)
struct text_style {
  // Bitmask-based style storage keeps the object small and easy to combine.
  color fg = color::none;
  bg_color bg = bg_color::none;
  uint16_t styles = 0;  // Bitmask for style flags

  // Style flags
  static constexpr uint16_t bold_bit = 1 << 0;
  static constexpr uint16_t dim_bit = 1 << 1;
  static constexpr uint16_t italic_bit = 1 << 2;
  static constexpr uint16_t underline_bit = 1 << 3;
  static constexpr uint16_t blink_bit = 1 << 4;
  static constexpr uint16_t reverse_bit = 1 << 5;
  static constexpr uint16_t hidden_bit = 1 << 6;
  static constexpr uint16_t strikethrough_bit = 1 << 7;

  // Default constructor
  constexpr text_style() = default;

  // Constructor with foreground color
  constexpr text_style(color fg_) : fg(fg_) {}

  // Constructor with foreground and background
  constexpr text_style(color fg_, bg_color bg_) : fg(fg_), bg(bg_) {}

  // Set style
  constexpr text_style& set_style(style s) {
    switch (s) {
      case style::bold: styles |= bold_bit; break;
      case style::dim: styles |= dim_bit; break;
      case style::italic: styles |= italic_bit; break;
      case style::underline: styles |= underline_bit; break;
      case style::blink: styles |= blink_bit; break;
      case style::reverse: styles |= reverse_bit; break;
      case style::hidden: styles |= hidden_bit; break;
      case style::strikethrough: styles |= strikethrough_bit; break;
      default: break;
    }
    return *this;
  }

  // Check if style is set
  constexpr bool has_style(style s) const {
    switch (s) {
      case style::bold: return (styles & bold_bit) != 0;
      case style::dim: return (styles & dim_bit) != 0;
      case style::italic: return (styles & italic_bit) != 0;
      case style::underline: return (styles & underline_bit) != 0;
      case style::blink: return (styles & blink_bit) != 0;
      case style::reverse: return (styles & reverse_bit) != 0;
      case style::hidden: return (styles & hidden_bit) != 0;
      case style::strikethrough: return (styles & strikethrough_bit) != 0;
      default: return false;
    }
  }

  // Check if any styling is applied
  constexpr bool is_empty() const {
    return fg == color::none && bg == bg_color::none && styles == 0;
  }

  // Reset to default
  constexpr void clear() {
    fg = color::none;
    bg = bg_color::none;
    styles = 0;
  }
};

// ============================================================================
// ANSI Escape Sequence Generator
// ============================================================================

class style_builder {
public:
  // Build ANSI escape sequence for a color
  static void build_color(char* buffer, size_t& size, color c) {
    size = build_single(buffer, 16, static_cast<int>(c));
  }

  // Build ANSI escape sequence for a background color
  static void build_bg_color(char* buffer, size_t& size, bg_color c) {
    size = build_single(buffer, 16, static_cast<int>(c));
  }

  // Build ANSI escape sequence for a style
  static void build_style(char* buffer, size_t& size, style s) {
    size = build_single(buffer, 16, static_cast<int>(s));
  }

  // Build complete ANSI escape sequence for a text_style
  // Merge all selected style attributes into a single CSI escape sequence.
  // Digits are written directly instead of through snprintf: the escape
  // sequence builder then needs no <cstdio>, which matters on embedded builds
  // with EFMT_ENABLE_STDIO=0.
  static size_t build(text_style ts, char* buffer, size_t buffer_size) {
    if (ts.is_empty() || buffer_size < 4) {
      return 0;
    }

    size_t pos = 0;
    buffer[pos++] = '\033';
    buffer[pos++] = '[';

    bool first = true;
    pos = append_code(buffer, pos, buffer_size, code_of(ts.fg), first);
    pos = append_code(buffer, pos, buffer_size, code_of(ts.bg), first);
    for (const style_entry& entry : style_table) {
      if ((ts.styles & entry.bit) != 0) {
        pos = append_code(buffer, pos, buffer_size,
                          static_cast<int>(entry.code), first);
      }
    }

    buffer[pos++] = 'm';

    return pos;
  }

  // Reset sequence
  static constexpr const char* reset() {
    return "\033[0m";
  }

  static constexpr size_t reset_length() {
    return 4;  // strlen("\033[0m")
  }

private:
  // 需要输出数字的样式位（顺序即输出顺序）
  struct style_entry {
    uint16_t bit;
    style code;
  };

  static constexpr style_entry style_table[] = {
      {text_style::bold_bit, style::bold},
      {text_style::dim_bit, style::dim},
      {text_style::italic_bit, style::italic},
      {text_style::underline_bit, style::underline},
      {text_style::blink_bit, style::blink},
      {text_style::reverse_bit, style::reverse},
      {text_style::hidden_bit, style::hidden},
      {text_style::strikethrough_bit, style::strikethrough},
  };

  static constexpr int code_of(color c) {
    return (c == color::none || c == color::default_) ? 0 : static_cast<int>(c);
  }

  static constexpr int code_of(bg_color c) {
    return (c == bg_color::none || c == bg_color::default_) ? 0
                                                            : static_cast<int>(c);
  }

  // 追加一个 ';' 分隔的十进制参数（0 表示跳过）
  static size_t append_code(char* buffer, size_t pos, size_t buffer_size,
                            int code, bool& first) {
    if (code <= 0 || pos + 4 > buffer_size) {
      return pos;
    }
    if (!first) {
      buffer[pos++] = ';';
    }
    first = false;

    if (code >= 100) {
      buffer[pos++] = static_cast<char>('0' + code / 100);
    }
    if (code >= 10) {
      buffer[pos++] = static_cast<char>('0' + (code / 10) % 10);
    }
    buffer[pos++] = static_cast<char>('0' + code % 10);
    return pos;
  }

  // "\033[<code>m"
  static size_t build_single(char* buffer, size_t buffer_size, int code) {
    if (code <= 0) {
      return 0;
    }
    size_t pos = 0;
    buffer[pos++] = '\033';
    buffer[pos++] = '[';
    bool first = true;
    pos = append_code(buffer, pos, buffer_size, code, first);
    buffer[pos++] = 'm';
    return pos;
  }
};

// ============================================================================
// Predefined Style Constants
// ============================================================================

namespace styles {
  // Prebuilt presets consumed by convenience APIs such as print_error().
  // Error styles
  constexpr text_style error() {
    text_style s(color::red);
    s.set_style(style::bold);
    return s;
  }

  constexpr text_style warning() {
    return text_style(color::yellow);
  }

  constexpr text_style info() {
    return text_style(color::blue);
  }

  constexpr text_style success() {
    text_style s(color::green);
    s.set_style(style::bold);
    return s;
  }

  constexpr text_style debug() {
    text_style s(color::bright_black);
    s.set_style(style::dim);
    return s;
  }

  // Special styles
  constexpr text_style highlight() {
    text_style s(color::bright_yellow);
    s.set_style(style::bold);
    return s;
  }

  constexpr text_style muted() {
    text_style s(color::bright_black);
    return s;
  }

  constexpr text_style emphasis() {
    text_style s(color::cyan);
    s.set_style(style::underline);
    return s;
  }

  // Header styles
  constexpr text_style header1() {
    text_style s(color::bright_white);
    s.set_style(style::bold);
    s.set_style(style::underline);
    return s;
  }

  constexpr text_style header2() {
    text_style s(color::bright_cyan);
    s.set_style(style::bold);
    return s;
  }

  // Code style
  constexpr text_style code() {
    return text_style(color::bright_black, bg_color::bright_white);
  }
}

} // namespace e_fmt::detail

#endif // FORMAT_STYLE_HPP
