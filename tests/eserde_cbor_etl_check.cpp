/**
 ******************************************************************************
 * @file           : eserde_cbor_etl_check.cpp
 * @brief          : eserde::cbor 与 ETL 类型（etl::string / etl::vector）
 * @attention      : 形状判定在 traits.hpp，只看成员函数（data/size/clear/push_back/max_size），
 *                   所以 std 与 ETL 走同一套代码 —— 这里把这条在二进制格式上再钉一遍。
 *                   需要 ETL 头文件，run_check.ps1 里在没有 ETL 时跳过。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string.h>
#include <middleware/etl/vector.h>
#include <eserde/cbor.hpp>

#include <cstdio>
#include <string>

using namespace e_fmt;
using namespace eserde;

// 类型名里带逗号的模板先 typedef（逗号会把 E_FMT_DERIVE 的第一个参数切断）
using int4 = etl::vector<int, 4>;

E_FMT_DERIVE(struct node {
  etl::string<8> id;
  int4 nums;
  etl::string<16> label;
}, Debug, Serialize, Deserialize);

static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static std::string to_hex(const std::string &bytes) {
  static const char *kHex = "0123456789abcdef";
  std::string out;
  for (std::size_t k = 0; k < bytes.size(); ++k) {
    out.push_back(kHex[static_cast<unsigned char>(bytes[k]) >> 4]);
    out.push_back(kHex[static_cast<unsigned char>(bytes[k]) & 0x0Fu]);
  }
  return out;
}

template <typename T> static std::string bytes_of(const T &v) {
  const std::size_t need = cbor::write_to(nullptr, 0, v);
  std::string out(need, '\0');
  cbor::write_to(reinterpret_cast<unsigned char *>(&out[0]), need, v);
  return out;
}

static cbor::error read_hex(const char *hex, node &out) {
  static const char *kDigits = "0123456789abcdef";
  std::string bytes;
  for (std::size_t k = 0; hex[k] != 0 && hex[k + 1] != 0; k += 2) {
    unsigned v = 0;
    for (std::size_t j = 0; j < 2; ++j) {
      const char c = hex[k + j];
      for (unsigned d = 0; d < 16; ++d) {
        if (kDigits[d] == c) { v = (v << 4) | d; break; }
      }
    }
    bytes.push_back(static_cast<char>(v));
  }
  return cbor::read_from(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), out);
}

int main() {
  node a{};
  a.id = etl::string<8>("n1");
  a.nums.push_back(1);
  a.nums.push_back(2);
  a.label = etl::string<16>("boot ok");

  // 定长 map 头 + 文本串键 + 定长数组头：黄金字节（独立解码器核过）
  check("etl serialize",
        to_hex(bytes_of(a)) == "a3626964626e31646e756d73820102656c6162656c67626f6f74206f6b",
        to_hex(bytes_of(a)));

  node b{};
  check("etl read", cbor::read_from(reinterpret_cast<const unsigned char *>(bytes_of(a).data()),
                                    bytes_of(a).size(), b) == cbor::error::ok);
  check("etl read values",
        b.id == a.id && b.label == a.label && b.nums.size() == 2 && b.nums[0] == 1 && b.nums[1] == 2);

  // etl::string 装不下 → truncated（不静默截断），原对象不动
  node c{};
  c.id = etl::string<8>("keep");
  check("etl string overflow",
        read_hex("a162696469313233343536373839", c) == cbor::error::truncated);   // {"id":"123456789"}
  check("etl string untouched", c.id == etl::string<8>("keep"));

  // etl::vector 容量不够 → truncated
  node d{};
  check("etl vector overflow",
        read_hex("a1646e756d73850102030405", d) == cbor::error::truncated);       // {"nums":[1,2,3,4,5]}

  // 空的数组 / 字符串
  node e{};
  check("etl empty", read_hex("a362696460646e756d7380656c6162656c60", e) == cbor::error::ok &&
                         e.id.empty() && e.nums.empty() && e.label.empty());

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
