/**
 ******************************************************************************
 * @file           : eserde_no_tags_check.cpp
 * @brief          : 裁剪检查 - -DEFMT_DERIVE_ENABLE_TAGS=0 时基座仍然可用
 * @attention      : 关掉标签解析只影响 [[efmt::arg(...)]] 的内容；字段名/类型名/能力
 *                   查询照常。属性本身仍会被跳过（不会把字段丢掉）。
 ******************************************************************************
 */

#include <middleware/efmt/core/format.hpp>
#include <eserde/serde.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace e_fmt;
using namespace eserde;

E_FMT_DERIVE(struct sample {
  int age;
  [[efmt::arg(short, long)]]
  std::string name;
}, Debug, Serialize);

static_assert(eserde::field_count<sample>() == 2, "字段数不受裁剪影响");
static_assert(eserde::field_name<sample>(1) == "name", "带属性的字段名照常");
static_assert(eserde::field_type_name<sample>(1) == "std::string", "类型名照常");
static_assert(eserde::tag_count<sample>(1) == 0, "标签被裁掉");
static_assert(!eserde::has_tag<sample>(1, "long"), "has_tag 恒 false");
static_assert(eserde::has_cap_v<sample, Serialize>, "能力查询不受裁剪影响");

int main() {
  sample s{18, "bob"};
  std::string visited;
  eserde::visit_fields(s, [&](std::string_view name, const auto &) {
    if (!visited.empty()) visited += ',';
    visited += std::string(name);
  });
  const bool ok = visited == "age,name" && format("{}", s) == "{ age = 18, name = bob }";
  std::printf("no-tags: visited=%s format=%s\n", visited.c_str(), format("{}", s).c_str());
  std::printf("%d checks, %d failures\n", 2, ok ? 0 : 1);
  return ok ? 0 : 1;
}
