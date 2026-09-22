// elog micro-benchmark: one-line leveled logging hot paths (built on efmt).
// Reproduces the "elog 一行日志" numbers in docs/EFMT-使用手册.md §8.4 / §13.6.
#include <elog/elog.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>

static char g_buf[512];
static std::size_t g_pos = 0;

static bool sink_write(const char* data, std::size_t size, void*) {
  if (g_pos + size < sizeof(g_buf)) {
    std::memcpy(g_buf + g_pos, data, size);
    g_pos += size;
  }
  return true;
}

volatile std::size_t g_sink = 0;

template <typename Fn>
static double time_ns(const char* name, std::size_t iters, Fn&& fn) {
  // warmup
  for (std::size_t i = 0; i < iters / 10 + 1; ++i) fn();
  const auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iters; ++i) fn();
  const auto t1 = std::chrono::steady_clock::now();
  const double ns =
      std::chrono::duration<double, std::nano>(t1 - t0).count() / double(iters);
  std::printf("%-28s %10.1f ns/op\n", name, ns);
  return ns;
}

int main() {
  e_log::logger* logger = e_log::create_logger(
      "bench", e_log::make_sink(&sink_write), e_log::level::trace);
  e_log::logger* quiet = e_log::create_logger(
      "quiet", e_log::make_sink(&sink_write), e_log::level::error);
  if (!logger || !quiet) {
    std::printf("create_logger failed\n");
    return 1;
  }

  const std::size_t N = 200000;

  // 级别通过：完整一行（前缀 + 2 参数 + 换行，sink 直写）
  time_ns("elog log 2 args (passes)", N, [&] {
    logger->info("boot {} {}", "ok", 42);
    g_sink += g_pos;
    g_pos = 0;
  });

  // 级别通过：含浮点 {:.1f}（走当前 EFMT_USE_LIBC_PRINTF 配置）
  time_ns("elog log float {:.1f}", N, [&] {
    logger->info("temp {:.1f} v {} c {}", 36.5, 2, 3);
    g_sink += g_pos;
    g_pos = 0;
  });

  // 级别被过滤：should_log 在格式化之前短路，几乎零开销
  time_ns("elog filtered (no fmt)", N, [&] {
    quiet->debug("skip this {}", 123);
    g_sink += g_pos;
    g_pos = 0;
  });

  // 对照：裸 format_to 写同一条消息（不含前缀/换行）
  time_ns("format_to same msg", N, [&] {
    char buf[128];
    g_sink += e_fmt::format_to(buf, sizeof(buf), "boot {} {}", "ok", 42);
  });

  std::printf("sink=%zu\n", (std::size_t)g_sink);
  return 0;
}
