#ifndef ELOG_HPP
#define ELOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

// elog 是日志库，容器能打是日志能力的一部分：ETL 容器（vector/map/...）在
// MCU 上是常用日志载荷，这里给 elog 用户默认打开容器格式化（实测：不用容器
// 时零 Flash 开销，模板惰性实例化；用容器才 +260 B 左右）。
// 仍可用 -DEFMT_ENABLE_CONTAINER_FORMAT=0 关掉（命令行定义先于本文件、优先）。
// efmt 核心自身不受影响：独立使用时仍维持 HOSTED 语义（宿主开 / 嵌入式关）。
#ifndef EFMT_ENABLE_CONTAINER_FORMAT
#define EFMT_ENABLE_CONTAINER_FORMAT 1
#endif

#include <middleware/efmt/core/format.hpp>
#include <middleware/efmt/core/format_output.hpp>
#include <middleware/etl/array.h>

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

// 可裁剪的容量上限（都能用 -D 覆盖）
//   ELOG_MAX_LOGGERS：注册表里的 logger 槽位数，每个约占 64 B 静态 RAM
//   ELOG_MAX_RECORD_SIZE：单条日志（时间戳/位置前缀 + 消息）的栈缓冲
#ifndef ELOG_MAX_LOGGERS
#define ELOG_MAX_LOGGERS 8
#endif
#ifndef ELOG_MAX_RECORD_SIZE
#define ELOG_MAX_RECORD_SIZE 384
#endif

struct config {
  static constexpr std::size_t max_loggers = ELOG_MAX_LOGGERS;
  static constexpr std::size_t max_logger_name = 31;
  static constexpr std::size_t max_sinks_per_logger = 4;
  // 消息在 log_at 里与整条记录共享同一块缓冲，保留名字仅为兼容旧代码
  static constexpr std::size_t max_payload_size = ELOG_MAX_RECORD_SIZE;
  static constexpr std::size_t max_record_size = ELOG_MAX_RECORD_SIZE;
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

  // 返回 false 表示写失败；日志调用路径不查看它（放不下的整行本来就丢弃）
  [[nodiscard]] bool write(const char *data, std::size_t size) const {
    if (write_fn_ != nullptr) {
      return write_fn_(data, size, user_data_);
    }

    if (efmt_fn_ != nullptr) {
      efmt_fn_(data, size);
      return true;
    }

    return false;
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
  // 返回 false：sink 无效或槽位已满
  [[nodiscard]] bool add_sink(const sink& value) {
    if (!value.is_valid() || count_ >= sinks_.size()) {
      return false;
    }

    sinks_[count_++] = value;
    return true;
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
      if (!self->sinks_[index].write(data, size)) {
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

  // 单遍直写：前缀先写进记录缓冲，消息紧接着写在它后面，一遍就完。
  // 参数一律按 const 引用转发：日志调用不该复制实参（字符串/自定义类型尤其明显）。
  // 放不下的一行整体丢弃（不输出半行）。
  template <typename... Args>
  void log(level value, const char *fmt, const Args &...args) const {
    log_at(value, source_location{}, fmt, args...);
  }

  template <typename... Args>
  void log_at(level value, const source_location &location,
              const char *fmt, const Args &...args) const {
    if (!should_log(value)) {
      return;
    }

    if (!sink_.is_valid()) {
      return;
    }

    char record[config::max_record_size + 1U];

    // 前缀是固定字面量：交给 E_FMT_STR 走编译期预解析快路径（跳过运行期扫描/规范解析）；
    // 消息是用户运行时格式串，照常走运行期路径。efmt 层实测同一行 ~113→~87ns（GCC x64 -O2），
    // elog 整行日志 ~151→~113ns；与 spdlog 同口径对拍由快转优（对拍源码见本次提交说明）。
    const std::size_t prefix_size = e_fmt::format_to(
        record, sizeof(record), E_FMT_STR("[{}] [{}:{} {}] "), to_string(value),
        basename(location.file), location.line, location.function);
    if (prefix_size >= sizeof(record)) {
      return;
    }

    // format_to 返回完整输出所需长度（snprintf 语义）：放不下就是消息太长 → 整行丢弃
    const std::size_t body_size = e_fmt::format_to(
        record + prefix_size, sizeof(record) - prefix_size, fmt, args...);
    if (body_size >= sizeof(record) - prefix_size) {
      return;
    }

    write_with_style(value, record, prefix_size + body_size);
    static constexpr char newline = '\n';
    (void)sink_.write(&newline, 1U);
  }
  template <typename... Args> void trace(const char *fmt, const Args &...args) const {
    log(level::trace, fmt, args...);
  }
  template <typename... Args> void debug(const char *fmt, const Args &...args) const {
    log(level::debug, fmt, args...);
  }
  template <typename... Args> void info(const char *fmt, const Args &...args) const {
    log(level::info, fmt, args...);
  }
  template <typename... Args> void warn(const char *fmt, const Args &...args) const {
    log(level::warn, fmt, args...);
  }
  template <typename... Args> void error(const char *fmt, const Args &...args) const {
    log(level::error, fmt, args...);
  }
  template <typename... Args> void critical(const char *fmt, const Args &...args) const {
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

  void write_with_style(level value, const char* data,
                        std::size_t size) const {
    const auto style = style_for_level(value);
    if (!style.is_empty()) {
      char style_buffer[64];
      const auto style_size =
          e_fmt::detail::style_builder::build(style, style_buffer, sizeof(style_buffer));
      if (style_size > 0U) {
        (void)sink_.write(style_buffer, style_size);
      }
    }

    (void)sink_.write(data, size);

    if (!style.is_empty()) {
      (void)sink_.write(e_fmt::detail::style_builder::reset(),
                        e_fmt::detail::style_builder::reset_length());
    }
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

  // 失败返回 nullptr：名字非法 / sink 无效 / 重名 / 注册表满
  logger *create(const char *name, const sink &sink_value,
                 level initial_level = level::info) {
    if (!validate_name(name) || !sink_value.is_valid() || find_index(name) >= 0) {
      return nullptr;
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

    return nullptr;
  }

  logger *get(const char *name) {
    const auto index = find_index(name);
    if (index < 0) {
      return nullptr;
    }
    return &loggers_[static_cast<std::size_t>(index)];
  }

  const logger *get(const char *name) const {
    const auto index = find_index(name);
    if (index < 0) {
      return nullptr;
    }
    return &loggers_[static_cast<std::size_t>(index)];
  }

  bool set_default(const char *name) {
    const auto index = find_index(name);
    if (index < 0) {
      return false;
    }

    default_index_ = index;
    return true;
  }

  bool set_default(logger &value) {
    const auto index = find_index(value.name());
    if (index < 0) {
      return false;
    }

    default_index_ = index;
    return true;
  }

  // 未设置时返回 nullptr
  logger *default_logger() {
    if (default_index_ < 0) {
      return nullptr;
    }

    return &loggers_[static_cast<std::size_t>(default_index_)];
  }

  const logger *default_logger() const {
    if (default_index_ < 0) {
      return nullptr;
    }

    return &loggers_[static_cast<std::size_t>(default_index_)];
  }

private:
  static bool validate_name(const char *name) {
    return name != nullptr && name[0] != '\0' &&
           std::strlen(name) <= config::max_logger_name;
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

// 失败返回 nullptr；bool 版本返回是否成功
inline logger *create_logger(const char *name, const sink &sink_value,
                             level initial_level = level::info) {
  return registry::instance().create(name, sink_value, initial_level);
}

inline logger *get(const char *name) {
  return registry::instance().get(name);
}

inline bool set_default_logger(const char *name) {
  return registry::instance().set_default(name);
}

inline bool set_default_logger(logger &value) {
  return registry::instance().set_default(value);
}

inline logger *default_logger() {
  return registry::instance().default_logger();
}

template <typename... Args> void log(level value, const char *fmt, const Args &...args) {
  if (logger *target = default_logger()) {
    target->log(value, fmt, args...);
  }
}

template <typename... Args>
void log_at(level value, const source_location& location, const char *fmt,
            const Args &...args) {
  if (logger *target = default_logger()) {
    target->log_at(value, location, fmt, args...);
  }
}

template <typename... Args> void trace(const char *fmt, const Args &...args) {
  log(level::trace, fmt, args...);
}
template <typename... Args> void debug(const char *fmt, const Args &...args) {
  log(level::debug, fmt, args...);
}
template <typename... Args> void info(const char *fmt, const Args &...args) {
  log(level::info, fmt, args...);
}
template <typename... Args> void warn(const char *fmt, const Args &...args) {
  log(level::warn, fmt, args...);
}
template <typename... Args> void error(const char *fmt, const Args &...args) {
  log(level::error, fmt, args...);
}
template <typename... Args> void critical(const char *fmt, const Args &...args) {
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
  return e_log::sink::from_efmt_output(e_fmt::get_output_handler()).write(data, size);
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

// ============================================================================
// ETL 类型支持（elog 层适配，efmt 核心零改动）
// ============================================================================
// elog 本身依赖 ETL（etl::array 是内部实现），这里顺带把 ETL 常用类型接进
// efmt 的格式化体系：
//   * etl::string<N> / etl::istring / etl::string_view -> 文本输出（宽度/
//     精度/对齐等格式规范照常生效）
//   * etl::optional<T>        -> 有值打值，空打 nullopt
//   * etl::pair<T1,T2>        -> 打 (a: b)，与 efmt 的 std::pair 风格一致
//   * etl::variant<Ts...>     -> 打当前活跃值（C++17，etl::visit）
//   * 容器（vector/map/list/deque/set/span/...）无需特化：efmt 的通用迭代器
//     通道自动覆盖，开关见文件顶部的 EFMT_ENABLE_CONTAINER_FORMAT。
// 实现说明：这里只能写 formatter 偏特化，不能追加 make_format_arg 重载——
// make_format_arg 的调用发生在 efmt 模板（pack_format_args）内部，两阶段查找
// 在模板定义点锁定了重载集，elog 层追加的重载永远不会被看见；而 formatter
// 偏特化在用户实例化点可见，能正常接管。
#include <middleware/etl/string.h>
#include <middleware/etl/string_view.h>
#include <middleware/etl/optional.h>
#include <middleware/etl/utility.h>
#include <middleware/etl/variant.h>

#include <type_traits>

namespace e_fmt::detail {

template <std::size_t N>
struct formatter<etl::string<N>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const etl::string<N> &value) {
    string_formatter::format(ctx, specs,
                             std::string_view(value.data(), value.size()));
  }
};

template <>
struct formatter<etl::istring> {
  static void format(format_context &ctx, const format_specs &specs,
                     const etl::istring &value) {
    string_formatter::format(ctx, specs,
                             std::string_view(value.data(), value.size()));
  }
};

template <>
struct formatter<etl::string_view> {
  static void format(format_context &ctx, const format_specs &specs,
                     const etl::string_view &value) {
    string_formatter::format(ctx, specs,
                             std::string_view(value.data(), value.size()));
  }
};

template <typename T>
struct formatter<etl::optional<T>> {
  static void format(format_context &ctx, const format_specs &specs,
                     const etl::optional<T> &value) {
    if (value.has_value()) {
      format_specs default_specs;
      formatter<T>::format(ctx, default_specs, value.value());
    } else {
      ctx.write_aligned("nullopt", specs);
    }
  }
};

template <typename T1, typename T2>
struct formatter<etl::pair<T1, T2>> {
  static void format(format_context &ctx, const format_specs &,
                     const etl::pair<T1, T2> &value) {
    ctx.write_char('(');
    format_specs default_specs;
    formatter<T1>::format(ctx, default_specs, value.first);
    ctx.write_str(": ");
    formatter<T2>::format(ctx, default_specs, value.second);
    ctx.write_char(')');
  }
};

#if ETL_USING_CPP17
template <typename... Types>
struct formatter<etl::variant<Types...>> {
  static void format(format_context &ctx, const format_specs &,
                     const etl::variant<Types...> &value) {
    format_specs default_specs;
    etl::visit(
        [&](const auto &item) {
          formatter<std::decay_t<decltype(item)>>::format(ctx, default_specs,
                                                          item);
        },
        value);
  }
};
#endif

} // namespace e_fmt::detail

#endif // ELOG_HPP
