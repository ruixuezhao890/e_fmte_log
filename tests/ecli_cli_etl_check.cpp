/**
 ******************************************************************************
 * @file           : ecli_cli_etl_check.cpp
 * @brief          : ecli::cli 与 ETL 类型（etl::string / etl::vector / etl::string_view）
 * @attention      : 取值助手只看成员函数（data/size/max_size/clear/push_back/assign），
 *                   所以 std 与 ETL 走的是同一套代码 —— 这里把这条钉住。
 *                   重点：etl::string 的 assign 满了是【静默截断】，CLI 必须自己挡住。
 *                   需要 ETL 头文件，run_check.ps1 里在没有 ETL 时跳过。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string.h>
#include <middleware/etl/string_view.h>
#include <middleware/etl/vector.h>
#include <eserde/traits.hpp>
#include <ecli/cli.hpp>

#include <cstdio>
#include <string>

using namespace e_fmt;
using namespace ecli;

// 类型名里带逗号的模板先 typedef（逗号会把 E_FMT_DERIVE 的第一个参数切断）
using int2 = etl::vector<int, 2>;
using str4 = etl::vector<etl::string<4>, 2>;

E_FMT_DERIVE(struct etl_args {
  [[efmt::arg(short = "n", long = "name")]]  etl::string<8> name;
  [[efmt::arg(short = "i", long = "id")]]    int2 ids;
  [[efmt::arg(long = "tag")]]                str4 tags;
  [[efmt::arg(long = "view")]]               etl::string_view view;
  [[efmt::arg(pos = "1")]]                   etl::string<4> first;
}, Cli);

static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, bool ok, const std::string &detail = {}) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : " -> ", detail.c_str());
}

static void check_error(const char *what, error e, error wanted) {
  check(what, e == wanted,
        std::string("actual=") + error_name(e) + " wanted=" + error_name(wanted));
}

static void check_text(const char *what, std::string_view actual, std::string_view wanted) {
  check(what, actual == wanted,
        "actual=[" + std::string(actual) + "] wanted=[" + std::string(wanted) + "]");
}

int main() {
  {
    const char *argv[] = {"app", "-n", "bob", "-i",   "3",  "-i",   "4",
                          "--tag", "a", "--tag", "bb", "--view", "vt", "in"};
    etl_args a{};
    check_error("etl: 解析", parse(14, argv, a), error::ok);
    check_text("etl::string 取值", a.name.c_str(), "bob");
    check("etl::vector 可重复", a.ids.size() == 2 && a.ids[1] == 4);
    check("etl::vector<etl::string> 可重复", a.tags.size() == 2 && a.tags[1] == "bb");
    check_text("etl::string_view 零拷贝", std::string_view(a.view.data(), a.view.size()), "vt");
    check_text("etl::string 位置参数", a.first.c_str(), "in");
  }
  {
    // etl::string<N> 满了是静默截断 —— CLI 必须先问容量再写，装不下就报错
    etl_args a{};
    check_error("etl::string 装不下报错（不截断）", parse("--name=123456789", a),
                error::value_too_long);
    check("etl::string 保持原值", a.name.empty());
    check_error("etl::string<4> 位置参数装不下", parse("abcde", a), error::value_too_long);
  }
  {
    // 定长容器塞满 → too_many_values
    etl_args a{};
    check_error("etl::vector<int,2> 塞满", parse("-i 1 -i 2 -i 3", a), error::too_many_values);
    check_error("etl::vector<etl::string<4>,2> 塞满", parse("--tag a --tag b --tag c", a),
                error::too_many_values);
    check_error("容器元素装不下", parse("--tag toolong", a), error::value_too_long);
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
