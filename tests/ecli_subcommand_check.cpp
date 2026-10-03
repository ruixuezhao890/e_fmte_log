// tests/ecli_subcommand_check.cpp
// 嵌套 struct 子命令（clap 风格，用户 m00088 语法）
//   1. 解析层：注册表能从声明原文里认出嵌套命令（名字 / 帮助属性 / 字段子表）
//   2. schema 视图：sub 查询与普通字段通道互不串道
// 第一步只测 eserde 原料层的解析；cli 集成（variant / 命令表 / matchit 消费）后续步骤加入。
#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string.h>
#include <eserde/traits.hpp>
#include <ecli/cli.hpp>
#include <matchit/matchit.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <variant>

using e_fmt::detail::derive_detail::parse_derived_declaration;
using e_fmt::detail::parse_derived_schema;
using e_fmt::Debug;
using ::ecli::Subcommand;
using ::ecli::Parser;
using ::ecli::Cli;

// —— 注册表：三个子命令（用户确认语法）——
constexpr const char *kRegistry =
    "struct Commands { "
    "[[efmt::arg(help = \"add a file\")]]struct Add  { "
    "[[efmt::arg(short, long, help = \"file path\")]] etl::string<64> file; }; "
    "[[efmt::arg(help = \"delete a file\")]]struct Del  { "
    "[[efmt::arg(short, long, help = \"file path\")]] etl::string<64> file; }; "
    "[[efmt::arg(help = \"find a file\")]]struct Find { "
    "[[efmt::arg(short, long, help = \"file path\")]] etl::string<64> file; }; "
    "}";

// —— 解析层（derive_detail）断言 ——
static_assert(parse_derived_declaration<16>(kRegistry).valid,
              "注册表解析必须成功（嵌套 struct 不能丢）");
static_assert(parse_derived_declaration<16>(kRegistry).sub_count == 3,
              "三个子命令都要登记");
static_assert(parse_derived_declaration<16>(kRegistry).count == 0,
              "子命令不算普通字段");
static_assert(parse_derived_declaration<16>(kRegistry).subs[0].name ==
                  std::string_view("Add"),
              "第一个命令名 Add");
static_assert(parse_derived_declaration<16>(kRegistry).subs[2].name ==
                  std::string_view("Find"),
              "第三个命令名 Find");
static_assert(parse_derived_declaration<16>(kRegistry).subs[1].attrs_head.find(
                  "help = \"delete a file\"") != std::string_view::npos,
              "Del 的帮助文本来自 struct 前的 [[efmt::arg(...)]]");
static_assert(parse_derived_declaration<16>(kRegistry).subs[0].body.find(
                  "file") != std::string_view::npos,
              "Add 的字段子表要原样保留");

// —— 子命令字段子表：body 可以当普通结构体体解析 ——
static_assert(e_fmt::detail::derive_detail::parse_struct_body<16>(
                  parse_derived_declaration<16>(kRegistry).subs[0].body)
                  .count == 1,
              "Add 有 1 个字段");
static_assert(e_fmt::detail::derive_detail::parse_struct_body<16>(
                  parse_derived_declaration<16>(kRegistry).subs[0].body)
                  .items[0] == std::string_view("file"),
              "Add 的字段名 file");

// —— schema 视图（e_fmt::detail）断言 ——
static_assert(parse_derived_schema<16>(kRegistry).valid, "schema valid");
static_assert(parse_derived_schema<16>(kRegistry).sub_count == 3,
              "schema 也要看到 3 个子命令");
static_assert(parse_derived_schema<16>(kRegistry).sub(0).name ==
                  std::string_view("Add"),
              "schema.sub(0) 是 Add");
static_assert(parse_derived_schema<16>(kRegistry).sub(2).attrs_head.find(
                  "find a file") != std::string_view::npos,
              "schema.sub(2) 带帮助属性");

// —— 回归：普通结构体不受影响 ——
constexpr const char *kPlain =
    "struct args { "
    "[[efmt::arg(short = \"v\", long = \"verbose\", help = \"show details\")]] "
    "bool verbose = false; "
    "}";
static_assert(parse_derived_schema<16>(kPlain).valid, "普通结构体仍要 valid");
static_assert(parse_derived_schema<16>(kPlain).count == 1,
              "普通字段数照常");
static_assert(parse_derived_schema<16>(kPlain).sub_count == 0,
              "普通结构体没有子命令");
static_assert(parse_derived_schema<16>(kPlain).field(0).name ==
                  std::string_view("verbose"),
              "普通字段名照常");

// —— 混合：嵌套 struct 与普通字段可以在同一个体里并存 ——
constexpr const char *kMixed =
    "struct T { int base; [[efmt::arg(help = \"x\")]]struct Inner { int v; }; }";
static_assert(parse_derived_schema<16>(kMixed).valid, "混合体 valid");
static_assert(parse_derived_schema<16>(kMixed).count == 1,
              "混合体普通字段 1 个（base）");
static_assert(parse_derived_schema<16>(kMixed).sub_count == 1,
              "混合体子命令 1 个（Inner）");
static_assert(parse_derived_schema<16>(kMixed).sub(0).name ==
                  std::string_view("Inner"),
              "子命令名 Inner");
static_assert(parse_derived_schema<16>(kMixed).field(0).name ==
                  std::string_view("base"),
              "字段 base 不被嵌套声明串道");

// ===========================================================================
// cli 集成（步骤②）：注册表真类型 / Parser 能力 / variant 槽 / 分发 / matchit
// ===========================================================================
// 注册表：三个嵌套 struct 就是三个子命令（用户 m00088 确认语法）
E_FMT_DERIVE(struct Commands {
  [[efmt::arg(help = "add a file")]] struct Add {
    [[efmt::arg(short, long, required, help = "file path")]] etl::string<64> file;
  };
  [[efmt::arg(help = "delete a file")]] struct Del {
    [[efmt::arg(short, long, help = "file path")]] etl::string<64> file;
  };
  [[efmt::arg(help = "find a file")]] struct Find {
    [[efmt::arg(short, long, help = "file path")]] etl::string<64> file;
  };
}, Subcommand, Debug);

// 解析块：variant 首备选 = 注册表类型（「未选」哨兵，兼作命令名/数量校验的来源）。
// 注意：E_FMT_DERIVE 是第一参数吃「声明文本」的宏，预处理切参数只认 () 不认 <>，
// std::variant<A, B, C> 的逗号会切开宏参数——所以类型名里带逗号一律先起别名（同 pair 的规矩）。
using CmdArgs = std::variant<Commands, Commands::Add, Commands::Del, Commands::Find>;
E_FMT_DERIVE(struct args {
  [[efmt::arg(short = "v", long = "verbose", help = "show details")]] bool verbose = false;
  [[efmt::arg(command)]] CmdArgs cmd;
}, Parser, Debug);

static_assert(::eserde::has_cap_v<Commands, Subcommand> && ::eserde::has_cap_v<Commands, Debug>,
              "注册表带上 Subcommand + Debug 能力");
static_assert(::eserde::detail::schema_holder<Commands>::value.sub_count == 3,
              "注册表 schema 看到 3 个子命令");
static_assert(::eserde::has_cap_v<args, Parser> && !::eserde::has_cap_v<args, Cli>,
              "解析块是 Parser 能力（不是 Cli；两者都要能解析）");

int main() {
  std::puts("ecli subcommand schema: all static asserts passed");

  // 分发：add -f x.txt → 1 号备选（Commands::Add），file 收值，其它字段不动
  {
    args a{};
    const ecli::error e = ecli::parse("add -f x.txt", a);
    if (e != ecli::error::ok) {
      std::printf("add: %s\n", ecli::error_name(e));
      return 1;
    }
    if (a.cmd.index() != 1 || a.verbose) {
      std::printf("add: index=%zu verbose=%d\n",
                  static_cast<std::size_t>(a.cmd.index()), static_cast<int>(a.verbose));
      return 1;
    }
    const auto &add = std::get<Commands::Add>(a.cmd);
    if (std::string_view(add.file.data(), add.file.size()) != "x.txt") {
      std::printf("add.file=%s\n", add.file.c_str());
      return 1;
    }
  }

  // 分发：无参数子命令（del）与取值子命令（find）
  {
    args a{};
    if (ecli::parse("del", a) != ecli::error::ok) return 1;
    if (a.cmd.index() != 2) return 1;
  }
  {
    args a{};
    if (ecli::parse("find -f y", a) != ecli::error::ok) return 1;
    if (a.cmd.index() != 3) return 1;
    if (std::string_view(std::get<Commands::Find>(a.cmd).file.data(),
                         std::get<Commands::Find>(a.cmd).file.size()) != "y")
      return 1;
  }

  // 未知命令名 → unknown_command（不是 too_many_args）
  {
    args a{};
    if (ecli::parse("nope", a) != ecli::error::unknown_command) return 1;
    if (a.cmd.index() != 0) return 1;   // 失败不动原对象（副本语义）
  }

  // 选项可以出现在子命令名之前：--verbose add -f x
  {
    args a{};
    if (ecli::parse("--verbose add -f x", a) != ecli::error::ok) return 1;
    if (!a.verbose || a.cmd.index() != 1) return 1;
    if (std::string_view(std::get<Commands::Add>(a.cmd).file.data(),
                         std::get<Commands::Add>(a.cmd).file.size()) != "x")
      return 1;
  }

  // 帮助文本列子命令（名字 + 帮助）
  {
    const std::string help = ecli::help_string<args>("prog");
    if (help.find("add") == std::string::npos ||
        help.find("delete a file") == std::string::npos ||
        help.find("find a file") == std::string::npos) {
      std::puts(help.c_str());
      return 1;
    }
  }

  // 子命令缺必填：报错必须指向子命令自己的字段（--file），而不是顶层的 --verbose
  {
    args a{};
    ecli::error_info info{};
    char scratch[128];
    const ecli::error e = ecli::parse("add", a, scratch, sizeof(scratch), &info);
    if (e != ecli::error::missing_required) {
      std::printf("expected missing_required for bare 'add', got %d\n", (int)e);
      return 1;
    }
    char ebuf[192];
    const std::size_t need = ecli::write_error<args>("prog", e, info, ebuf, sizeof(ebuf));
    const std::string text(ebuf, need < sizeof(ebuf) ? need : sizeof(ebuf) - 1);
    if (text.find("--file") == std::string::npos || text.find("--verbose") != std::string::npos) {
      std::printf("bad required-error text: %s\n", text.c_str());
      return 1;
    }
    if (text.find("usage: prog [options]") == std::string::npos) {
      std::printf("bad usage tail: %s\n", text.c_str());
      return 1;
    }
  }

  // matchit 消费：as<备选>(_) 按当前选中分支
  {
    args a{};
    if (ecli::parse("add -f z", a) != ecli::error::ok) return 1;
    const auto picked = matchit::match(a.cmd)(
        matchit::pattern | matchit::as<Commands::Add>(matchit::_) = [] { return 1; },
        matchit::pattern | matchit::as<Commands::Del>(matchit::_) = [] { return 2; },
        matchit::pattern | matchit::as<Commands::Find>(matchit::_) = [] { return 3; },
        matchit::pattern | matchit::_ = [] { return 0; });
    if (picked != 1) {
      std::printf("matchit picked %d\n", picked);
      return 1;
    }
    args u{};
    const auto unpicked = matchit::match(u.cmd)(
        matchit::pattern | matchit::as<Commands::Add>(matchit::_) = [] { return 1; },
        matchit::pattern | matchit::as<Commands::Del>(matchit::_) = [] { return 2; },
        matchit::pattern | matchit::as<Commands::Find>(matchit::_) = [] { return 3; },
        matchit::pattern | matchit::_ = [] { return 0; });
    if (unpicked != 0) return 1;   // 未选 → 兜底分支
  }

  std::puts("ecli subcommand cli: integration passed");
  return 0;
}
