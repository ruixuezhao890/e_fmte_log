/**
 ******************************************************************************
 * @file           : command.hpp
 * @author         : ruixuezhao
 * @brief          : ecli 的命令表：多命令 / 子命令分发 + 输出回给"发起命令的那一路"
 * @attention      : 依赖方向：command.hpp → cli.hpp → { reply.hpp, eserde/traits.hpp } → efmt。
 *
 *                   设计只用三句话就能说完：
 *                     1. 命令表是一个【零堆静态数组】：{名字, 帮助, 处理函数 thunk}
 *                     2. 命令名允许带空格 —— "wifi set" 就是子命令；从 v1.11 起还允许
 *                        【模式段】："wifi set :ssid"（一个 token → 捕获成 ssid）、
 *                        "log *rest"（余下 token 全收）。匹配规则是
 *                        【段特异性优先，再比 token 数】，所以不需要树、不需要 new
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
 *                       {"status",   "show link status", ecli::command_of<status_args, status_run>(),
 *                                                        ecli::help_of<status_args>()},
 *                       {"wifi set", "set ssid",         ecli::command_of<wifi_args, set_run>(),
 *                                                        ecli::help_of<wifi_args>()},
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

// 命令名里的模式段（:name / *name）用 matchit 的 extractor 协议匹配。
// 关掉它就退回"只认字面量段"的老行为（省掉 matchit 的编译期与体积开销）。
#ifndef ECLI_ENABLE_PATTERN_COMMANDS
#define ECLI_ENABLE_PATTERN_COMMANDS 1
#endif

// 一次命令最多记几个捕获（:name / *name 各算一个）
#ifndef ECLI_MAX_CAPTURES
#define ECLI_MAX_CAPTURES 8
#endif

#if ECLI_ENABLE_PATTERN_COMMANDS
#include <matchit/matchit.h>   // 第三方便携库（本地改造版，见 matchit/PATCHES.md）
#include <optional>
#endif

namespace ecli {

// ---------------------------------------------------------------------------
// 命令名模式里捕获到的东西
// ---------------------------------------------------------------------------
//   "wifi set :ssid"  → ssid 收一个 token
//   "log *rest"       → rest 收余下的全部 token（可以是 0 个；只能出现在模式末尾）
// 捕获值会【按名字注入到参数结构体的同名字段】（扫 schema 找字段）：
//   :name → 标量/字符串/枚举字段直接赋该 token
//   *name → 容器字段逐个 push（std::vector / etl::vector）；标量字段不注入
// 没有同名字段也不报错：值仍然能在处理函数的 params 形参里拿到。
struct params {
  static constexpr std::size_t capacity = ECLI_MAX_CAPTURES;

  std::string_view names[ECLI_MAX_CAPTURES]{};
  std::string_view values[ECLI_MAX_CAPTURES]{};   // :name = 该 token；*name = 收下的第一个 token（可能空）
  std::size_t rest_from[ECLI_MAX_CAPTURES]{};     // *name：在本次命令 token 表里的起点
  const std::string_view *tokens = nullptr;       // *name 的取值从这里往后读
  std::size_t token_count = 0;
  std::size_t count = 0;                          // 捕获项数
  std::size_t spec = 0;                           // 匹配时吃掉的段数（分发排序用）
  bool overflow = false;                          // 捕获项超过 ECLI_MAX_CAPTURES

  std::size_t size() const { return count; }
  std::string_view name_at(std::size_t i) const { return i < count ? names[i] : std::string_view{}; }
  std::string_view value_at(std::size_t i) const { return i < count ? values[i] : std::string_view{}; }
  bool is_rest(std::size_t i) const { return i < count && rest_from[i] != npos; }
  std::size_t rest_count(std::size_t i) const {
    if (!is_rest(i)) return 0;
    return token_count > rest_from[i] ? token_count - rest_from[i] : 0;
  }
  std::string_view rest_at(std::size_t i, std::size_t k) const {
    if (!is_rest(i) || k >= rest_count(i) || tokens == nullptr) return std::string_view{};
    return tokens[rest_from[i] + k];
  }
  bool has(std::string_view name) const {
    for (std::size_t i = 0; i < count; ++i) {
      if (names[i] == name) return true;
    }
    return false;
  }
  std::string_view get(std::string_view name, std::string_view fallback = {}) const {
    for (std::size_t i = 0; i < count; ++i) {
      if (names[i] == name) return values[i];
    }
    return fallback;
  }
};

// ---------------------------------------------------------------------------
// 命令表
// ---------------------------------------------------------------------------
// 处理函数签名有两种（都行，按你写的那个自动选）：
//   void(const Args&, reply)                  不需要看捕获
//   void(const Args&, const params&, reply)   想看 :name / *rest 的原始值
// reply 用来回话；不用就留空名字，避免 -Wunused-parameter。
// 参数已解析完（选项 + 位置参数），命令名模式吃掉的 token 不在里面。
// 不要回话的处理函数把第二个参数留空名字即可（避免 -Wunused-parameter）：
//   void status_run(const status_args& a, ecli::reply) { ... }
using invoke_fn = error (*)(std::string_view name, std::string_view about, const params &p,
                            const token_list &tokens, reply out);

struct command {
  // 命令名：空格分段的模式 —— 字面量 "wifi" / 参数段 ":ssid" / 余下段 "*rest"（只能收尾）
  std::string_view name;
  std::string_view help;   // 命令表与 help <命令> 里显示的说明
  invoke_fn invoke;        // 由 command_of<Args, Fn>() 生成的自包含 thunk
  // 帮助专用 thunk（C2）：help <命令> 走这条，不解析不执行处理函数。
  // 由 help_of<Args>() 生成；四字段初始化缺席时聚合初始化补 nullptr，
  // dispatch 退化为借 -h 通道（旧调用点行为不变）。
  invoke_fn help_of;
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

#if ECLI_ENABLE_PATTERN_COMMANDS
// 段模式的"提取器"：命中就把 token 交出去（matchit 的 app/extractor 协议）
//   ":name" / "*name" → 任意 token 都算命中（值被捕获）
//   字面量            → 必须与 token 相等
struct segment_extractor {
  std::string_view pattern{};

  std::optional<std::string_view> operator()(std::string_view token) const {
    if (!pattern.empty() && (pattern[0] == ':' || pattern[0] == '*')) return token;
    if (token == pattern) return token;
    return std::nullopt;
  }
};

// 一个 token ↔ 一个段模式：判定交给 matchit（app/extractor + some + 通配）。
// 捕获值不用 matchit 的 Id 绑定 —— Id 存的是【指针】，绑定的是 match 表达式里的临时值，
// 出了那个表达式就悬垂（第一版就是这么拿到垃圾的）。token 本来就在手上，命中即取值。
inline bool match_segment(std::string_view pattern, std::string_view token,
                          std::string_view &captured) {
  const bool hit = ::matchit::match(token)(
      ::matchit::pattern |
              ::matchit::app(segment_extractor{pattern}, ::matchit::some(::matchit::_)) = true,
      ::matchit::pattern | ::matchit::_ = false);
  if (hit) captured = token;
  return hit;
}
#else
// 裁剪版（ECLI_ENABLE_PATTERN_COMMANDS=0）：:name / *name 当字面量处理
inline bool match_segment(std::string_view pattern, std::string_view token,
                          std::string_view &captured) {
  if (token != pattern) return false;
  captured = token;
  return true;
}
#endif

// 命令名（模式串）与 token 前缀匹配：
//   返回值 = 吃掉的 token 数（0 = 不匹配）；p.spec = 吃掉的【非 * 段】数（分发排序用）
//   "log *rest" 会把余下 token 全吃掉；所以 * 段不参与"特异性"计数 —— 否则一个
//   catch-all 命令会盖掉更具体的命令（"sensor read" vs "sensor *rest"）。
inline std::size_t match_command_pattern(std::string_view name, const token_list &tokens,
                                         params &p) {
  std::size_t k = 0;   // token 下标
  std::size_t i = 0;   // name 里的位置
  std::size_t spec = 0;
  while (i < name.size()) {
    while (i < name.size() && name[i] == ' ') ++i;
    if (i >= name.size()) break;
    const std::size_t begin = i;
    while (i < name.size() && name[i] != ' ') ++i;
    const std::string_view seg = name.substr(begin, i - begin);
#if ECLI_ENABLE_PATTERN_COMMANDS
    const bool is_rest = !seg.empty() && seg[0] == '*';
    const bool is_param = !seg.empty() && seg[0] == ':';
#else
    const bool is_rest = false;   // 裁剪版：整段当字面量
    const bool is_param = false;
#endif

    if (is_rest) {   // 余下全收（可为空）——只能出现在末尾
      if (p.count < params::capacity) {
        p.names[p.count] = seg.substr(1);
        p.values[p.count] = k < tokens.count ? tokens.items[k] : std::string_view{};
        p.rest_from[p.count] = k;
        ++p.count;
      } else {
        p.overflow = true;
      }
      p.spec = spec;
      return tokens.count;
    }

    if (k >= tokens.count) return 0;   // 段比 token 多 → 不匹配
    std::string_view captured{};
    if (!match_segment(seg, tokens.items[k], captured)) return 0;
    if (is_param) {   // 参数段：记下来
      if (p.count < params::capacity) {
        p.names[p.count] = seg.substr(1);
        p.values[p.count] = captured;
        p.rest_from[p.count] = npos;
        ++p.count;
      } else {
        p.overflow = true;
      }
    }
    ++k;
    ++spec;
  }
  p.spec = spec;
  return k;
}

// help <命令…> 用：用户给的那几个 token 是否是这个命令模式的前缀（参数段算任意 token）
// 返回匹配到的段数（0 = 不是）；用于"help wifi set" 能命中 "wifi set :ssid"
inline std::size_t match_command_prefix(std::string_view name, const token_list &tokens) {
  std::size_t k = 0;
  std::size_t i = 0;
  std::size_t spec = 0;
  while (i < name.size() && k < tokens.count) {
    while (i < name.size() && name[i] == ' ') ++i;
    if (i >= name.size()) break;
    const std::size_t begin = i;
    while (i < name.size() && name[i] != ' ') ++i;
    const std::string_view seg = name.substr(begin, i - begin);
    if (seg.empty()) break;
#if ECLI_ENABLE_PATTERN_COMMANDS
    const bool is_rest = seg[0] == '*';
    const bool is_param = seg[0] == ':';
#else
    const bool is_rest = false;
    const bool is_param = false;
#endif
    if (is_rest) return spec;                             // 通配尾段：前缀到此为止
    if (is_param) {
      ++k;                                                // 参数段：任意一个 token
    } else if (tokens.items[k] == seg) {
      ++k;
    } else {
      return 0;
    }
    ++spec;
  }
  return k == tokens.count ? spec : 0;   // 用户给的 token 必须全用上
}

// 命令名模式捕获到的值 → 按名字注入参数结构体的同名字段
//   找不到同名字段：跳过（值仍在 params 里，处理函数能看见）
//   *name → 只注入容器字段（逐个 push）；标量字段不注入（免得"最后一个赢"这种意外）
// 按字段名找下标：查的是编译期那张规格表（rodata），【不】用 eserde::find_field ——
// 后者会把 efmt 的声明原文解析函数拖进固件（实测 ~1.5 KB / 类型）。
template <typename Args>
inline std::size_t find_field_index(std::string_view name) {
  const option_view *opts = options_holder<Args>::value.data();
  const std::size_t n = options_holder<Args>::count;
  for (std::size_t i = 0; i < n; ++i) {
    if (opts[i].field == name) return i;
  }
  return npos;
}

template <typename Args>
error inject_captures(Args &obj, const params &p, error_info &info) {
#if ECLI_ENABLE_PATTERN_COMMANDS
  for (std::size_t i = 0; i < p.count; ++i) {
    const std::size_t idx = find_field_index<Args>(p.name_at(i));
    if (idx == npos) continue;
    if (p.is_rest(i)) {
      if (!option<Args>(idx).repeatable) continue;
      for (std::size_t k = 0; k < p.rest_count(i); ++k) {
        const error e = assign_capture_at(obj, idx, p.rest_at(i, k));
        if (e != error::ok) {
          info.option_index = idx;
          info.token = p.rest_at(i, k);
          return e;
        }
      }
      continue;
    }
    const error e = assign_capture_at(obj, idx, p.value_at(i));
    if (e != error::ok) {
      info.option_index = idx;
      info.token = p.value_at(i);
      return e;
    }
  }
#else
  (void)obj;   // ECLI_ENABLE_PATTERN_COMMANDS=0：没有模式段可捕获，整段编译掉
  (void)p;
  (void)info;
#endif
  return error::ok;
}

// 每个命令的自包含 thunk：声明参数类型 → 解析参数 → 注入捕获 → 回话 → 交给处理函数
// 主体只有一份（C3）：WithParams=false → Fn(a, out)；=true → Fn(a, p, out)
// （想看模式捕获的原始值走后一种签名）。下面两个薄包装只按 Fn 签名选开关。
template <typename Args, bool WithParams, auto Fn>
error command_thunk_run(std::string_view name, std::string_view about, const params &p,
                        const token_list &tokens, reply out) {
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
  const error ie = inject_captures(a, p, info);   // 捕获后写：同名时以模式捕获为准
  if (ie != error::ok) {
    send_error<Args>(name, ie, info, out);
    return ie;
  }
  if constexpr (WithParams) {
    Fn(a, p, out);
  } else {
    Fn(a, out);
  }
  return error::ok;
}

// 薄包装 A：处理函数签名 void(const Args&, reply)
template <typename Args, void (*Fn)(const Args &, reply)>
error command_thunk(std::string_view name, std::string_view about, const params &p,
                    const token_list &tokens, reply out) {
  return command_thunk_run<Args, false, Fn>(name, about, p, tokens, out);
}

// 薄包装 B：处理函数签名 void(const Args&, const params&, reply)（想看原始捕获值）
template <typename Args, void (*Fn)(const Args &, const params &, reply)>
error command_thunk_with_params(std::string_view name, std::string_view about, const params &p,
                                const token_list &tokens, reply out) {
  return command_thunk_run<Args, true, Fn>(name, about, p, tokens, out);
}

// 帮助专用 thunk（C2）：不 parse、不注入捕获、不执行处理函数 —— 直接给帮助。
// dispatch 的 help <命令…> 优先走这条，不再伪造 -h 借 parse 的 help_requested 通道。
// 旧调用点没有 help_of（聚合初始化缺省为 nullptr）时 dispatch 退化回借 -h 通道。
template <typename Args>
error command_help_thunk(std::string_view name, std::string_view about, const params &,
                         const token_list &, reply out) {
  send_help<Args>(name, about, out);
  return error::help_requested;
}

}  // namespace detail

// 把 (参数类型, 处理函数) 变成命令表里的一项（编译期，零运行时代价）。
// 两个重载按处理函数签名自动选：void(const Args&, reply) 或 void(const Args&, const params&, reply)
template <typename Args, void (*Fn)(const Args &, reply)>
inline constexpr invoke_fn command_of() {
  return &detail::command_thunk<Args, Fn>;
}

template <typename Args, void (*Fn)(const Args &, const params &, reply)>
inline constexpr invoke_fn command_of() {
  return &detail::command_thunk_with_params<Args, Fn>;
}

// 帮助专用 thunk（C2）：命令表第四字段 help_of<Args>()。
// 不解析参数、不注入捕获、不执行处理函数 —— help <命令> 拿到的就是纯帮助文本。
template <typename Args>
inline constexpr invoke_fn help_of() {
  return &detail::command_help_thunk<Args>;
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
// 内置词（命令表里别用）：help / -h / --help / ? 与 -V / --version。
// 匹配规则：段特异性优先（* 段不算），再比吃掉的 token 数 —— 所以
// "sensor read" 会赢过 "sensor *rest"，"wifi set :ssid" 会赢过 "wifi"。
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
    // help <命令…>：用户给的 token 是某个命令模式的前缀就命中（"help wifi set" 也能
    // 命中 "wifi set :ssid"），命中最具体的那个
    std::size_t best = npos;
    std::size_t best_spec = 0;
    for (std::size_t i = 0; i < N; ++i) {
      const std::size_t spec = detail::match_command_prefix(table[i].name, rest);
      if (spec > best_spec) {
        best_spec = spec;
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
    // 帮助走命令自带的 help_of 专用 thunk（C2）：不解析参数、不注入捕获、
    // 不执行处理函数。旧调用点四字段缺席（help_of 聚合初始化为 nullptr）
    // 时退化为借 -h 通道（行为不变）。
    params none{};   // 帮助路径不做捕获，也不注入
    if (table[best].help_of != nullptr) {
      return table[best].help_of(table[best].name, table[best].help, none, token_list{}, out);
    }
    // 借命令自己的 -h 通道拿帮助：不执行处理函数，也不用给 thunk 加分支
    token_list help_tokens{};
    help_tokens.items[0] = std::string_view("-h");
    help_tokens.count = 1;
    return table[best].invoke(table[best].name, table[best].help, none, help_tokens, out);
  }

  std::size_t best = npos;
  std::size_t best_spec = 0;
  std::size_t best_used = 0;
  params best_params{};
  best_params.tokens = tokens.items;
  best_params.token_count = tokens.count;
  for (std::size_t i = 0; i < N; ++i) {
    params p{};
    p.tokens = tokens.items;
    p.token_count = tokens.count;
    const std::size_t used = detail::match_command_pattern(table[i].name, tokens, p);
    if (used == 0) continue;
    if (p.spec > best_spec || (p.spec == best_spec && used > best_used)) {
      best = i;
      best_spec = p.spec;
      best_used = used;
      best_params = p;
    }
  }
  if (best == npos) {
    out.put_lit("error: unknown command '");
    out.put(first);
    out.put_lit("'\n\n");
    write_command_list(table, out);
    return error::unknown_command;
  }
  const token_list rest = detail::skip_tokens(tokens, best_used);
  return table[best].invoke(table[best].name, table[best].help, best_params, rest, out);
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
