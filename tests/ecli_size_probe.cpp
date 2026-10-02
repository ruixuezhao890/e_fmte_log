/**
 ******************************************************************************
 * @file           : ecli_size_probe.cpp
 * @brief          : ecli 命令行解析的真实嵌入式工具链体积样例（run_check.ps1 -Size 用）
 * @attention      : 三条读数共用同一份源码，靠宏切换，差值就是 CLI 的代价：
 *                     -DECLI_SIZE_PROBE_OFF=1    只留声明（基线：标签/schema 不产生运行时代码）
 *                     默认                       再带上 parse（词法 + 取值 + 匹配）
 *                     -DECLI_SIZE_PROBE_HELP=1   再带上 write_help / write_error（usage/帮助文本）
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <ecli/cli.hpp>

using namespace ecli;

E_FMT_DERIVE(struct cli_args {
  [[efmt::arg(short, long, help = "verbose output")]]              bool verbose = false;
  [[efmt::arg(short = "o", long = "output", help = "dump file")]]  const char *out = nullptr;
  [[efmt::arg(short = "l", long = "level", help = "0..9")]]        int level = 3;
  [[efmt::arg(pos = "1", help = "input file")]]                    char input[32];
}, Cli);

int main() {
#if defined(ECLI_SIZE_PROBE_OFF)
  return 0;   // 基线：只剩声明与 schema（编译期数据，零运行时代码）
#else
  cli_args a{};
  char scratch[ECLI_MAX_LINE];
  ecli::error_info info{};
  const ecli::error e =
      ecli::parse("--level 7 -vo dump.bin in.txt", a, scratch, sizeof(scratch), &info);
#if defined(ECLI_SIZE_PROBE_HELP)
  char text[256];
  const std::size_t h = ecli::write_help<cli_args>("probe", "size probe", text, sizeof(text));
  const std::size_t w = ecli::write_error<cli_args>("probe", e, info, text, sizeof(text));
  const bool unused = (h == 0 || w == 0);
#else
  const bool unused = false;
#endif
  return (e == ecli::error::ok && !unused) ? 0 : 1;
#endif
}
