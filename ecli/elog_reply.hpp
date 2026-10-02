/**
 ******************************************************************************
 * @file           : elog_reply.hpp
 * @author         : ruixuezhao
 * @brief          : ecli × elog —— 命令回复接上 elog 的输出后端（sink）
 * @attention      : 可选层：**不 include 就是零开销**（ecli 本体不认识 elog）。
 *
 *                   本仓库的分工规矩：
 *                     * 文本【格式化】一律 efmt  —— usage/help/报错本来就是 efmt 的
 *                       snprintf 语义（写进调用方缓冲，格式化与输出是分开的两件事）
 *                     * 文本【输出】  一律 elog  —— 这里就是"回复"那一路的入口
 *
 *                   回复为什么用 sink，而不是 logger？
 *                     logger::log 是【日志语义】：固定加 "[级别] [文件:行 函数] " 前缀、
 *                     自己补换行、整行超过 ELOG_MAX_RECORD_SIZE 就【整行丢弃】。
 *                     命令回复要的是【原样字节】：usage/help 是多行整块文本、它的上限是
 *                     调用方的缓冲区而不是"一行"，而且要回给"发起命令的那一路"（串口问的
 *                     回串口）。sink 恰好就是 elog 的字节出口（(data,size) 写函数），
 *                     语义完全对得上：
 *                       * 日志行   → ELOG_INFO / ELOG_ERROR（有级别、有来源）
 *                       * 命令回复 → ecli::reply_to_logger(lg) / reply_to_sink(sink)
 *                         （原样、不经日志格式）
 *
 *                   用法（首选：复用日志已经绑好的那条通道）：
 *                     ecli::dispatch(kCommands, line.line(), scratch, sizeof(scratch),
 *                                    ecli::reply_to_logger(*lg));         // 回复原样走 lg 的 sink
 *
 *                    手上有裸 sink（没建 logger / 想另指一路）时：
 *                     e_log::sink uart = e_log::make_sink(&uart_write);   // 或 elog 的 stdout_sink()
 *                     ecli::dispatch(kCommands, line.line(), scratch, sizeof(scratch),
 *                                    ecli::reply_to_sink(uart));          // 回复原样走 elog 的 sink
 *
 *                    只想"回 stdout"就用 ecli::elog_stdout_reply()（内部持有一个静态 sink，
 *                    不会踩临时对象悬垂）。
 *
 *                   【一次绑定，多处使用】——本仓库对"输出通道"的总规矩：
 *                     通道只在 elog 那一处定义/绑定一次（sink = 你的 (data, size) 写函数），
 *                     日志、命令回复、别的消费者全都复用【同一个已绑定的对象】，不再各绑一次。
 *                     写法就是把 logger 拿出来直接问它要通道：
 *                       e_log::logger *lg = e_log::create_logger("app", e_log::make_sink(&uart_write));
 *                       ELOG_LOGGER_INFO(*lg, "boot {}", 1);                    // 日志
 *                       dispatch(kCommands, line, scratch, sizeof(scratch),
 *                                ecli::reply_to_logger(*lg));                   // 回复：同一条通道
 *                     通道是 multi_sink 时，这里连"扇出到串口+蓝牙+屏幕"也一并白拿。
 *
 *                   两条硬约束：
 *                     1. reply 只存指针 —— sink 必须比这次 dispatch 活得久
 *                        （传临时 sink 会编译报错，见下面的删除重载）
 *                     2. include 这个头 = 同时需要 elog 与它依赖的 ETL（etl::array）
 ******************************************************************************
 */

#ifndef ECLI_ELOG_REPLY_HPP
#define ECLI_ELOG_REPLY_HPP

#include <ecli/command.hpp>

#include <elog/elog.hpp>

#include <cstddef>

namespace ecli {

namespace detail {

inline void elog_sink_write(void *ctx, const char *data, std::size_t size) {
  // sink::write 是 const：这里只是把 sink 当"出口"用，sink 本身不会被改
  (void)static_cast<const e_log::sink *>(ctx)->write(data, size);
}

}  // namespace detail

// 命令回复原样写进 elog 的 sink：不加级别前缀、不补换行、不按行截断。
// 返回值是"回复通道"本身 —— 和 reply_to<uart_write>() 是同一个位置的东西，
// 区别只在出口是 elog 的 sink 还是裸写函数。
inline reply reply_to_sink(const e_log::sink &out) {
  return reply{const_cast<e_log::sink *>(&out), &detail::elog_sink_write};
}

// 传临时 sink 会在 dispatch 期间悬垂（reply 只存指针）—— 编译期就拦掉，别等运行时
reply reply_to_sink(e_log::sink &&) = delete;

// 「一次绑定，多处使用」的主入口：日志已经绑在哪条通道上，回复就走哪条 —— 不再绑第二次。
//   e_log::logger *lg = e_log::create_logger("app", e_log::make_sink(&uart_write));
//   dispatch(kCommands, line, scratch, sizeof(scratch), reply_to_logger(*lg));
// 连接口都不用另写：通道若在 logger 上写成 multi_sink，回复自动跟着扇出到多路。
// 生命周期天然成立：sink 由 logger 持有（注册表的静态存储），比任何一次 dispatch 都活得久。
inline reply reply_to_logger(const e_log::logger &target) {
  return reply_to_sink(target.output_sink());
}

// logger 指针不在手边时的便利版：取当前默认 logger 的那条通道
// （create_logger 建的第一个就是默认；一次都没建过 = 丢弃输出，与 reply{} 同语义，不崩）。
inline reply reply_to_default_logger() {
  const e_log::logger *target = e_log::default_logger();
  return target != nullptr ? reply_to_sink(target->output_sink()) : reply{};
}

// 最常用的一种：回复直接写 stdout，但出口仍然是 elog 的 stdout sink
inline reply elog_stdout_reply() {
  static const e_log::sink kStdout = e_log::stdout_sink();
  return reply_to_sink(kStdout);
}

}  // namespace ecli

#endif  // ECLI_ELOG_REPLY_HPP
