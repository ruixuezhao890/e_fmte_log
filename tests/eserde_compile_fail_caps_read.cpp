/**
 ******************************************************************************
 * @file           : eserde_compile_fail_caps_read.cpp
 * @brief          : 反例 —— 只写了 Serialize、没写 Deserialize 就想读，必须编译期报错
 * @attention      : 由 run_check.ps1 的 Invoke-EfmtCompileFail 驱动：
 *                   编译必须失败，且诊断里要出现「这个类型不能反序列化」。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/cbor.hpp>

using namespace e_fmt;
using namespace eserde;

E_FMT_DERIVE(struct write_only {
  int x;
}, Debug, Serialize);

int main() {
  write_only w{};
  const unsigned char bytes[2] = {0xA1u, 0x61u};
  return static_cast<int>(cbor::read_from(bytes, sizeof(bytes), w));
}
