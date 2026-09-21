/**
 ******************************************************************************
 * @file           : efmt_embedded_build.cpp
 * @brief          : 嵌入式配置（EFMT_ENABLE_HOSTED=0）的编译 + 行为检查
 * @attention      : 无 std::string / 流 / stdio / ANSI；浮点走自带引擎。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include <cstring>

#if EFMT_ENABLE_DYNAMIC_STRING
#error "embedded build must not enable dynamic strings"
#endif
#if EFMT_ENABLE_STREAM_API
#error "embedded build must not enable the stream API"
#endif
#if EFMT_ENABLE_STDIO
#error "embedded build must not enable stdio"
#endif
#if EFMT_ENABLE_ANSI_STYLES
#error "embedded build must not enable ANSI styles by default"
#endif
#if EFMT_ENABLE_CONTAINER_FORMAT
#error "embedded build must not enable container formatting by default"
#endif
#if EFMT_USE_LIBC_PRINTF
#error "embedded build must not use the libc float printf path"
#endif
#if EFMT_MAX_FORMAT_ARGS != 8
#error "embedded build should default to 8 format arguments"
#endif

using namespace e_fmt;

struct sensor_reading {
  int raw;
};

int main() {
  int failures = 0;
  char formatted[128];

  // 输出重定向到用户缓冲区（嵌入式里就是 UART/RTT）
  char log_sink[256];
  set_buffer_output(log_sink, sizeof(log_sink));

  println_info("boot ok");
  println_info("raw={} hex={:#x} half={:.2f}", 42, 0xAB, 1.5);

  // 关闭 ANSI 时必须一个转义字节都不产生：直接扫缓冲区找 0x1B
  const size_t logged = get_buffer_output_pos();
  for (size_t i = 0; i < logged; ++i) {
    if (log_sink[i] == '\x1b') {
      ++failures;  // ESC 出现在 UART 流里 = 终端会看到乱码
      break;
    }
  }

  // 自带浮点引擎在嵌入式配置下的输出
  size_t n = format_to(formatted, sizeof(formatted), "{:.2f}", 1.5);
  failures += (n != 4 || std::strcmp(formatted, "1.50") != 0) ? 1 : 0;

  n = format_to(formatted, sizeof(formatted), "{:.0f}", 2.5);  // 半偶舍入
  failures += (n != 1 || std::strcmp(formatted, "2") != 0) ? 1 : 0;

  n = format_to(formatted, sizeof(formatted), "{:e}", 1.0);
  failures += (n != 12 || std::strcmp(formatted, "1.000000e+00") != 0) ? 1 : 0;

  n = format_to(formatted, sizeof(formatted), "{:g}", 100000.0);
  failures += (n != 6 || std::strcmp(formatted, "100000") != 0) ? 1 : 0;

  n = format_to(formatted, sizeof(formatted), "{:.3f}", -0.0005);
  failures += (n != 6 || std::strcmp(formatted, "-0.001") != 0) ? 1 : 0;

  n = format_to(formatted, sizeof(formatted), "{:08.3f}", 3.14159);
  failures += (n != 8 || std::strcmp(formatted, "0003.142") != 0) ? 1 : 0;

  // 其余基础能力
  const size_t written = format_to(formatted, sizeof(formatted), "[{}|{}]",
                                   sensor_reading{7}.raw, "text");
  const size_t needed = format_to(formatted, sizeof(formatted),
                                  "{:>8}|{:<8}|{:^8}", 1, 2, 3);
  const size_t size = formatted_size("{}-{}", 1, 2);

  // 编译期校验在嵌入式配置下同样可用
  const size_t checked = format_to(formatted, sizeof(formatted), E_FMT_STR("{}"), 1);

  // 参数上限（默认 8）：8 个实参必须能过
  const size_t eight = format_to(formatted, sizeof(formatted), "{}{}{}{}{}{}{}{}",
                                 1, 2, 3, 4, 5, 6, 7, 8);

  // 截断判定：返回值 >= 缓冲区大小
  char tiny[4];
  const bool truncated = format_to(tiny, sizeof(tiny), "{}", 123456) >= sizeof(tiny);

  if (!(written > 0 && needed < sizeof(formatted) && size == 3 && checked == 1 &&
        eight == 8 && truncated && get_output_handler() != nullptr)) {
    ++failures;
  }

  reset_output_handler();
  return failures == 0 ? 0 : 1;
}
