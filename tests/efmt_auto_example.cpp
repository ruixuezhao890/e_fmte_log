/**
 ******************************************************************************
 * @file           : efmt_auto_example.cpp
 * @brief          : E_FMT_FORMATTER_AUTO 的完整小例子 —— 一个传感器数据帧，
 *                   字段名、字段类型、显示字符串一个都不用写
 * @attention      : 编译运行（从仓库根目录）：
 *                     g++ -std=c++17 -O2 -I tests/include tests/efmt_auto_example.cpp -o tests/out/auto_example.exe
 *                     tests/out/auto_example.exe
 ******************************************************************************
 */

#define EFMT_ENABLE_ANSI_STYLES 0   // 串口上不要颜色转义序列

#include <middleware/efmt/core/format.hpp>

#include <cstdio>
#include <string_view>

using namespace e_fmt;

// ============================================================================
// ① 最简单：零声明。输出 imu(1.5, -2.25, 9) —— 只有值，没有字段名
// ============================================================================
struct imu {
  float ax, ay, az;
};
E_FMT_FORMATTER_AUTO(imu);      // 宏写在类型所在作用域；ADL 靠它找到格式化器

// ============================================================================
// ② 嵌套：成员自己注册过格式化器，就自动递归
// ============================================================================
struct frame {
  imu sample;
  unsigned ts;
};
E_FMT_FORMATTER_AUTO(frame);

// ============================================================================
// ③ 枚举：取值 → 名字（没列出的取值打底层整数，方便发现漏项）
// ============================================================================
enum class state { idle, sampling, fault };
E_FMT_FORMATTER_ENUM(state, idle, sampling, fault);

// ============================================================================
// ④ 含 C 型数组时必须显式给字段个数
// ============================================================================
// 自动推导会把 char tag[8] 的 8 个元素当成 8 个字段（brace elision），
// 于是算出 10 个字段、跟结构化绑定的 3 个对不上 → 编译报错。
struct packet {
  imu sample;
  char tag[8];
  int count;
};
E_FMT_FORMATTER_AUTO_N(packet, 3);   // 手动给 3，跳过推导

// ============================================================================
// ⑤ 对比：要字段名就用 FIELDS（同一份数据，两种风格）
// ============================================================================
struct point {
  int x, y;
};
E_FMT_FORMATTER_FIELDS(point, x, y);   // 输出 {x=10, y=20}，不是 point(10, 20)

// ============================================================================
// 格式化输出：写进固定缓冲区（MCU 上就是那条串口缓冲）
// ============================================================================
static char g_buf[256];
static std::string_view line;   // 上一次格式化出来的内容

template <typename... Args>
static void send(std::string_view fmt_str, const Args &...args) {
  std::size_t n = format_to(g_buf, sizeof(g_buf), fmt_str, args...);
  if (n >= sizeof(g_buf)) {   // 截断保护：format_to 返回"本该写多少"
    n = sizeof(g_buf) - 1;
  }
  line = std::string_view(g_buf, n);
}

// ============================================================================
// 自检：把真实输出和期望逐条比对，不合就非零退出（防文档腐烂）
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void expect(const char *what, std::string_view wanted) {
  ++g_checks;
  const bool ok = (line == wanted);
  if (!ok) {
    ++g_failures;
  }
  std::printf("%-4s %-22s %s\n", ok ? "OK" : "FAIL", what, line.data());
  if (!ok) {
    std::printf("     expected: %s\n", wanted.data());
  }
}

int main() {
  send("{}", imu{1.5f, -2.25f, 9.0f});
  expect("imu", "imu(1.5, -2.25, 9)");

  send("{}", frame{imu{1, 2, 3}, 12345u});
  expect("frame 嵌套", "frame(imu(1, 2, 3), 12345)");

  send("{}", state::sampling);
  expect("state 枚举", "sampling");

  send("{}", packet{imu{1, 2, 3}, "abc", 7});
  expect("packet 数组", "packet(imu(1, 2, 3), abc, 7)");

  send("{}", point{10, 20});
  expect("point 对比", "{x=10, y=20}");

  // 一条混合日志：文本 + AUTO 结构体 + 枚举 + 普通整数
  send("boot: {} {} n={}", imu{0, 0, 9.81f}, state::idle, 3);
  expect("混合日志", "boot: imu(0, 0, 9.81) idle n=3");

  std::printf("\n%d/%d passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
