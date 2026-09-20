/**
 ******************************************************************************
 * @file           : elog_integration.cpp
 * @brief          : Integration smoke test - elog logging through efmt
 * @attention      : Verifies that the efmt API used by elog still behaves.
 ******************************************************************************
 */

#include <elog/elog.hpp>

#include <cstdio>
#include <cstring>

static char g_sink[512];
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

int main() {
  auto logger = e_log::create_logger("app", e_log::make_sink(&sink_write),
                                     e_log::level::info);
  if (!logger.has_value()) {
    std::printf("create_logger failed\n");
    return 1;
  }

  auto result = logger.value()->try_info("boot {} {}", "ok", 42);
  if (!result.has_value()) {
    std::printf("try_info failed\n");
    return 1;
  }

  std::printf("sink=[%s]\n", g_sink);
  const bool formatted = std::strstr(g_sink, "boot ok 42") != nullptr;
  const bool levelled = std::strstr(g_sink, "[info]") != nullptr;
  return (formatted && levelled) ? 0 : 1;
}
