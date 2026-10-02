/**
 ******************************************************************************
 * @file           : ecli_compile_fail_caps.cpp
 * @brief          : 反例 —— 没在声明里写 Cli 就想当命令行参数，必须编译期报错
 * @attention      : 这条由 run_check.ps1 的 Invoke-EfmtCompileFail 驱动：
 *                   编译必须失败，且诊断里要出现「这个类型不能做命令行参数」。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/traits.hpp>
#include <ecli/cli.hpp>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE(struct unmarked {
  int x;
});   // 只有默认的 Debug：能力标签没写，就不许解析命令行

int main() {
  unmarked u{};
  const char *argv[] = {"app", "--x=1"};
  return static_cast<int>(parse(2, argv, u));
}
