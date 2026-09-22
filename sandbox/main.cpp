/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : efmt / elog 的沙盒 —— 想测什么功能就往这里加
 * @attention      : include 根由 CMakeLists.txt 接好（和 tests/run_check.ps1 一致）：
 *                     <repo>/tests/include → <middleware/efmt/...> <middleware/etl/...>
 *                     <repo>               → <elog/elog.hpp>
 ******************************************************************************
 */

#define EFMT_ENABLE_ANSI_STYLES 0   // 关掉颜色转义，CLion 运行窗口更干净

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <elog/elog.hpp>

// ETL：sandbox 显式依赖（CMakeLists.txt 的 ETL_ROOT），elog 已把 ETL 常用类型
// 接进格式化（etl::string / etl::vector / etl::optional / etl::pair / etl::variant...）
#include <middleware/etl/string.h>
#include <middleware/etl/vector.h>
#include <middleware/etl/optional.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace e_fmt;

// ============================================================================
// 自定义类型：四种注册写法
// ============================================================================
struct imu {                            // ① AUTO：零声明，位置式
  float ax, ay, az;
};
E_FMT_FORMATTER_AUTO(imu);              // → imu(1.5, 2.5, 3.5)

struct point {                          // ② 只列字段名
  int x, y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);    // → {x=3, y=4}

struct cfg {                            // ③ 类型内一行（同样是聚合体：按声明顺序绑定）
  int baud = 115200;
  bool verbose = false;
  E_FMT_FIELDS(baud, verbose)           // → { baud = 115200, verbose = false }
};

E_FMT_DERIVE(struct frame {             // ④ 声明即推导：结构体 / 枚举通用
  point p;                              //    字段名一个都不用手写
  unsigned ts;
});

E_FMT_DERIVE(enum class state { idle, sampling, fault });


E_FMT_DERIVE(struct person {
  int age;
  float weight;
  float high;
  std::string name;
  state state;
});


// ============================================================================
// 小工具
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

template <typename... Args>
static std::string text(std::string_view fmt_str, const Args &...args) {
  char buf[256];
  std::size_t n = format_to(buf, sizeof(buf), fmt_str, args...);
  if (n >= sizeof(buf)) {
    n = sizeof(buf) - 1;
  }
  return std::string(buf, n);
}

static void check(const char *what, const std::string &actual, const char *wanted) {
  ++g_checks;
  if (actual == wanted) {
    std::printf("  OK   %-22s %s\n", what, actual.c_str());
    return;
  }
  ++g_failures;
  std::printf("  FAIL %-22s actual=[%s] wanted=[%s]\n", what, actual.c_str(), wanted);
}

static void section(const char *title) { std::printf("\n--- %s ---\n", title); }

// ============================================================================
// elog 的接收端：一块内存缓冲（MCU 上换成 UART 即可）
// ============================================================================
static char g_log[1024];
static std::size_t g_log_pos = 0;
static std::size_t g_shown = 0;

static bool log_sink_write(const char *data, std::size_t size, void *) {
  if (g_log_pos + size >= sizeof(g_log)) {
    return false;
  }
  std::memcpy(g_log + g_log_pos, data, size);
  g_log_pos += size;
  g_log[g_log_pos] = '\0';
  return true;
}

// 只打印"上次之后新增的"内容，避免重复刷屏
static void show_new(const char *label) {
  std::printf("  [%s] %.*s", label, static_cast<int>(g_log_pos - g_shown), g_log + g_shown);
  g_shown = g_log_pos;
}
int main() {

  person p ={
    .age = 18,
    .weight = 1.0,
    .high = 1.0,
    .name = "xiaoming",
    .state = state::idle
  };

  println_info("person info {}",p);
  print_info("person info {:#}", p);

  // elog：第一个创建的 logger 自动成为默认 logger，之后的 ELOG_* 宏都走它。
  // 创建必须发生在第一次 ELOG_* 之前，否则默认 logger 为空、日志被静默丢弃。
  if (!e_log::create_logger("etl-demo", e_log::stdout_sink(), e_log::level::debug)) {
    std::printf("  create_logger failed\n");
  }

  const etl::vector<int,8> v ={1,2,3,4,5,6,7,8};
  ELOG_INFO("vector print:{}",v);
  ELOG_INFO("person info {}",p);

  // std::printf("\n");   // print_info 不带换行
  // check("person", text("{}", p),
  //       "{ age = 18, weight = 1, high = 1, name = xiaoming, state = idle }");
  // check("person {:#}", text("{:#}", p),
  //       "{\n  age = 18,\n  weight = 1,\n  high = 1,\n  name = xiaoming,\n  state = idle\n}");
  // std::printf("  print_info({:#}) ->\n%s\n", text("{:#}", p).c_str());
  // section("efmt：基础类型与格式规格");
  // check("int", text("{}", 42), "42");
  // check("宽度/填充", text("[{:>6}]", 42), "[    42]");
  // check("十六进制", text("{:#x}", 48879), "0xbeef");
  // check("浮点精度", text("{:.2f}", 3.14159), "3.14");
  // // bool 打印成 1/0（嵌入式风格，省 Flash），不是 true/false
  // check("字符串/bool", text("{} {}", std::string("abc"), true), "abc 1");
  //
  // section("efmt：运行时格式串（同一套 API）");
  // const std::string_view runtime_fmt = "{} @ {}";
  // check("runtime fmt", text(runtime_fmt, "dev", 7), "dev @ 7");
  //
  // section("efmt：自定义类型");
  // check("AUTO", text("{}", imu{1.5f, 2.5f, 3.5f}), "imu(1.5, 2.5, 3.5)");
  // check("FORMATTER_FIELDS", text("{}", point{3, 4}), "{x=3, y=4}");
  // check("E_FMT_FIELDS", text("{}", cfg{}), "{ baud = 115200, verbose = 0 }");
  // check("DERIVE 结构体", text("{}", frame{point{3, 4}, 9u}), "{ p = {x=3, y=4}, ts = 9 }");
  // check("DERIVE 枚举", text("{}", state::sampling), "sampling");
  // check("未列出的枚举值", text("{}", static_cast<state>(9)), "9");
  //
  // section("efmt：std::string（宿主默认开 EFMT_ENABLE_DYNAMIC_STRING）");
  // check("string 作参数", text("{}", std::string("hi efmt")), "hi efmt");
  // check("string_view", text("{}", std::string_view("view ok")), "view ok");
  // check("带宽度", text("[{:>10}]", std::string("ab")), "[        ab]");
  // {
  //   std::string acc;
  //   format_to(acc, "{} {}", 5, std::string("appended"));    // 返回 void；实测是先清空再写入
  //   check("format_to(string&)", acc, "5 appended");
  // }
  // check("format() 返回 string", format("n={}", 7), "n=7");
  //
  // section("efmt：容器（宿主默认开启）");
  // check("vector", text("{}", std::vector<int>{1, 2, 3}), "[1, 2, 3]");
  //
  // section("efmt：输出通道");
  // std::printf("  println_info → ");
  // println_info("hello {}, n={}", "efmt", 3);
  //
  // char captured[128];
  // set_buffer_output(captured, sizeof(captured));
  // print_info("buffer {}", 42);
  // std::printf("  set_buffer_output 捕获: [%.*s]\n",
  //             static_cast<int>(get_buffer_output_pos()), captured);
  // reset_output_handler();
  //
  // section("elog：logger + 自定义 sink");
  // auto made = e_log::create_logger("app", e_log::make_sink(&log_sink_write),
  //                                  e_log::level::info);
  // if (!made.has_value()) {
  //   std::printf("  create_logger 失败: %s\n", e_log::to_string(made.error()));
  //   ++g_failures;
  // } else {
  //   e_log::logger *logger = made.value();
  //   std::printf("  logger name=%s, level=%s\n", logger->name(),
  //               e_log::to_string(logger->current_level()));
  //   (void)logger->try_info("boot {} {}", "ok", 42);
  //   const auto filtered = logger->try_debug("这条应该被等级过滤掉");
  //   show_new("sink");
  //   check("info 落到 sink", std::strstr(g_log, "boot ok 42") ? "yes" : "no", "yes");
  //   check("带等级前缀", std::strstr(g_log, "[info]") ? "yes" : "no", "yes");
  //   check("debug 被过滤", std::strstr(g_log, "过滤掉") ? "yes" : "no", "no");
  //   check("过滤仍算成功", filtered.has_value() ? "yes" : "no", "yes");
  //   const auto too_long = logger->try_info("long {}", std::string(500, 'x'));
  //   check("超长消息被拒", too_long.has_value() ? "ok" : e_log::to_string(too_long.error()),
  //         "message too long");
  // }
  //
  // section("elog：默认 logger + ELOG_INFO 宏（自带 __FILE__/__LINE__）");
  // if (e_log::set_default_logger("app").has_value()) {
  //   ELOG_INFO("from macro: {} + {}", 1, 2);
  //   show_new("sink");
  // }
  //
  // section("elog：多 sink（stdout + 内存）");
  // e_log::multi_sink multi;   // 注意：multi 必须活得比用它创建的 logger 长
  // (void)multi.add_sink(e_log::stdout_sink());
  // (void)multi.add_sink(e_log::make_sink(&log_sink_write));
  // auto made_multi = e_log::create_logger("multi", multi.output_sink(),
  //                                        e_log::level::debug);
  // if (made_multi.has_value()) {
  //   std::printf("  下一行会同时到 stdout 和内存:\n");
  //   (void)made_multi.value()->try_info("double sink");
  //   show_new("sink");
  //   check("内存也收到", std::strstr(g_log, "double sink") ? "yes" : "no", "yes");
  // }
  //
  // section("elog：错误处理走 etl::expected");
  // auto bad = e_log::create_logger("this-name-is-longer-than-thirty-one-chars",
  //                                 e_log::stdout_sink());
  // check("超长名字被拒", bad.has_value() ? "no" : e_log::to_string(bad.error()),
  //       "logger name too long");
  //
  // std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  // return g_failures == 0 ? 0 : 1;
  // ============================================================================
  // elog × ETL：嵌入式类型直接打
  // ============================================================================
  section("elog × ETL 类型");
  etl::string<32> dev = "imu01";
  etl::vector<int, 8> raw{1, 2, 3};
  etl::vector<etl::string<16>, 4> names;
  names.push_back("a");
  names.push_back("bc");
  etl::optional<float> temp = 36.5f;
  etl::optional<int> absent;

  check("etl::string", text("{}", dev), "imu01");
  check("etl::string 宽度/截断", text("[{:>10.3}]", dev), "[       imu]");
  check("etl::vector", text("{}", raw), "[1, 2, 3]");
  check("嵌套 etl::vector<etl::string>", text("{}", names), "[a, bc]");
  check("etl::optional 有值", text("{}", temp), "36.5");
  check("etl::optional 空", text("{}", absent), "nullopt");

  // elog 全链路：ETL 类型直接进日志（logger 已在 main 开头创建）。
  // elog 对用户默认打开容器格式（EFMT_ENABLE_CONTAINER_FORMAT=1），
  // MCU 上打 etl::vector 无需任何配置。
  ELOG_INFO("dev={} raw={} temp={}", dev, raw, temp);

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
