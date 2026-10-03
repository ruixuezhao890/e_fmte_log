/**
 ******************************************************************************
 * @file           : efmt_derive_auto_check.cpp
 * @brief          : E_FMT_DERIVE（声明即推导）的行为检查
 * @attention      : 对应 Rust 的 #[derive(Debug)]：声明结构体/枚举时字段名、取值名
 *                   一个字都不用写。这里逐条钉住输出、边界与容错。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace e_fmt;

// ============================================================================
// 被测类型：全部只写声明，不写任何字段名
// ============================================================================
E_FMT_DERIVE(struct imu {
  float ax;                  // 一行一个字段：宏的第一个参数是声明本身，
  float ay;                  // 声明里的顶层逗号会把宏参数切断（枚举走 E_FMT_DERIVE_ENUM）
  float az;
});

E_FMT_DERIVE(struct one_field {
  int only;
});

E_FMT_DERIVE(struct full16 {
  int a; int b; int c; int d; int e; int f; int g; int h;
  int i; int j; int k; int l; int m; int n; int o; int p;
});

E_FMT_DERIVE(struct with_bits {
  unsigned seq;
  unsigned flags : 3;        // 位域：GCC 下 std::tie 会打出 0，这里必须是对的
  int gain = 2;              // 默认成员初始化
  static const int kMax = 9; // 静态成员：不算字段
  int raw() const { return (int)seq; }   // 成员函数：不算字段
});

struct raw_pair { int lo; int hi; };      // 故意不推导

E_FMT_DERIVE(struct nested {
  imu sample;                // 内层也推导了
  raw_pair pair;             // 内层没推导 → 位置式
  int n;
});

E_FMT_DERIVE(struct with_arrays {
  int ints[3];
  float floats[2];
  char name[8];
  int many[10];              // 超过 8 个 → 省略号
});

E_FMT_DERIVE(struct with_fnptr {
  int id;
  void (*callback)(int);     // 函数指针成员：要算字段
});

namespace app {
E_FMT_DERIVE(struct cfg {
  int retry;
  bool verbose;
});
}  // namespace app

// ---- 类型内一行 E_FMT_FIELDS：E_FMT_DERIVE 覆盖不到的场合 ----
struct manual_fields {
  int retry;
  bool verbose;
  E_FMT_FIELDS(retry, verbose);          // 只列名字
};

struct with_conditional {
  int base;
#ifdef EFMT_TEST_EXTRA_FIELD
  int extra;
  E_FMT_FIELDS(base, extra);
#else
  E_FMT_FIELDS(base);                    // 声明里有 #if → E_FMT_DERIVE 做不到，这里可以
#endif
};

template <typename T>                    // 模板结构体 → E_FMT_DERIVE 做不到，这里可以
struct box {
  T value;
  int tag;
  E_FMT_FIELDS(value, tag);
};

E_FMT_DERIVE(struct holds_manual {       // 两种写法可以互相嵌套
  manual_fields mf;
  box<imu> wrapped;
  int n;
});

// ---- 限定名成员：'::' 不能被当成位域分隔符，字段名要取到最后一个标识符 ----
namespace detail_payload {
E_FMT_DERIVE(struct payload {
  int v;
});
}  // namespace detail_payload

E_FMT_DERIVE(struct with_qualified {
  detail_payload::payload body;
  int n;
});

#if EFMT_ENABLE_DYNAMIC_STRING
E_FMT_DERIVE(struct with_string_member {
  std::string label;
  int count;
});
#endif

// 字段标签：属性不能把字段弄丢（旧实现里含 '(' 的语句会被当成成员函数跳过），
// 标签要能在编译期解析出来 —— 这是 eserde 之类上层的原料。
E_FMT_DERIVE(struct with_tags {
  int age;
  [[efmt::arg(short, long)]]
  int level;
  int extra [[maybe_unused]];
});

E_FMT_DERIVE_ENUM(enum class state {
  idle,
  busy = 5,
  fault,                     // 自增：6
  down = -2
});

E_FMT_DERIVE_ENUM(enum class code : unsigned char {
  ok = 0,
  warn = 0x10,
  fail = 200
});

// ============================================================================
// 极简断言
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

template <typename... Args>
static std::string text(std::string_view fmt_str, const Args &...args) {
  char buffer[256];
  const size_t needed = format_to(buffer, sizeof(buffer), fmt_str, args...);
  return std::string(buffer, (needed < sizeof(buffer)) ? needed : sizeof(buffer) - 1);
}

static void check_text(const char *what, const std::string &actual, const char *expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  ++g_failures;
  std::printf("FAIL %s\n     actual  =[%s]\n     expected=[%s]\n", what, actual.c_str(),
              expected);
}

#define CHECK_TEXT(actual, expected) check_text(#actual, (actual), (expected))

int main() {
  // ---- 基本形态 ----
  CHECK_TEXT(text("{}", imu{1.5f, 2.5f, 3.5f}), "imu { ax = 1.5, ay = 2.5, az = 3.5 }");
  CHECK_TEXT(text("{}", one_field{7}), "one_field { only = 7 }");
  CHECK_TEXT(text("{}", full16{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}),
             "full16 { a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9, j = 10, "
             "k = 11, l = 12, m = 13, n = 14, o = 15, p = 16 }");

  // ---- 位域 / 默认值 / 静态成员 / 成员函数 ----
  with_bits b{};
  b.seq = 7;
  b.flags = 3;
  CHECK_TEXT(text("{}", b), "with_bits { seq = 7, flags = 3, gain = 2 }");

  // ---- 嵌套（内层推导 / 内层没推导）----
  CHECK_TEXT(text("{}", nested{imu{1.0f, 2.0f, 3.0f}, raw_pair{4, 5}, 6}),
             "nested { sample = imu { ax = 1, ay = 2, az = 3 }, pair = raw_pair (4, 5), n = 6 }");

  // ---- 数组 ----
  with_arrays a{};
  a.ints[0] = 1; a.ints[1] = 2; a.ints[2] = 3;
  a.floats[0] = 1.5f; a.floats[1] = 2.5f;
  std::strncpy(a.name, "imu0", sizeof(a.name) - 1);
  for (int i = 0; i < 10; ++i) a.many[i] = i;
  CHECK_TEXT(text("{}", a),
             "with_arrays { ints = [1, 2, 3], floats = [1.5, 2.5], name = imu0, "
             "many = [0, 1, 2, 3, 4, 5, 6, 7, ...] }");

  // ---- 函数指针成员 ----
  with_fnptr fn{};
  fn.id = 3;
  fn.callback = nullptr;
  CHECK_TEXT(text("{}", fn), "with_fnptr { id = 3, callback = (nil) }");

  // ---- 类型内一行 E_FMT_FIELDS ----
  CHECK_TEXT(text("{}", manual_fields{3, true}), "manual_fields { retry = 3, verbose = 1 }");
  CHECK_TEXT(text("{}", with_conditional{5}), "with_conditional { base = 5 }");
  CHECK_TEXT(text("{}", box<int>{7, 2}), "box<int> { value = 7, tag = 2 }");
  CHECK_TEXT(text("{}", box<imu>{imu{1.0f, 2.0f, 3.0f}, 2}),
             "box<imu> { value = imu { ax = 1, ay = 2, az = 3 }, tag = 2 }");
  CHECK_TEXT(text("{}", holds_manual{manual_fields{1, false}, box<imu>{imu{4.0f, 5.0f, 6.0f}, 2}, 9}),
             "holds_manual { mf = manual_fields { retry = 1, verbose = 0 }, "
             "wrapped = box<imu> { value = imu { ax = 4, ay = 5, az = 6 }, tag = 2 }, n = 9 }");

  // ---- 命名空间里的类型 ----
  CHECK_TEXT(text("{}", app::cfg{3, true}), "app::cfg { retry = 3, verbose = 1 }");

  // ---- 限定名成员（::）----
  CHECK_TEXT(text("{}", with_qualified{detail_payload::payload{5}, 2}),
             "with_qualified { body = detail_payload::payload { v = 5 }, n = 2 }");
#if EFMT_ENABLE_DYNAMIC_STRING
  CHECK_TEXT(text("{}", with_string_member{std::string("imu"), 3}),
             "with_string_member { label = imu, count = 3 }");
#endif

  // ---- 字段标签：带属性的字段照样是字段，标签也能解析出来 ----
  CHECK_TEXT(text("{}", with_tags{18, 3, 1}), "with_tags { age = 18, level = 3, extra = 1 }");
  {
    constexpr auto s = ::e_fmt::detail::parse_derived_schema<EFMT_DERIVE_MAX_FIELDS>(
        "struct with_tags { int age; [[efmt::arg(short, long)]] int level;"
        " int extra [[maybe_unused]]; }");
    static_assert(s.parsed && s.count == 3, "带属性的声明照样解析出 3 个字段");
    static_assert(s.field(1).name == "level" && s.field(1).type_name == "int", "字段名/类型名");
    static_assert(s.tags(1).valid && s.tags(1).count == 2, "两个标签");
    static_assert(s.tags(1).items[0].name == "short" && s.tags(1).items[1].name == "long", "标签名");
    static_assert(s.field(2).name == "extra" && s.field(2).has_attrs, "尾部属性不影响字段名");
  }

  // ---- 样式：{:#} 多行缩进（EFMT_DERIVE_STYLE_MULTILINE 默认开）----
  CHECK_TEXT(text("{:#}", imu{1.5f, 2.5f, 3.5f}),
             "imu {\n  ax = 1.5,\n  ay = 2.5,\n  az = 3.5\n}");
  CHECK_TEXT(text("{:#}", one_field{7}), "one_field {\n  only = 7\n}");
  // 嵌套成员固定单行（缩进不乱），只有顶层多行
  CHECK_TEXT(text("{:#}", nested{imu{1.0f, 2.0f, 3.0f}, raw_pair{4, 5}, 6}),
             "nested {\n  sample = imu { ax = 1, ay = 2, az = 3 },\n  pair = raw_pair (4, 5),\n  n = 6\n}");
  // 类型内一行 E_FMT_FIELDS 同样支持
  CHECK_TEXT(text("{:#}", manual_fields{3, true}), "manual_fields {\n  retry = 3,\n  verbose = 1\n}");

  // ---- 枚举：取值 → 名字；未列出的取值 → 底层整数 ----
  // EFMT_DERIVE_SHOW_TYPE=1（默认）时像结构体一样带类型名前缀：Rust #[derive(Debug)] 风格
  CHECK_TEXT(text("{}", state::idle), "state::idle");
  CHECK_TEXT(text("{}", state::busy), "state::busy");
  CHECK_TEXT(text("{}", state::fault), "state::fault");
  CHECK_TEXT(text("{}", state::down), "state::down");
  CHECK_TEXT(text("{}", static_cast<state>(99)), "state::99");
  CHECK_TEXT(text("{}", static_cast<state>(-2)), "state::down");
  CHECK_TEXT(text("{:>8}", state::busy), "state::    busy");   // 前缀不进对齐，对齐只作用于名字
  CHECK_TEXT(text("{}", code::ok), "code::ok");
  CHECK_TEXT(text("{}", code::warn), "code::warn");
  CHECK_TEXT(text("{}", code::fail), "code::fail");
  CHECK_TEXT(text("{}", static_cast<code>(77)), "code::77");

  // ---- 和其它类型混用、可重复使用 ----
  CHECK_TEXT(text("imu={} state={} n={}", imu{1.0f, 2.0f, 3.0f}, state::fault, 42),
             "imu=imu { ax = 1, ay = 2, az = 3 } state=state::fault n=42");

  // ---- 走 print 系列 ----
  {
    char sink[256];
    set_buffer_output(sink, sizeof(sink));
    println_info("{}", one_field{9});
    const size_t used = get_buffer_output_pos();
    sink[used] = '\0';
    ++g_checks;
    if (std::strstr(sink, "one_field { only = 9 }") == nullptr) {
      ++g_failures;
      std::printf("FAIL println_info -> [%s]\n", sink);
    }
    reset_output_handler();
  }

  // ---- 截断语义不受影响 ----
  {
    char tiny[8];
    const size_t needed = format_to(tiny, sizeof(tiny), "{}", imu{1.0f, 2.0f, 3.0f});
    ++g_checks;
    if (needed < sizeof(tiny)) {
      ++g_failures;
      std::printf("FAIL 截断判定：needed=%zu\n", needed);
    }
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
