/**
 ******************************************************************************
 * @file           : ecli_compile_fail_relation.cpp
 * @brief          : 反例 —— needs / conflicts / unless 引用了一个不存在的字段，必须编译期报错
 * @attention      : run_check.ps1 的 Invoke-EfmtCompileFail 驱动：编译必须失败，
 *                   且诊断里要出现「needs / conflicts / unless 的取值必须是本类型里真实存在」。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <ecli/cli.hpp>

using namespace e_fmt;
using namespace ecli;

E_FMT_DERIVE(struct bad_relation {
  [[efmt::arg(long = "a", needs = "nosuchfield")]] int a;
}, Cli);

int main() {
  bad_relation r{};
  const char *argv[] = {"app", "--a=1"};
  return static_cast<int>(parse(2, argv, r));
}
