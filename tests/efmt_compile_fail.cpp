/**
 ******************************************************************************
 * @file           : efmt_compile_fail.cpp
 * @brief          : Negative test - this file MUST NOT compile
 * @attention      : tests/run_check.ps1 compiles it and expects the static
 *                   assertion "Number of arguments does not match format
 *                   string". It is never linked into a test binary.
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

int main() {
  // 2 placeholders, 1 argument -> static_assert
  return static_cast<int>(e_fmt::format(E_FMT_STR("x={} y={}"), 1).size());
}
