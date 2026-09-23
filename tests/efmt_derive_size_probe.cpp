/**
 ******************************************************************************
 * @file           : efmt_derive_size_probe.cpp
 * @brief          : E_FMT_DERIVE 的真实嵌入式工具链编译样例（run_check.ps1 -Size 用）
 * @attention      : 宿主校验只能证明"GCC 15 能过"；这里在 arm-none-eabi / xtensa 上
 *                   真实编译一次 derive 路径，并给出每个派生类型的体积读数。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

E_FMT_DERIVE(struct imu { float ax, ay, az; });
E_FMT_DERIVE(struct cfg { int retry; bool verbose; char tag[8]; });
E_FMT_DERIVE(enum class state { idle, busy = 5, fault });

int main() {
  char line[128];
  // 直接写缓冲区，不依赖任何输出处理器
  const size_t n1 = e_fmt::format_to(line, sizeof(line), "{}", imu{1.5f, 2.0f, 3.0f});
  const size_t n2 = e_fmt::format_to(line, sizeof(line), "{}", cfg{3, true, "boot"});
  const size_t n3 = e_fmt::format_to(line, sizeof(line), "{}", state::busy);
  // 用不掉的返回值防优化
  return (n1 == 0 || n2 == 0 || n3 == 0) ? 1 : 0;
}
