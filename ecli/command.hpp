/**
 ******************************************************************************
 * @file           : command.hpp
 * @author         : ruixuezhao
 * @brief          : ecli 的命令表：多命令 / 子命令分发 + 输出回给"发起命令的那一路"
 * @attention      : 依赖方向：command.hpp → cli.hpp → eserde/traits.hpp → efmt。
 *
 *                   设计只用三句话就能说完：
 *                     1. 命令表是一个【零堆静态数组】：{名字, 帮助, 处理函数 thunk}
 *                     2. 命令名允许带空格 —— "wifi set" 就是子命令，匹配规则是
 *                        【最长 token 前缀】（"wifi set x" 命中 "wifi set"，
 *                        而不是 "wifi"），所以不需要树、不需要插值、不需要 new
 *                     3. 每个命令一个自包含 thunk：自己声明参数类型、自己 parse、
 *                        自己把帮助/报错写回 reply；参数类型仍然由 E_FMT_DERIVE 推导
 *
 *                   与"多输入源"的关系：dispatch 只认 token 表与 reply ——
 *                   命令从串口来就回串口，从蓝牙来就回蓝牙（reply 由调用方选）。
 *
 *                   用法：
 *                     void status_run(const status_args&, ecli::reply out) { out.put_lit("ok\n"); }
 *                     void set_run(const wifi_args& a, ecli::reply) { ... }
 *
 *                     constexpr ecli::command kCommands[] = {
 *                       {"status",   "show link status", ecli::command_of<status_args, status_run>()},
 *                       {"wifi set", "set ssid",         ecli::command_of<wifi_args, set_run>()},
 *                     };
 *
 *                     ecli::line_reader<128> line;      // 或 argv
 *                     ecli::dispatch(kCommands, line.line(), scratch, sizeof(scratch),
 *                                    ecli::reply_to<uart_write>());
 *
 *                   内置 help：help / help <命令名> / <命令> -h / -h / --help / ?
 ******************************************************************************
 */

#ifndef ECLI_COMMAND_HPP
#define ECLI_COMMAND_HPP

#include <ecli/cli.hpp>

#include <cstddef>
#include <string_view>

#if EFMT_ENABLE_STDIO
#include <cstdio>
#endif

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

// 帮助 / 报错文本的栈缓冲。装不下会如实追加 "...(truncated)"，不静默截断。
#ifndef ECLI_REPLY_BUFFER
#define ECLI_REPLY_BUFFER 384
#endif

namespace ecli {

// ---------------------------------------------------------------------------
// 回复通道：命令的输出回给"发起命令的那一路"
// ---------------------------------------------------------------------------
// 两个指针（上下文 + 写函数），零堆、可拷贝、可空（空 = 丢弃输出）。
struct reply {
  void *ctx = nullptr;
  void (*write)(void *ctx, const char *data, std::size_t size) = nullptr;

  void put(std::string_view text) const {
    if (write != nullptr && !text.empty()) write(ctx, text.data(), text.size());
  }
  void put_lit(const char *text) const { put(std::string_view(text)); }
  bool valid() const { return write != nullptr; }
};

namespace detail {
template <void (*Sink)(const char *, std::size_t)> struct sink_thunk {
  static void call(void *, const char *data, std::size_t size) { Sink(data, size); }
};
}  // namespace detail

// 嵌入式最常用的一种：库直接调你的 (data, size) 写函数（UART / RTT / 环形缓冲都行）
//   ecli::dispatch(kCommands, line.line(), scratch, sizeof(scratch),
//                  ecli::reply_to<uart_write>());
template <void (*Sink)(const char *, std::size_t)> inline reply reply_to() {
  return reply{nullptr, &detail::sink_thunk<Sink>::call};
}

// 写进一块定长缓冲（snprintf 语义：超出只计数，永远 NUL 结尾）
struct buffer_reply {
  char *buf = nullptr;
  std::size_t cap = 0;
  std::size_t used = 0;

  void write(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (used + 1 < cap) buf[used] = text[i];
      ++used;
    }
    if (cap != 0) buf[used < cap ? used : cap - 1] = '\0';
  }
  reply as_reply();   // 定义在下面（要先有 buffer_reply_write 的声明）
};

namespace detail {
inline void buffer_reply_write(void *ctx, const char *data, std::size_t size) {
  static_cast<ecli::buffer_reply *>(ctx)->write(std::string_view(data, size));
}
}  // namespace detail

inline reply buffer_reply::as_reply() { return reply{this, &detail::buffer_reply_write}; }

#if EFMT_ENABLE_STDIO
inline void stdout_sink(const char *data, std::size_t size) { std::fwrite(data, 1, size, stdout); }
inline reply stdout_reply() { return reply_to<stdout_sink>(); }
#endif

#if EFMT_ENABLE_DYNAMIC_STRING
namespace detail {
inline void string_reply_write(void *ctx, const char *data, std::size_t size) {
  static_cast<std::string *>(ctx)->append(data, size);
}
}  // namespace detail
inline reply string_reply(std::string &out) { return reply{&out, &detail::string_reply_write}; }
#endif

// ---------------------------------------------------------------------------
// 命令表
// ---------------------------------------------------------------------------
// 处理函数签名统一是 void(const Args&, reply)：参数已解析完，reply 用来回话。
// 不要回话的处理函数把第二个参数留空名字即可（避免 -Wunused-parameter）：
//   void status_run(const status_args& a, ecli::reply) { ... }
using invoke_fn = error (*)(std::string_view name, std::string_view about,
                            const token_list &tokens, reply out);

struct command {
  std::string_view name;   // 可含空格 = 子命令；按最长 token 前缀匹配
  std::string_view help;   // 命令表与 help <命令> 里显示的说明
  invoke_fn invoke;        // 由 command_of<Args, Fn>() 生成的自包含 thunk
};

namespace detail {

// 去掉前 n 个 token 的视图（token 仍指向同一块存储，只挪窗口）
inline token_list skip_tokens(const token_list &tokens, std::size_t n) {
  token_list out{};
  out.count = tokens.count > n ? tokens.count - n : 0;
  for (std::size_t i = 0; i < out.count; ++i) out.items[i] = tokens.items[n + i];
  out.overflow = tokens.overflow;
  out.bad_quote = tokens.bad_quote;
  return out;
}

// 命令名按空格切词，逐词与 token 比：全中 → 消耗的 token 数；否则 0
inline std::size_t match_command_name(std::string_view name, const token_list &tokens) {
  std::size_t k = 0;
  std::size_t i = 0;
  std::size_t words = 0;
  while (i < name.size()) {
    while (i < name.size() && name[i] == ' ') ++i;
    if (i >= name.size()) break;
    const std::size_t begin = i;
    while (i < name.size() && name[i] != ' ') ++i;
    if (k >= tokens.count || tokens.items[k] != name.substr(begin, i - begin)) return 0;
    ++k;
    ++words;
  }
  return words;
}

// 写文本：装不下就如实追加 "(truncated)" —— 标记要【先留出位置】，
// 否则正文把缓冲填满，标记自己反而塞不进去（第一版就是这么错的）。
inline void send_text(std::size_t need, const char *buf, std::size_t cap, reply out) {
  if (cap == 0) return;
  if (need < cap) {
    out.put(std::string_view(buf, need));
    return;
  }
  constexpr std::string_view kMark = "...(truncated)\n";
  const std::size_t keep = cap > kMark.size() + 1 ? cap - kMark.size() - 1 : 0;
  out.put(std::string_view(buf, keep));
  out.put(kMark);
}

template <typename T>
void send_help(std::string_view name, std::string_view about, reply out) {
  char buf[ECLI_REPLY_BUFFER];
  const std::size_t need = write_help<T>(name, about, buf, sizeof(buf));
  send_text(need, buf, sizeof(buf), out);
}

template <typename T>
void send_error(std::string_view name, error e, const error_info &info, reply out) {
  char buf[ECLI_REPLY_BUFFER];
  const std::size_t need = write_error<T>(name, e, info, buf, sizeof(buf));
  send_text(need, buf, sizeof(buf), out);
}

// 每个命令的自包含 thunk：声明参数类型 → 解析 → 回话 → 交给处理函数
template <typename Args, void (*Fn)(const Args &, reply)>
error command_thunk(std::string_view name, std::string_view about, const token_list &tokens,
                    reply out) {
  Args a{};
  error_info info{};
  const error e = parse(tokens, a, &info);
  if (e == error::help_requested) {
    send_help<Args>(name, about, out);
    return e;
  }
  if (e != error::ok) {
    send_error<Args>(name, e, info, out);
    return e;
  }
  Fn(a, out);
  return error::ok;
}

}  // namespace detail

// 把 (参数类型, 处理函数) 变成命令表里的一项（编译期，零运行时代价）
template <typename Args, void (*Fn)(const Args &, reply)>
inline constexpr invoke_fn command_of() {
  return &detail::command_thunk<Args, Fn>;
}

// ---------------------------------------------------------------------------
// 命令表文本：commands: 列表（左列按最长命令名对齐）
// ---------------------------------------------------------------------------
template <std::size_t N> std::size_t write_command_list(const command (&table)[N], reply out) {
  std::size_t width = 0;
  for (std::size_t i = 0; i < N; ++i) {
    const std::size_t w = 2 + table[i].name.size();
    if (w > width) width = w;
  }
  constexpr std::size_t k_help_column = 2 + 14;   // "  help [command]" 的显示宽度
  if (k_help_column > width) width = k_help_column;

  char buf[ECLI_REPLY_BUFFER];
  detail::text_out t{buf, sizeof(buf), 0};
  t.put_lit("commands:\n");
  for (std::size_t i = 0; i < N; ++i) {
    t.put_lit("  ");
    t.put(table[i].name);
    if (!table[i].help.empty()) {
      const std::size_t used = 2 + table[i].name.size();
      t.spaces(width >= used ? width - used + 2 : 2);
      t.put(table[i].help);
    }
    t.put('\n');
  }
  t.put_lit("  help [command]");
  t.spaces(width > k_help_column ? width - k_help_column + 2 : 2);
  t.put_lit("show this help\n");
  const std::size_t need = t.finish();
  detail::send_text(need, buf, sizeof(buf), out);
  return need;
}

// ---------------------------------------------------------------------------
// 分发
// ---------------------------------------------------------------------------
// 内置词（命令表里别用）：help / -h / --help / ?  —— 它们永远走帮助，不执行处理函数。
template <std::size_t N>
error dispatch(const command (&table)[N], const token_list &tokens, reply out) {
  if (tokens.bad_quote) {
    out.put_lit("error: unterminated quote\n");
    return error::bad_quote;
  }
  if (tokens.overflow) {
    out.put_lit("error: command line too long (ECLI_MAX_TOKENS / ECLI_MAX_LINE)\n");
    return error::too_many_tokens;
  }
  if (tokens.count == 0) {   // 空行：列命令，不算失败
    write_command_list(table, out);
    return error::help_requested;
  }

  const std::string_view first = tokens.items[0];
  if (first == "-V" || first == "--version") {
    // 版本号是调用方的事（库不猜你的版本）：返回错误码，打印交给调用方
    return error::version_requested;
  }
  if (first == "help" || first == "-h" || first == "--help" || first == "?") {
    const token_list rest = detail::skip_tokens(tokens, 1);
    if (rest.count == 0) {
      write_command_list(table, out);
      return error::help_requested;
    }
    std::size_t best = npos;
    std::size_t best_len = 0;
    for (std::size_t i = 0; i < N; ++i) {
      const std::size_t len = detail::match_command_name(table[i].name, rest);
      if (len > best_len) {
        best_len = len;
        best = i;
      }
    }
    if (best == npos) {
      out.put_lit("error: unknown command '");
      out.put(rest.items[0]);
      out.put_lit("'\n\n");
      write_command_list(table, out);
      return error::unknown_command;
    }
    // 借命令自己的 -h 通道拿帮助：不执行处理函数，也不用给 thunk 加分支
    token_list help_tokens{};
    help_tokens.items[0] = std::string_view("-h");
    help_tokens.count = 1;
    return table[best].invoke(table[best].name, table[best].help, help_tokens, out);
  }

  std::size_t best = npos;
  std::size_t best_len = 0;
  for (std::size_t i = 0; i < N; ++i) {
    const std::size_t len = detail::match_command_name(table[i].name, tokens);
    if (len > best_len) {   // 最长前缀优先："wifi set x" 命中 "wifi set" 而不是 "wifi"
      best_len = len;
      best = i;
    }
  }
  if (best == npos) {
    out.put_lit("error: unknown command '");
    out.put(first);
    out.put_lit("'\n\n");
    write_command_list(table, out);
    return error::unknown_command;
  }
  const token_list rest = detail::skip_tokens(tokens, best_len);
  return table[best].invoke(table[best].name, table[best].help, rest, out);
}

// 一行文本（串口 / 蓝牙 / 键盘这条路上用；scratch 由调用方管寿命）
template <std::size_t N>
error dispatch(const command (&table)[N], std::string_view line, char *scratch, std::size_t cap,
               reply out) {
  return dispatch(table, tokenize(line, scratch, cap), out);
}

// argv（宿主工具的入口：myapp wifi set -s mynet）
template <std::size_t N>
error dispatch(const command (&table)[N], int argc, const char *const *argv, reply out) {
  return dispatch(table, from_argv(argc, argv), out);
}

}  // namespace ecli

#endif  // ECLI_COMMAND_HPP
