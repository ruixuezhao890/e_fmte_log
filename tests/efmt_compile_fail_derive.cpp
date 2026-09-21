/**
 ******************************************************************************
 * @file           : efmt_compile_fail_derive.cpp
 * @brief          : 反例 - E_FMT_DERIVE 遇到"没有格式化器的成员"必须编译失败
 * @attention      : 对应 Rust 里"内层类型没实现 Debug"的编译错误。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

enum class not_derived { a, b };   // 故意不给它 E_FMT_DERIVE

E_FMT_DERIVE(struct has_unformattable {
  not_derived s;                   // 这个成员没有格式化器
  int n;
});

int main() {
  has_unformattable v{};
  return static_cast<int>(e_fmt::format("{}", v).size());
}
