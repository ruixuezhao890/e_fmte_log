/**
 ******************************************************************************
 * @file           : efmt_tiny_build.cpp
 * @brief          : 最小配置裁剪检查（关浮点、参数上限 4、关容器/流/ANSI）
 * @attention      : 用 -DEFMT_ENABLE_FLOAT=0 -DEFMT_MAX_FORMAT_ARGS=4 编译，
 *                   验证裁剪开关真的把东西裁掉、且剩下的接口仍然正确。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include <cstring>

#if EFMT_ENABLE_FLOAT
#error "this configuration must compile without the float channel"
#endif
#if EFMT_MAX_FORMAT_ARGS != 4
#error "this configuration must cap the argument count at 4"
#endif
#if EFMT_ENABLE_CONTAINER_FORMAT
#error "this configuration must not compile container formatting"
#endif
#if EFMT_ENABLE_DYNAMIC_STRING
#error "this configuration must not use std::string"
#endif

using namespace e_fmt;

int main() {
  int failures = 0;
  char buffer[64];

  // 4 个参数正常
  size_t n = format_to(buffer, sizeof(buffer), "{}{}{}{}", 1, 2, 3, 4);
  failures += (n != 4 || std::strcmp(buffer, "1234") != 0) ? 1 : 0;

  // 整数 / 字符串 / 指针 / 字符 / bool 仍然可用
  n = format_to(buffer, sizeof(buffer), "{:#06x}", 255);
  failures += (n != 6 || std::strcmp(buffer, "0x00ff") != 0) ? 1 : 0;

  n = format_to(buffer, sizeof(buffer), "{:<6}|{:.2}", "ab", "xyz");
  failures += (n != 9 || std::strcmp(buffer, "ab    |xy") != 0) ? 1 : 0;

  n = format_to(buffer, sizeof(buffer), "{:s}", true);
  failures += (n != 4 || std::strcmp(buffer, "true") != 0) ? 1 : 0;

  // 截断语义（snprintf）
  char tiny[4];
  failures += (format_to(tiny, sizeof(tiny), "{}", 123456) < sizeof(tiny)) ? 1 : 0;
  failures += (std::strcmp(tiny, "123") != 0) ? 1 : 0;

  // 参数表的栈占用确实降下来了（4 × 24 B + 计数）
  static_assert(sizeof(detail::format_args) <= 112,
                "EFMT_MAX_FORMAT_ARGS=4 should shrink the argument table");

  // 关闭 ANSI 时 print 系列不产生任何转义字节
  char sink[64];
  set_buffer_output(sink, sizeof(sink));
  println_error("e={}", 1);
  const size_t logged = get_buffer_output_pos();
  for (size_t i = 0; i < logged; ++i) {
    if (sink[i] == '\x1b') {
      ++failures;
      break;
    }
  }
  reset_output_handler();

  return failures == 0 ? 0 : 1;
}
