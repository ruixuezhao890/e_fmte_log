/**
 ******************************************************************************
 * @file           : format_specs.hpp
 * @author         : ruixuezhao
 * @brief          : None
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_SPECS_HPP
#define FORMAT_SPECS_HPP

namespace e_fmt::detail {
// Parsed representation of the format specifier part after ':'.
// Example: "{:08x}" becomes fill='0', width=8, type=hex_lower.
// 对齐类型
enum class align : char {
  none = 0,    // 无符号
  left = '<',  // 左对齐
  right = '>', // 右对齐
  center = '^' // 居中对齐
};

// 符号类型
enum class sign : char {
  none = '\0', // 无符号
  plus = '+',  // 总是显示符号
  minus = '-', // 仅负数显示符号
  space = ' '  // 正数显示空格
};

// 类型类型
enum class presentation : char {
  none = '\0',

  // 整数
  dec = 'd',       // 十进制
  oct = 'o',       // 八进制
  hex_lower = 'x', // 十六进制小写
  hex_upper = 'X', // 十六进制大写
  bin = 'b',       // 二进制小写
  bin_upper = 'B', // 二进制大写

  // 浮点
  fixed_lower = 'f',
  fixed_upper = 'F',
  exp_lower = 'e',
  exp_upper = 'E',
  general_lower = 'g',
  general_upper = 'G',

  // 其他
  chr = 'c',    // 字符
  str = 's',    // 字符串
  pointer = 'p' // 指针
};

// 格式规范
// Complete formatting options for a single replacement field.
struct format_specs {
  // 填充和对齐
  char fill = ' ';               // 填充字符
  align alignment = align::none; // 对齐方式

  // 符号
  sign sign_mode = sign::none; // 符号方式

  // 其他选项
  bool alt = false;  // 替代形式 (#)
  bool zero = false; // 前导零（等价于 fill='0', alignment=right）

  // 宽度和精度
  int width = 0;      // 宽度
  int precision = -1; // 精度（-1 表示未指定）

  // 类型
  presentation type = presentation::none;
};

} // namespace e_fmt::detail

#endif // FORMAT_SPECS_HPP
