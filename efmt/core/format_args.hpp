/**
 ******************************************************************************
 * @file           : format_args.hpp
 * @author         : ruixuezhao
 * @brief          : Type-erased argument storage for format
 * @attention      : None
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_ARGS_HPP
#define FORMAT_ARGS_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_specs.hpp>
#include <middleware/efmt/core/format_context.hpp>

namespace e_fmt::detail {
// Runtime argument storage for the variadic formatting API.
// Each original argument is converted into a small erased record containing
// a type tag, payload and formatter callback.

// 前向声明
class format_context;

// 参数类型枚举
enum class arg_type : uint8_t {
  none,
  integral,
  unsigned_integral,
  floating,
  boolean,
  string,
  pointer,
  char_type
};

// 类型擦除的参数值
// Deliberately trivial: the storage is written before it is read, so the
// packing path pays no initialization cost for the unused slots.
union format_arg_value {
  int64_t integral;
  uint64_t unsigned_integral;
  double floating;
  bool boolean;
  const char *string;
  const void *pointer;
  char char_value;
};

struct format_arg;

// 格式化函数指针类型
using format_fn = void (*)(format_context &ctx, const format_specs &specs,
                           const format_arg &value);

// 单个格式化参数
// Runtime representation of a single formatting argument.
// string_size rides in the padding after the tag, so carrying a length costs
// no extra storage and string arguments never need strlen().
struct format_arg {
  arg_type type;
  uint32_t string_size;
  format_arg_value value;
  format_fn formatter;

  // 各类型的 make 静态方法
  static format_arg make_integral(int64_t v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::integral;
    arg.value.integral = v;
    arg.formatter = f;
    return arg;
  }

  static format_arg make_uintegral(uint64_t v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::unsigned_integral;
    arg.value.unsigned_integral = v;
    arg.formatter = f;
    return arg;
  }

  static format_arg make_floating(double v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::floating;
    arg.value.floating = v;
    arg.formatter = f;
    return arg;
  }

  static format_arg make_boolean(bool v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::boolean;
    arg.value.boolean = v;
    arg.formatter = f;
    return arg;
  }

  static format_arg make_string(const char *v, size_t len, format_fn f) {
    format_arg arg;
    arg.type = arg_type::string;
    arg.value.string = v;
    arg.string_size = static_cast<uint32_t>(len);
    arg.formatter = f;
    return arg;
  }

  static format_arg make_char(char v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::char_type;
    arg.value.char_value = v;
    arg.formatter = f;
    return arg;
  }

  static format_arg make_pointer(const void *v, format_fn f) {
    format_arg arg;
    arg.type = arg_type::pointer;
    arg.value.pointer = v;
    arg.formatter = f;
    return arg;
  }
};

// 参数存储
// Fixed-size argument array addressed by replacement field index.
class format_args {
public:
  format_args() = default;

  void set(size_t index, const format_arg &arg) {
    if (index < max_format_args) {
      args_[index] = arg;
      size_ = (index >= size_) ? index + 1 : size_;
    }
  }

  const format_arg &get(size_t index) const {
    if (index < static_cast<size_t>(size_)) {
      return args_[index];
    }
    // Returning a stable empty object lets the caller convert this case into
    // an argument_out_of_range error without special casing null pointers.
    static const format_arg empty_arg{};
    return empty_arg;
  }

  [[nodiscard]] size_t size() const { return static_cast<size_t>(size_); }

private:
  format_arg args_[max_format_args];
  size_t size_ = 0;
};

} // namespace e_fmt::detail

#endif // FORMAT_ARGS_HPP
