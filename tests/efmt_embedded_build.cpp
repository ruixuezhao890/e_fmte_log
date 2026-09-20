/**
 ******************************************************************************
 * @file           : efmt_embedded_build.cpp
 * @brief          : Compile-only check for the embedded configuration
 * @attention      : Built with -DEFMT_ENABLE_HOSTED=0 (no std::string, no
 *                   streams, no stdio, no ANSI styles). Everything here must
 *                   compile and run on a freestanding target.
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#if EFMT_ENABLE_DYNAMIC_STRING
#error "embedded build must not enable dynamic strings"
#endif
#if EFMT_ENABLE_STREAM_API
#error "embedded build must not enable the stream API"
#endif
#if EFMT_ENABLE_STDIO
#error "embedded build must not enable stdio"
#endif

using namespace e_fmt;

struct sensor_reading {
  int raw;
};

int main() {
  // 输出重定向到用户缓冲区（嵌入式等价于 UART/RTT）
  char log_sink[256];
  char formatted[64];
  set_buffer_output(log_sink, sizeof(log_sink));

  println_info("boot ok");
  println_info("raw={} hex={:#x} half={:.2f}", 42, 0xAB, 1.5);

  const size_t written = format_to(formatted, sizeof(formatted), "[{}|{}]",
                                   sensor_reading{7}.raw, "text");
  const size_t needed = format_to(formatted, sizeof(formatted),
                                  "{:>8}|{:<8}|{:^8}", 1, 2, 3);
  const size_t size = formatted_size("{}-{}", 1, 2);

  // 编译期校验在嵌入式配置下同样可用
  const size_t checked = format_to(formatted, sizeof(formatted),
                                   E_FMT_STR("{}"), 1);

  // 截断判定：返回值 >= 缓冲区大小
  char tiny[4];
  const bool truncated = format_to(tiny, sizeof(tiny), "{}", 123456) >= sizeof(tiny);

  const bool ok = written > 0 && needed < sizeof(formatted) && size == 3 &&
                  checked == 1 && truncated &&
                  get_output_handler() != nullptr;
  reset_output_handler();
  return ok ? 0 : 1;
}
