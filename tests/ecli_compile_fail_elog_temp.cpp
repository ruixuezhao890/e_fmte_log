/**
 ******************************************************************************
 * @file           : ecli_compile_fail_elog_temp.cpp
 * @brief          : 反例 —— 把临时 sink 交给 reply_to_sink（必须编译不过）
 * @attention      : reply 只存指针，临时 sink 在 dispatch 期间就没了；
 *                   这里被删除重载当场拦下，而不是留到运行时崩。
 ******************************************************************************
 */

#include <ecli/elog_reply.hpp>

int main() {
  const ecli::reply bad = ecli::reply_to_sink(e_log::stdout_sink());   // ← 应当编译报错
  (void)bad;
  return 0;
}
