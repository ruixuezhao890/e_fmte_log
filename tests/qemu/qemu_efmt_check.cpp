/**
 ******************************************************************************
 * @file           : qemu_efmt_check.cpp
 * @brief          : 嵌入式行为验证 —— 在 QEMU (mps2-an386, Cortex-M4) 里真实运行
 * @attention      : 完全自包含：无 newlib/libc 依赖（efmt 浮点走自带引擎），
 *                   UART0 = CMSDK APB UART @ 0x40004000，输出到 QEMU -serial stdio。
 *                   结尾输出 ALL PASS (N) / FAILED (M) 后死循环，脚本超时收尾。
 *
 *                   链接注意：必须链接 thumb/v7e-m 的 libgcc（目录由
 *                   arm-none-eabi-g++ -print-libgcc-file-name 给出）：
 *                   -nostdlib 会让 GCC 驱动丢掉 multilib 的 -L，导致 -lgcc 解析到
 *                   A32 (ARM) libgcc —— Thumb 代码调其 64 位除法（__aeabi_uldivmod）
 *                   会指令流错乱（实测 42 被格式化成 80 并最终 HardFault-Lockup）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>

#include <cstdint>
#include <cstddef>

#if !EFMT_ENABLE_FLOAT
#error "qemu check should run with EFMT_ENABLE_FLOAT=1"
#endif

// ---- CMSDK APB UART0（MPS2 板卡，-serial stdio）----
// STATE bit0 = TXBUSY；写 DATA 发送（与 STM32 UART 轮询写法同构）
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

// efmt 的输出回调：一次写一整块（等价于文档示例里的 HAL_UART_Transmit）
static void uart_sink(const char *data, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    uart_send(data[i]);
  }
}

// 自包含路径不链任何 libc：补齐 C 标准库里编译器引用的三个符号
// （memcpy/memset 也常被内建展开，这里兜底外部引用；strlen 供 string_view 构造）
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

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char *name) {
  if (ok) {
    ++g_pass;
    uart_text("PASS ");
  } else {
    ++g_fail;
    uart_text("FAIL ");
  }
  uart_text(name);
  uart_send(char(10));
}

static bool streq(const char *a, const char *b) {
  while (*a != 0 && *b != 0 && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

// 声明即推导：结构体 + 枚举（ARM 工具链可移植性在此一并验证）
E_FMT_DERIVE(struct imu3 { float ax, ay, az; });
E_FMT_DERIVE(enum class state3 { idle, busy = 5, fault });

int main() {
  char buf[96];

  // 基础整数 / 十六进制 / 补零
  e_fmt::format_to(buf, sizeof(buf), "{}", 42);
  check(streq(buf, "42"), "int 42");

  e_fmt::format_to(buf, sizeof(buf), "{:#x}", 0xAB);
  check(streq(buf, "0xab"), "hex #x");

  e_fmt::format_to(buf, sizeof(buf), "{:02X}", 0x0A);
  check(streq(buf, "0A"), "hex 02X");

  e_fmt::format_to(buf, sizeof(buf), "{:d}", 255u);
  check(streq(buf, "255"), "dec unsigned");

  e_fmt::format_to(buf, sizeof(buf), "{:04x}", 0x5);
  check(streq(buf, "0005"), "hex 04x");

  // uint64 全范围
  e_fmt::format_to(buf, sizeof(buf), "{}",
                   static_cast<unsigned long long>(18446744073709551615ULL));
  check(streq(buf, "18446744073709551615"), "uint64 max");

  // 对齐
  e_fmt::format_to(buf, sizeof(buf), "{:>8}|{:<8}", 1, 2);
  check(streq(buf, "       1|2       "), "align");

  // 浮点（自带引擎，无 libc）
  e_fmt::format_to(buf, sizeof(buf), "{:.2f}", 1.5);
  check(streq(buf, "1.50"), "float .2f");

  e_fmt::format_to(buf, sizeof(buf), "{:.0f}", 2.5);
  check(streq(buf, "2"), "float half-even");

  e_fmt::format_to(buf, sizeof(buf), "{:.3f}", -0.0005);
  check(streq(buf, "-0.001"), "float neg");

  e_fmt::format_to(buf, sizeof(buf), "{:.3f}", 999.9995);  // 进位链
  check(streq(buf, "1000.000"), "float carry");

  e_fmt::format_to(buf, sizeof(buf), "{:e}", 1.0);
  check(streq(buf, "1.000000e+00"), "float e");

  e_fmt::format_to(buf, sizeof(buf), "{:g}", 100000.0);
  check(streq(buf, "100000"), "float g");

  e_fmt::format_to(buf, sizeof(buf), "{:08.3f}", 3.14159);
  check(streq(buf, "0003.142"), "float 08.3f");

  // 截断语义
  char tiny[4];
  const std::size_t needed = e_fmt::format_to(tiny, sizeof(tiny), "{}", 123456);
  check(needed >= sizeof(tiny) && tiny[sizeof(tiny) - 1] == 0, "truncate");

  // 编译期校验（E_FMT_STR）
  e_fmt::format_to(buf, sizeof(buf), E_FMT_STR("{}"), 9);
  check(streq(buf, "9"), "checked str");

  // 参数上限：8 个实参
  e_fmt::format_to(buf, sizeof(buf), "{}{}{}{}{}{}{}{}", 1, 2, 3, 4, 5, 6, 7, 8);
  check(streq(buf, "12345678"), "eight args");

  // formatted_size
  check(e_fmt::formatted_size("ab{}", 1) == 3, "formatted_size");

  // E_FMT_DERIVE：结构体 + 枚举
  e_fmt::format_to(buf, sizeof(buf), "{}", imu3{1.5f, 2.5f, 3.5f});
  check(streq(buf, "{ ax = 1.5, ay = 2.5, az = 3.5 }"), "derive struct");

  e_fmt::format_to(buf, sizeof(buf), "{}", state3::busy);
  check(streq(buf, "busy"), "derive enum");

  // 输出回调路径：set_output_handler + println 经 UART 真实外设
  e_fmt::set_output_handler(&uart_sink);
  e_fmt::print_info("boot ok 42");  // 经全局回调写 UART，回路与文档示例一致

  // 汇总
  uart_text(g_fail == 0 ? "ALL PASS (" : "FAILED (");
  char tail[8];
  e_fmt::format_to(tail, sizeof(tail), "{}", g_fail == 0 ? g_pass : g_fail);
  uart_text(tail);
  uart_text(")");
  uart_send(char(10));

  // 死循环：脚本超时收尾并校验输出文本
  for (;;) {
  }
}
