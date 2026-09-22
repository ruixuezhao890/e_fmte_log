/**
 ******************************************************************************
 * @file           : elog_integration.cpp
 * @brief          : Integration smoke test - elog logging through efmt
 * @attention      : Verifies that the efmt API used by elog still behaves,
 *                   plus the ETL type support (strings / containers /
 *                   optional / pair / variant) that elog provides.
 *                   Runs in both hosted and embedded configurations
 *                   (run_check.ps1 builds it twice).
 ******************************************************************************
 */

#include <elog/elog.hpp>

#include <cstdio>
#include <cstring>

#include <middleware/etl/string.h>
#include <middleware/etl/string_view.h>
#include <middleware/etl/vector.h>
#include <middleware/etl/map.h>
#include <middleware/etl/optional.h>
#include <middleware/etl/variant.h>
#include <middleware/etl/utility.h>

static char g_sink[2048];
static size_t g_sink_pos = 0;

static bool sink_write(const char *data, std::size_t size, void *) {
  if (g_sink_pos + size >= sizeof(g_sink)) {
    return false;
  }
  std::memcpy(g_sink + g_sink_pos, data, size);
  g_sink_pos += size;
  g_sink[g_sink_pos] = '\0';
  return true;
}

static void reset_sink() {
  g_sink_pos = 0;
  g_sink[0] = '\0';
}

#define CHECK(cond)                         \
  do {                                      \
    if (!(cond)) {                          \
      std::printf("FAIL %s:%d: %s\n",      \
                  __FILE__, __LINE__, #cond); \
      ++failures;                           \
    }                                       \
  } while (0)

int main() {
  int failures = 0;

  e_log::logger *logger = e_log::create_logger(
      "app", e_log::make_sink(&sink_write), e_log::level::info);
  CHECK(logger != nullptr);

  // ---- basic scalar logging (pre-existing behavior) ----
  reset_sink();
  logger->info("boot {} {}", "ok", 42);
  CHECK(std::strstr(g_sink, "boot ok 42") != nullptr);
  CHECK(std::strstr(g_sink, "[info]") != nullptr);

  // ---- ETL strings ----
  reset_sink();
  etl::string<32> s = "hello etl";
  logger->info("str={}", s);
  CHECK(std::strstr(g_sink, "str=hello etl") != nullptr);

  reset_sink();
  etl::istring &istr = s;  // interface base must format too
  logger->info("istr={}", istr);
  CHECK(std::strstr(g_sink, "istr=hello etl") != nullptr);

  reset_sink();
  etl::string_view sv = "view me";
  logger->info("sv={}", sv);
  CHECK(std::strstr(g_sink, "sv=view me") != nullptr);

  // format specs (width / precision) still apply to ETL strings
  reset_sink();
  logger->info("pad=[{:>10.3}]", s);
  CHECK(std::strstr(g_sink, "pad=[       hel]") != nullptr);

  // ---- ETL containers (elog enables container format by default) ----
  reset_sink();
  etl::vector<int, 8> v;
  v.push_back(1); v.push_back(2); v.push_back(3);
  logger->info("vec={}", v);
  CHECK(std::strstr(g_sink, "vec=[1, 2, 3]") != nullptr);

  reset_sink();
  etl::vector<etl::string<16>, 8> vs;
  vs.push_back("ab"); vs.push_back("cd");
  logger->info("vecstr={}", vs);
  CHECK(std::strstr(g_sink, "vecstr=[ab, cd]") != nullptr);

  reset_sink();
  etl::map<int, int, 4> m;
  m[1] = 10; m[2] = 20;
  logger->info("map={}", m);
  CHECK(std::strstr(g_sink, "map={1: 10, 2: 20}") != nullptr);

  // ---- ETL optional / pair / variant ----
  reset_sink();
  etl::optional<int> o1 = 5;
  etl::optional<int> o2;
  logger->info("opt={} {}", o1, o2);
  CHECK(std::strstr(g_sink, "opt=5 nullopt") != nullptr);

  reset_sink();
  etl::pair<int, int> p{7, 8};
  logger->info("pair={}", p);
  CHECK(std::strstr(g_sink, "pair=(7: 8)") != nullptr);

  reset_sink();
  etl::variant<int, const char *> var = 9;
  logger->info("var={}", var);
  CHECK(std::strstr(g_sink, "var=9") != nullptr);

  if (failures == 0) {
    std::printf("elog integration OK (ETL types supported)\n");
    return 0;
  }
  std::printf("elog integration FAILED: %d failure(s)\n", failures);
  return 1;
}