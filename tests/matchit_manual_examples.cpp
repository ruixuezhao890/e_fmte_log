/**
 ******************************************************************************
 * @file           : matchit_manual_examples.cpp
 * @brief          : 手册里的示例代码必须有可编译版本（防止文档腐烂）
 * @attention      : 手册 docs/libs/MATCHIT-使用手册.md 里的每一段示例都在这里编译并跑断言；
 *                   片段与实现脱节时，这里先红。分段注释标出对应的手册小节。
 *
 *                   单独跑（run_check.ps1 里接上之前用这条）：
 *                     g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I. \
 *                         tests/matchit_manual_examples.cpp -o tests/out/matchit_manual_examples.exe
 *                     ./tests/out/matchit_manual_examples.exe
 *
 *                   另外两种配置也应当能编：
 *                     -fno-exceptions                  （手册 5 坑 2：无异常构建）
 *                     -DECLI_ENABLE_PATTERN_COMMANDS=0 （手册 4.3：裁剪版，模式段当字面量）
 ******************************************************************************
 */

#include <matchit/matchit.h>
#include <ecli/command.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

using namespace e_fmt;
using namespace ecli;

// ============================================================================
// 断言脚手架（照 tests/ecli_pattern_check.cpp 的写法：不引测试框架）
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

// ============================================================================
// 手册 2：第一个能跑的例子（match(值)( 模式 = 结果, … )）
// ============================================================================
static const char *level_text(int n) {
  using namespace matchit;
  return match(n)(
      // clang-format off
      pattern | 0                    = "0：关",
      pattern | and_(_ >= 1, _ <= 9) = "1..9：开",
      pattern | _                    = "超出 0..9"
      // clang-format on
  );
}

// 手册 2 的「完整程序」：手册里的 int main() 就是这三行（这里包成函数好被 main 调）
static void first_example_main() {
  std::printf("%s\n", level_text(0));    // 0：关
  std::printf("%s\n", level_text(5));    // 1..9：开
  std::printf("%s\n", level_text(99));   // 超出 0..9
}

// ============================================================================
// 手册 3.1：返回值 —— 表达式形式返回分支的值，语句形式不需要兜底
// ============================================================================
static const char *traffic_light(int n) {
  using namespace matchit;
  return match(n)(pattern | 0 = "红", pattern | 1 = "黄", pattern | _ = "绿");
}

static int side_effect_only(int n) {
  using namespace matchit;
  int hits = 0;
  // 所有分支都返回 void → 语句形式：一条都没匹配上也不报错（也不抛）
  match(n)(pattern | 1 = [&] { hits = 1; }, pattern | 2 = [&] { hits = 2; });
  return hits;
}

// ============================================================================
// 手册 3.2：字面量与通配 _
// ============================================================================
static int segment_kind(std::string_view seg) {
  using namespace matchit;
  return match(seg)(
      pattern | "" = 0,        // 字面量：与值相等才命中
      pattern | "wifi" = 1,
      pattern | _ = 2);        // 通配：什么都命中，也当兜底用
}

// ============================================================================
// 手册 3.3：组合 —— 比较（_ >= x）、and_、or_、not_
// ============================================================================
static const char *level_bucket(int n) {
  using namespace matchit;
  return match(n)(
      pattern | 0 = "关",
      pattern | and_(_ >= 1, _ <= 9) = "1..9",
      pattern | or_(10, 20, 30) = "10/20/30",
      pattern | not_(_ < 0) = "其它非负",
      pattern | _ = "负数");
}

// ============================================================================
// 手册 3.4：谓词模式 meet(...)
// ============================================================================
// 谓词得是【类类型】（有 operator() 的东西）：lambda 或仿函数。
// 传裸函数名 / 函数指针会撞上 matchit 里 `class Meet : public Pred` —— 编不过。
struct is_even_fn {
  bool operator()(int v) const { return v % 2 == 0; }
};

static const char *parity(int n) {
  using namespace matchit;
  return match(n)(
      pattern | meet(is_even_fn{}) = "偶",
      pattern | (_ % 2 != 0) = "奇",   // 比较/算术写法：让 _ 参与运算就得到谓词模式
      pattern | _ = "?");
}

// ============================================================================
// 手册 3.5：提取器 app(提取器, 模式) + some(...)
//   这就是本仓库判定"一个 token 是不是一个段"的那套（见 4.1）
// ============================================================================
struct digit_head {
  // 命中就交出东西（这里交出 token 本身），不命中就 nullopt
  std::optional<std::string_view> operator()(std::string_view token) const {
    if (!token.empty() && token[0] >= '0' && token[0] <= '9') return token;
    return std::nullopt;
  }
};

static bool is_number_token(std::string_view token) {
  using namespace matchit;
  return match(token)(
      pattern | app(digit_head{}, some(_)) = true,   // some(_)：拿到提取器交出来的东西
      pattern | _ = false);
}

// ============================================================================
// 手册 3.6：绑定 Id<T>（记得别声明成 const）
// ============================================================================
static void id_binding_demo() {
  using namespace matchit;
  Id<int> hit;
  int v = 42;
  const bool ok = match(v)(pattern | hit = true, pattern | _ = false);
  check("Id<T> 绑定：命中后用 *hit 取值", ok && *hit == 42);
  check("Id<T> 里存的是【指针】：*hit 就是被匹配的那个对象", &*hit == &v);
}

// ============================================================================
// 手册 3.7：多值 / tuple —— match(a, b) 配 ds(...)
// ============================================================================
static int quadrant(int x, int y) {
  using namespace matchit;
  return match(x, y)(
      pattern | ds(0, 0) = 0,
      pattern | ds(_, 0) = 1,
      pattern | ds(0, _) = 2,
      pattern | _ = 3);
}

static bool is_pair_1_2(const std::tuple<int, int> &t) {
  using namespace matchit;
  return match(t)(pattern | ds(1, 2) = true, pattern | _ = false);
}

// ============================================================================
// 手册 3.8：结构体成员 —— dsVia(&T::member, ...)
// ============================================================================
struct point2 {
  int x;
  int y;
};

static bool is_origin(const point2 &p) {
  using namespace matchit;
  return match(p)(pattern | dsVia(&point2::x, &point2::y)(0, 0) = true, pattern | _ = false);
}

// ============================================================================
// 手册 3.9：范围 + ooo + 中间那段（Subrange）
// ============================================================================
static void range_demo() {
  using namespace matchit;
  std::array<int, 5> arr{1, 2, 3, 4, 5};
  Id<SubrangeT<std::array<int, 5>>> mid;
  int size = -1;
  // 每条分支都返回 bool → 表达式形式可以接住结果（有一条返回 void 就得走语句形式）
  const bool ok = match(arr)(
      pattern | ds(1, mid.at(ooo), 5) = [&] {
        size = static_cast<int>((*mid).size());
        return true;
      },
      pattern | _ = [&] {
        size = -1;
        return false;
      });
  check("ds(1, ooo, 5)：两头固定、中间那段被绑定", ok && size == 3, std::to_string(size));
  const std::array<int, 3> want{2, 3, 4};
  check("绑定到的 Subrange 可以当面遍历",
        std::equal((*mid).begin(), (*mid).end(), want.begin()));
}

// ============================================================================
// 手册 3.10：守卫 when(...)（跟主谓模式 Id 配合）
// ============================================================================
static const char *sign_of(int n) {
  using namespace matchit;
  Id<int> v;
  return match(n)(
      pattern | v | when([&] { return *v == 0; }) = "零",
      pattern | v | when([&] { return *v > 0; }) = "正",
      pattern | _ = "负");
}

// ============================================================================
// 手册 3.11：没兜底会怎样（上游：抛 std::logic_error；无异常构建：MATCHIT_NO_MATCH）
// ============================================================================
static void no_fallback_demo() {
#if MATCHIT_USE_EXCEPTIONS
  bool threw = false;
  try {
    (void)matchit::match(99)(matchit::pattern | 1 = 0);
  } catch (const std::logic_error &) {
    threw = true;
  }
  check("有异常构建：一条都没匹配上 → 抛 std::logic_error（上游行为）", threw);
#else
  check("无异常构建：MATCHIT_USE_EXCEPTIONS 自动探测为 0",
        MATCHIT_USE_EXCEPTIONS == 0);
#endif
}

// ============================================================================
// 手册 3.12：上游还有这些（本仓库没接；要用就在这儿加一条检查，别让它烂掉）
// ============================================================================
static int optional_state(const std::optional<int> &o) {
  using namespace matchit;
  return match(o)(pattern | none = 0, pattern | _ = 1);   // none：空 optional / 空指针
}

static int variant_tag(const std::variant<int, std::string> &v) {
  using namespace matchit;
  return match(v)(
      pattern | as<int>(_) = 1, pattern | as<std::string>(_) = 2, pattern | _ = 0);
}

struct pair_t {
  int a;
  int b;
};

static int pair_via_variant(const std::variant<pair_t, int> &v) {
  using namespace matchit;
  return match(v)(
      pattern | asDsVia<pair_t>(&pair_t::a, &pair_t::b)(1, 2) = 5, pattern | _ = 0);
}

static bool matched_helper(int n) { return matchit::matched(n, matchit::or_(1, 5, 9)); }

// ============================================================================
// 手册 4.1：本仓库的接缝 —— ecli/command.hpp 的 segment_extractor / match_segment
// ============================================================================
static void seam_demo() {
  std::string_view captured;
#if ECLI_ENABLE_PATTERN_COMMANDS
  check(":ssid 段：任意 token 都命中，并把 token 交回来",
        ecli::detail::match_segment(":ssid", "home", captured) && captured == "home");
  check("*rest 段：同样吃任意 token",
        ecli::detail::match_segment("*rest", "x", captured) && captured == "x");
  check("字面量段：与 token 相等才命中", ecli::detail::match_segment("wifi", "wifi", captured));
  check("字面量段：不等就不命中", !ecli::detail::match_segment("wifi", "lan", captured));
#else
  check("裁剪版：:ssid 当字面量，只跟同名 token 命中",
        !ecli::detail::match_segment(":ssid", "home", captured) &&
            ecli::detail::match_segment(":ssid", ":ssid", captured));
#endif
}

// ============================================================================
// 手册 4.2：端到端小工具 —— 命令表 + 模式段 + 回复写进定长缓冲
// ============================================================================
E_FMT_DERIVE(struct net_set_args {
  [[efmt::arg(skip)]] std::string ssid;   // 由 "net set :ssid" 注入
}, Cli);

E_FMT_DERIVE(struct log_args_t {
  [[efmt::arg(skip)]] std::vector<std::string> rest;   // 由 "log *rest" 逐个 push
}, Cli);

E_FMT_DERIVE(struct plain_args {
  [[efmt::arg(short = "v", long = "verbose", help = "多说两句")]] bool verbose = false;
}, Cli);

// efmt 格式化 + 回给提问的那一路（跟 sandbox/main.cpp 里的 replyf 同形）
template <typename... A>
static void replyf(reply out, std::string_view fmt, const A &...args) {
  char buf[384];
  const std::size_t n = e_fmt::format_to(buf, sizeof(buf), fmt, args...);
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
  if (n >= sizeof(buf)) out.put_lit("...(truncated)\n");
}

static std::string g_trace;

static void net_run(const plain_args &, reply out) {
  g_trace = "net";
  out.put_lit("net: 概览\n");
}
static void net_set_run(const net_set_args &a, reply out) {
  g_trace = "net-set:" + a.ssid;
  replyf(out, "ssid -> {}\n", a.ssid);
}
static void net_rest_run(const plain_args &, reply out) {
  g_trace = "net-rest";
  out.put_lit("net: 未知子命令\n");
}
static void log_run(const log_args_t &a, const params &p, reply out) {
  g_trace = "log:";
  for (std::size_t i = 0; i < a.rest.size(); ++i) {
    g_trace += a.rest[i];
    g_trace += "|";
  }
  g_trace += "rest_count=" + std::to_string(p.rest_count(0));
  replyf(out, "{} 行\n", a.rest.size());
}

static constexpr command kTool[] = {
    {"net", "net 概览", command_of<plain_args, net_run>(), help_of<plain_args>()},
    {"net set :ssid", "设置 ssid", command_of<net_set_args, net_set_run>(), help_of<net_set_args>()},
    {"net *rest", "未知子命令", command_of<plain_args, net_rest_run>(), help_of<plain_args>()},
    {"log *rest", "写日志", command_of<log_args_t, log_run>(), help_of<log_args_t>()},
};

static char g_reply[512];

template <std::size_t N>
static error run_line(const command (&table)[N], const char *line, std::string &text) {
  static char scratch[ECLI_MAX_LINE];
  buffer_reply b{g_reply, sizeof(g_reply), 0};
  const error e = dispatch(table, line, scratch, sizeof(scratch), b.as_reply());
  text.assign(g_reply, b.used < sizeof(g_reply) ? b.used : sizeof(g_reply) - 1);
  return e;
}

static void tool_demo() {
  std::string text;
  g_trace.clear();
#if ECLI_ENABLE_PATTERN_COMMANDS
  check("net set home：最具体的那条命令赢",
        run_line(kTool, "net set home", text) == error::ok && g_trace == "net-set:home", g_trace);
  check("捕获值进了回复文本", text == "ssid -> home\n", text);
  check("net：短的那条照旧", run_line(kTool, "net", text) == error::ok && g_trace == "net", g_trace);
  check("net xyz：落给 *rest 兜底", run_line(kTool, "net xyz", text) == error::ok && g_trace == "net-rest", g_trace);
  check("log a b c：*rest 全收并逐个 push",
        run_line(kTool, "log a b c", text) == error::ok && g_trace == "log:a|b|c|rest_count=3", g_trace);
  check("log：*rest 收 0 个也算命中",
        run_line(kTool, "log", text) == error::ok && g_trace == "log:rest_count=0", g_trace);
  check("help net set：前缀命中模式命令并打它的 usage",
        run_line(kTool, "help net set", text) == error::help_requested &&
            text.find("usage: net set :ssid") != std::string::npos,
        text);
#else
  check("裁剪版：:ssid / *rest 当字面量段比，捕获值为空",
        run_line(kTool, "net set :ssid", text) == error::ok && g_trace == "net-set:", g_trace);
  check("裁剪版：net set home 只命中 net，多出的 token 由参数解析报错",
        run_line(kTool, "net set home", text) == error::too_many_args);
#endif
}

// ============================================================================
// 手册 5 坑 1：Id<T&> 存的是指针 —— 绑到会死的对象上就悬垂
// ============================================================================
struct lifetime_probe {
  bool *alive;
  int tag;
  lifetime_probe(bool *a, int t) : alive(a), tag(t) { *a = true; }
  ~lifetime_probe() { *alive = false; }
  // Id<T> 要求 T 能比相等（上游 IdTraits<T>::equal 就是 lhs == rhs）
  bool operator==(const lifetime_probe &other) const { return tag == other.tag; }
};

static bool id_points_at_a_dead_object() {
  bool alive = false;
  matchit::Id<lifetime_probe &> id;
  {
    lifetime_probe p{&alive, 7};
    const bool ok = matchit::match(p)(
        matchit::pattern | id = true, matchit::pattern | matchit::_ = false);
    if (!ok || &*id != &p) return false;   // 表达式内：*id 就是 p 本身
  }
  // 出了块 p 已经析构 —— 此时再读 *id 就是悬垂读（手册里讲的坑就是这个）。
  // 这里【不】读它，只报告"Id 指着的那个对象已经没了"。
  return !alive;
}

// 坑的绕法：按值持有的模式可以存下来复用；按引用捕获临时量的比较模式不能。
//   auto p = matchit::or_(1, 2, 3);   // 值存在模式里 → 安全
//   auto q = matchit::meet(is_even_fn{});   // 仿函数按值存 → 安全
//   auto r = matchit::_ >= 1;         // 里面按住临时量 1 的引用 → 出语句就悬垂（GCC 会告 -Wuninitialized）
static bool value_held_patterns_can_be_stored() {
  auto p = matchit::or_(1, 2, 3);
  auto q = matchit::meet(is_even_fn{});
  using namespace matchit;
  const bool a = match(2)(pattern | p = true, pattern | _ = false);
  const bool b = match(2)(pattern | q = true, pattern | _ = false);
  return a && b;
}

// ============================================================================
// 手册 5 坑 3：ARM 上 int32_t 不是 int（本机复现不了，只能如实报告平台事实）
// ============================================================================
static_assert(sizeof(std::int32_t) == sizeof(int), "int32_t 与 int 同宽");

int main() {
  // ---- 手册 2 ----
  first_example_main();   // 手册里那段完整程序的输出（三行）
  check("level_text(0)", std::string(level_text(0)) == "0：关", level_text(0));
  check("level_text(5)", std::string(level_text(5)) == "1..9：开", level_text(5));
  check("level_text(99)", std::string(level_text(99)) == "超出 0..9", level_text(99));
#if defined(__cpp_exceptions)
  constexpr int kExpectUseExceptions = 1;   // 有异常构建：与上游行为一致（该抛就抛）
#else
  constexpr int kExpectUseExceptions = 0;   // -fno-exceptions：失败宏顶替 throw
#endif
  check("MATCHIT_USE_EXCEPTIONS 自动探测（有异常 1 / -fno-exceptions 0）",
        MATCHIT_USE_EXCEPTIONS == kExpectUseExceptions);

  // ---- 手册 3.1 ----
  check("表达式形式返回值", std::string(traffic_light(1)) == "黄", traffic_light(1));
  check("语句形式（全 void）不匹配也不报错", side_effect_only(7) == 0);
  check("语句形式命中的那条跑了", side_effect_only(2) == 2);

  // ---- 手册 3.2 ----
  check("字面量段命中空串", segment_kind("") == 0);
  check("字面量段命中 wifi", segment_kind("wifi") == 1);
  check("其它落给通配 _", segment_kind("lan") == 2);

  // ---- 手册 3.3 ----
  check("区间模式", std::string(level_bucket(5)) == "1..9", level_bucket(5));
  check("or_ 集合", std::string(level_bucket(20)) == "10/20/30", level_bucket(20));
  check("not_ 取反", std::string(level_bucket(100)) == "其它非负", level_bucket(100));
  check("兜底", std::string(level_bucket(-1)) == "负数", level_bucket(-1));

  // ---- 手册 3.4 ----
  check("meet(具名谓词)", std::string(parity(4)) == "偶", parity(4));
  check("_ 参与运算（% / !=）", std::string(parity(5)) == "奇", parity(5));

  // ---- 手册 3.5 ----
  check("app + some：提取器命中", is_number_token("12"));
  check("app + some：提取器不命中（空 token）", !is_number_token(""));
  check("app + some：提取器不命中（字母开头）", !is_number_token("ab"));

  // ---- 手册 3.6 ----
  id_binding_demo();

  // ---- 手册 3.7 ----
  check("多值 ds(0, 0)", quadrant(0, 0) == 0);
  check("多值 ds(_, 0)", quadrant(9, 0) == 1);
  check("多值 ds(0, _)", quadrant(0, 9) == 2);
  check("多值兜底", quadrant(9, 9) == 3);
  check("tuple 值配 ds", is_pair_1_2(std::tuple<int, int>{1, 2}));
  check("tuple 值不匹配落兜底", !is_pair_1_2(std::tuple<int, int>{1, 3}));

  // ---- 手册 3.8 ----
  check("dsVia 命中字段全 0", is_origin(point2{0, 0}));
  check("dsVia 不命中", !is_origin(point2{0, 1}));

  // ---- 手册 3.9 ----
  range_demo();

  // ---- 手册 3.10 ----
  check("when 守卫：零", std::string(sign_of(0)) == "零", sign_of(0));
  check("when 守卫：正", std::string(sign_of(7)) == "正", sign_of(7));
  check("when 守卫：负", std::string(sign_of(-3)) == "负", sign_of(-3));

  // ---- 手册 3.11 ----
  no_fallback_demo();

  // ---- 手册 3.12 ----
  check("none：空 optional 命中", optional_state(std::nullopt) == 0);
  check("none：有值就不命中", optional_state(std::optional<int>{7}) == 1);
  check("as<T>：按 variant 里当前是哪个类型挑分支",
        variant_tag(std::variant<int, std::string>{42}) == 1 &&
            variant_tag(std::variant<int, std::string>{std::string("s")}) == 2);
  check("asDsVia<T>：as + 结构体成员一起用",
        pair_via_variant(std::variant<pair_t, int>{pair_t{1, 2}}) == 5);
  check("matched(v, p)：一句话问匹不匹配", matched_helper(5) && !matched_helper(4));

  // ---- 手册 4.1 / 4.2 ----
  seam_demo();
  tool_demo();

  // ---- 手册 5 坑 1 ----
  check("Id<T&> 指着块内局部量：出块后那个对象已经析构（再读 *id 就是悬垂读）",
        id_points_at_a_dead_object());
  check("按值持有的模式（or_ / meet）可以存下来复用", value_held_patterns_can_be_stored());

  // ---- 手册 5 坑 3 ----
  check("本机 int32_t 就是 int —— ARM 那条坑在这里编不出来（依据 PATCHES.md 改动 2）",
        std::is_same<std::int32_t, int>::value);

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
