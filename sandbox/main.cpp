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
#include <eserde/serde.hpp>       // 可选基座：能力标签查询 + schema（efmt 本体不认识它）
#include <eserde/json.hpp>        // JSON 序列化 / 反序列化（构建在基座上）
#include <eserde/cbor.hpp>        // CBOR 二进制（RFC 8949 子集，写出的字节标准解码器能读）
#include <ecli/cli.hpp>           // 命令行解析（声明即推导；argv 与"一行文本"同一条路）

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
using namespace eserde;
using namespace ecli;

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

E_FMT_DERIVE_ENUM(enum class state { idle, sampling, fault });   // 枚举走专用入口（枚举体的逗号是顶层逗号）


E_FMT_DERIVE(struct person {              // ⑤ 声明即推导 + 字段标签 + 能力标签
  int age;                                //    名字照样一个都不用写
  float weight;
  float high;
  [[efmt::arg(short, long)]]              //    字段标签：efmt 只解析，eserde 之类上层来查
  std::string name;
  state state;
}, Debug, Serialize, Deserialize);        //    能力标签：原样登记，efmt 本体只认 Debug
                                          //    写要 Serialize、读要 Deserialize（缺了编译期报错）


// ⑥ 命令行参数：声明即推导解析器（对标 Rust clap 的 #[derive(Parser)]）。
//    字段名一个都不用写，标签就是 clap 的 #[arg(...)]：short / long / pos / help / required / skip。
//    解析器不认识 argv —— 它只认识 token 表，所以串口 / 蓝牙 / 键盘的"一行文本"走同一个入口。
E_FMT_DERIVE(struct cli_args {
  [[efmt::arg(short, long, help = "verbose output")]]              bool verbose = false;
  [[efmt::arg(short = "o", long = "output", help = "dump file")]]  const char *out = nullptr;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]]        int level = 3;
  [[efmt::arg(long = "tag", help = "repeatable")]]                 std::vector<std::string> tags;
  [[efmt::arg(pos = "1", help = "input file")]]                    std::string input;
}, Cli);

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

  // 基座：声明原文 → 编译期数据（能力标签 / schema / 字段标签）
  static_assert(eserde::has_cap_v<person, Serialize>, "person 带了 Serialize 能力标签");
  static_assert(eserde::has_cap_v<person, Deserialize>, "person 带了 Deserialize 能力标签");
  static_assert(eserde::find_by_tag<person>("short") == 3, "name 字段带 short 标签");
  println_info("schema: {} 个字段；字段 3 的类型名 = {}，标签数 = {}",
               eserde::field_count<person>(),
               eserde::field_type_name<person>(3),
               eserde::tag_count<person>(3));
  eserde::visit_fields(p, [](std::string_view field, const auto &) {
    std::printf("  field %.*s\n", static_cast<int>(field.size()), field.data());
  });

  // JSON：写→读一圈（eserde::json，零第三方、不抛异常）
  char json_buf[256];
  std::size_t json_len = eserde::json::write_to(json_buf, sizeof(json_buf), p);
  const bool json_fits = json_len < sizeof(json_buf);
  if (!json_fits) json_len = sizeof(json_buf) - 1;
  println_info("json ({} B): {}", json_len, std::string_view(json_buf, json_len));

  person q{};
  const eserde::json::error je =
      eserde::json::read_from(std::string_view(json_buf, json_len), q);
  if (je == eserde::json::error::ok && json_fits) {
    println_info("json round-trip: {}", q);
  } else {
    println_info("json round-trip skipped: {}", eserde::json::error_name(je));
  }

  // CBOR：同一个对象走二进制（同一套语义：snprintf 语义 / 失败不动原对象 / 错误码）
  unsigned char cbor_buf[128];
  std::size_t cbor_len = eserde::cbor::write_to(cbor_buf, sizeof(cbor_buf), p);
  const bool cbor_fits = cbor_len < sizeof(cbor_buf);
  if (!cbor_fits) {
    cbor_len = sizeof(cbor_buf) - 1;
  }
  println_info("cbor ({} B；同样内容 json 是 {} B)", cbor_len, json_len);

  person r{};
  const eserde::cbor::error ce = eserde::cbor::read_from(cbor_buf, cbor_len, r);
  if (ce == eserde::cbor::error::ok && cbor_fits) {
    const std::string want = text("{}", p);
    check("cbor round-trip", text("{}", r), want.c_str());
  } else {
    println_info("cbor round-trip skipped: {}", eserde::cbor::error_name(ce));
  }

  // elog：第一个创建的 logger 自动成为默认 logger，之后的 ELOG_* 宏都走它。
  // 创建必须发生在第一次 ELOG_* 之前，否则默认 logger 为空、日志被静默丢弃。
  if (!e_log::create_logger("etl-demo", e_log::stdout_sink(), e_log::level::debug)) {
    std::printf("  create_logger failed\n");
  }

  const etl::vector<int,8> v ={1,2,3,4,5,6,7,8};
  ELOG_INFO("vector print:{}",v);
  ELOG_INFO("person info {}",p);


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

  // ============================================================================
  // ecli：命令行解析（宿主 argv 与设备端"一行文本"共用同一个解析器）
  // ============================================================================
  section("ecli 命令行解析");
  {
    cli_args a{};
    const char *argv[] = {"sandbox", "-vo", "dump.bin", "--level=7", "--tag", "net", "input.txt"};
    ecli::error_info info{};
    const ecli::error e = ecli::parse(7, argv, a, &info);
    check("argv 解析", ecli::error_name(e), "ok");
    check("短选项聚簇 + 粘连取值", a.verbose ? "true" : "false", "true");
    check("取值与可重复项", text("{}|{}|{}", a.out, a.level, a.tags[0]), "dump.bin|7|net");
    check("位置参数", a.input, "input.txt");

    // 设备端：串口 / 蓝牙 / 键盘读进来的就是"一行文本"，同一个解析器、同一套标签
    cli_args b{};
    char line_scratch[ECLI_MAX_LINE];
    const ecli::error e2 =
        ecli::parse("--level 9 \"in put.txt\"", b, line_scratch, sizeof(line_scratch), &info);
    check("一行文本解析", ecli::error_name(e2), "ok");
    check("引号包住的空格算一个 token", b.input, "in put.txt");

    // 帮助 / 报错：snprintf 语义写进缓冲区 —— 谁问的就回给谁（串口问的回串口）
    std::printf("%s\n", ecli::help_string<cli_args>("sandbox", "efmt sandbox CLI").c_str());
    cli_args c{};
    const ecli::error e3 = ecli::parse("--level=abc", c, line_scratch, sizeof(line_scratch), &info);
    std::printf("%s\n", ecli::error_string<cli_args>("sandbox", e3, info).c_str());
  }

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
