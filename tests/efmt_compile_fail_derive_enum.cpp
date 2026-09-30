/**
 ******************************************************************************
 * @file           : efmt_compile_fail_derive_enum.cpp
 * @brief          : 反例 - 枚举不能用 E_FMT_DERIVE（枚举体的逗号是顶层逗号）
 * @attention      : 枚举必须走 E_FMT_DERIVE_ENUM(...)（整段声明一起进去）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

E_FMT_DERIVE(enum class color { red, green, blue });

int main() {
  return static_cast<int>(e_fmt::format("{}", color::red).size());
}
