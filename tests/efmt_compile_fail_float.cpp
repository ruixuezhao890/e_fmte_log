/**
 ******************************************************************************
 * @file           : efmt_compile_fail_float.cpp
 * @brief          : 反例 - EFMT_ENABLE_FLOAT=0 时必须编译失败
 * @attention      : tests/run_check.ps1 用它验证"关掉浮点后仍写浮点参数"会在
 *                   编译期被拦住，而不是静默输出错误内容。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

int main() {
  char buffer[32];
  // 本配置不编译浮点通道 -> static_assert: EFMT_ENABLE_FLOAT=0
  return static_cast<int>(e_fmt::format_to(buffer, sizeof(buffer), "{}", 1.5));
}
