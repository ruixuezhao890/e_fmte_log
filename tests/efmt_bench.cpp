// efmt micro-benchmark: format-string parsing + formatting hot paths.
#include <middleware/efmt/core/format.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

using namespace e_fmt;
using clock_type = std::chrono::steady_clock;

volatile size_t g_sink = 0;

template <typename Fn>
static double time_ns(const char* name, size_t iters, Fn&& fn) {
  // warmup
  for (size_t i = 0; i < iters / 10 + 1; ++i) fn();
  auto t0 = clock_type::now();
  for (size_t i = 0; i < iters; ++i) fn();
  auto t1 = clock_type::now();
  double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / double(iters);
  std::printf("%-28s %10.1f ns/op\n", name, ns);
  return ns;
}

int main() {
  const size_t N = 200000;

  time_ns("log record (char buffer)", N, [&] {
    char buf[256];
    g_sink += format_to(buf, sizeof(buf), "[{}] [{}:{} {}] {}",
                        "info", "main.cpp", 42, "main", "hello world");
  });

  time_ns("format -> std::string {}", N, [&] {
    std::string s = format("{}", 42);
    g_sink += s.size();
  });

  time_ns("format spec mix -> string", N, [&] {
    std::string s = format("{:<12}|{:>8.2f}|{:#06x}", "name", 3.14159, 255u);
    g_sink += s.size();
  });

  time_ns("format 3 fields -> string", N, [&] {
    std::string s = format("x={}, y={}, z={}", 10, 20, 30);
    g_sink += s.size();
  });

  time_ns("formatted_size", N, [&] { g_sink += formatted_size("Value: {} of {}", 3, 10); });

  time_ns("long text (200 chars)", N, [&] {
    std::string s = format(
        "The quick brown fox jumps over the lazy dog, and then keeps running for a "
        "while: {} --- and here is some more trailing text to make it long. {}",
        123456, 3.5);
    g_sink += s.size();
  });

  std::printf("sink=%zu\n", (size_t)g_sink);
  return 0;
}
