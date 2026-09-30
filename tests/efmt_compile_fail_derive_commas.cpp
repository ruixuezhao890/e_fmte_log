/**
 ******************************************************************************
 * @file           : efmt_compile_fail_derive_commas.cpp
 * @brief          : 反例 - E_FMT_DERIVE 的第一个参数必须是【没有顶层逗号的声明】
 * @attention      : 一行多字段（int x, y;）会把宏参数切断，必须编译失败并给出
 *                   看得懂的提示（预处理器不会替你拼回声明）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

E_FMT_DERIVE(struct pair_like { int x, y; });

int main() {
  pair_like v{1, 2};
  return static_cast<int>(e_fmt::format("{}", v).size());
}
