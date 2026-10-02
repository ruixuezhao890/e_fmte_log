/**
 ******************************************************************************
 * @file           : ecli_compile_fail_names.cpp
 * @brief          : 反例 —— 两个字段抢同一个长选项名，必须编译期报错
 * @attention      : run_check.ps1 的 Invoke-EfmtCompileFail 驱动：编译必须失败，
 *                   且诊断里要出现「选项名撞车」。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <ecli/cli.hpp>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE(struct dup_names {
  [[efmt::arg(long = "x")]] int a;
  [[efmt::arg(long = "x")]] int b;   // 撞车：long / alias / short / short_alias 必须两两不同
}, Cli);

int main() {
  dup_names d{};
  const char *argv[] = {"app", "--x=1"};
  return static_cast<int>(parse(2, argv, d));
}
