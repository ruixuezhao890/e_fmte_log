/**
 ******************************************************************************
 * @file           : eserde_json_etl_check.cpp
 * @brief          : eserde::json 与 ETL 类型（etl::string / etl::vector）
 * @attention      : 基座不认 ETL 具体类型，只看成员函数（data/size/clear/push_back），
 *                   所以 std 与 ETL 走的是同一套代码 —— 这里把这条钉住。
 *                   需要 ETL 头文件，run_check.ps1 里在没有 ETL 时跳过。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string.h>
#include <middleware/etl/vector.h>
#include <eserde/json.hpp>

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

static std::string text_of(const node &n) {
  const std::size_t need = json::write_to(nullptr, 0, n);
  std::string out(need, '\0');
  json::write_to(&out[0], need, n);
  return out;
}

int main() {
  node a{};
  a.id = etl::string<8>("n1");
  a.nums.push_back(1);
  a.nums.push_back(2);
  a.label = etl::string<16>("boot ok");

  check("etl serialize", text_of(a) == "{\"id\":\"n1\",\"nums\":[1,2],\"label\":\"boot ok\"}",
        text_of(a));

  node b{};
  check("etl read", json::read_from(text_of(a), b) == json::error::ok);
  check("etl read values",
        b.id == a.id && b.label == a.label && b.nums.size() == 2 && b.nums[0] == 1 && b.nums[1] == 2);

  // etl::string 装不下 → truncated（不静默截断）
  node c{};
  c.id = etl::string<8>("keep");
  check("etl string overflow",
        json::read_from("{\"id\":\"123456789\"}", c) == json::error::truncated);
  check("etl string untouched", c.id == etl::string<8>("keep"));

  // etl::vector 容量不够 → truncated
  node d{};
  check("etl vector overflow",
        json::read_from("{\"nums\":[1,2,3,4,5]}", d) == json::error::truncated);

  // 空的数组 / 字符串
  node e{};
  check("etl empty", json::read_from("{\"id\":\"\",\"nums\":[],\"label\":\"\"}", e) ==
                          json::error::ok &&
                          e.id.empty() && e.nums.empty() && e.label.empty());

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
