#ifndef ELOG_HPP
#define ELOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <middleware/etl/array.h>
#include <middleware/etl/expected.h>

namespace e_log {

#ifndef ELOG_ENABLE_COLOR
#define ELOG_ENABLE_COLOR EFMT_ENABLE_ANSI_STYLES
#endif

enum class level : std::uint8_t {
  trace = 0,
  debug,
  info,
  warn,
  error,
  critical,
  off
};

enum class errc : std::uint8_t {
  success = 0,
  invalid_name,
  name_too_long,
  duplicate_logger_name,
  registry_full,
  logger_not_found,
  default_logger_not_set,
  invalid_sink,
  sink_list_full,
  sink_write_failed,
  message_too_long
};

template <typename T> using result = etl::expected<T, errc>;
using void_result = etl::expected<void, errc>;

inline auto make_unexpected(errc error) {
  return etl::unexpected<errc>(error);
}

inline const char *to_string(level value) {
  switch (value) {
  case level::trace:
    return "trace";
  case level::debug:
    return "debug";
  case level::info:
    return "info";
  case level::warn:
    return "warn";
  case level::error:
    return "error";
  case level::critical:
    return "critical";
  case level::off:
    return "off";
  default:
    return "unknown";
  }
}

inline const char *to_string(errc value) {
  switch (value) {
  case errc::success:
    return "success";
  case errc::invalid_name:
    return "invalid name";
  case errc::name_too_long:
    return "logger name too long";
  case errc::duplicate_logger_name:
    return "duplicate logger name";
  case errc::registry_full:
    return "logger registry full";
  case errc::logger_not_found:
    return "logger not found";
  case errc::default_logger_not_set:
    return "default logger not set";
  case errc::invalid_sink:
    return "invalid sink";
  case errc::sink_list_full:
    return "sink list full";
  case errc::sink_write_failed:
    return "sink write failed";
  case errc::message_too_long:
    return "message too long";
  default:
    return "unknown error";
  }
}

struct config {
  static constexpr std::size_t max_loggers = 8;
  static constexpr std::size_t max_logger_name = 31;
  static constexpr std::size_t max_sinks_per_logger = 4;
  static constexpr std::size_t max_payload_size = 256;
  static constexpr std::size_t max_record_size = 384;
};

struct source_location {
  const char* file = "<unknown>";
  int line = 0;
  const char* function = "<unknown>";
};

class sink {
public:
  using write_fn = bool (*)(const char *data, std::size_t size, void *user_data);

  constexpr sink() = default;
  constexpr sink(write_fn fn, void *user_data = nullptr)
      : write_fn_(fn), user_data_(user_data) {}
  constexpr explicit sink(e_fmt::output_fn fn) : efmt_fn_(fn) {}

  [[nodiscard]] bool is_valid() const {
    return write_fn_ != nullptr || efmt_fn_ != nullptr;
  }

  [[nodiscard]] void_result write(const char *data, std::size_t size) const {
    if (write_fn_ != nullptr) {
      if (!write_fn_(data, size, user_data_)) {
        return make_unexpected(errc::sink_write_failed);
      }

      return {};
    }

    if (efmt_fn_ != nullptr) {
      efmt_fn_(data, size);
      return {};
    }

    return make_unexpected(errc::invalid_sink);
  }

  static sink from_callback(write_fn fn, void *user_data = nullptr) {
    return sink(fn, user_data);
  }

  static sink from_efmt_output(e_fmt::output_fn fn) {
    return sink(fn);
  }

private:
  write_fn write_fn_ = nullptr;
  e_fmt::output_fn efmt_fn_ = nullptr;
  void *user_data_ = nullptr;
};

class multi_sink {
public:
  [[nodiscard]] void_result add_sink(const sink& value) {
    if (!value.is_valid()) {
      return make_unexpected(errc::invalid_sink);
    }
    if (count_ >= sinks_.size()) {
      return make_unexpected(errc::sink_list_full);
    }

    sinks_[count_++] = value;
    return {};
  }

  [[nodiscard]] sink output_sink() {
    return sink::from_callback(&write_all, this);
  }

  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0U; }

private:
  static bool write_all(const char* data, std::size_t size, void* user_data) {
    auto* self = static_cast<multi_sink*>(user_data);
    if (self == nullptr || self->empty()) {
      return false;
    }

    bool all_succeeded = true;
    for (std::size_t index = 0; index < self->count_; ++index) {
      if (!self->sinks_[index].write(data, size).has_value()) {
        all_succeeded = false;
      }
    }
    return all_succeeded;
  }

  etl::array<sink, config::max_sinks_per_logger> sinks_{};
  std::size_t count_ = 0;
};

class logger {
public:
  logger() = default;

  [[nodiscard]] const char *name() const { return name_.data(); }
  [[nodiscard]] level current_level() const { return level_; }
  [[nodiscard]] bool has_sink() const { return sink_.is_valid(); }

  void set_level(level value) { level_ = value; }
  void set_sink(const sink &value) { sink_ = value; }

  [[nodiscard]] bool should_log(level value) const {
    return value >= level_ && level_ != level::off;
  }

  template <typename... Args>
  void_result try_log(level value, const char *fmt, Args... args) const {
    return try_log_at(value, source_location{}, fmt, args...);
  }

  template <typename... Args>
  void_result try_log_at(level value, const source_location& location,
                         const char *fmt, Args... args) const {
    if (!should_log(value)) {
      return {};
    }

    if (!sink_.is_valid()) {
      return make_unexpected(errc::invalid_sink);
    }

    // format_to 返回完整输出所需长度（snprintf 语义）：放不下就是消息太长
    char payload[config::max_payload_size + 1U] = {};
    const std::size_t payload_size =
        e_fmt::format_to(payload, sizeof(payload), fmt, args...);
    if (payload_size >= sizeof(payload)) {
      return make_unexpected(errc::message_too_long);
    }

    char record[config::max_record_size + 1U] = {};
    const std::size_t record_size = e_fmt::format_to(
        record,
        sizeof(record),
        "[{}] [{}:{} {}] {}",
        to_string(value),
        basename(location.file),
        location.line,
        location.function,
        payload);
    if (record_size >= sizeof(record)) {
      return make_unexpected(errc::message_too_long);
    }

    auto write_result = write_with_style(value, record, record_size);
    if (!write_result.has_value()) {
      return write_result;
    }

    static constexpr char newline = '\n';
    return sink_.write(&newline, 1U);
  }

  template <typename... Args>
  void log(level value, const char *fmt, Args... args) const {
    (void)try_log(value, fmt, args...);
  }

  template <typename... Args>
  void log_at(level value, const source_location& location, const char *fmt,
              Args... args) const {
    (void)try_log_at(value, location, fmt, args...);
  }

  template <typename... Args> void_result try_trace(const char *fmt, Args... args) const {
    return try_log(level::trace, fmt, args...);
  }
  template <typename... Args> void_result try_debug(const char *fmt, Args... args) const {
    return try_log(level::debug, fmt, args...);
  }
  template <typename... Args> void_result try_info(const char *fmt, Args... args) const {
    return try_log(level::info, fmt, args...);
  }
  template <typename... Args> void_result try_warn(const char *fmt, Args... args) const {
    return try_log(level::warn, fmt, args...);
  }
  template <typename... Args> void_result try_error(const char *fmt, Args... args) const {
    return try_log(level::error, fmt, args...);
  }
  template <typename... Args> void_result try_critical(const char *fmt, Args... args) const {
    return try_log(level::critical, fmt, args...);
  }

  template <typename... Args> void trace(const char *fmt, Args... args) const {
    log(level::trace, fmt, args...);
  }
  template <typename... Args> void debug(const char *fmt, Args... args) const {
    log(level::debug, fmt, args...);
  }
  template <typename... Args> void info(const char *fmt, Args... args) const {
    log(level::info, fmt, args...);
  }
  template <typename... Args> void warn(const char *fmt, Args... args) const {
    log(level::warn, fmt, args...);
  }
  template <typename... Args> void error(const char *fmt, Args... args) const {
    log(level::error, fmt, args...);
  }
  template <typename... Args> void critical(const char *fmt, Args... args) const {
    log(level::critical, fmt, args...);
  }

private:
  friend class registry;

  static const char* basename(const char* path) {
    if (path == nullptr) {
      return "<unknown>";
    }

    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* last = slash;
    if (backslash != nullptr && (last == nullptr || backslash > last)) {
      last = backslash;
    }

    return (last == nullptr) ? path : last + 1;
  }

  static e_fmt::text_style style_for_level(level value) {
#if ELOG_ENABLE_COLOR
    switch (value) {
    case level::trace:
      return e_fmt::detail::styles::muted();
    case level::debug:
      return e_fmt::detail::styles::debug();
    case level::info:
      return e_fmt::detail::styles::info();
    case level::warn:
      return e_fmt::detail::styles::warning();
    case level::error:
      return e_fmt::detail::styles::error();
    case level::critical: {
      e_fmt::text_style critical_style(e_fmt::detail::color::bright_red);
      critical_style.set_style(e_fmt::detail::style::bold);
      critical_style.set_style(e_fmt::detail::style::underline);
      return critical_style;
    }
    case level::off:
    default:
      return {};
    }
#else
    (void)value;
    return {};
#endif
  }

  void_result write_with_style(level value, const char* data,
                               std::size_t size) const {
    const auto style = style_for_level(value);
    if (!style.is_empty()) {
      char style_buffer[64];
      const auto style_size =
          e_fmt::detail::style_builder::build(style, style_buffer, sizeof(style_buffer));
      if (style_size > 0U) {
        auto style_result = sink_.write(style_buffer, style_size);
        if (!style_result.has_value()) {
          return style_result;
        }
      }
    }

    auto write_result = sink_.write(data, size);
    if (!write_result.has_value()) {
      return write_result;
    }

    if (!style.is_empty()) {
      auto reset_result = sink_.write(e_fmt::detail::style_builder::reset(),
                                      e_fmt::detail::style_builder::reset_length());
      if (!reset_result.has_value()) {
        return reset_result;
      }
    }

    return {};
  }

  void reset() {
    name_.fill('\0');
    sink_ = sink{};
    level_ = level::info;
  }

  void set_name_unchecked(const char *value) {
    name_.fill('\0');
    std::strncpy(name_.data(), value, config::max_logger_name);
    name_[config::max_logger_name] = '\0';
  }

  etl::array<char, config::max_logger_name + 1U> name_{};
  sink sink_{};
  level level_ = level::info;
};

class registry {
public:
  static registry &instance() {
    static registry storage;
    return storage;
  }

  result<logger *> create(const char *name, const sink &sink_value,
                          level initial_level = level::info) {
    auto validation = validate_name(name);
    if (!validation.has_value()) {
      return make_unexpected(validation.error());
    }

    if (!sink_value.is_valid()) {
      return make_unexpected(errc::invalid_sink);
    }

    if (find_index(name) >= 0) {
      return make_unexpected(errc::duplicate_logger_name);
    }

    for (std::size_t index = 0; index < config::max_loggers; ++index) {
      if (!used_[index]) {
        used_[index] = true;
        loggers_[index].reset();
        loggers_[index].set_name_unchecked(name);
        loggers_[index].set_sink(sink_value);
        loggers_[index].set_level(initial_level);

        if (default_index_ < 0) {
          default_index_ = static_cast<int>(index);
        }

        return &loggers_[index];
      }
    }

    return make_unexpected(errc::registry_full);
  }

  result<logger *> get(const char *name) {
    const auto index = find_index(name);
    if (index < 0) {
      return make_unexpected(errc::logger_not_found);
    }
    return &loggers_[static_cast<std::size_t>(index)];
  }

  result<const logger *> get(const char *name) const {
    const auto index = find_index(name);
    if (index < 0) {
      return make_unexpected(errc::logger_not_found);
    }
    return &loggers_[static_cast<std::size_t>(index)];
  }

  void_result set_default(const char *name) {
    const auto index = find_index(name);
    if (index < 0) {
      return make_unexpected(errc::logger_not_found);
    }

    default_index_ = index;
    return {};
  }

  void_result set_default(logger &value) {
    const auto index = find_index(value.name());
    if (index < 0) {
      return make_unexpected(errc::logger_not_found);
    }

    default_index_ = index;
    return {};
  }

  result<logger *> default_logger() {
    if (default_index_ < 0) {
      return make_unexpected(errc::default_logger_not_set);
    }

    return &loggers_[static_cast<std::size_t>(default_index_)];
  }

  result<const logger *> default_logger() const {
    if (default_index_ < 0) {
      return make_unexpected(errc::default_logger_not_set);
    }

    return &loggers_[static_cast<std::size_t>(default_index_)];
  }

private:
  static void_result validate_name(const char *name) {
    if (name == nullptr || name[0] == '\0') {
      return make_unexpected(errc::invalid_name);
    }

    if (std::strlen(name) > config::max_logger_name) {
      return make_unexpected(errc::name_too_long);
    }

    return {};
  }

  int find_index(const char *name) const {
    if (name == nullptr) {
      return -1;
    }

    for (std::size_t index = 0; index < config::max_loggers; ++index) {
      if (used_[index] &&
          std::strncmp(loggers_[index].name(), name, config::max_logger_name + 1U) == 0) {
        return static_cast<int>(index);
      }
    }

    return -1;
  }

  registry() = default;

  etl::array<logger, config::max_loggers> loggers_{};
  etl::array<bool, config::max_loggers> used_{};
  int default_index_ = -1;
};

inline result<logger *> create_logger(const char *name, const sink &sink_value,
                                      level initial_level = level::info) {
  return registry::instance().create(name, sink_value, initial_level);
}

inline result<logger *> get(const char *name) {
  return registry::instance().get(name);
}

inline void_result set_default_logger(const char *name) {
  return registry::instance().set_default(name);
}

inline void_result set_default_logger(logger &value) {
  return registry::instance().set_default(value);
}

inline result<logger *> default_logger() {
  return registry::instance().default_logger();
}

template <typename... Args>
void_result try_log(level value, const char *fmt, Args... args) {
  auto logger_result = default_logger();
  if (!logger_result.has_value()) {
    return make_unexpected(logger_result.error());
  }

  return logger_result.value()->try_log(value, fmt, args...);
}

template <typename... Args>
void_result try_log_at(level value, const source_location& location,
                       const char *fmt, Args... args) {
  auto logger_result = default_logger();
  if (!logger_result.has_value()) {
    return make_unexpected(logger_result.error());
  }

  return logger_result.value()->try_log_at(value, location, fmt, args...);
}

template <typename... Args> void log(level value, const char *fmt, Args... args) {
  (void)try_log(value, fmt, args...);
}

template <typename... Args>
void log_at(level value, const source_location& location, const char *fmt,
            Args... args) {
  (void)try_log_at(value, location, fmt, args...);
}

template <typename... Args> void_result try_trace(const char *fmt, Args... args) {
  return try_log(level::trace, fmt, args...);
}
template <typename... Args> void_result try_debug(const char *fmt, Args... args) {
  return try_log(level::debug, fmt, args...);
}
template <typename... Args> void_result try_info(const char *fmt, Args... args) {
  return try_log(level::info, fmt, args...);
}
template <typename... Args> void_result try_warn(const char *fmt, Args... args) {
  return try_log(level::warn, fmt, args...);
}
template <typename... Args> void_result try_error(const char *fmt, Args... args) {
  return try_log(level::error, fmt, args...);
}
template <typename... Args> void_result try_critical(const char *fmt, Args... args) {
  return try_log(level::critical, fmt, args...);
}

template <typename... Args> void trace(const char *fmt, Args... args) {
  log(level::trace, fmt, args...);
}
template <typename... Args> void debug(const char *fmt, Args... args) {
  log(level::debug, fmt, args...);
}
template <typename... Args> void info(const char *fmt, Args... args) {
  log(level::info, fmt, args...);
}
template <typename... Args> void warn(const char *fmt, Args... args) {
  log(level::warn, fmt, args...);
}
template <typename... Args> void error(const char *fmt, Args... args) {
  log(level::error, fmt, args...);
}
template <typename... Args> void critical(const char *fmt, Args... args) {
  log(level::critical, fmt, args...);
}

inline sink make_sink(sink::write_fn fn, void *user_data = nullptr) {
  return sink::from_callback(fn, user_data);
}

inline sink make_efmt_sink(e_fmt::output_fn fn) { return sink::from_efmt_output(fn); }

inline bool stdout_sink_write(const char *data, std::size_t size, void *) {
#if EFMT_ENABLE_STDIO
  e_fmt::stdout_output_handler(data, size);
#else
  auto sink_result = e_log::sink::from_efmt_output(e_fmt::get_output_handler()).write(data, size);
  if (!sink_result.has_value()) {
    return false;
  }
#endif
  return true;
}

inline sink stdout_sink() { return make_sink(&stdout_sink_write); }

} // namespace e_log

#define ELOG_SOURCE_LOCATION ::e_log::source_location{__FILE__, __LINE__, __func__}

#define ELOG_TRACE(...) ::e_log::log_at(::e_log::level::trace, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_DEBUG(...) ::e_log::log_at(::e_log::level::debug, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_INFO(...) ::e_log::log_at(::e_log::level::info, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_WARN(...) ::e_log::log_at(::e_log::level::warn, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_ERROR(...) ::e_log::log_at(::e_log::level::error, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_CRITICAL(...) ::e_log::log_at(::e_log::level::critical, ELOG_SOURCE_LOCATION, __VA_ARGS__)

#define ELOG_LOGGER_TRACE(logger, ...) (logger).log_at(::e_log::level::trace, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_LOGGER_DEBUG(logger, ...) (logger).log_at(::e_log::level::debug, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_LOGGER_INFO(logger, ...) (logger).log_at(::e_log::level::info, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_LOGGER_WARN(logger, ...) (logger).log_at(::e_log::level::warn, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_LOGGER_ERROR(logger, ...) (logger).log_at(::e_log::level::error, ELOG_SOURCE_LOCATION, __VA_ARGS__)
#define ELOG_LOGGER_CRITICAL(logger, ...) (logger).log_at(::e_log::level::critical, ELOG_SOURCE_LOCATION, __VA_ARGS__)

#endif // ELOG_HPP
