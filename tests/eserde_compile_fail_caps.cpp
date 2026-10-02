/**
 ******************************************************************************
 * @file           : eserde_compile_fail_caps.cpp
 * @brief          : 反例 —— 没在声明里写 Serialize 就想序列化，必须编译期报错
 * @attention      : 这条由 run_check.ps1 的 Invoke-EfmtCompileFail 驱动：
 *                   编译必须失败，且诊断里要出现「这个类型不能序列化」。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/cbor.hpp>
#include <eserde/json.hpp>

using namespace e_fmt;
using namespace eserde;

E_FMT_DERIVE(struct unmarked {
  int x;
});   // 只有默认的 Debug：能力标签没写，就不许（反）序列化

int main() {
  unmarked u{};
  u.x = 1;
  char buf[64];
  return static_cast<int>(json::write_to(buf, sizeof(buf), u));
}
