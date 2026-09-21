/**
 ******************************************************************************
 * @file           : format_float.hpp
 * @author         : ruixuezhao
 * @brief          : 自带浮点格式化引擎（不依赖 libc 的 printf/snprintf）
 * @attention      : 逐位对齐 printf：定点 f、科学计数 e、通用 g，默认舍入方式
 *                   为 round-half-even，与 glibc / newlib / UCRT 一致。
 * @date           : 26-3-22
 ******************************************************************************
 */

#ifndef FORMAT_FLOAT_HPP
#define FORMAT_FLOAT_HPP

#include <middleware/efmt/core/format_base.hpp>
#include <middleware/efmt/core/format_context.hpp>
#include <middleware/efmt/core/format_specs.hpp>

#include <cstdint>
#include <cstring>

namespace e_fmt::detail {

// ============================================================================
// 为什么自带一套浮点格式化
// ============================================================================
// libc 的 %f 在嵌入式上代价极高：newlib-nano 下仅仅一处 {:.2f} 就会让 Cortex-M4
// 的 .text 增加约 3.5 KB，并把 _malloc_r/_free_r/_sbrk 一起链进固件（printf 的
// dtoa 会动态分配内存）。这里的实现只用定点整数运算：
//
//   * 不需要 <cstdio>、<cmath>、libm，也不需要堆
//   * 二进制侧精确缩放（value·10^F 用大整数算准），十进制侧精确舍入，全程只
//     舍入一次，因此与 printf 逐位一致（round-half-even）
//   * 栈占用固定，由 EFMT_FLOAT_BIGNUM_LIMBS / EFMT_FLOAT_DIGIT_GROUPS 决定

// ---------------------------------------------------------------------------
// double 的二进制解剖
// ---------------------------------------------------------------------------
// 不调用 <cmath>（避免链入 libm / soft-float 辅助函数），只读 IEEE-754 的位。
struct binary_float {
  bool negative = false;
  bool is_nan = false;
  bool is_inf = false;
  bool is_zero = false;
  uint64_t mantissa = 0;  // value = mantissa * 2^exponent
  int exponent = 0;
};

inline binary_float decompose_double(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));

  binary_float out;
  out.negative = (bits >> 63) != 0;
  const uint32_t raw_exp = static_cast<uint32_t>((bits >> 52) & 0x7FFu);
  const uint64_t frac = bits & 0x000FFFFFFFFFFFFFull;

  out.is_nan = (raw_exp == 0x7FFu) && (frac != 0);
  out.is_inf = (raw_exp == 0x7FFu) && (frac == 0);
  out.is_zero = (raw_exp == 0) && (frac == 0);
  if (raw_exp == 0) {
    out.mantissa = frac;  // 次正规数（含 ±0）
    out.exponent = -1074;
  } else {
    out.mantissa = frac | (uint64_t(1) << 52);
    out.exponent = static_cast<int>(raw_exp) - 1075;
  }
  return out;
}

// ---------------------------------------------------------------------------
// 定点大整数
// ---------------------------------------------------------------------------
// 32 位 limb、小端、容量固定：无动态分配、无异常。任何一处判断为"放不下"都返回
// false，由调用方降精度（容量说明见文件末尾）。
template <size_t Limbs> class big_uint {
public:
  void clear() { n_ = 0; }

  void set_u64(uint64_t value) {
    n_ = 0;
    while (value != 0) {
      limb_[n_++] = static_cast<uint32_t>(value);
      value >>= 32;
    }
  }

  bool is_zero() const { return n_ == 0; }
  size_t limb_count() const { return n_; }

  // * 小整数（< 2^32）
  bool mul_small(uint32_t m) {
    if (n_ == 0) {
      return true;
    }
    uint64_t carry = 0;
    for (size_t i = 0; i < n_; ++i) {
      const uint64_t cur = static_cast<uint64_t>(limb_[i]) * m + carry;
      limb_[i] = static_cast<uint32_t>(cur);
      carry = cur >> 32;
    }
    while (carry != 0) {
      if (n_ >= Limbs) {
        return false;
      }
      limb_[n_++] = static_cast<uint32_t>(carry);
      carry >>= 32;
    }
    return true;
  }

  // * 5^p（5^13 < 2^31，按 13 次幂分块，余数逐次乘 5）
  bool mul_pow5(unsigned p) {
    constexpr uint32_t five13 = 1220703125u;  // 5^13
    while (p >= 13) {
      if (!mul_small(five13)) {
        return false;
      }
      p -= 13;
    }
    while (p > 0) {
      if (!mul_small(5u)) {
        return false;
      }
      --p;
    }
    return true;
  }

  // 左移二进制位
  bool shl(size_t bits) {
    if (n_ == 0 || bits == 0) {
      return true;
    }
    const size_t limb_shift = bits / 32;
    const size_t bit_shift = bits % 32;
    const size_t extra = limb_shift + ((bit_shift != 0) ? 1u : 0u);
    if (n_ + extra > Limbs) {
      return false;
    }

    if (bit_shift == 0) {
      for (size_t i = n_; i-- > 0;) {
        limb_[i + limb_shift] = limb_[i];
      }
    } else {
      limb_[n_ + limb_shift] = limb_[n_ - 1] >> (32 - bit_shift);
      for (size_t i = n_ - 1; i > 0; --i) {
        limb_[i + limb_shift] =
            (limb_[i] << bit_shift) | (limb_[i - 1] >> (32 - bit_shift));
      }
      limb_[limb_shift] = limb_[0] << bit_shift;
    }
    for (size_t i = 0; i < limb_shift; ++i) {
      limb_[i] = 0;
    }
    n_ += extra;
    while (n_ > 0 && limb_[n_ - 1] == 0) {
      --n_;
    }
    return true;
  }

  // 右移并四舍五入（round-half-even）：丢弃部分 > 1/2，或 = 1/2 且当前最低位为
  // 奇数时进位。printf 默认就是这个舍入模式。
  void shr_round(size_t bits) {
    if (bits == 0 || n_ == 0) {
      return;
    }

    const size_t half_index = bits - 1;
    const size_t half_limb = half_index / 32;
    const size_t half_bit = half_index % 32;
    bool half = false;
    bool sticky = false;
    if (half_limb < n_) {
      half = ((limb_[half_limb] >> half_bit) & 1u) != 0;
      if (half_bit > 0) {
        sticky = (limb_[half_limb] & ((uint32_t(1) << half_bit) - 1u)) != 0;
      }
    }
    for (size_t i = 0; i < half_limb && i < n_ && !sticky; ++i) {
      sticky = limb_[i] != 0;
    }

    const size_t limb_shift = bits / 32;
    const size_t bit_shift = bits % 32;
    if (limb_shift >= n_) {
      n_ = 0;
    } else {
      const size_t keep = n_ - limb_shift;
      if (bit_shift == 0) {
        for (size_t i = 0; i < keep; ++i) {
          limb_[i] = limb_[i + limb_shift];
        }
      } else {
        for (size_t i = 0; i < keep; ++i) {
          const uint32_t low = limb_[i + limb_shift] >> bit_shift;
          const uint32_t high = (i + limb_shift + 1 < n_)
                                    ? (limb_[i + limb_shift + 1] << (32 - bit_shift))
                                    : 0u;
          limb_[i] = low | high;
        }
      }
      n_ = keep;
      while (n_ > 0 && limb_[n_ - 1] == 0) {
        --n_;
      }
    }

    if (half && (sticky || ((n_ > 0) && ((limb_[0] & 1u) != 0)))) {
      add_small(1);
    }
  }

  // 除以小整数，返回余数（商留在自身）
  uint32_t divmod_small(uint32_t divisor) {
    uint64_t rem = 0;
    for (size_t i = n_; i-- > 0;) {
      const uint64_t cur = (rem << 32) | limb_[i];
      limb_[i] = static_cast<uint32_t>(cur / divisor);
      rem = cur % divisor;
    }
    while (n_ > 0 && limb_[n_ - 1] == 0) {
      --n_;
    }
    return static_cast<uint32_t>(rem);
  }

private:
  void add_small(uint32_t value) {
    uint64_t carry = value;
    size_t i = 0;
    while (carry != 0 && i < n_) {
      const uint64_t cur = static_cast<uint64_t>(limb_[i]) + carry;
      limb_[i] = static_cast<uint32_t>(cur);
      carry = cur >> 32;
      ++i;
    }
    if (carry != 0 && n_ < Limbs) {
      limb_[n_++] = static_cast<uint32_t>(carry);
    }
  }

  uint32_t limb_[Limbs];
  size_t n_ = 0;
};

// ---------------------------------------------------------------------------
// 十进制数字缓冲
// ---------------------------------------------------------------------------
// 每 9 位十进制数字打包进一个 uint32，低位组在前。low 是"最低有效位在组数组中的
// 绝对下标"：丢弃低位（舍入）只是把 low 往后挪，不需要搬数据。
struct decimal_digits {
  static constexpr size_t group_digits = 9;
  static constexpr uint32_t pow10_table[9] = {1u,     10u,     100u,     1000u,
                                              10000u, 100000u, 1000000u, 10000000u,
                                              100000000u};

  uint32_t *groups = nullptr;
  int low = 0;    // 最低有效位的绝对下标
  int total = 0;  // 有效位数（最高位不为 0）

  int digit_at_low(int j) const {
    return static_cast<int>((groups[j / 9] / pow10_table[j % 9]) % 10u);
  }

  void set_digit_at_low(int j, uint32_t value) {
    uint32_t &group = groups[j / 9];
    const uint32_t place = pow10_table[j % 9];
    group = group - (group / place % 10u) * place + value * place;
  }

  // i = 0 表示最低位
  int digit(int i) const { return digit_at_low(low + i); }
  void set_digit(int i, uint32_t value) { set_digit_at_low(low + i, value); }
};

// ---------------------------------------------------------------------------
// 数字字段排版（符号 / '0' 填充 / 宽度 / 对齐）
// ---------------------------------------------------------------------------
struct field_layout {
  char fill = ' ';
  size_t left_fill = 0;
  size_t zero_fill = 0;
  size_t right_fill = 0;
};

inline field_layout layout_field(const format_specs &specs, size_t body_len) {
  field_layout out;
  out.fill = specs.fill;
  const size_t width = (specs.width > 0) ? static_cast<size_t>(specs.width) : 0;
  const size_t pad = (width > body_len) ? (width - body_len) : 0;
  if (pad == 0) {
    return out;
  }

  // '0' 选项只在没有显式对齐时生效，补零位置在符号之后（printf 语义）
  if (specs.zero && specs.alignment == align::none) {
    out.zero_fill = pad;
    return out;
  }

  switch (specs.alignment) {
  case align::left:
    out.right_fill = pad;
    break;
  case align::center:
    out.left_fill = pad / 2;
    out.right_fill = pad - out.left_fill;
    break;
  default:
    out.left_fill = pad;
    break;
  }
  return out;
}

inline void write_sign(format_context &ctx, bool negative, sign mode) {
  if (negative) {
    ctx.write_char('-');
  } else if (mode == sign::plus) {
    ctx.write_char('+');
  } else if (mode == sign::space) {
    ctx.write_char(' ');
  }
}

// ---------------------------------------------------------------------------
// 自带浮点格式化器
// ---------------------------------------------------------------------------
class builtin_float_formatter {
public:
  using bignum = big_uint<EFMT_FLOAT_BIGNUM_LIMBS>;

  static void format(format_context &ctx, const format_specs &specs, double value) {
    const binary_float bin = decompose_double(value);

    if (bin.is_nan) {
      write_special(ctx, specs, bin.negative, "nan", "NAN");
      return;
    }
    if (bin.is_inf) {
      write_special(ctx, specs, bin.negative, "inf", "INF");
      return;
    }

    const bool upper = is_upper(specs);
    int precision = specs.precision;
    if (precision < 0) {
      precision = 6;  // printf 的默认精度
    }

    switch (specs.type) {
    case presentation::exp_lower:
    case presentation::exp_upper:
      format_scientific(ctx, specs, bin, static_cast<unsigned>(precision), upper);
      return;
    case presentation::general_lower:
    case presentation::general_upper:
    case presentation::none:
    case presentation::chr:
    case presentation::str:
      format_general(ctx, specs, bin, static_cast<unsigned>(precision), upper);
      return;
    default:  // f/F（以及历史行为：x/X 也按定点处理）
      format_fixed(ctx, specs, bin, static_cast<unsigned>(precision));
      return;
    }
  }

private:
  // --- 特殊值 ---------------------------------------------------------------
  static bool is_upper(const format_specs &specs) {
    switch (specs.type) {
    case presentation::fixed_upper:
    case presentation::exp_upper:
    case presentation::general_upper:
    case presentation::hex_upper:
      return true;
    default:
      return false;
    }
  }

  static void write_special(format_context &ctx, const format_specs &specs,
                            bool negative, const char *lower, const char *upper) {
    const char *text = is_upper(specs) ? upper : lower;
    char buffer[4];
    size_t prefix = 0;
    if (negative) {
      buffer[prefix++] = '-';
    } else if (specs.sign_mode == sign::plus) {
      buffer[prefix++] = '+';
    } else if (specs.sign_mode == sign::space) {
      buffer[prefix++] = ' ';
    }
    std::memcpy(buffer + prefix, text, 3);
    ctx.write_aligned(std::string_view(buffer, prefix + 3), specs);
  }

  // 0 的统一排版：符号 + precision 位小数（precision == 0 时不输出小数点）
  static void write_zero(format_context &ctx, const format_specs &specs,
                         bool negative, unsigned precision, bool scientific) {
    const bool point = (precision > 0) || specs.alt;
    size_t body = 1;  // 整数部分的 '0'
    body += (negative || specs.sign_mode == sign::plus ||
             specs.sign_mode == sign::space)
                ? 1u
                : 0u;
    body += (point ? 1u : 0u) + precision;
    if (scientific) {
      body += 4;  // "e+00"
    }
    const field_layout layout = layout_field(specs, body);

    ctx.write_fill(layout.fill, layout.left_fill);
    write_sign(ctx, negative, specs.sign_mode);
    ctx.write_fill('0', layout.zero_fill);
    ctx.write_char('0');
    if (point) {
      ctx.write_char('.');
    }
    ctx.write_fill('0', precision);
    if (scientific) {
      ctx.write_char(is_upper(specs) ? 'E' : 'e');
      ctx.write_char('+');
      ctx.write_char('0');
      ctx.write_char('0');
    }
    ctx.write_fill(layout.fill, layout.right_fill);
  }

  // --- 二进制 → 精确定点整数 ------------------------------------------------
  // 计算 round(value · 10^frac_digits)（round-half-even，二进制侧一次舍入）。
  static bool scaled_rounded(const binary_float &bin, unsigned frac_digits,
                             bignum &out) {
    out.clear();
    out.set_u64(bin.mantissa);
    if (!out.mul_pow5(frac_digits)) {
      return false;
    }
    const long shift =
        static_cast<long>(bin.exponent) + static_cast<long>(frac_digits);
    if (shift >= 0) {
      return out.shl(static_cast<size_t>(shift));
    }
    out.shr_round(static_cast<size_t>(-shift));
    return true;
  }

  // --- 大整数 → 十进制 ------------------------------------------------------
  // 返回有效位数；-1 表示超出 EFMT_FLOAT_DIGIT_GROUPS（调用方降精度）。
  static int to_digits(bignum &value, decimal_digits &out) {
    int used = 0;
    while (!value.is_zero()) {
      // 最高一组留给进位（999… 舍入成 1000…）
      if (used >= EFMT_FLOAT_DIGIT_GROUPS - 1) {
        return -1;
      }
      out.groups[used++] = value.divmod_small(1000000000u);
    }

    if (used == 0) {
      out.groups[0] = 0;
      out.low = 0;
      out.total = 1;
      return 1;
    }

    int digits = (used - 1) * 9;
    uint32_t top = out.groups[used - 1];
    while (top != 0) {
      ++digits;
      top /= 10;
    }
    out.low = 0;
    out.total = digits;
    return digits;
  }

  // 十进制侧舍入到 keep 位有效数字（round-half-even），返回新的位数
  static int round_keep(decimal_digits &d, int total, int keep) {
    const int drop = total - keep;
    const int round_index = drop - 1;  // 被丢弃的最高位（从最低位算）
    const int round_digit = d.digit(round_index);

    bool sticky = false;
    for (int i = 0; i < round_index && !sticky; ++i) {
      sticky = d.digit(i) != 0;
    }

    d.low += drop;  // 丢弃低位只是挪起点
    d.total = keep;

    const bool last_odd = (d.digit(0) & 1) != 0;
    if (round_digit > 5 || (round_digit == 5 && (sticky || last_odd))) {
      int i = 0;
      for (; i < d.total; ++i) {
        const int next = d.digit(i) + 1;
        if (next < 10) {
          d.set_digit(i, static_cast<uint32_t>(next));
          return d.total;
        }
        d.set_digit(i, 0);
      }
      // 999… → 1000…：低位清零，最高位多出一位（下标 0 是最低位）
      for (int j = 0; j < d.total; ++j) {
        d.set_digit(j, 0);
      }
      d.set_digit(d.total, 1);
      d.total += 1;
    }
    return d.total;
  }

  // floor(log10(value)) 的估计
  // value ∈ [2^(e+bits-1), 2^(e+bits))，这里取下界：估计值一定 <= 真实指数，
  // 于是第一遍不会把有效数字舍掉（只可能多算几位），再由位数校验收敛。
  static int estimate_exponent(const binary_float &bin) {
    int bits = 0;
    for (uint64_t m = bin.mantissa; m != 0; m >>= 1) {
      ++bits;
    }
    const int lower_binary = bin.exponent + bits - 1;  // value >= 2^lower_binary
    // log10(2) ≈ 30103/100000，负数也要向下取整
    const long product = static_cast<long>(lower_binary) * 30103;
    return static_cast<int>(product >= 0 ? (product / 100000)
                                         : -(((-product) + 99999) / 100000));
  }

  // 符号占的宽度（宽度/对齐要把符号算进字段里）
  static size_t sign_length(const format_specs &specs, bool negative) {
    return (negative || specs.sign_mode == sign::plus ||
            specs.sign_mode == sign::space)
               ? 1u
               : 0u;
  }

  // 舍入到 sig 位有效数字：得到数字串 d 与十进制指数 k（值 ≈ d1.d2d3… × 10^k）
  static bool round_significant(const binary_float &bin, unsigned sig,
                                decimal_digits &d, int &exponent) {
    if (sig == 0) {
      sig = 1;
    }
    int k = estimate_exponent(bin);

    for (int attempt = 0; attempt < 3; ++attempt) {
      const long frac_digits = static_cast<long>(sig) - 1 - k;
      if (frac_digits >= 0) {
        int digits = -1;
        {
          bignum scaled;
          if (!scaled_rounded(bin, static_cast<unsigned>(frac_digits), scaled)) {
            return false;
          }
          digits = to_digits(scaled, d);
        }
        if (digits < 0) {
          return false;
        }
        const int k_new = digits - 1 - static_cast<int>(frac_digits);
        if (k_new == k) {
          exponent = k;
          return true;
        }
        k = k_new;  // 估计偏了一位：用真实位数重算
        continue;
      }

      // 值远大于精度（例如 1e300 打印 {:.2e}）：先取精确整数，再在十进制侧舍入
      const unsigned exact_frac =
          (bin.exponent >= 0) ? 0u : static_cast<unsigned>(-bin.exponent);
      int digits = -1;
      {
        bignum exact;
        if (!scaled_rounded(bin, exact_frac, exact)) {
          return false;
        }
        digits = to_digits(exact, d);
      }
      if (digits < 0) {
        return false;
      }
      const int keep =
          (static_cast<int>(sig) < digits) ? static_cast<int>(sig) : digits;
      const int dropped = digits - keep;  // 十进制侧丢掉的低位位数
      const int total = (dropped > 0) ? round_keep(d, digits, keep) : digits;
      // value = T / 10^(exact_frac - dropped)
      exponent = total - 1 - (static_cast<int>(exact_frac) - dropped);
      return true;
    }
    return false;
  }

  // --- 共享输出 -------------------------------------------------------------
  // 定点：符号 + 整数部分 + '.' + 小数部分
  //   int_len       整数位数（<= 0 时输出一个 '0'）
  //   leading_zeros 小数部分的前导零个数
  //   lowest        小数数字串要输出的最低下标（去尾零后的起点）
  //   frac_avail    小数数字串可用的最高下标 + 1
  //   frac_pad      再补多少个 '0'（%f 补到请求精度；%g 不补）
  static void emit_fixed(format_context &ctx, const format_specs &specs,
                         bool negative, const decimal_digits &d, int int_len,
                         int leading_zeros, int lowest, int frac_avail,
                         int frac_pad) {
    const int frac_digits = leading_zeros + (frac_avail - lowest) + frac_pad;
    const bool point = (frac_digits > 0) || specs.alt;
    const size_t body = sign_length(specs, negative) +
                        static_cast<size_t>(int_len > 0 ? int_len : 1) +
                        (point ? 1u : 0u) + static_cast<size_t>(frac_digits);
    const field_layout layout = layout_field(specs, body);

    ctx.write_fill(layout.fill, layout.left_fill);
    write_sign(ctx, negative, specs.sign_mode);
    ctx.write_fill('0', layout.zero_fill);

    if (int_len > 0) {
      for (int i = d.total - 1; i >= d.total - int_len; --i) {
        ctx.write_char(static_cast<char>('0' + d.digit(i)));
      }
    } else {
      ctx.write_char('0');
    }

    if (point) {
      ctx.write_char('.');
    }
    ctx.write_fill('0', static_cast<size_t>(leading_zeros));
    for (int i = frac_avail - 1; i >= lowest; --i) {
      ctx.write_char(static_cast<char>('0' + d.digit(i)));
    }
    ctx.write_fill('0', static_cast<size_t>(frac_pad));
    ctx.write_fill(layout.fill, layout.right_fill);
  }

  // 科学计数：d.ddde±XX，恰好 fraction_digits 位小数
  static void emit_scientific(format_context &ctx, const format_specs &specs,
                              bool negative, bool upper, const decimal_digits &d,
                              int exponent, int lowest, int fraction_digits) {
    const bool point = (fraction_digits > 0) || specs.alt;
    size_t exp_len = 2;  // 指数至少两位
    {
      int magnitude = (exponent < 0) ? -exponent : exponent;
      while (magnitude >= 100) {
        magnitude /= 10;
        ++exp_len;
      }
    }
    const size_t body = sign_length(specs, negative) + 1u + (point ? 1u : 0u) +
                        static_cast<size_t>(fraction_digits) + 2u + exp_len;
    const field_layout layout = layout_field(specs, body);

    ctx.write_fill(layout.fill, layout.left_fill);
    write_sign(ctx, negative, specs.sign_mode);
    ctx.write_fill('0', layout.zero_fill);

    ctx.write_char(static_cast<char>('0' + d.digit(d.total - 1)));
    if (point) {
      ctx.write_char('.');
    }
    int produced = 0;
    for (int i = d.total - 2; i >= lowest && produced < fraction_digits;
         --i, ++produced) {
      ctx.write_char(static_cast<char>('0' + d.digit(i)));
    }
    ctx.write_fill('0', static_cast<size_t>(fraction_digits - produced));
    write_exponent(ctx, exponent, upper);
    ctx.write_fill(layout.fill, layout.right_fill);
  }

  // --- 定点：整数部分 + '.' + precision 位小数 ------------------------------
  static void format_fixed(format_context &ctx, const format_specs &specs,
                           const binary_float &bin, unsigned precision) {
    if (bin.is_zero) {
      write_zero(ctx, specs, bin.negative, precision, false);
      return;
    }

    uint32_t group_buffer[EFMT_FLOAT_DIGIT_GROUPS];
    decimal_digits d;
    d.groups = group_buffer;

    // 值在 10^-exact_frac 位之后全是 0，不需要更多位
    const unsigned exact_frac =
        (bin.exponent >= 0) ? 0u : static_cast<unsigned>(-bin.exponent);
    unsigned scaled = (precision < exact_frac) ? precision : exact_frac;

    // 大整数用完就出作用域：它的栈槽能和后面的输出逻辑复用，浮点路径的栈峰值
    // 因此少一个 bignum（嵌入式栈紧张时很值钱）
    int digits = -1;
    {
      bignum value;
      for (;;) {
        if (scaled_rounded(bin, scaled, value)) {
          digits = to_digits(value, d);
          if (digits >= 0) {
            break;
          }
        }
        if (scaled == 0) {
          return;  // 容量兜底：连整数部分都放不下（真实 double 不会发生）
        }
        --scaled;
      }
    }

    const int int_len =
        (digits > static_cast<int>(scaled)) ? (digits - static_cast<int>(scaled)) : 0;
    const int leading_zeros =
        (digits < static_cast<int>(scaled)) ? (static_cast<int>(scaled) - digits) : 0;
    const int frac_avail = (digits < static_cast<int>(scaled)) ? digits : static_cast<int>(scaled);
    emit_fixed(ctx, specs, bin.negative, d, int_len, leading_zeros, 0, frac_avail,
               static_cast<int>(precision) - static_cast<int>(scaled));
  }

  // --- 科学计数：d.ddde±XX（恰好 precision + 1 位有效数字）------------------
  static void format_scientific(format_context &ctx, const format_specs &specs,
                                const binary_float &bin, unsigned precision,
                                bool upper) {
    if (bin.is_zero) {
      write_zero(ctx, specs, bin.negative, precision, true);
      return;
    }

    uint32_t group_buffer[EFMT_FLOAT_DIGIT_GROUPS];
    decimal_digits d;
    d.groups = group_buffer;

    // 有效数字 = precision + 1（precision 为 0 也要输出 1 位，例如 "1e+00"）
    unsigned sig = precision + 1;
    int exponent = 0;
    while (!round_significant(bin, sig, d, exponent)) {
      if (sig == 1) {
        return;  // 容量兜底都失败，只能放弃这个字段
      }
      --sig;  // 容量兜底：精度退一位，最后按原精度补零
    }

    emit_scientific(ctx, specs, bin.negative, upper, d, exponent, 0,
                    static_cast<int>(precision));
  }

  // --- 通用：按有效位数选定点/科学计数，非 '#' 时去掉尾零 --------------------
  static void format_general(format_context &ctx, const format_specs &specs,
                             const binary_float &bin, unsigned precision,
                             bool upper) {
    if (precision == 0) {
      precision = 1;  // printf：%g 的精度 0 等同于 1
    }
    if (bin.is_zero) {
      write_zero(ctx, specs, bin.negative, specs.alt ? precision - 1 : 0, false);
      return;
    }

    uint32_t group_buffer[EFMT_FLOAT_DIGIT_GROUPS];
    decimal_digits d;
    d.groups = group_buffer;

    unsigned sig = precision;
    int exponent = 0;
    while (!round_significant(bin, sig, d, exponent)) {
      if (sig == 1) {
        return;
      }
      --sig;  // 容量兜底
    }
    const bool strip = !specs.alt;

    if (exponent < -4 || exponent >= static_cast<int>(sig)) {
      // 科学计数分支：有效数字 sig 位（尾零在发射窗口的低位）
      int lowest = d.total - static_cast<int>(sig);
      if (strip) {
        while (lowest < d.total - 1 && d.digit(lowest) == 0) {
          ++lowest;
        }
      }
      emit_scientific(ctx, specs, bin.negative, upper, d, exponent, lowest,
                      d.total - lowest - 1);
      return;
    }

    // 定点分支：整数 int_len 位（<= 0 时输出 "0"），小数 frac 位
    const int frac = static_cast<int>(sig) - 1 - exponent;
    const int int_len = exponent + 1;
    const int frac_avail = (int_len > 0) ? (d.total - int_len) : d.total;
    const int leading_zeros = frac - frac_avail;  // 值 < 1 时的小数前导零

    // 去尾零：小数的尾零在数字串的低位，从下标 0 往上跳过
    int lowest = 0;
    if (strip) {
      while (lowest < frac_avail && d.digit(lowest) == 0) {
        ++lowest;
      }
    }
    emit_fixed(ctx, specs, bin.negative, d, int_len, leading_zeros, lowest,
               frac_avail, 0);
  }

  static void write_exponent(format_context &ctx, int exponent, bool upper) {
    ctx.write_char(upper ? 'E' : 'e');
    if (exponent < 0) {
      ctx.write_char('-');
      exponent = -exponent;
    } else {
      ctx.write_char('+');
    }
    char digits[8];
    int count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (exponent % 10));
      exponent /= 10;
    } while (exponent != 0);
    while (count < 2) {
      digits[count++] = '0';
    }
    while (count > 0) {
      ctx.write_char(digits[--count]);
    }
  }
};

// ============================================================================
// 容量与降级说明
// ============================================================================
// 默认（EFMT_FLOAT_BIGNUM_LIMBS=48、EFMT_FLOAT_DIGIT_GROUPS=48）的覆盖范围：
//   * 任意 double 的整数部分（最大 309 位十进制）都能精确输出
//   * %f 精度 <= 600、%e/%g 精度 <= 400 与 printf 逐位一致
//   * 输出长度上限由 EFMT_FLOAT_DIGIT_GROUPS 决定（48 组 = 423 位数字）
// 超出容量时精度自动退到最后能算准的位数（不会写坏内存、不会输出错位），
// 需要"任意精度都精确"就调大这两个宏（80 + 120 覆盖全部 double）。

} // namespace e_fmt::detail

#endif // FORMAT_FLOAT_HPP
