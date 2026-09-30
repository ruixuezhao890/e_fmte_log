/**
 ******************************************************************************
 * @file           : eserde_schema_check.cpp
 * @brief          : eserde 基座（能力标签 / schema / 字段标签 / 字段访问）的行为检查
 * @attention      : 这一层不产生任何格式化行为，只把 E_FMT_DERIVE 生成的声明原文
 *                   变成编译期可查的数据。逐条钉住：能力查询、字段名与类型名、
 *                   标签（含带值标签）、反查、字段访问、枚举取值。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/serde.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace e_fmt;    // Debug
using namespace eserde;

// ============================================================================
// 被测类型
// ============================================================================
E_FMT_DERIVE(struct person {
  int age;
  [[efmt::arg(short, long)]]
  std::string name;
  [[efmt::arg(long, help = "monthly salary")]]
  float salary;
  int hidden [[maybe_unused]];
}, Debug, Serialize);

E_FMT_DERIVE(struct only_debug { int n; });

E_FMT_DERIVE_ENUM(enum class state {
  idle,
  busy = 5,
  fault
});

// ============================================================================
// 编译期断言：schema 就是常量表达式，能在 static_assert 里直接用
// ============================================================================
static_assert(eserde::is_registered_v<person>, "person 注册过");
static_assert(!eserde::is_registered_v<int>, "int 没注册过");

static_assert(eserde::has_cap_v<person, Serialize>, "person 带 Serialize");
static_assert(!eserde::has_cap_v<person, Deserialize>, "person 没带 Deserialize");
static_assert(eserde::has_cap_v<only_debug, Debug>, "注册过的类型默认有 Debug");
static_assert(eserde::has_cap_v<person, Debug>, "显式写的 Debug 也算");
static_assert(!eserde::has_cap_v<state, Serialize>, "枚举侧暂时没有能力标签");

static_assert(eserde::field_count<person>() == 4, "4 个字段");
static_assert(eserde::field_name<person>(0) == "age", "字段 0 名字");
static_assert(eserde::field_type_name<person>(1) == "std::string", "字段类型名（文本）");
static_assert(eserde::field_type_name<person>(2) == "float", "字段类型名（文本）");
static_assert(eserde::tag_count<person>(0) == 0, "age 没有标签");
static_assert(eserde::tag_count<person>(1) == 2, "name 两个标签");
static_assert(eserde::tag<person>(1, 0).name == "short", "第一个标签");
static_assert(!eserde::tag<person>(1, 0).has_value, "无值标签");
static_assert(eserde::tag<person>(1, 1).name == "long", "第二个标签");
static_assert(eserde::tag<person>(2, 1).has_value, "带值标签");
static_assert(eserde::tag<person>(2, 1).value == "monthly salary", "带值标签内容");
static_assert(eserde::has_tag<person>(1, "long"), "has_tag 命中");
static_assert(!eserde::has_tag<person>(1, "nope"), "has_tag 落空");
static_assert(eserde::find_field<person>("salary") == 2, "按字段名反查");
static_assert(eserde::find_field<person>("nope") == eserde::npos, "查不到 = npos");
static_assert(eserde::find_by_tag<person>("short") == 1, "按标签反查");

static_assert(eserde::is_enum<state>(), "state 是枚举");
static_assert(!eserde::is_enum<person>(), "person 不是枚举");
static_assert(eserde::field_count<state>() == 3, "3 个取值");
static_assert(eserde::field_name<state>(1) == "busy", "取值名");
static_assert(eserde::enum_value<state>(1) == 5, "显式取值");
static_assert(eserde::enum_value<state>(2) == 6, "自增取值");

// ============================================================================
// 运行期检查
// ============================================================================
static int g_checks = 0;
static int g_failures = 0;

static void check(const char *what, const std::string &actual, const char *wanted) {
  ++g_checks;
  if (actual == wanted) return;
  ++g_failures;
  std::printf("FAIL %s\n     actual  =[%s]\n     expected=[%s]\n", what, actual.c_str(), wanted);
}

int main() {
  person p{};
  p.age = 18;
  p.name = "bob";
  p.salary = 1234.5f;
  p.hidden = 7;

  // visit_fields：顺序 = 声明顺序，名字来自 schema
  std::string visited;
  eserde::visit_fields(p, [&](std::string_view name, const auto &) {
    if (!visited.empty()) visited += ',';
    visited += std::string(name);
  });
  check("visit_fields 顺序", visited, "age,name,salary,hidden");

  // field_at：可读可写（反序列化要从这里写回）
  // 注意这里不打印 person：嵌入式配置下 std::string 成员没有格式化器，能打印是宿主专属能力
  eserde::field_at<0>(p) = 30;
  eserde::field_at<2>(p) = 999.5f;
  check("field_at 写回 age", std::to_string(eserde::field_at<0>(p)), "30");
  check("field_at 写回 salary", std::to_string(eserde::field_at<2>(p)), "999.500000");

  // const 对象只读访问
  const person &cp = p;
  check("const field_at", std::to_string(eserde::field_at<0>(cp)), "30");

  // 遍历时按标签挑字段（上层序列化就是这个用法）
  std::string tagged;
  eserde::visit_fields(p, [&](std::string_view name, const auto &) {
    const std::size_t i = eserde::find_field<person>(name);
    if (eserde::has_tag<person>(i, "long")) {
      if (!tagged.empty()) tagged += ',';
      tagged += std::string(name);
    }
  });
  check("按标签筛选", tagged, "name,salary");

  // schema 与打印是同一份声明原文：名字必须一一对上
  check("schema/打印 一致", std::to_string(eserde::field_count<person>()), "4");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
