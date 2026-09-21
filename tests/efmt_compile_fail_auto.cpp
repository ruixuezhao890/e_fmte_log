/**
 ******************************************************************************
 * @file           : efmt_compile_fail_auto.cpp
 * @brief          : 反例 - AUTO 用在不支持的形态上必须编译失败且提示可操作
 * @attention      : 这个类型有自定义构造函数（不是聚合体），AUTO 推导不出字段，
 *                   必须报错并提示改用 E_FMT_FORMATTER_FIELDS，而不是给个错的结果。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

struct not_aggregate {
  explicit not_aggregate(int value) : v(value) {}
  int v;
};

E_FMT_FORMATTER_AUTO(not_aggregate);   // static_assert: 只能用于聚合体

int main() {
  char buffer[64];
  return static_cast<int>(e_fmt::format_to(buffer, sizeof(buffer), "{}", not_aggregate{7}));
}
