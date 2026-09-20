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
// Fixed limits chosen for embedded-friendly usage.
// 常用大小常量
constexpr size_t max_format_args = 16;

#ifndef EFMT_ENABLE_HOSTED
#if defined(__MBED__) || defined(ARDUINO) || defined(__AVR__) || defined(__arm__) || defined(__thumb__)
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

#ifndef EFMT_ENABLE_STREAM_FALLBACK
#define EFMT_ENABLE_STREAM_FALLBACK EFMT_ENABLE_STREAM_API
#endif

#ifndef EFMT_ENABLE_ANSI_STYLES
#define EFMT_ENABLE_ANSI_STYLES EFMT_ENABLE_HOSTED
#endif

#ifndef EFMT_ENABLE_STDIO
#define EFMT_ENABLE_STDIO EFMT_ENABLE_HOSTED
#endif

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
