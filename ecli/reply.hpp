/**
 ******************************************************************************
 * @file           : reply.hpp
 * @author         : ruixuezhao
 * @brief          : ecli 的回复通道：命令的输出回给"发起命令的那一路"
 * @attention      : 依赖方向：reply.hpp → eserde/traits.hpp → efmt（只要宏）。
 *                   与 cli.hpp 的分工（C1）：
 *                     * reply.hpp —— 回复【通道】本身：reply 结构 + 各适配器
 *                       （reply_to<写函数> / buffer_reply / stdout_reply /
 *                       string_reply），外加"写进通道"的底层 detail::send_text
 *                       （截断如实标记，不静默丢）
 *                     * cli.hpp    —— 文本【生产】与【送达】：write_*（snprintf
 *                       语义、调用方给缓冲）与 send_*（自带缓冲一行送达，见
 *                       cli.hpp 尾部）；command.hpp 只剩命令表与分发
 *
 *                   两个指针（上下文 + 写函数），零堆、可拷贝、可空（空 = 丢弃输出）。
 *                   回复只存指针 —— sink / 缓冲必须比这次 dispatch 活得久
 *                   （临时对象交给 reply_to_sink 会被删除重载拦掉）。
 ******************************************************************************
 */

#ifndef ECLI_REPLY_HPP
#define ECLI_REPLY_HPP

#include <eserde/traits.hpp>   // EFMT_ENABLE_STDIO / EFMT_ENABLE_DYNAMIC_STRING

#include <cstddef>
#include <string_view>

#if EFMT_ENABLE_STDIO
#include <cstdio>
#endif

#if EFMT_ENABLE_DYNAMIC_STRING
#include <string>
#endif

// 帮助 / 报错文本的栈缓冲。装不下会如实追加 "...(truncated)"，不静默截断。
// send_* 就吃这个缓冲；要更大的（或更小的）在 include 之前覆写它：
//   #define ECLI_REPLY_BUFFER 768
#ifndef ECLI_REPLY_BUFFER
#define ECLI_REPLY_BUFFER 384
#endif

namespace ecli {

// ---------------------------------------------------------------------------
// 回复通道：命令的输出回给"发起命令的那一路"
// ---------------------------------------------------------------------------
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

namespace detail {

// 把 (need, buf, cap) 写进回复通道：装不下就如实追加 "...(truncated)"——
// 标记要【先留出位置】，否则正文把缓冲填满，标记自己反而塞不进去（第一版就是这么错的）。
// 返回 snprintf 语义的"完整长度"：返回值 ≤ cap 即完整送达，超过说明截断了
// （差多少 = 返回值 - cap，可以自己加大缓冲重发）。
inline std::size_t send_text(std::size_t need, const char *buf, std::size_t cap, reply out) {
  if (cap == 0) return need;
  if (need < cap) {
    out.put(std::string_view(buf, need));
    return need;
  }
  constexpr std::string_view kMark = "...(truncated)\n";
  const std::size_t keep = cap > kMark.size() + 1 ? cap - kMark.size() - 1 : 0;
  out.put(std::string_view(buf, keep));
  out.put(kMark);
  return need;
}

}  // namespace detail

}  // namespace ecli

#endif  // ECLI_REPLY_HPP