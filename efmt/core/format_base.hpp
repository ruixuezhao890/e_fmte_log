/**
 ******************************************************************************
 * @file           : format_base.hpp
 * @author         : ruixuezhao
 * @brief          : None
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_BASE_HPP
#define FORMAT_BASE_HPP
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace e_fmt {
// Core base definitions shared by the whole efmt library.
// Keep this header lightweight because nearly every other module includes it.
// Only the C++ standard library is required (no ETL).
using char_type = char;
using wchar_type = wchar_t;

template <typename T> struct char_traits;

template <> struct char_traits<char> {
  using char_type = char;
};

template <> struct char_traits<wchar_t> {
  using char_type = wchar_t;
};

// 类型别名
using size_t = std::size_t;
using ptrdiff_t = std::ptrdiff_t;

// 整数类型
using int8_t = std::int8_t;
using int16_t = std::int16_t;
using int32_t = std::int32_t;
using int64_t = std::int64_t;

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;
} // namespace e_fmt

namespace e_fmt {

// ============================================================================
// 平台开关
// ============================================================================
// 所有开关都可以在命令行覆盖（例如 -DEFMT_ENABLE_FLOAT=0），也可以在包含任何
// efmt 头文件之前用 #define 覆盖。改动这里就能整体裁剪功能与资源占用。
//
// EFMT_ENABLE_HOSTED：总开关。1 = 有完整宿主环境（std::string / iostream /
// stdio / ANSI），0 = 裸机或 RTOS（这几个默认全部关闭）。
//
// 未显式指定时按下面的规则自动判定：
//   * __STDC_HOSTED__ == 0             -> 嵌入式（标准规定的 freestanding）
//   * AVR / MSP430 / Arduino / Mbed    -> 嵌入式
//   * 非 Linux/Unix 的 ARM（Cortex-M） -> 嵌入式
//   * 其余（Linux / Windows / ESP-IDF）-> 宿主
// ESP-IDF 默认走宿主模式（有完整 libc），需要极限压缩时显式 -DEFMT_ENABLE_HOSTED=0。
#ifndef EFMT_ENABLE_HOSTED
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
#define EFMT_ENABLE_HOSTED 0
#elif defined(__AVR__) || defined(__MSP430__) || defined(ARDUINO) || \
    defined(__MBED__) ||                                                     \
    (defined(__arm__) && !defined(__linux__) && !defined(__unix__))
#define EFMT_ENABLE_HOSTED 0
#else
#define EFMT_ENABLE_HOSTED 1
#endif
#endif

#ifndef EFMT_ENABLE_DYNAMIC_STRING
#define EFMT_ENABLE_DYNAMIC_STRING EFMT_ENABLE_HOSTED
#endif

#ifndef EFMT_ENABLE_STREAM_API
#define EFMT_ENABLE_STREAM_API EFMT_ENABLE_HOSTED
#endif

// operator<< 回退（会拉进 <sstream>），嵌入式默认关闭
#ifndef EFMT_ENABLE_STREAM_FALLBACK
#define EFMT_ENABLE_STREAM_FALLBACK (EFMT_ENABLE_STREAM_API && EFMT_ENABLE_HOSTED)
#endif

#ifndef EFMT_ENABLE_ANSI_STYLES
#define EFMT_ENABLE_ANSI_STYLES EFMT_ENABLE_HOSTED
#endif

#ifndef EFMT_ENABLE_STDIO
#define EFMT_ENABLE_STDIO EFMT_ENABLE_HOSTED
#endif

// 浮点格式化总开关：0 = 完全不编译浮点通道（float/double 参数会编译报错），
// 不打印浮点的固件可以再省掉这部分 Flash。
#ifndef EFMT_ENABLE_FLOAT
#define EFMT_ENABLE_FLOAT 1
#endif

// 浮点是否借用 libc 的 snprintf("%.*f")。默认只在宿主环境借用：嵌入式自带实现
// 不需要 libc 的浮点 printf（newlib-nano 下那一处 {:.2f} 大约多 3.5 KB Flash，
// 还会把 malloc/sbrk 拉进固件）。
#ifndef EFMT_USE_LIBC_PRINTF
#define EFMT_USE_LIBC_PRINTF (EFMT_ENABLE_HOSTED && EFMT_ENABLE_FLOAT)
#endif

// 自带浮点引擎的定点大整数容量（单位：32 位 limb，一个 limb 8 位十进制数字）。
// 48 limb = 1536 bit ≈ 192 B 栈，覆盖：%f 精度 ≤ 600、%e/%g 精度 ≤ 400 左右。
// 需要"任意精度都精确"时改成 80（= 2560 bit，320 B 栈，覆盖全部 double）。
#ifndef EFMT_FLOAT_BIGNUM_LIMBS
#define EFMT_FLOAT_BIGNUM_LIMBS 48
#endif

// 自带浮点引擎一次能产生多少组十进制数字（每组 9 位）。48 组 = 432 位数字，
// 覆盖：任意 double 的整数部分（最大 309 位）+ 常见小数位。
#ifndef EFMT_FLOAT_DIGIT_GROUPS
#define EFMT_FLOAT_DIGIT_GROUPS 48
#endif

// 容器 / tuple / pair 的自动格式化。嵌入式默认关闭：省 Flash，且 std::vector
// 这类容器本来就不该出现在 MCU 的日志里；需要时 -DEFMT_ENABLE_CONTAINER_FORMAT=1。
#ifndef EFMT_ENABLE_CONTAINER_FORMAT
#define EFMT_ENABLE_CONTAINER_FORMAT EFMT_ENABLE_HOSTED
#endif

// 单次调用最多几个参数。每个参数在栈上占 24 B，嵌入式默认 8 个（192 B），
// 宿主保持原来的 16 个（384 B）。
#ifndef EFMT_MAX_FORMAT_ARGS
#define EFMT_MAX_FORMAT_ARGS (EFMT_ENABLE_HOSTED ? 16 : 8)
#endif

// 常用大小常量
constexpr size_t max_format_args = EFMT_MAX_FORMAT_ARGS;

static_assert(EFMT_MAX_FORMAT_ARGS >= 1 && EFMT_MAX_FORMAT_ARGS <= 64,
              "EFMT_MAX_FORMAT_ARGS must be within 1..64");

// print/println 输出的栈缓冲区大小（超出部分会被截断）
#ifndef EFMT_PRINT_BUFFER_SIZE
#define EFMT_PRINT_BUFFER_SIZE 256
#endif

// format() 返回 std::string 时先用这块栈缓冲区，装不下才重新分配
#ifndef EFMT_STRING_BUFFER_SIZE
#define EFMT_STRING_BUFFER_SIZE 256
#endif

namespace detail {
// 极小的类型工具（标准库同名即可）
template <typename T, typename U> using is_same = std::is_same<T, U>;

template <typename T> using remove_const = std::remove_const<T>;

template <typename T> using remove_reference = std::remove_reference<T>;

template <typename T>
using remove_cvref = std::remove_cv_t<std::remove_reference_t<T>>;

// void_t 模拟
template <typename T> struct type_identity {
  using type = T;
};

template <typename T> using type_identity_t = typename type_identity<T>::type;
} // namespace detail

} // namespace e_fmt
#endif // FORMAT_BASE_HPP
