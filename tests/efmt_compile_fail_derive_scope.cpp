/**
 ******************************************************************************
 * @file           : efmt_compile_fail_derive_scope.cpp
 * @brief          : 反例 - 把派生宏写在类型命名空间之外，必须编译失败
 * @attention      : 这里宏在全局作用域，而类型在 namespace app 里：宏展开出的
 *                   efmt_derive_format 在全局命名空间，ADL 找不到它。
 *                   早期版本会静默退化成 obj@地址，现在必须编译报错并说明原因。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

namespace app {
struct cfg {
  int retry;
  bool verbose;
};
}  // namespace app

// ✗ 写错位置：应该写在 namespace app 里面
E_FMT_FORMATTER_FIELDS(app::cfg, retry, verbose);

int main() {
  char buffer[64];
  return static_cast<int>(e_fmt::format_to(buffer, sizeof(buffer), "{}", app::cfg{1, true}));
}
