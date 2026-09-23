/**
 ******************************************************************************
 * @file           : qemu_efmt_bench.cpp
 * @brief          : 嵌入式性能周期数 —— QEMU (mps2-an386, Cortex-M4) + -icount + SysTick
 * @attention      : 计数源是 SysTick（QEMU 的 DWT CYCCNT 实测不递增；SysTick 在 -icount
 *                   下每约 570 条 guest 指令计 1 tick，比例固定、可复现）。输出单位
 *                   "ticks/op"（百位小数），相对对比可靠；不表示真板时序。
 *                   结尾死循环，由脚本超时收尾。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>

#include <cstdint>
#include <cstddef>

// ---- CMSDK APB UART0（MPS2 板卡，-serial stdio）----
#define UART0 ((volatile std::uint32_t *)0x40004000U)
static void uart_send(char c) {
  UART0[0x08 / 4] = 3u;  // CTRL: TXEN | RXEN
  while ((UART0[0x04 / 4] & 1u) != 0) {
  }  // STATE.TXBUSY
  UART0[0x00 / 4] = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
}

static void uart_text(const char *s) {
  while (*s != 0) {
    uart_send(*s++);
  }
}

extern "C" {

void *memcpy(void *dst, const void *src, std::size_t n) {
  char *d = static_cast<char *>(dst);
  const char *s = static_cast<const char *>(src);
  while (n-- != 0) {
    *d++ = *s++;
  }
  return dst;
}

void *memset(void *dst, int c, std::size_t n) {
  char *d = static_cast<char *>(dst);
  while (n-- != 0) {
    *d++ = static_cast<char>(c);
  }
  return dst;
}

std::size_t strlen(const char *s) {
  std::size_t n = 0;
  while (s[n] != 0) {
    ++n;
  }
  return n;
}

}  // extern "C"

// ---- 周期计数：SysTick（24 位向下计数，回绕用模差）----
#define SYST_CSR (*(volatile std::uint32_t *)0xE000E010U)
#define SYST_RVR (*(volatile std::uint32_t *)0xE000E014U)
#define SYST_CVR (*(volatile std::uint32_t *)0xE000E018U)

static void cyc_init(void) {
  SYST_RVR = 0xFFFFFFu;
  SYST_CVR = 0u;
  SYST_CSR = 3u;  // ENABLE | CLKSOURCE=处理器时钟
}

static inline std::uint32_t cycles_now(void) {
  return 0xFFFFFFu - (SYST_CVR & 0xFFFFFFu);
}

volatile std::size_t g_sink = 0;


static void run_case(const char *name, std::uint32_t iters, void (*fn)(void)) {
  fn();  // warmup
  const std::uint32_t t0 = cycles_now();
  for (std::uint32_t i = 0; i < iters; ++i) {
    fn();
  }
  const std::uint32_t t1 = cycles_now();
  const std::uint32_t per100 = (((t1 - t0) & 0xFFFFFFu) * 100u) / iters;

  uart_text("bench ");
  uart_text(name);
  uart_text(" ");
  char num[16];
  e_fmt::format_to(num, sizeof(num), "{}",
                   static_cast<unsigned long long>(per100 / 100));
  uart_text(num);
  uart_send('.');
  char num2[16];
  {
    const unsigned long long frac =
        static_cast<unsigned long long>(per100 % 100);
    if (frac < 10) {
      uart_send('0');
    }
    e_fmt::format_to(num2, sizeof(num2), "{}", frac);
    uart_text(num2);
  }
  uart_text(" ticks");
  uart_send(char(10));
}

static char g_buf[256];

static void c_int42(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{}", 42);
}
static void c_mix(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:<12}|{:>8.2f}|{:#06x}", "name",
                             3.14159, 255u);
}
static void c_3field(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "x={}, y={}, z={}", 10, 20, 30);
}
static void c_2arg(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "boot {} {}", "ok", 42);
}
static void c_fmt_size(void) {
  g_sink += e_fmt::formatted_size("Value: {} of {}", 3, 10);
}
static void c_longtext(void) {
  g_sink += e_fmt::format_to(
      g_buf, sizeof(g_buf),
      "The quick brown fox jumps over the lazy dog, and then keeps running for a while: "
      "{} --- and here is some more trailing text to make it long. {}",
      123456, 3.5);
}
static void c_checked_mix(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), E_FMT_STR("{:<12}|{:>8.2f}|{:#06x}"),
                             "name", 3.14159, 255u);
}
static void c_checked_3(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), E_FMT_STR("x={}, y={}, z={}"), 10, 20,
                             30);
}
static void c_float_f2(void) { g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:.2f}", 1.5); }
static void c_float_e(void) { g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:e}", 1.0); }
static void c_float_g(void) { g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:g}", 100000.0); }
static void c_float_big(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:.6f}", 12345.6789);
}
static void c_hex8(void) { g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{:#010x}", 0xDEADBEEF); }

E_FMT_DERIVE(struct pt2 { int x, y; });
static void c_derive_struct(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{}", pt2{1, 2});
}
E_FMT_DERIVE(enum class color4 { red, green, blue });
static void c_derive_enum(void) {
  g_sink += e_fmt::format_to(g_buf, sizeof(g_buf), "{}", color4::green);
}

int main() {
  cyc_init();
  uart_text("bench start");
  uart_send(char(10));

  run_case("int {}", 30000, c_int42);
  run_case("mix spec", 12000, c_mix);
  run_case("3 fields", 12000, c_3field);
  run_case("2 args", 30000, c_2arg);
  run_case("formatted_size", 30000, c_fmt_size);
  run_case("long text 200c", 6000, c_longtext);
  run_case("checked mix (E_FMT_STR)", 12000, c_checked_mix);
  run_case("checked 3 (E_FMT_STR)", 12000, c_checked_3);
  run_case("float {:.2f}", 8000, c_float_f2);
  run_case("float {:e}", 6000, c_float_e);
  run_case("float {:g}", 6000, c_float_g);
  run_case("float {:.6f} big", 4000, c_float_big);
  run_case("hex {:#010x}", 20000, c_hex8);
  run_case("derive struct", 8000, c_derive_struct);
  run_case("derive enum", 20000, c_derive_enum);

  uart_text("sink=");
  char tail[16];
  e_fmt::format_to(tail, sizeof(tail), "{}", static_cast<unsigned long long>(g_sink));
  uart_text(tail);
  uart_send(char(10));

  for (;;) {
  }
}