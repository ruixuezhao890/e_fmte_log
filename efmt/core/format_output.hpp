/**
 ******************************************************************************
 * @file           : format_output.hpp
 * @author         : ruixuezhao
 * @brief          : Custom output handler for embedded systems
 * @attention      : Allows custom output functions (UART, file, SD card, etc.)
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_OUTPUT_HPP
#define FORMAT_OUTPUT_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_style.hpp>
#include <cstddef>
#include <cstring>

#if EFMT_ENABLE_STDIO
#include <cstdio>
#endif

namespace e_fmt {
// Output redirection layer used by print/println style APIs.
// On desktop it can default to stdout; on embedded targets it can be routed to
// UART, RTT, files or any user-supplied sink.

// ============================================================================
// Output Handler Type Definition
// ============================================================================

/// Output function type: writes data to custom output destination
/// @param data Pointer to the data to write
/// @param size Size of the data in bytes
using output_fn = void(*)(const char* data, size_t size);

// ============================================================================
// Output Handler Management
// ============================================================================

namespace detail {

// Global output handler (can be set by user)
// Global sink used by internal_write().
extern output_fn g_output_handler;

// Internal write function that uses the current output handler
void internal_write(const char* data, size_t size);

// RAII helper to temporarily override output handler
// Temporarily override the active sink inside a scope.
class output_handler_scope {
public:
  explicit output_handler_scope(output_fn new_handler);
  ~output_handler_scope();

  // Non-copyable
  output_handler_scope(const output_handler_scope&) = delete;
  output_handler_scope& operator=(const output_handler_scope&) = delete;

private:
  output_fn prev_handler_;
};

} // namespace detail

// ============================================================================
// Global Output Handler Management
// ============================================================================

/// Set the global output handler for all efmt output functions
/// @param handler Function pointer to the output handler, or nullptr to reset to default
inline void set_output_handler(output_fn handler) {
  detail::g_output_handler = handler;
}

/// Get the current global output handler
/// @return Current output handler, or nullptr if using default
inline output_fn get_output_handler() {
  return detail::g_output_handler;
}

/// Reset to default output handler (stdout on desktop systems)
inline void reset_output_handler() {
  detail::g_output_handler = nullptr;
}

// ============================================================================
// Built-in Output Handlers (for common use cases)
// ============================================================================

#if EFMT_ENABLE_STDIO
// Default output to stdout (requires stdio)
inline void stdout_output_handler(const char* data, size_t size) {
  // Use fwrite for efficiency
  fwrite(data, 1, size, stdout);
}

// Output to stderr
inline void stderr_output_handler(const char* data, size_t size) {
  fwrite(data, 1, size, stderr);
}

// Output to FILE*
inline void file_output_handler(FILE* file, const char* data, size_t size) {
  fwrite(data, 1, size, file);
}
#endif

// ============================================================================
// Buffer Output Handler (for capturing output to buffer)
// ============================================================================

/// Buffer output handler context
struct buffer_output_ctx {
  char* buffer;
  size_t size;
  size_t pos;

  explicit buffer_output_ctx(char* buf = nullptr, size_t sz = 0)
    : buffer(buf), size(sz), pos(0) {}
};

/// Global buffer output context (for simple use cases)
namespace detail {
  extern buffer_output_ctx g_buffer_output_ctx;
}

/// Buffer output handler function
inline void buffer_output_handler(const char* data, size_t size) {
  auto& ctx = detail::g_buffer_output_ctx;
  if (ctx.buffer && ctx.pos < ctx.size) {
    // Copy as much as fits; callers can inspect the final position separately.
    size_t write_size = (ctx.pos + size <= ctx.size) ? size : (ctx.size - ctx.pos);
    std::memcpy(ctx.buffer + ctx.pos, data, write_size);
    ctx.pos += write_size;
  }
}

/// Set buffer output handler
inline void set_buffer_output(char* buffer, size_t size) {
  detail::g_buffer_output_ctx = buffer_output_ctx(buffer, size);
  set_output_handler(buffer_output_handler);
}

/// Get current buffer write position
inline size_t get_buffer_output_pos() {
  return detail::g_buffer_output_ctx.pos;
}

/// Reset buffer write position
inline void reset_buffer_output_pos() {
  detail::g_buffer_output_ctx.pos = 0;
}

// ============================================================================
// Null Output Handler (discards all output)
// ============================================================================

inline void null_output_handler(const char*, size_t) {
  // Do nothing - discard output
}

// ============================================================================
// User-defined output handler examples
// ============================================================================

#if 0  // Example code - not compiled by default

// Example: UART output handler for ARM CMSIS
extern void CMSIS_UART_Send(const uint8_t* data, uint32_t size);

inline void uart_output_handler(const char* data, size_t size) {
  CMSIS_UART_Send(reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(size));
}

// Example: Custom HAL output handler
extern void HAL_UART_Transmit_Custom(uint8_t* data, uint16_t size);

inline void hal_uart_output_handler(const char* data, size_t size) {
  HAL_UART_Transmit_Custom(reinterpret_cast<uint8_t*>(const_cast<char*>(data)),
                          static_cast<uint16_t>(size));
}

// Example: Log to SD card via FatFS
extern FATFS fs;
extern FIL log_file;

inline void sd_card_output_handler(const char* data, size_t size) {
  UINT written;
  f_write(&log_file, data, static_cast<UINT>(size), &written);
}

// Example: I2C logging to external device
extern void I2C_Transmit_Log(uint8_t addr, const uint8_t* data, uint16_t size);

inline void i2c_log_output_handler(const char* data, size_t size) {
  I2C_Transmit_Log(0x50, reinterpret_cast<const uint8_t*>(data), static_cast<uint16_t>(size));
}

// Example: RTT (Real-Time Transfer) for Segger J-Link
extern int SEGGER_RTT_Write(int BufferIndex, const char* pBuffer, unsigned int NumBytes);

inline void rtt_output_handler(const char* data, size_t size) {
  SEGGER_RTT_Write(0, data, static_cast<unsigned int>(size));
}

// Example: ITM (Instrumentation Trace Macrocell) for ARM Cortex-M
inline void itm_output_handler(const char* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    ITM_SendChar(data[i]);
  }
}

#endif

} // namespace e_fmt

// ============================================================================
// Output Handler Implementation
// ============================================================================

namespace e_fmt::detail {

// Global output handler (default: nullptr = use stdout)
inline output_fn g_output_handler = nullptr;

// Global buffer output context
inline buffer_output_ctx g_buffer_output_ctx;

// Internal write function
inline void internal_write(const char* data, size_t size) {
  if (g_output_handler) {
    // User-selected sink has highest priority.
    g_output_handler(data, size);
  } else {
#if EFMT_ENABLE_STDIO
    // Default to stdout
    fwrite(data, 1, size, stdout);
#else
    // Embedded: no default handler, user must set one
    (void)data;
    (void)size;
#endif
  }
}

// RAII scope implementation
inline output_handler_scope::output_handler_scope(output_fn new_handler)
  : prev_handler_(g_output_handler) {
  g_output_handler = new_handler;
}

inline output_handler_scope::~output_handler_scope() {
  g_output_handler = prev_handler_;
}

} // namespace e_fmt::detail

#endif // FORMAT_OUTPUT_HPP
