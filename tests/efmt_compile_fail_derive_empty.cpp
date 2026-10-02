/**
 ******************************************************************************
 * @file           : efmt_compile_fail_derive_empty.cpp
 * @brief          : 反例 —— 空类型不能推导（结构体至少要有一个字段 / 枚举至少要有一个取值）
 * @attention      : 这一条同时钉住"一次报错只说一个原因"：声明没解析成功时不再实例化
 *                   打印器，所以不会追着抛 derived_names_t<1> 转不成 derived_names_t<0>、
 *                   cannot decompose class type ... 这类二次错误，把真原因淹掉。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

using namespace e_fmt;

E_FMT_DERIVE(struct empty_cfg {}, Debug);   // ← 应当编译报错

int main() { return 0; }
