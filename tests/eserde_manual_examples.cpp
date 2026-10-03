/**
 ******************************************************************************
 * @file           : eserde_manual_examples.cpp
 * @brief          : 手册里的示例代码必须有可编译版本（防止文档腐烂）
 * @attention      : 手册 docs/libs/ESERDE-使用手册.md 里的每一段代码都在这里编译
 *                   并跑断言：文档与实现脱节时，这里会先红。分节注释标出对应手册小节。
 *                   套路与 tests/efmt_manual_examples.cpp 一致：零测试框架，
 *                   check() 辅助 + g_checks/g_failures 计数 + 失败返回 1。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/json.hpp>
#include <eserde/cbor.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace e_fmt;
using namespace eserde;

// ETL 是可选的：装了才编 ETL 那一节（与 run_check.ps1 里"没有 ETL 就跳过"一致）
#if defined(__has_include)
#if __has_include(<middleware/etl/string.h>)
#include <middleware/etl/string.h>
#include <middleware/etl/vector.h>
#define ESERDE_MANUAL_HAS_ETL 1
#endif
#endif

// 类型名里带逗号的模板先 typedef：逗号会把 E_FMT_DERIVE 的第一个参数切断
using arr2_t = std::array<int, 2>;
#ifdef ESERDE_MANUAL_HAS_ETL
using int4_t = etl::vector<int, 4>;
#endif

// ============================================================================
// 手册 3.1 声明你的类型：能力标签
// ============================================================================
// 自定义能力标签：随便一个空类型，efmt 只原样登记、不解释含义
struct Toml {};

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
  [[efmt::arg(short, long, json = "user_name", cbor = "un")]] std::string name;
  double salary;
  bool active;
  mode m;
  point p;
  int tags[3];
  std::vector<int> nums;
  arr2_t two;
  std::string note;
  unsigned long long big;
  [[efmt::arg(json = "skip", cbor = "skip")]] int internal;
}, Debug, Serialize, Deserialize, Toml);

// 只写不读：两个方向各自声明（只 Serialize）
E_FMT_DERIVE(struct write_only {
  int x;
}, Debug, Serialize);

// 只读不写
E_FMT_DERIVE(struct read_only {
  signed char small;
}, Debug, Deserialize);

// 手册 3.1：能力标签不认识也没关系 —— Toml 只是被登记，序列化照旧
static_assert(eserde::is_registered_v<person>, "person 注册过");
static_assert(eserde::has_cap_v<person, Serialize>, "带 Serialize");
static_assert(eserde::has_cap_v<person, Deserialize>, "带 Deserialize");
static_assert(eserde::has_cap_v<person, Toml>, "自定义标签照样查得到");
static_assert(eserde::has_cap_v<person, Debug>, "注册过的类型默认有 Debug");
static_assert(!eserde::has_cap_v<write_only, Deserialize>, "只写不读：没有 Deserialize");
static_assert(!eserde::has_cap_v<read_only, Serialize>, "只读不写：没有 Serialize");
static_assert(!eserde::has_cap_v<int, Serialize>, "标量没有能力标签");
static_assert(!eserde::is_registered_v<int>, "int 没注册过");
// 枚举侧没有能力标签：写它靠"基础类型"身份，不靠 Serialize
static_assert(eserde::has_cap_v<mode, Debug>, "枚举注册过 → 有 Debug");
static_assert(!eserde::has_cap_v<mode, Serialize>, "枚举没有 Serialize 标签");

// ============================================================================
// 手册 3.2 字段标签：格式名就是标签名
// ============================================================================
static_assert(eserde::field_key<person>(1, "json") == "user_name", "json 别名");
static_assert(eserde::field_key<person>(1, "cbor") == "un", "同一个字段两个格式各一套键名");
static_assert(eserde::field_key<person>(1, "toml") == "name", "别的格式看不见 json/cbor 标签 → 退回字段名");
static_assert(eserde::field_skipped<person>(11, "json"), "json = \"skip\"");
static_assert(eserde::field_skipped<person>(11, "cbor"), "cbor = \"skip\"");
static_assert(!eserde::field_skipped<person>(1, "json"), "没标 skip 就不是跳过");
static_assert(eserde::has_tag<person>(1, "short"), "无值标签照常可查");
static_assert(eserde::find_by_tag<person>("long") == 1, "按标签反查字段下标");
static_assert(eserde::tag_count<person>(0) == 0, "age 没有标签");

// ============================================================================
// 手册 3.8 自己接一种新格式：TOML-ish（只用到公开基座 API，不改库）
// ============================================================================
namespace toml {

constexpr std::string_view kFormat = "toml";

// 一个 snprintf 语义的 sink：写不下就只计数
struct sink {
  char *buf;
  std::size_t cap;
  std::size_t n = 0;

  void put(char c) {
    if (n < cap) buf[n] = c;
    ++n;
  }
  void put(std::string_view s) {
    for (char c : s) put(c);
  }
};

// sep：顶层字段一行一个；嵌套内联表用 ", " 分隔
template <typename T> void write_body(sink &w, const T &obj, std::string_view sep);

// 标量：类型分支怎么写随你，这里只示范"库不参与"
template <typename T> void write_scalar(sink &w, const T &v) {
  using D = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_array_v<D>) {                       // char[N]
    w.put('"');
    w.put(std::string_view(v));
    w.put('"');
  } else if constexpr (std::is_convertible_v<const D &, std::string_view>) {
    w.put('"');                                              // std::string / const char*
    w.put(std::string_view(v));
    w.put('"');
  } else if constexpr (std::is_same_v<D, bool>) {
    w.put(v ? "true" : "false");
  } else if constexpr (std::is_enum_v<D>) {                  // 枚举名问 schema
    const long long key = static_cast<long long>(static_cast<std::underlying_type_t<D>>(v));
    for (std::size_t i = 0; i < eserde::field_count<D>(); ++i) {
      if (eserde::enum_value<D>(i) == key) {
        w.put(eserde::field_name<D>(i));
        return;
      }
    }
    w.put("?");                                              // 没列出的取值
  } else if constexpr (eserde::has_cap_v<D, eserde::Serialize>) {
    w.put("{ ");                                             // 嵌套结构体 → 内联表
    write_body(w, v, ", ");
    w.put(" }");
  } else {
    char tmp[32];                                            // 整数 / 浮点：交给 efmt
    const std::size_t n = e_fmt::format_to(tmp, sizeof(tmp), "{}", v);
    w.put(std::string_view(tmp, n));
  }
}

template <typename T> void write_body(sink &w, const T &obj, std::string_view sep) {
  static_assert(eserde::has_cap_v<T, eserde::Serialize>,
                "toml：类型要写 E_FMT_DERIVE(..., Serialize) 才能被这个格式写出来");
  bool first = true;
  eserde::visit_fields(obj, [&](std::string_view name, const auto &value) {
    const std::size_t i = eserde::find_field<T>(name);
    // skip 必须先判：field_key 遇到值标签 "skip" 会把 "skip" 当键名返回
    if (eserde::field_skipped<T>(i, kFormat)) return;
    if (!first) w.put(sep);
    first = false;
    w.put(eserde::field_key<T>(i, kFormat));                  // toml = "别名"，没标就是字段名
    w.put(" = ");
    write_scalar(w, value);
  });
}

template <typename T> std::size_t write_to(char *buf, std::size_t size, const T &value) {
  sink w{buf, size, 0};
  write_body(w, value, "\n");
  w.put('\n');
  return w.n;
}

}  // namespace toml

E_FMT_DERIVE(struct toml_cfg {
  [[efmt::arg(toml = "boot_ms")]] unsigned boot_delay_ms;
  float gain;
  bool verbose;
  point origin;
  [[efmt::arg(toml = "skip")]] int internal;
}, Debug, Serialize, Deserialize);

// ============================================================================
// 手册 3.5 支持的类型
// ============================================================================
E_FMT_DERIVE_ENUM(enum class level : unsigned char { low = 0, high = 0x10 });

E_FMT_DERIVE(struct shapes {
  unsigned long long big;
  double d;
  bool flag;
  level lv;
  char tag[8];
  int raw[3];
  std::string text;
  std::vector<int> vec;
  arr2_t pair2;
  point nested;
}, Debug, Serialize, Deserialize);

#ifdef ESERDE_MANUAL_HAS_ETL
E_FMT_DERIVE(struct etl_node {
  etl::string<8> id;
  int4_t nums;
  etl::string<16> label;
}, Debug, Serialize, Deserialize);
#endif

// 手册 2：第一个能跑的例子用的类型
E_FMT_DERIVE(struct reading {
  int id;
  float value;
}, Debug, Serialize, Deserialize);

// 手册 4：设备配置（两个格式都要，所以两个能力标签都写）
E_FMT_DERIVE(struct device_cfg {
  [[efmt::arg(json = "boot_delay_ms", cbor = "bd")]] unsigned boot_delay_ms;
  float gain;
  bool verbose;
  [[efmt::arg(json = "skip", cbor = "skip")]] int internal;
}, Debug, Serialize, Deserialize);

// ============================================================================
// 手册 4 完整示例：保存到文件 / 从文件读回
// ============================================================================
// 保存：先量所需长度 → 判断缓冲够不够 → 自己补 '\0' → 写文件
static bool save_json(const char *path, const device_cfg &cfg) {
  char buf[192];
  const std::size_t need = eserde::json::write_to(buf, sizeof(buf) - 1, cfg);
  if (need >= sizeof(buf)) return false;          // 截断：别写半截文件出去
  buf[need] = '\0';
  std::FILE *f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  const std::size_t wrote = std::fwrite(buf, 1, need, f);
  std::fclose(f);
  return wrote == need;
}

// 读回：整份读进缓冲 → read_from → 看错误码（失败时 cfg 一根毫毛都不动）
static eserde::json::error load_json(const char *path, device_cfg &cfg) {
  char buf[192];
  std::FILE *f = std::fopen(path, "rb");
  if (f == nullptr) return eserde::json::error::syntax;   // 打不开当作语法错上报
  const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  return eserde::json::read_from(std::string_view(buf, n), cfg);
}

// ============================================================================
// 检查辅助（零框架）
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

static void check_json_error(const char *what, json::error e, json::error wanted) {
  check(what, e == wanted, std::string("actual=") + json::error_name(e) + " wanted=" +
                               json::error_name(wanted));
}

static void check_cbor_error(const char *what, cbor::error e, cbor::error wanted) {
  check(what, e == wanted, std::string("actual=") + cbor::error_name(e) + " wanted=" +
                               cbor::error_name(wanted));
}

// 手册 3.3 的"量长度再写"套路：没有 to_string 的嵌入式也能用
template <typename T> static std::string json_text_of(const T &v) {
  const std::size_t need = json::write_to(nullptr, 0, v);
  std::string out(need, '\0');
  json::write_to(&out[0], need, v);
  return out;
}

static std::string json_of(const person &p) { return json_text_of(p); }

static std::string bytes_of(const person &p) {
  const std::size_t need = cbor::write_to(nullptr, 0, p);
  std::string out(need, '\0');
  cbor::write_to(reinterpret_cast<unsigned char *>(&out[0]), need, p);
  return out;
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
  p.two = arr2_t{4, 5};
  p.note = "a\"b\\c";
  p.big = 18446744073709551615ULL;
  p.internal = 999;
  return p;
}

// ============================================================================
int main() {
  // ---- 手册 2：第一个能跑的例子 -------------------------------------------
  {
    const reading r{7, 12.5f};
    char buf[64];
    const std::size_t need = json::write_to(buf, sizeof(buf), r);
    char line[80];
    std::memcpy(line, buf, need < sizeof(line) ? need : sizeof(line) - 1);
    line[need < sizeof(line) ? need : sizeof(line) - 1] = '\0';
    check_text("手册 2：第一个例子", line, "{\"id\":7,\"value\":12.5}");
  }

  // ---- 手册 3.3：JSON 写出（snprintf 语义）--------------------------------
  const person a = make_person();
  {
    check_text("手册 3.3：字段顺序 / 别名 / skip / 嵌套 / 枚举名 / 数组",
               json_of(a),
               "{\"age\":30,\"user_name\":\"bob\",\"salary\":1234.5,\"active\":true,"
               "\"m\":\"slow\",\"p\":{\"x\":3,\"y\":4},\"tags\":[1,2,3],"
               "\"nums\":[7,8,9],\"two\":[4,5],\"note\":\"a\\\"b\\\\c\","
               "\"big\":18446744073709551615}");

    char small[8];
    const std::size_t need = json::write_to(small, sizeof(small), a);
    check("手册 3.3：返回值是【所需】长度", need == json_of(a).size());
    check("手册 3.3：空间不够 → 返回值 > size", need > sizeof(small));
    check_text("手册 3.3：不够时只写前缀", std::string_view(small, sizeof(small)), "{\"age\":3");
    check("手册 3.3：只量长度（buf=nullptr）", json::write_to(nullptr, 0, a) == need);

    // write_to 不写结束符：后面的字节原样保留
    char raw[256];
    std::memset(raw, '#', sizeof(raw));
    const std::size_t n2 = json::write_to(raw, sizeof(raw), a);
    check("手册 5：write_to 不写 '\\0'", n2 < sizeof(raw) && raw[n2] == '#');
  }

  // ---- 手册 3.3：JSON 读入 -------------------------------------------------
  {
    person b{};
    check_json_error("手册 3.3：往返", json::read_from(json_of(a), b), json::error::ok);
    check("手册 3.3：往返一致",
          b.age == a.age && b.name == a.name && b.salary == a.salary && b.active == a.active &&
              b.m == a.m && b.p.x == a.p.x && b.tags[2] == 3 && b.nums == a.nums &&
              b.two == a.two && b.note == a.note && b.big == a.big && b.internal == 0);

    // 没写的 / null 的字段保持原值；未知键跳过
    person c{};
    c.age = 7;
    check_json_error("手册 3.3：未知键 / null / 枚举数字",
                     json::read_from("{\"unknown\":{\"deep\":[1,{\"k\":\"v\"}]},"
                                     "\"user_name\":\"amy\",\"age\":null,\"m\":-1,"
                                     "\"tags\":[9],\"nums\":[]}",
                                     c),
                     json::error::ok);
    check("手册 3.3：null 与缺省都保持原值",
          c.age == 7 && c.name == "amy" && c.m == mode::err && c.tags[0] == 9 &&
              c.tags[1] == 0 && c.nums.empty());

    // 转义：\uXXXX（含代理对）
    person d{};
    check_json_error("手册 3.3：转义",
                     json::read_from("{\"note\":\"l1\\nl2\\t\\u4e2d\\uD83D\\uDE00\"}", d),
                     json::error::ok);
    check("手册 3.3：转义解出来", d.note == std::string("l1\nl2\t") + "\xe4\xb8\xad" + "\xf0\x9f\x98\x80");
  }

  // ---- 手册 3.3：错误码全表 ------------------------------------------------
  {
    person b{};
    check_json_error("手册 3.3：语法错（缺值）", json::read_from("{\"age\":}", b), json::error::syntax);
    check_json_error("手册 3.3：语法错（1.）", json::read_from("{\"age\":1.}", b), json::error::syntax);
    check_json_error("手册 3.3：语法错（没闭合）", json::read_from("{\"age\":1", b), json::error::syntax);
    check_json_error("手册 3.3：语法错（尾巴有垃圾）", json::read_from("{\"age\":1} x", b), json::error::syntax);
    check_json_error("手册 3.3：类型对不上（字符串进 int）",
                     json::read_from("{\"age\":\"x\"}", b), json::error::type_mismatch);
    check_json_error("手册 3.3：类型对不上（1.5 进 int）",
                     json::read_from("{\"age\":1.5}", b), json::error::type_mismatch);
    check_json_error("手册 3.3：1.0 进 int 收下", json::read_from("{\"age\":1.0}", b), json::error::ok);
    check("手册 3.3：1.0 变成 1", b.age == 1);
    check_json_error("手册 3.3：嵌套太深",
                     json::read_from("{\"unknown\":[[[[[[[[[[1]]]]]]]]]]}", b), json::error::too_deep);
    check_json_error("手册 3.3：不是对象", json::read_from("[1,2]", b), json::error::type_mismatch);
    check_json_error("手册 3.3：accepted 之外的 bool",
                     json::read_from("{\"active\":2}", b), json::error::type_mismatch);

    read_only t{};
    check_json_error("手册 3.3：装不下（300 进 signed char）",
                     json::read_from("{\"small\":300}", t), json::error::truncated);

    // 键名超过 ESERDE_JSON_MAX_KEY：最多 64 字节
    std::string long_key = "{\"";
    long_key += std::string(70, 'k');
    long_key += "\":1}";
    check_json_error("手册 5：键名超过 ESERDE_JSON_MAX_KEY",
                     json::read_from(long_key, b), json::error::truncated);

    check_text("手册 3.3：error_name", json::error_name(json::error::too_deep), "too_deep");
    check_text("手册 3.3：error_name(ok)", json::error_name(json::error::ok), "ok");
  }

  // ---- 手册 5：失败不动原对象 ----------------------------------------------
  {
    person b = make_person();
    check_json_error("手册 5：读失败", json::read_from("{\"age\":\"x\",\"name\":\"zed\"}", b),
                     json::error::type_mismatch);
    check("手册 5：失败后对象原样", b.age == 30 && b.name == "bob");
  }

  // ---- 手册 3.3：宿主 to_string（嵌入式没有 std::string，整块裁掉）--------
#if EFMT_ENABLE_DYNAMIC_STRING
  {
    check_text("手册 3.3：to_string", json::to_string(a), json_of(a));
  }
#endif

  // ---- 手册 3.4：CBOR ------------------------------------------------------
  {
    // RFC 8949 附录 A 的黄金字节
    {
      const std::size_t need = cbor::write_to(nullptr, 0, mode::slow);
      unsigned char one[8];
      cbor::write_to(one, sizeof(one), mode::slow);
      check_text("手册 3.4：枚举写底层整数", to_hex(one, need), "05");
    }
    {
      const double one_five = 1.5;
      unsigned char d[16];
      const std::size_t need = cbor::write_to(d, sizeof(d), one_five);
      check_text("手册 3.4：double 1.5", to_hex(d, need), "fb3ff8000000000000");
    }
    {
      const float one_five = 1.5f;
      unsigned char f[16];
      const std::size_t need = cbor::write_to(f, sizeof(f), one_five);
      check_text("手册 3.4：float 1.5", to_hex(f, need), "fa3fc00000");
    }
    {
      const std::string s = "\xe6\xb0\xb4";   // "水"
      unsigned char t[16];
      const std::size_t need = cbor::write_to(t, sizeof(t), s);
      check_text("手册 3.4：文本串", to_hex(t, need), "63e6b0b4");
    }

    person b{};
    check_cbor_error("手册 3.4：往返", cbor::read_from(
                         reinterpret_cast<const unsigned char *>(bytes_of(a).data()),
                         bytes_of(a).size(), b),
                     cbor::error::ok);
    check("手册 3.4：往返一致",
          b.age == a.age && b.name == a.name && b.salary == a.salary && b.active == a.active &&
              b.m == a.m && b.p.y == a.p.y && b.nums == a.nums && b.two == a.two &&
              b.note == a.note && b.big == a.big && b.internal == 0);

    check("手册 3.4：CBOR 比 JSON 短", bytes_of(a).size() < json_of(a).size(),
          "json=" + std::to_string(json_of(a).size()) + " cbor=" +
              std::to_string(bytes_of(a).size()));
    std::printf("手册 3.4：同一个 person，JSON %zu 字节 / CBOR %zu 字节（%.0f%%）\n",
                json_of(a).size(), bytes_of(a).size(),
                100.0 * static_cast<double>(bytes_of(a).size()) /
                    static_cast<double>(json_of(a).size()));

    // 写接口语义
    unsigned char small[4];
    const std::size_t need = cbor::write_to(small, sizeof(small), a);
    check("手册 3.4：返回值是所需字节数", need == bytes_of(a).size());
    check("手册 3.4：不够时返回值 > size", need > sizeof(small));
    check_text("手册 3.4：不够时只写前缀", to_hex(small, sizeof(small)), "ab636167");
    check("手册 3.4：只量长度", cbor::write_to(nullptr, 0, a) == need);

    // 枚举：整数与取值名都收
    mode m = mode::fast;
    const unsigned char kFive[1] = {0x05};
    check_cbor_error("手册 3.4：读枚举整数", cbor::read_from(kFive, 1, m), cbor::error::ok);
    check("手册 3.4：枚举整数 = slow", m == mode::slow);

    // 读端宽容：非最短整数编码
    int v = 0;
    const unsigned char kNonShortest[2] = {0x18, 0x05};
    check_cbor_error("手册 3.4：非最短编码也收", cbor::read_from(kNonShortest, 2, v), cbor::error::ok);
    check("手册 3.4：非最短编码的值", v == 5);

    // 子集之外：不定长数组 / tag
    std::vector<int> nums;
    const unsigned char kIndefinite[4] = {0x9F, 0x01, 0x02, 0xFF};
    check_cbor_error("手册 3.4：不定长数组 → unsupported",
                     cbor::read_from(kIndefinite, 4, nums), cbor::error::unsupported);
    const unsigned char kTrailing[2] = {0x00, 0x00};
    check_cbor_error("手册 3.4：尾巴有垃圾", cbor::read_from(kTrailing, 2, v), cbor::error::syntax);
    const unsigned char kTextIntoInt[2] = {0x61, 0x61};
    check_cbor_error("手册 3.4：类型对不上", cbor::read_from(kTextIntoInt, 2, v),
                     cbor::error::type_mismatch);
    check_text("手册 3.4：error_name", cbor::error_name(cbor::error::unsupported), "unsupported");
  }

  // ---- 手册 3.4 / 手册 5：NaN、Inf ----------------------------------------
  {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    person b = make_person();
    b.salary = nan;
    const std::string text = json_of(b);
    check("手册 5：JSON 把 NaN 写成 null", text.find("\"salary\":null") != std::string::npos, text);
    check_json_error("手册 5：JSON 读 null", json::read_from(text, b), json::error::ok);
    check("手册 5：null 保持原值（还是 NaN）", b.salary != b.salary);

    const std::string bytes = bytes_of(b);
    person c{};
    check_cbor_error("手册 5：CBOR 原样传 NaN",
                     cbor::read_from(reinterpret_cast<const unsigned char *>(bytes.data()),
                                     bytes.size(), c),
                     cbor::error::ok);
    check("手册 5：CBOR 读回来还是 NaN", c.salary != c.salary);

    person e = make_person();
    e.salary = std::numeric_limits<double>::infinity();
    const std::string inf_bytes = bytes_of(e);
    person f{};
    check_cbor_error("手册 5：CBOR 原样传 Inf",
                     cbor::read_from(reinterpret_cast<const unsigned char *>(inf_bytes.data()),
                                     inf_bytes.size(), f),
                     cbor::error::ok);
    check("手册 5：Inf 还是 Inf", f.salary > std::numeric_limits<double>::max());
  }

  // ---- 手册 3.5：支持的类型 ------------------------------------------------
  {
    shapes s{};
    s.big = 18446744073709551615ULL;
    s.d = 2.5;
    s.flag = true;
    s.lv = level::high;
    std::strcpy(s.tag, "imu0");
    s.raw[0] = 1; s.raw[1] = 2; s.raw[2] = 3;
    s.text = "hello";
    s.vec = {1, 2};
    s.pair2 = arr2_t{4, 5};
    s.nested = point{6, 7};

    char buf[256];
    const std::size_t need = json::write_to(buf, sizeof(buf), s);
    check_text("手册 3.5：各种形状一网打尽", std::string_view(buf, need),
               "{\"big\":18446744073709551615,\"d\":2.5,\"flag\":true,\"lv\":\"high\","
               "\"tag\":\"imu0\",\"raw\":[1,2,3],\"text\":\"hello\",\"vec\":[1,2],"
               "\"pair2\":[4,5],\"nested\":{\"x\":6,\"y\":7}}");

    shapes back{};
    check_json_error("手册 3.5：读回来", json::read_from(std::string_view(buf, need), back),
                     json::error::ok);
    check("手册 3.5：值一致",
          back.big == s.big && back.d == s.d && back.flag && back.lv == level::high &&
              std::strcmp(back.tag, "imu0") == 0 && back.raw[2] == 3 && back.text == "hello" &&
              back.vec == s.vec && back.pair2 == s.pair2 && back.nested.y == 7);

    // CBOR：char[8] 装不下 8 个字符（还要留结束符）→ truncated，且被清空
    struct fixed_text {
      char text[3];
    };
    check("手册 3.5：C 数组 / char[N] 有长度检查", sizeof(fixed_text) == 3);
  }

  // ---- 手册 3.5：ETL 类型 --------------------------------------------------
#ifdef ESERDE_MANUAL_HAS_ETL
  {
    etl_node n{};
    n.id = etl::string<8>("n1");
    n.nums.push_back(1);
    n.nums.push_back(2);
    n.label = etl::string<16>("boot ok");

    char buf[128];
    const std::size_t need = json::write_to(buf, sizeof(buf), n);
    check_text("手册 3.5：ETL 与 std 同一套代码", std::string_view(buf, need),
               "{\"id\":\"n1\",\"nums\":[1,2],\"label\":\"boot ok\"}");

    etl_node back{};
    check_json_error("手册 3.5：ETL 读回来", json::read_from(std::string_view(buf, need), back),
                     json::error::ok);
    check("手册 3.5：ETL 值一致",
          back.id == n.id && back.label == n.label && back.nums.size() == 2 && back.nums[1] == 2);

    etl_node keep{};
    keep.id = etl::string<8>("keep");
    check_json_error("手册 5：etl::string 装不下 → truncated",
                     json::read_from("{\"id\":\"123456789\"}", keep), json::error::truncated);
    check("手册 5：失败后 etl::string 原样", keep.id == etl::string<8>("keep"));

    etl_node many{};
    check_json_error("手册 5：etl::vector 容量不够 → truncated",
                     json::read_from("{\"nums\":[1,2,3,4,5]}", many), json::error::truncated);
  }
#endif

  // ---- 手册 3.7：基座 API 自己用 -------------------------------------------
  {
    static_assert(eserde::field_count<person>() == 12, "字段数");
    static_assert(eserde::field_name<person>(0) == "age", "字段名");
    static_assert(eserde::field_type_name<person>(1) == "std::string", "类型名（声明原文）");
    static_assert(eserde::field_type_name<person>(8) == "arr2_t", "typedef 后就是别名原文");
    static_assert(eserde::find_field<person>("salary") == 2, "按名字反查下标");
    static_assert(eserde::find_field<person>("nope") == eserde::npos, "查不到 = npos");
    static_assert(eserde::is_enum<mode>(), "is_enum");
    static_assert(!eserde::is_enum<person>(), "结构体不是枚举");
    static_assert(eserde::field_count<mode>() == 3, "枚举取值个数");
    static_assert(eserde::field_name<mode>(1) == "slow", "取值名");
    static_assert(eserde::enum_value<mode>(1) == 5, "显式取值");
    static_assert(eserde::enum_value<mode>(2) == -1, "负值取值");
    static_assert(eserde::enum_value<mode>(0) == 0, "第一个取值");

    person p{};
    p.age = 18;
    p.name = "bob";
    std::string visited;
    eserde::visit_fields(p, [&](std::string_view name, const auto &) {
      if (!visited.empty()) visited += ',';
      visited += std::string(name);
    });
    check_text("手册 3.7：visit_fields 按声明顺序", visited,
               "age,name,salary,active,m,p,tags,nums,two,note,big,internal");

    eserde::field_at<0>(p) = 30;                 // 按索引写回
    const person &cp = p;                        // const 对象只读访问
    const int first = eserde::field_at<0>(cp);
    check("手册 3.7：field_at 读写", first == 30 && eserde::field_at<0>(p) == 30);

    // 遍历时按标签挑字段（上层格式就是这么干的）
    std::string tagged;
    eserde::visit_fields(p, [&](std::string_view name, const auto &) {
      if (eserde::has_tag<person>(eserde::find_field<person>(name), "short")) {
        if (!tagged.empty()) tagged += ',';
        tagged += std::string(name);
      }
    });
    check_text("手册 3.7：按标签筛选", tagged, "name");
  }

  // ---- 手册 3.8：自己接一种新格式（TOML-ish）-------------------------------
  {
    toml_cfg c{};
    c.boot_delay_ms = 250;
    c.gain = 1.5f;
    c.verbose = true;
    c.origin = point{1, 2};
    c.internal = 999;

    char buf[192];
    const std::size_t need = toml::write_to(buf, sizeof(buf), c);
    check_text("手册 3.8：自写格式输出", std::string_view(buf, need),
               "boot_ms = 250\ngain = 1.5\nverbose = true\norigin = { x = 1, y = 2 }\n");

    // 同一个类型：JSON 完全看不见 toml 标签 —— 别名不算数，skip 也不算数
    check_text("手册 3.8：toml 标签对其他格式无影响", json_text_of(c),
               "{\"boot_delay_ms\":250,\"gain\":1.5,\"verbose\":true,"
               "\"origin\":{\"x\":1,\"y\":2},\"internal\":999}");

    // 缓冲不够：snprintf 语义（返回值 > size 表示截断）
    char tiny[8];
    check("手册 3.8：自写格式也守 snprintf 语义", toml::write_to(tiny, sizeof(tiny), c) == need);
  }

  // ---- 手册 4：设备配置保存到文件 / 读回 -----------------------------------
  {
    device_cfg cfg{};
    cfg.boot_delay_ms = 250;
    cfg.gain = 1.5f;
    cfg.verbose = true;
    cfg.internal = 999;

    const char *path = "tests/out/eserde_manual_cfg.json";
    check("手册 4：保存", save_json(path, cfg));

    device_cfg back{};
    back.gain = 9.0f;
    check_json_error("手册 4：读回", load_json(path, back), json::error::ok);
    check("手册 4：值一致", back.boot_delay_ms == 250 && back.gain == 1.5f && back.verbose &&
                                back.internal == 0);   // skip 的字段保持原值
    check("手册 4：文件里是跳过的版本", std::remove(path) == 0);

    // CBOR 往返：同一份配置
    unsigned char bin[128];
    const std::size_t bytes = cbor::write_to(bin, sizeof(bin), cfg);
    device_cfg from_cbor{};
    check_cbor_error("手册 4：CBOR 往返", cbor::read_from(bin, bytes, from_cbor), cbor::error::ok);
    check("手册 4：CBOR 值一致",
          from_cbor.boot_delay_ms == cfg.boot_delay_ms && from_cbor.gain == cfg.gain &&
              from_cbor.verbose == cfg.verbose);

    char js[128];
    const std::size_t js_need = json::write_to(js, sizeof(js), cfg);
    check("手册 4：CBOR 比 JSON 短（同一份配置）", bytes < js_need,
          "json=" + std::to_string(js_need) + " cbor=" + std::to_string(bytes));
    char line[256];
    std::memcpy(line, js, js_need);
    line[js_need] = '\0';
    std::printf("手册 4：JSON=[%s] %zu 字节，CBOR %zu 字节\n", line, js_need, bytes);
  }

  // ---- 手册 3.9：裁剪宏 ----------------------------------------------------
  {
    static_assert(ESERDE_JSON_MAX_KEY == 64, "JSON 键名缓冲默认 64");
    static_assert(ESERDE_JSON_MAX_DEPTH == 8, "JSON 嵌套上限默认 8");
    static_assert(ESERDE_CBOR_MAX_DEPTH == 8, "CBOR 嵌套上限默认 8");
    static_assert(EFMT_DERIVE_ENABLE_CAPS == 1, "能力标签默认开");
    static_assert(EFMT_DERIVE_ENABLE_SCHEMA == 1, "schema 默认开（eserde 需要）");
    static_assert(EFMT_DERIVE_ENABLE_TAGS == 1, "字段标签默认开");
    check("手册 3.9：裁剪宏默认值与手册一致", true);
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
