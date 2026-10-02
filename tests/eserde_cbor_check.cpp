/**
 ******************************************************************************
 * @file           : eserde_cbor_check.cpp
 * @brief          : eserde::cbor 的行为检查（RFC 8949 黄金字节 / 往返 / 错误码 / 标签）
 * @attention      : 只依赖标准库，宿主与嵌入式两种配置都能编。
 *                   黄金向量取自 RFC 8949 附录 A（标准编码器的输出）——
 *                   这是"我们写的是合法 CBOR"的零依赖证据：写出的字节要逐字节相等，
 *                   标准编码器写出的字节要能读回来。ETL 类型的对应检查见
 *                   tests/eserde_cbor_etl_check.cpp。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/cbor.hpp>

#include <array>
#include <cstdio>
#include <limits>
#include <string>
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
}, Debug, Serialize, Deserialize);

E_FMT_DERIVE(struct person {
  int age;
  [[efmt::arg(short, cbor = "user_name")]]   // cbor = "别名"：CBOR 的键用 user_name
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
  [[efmt::arg(cbor = "skip")]]               // 不进 CBOR，也不从 CBOR 读
  int internal;
}, Debug, Serialize, Deserialize);

E_FMT_DERIVE(struct tiny_range {
  signed char small;
}, Debug, Serialize, Deserialize);

// 键名别名 + 跳过 + map 条目数（定长头）的最小组合
E_FMT_DERIVE(struct keys_demo {
  int a;
  [[efmt::arg(cbor = "b")]] int bee;
  bool flag;
}, Debug, Serialize, Deserialize);

E_FMT_DERIVE(struct skip_demo {
  int keep;
  [[efmt::arg(cbor = "skip")]] int drop;
}, Debug, Serialize, Deserialize);

E_FMT_DERIVE(struct fixed_text {
  char text[3];
}, Debug, Serialize, Deserialize);

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

static void check_error(const char *what, cbor::error e, cbor::error wanted) {
  check(what, e == wanted, std::string("actual=") + cbor::error_name(e) + " wanted=" +
                               cbor::error_name(wanted));
}

static std::string to_hex(const unsigned char *p, std::size_t n) {
  static const char *kHex = "0123456789abcdef";
  std::string out;
  for (std::size_t k = 0; k < n; ++k) {
    out.push_back(kHex[p[k] >> 4]);
    out.push_back(kHex[p[k] & 0x0Fu]);
  }
  return out;
}

// 序列化到 std::string（宿主上没有 to_bytes 之类的便利函数，两行就够）
template <typename T> static std::string bytes_of(const T &v) {
  const std::size_t need = cbor::write_to(nullptr, 0, v);
  std::string out(need, '\0');
  cbor::write_to(reinterpret_cast<unsigned char *>(&out[0]), need, v);
  return out;
}

template <typename T> static std::string hex_of(const T &v) {
  const std::string bytes = bytes_of(v);
  return to_hex(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
}

// hex 字面量 → 字节串（读端用标准编码器产出的字节喂进来）
static std::string unhex(const char *hex) {
  std::string out;
  for (std::size_t k = 0; hex[k] != 0 && hex[k + 1] != 0; k += 2) {
    unsigned nibble[2] = {0, 0};
    for (std::size_t j = 0; j < 2; ++j) {
      const char c = hex[k + j];
      if (c >= '0' && c <= '9') nibble[j] = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') nibble[j] = static_cast<unsigned>(c - 'a' + 10);
    }
    out.push_back(static_cast<char>((nibble[0] << 4) | nibble[1]));
  }
  return out;
}

template <typename T> static cbor::error read_hex(const char *hex, T &out) {
  const std::string bytes = unhex(hex);
  return cbor::read_from(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), out);
}

template <typename T> static cbor::error read_bytes(const std::string &bytes, T &out) {
  return cbor::read_from(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), out);
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
  // ---- RFC 8949 附录 A：写出的字节必须逐字节相等（标准编码器的输出）----
  check_text("uint 0", hex_of(0), "00");
  check_text("uint 10", hex_of(10), "0a");
  check_text("uint 23", hex_of(23), "17");
  check_text("uint 24", hex_of(24), "1818");
  check_text("uint 100", hex_of(100), "1864");
  check_text("uint 1000", hex_of(1000), "1903e8");
  check_text("uint 1000000", hex_of(1000000), "1a000f4240");
  check_text("uint 1000000000000", hex_of(1000000000000LL), "1b000000e8d4a51000");
  check_text("uint max", hex_of(18446744073709551615ULL), "1bffffffffffffffff");
  check_text("neg -1", hex_of(-1), "20");
  check_text("neg -10", hex_of(-10), "29");
  check_text("neg -100", hex_of(-100), "3863");
  check_text("neg -1000", hex_of(-1000), "3903e7");
  check_text("int64 min", hex_of(std::numeric_limits<long long>::min()),
             "3b7fffffffffffffff");
  check_text("false", hex_of(false), "f4");
  check_text("true", hex_of(true), "f5");
  check_text("float 1.5", hex_of(1.5f), "fa3fc00000");
  check_text("double 1.5", hex_of(1.5), "fb3ff8000000000000");
  check_text("double 1.1", hex_of(1.1), "fb3ff199999999999a");
  check_text("text empty", hex_of(std::string("")), "60");
  check_text("text IETF", hex_of(std::string("IETF")), "6449455446");
  check_text("text utf8", hex_of(std::string("水")), "63e6b0b4");
  check_text("array int[2]", hex_of(pair2_t{1, 2}), "820102");
  check_text("array vector", hex_of(std::vector<int>{1, 2, 3}), "83010203");
  check_text("empty vector", hex_of(std::vector<int>{}), "80");
  check_text("map by struct", hex_of(skip_demo{1, 9}), "a1646b65657001");
  check_text("map 3 pairs + alias", hex_of(keys_demo{1, 2, true}), "a361610161620264666c6167f5");

  // ---- 读：标准编码器写出的字节必须读得回来 ----
  {
    int v = 0;
    check_error("read uint 1000", read_hex("1903e8", v), cbor::error::ok);
    check("read uint 1000 value", v == 1000, std::to_string(v));
    check_error("read neg -1000", read_hex("3903e7", v), cbor::error::ok);
    check("read neg value", v == -1000, std::to_string(v));
    unsigned long long u = 0;
    check_error("read uint max", read_hex("1bffffffffffffffff", u), cbor::error::ok);
    check("read uint max value", u == 18446744073709551615ULL);
    // 非最短编码也收（读端宽松：别人可以写 1 字节参数的 5）
    check_error("read non-shortest 5", read_hex("1805", v), cbor::error::ok);
    check("read non-shortest value", v == 5, std::to_string(v));
    check_error("read non-shortest -1", read_hex("3800", v), cbor::error::ok);
    check("read non-shortest -1 value", v == -1, std::to_string(v));
    // 半精度浮点（0xF9）：我们自己不写，但别人的编码器会写
    double d = 0;
    check_error("read half 1.5", read_hex("f93e00", d), cbor::error::ok);
    check("half value", d == 1.5, std::to_string(d));
    check_error("read float32", read_hex("fa3fc00000", d), cbor::error::ok);
    check("float32 value", d == 1.5, std::to_string(d));
    float f = 0;
    check_error("read double into float", read_hex("fb3ff8000000000000", f), cbor::error::ok);
    check("double into float value", f == 1.5f);
    bool b = false;
    check_error("read true", read_hex("f5", b), cbor::error::ok);
    check("true value", b);
    check_error("read 1 as bool", read_hex("01", b), cbor::error::ok);
    check("1 as bool value", b);
    std::string s;
    check_error("read text", read_hex("6449455446", s), cbor::error::ok);
    check_text("text value", s, "IETF");
    std::vector<int> vi;
    check_error("read array", read_hex("83010203", vi), cbor::error::ok);
    check("array size", vi.size() == 3 && vi[0] == 1 && vi[2] == 3);
  }

  // ---- 读：标准编码器的 map（键序任意、别名、未知键）----
  {
    keys_demo s{};
    // {"b":2,"a":1,"flag":true,"zzz":[1,[2,3]]} —— 键序打乱 + 多个未知键
    check_error("read map shuffled",
                read_hex("a461620261610164666c6167f5637a7a7a8201820203", s), cbor::error::ok);
    check("map values", s.a == 1 && s.bee == 2 && s.flag);
  }

  // ---- 结构与往返：字段顺序、别名、跳过、嵌套、枚举、整数 / 浮点 / 布尔 ----
  const person a = make_person();
  {
    person b{};
    check_error("roundtrip read", read_bytes(bytes_of(a), b), cbor::error::ok);
    check("roundtrip equal",
          b.age == a.age && b.name == a.name && b.salary == a.salary && b.active == a.active &&
              b.m == a.m && b.p.x == a.p.x && b.p.y == a.p.y && b.tags[0] == 1 &&
              b.tags[1] == 2 && b.tags[2] == 3 && b.nums == a.nums && b.two == a.two &&
              b.note == a.note && b.big == a.big && b.internal == 0);
  }

  // ---- 枚举：写底层整数（二进制要的是字节数），读整数或取值名都收 ----
  {
    check_text("enum writes number", hex_of(mode::slow), "05");
    check_text("enum negative number", hex_of(mode::err), "20");
    mode m = mode::fast;
    check_error("read enum number", read_hex("05", m), cbor::error::ok);
    check("enum number value", m == mode::slow);
    check_error("read enum name", read_hex("6466617374", m), cbor::error::ok);   // "fast"
    check("enum name value", m == mode::fast);
    check_error("unknown enum name", read_hex("647a7a7a7a", m), cbor::error::type_mismatch);
  }

  // ---- 写接口语义：snprintf 风格（返回所需长度、空间不够照样返回长度）----
  {
    unsigned char small_buf[4];
    const std::size_t need = cbor::write_to(small_buf, sizeof(small_buf), a);
    check("write_to need", need == bytes_of(a).size());
    check("write_to truncates", need > sizeof(small_buf));
    check_text("write_to prefix", to_hex(small_buf, sizeof(small_buf)), "ab636167");   // map(11) + "age" 开头
    check("write_to count-only", cbor::write_to(nullptr, 0, a) == need);
  }

  // ---- 读：未知键跳过、null 保持原值、缺省字段保持原值 ----
  {
    person b{};
    b.age = 7;
    // {"unknown":{"deep":[1,2]},"user_name":"amy","age":null}
    check_error("unknown keys / null",
                read_hex("a369757365725f6e616d6563616d7963616765f667756e6b6e6f776e"
                         "a16464656570820102",
                         b),
                cbor::error::ok);
    check("unknown key skipped", b.age == 7 && b.name == "amy",
          "age=" + std::to_string(b.age) + " name=" + b.name);
  }

  // ---- NaN / Inf 原样传（二进制相对 JSON 的实质收益：JSON 只能写成 null）----
  {
    person b = make_person();
    b.salary = std::numeric_limits<double>::quiet_NaN();
    person c{};
    check_error("nan roundtrip", read_bytes(bytes_of(b), c), cbor::error::ok);
    check("nan preserved", c.salary != c.salary);

    b.salary = std::numeric_limits<double>::infinity();
    person d{};
    check_error("inf roundtrip", read_bytes(bytes_of(b), d), cbor::error::ok);
    check("inf preserved", d.salary > std::numeric_limits<double>::max());
  }

  // ---- 错误码：坏编码 / 类型错 / 装不下 / 太深 / 子集之外 ----
  {
    person b{};
    int v = 0;
    check_error("empty input", read_hex("", v), cbor::error::syntax);
    check_error("truncated head", read_hex("18", v), cbor::error::syntax);
    check_error("truncated uint64", read_hex("1b0102", v), cbor::error::syntax);
    check_error("reserved ai 28", read_hex("1c", v), cbor::error::syntax);
    check_error("text longer than input", read_hex("64494546", b.name), cbor::error::syntax);
    check_error("trailing junk", read_hex("0000", v), cbor::error::syntax);
    check_error("text into int", read_hex("6161", v), cbor::error::type_mismatch);
    check_error("int into struct", read_hex("01", b), cbor::error::type_mismatch);
    check_error("array into struct", read_hex("83010203", b), cbor::error::type_mismatch);
    check_error("bool strict", read_hex("02", b.active), cbor::error::type_mismatch);
    check_error("indefinite array", read_hex("9f0102ff", b.nums), cbor::error::unsupported);
    check_error("tag value skipped", read_hex("a16178c001", b), cbor::error::unsupported);
    check_error("too deep",
                read_hex("a1617881818181818181818101", b), cbor::error::too_deep);

    tiny_range t{};
    check_error("out of range int", read_hex("a165736d616c6c19012c", t), cbor::error::truncated);
    std::array<int, 2> fixed2{};
    check_error("fixed array overflow", read_hex("83010203", fixed2), cbor::error::truncated);
    // {"text":"abcd"}：char[3] 装不下 4 个字符（连结束符要 5 字节）→ truncated
    fixed_text ft{};
    check_error("char array overflow", read_hex("a164746578746461626364", ft), cbor::error::truncated);
    check("char array cleared", ft.text[0] == 0);
    fixed_text ft2{};
    check_error("char array fits", read_hex("a16474657874626162", ft2), cbor::error::ok);
    check("char array value", std::string(ft2.text) == "ab");
  }

  // ---- 失败不动原对象（先解析到副本，成功才赋回）----
  {
    person b = make_person();
    // {"age":"x","name":"zed"}：age 是 int 字段，喂文本串 → type_mismatch
    check_error("failing read", read_hex("a2636167656178646e616d65637a6564", b),
                cbor::error::type_mismatch);
    check("target untouched", b.age == 30 && b.name == "bob");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
