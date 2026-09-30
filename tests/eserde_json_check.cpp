/**
 ******************************************************************************
 * @file           : eserde_json_check.cpp
 * @brief          : eserde::json 的行为检查（序列化 / 反序列化 / 错误码 / 标签）
 * @attention      : 只用标准库类型，宿主与嵌入式两种配置都能编（宿主才有的
 *                   to_string 单独用宏圈出来）。ETL 类型的对应检查在
 *                   tests/eserde_json_etl_check.cpp。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/json.hpp>

#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;
using namespace eserde;

// 类型名里带逗号的模板先 typedef（逗号会让 E_FMT_DERIVE 的第一个参数被切断）
using pair2_t = std::array<int, 2>;

E_FMT_DERIVE_ENUM(enum class mode {
  fast,
  slow = 5,
  err = -1
});

E_FMT_DERIVE(struct point {
  int x;
  int y;
});

E_FMT_DERIVE(struct person {
  int age;
  [[efmt::arg(short, json = "user_name")]]   // json = "别名"：JSON 里用 user_name
  std::string name;
  double salary;
  bool active;
  mode m;
  point p;
  int tags[3];
  std::vector<int> nums;
  pair2_t two;
  std::string note;
  unsigned long long big;
  [[efmt::arg(json = "skip")]]               // 不进 JSON，也不从 JSON 读
  int internal;
}, Debug, Serialize);

E_FMT_DERIVE(struct tiny_range {
  signed char small;
});

// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static void check_text(const char *what, std::string_view actual, std::string_view wanted) {
  check(what, actual == wanted,
        "actual=[" + std::string(actual) + "] wanted=[" + std::string(wanted) + "]");
}

static void check_error(const char *what, json::error e, json::error wanted) {
  check(what, e == wanted, std::string("actual=") + json::error_name(e) + " wanted=" +
                               json::error_name(wanted));
}

static std::string json_of(const person &p) {
  const std::size_t need = json::write_to(nullptr, 0, p);
  std::string out(need, '\0');
  json::write_to(&out[0], need, p);
  return out;
}

static person make_person() {
  person p{};
  p.age = 30;
  p.name = "bob";
  p.salary = 1234.5;
  p.active = true;
  p.m = mode::slow;
  p.p = point{3, 4};
  p.tags[0] = 1; p.tags[1] = 2; p.tags[2] = 3;
  p.nums = {7, 8, 9};
  p.two = pair2_t{4, 5};
  p.note = "a\"b\\c";
  p.big = 18446744073709551615ULL;
  p.internal = 999;
  return p;
}

int main() {
  const person a = make_person();

  // ---- 序列化：字段顺序、别名、跳过、嵌套、枚举名、数组、整数 / 浮点 / 布尔 ----
  check_text("serialize",
             json_of(a),
             "{\"age\":30,\"user_name\":\"bob\",\"salary\":1234.5,\"active\":true,"
             "\"m\":\"slow\",\"p\":{\"x\":3,\"y\":4},\"tags\":[1,2,3],"
             "\"nums\":[7,8,9],\"two\":[4,5],\"note\":\"a\\\"b\\\\c\","
             "\"big\":18446744073709551615}");

  // ---- 往返：读回来必须一模一样（internal 被跳过，所以保持默认）----
  {
    person b{};
    check_error("roundtrip read", json::read_from(json_of(a), b), json::error::ok);
    check("roundtrip equal",
          b.age == a.age && b.name == a.name && b.salary == a.salary && b.active == a.active &&
              b.m == a.m && b.p.x == a.p.x && b.p.y == a.p.y && b.tags[0] == 1 &&
              b.tags[1] == 2 && b.tags[2] == 3 && b.nums == a.nums && b.two == a.two &&
              b.note == a.note && b.big == a.big && b.internal == 0);
  }

  // ---- 写接口语义：snprintf 风格（返回所需长度、空间不够照样返回长度）----
  {
    char small[8];
    const std::size_t need = json::write_to(small, sizeof(small), a);
    check("write_to need", need == json_of(a).size());
    check("write_to truncates", need > sizeof(small));
    check_text("write_to prefix", std::string_view(small, sizeof(small)), "{\"age\":3");
    check("write_to count-only", json::write_to(nullptr, 0, a) == need);
  }

  // ---- 读：未知键跳过（前向兼容）、null 保持原值、别名与枚举数字 ----
  {
    person b{};
    b.age = 7;
    const char *text =
        "{\"unknown\":{\"deep\":[1,{\"k\":\"v\"}]},\"user_name\":\"amy\","
        "\"age\":null,\"m\":-1,\"tags\":[9],\"nums\":[]}";
    check_error("unknown keys / null / enum number", json::read_from(text, b), json::error::ok);
    // 没写到的 / 写成 null 的字段保持原值（配置合并语义）
    check("unknown key skipped",
          b.age == 7 && b.name == "amy" && b.m == mode::err,
          "age=" + std::to_string(b.age) + " name=" + b.name + " m=" + std::to_string(static_cast<int>(b.m)));
    check("partial array read", b.tags[0] == 9 && b.tags[1] == 0 && b.nums.empty());
  }

  // ---- 转义：\uXXXX（含代理对）与 \n \t 等 ----
  {
    person b{};
    check_error("escapes", json::read_from("{\"note\":\"l1\\nl2\\t\\u4e2d\\uD83D\\uDE00\"}", b),
                json::error::ok);
    check("escape decoded",
          b.note == std::string("l1\nl2\t") + "\xe4\xb8\xad" + "\xf0\x9f\x98\x80");
  }

  // ---- 错误码：语法错 / 类型错 / 装不下 / 嵌套太深 ----
  {
    person b{};
    check_error("missing value", json::read_from("{\"age\":}", b), json::error::syntax);
    check_error("bad number", json::read_from("{\"age\":1.}", b), json::error::syntax);
    check_error("unclosed", json::read_from("{\"age\":1", b), json::error::syntax);
    check_error("trailing junk", json::read_from("{\"age\":1} x", b), json::error::syntax);
    check_error("string into int", json::read_from("{\"age\":\"x\"}", b), json::error::type_mismatch);
    check_error("real into int", json::read_from("{\"age\":1.5}", b), json::error::type_mismatch);
    check_error("too deep", json::read_from("{\"unknown\":[[[[[[[[[[1]]]]]]]]]]}", b),
                json::error::too_deep);
    check_error("not an object", json::read_from("[1,2]", b), json::error::type_mismatch);

    tiny_range t{};
    check_error("out of range int", json::read_from("{\"small\":300}", t), json::error::truncated);
    check_error("bool strict", json::read_from("{\"active\":2}", b), json::error::type_mismatch);
  }

  // ---- 失败不动原对象（先解析到临时对象，成功才赋回）----
  {
    person b = make_person();
    check_error("failing read", json::read_from("{\"age\":\"x\",\"name\":\"zed\"}", b),
                json::error::type_mismatch);
    check("target untouched", b.age == 30 && b.name == "bob");
  }

  // ---- NaN / Inf 写成 null ----
  {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    // 借 person.salary 走一遍：NaN 应写成 null，读 null 时保持原值
    person b = make_person();
    b.salary = nan;
    const std::string text = json_of(b);
    check("nan writes null", text.find("\"salary\":null") != std::string::npos, text);
    check_error("null keeps value", json::read_from(text, b), json::error::ok);
    check("null did not clobber", b.salary != b.salary);   // 仍是 NaN
  }

#if EFMT_ENABLE_DYNAMIC_STRING
  check_text("to_string()", json::to_string(a), json_of(a));
#endif

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
