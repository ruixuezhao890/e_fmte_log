# MATCHIT 使用手册（第三方库 · 本仓库怎么用）

matchit.cpp 是 Rust `match` 表达式的 C++17 移植，在本仓库里是一份**第三方冻结副本**，不是我们的代码：
上游 <https://github.com/BowenFu/matchit.cpp>（Apache-2.0，单头文件）。

| 想知道的 | 去哪看 |
|---|---|
| 我们为什么引进它、用在哪、踩过哪三个坑 | [matchit/README.md](../../matchit/README.md) |
| 本地改了哪几行、怎么重新同步上游 | [matchit/PATCHES.md](../../matchit/PATCHES.md) |
| 许可证原文 | `matchit/LICENSE`（Apache-2.0，随副本原样复制） |
| 命令名模式段的**使用**说明（写给用 ecli 的人） | [EFMT-使用手册.md](../EFMT-使用手册.md) 的 5.11 |
| 它的体积读数 | 同上的 8.2c |

本手册只讲**怎么写代码**：示例怎么摆、本仓库的接缝在哪、坑长什么样。不抄 API 字典。

> 本手册的每一段示例都由 [tests/matchit_manual_examples.cpp](../../tests/matchit_manual_examples.cpp) 编译并跑断言
> （`g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I.`，零 warning，默认配置 54 checks / 0 failures）。

---

## ① 30 秒

| 问题 | 答案 |
|---|---|
| 这是什么 | `match(值)( pattern \| 模式 = 结果, … )` —— 值进去，第一条命中的分支的结果出来 |
| 解决什么问题 | "值 → 分支"的对照表。比 if/else 链好读，而且模式能写区间、集合、谓词、绑定、解构 |
| 怎么进来 | `#include <matchit/matchit.h>`（我们放在 `matchit/`，上游是 `include/matchit.h`）。单头文件、只用标准库、无堆、可 constexpr |
| 需要什么前置 | C++17；编译时 `-I<仓库根>` 让它可见；**不用**定义任何宏（`MATCHIT_USE_EXCEPTIONS` 自己探测） |
| 本仓库为什么要它 | 只有一件事：ecli 命令名的**模式段**（`"wifi set :ssid"` / `"log *rest"`）里"一个 token 算不算一个段"的判定，交给它的 extractor 协议（`app` + `some` + 通配 `_`），我们不自己写模式解释器 —— 见 ④ |
| 谁在用 | `ecli/command.hpp`（模式命令的判定）、`sandbox/main.cpp`（`level :n` 命令里直接写 match 表达式）。efmt / elog / eserde 都不认识它 |
| 不想要它 | 编译时 `-DECLI_ENABLE_PATTERN_COMMANDS=0`：不再 include matchit，`:name`/`*name` 当字面量。整程序省约 **0.6 KB**（8.2c 实测 10580 → 9924 B；细节见 ⑤.8） |
| 上游能力 vs 我们的用法 | 上游一整套都在文件里（绑定、范围、guard、`as`、`dsVia`…）；**本仓库只用了 `app`+`some`+`_` 那一小块**。手册里每处都会标明是哪一类 |

---

## ② 第一个能跑的例子

三段拼起来就是一个完整程序（下面每段都来自 tests/matchit_manual_examples.cpp，逐字）：

```cpp
#include <matchit/matchit.h>
#include <cstdio>

// —— 手册 2：level_text() ——
static const char *level_text(int n) {
  using namespace matchit;
  return match(n)(
      // clang-format off
      pattern | 0                    = "0：关",
      pattern | and_(_ >= 1, _ <= 9) = "1..9：开",
      pattern | _                    = "超出 0..9"
      // clang-format on
  );
}

// —— 测试文件里同样的三行在 first_example_main() 里 ——
int main() {
  std::printf("%s\n", level_text(0));    // 0：关
  std::printf("%s\n", level_text(5));    // 1..9：开
  std::printf("%s\n", level_text(99));   // 超出 0..9
}
```

真跑出来的输出：

```
0：关
1..9：开
超出 0..9
```

三件必须知道的事：

| 事 | 说明 |
|---|---|
| 写在哪 | 仓库里的写法是**函数内** `using namespace matchit;`（`sandbox/main.cpp` 的 `run_level`、`ecli` 的测试都这样）；只在头文件里写全 `::matchit::`，见 ④.1 |
| 每行是什么 | `pattern` 是个空对象，只为让 `\|` 读起来像 Rust；左边是模式，右边是结果（值或 `[&]{…}`） |
| 返回值 | 分支写 `= "文本"` / `= 42` / `= [&]{…}` 都行，这个例子返回 `const char*`。**每条分支的返回值要能算出一个共同类型**（`std::common_type`），否则编译期就报错，见 ⑤.4 |
| 最后那行 `pattern \| _` | 兜底。没有它，一条都不命中时上游会抛异常（无异常构建走另一条路），见 ⑤.7 |

---

## ③ 逐块讲清楚

> 本节示例由 tests/matchit_manual_examples.cpp 编译验证（每小节名 = 测试文件里的注释小节）。
> 示例里用到的标准头（`<optional>` / `<string_view>` / `<array>` / `<tuple>` / `<variant>` …）
> 按需自己 include —— 完整清单在示例文件顶部。

### 3.0 骨架就三句话

1. `match(值)` 拿到待匹配对象 —— **它存的是引用**，被匹配的那个值活到这条表达式结束就行。
2. 一条分支是 `pattern | 模式 = 结果`。
3. `match(值)(分支…)` 返回命中分支的结果；所有分支都返回 `void` 时它是一句普通语句，**一条都不命中也不算错**。

### 3.1 返回值与两种收尾

```cpp
// —— 手册 3.1：返回值 ——
static const char *traffic_light(int n) {
  using namespace matchit;
  return match(n)(pattern | 0 = "红", pattern | 1 = "黄", pattern | _ = "绿");
}

static int side_effect_only(int n) {
  using namespace matchit;
  int hits = 0;
  // 所有分支都返回 void → 语句形式：一条都没匹配上也不报错（也不抛）
  match(n)(pattern | 1 = [&] { hits = 1; }, pattern | 2 = [&] { hits = 2; });
  return hits;
}
```

| 写法 | 含义 | 不命中时 |
|---|---|---|
| `return match(值)(…)` | 表达式形式：返回命中分支的值 | 没兜底 → 抛 `std::logic_error`（⑤.7） |
| `match(值)(…);` | 语句形式：所有分支返回 `void` | 什么都不发生（`side_effect_only(7) == 0`） |

### 3.2 字面量与通配 `_`

```cpp
// —— 手册 3.2：字面量与通配 ——
static int segment_kind(std::string_view seg) {
  using namespace matchit;
  return match(seg)(
      pattern | "" = 0,        // 字面量：与值相等才命中
      pattern | "wifi" = 1,
      pattern | _ = 2);        // 通配：什么都命中，也当兜底用
}
```

| 写法 | 含义 |
|---|---|
| `pattern \| "wifi"` | 字面量模式：拿值去 `==` 。值可以是 `std::string_view`、整数、枚举、指针… |
| `pattern \| _` | 通配：恒真。放在末尾就是兜底 |

> 值类型要能跟模式比相等。整型配整型、`string_view` 配 `""` 都行；拿 `const char*` 模式去比 `int` 会直接编译失败。

### 3.3 比较、`and_`、`or_`、`not_`

```cpp
// —— 手册 3.3：组合 ——
static const char *level_bucket(int n) {
  using namespace matchit;
  return match(n)(
      pattern | 0 = "关",
      pattern | and_(_ >= 1, _ <= 9) = "1..9",
      pattern | or_(10, 20, 30) = "10/20/30",
      pattern | not_(_ < 0) = "其它非负",
      pattern | _ = "负数");
}
```

| 写法 | 含义 |
|---|---|
| `_ >= 1` / `_ < 0` / `_ % 2 != 0` | 让 `_` 参与比较或算术，得到的**就是谓词模式**（内部是 `meet(lambda)`） |
| `and_(a, b, …)` | 全部成立。短路求值：第一个不成立就不往下走（所以 3.5 的 `some` 不会空指针解引用） |
| `or_(a, b, …)` | 任一成立，左边优先 |
| `not_(p)` | 取反 |

### 3.4 谓词模式 `meet(...)`

```cpp
// —— 手册 3.4：谓词模式 ——
// 谓词得是【类类型】（有 operator() 的东西）：lambda 或仿函数。
// 传裸函数名 / 函数指针会撞上 matchit 里 `class Meet : public Pred` —— 编不过。
struct is_even_fn {
  bool operator()(int v) const { return v % 2 == 0; }
};

static const char *parity(int n) {
  using namespace matchit;
  return match(n)(
      pattern | meet(is_even_fn{}) = "偶",
      pattern | (_ % 2 != 0) = "奇",   // 比较/算术写法：让 _ 参与运算就得到谓词模式
      pattern | _ = "?");
}
```

| 写法 | 含义 |
|---|---|
| `meet(仿函数)` | 用自己的谓词；`meet(is_even_fn{})`、`meet([](int v){ return v % 2 == 0; })` 都行 |
| `meet(裸函数名)` | **不行**。上游 `class Meet : public Pred` 要求 `Pred` 是类类型，函数名推出来是 `bool(int)`，直接编译失败（⑤.6） |

### 3.5 提取器 `app(提取器, 内层模式)` + `some(...)`

这一节就是本仓库引进 matchit 的原因 —— ④ 里 `segment_extractor` 用的是同一套。

```cpp
// —— 手册 3.5：提取器 ——
struct digit_head {
  // 命中就交出东西（这里交出 token 本身），不命中就 nullopt
  std::optional<std::string_view> operator()(std::string_view token) const {
    if (!token.empty() && token[0] >= '0' && token[0] <= '9') return token;
    return std::nullopt;
  }
};

static bool is_number_token(std::string_view token) {
  using namespace matchit;
  return match(token)(
      pattern | app(digit_head{}, some(_)) = true,   // some(_)：拿到提取器交出来的东西
      pattern | _ = false);
}
```

| 写法 | 含义 |
|---|---|
| `app(f, p)` | 先用 `f` 把值变一下（或"抽出来"），再拿结果去匹配 `p`。`f` 可以是 lambda、仿函数、`&T::member`（见 3.8） |
| `some(p)` | 上游的糖，等价于 `and_(app(cast<bool>, true), app(deref, p))`：**"有值就拿它出来，再匹配 `p`"**。对 `std::optional` / 指针 都成立 |
| `app(f, _)` | 只用 `f` 判断"抽得出东西吗"，不关心抽出什么 |

> 关键性质：`and_` 短路。`f` 返回 `nullopt` 时 `deref` 那一步根本不会跑 —— 空 token / 空 optional 不会炸。

### 3.6 绑定 `Id<T>`

```cpp
// —— 手册 3.6：绑定 ——
static void id_binding_demo() {
  using namespace matchit;
  Id<int> hit;
  int v = 42;
  const bool ok = match(v)(pattern | hit = true, pattern | _ = false);
  check("Id<T> 绑定：命中后用 *hit 取值", ok && *hit == 42);
  check("Id<T> 里存的是【指针】：*hit 就是被匹配的那个对象", &*hit == &v);
}
```

| 写法 | 含义 |
|---|---|
| `pattern \| id` | 绑定：命中后 `*id` 就是被匹配的那个值 |
| `id.at(模式)` | 有绑定也要挑形状（例如 `id.at(ooo)` 绑"中间那一段"，见 3.9） |
| `Id<T>` 别声明成 `const` | 上游的 `get()` / `operator*` 都是非 const 的，写 `const Id<T>` 编不过 |
| `T` 要能 `==` | 上游 `IdTraits<T>::equal` 就是 `lhs == rhs`；自定义类型得给 `operator==`（否则报错，⑤.6） |

> **`Id<T>` 里存的是指针**（上游 `ValueVariant` / `StorePointer` 那套）。指向的对象一死，`*id` 就是悬垂读 —— 这是 ⑤.1 的坑，本仓库的结论是：**判定用它、取值不用它**。

### 3.7 多值 / tuple：`match(a, b)` 配 `ds(...)`

```cpp
// —— 手册 3.7：多值 ——
static int quadrant(int x, int y) {
  using namespace matchit;
  return match(x, y)(
      pattern | ds(0, 0) = 0,
      pattern | ds(_, 0) = 1,
      pattern | ds(0, _) = 2,
      pattern | _ = 3);
}

static bool is_pair_1_2(const std::tuple<int, int> &t) {
  using namespace matchit;
  return match(t)(pattern | ds(1, 2) = true, pattern | _ = false);
}
```

| 写法 | 含义 |
|---|---|
| `match(a, b, …)` | 一次匹配多个值（内部收成 `forward_as_tuple`） |
| `ds(p1, p2, …)` | "结构模式"：按位置逐个匹配。上面 `match(t)` 那个是 tuple 值直接配 `ds` |
| `pattern \| _` | 多值匹配也能用通配兜底 |

### 3.8 结构体成员：`dsVia(&T::member, …)`

```cpp
// —— 手册 3.8：结构体成员 ——
struct point2 {
  int x;
  int y;
};

static bool is_origin(const point2 &p) {
  using namespace matchit;
  return match(p)(pattern | dsVia(&point2::x, &point2::y)(0, 0) = true, pattern | _ = false);
}
```

| 写法 | 含义 |
|---|---|
| `dsVia(&T::a, &T::b)(p1, p2)` | 先按成员指针取值（`app(&T::member, …)`），再按 `ds` 匹配。字段顺序自己定，不必跟声明顺序一致 |
| 值类型 | 传 `const point2&` 也行（成员取出来是 `const int&`，不需要拷贝） |

### 3.9 范围、`ooo`、中间那一段 `Subrange`

```cpp
// —— 手册 3.9：范围 ——
static void range_demo() {
  using namespace matchit;
  std::array<int, 5> arr{1, 2, 3, 4, 5};
  Id<SubrangeT<std::array<int, 5>>> mid;
  int size = -1;
  // 每条分支都返回 bool → 表达式形式可以接住结果（有一条返回 void 就得走语句形式）
  const bool ok = match(arr)(
      pattern | ds(1, mid.at(ooo), 5) = [&] {
        size = static_cast<int>((*mid).size());
        return true;
      },
      pattern | _ = [&] {
        size = -1;
        return false;
      });
  check("ds(1, ooo, 5)：两头固定、中间那段被绑定", ok && size == 3, std::to_string(size));
  const std::array<int, 3> want{2, 3, 4};
  check("绑定到的 Subrange 可以当面遍历",
        std::equal((*mid).begin(), (*mid).end(), want.begin()));
}
```

| 写法 | 含义 |
|---|---|
| `ds(1, ooo, 5)` | 数组 / `std::array` / 任何带 `begin/end` 的范围：`ooo` 表示"中间随便几个" |
| `mid.at(ooo)` | 把中间那段绑成 `SubrangeT<范围类型>`；`(*mid).begin()/end()/size()` 直接当面用 |
| 长度 | 两头固定时，长度必须正好对上（`{1,2,3,4,5}` 配 `ds(1, ooo, 5)` 取出 `{2,3,4}`） |

### 3.10 守卫 `when(...)`

```cpp
// —— 手册 3.10：守卫 ——
static const char *sign_of(int n) {
  using namespace matchit;
  Id<int> v;
  return match(n)(
      pattern | v | when([&] { return *v == 0; }) = "零",
      pattern | v | when([&] { return *v > 0; }) = "正",
      pattern | _ = "负");
}
```

| 写法 | 含义 |
|---|---|
| `pattern \| id \| when(谓词)` | 先绑 `id`，再用谓词（无参，可读 `*id`）决定这条分支算不算命中 |

### 3.11 一条都没匹配上会怎样

```cpp
// —— 手册 3.11：没兜底 ——
static void no_fallback_demo() {
#if MATCHIT_USE_EXCEPTIONS
  bool threw = false;
  try {
    (void)matchit::match(99)(matchit::pattern | 1 = 0);
  } catch (const std::logic_error &) {
    threw = true;
  }
  check("有异常构建：一条都没匹配上 → 抛 std::logic_error（上游行为）", threw);
#else
  check("无异常构建：MATCHIT_USE_EXCEPTIONS 自动探测为 0",
        MATCHIT_USE_EXCEPTIONS == 0);
#endif
}
```

| 宏 | 上游 | 本仓库的做法 |
|---|---|---|
| `MATCHIT_USE_EXCEPTIONS` | 无（上游只认异常） | 自动探测：有异常 1，`-fno-exceptions` 下 0；可自己 `-D` 覆盖 |
| （无匹配） | `throw std::logic_error{"Error: no patterns got matched!"}` | 有异常时**保留原 throw**；无异常时走 `MATCHIT_NO_MATCH()`（默认 `assert`，NDEBUG 下返回返回值类型的默认值） |
| （不该出现的 Id 状态） | `throw std::logic_error(...)` × 5 | `MATCHIT_FAIL()`（默认 `assert` + `std::abort()`） |

> 本仓库的用法**一律带 `pattern | _` 兜底**，所以这两条路都走不到。想要别的行为（写日志、复位），在 include 之前自己定义 `MATCHIT_FAIL()` / `MATCHIT_NO_MATCH()` 即可 —— 见 [matchit/PATCHES.md](../../matchit/PATCHES.md) 改动 1。

### 3.12 上游还有这些（本仓库没接，但真能用）

```cpp
// —— 手册 3.12：上游其余能力 ——
static int optional_state(const std::optional<int> &o) {
  using namespace matchit;
  return match(o)(pattern | none = 0, pattern | _ = 1);   // none：空 optional / 空指针
}

static int variant_tag(const std::variant<int, std::string> &v) {
  using namespace matchit;
  return match(v)(
      pattern | as<int>(_) = 1, pattern | as<std::string>(_) = 2, pattern | _ = 0);
}

struct pair_t {
  int a;
  int b;
};

static int pair_via_variant(const std::variant<pair_t, int> &v) {
  using namespace matchit;
  return match(v)(
      pattern | asDsVia<pair_t>(&pair_t::a, &pair_t::b)(1, 2) = 5, pattern | _ = 0);
}

static bool matched_helper(int n) { return matchit::matched(n, matchit::or_(1, 5, 9)); }
```

| 名字 | 一句话 |
|---|---|
| `none` | `app(cast<bool>, false)`：空 optional / 空指针专用 |
| `some(p)` | 反过来："有值，且值匹配 `p`"（3.5 用的就是它） |
| `as<T>(p)` | 先把值当 `std::variant` / `std::any` 里的 `T` 看一眼（取不到就不命中） |
| `dsVia` / `asDsVia` | 结构体成员解构；`asDsVia<T>` 是"variant 里取 `T` 再解构" |
| `matched(v, p)` | 一句话问"匹不匹配"，等价于包一层 `match` 返回 `bool` |
| `expr` | 把普通值包成"无参可调用"（分支右边写 `= 0` 时，上游内部就是用它包的） |
| `Subrange` | `ooo` 绑出来的区间类型（3.9 里的 `SubrangeT` = 它的别名 + 迭代器类型） |

> 这些本仓库都没接。要用就先在 tests/matchit_manual_examples.cpp 的 3.12 小节加一条检查 —— 别让它们只活在文档里。

### 3.13 写错时长什么样（三条真实报错）

| 你写的 | 编译器说 |
|---|---|
| `pattern \| 1 = 0, pattern \| _ = "two"` | `error: no type named 'type' in 'struct std::common_type<int, const char*>'` |
| `meet(裸函数名)`（不是 3.4 的 `is_even_fn{}`） | `error: base type 'bool(int)' fails to be a struct or class type`（`class Meet : public Pred`） |
| 用 `Id<我的类型>` 但没给 `operator==` | `error: no match for 'operator==' (operand types are 'const 我的类型' and 'const 我的类型')` |

---

## ④ 本仓库的接缝，和一个完整可抄示例

> 本节代码里：4.1 是**逐字引用** `ecli/command.hpp`（不是手册示例，改它要改头文件）；4.2 的工具逐字来自 tests/matchit_manual_examples.cpp。
> 面向使用者的说明（怎么写命令名、怎么注入参数）在 [EFMT-使用手册.md](../EFMT-使用手册.md) 的 5.11；这里只讲 matchit 那一层。

### 4.0 全景：只有三个函数

| 函数 | 位置 | 干什么 |
|---|---|---|
| `segment_extractor` | `ecli::detail` | 段模式的"提取器"：喂一个 token，命中就把 token 交出去 |
| `match_segment()` | `ecli::detail` | 一个 token ↔ 一个段模式：**matchit 只在这里出现** |
| `match_command_pattern()` | `ecli::detail` | 沿命令名逐段走 token，收集捕获、数"特异性" |

### 4.1 一个 token ↔ 一个段模式

```cpp
// 逐字引用 ecli/command.hpp
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
```

逐句对应（左边是我们的代码，右边是 ③ 里的写法）：

| 这行 | 为什么这么写 |
|---|---|
| `segment_extractor{pattern}` | 提取器是**按值**带进 match 表达式的（`app` 存的是副本），所以它持有 `pattern` 的拷贝，不会悬垂 |
| `some(::matchit::_)` | 提取器返回的是 `std::optional` → 用 `some(_)` 表示"有值就取出来，值随便"。返回 `nullopt` 时 `and_` 短路，不会解引用 |
| `= true / = false` | 两条分支同类型（`bool`），所以能直接拿去 `if` |
| 通配兜底 | 分支表最后那行 `pattern \| _ = false` —— 有兜底，就不会走 ⑤.7 那条路 |
| 全限定 `::matchit::` | 头文件里不 `using namespace`，免得跟 `ecli` / `e_fmt` 的 `detail` 之类撞名（手册示例里为了短才 inside 函数 `using`） |
| 返回值另外抄一份 | `if (hit) captured = token;` —— **不用 `Id<std::string_view>` 接**，理由见 4.5 |

跑起来是什么样（断言在测试文件的 `seam_demo()`）：

| 调用 | 结果 |
|---|---|
| `match_segment(":ssid", "home", cap)` | `true`，`cap == "home"` |
| `match_segment("*rest", "x", cap)` | `true`，`cap == "x"` |
| `match_segment("wifi", "wifi", cap)` | `true` |
| `match_segment("wifi", "lan", cap)` | `false`（字面量段不相等） |

### 4.2 端到端小工具：命令表 + 模式段

一个能直接抄的真实场景 —— 两块命令、一个 `:参数` 段、一个 `*余下` 段、一个"具体命令赢过兜底"的例子。
回复写进定长缓冲（`buffer_reply`），所以不用串口也能跑。

```cpp
// —— 手册 4.2：端到端小工具 ——
E_FMT_DERIVE(struct net_set_args {
  [[efmt::arg(skip)]] std::string ssid;   // 由 "net set :ssid" 注入
}, Cli);

E_FMT_DERIVE(struct log_args_t {
  [[efmt::arg(skip)]] std::vector<std::string> rest;   // 由 "log *rest" 逐个 push
}, Cli);

E_FMT_DERIVE(struct plain_args {
  [[efmt::arg(short = "v", long = "verbose", help = "多说两句")]] bool verbose = false;
}, Cli);

// efmt 格式化 + 回给提问的那一路（跟 sandbox/main.cpp 里的 replyf 同形）
template <typename... A>
static void replyf(reply out, std::string_view fmt, const A &...args) {
  char buf[384];
  const std::size_t n = e_fmt::format_to(buf, sizeof(buf), fmt, args...);
  out.put(std::string_view(buf, n < sizeof(buf) ? n : sizeof(buf) - 1));
  if (n >= sizeof(buf)) out.put_lit("...(truncated)\n");
}

static std::string g_trace;

static void net_run(const plain_args &, reply out) {
  g_trace = "net";
  out.put_lit("net: 概览\n");
}
static void net_set_run(const net_set_args &a, reply out) {
  g_trace = "net-set:" + a.ssid;
  replyf(out, "ssid -> {}\n", a.ssid);
}
static void net_rest_run(const plain_args &, reply out) {
  g_trace = "net-rest";
  out.put_lit("net: 未知子命令\n");
}
static void log_run(const log_args_t &a, const params &p, reply out) {
  g_trace = "log:";
  for (std::size_t i = 0; i < a.rest.size(); ++i) {
    g_trace += a.rest[i];
    g_trace += "|";
  }
  g_trace += "rest_count=" + std::to_string(p.rest_count(0));
  replyf(out, "{} 行\n", a.rest.size());
}

static constexpr command kTool[] = {
    {"net", "net 概览", command_of<plain_args, net_run>(), help_of<plain_args>()},
    {"net set :ssid", "设置 ssid", command_of<net_set_args, net_set_run>(), help_of<net_set_args>()},
    {"net *rest", "未知子命令", command_of<plain_args, net_rest_run>(), help_of<plain_args>()},
    {"log *rest", "写日志", command_of<log_args_t, log_run>(), help_of<log_args_t>()},
};

static char g_reply[512];

template <std::size_t N>
static error run_line(const command (&table)[N], const char *line, std::string &text) {
  static char scratch[ECLI_MAX_LINE];
  buffer_reply b{g_reply, sizeof(g_reply), 0};
  const error e = dispatch(table, line, scratch, sizeof(scratch), b.as_reply());
  text.assign(g_reply, b.used < sizeof(g_reply) ? b.used : sizeof(g_reply) - 1);
  return e;
}
```

喂几条命令进去，`g_trace` 与回复分别是（断言在测试文件的 `tool_demo()`）：

| 敲的命令 | 谁接走了 | 处理函数看到的 | 回复 |
|---|---|---|---|
| `net set home` | `"net set :ssid"`（段最具体） | `ssid == "home"`（按名字注入同名字段） | `ssid -> home` |
| `net` | `"net"` | — | `net: 概览` |
| `net xyz` | `"net *rest"`（兜底） | `*rest` 收了 `xyz` | `net: 未知子命令` |
| `log a b c` | `"log *rest"` | `rest == {a,b,c}`，`p.rest_count(0) == 3` | `3 行` |
| `log` | `"log *rest"`（`*rest` 可以是 0 个） | `rest` 空，`p.rest_count(0) == 0` | `0 行` |
| `help net set` | 内置 help（前缀命中） | 不执行处理函数 | `usage: net set :ssid` |

三件容易踩的事：

- 处理函数有两种签名：`void(const Args&, reply)` 和 `void(const Args&, const params&, reply)`。
  想看原始捕获（`p.get("rest")` / `p.rest_count(i)` / `p.rest_at(i, k)`）就用第二种；用第一种时 **`reply` 那个参数名可以省**，省掉 `-Wunused-parameter`。
- 想喂给捕获的字段建议标 `[[efmt::arg(skip)]]`：它自己不进选项表，值由模式段注入。
- 捕获注入按**字段名**找下标，查的是编译期那张 rodata 规格表（不是 `eserde::find_field` —— 那会拖进 efmt 的声明原文解析函数，一个类型约 1.5 KB）。

### 4.3 裁剪版：`-DECLI_ENABLE_PATTERN_COMMANDS=0`

| 写法 | 模式段开着（默认） | 关掉之后 |
|---|---|---|
| `:ssid` | 吃一个 token，捕获注入同名字段 | **当字面量**：token 必须正好是 `:ssid`，且不产生捕获 |
| `*rest` | 吃余下全部（可为 0），注入容器字段 | 当字面量：token 必须正好是 `*rest` |
| `net set home` | 命中 `"net set :ssid"` | 只命中 `"net"`，多出的 token 由参数解析报 `error::too_many_args` |
| matchit | 被 include（约 0.6 KB） | **一行都不进固件** |

同一个 `kTool` 表、同一份源码，两种配置都能编 —— 断言分在 `#if ECLI_ENABLE_PATTERN_COMMANDS` 的两支里（测试文件 `tool_demo()`）。

### 4.4 匹配排序落在哪

排序不在 matchit 里，在 `ecli/command.hpp` 的 `match_command_pattern()` + `dispatch()`：

| 概念 | 在哪 | 规则 |
|---|---|---|
| 吃掉的 token 数 | `match_command_pattern()` 的返回值 | 逐段走；段比 token 多 → 返回 0（不匹配）；`*rest` 一次吃掉余下全部 |
| 段特异性 `spec` | 同一个函数写进 `params::spec` | 只数**非 `*` 段**：`"sensor read"` 是 2，`"sensor *rest"` 是 1 —— 否则 catch-all 会盖掉更具体的命令 |
| 谁赢 | `dispatch()` 里那三行 | `p.spec > best_spec \|\| (p.spec == best_spec && used > best_used)`：先比特异性，再比吃掉的 token 数 |
| help 的前缀匹配 | `match_command_prefix()` | `help net set` 用"用户给的 token 是不是命令模式的前缀"找命令，参数段算任意 token，`*` 段到此为止 |

> `match_command_pattern()` 里唯一调 matchit 的地方就是 `match_segment(seg, tokens.items[k], captured)`；把模式段关掉时它退化成 `token != pattern` 的字符串比较，别的逻辑一模一样。

命令表这条线的前因后果：[ECLI-命令行解析-方案.md](../ECLI-命令行解析-方案.md)（阶段二：命令表 + 子命令、已知上限）、
[ECLI-与clap的差距清单.md](../ECLI-与clap的差距清单.md)（第 1 条就是命令名模式段）。
给**用 ecli 的人**看的说明在 [EFMT-使用手册.md](../EFMT-使用手册.md) 的 5.11。

### 4.5 为什么捕获值不走 `Id<T>`

- 现象：第一版用 `Id<std::string_view>` 接捕获值，出了 match 表达式拿到的是垃圾内存。
- 原因：`Id<T>` 里存的是**指针**（上游 `ValueVariant` / `StorePointer`），绑的是 match 表达式里的临时值；表达式一结束，那个对象的生命周期就到头了（⑤.1 是可复现的演示）。
- 怎么办：matchit 只回答"命中不命中"这个 `bool`，**值我们从手上的 token 表直接取**（`if (hit) captured = token;`）。token 本来就活在整个 dispatch 里，不需要谁来"记住"它。

---

## ⑤ 坑与 FAQ

> 5.1 的坑有可跑的演示与断言（测试文件 `id_points_at_a_dead_object()`）；5.5 的坑**故意没放进测试文件** —— 它会让 GCC 直接告警，跟"零 warning"冲突，手册里给出的是我实测到的现象。

### 5.1 `Id<T>` 存的是指针，绑到会死的对象上就悬垂

- **现象**：`*id` 读出垃圾值 / 崩溃。上游 README 与我们的 `ecli/command.hpp` 注释都记着这一笔。
- **原因**：绑定不是拷贝 —— `Id<T&>` 里放的就是 `&值`。被匹配的值一旦是临时量或块内局部量，出了那条表达式（或那个块）指针就悬垂。
- **怎么办**：判定用 matchit、取值直接用手上的值；确实要绑，就绑**活得比你长**的对象。演示：`lifetime_probe` 出了块就析构，此时 `id` 指着的对象已经没了（断言证明了这一点，而且**不去**读它）。
- 附注：`match(值)` 自己也是按引用持有值的，所以别写 `match(构造出来的临时量)` 再把结果用到表达式外面。

### 5.2 `-fno-exceptions` 上游编不过（本地已修）

- **现象**：整库 `-fno-exceptions` 编译时，上游头文件里 6 处 `throw std::logic_error(…)` 直接报错。
- **原因**：上游在"逻辑上不该发生"的地方抛异常（`IdBlockBase` 的 `std::monostate` 分支与空指针解引用 5 处 + `match(...)` 一条都没匹配 1 处）。
- **怎么办**：本仓库用可配置的失败宏顶掉（[PATCHES.md](../../matchit/PATCHES.md) 改动 1）：`MATCHIT_USE_EXCEPTIONS` 自动探测（有异常 1 / 无异常 0，可自己 `-D` 覆盖）、`MATCHIT_FAIL()`、`MATCHIT_NO_MATCH()`。**有异常时行为与上游完全一致**。无异常构建下：`assert` 开着时直接 abort；NDEBUG 下"一条都没匹配上"会返回返回值类型的默认值 —— 所以千万别省掉 `pattern | _`。
- 自测：本手册的示例文件在 `-fno-exceptions` 下也编译并跑过（54 checks / 0 failures，宏的取值由 `#if` 分支断言）。

### 5.3 ARM 上 `int32_t` 不是 `int`，上游自检编不过（本地已修）

- **现象**：`arm-none-eabi-g++` 上编译失败，报 `std::holds_alternative` 相关错误。
- **原因**：上游文件里有一段自检 `constexpr auto y = 1;`，`1` 的类型是 `int`，而它拿去跟 `int32_t const *` 比 —— 在 `int32_t` 是 `long int` 的平台（arm-none-eabi）就对不上。
- **怎么办**：本地把它改成 `constexpr auto y = int32_t{1};`（[PATCHES.md](../../matchit/PATCHES.md) 改动 2，语义不变）。
- 本机复现不了：x86-64 上 `int32_t` **就是** `int`，示例文件里用 `std::is_same<std::int32_t, int>::value` 把这件事如实登记了。想真验，得在 ARM 工具链上编一次。

### 5.4 分支返回值类型不齐，编译期就红

- **现象**：`error: no type named 'type' in 'struct std::common_type<int, const char*>'`。
- **原因**：上游用 `std::common_type_t` 算出所有分支的共同返回类型。
- **怎么办**：让每条分支同类型（都返回 `const char*`、都返回 `int`、都返回同一个结构体…），或者干脆全部返回 `void` 走语句形式。

### 5.5 模式表达式别存下来跨语句复用

- **现象（实测）**：`auto p = matchit::_ >= 1;` 存起来，之后再用 —— GCC 15 `-O2 -Wall -Wextra` 报 `warning: '<anonymous>' is used uninitialized [-Wuninitialized]`（指向 matchit.h 里 `and_` 的实现），行为不可依赖。
- **原因**：比较/算术模式（`_ >= 1`、`_ % 2 == 0`）是用**按引用捕获** `[&]` 的 lambda 包出来的 `Meet`，捕获的是那个字面量临时量的引用；语句一结束，字面量就没了。这是上游的写法，我们没有改它。
- **怎么办**：两种都行 —— ① 写在 match 表达式里（本手册所有示例都是这么写的）；② 只存**按值持有**的模式：`or_(1, 2, 3)`、`meet(仿函数{})` 是安全的（示例文件里 `value_held_patterns_can_be_stored()` 断言过）。

### 5.6 `meet(...)` 只吃类类型

- **现象**：`meet(is_even)`（裸函数名）→ `error: base type 'bool(int)' fails to be a struct or class type`；
  函数指针 `meet(&is_even)` 同理（`base type 'bool (*)(int)' …`）。
- **原因**：上游 `template <typename Pred> class Meet : public Pred` —— 得能当基类。
- **怎么办**：传 lambda，或写个小仿函数（`struct is_even_fn { bool operator()(int) const; };`）。同理，`Id<T>` 的 `T` 要有 `operator==`（`IdTraits<T>::equal` 就是 `lhs == rhs`）。

### 5.7 省掉兜底 = 把"没匹配上"变成异常 / 默认值

- **现象**：运行时抛 `std::logic_error`（无异常构建：`assert` 或返回默认值）。
- **原因**：3.11 那张表 —— 表达式形式下"一条都没命中"是**错误**；只有全 `void` 的语句形式才算合法。
- **怎么办**：分支表最后永远加一条 `pattern | _`。本仓库的 `match_segment` 就是这么写的。

### 5.8 体积与代价

Cortex-M4 / `-Os` / 整程序（`run_check.ps1 -Size` 实测，出自 [EFMT-使用手册.md](../EFMT-使用手册.md) 8.2c 与 [matchit/README.md](../../matchit/README.md)）：

| 配置 | .text |
|---|---|
| 命令表 2 条（`ECLI_ENABLE_PATTERN_COMMANDS=0`，不含 matchit） | 9924 B |
| 命令表 2 条（模式匹配开，含 matchit） | **10580 B** |
| 单条 `:param` 命令（含 matchit） | 7184 B |

| 记在谁头上 | 量级 |
|---|---|
| matchit 本体（`app` + `some` + 通配那一小块） | 约 **0.6 KB**（10580 − 9924） |
| 捕获注入与 `params` 管线 | 约 0.24 KB（9924 vs 集成前的 9684） |

不用模式命令就 `-DECLI_ENABLE_PATTERN_COMMANDS=0`，一次全省回来，还少一层依赖。

### 5.9 边界：这是第三方库

| 事项 | 归谁 |
|---|---|
| 上游的功能、bug、文档 | 上游 <https://github.com/BowenFu/matchit.cpp>（Apache-2.0） |
| 本地副本里的改动 | `matchit/matchit.h` 横幅 + [PATCHES.md](../../matchit/PATCHES.md)（无异常构建、ARM 自检，共 2 处） |
| 我们**怎么用**它（段模式、捕获、排序） | `ecli/command.hpp`，不是 matchit 的一部分 |
| 上游能力但本仓库没接 | 3.12 那些（`as` / `asDsVia` / `matched` / `none` …），要用自己加检查 |

---

## ⑥ 怎么自测 / 怎么进一步验证

### 6.1 跑手册的示例（一条命令）

```powershell
Set-Location <仓库根>
g++ -std=c++17 -O2 -Wall -Wextra -Itests/include -I. tests/matchit_manual_examples.cpp -o tests/out/matchit_manual_examples.exe
./tests/out/matchit_manual_examples.exe
```

真实输出（本机 g++ 15.1.0，零 warning）：

```
0：关
1..9：开
超出 0..9
54 checks, 0 failures
```

前三行是 ② 那个例子的输出；`check` 是文件顶部 3 行脚手架（`g_checks` / `g_failures` 计数 + `FAIL …` 打印），失败时进程返回 1。

### 6.2 三种配置都该能编、能跑

| 配置 | 编译命令 | 结果 |
|---|---|---|
| 默认（模式段开） | 见 6.1 | 54 checks, 0 failures |
| 裁剪版 | 加 `-DECLI_ENABLE_PATTERN_COMMANDS=0` | 46 checks, 0 failures（断言少几条：裁剪分支代替了模式分支） |
| 无异常构建 | 加 `-fno-exceptions` | 54 checks, 0 failures（`MATCHIT_USE_EXCEPTIONS == 0` 由 `#if` 分支断言） |

### 6.3 已经挂进 `run_check.ps1`

本手册的示例文件已经在流水线里了 —— `tests/run_check.ps1` 的 ecli 段里就是这两行，跑全量时会看到 pass 名
`matchit manual examples (docs stay honest)`：

```powershell
$matchitManualExe = Join-Path $out 'matchit_manual_examples.exe'
$matchitManualArgs = @('-std=c++17') + $baseArgs + @("-I$root", (Join-Path $PSScriptRoot 'matchit_manual_examples.cpp'), '-o', $matchitManualExe)
if (Invoke-EfmtBuild 'matchit manual examples (docs stay honest)' $matchitManualArgs) {
    Invoke-EfmtRun 'matchit manual examples' $matchitManualExe
}
```

流水线只跑默认配置；6.2 的另外两种配置（`-DECLI_ENABLE_PATTERN_COMMANDS=0`、`-fno-exceptions`）手动跑 ——
各有各的用途，但没必要每次都编。**给手册加内容时，记得同步在示例文件里加一份可编译版本**，
否则这条 pass 就证明不了你新写的那段。

### 6.4 相关的现成测试（改模式段之前先看它们）

| 文件 | 管什么 |
|---|---|
| [tests/ecli_pattern_check.cpp](../../tests/ecli_pattern_check.cpp) | 模式段的全部边界：`:参数` 注入标量/字符串、`*rest` 注入容器、`*rest` 收 0 个、类型不对报 `invalid_value`、`sensor read` 赢过 `sensor *rest`、`help` 前缀命中、段数不够落回短命令、`-DECLI_ENABLE_PATTERN_COMMANDS=0` 的对照分支 |
| [tests/ecli_command_check.cpp](../../tests/ecli_command_check.cpp) | 命令表本身（分发、帮助、错误码） |
| `run_check.ps1` 里的 `ecli patterns` / `ecli patterns (embedded)` / `ecli patterns (off)` 三条 | 宿主、嵌入式、裁剪三种配置 |

```powershell
.\tests\run_check.ps1            # 全套（含上面那三个模式段用例）
```

想手敲看行为：跑 `sandbox` 进命令台，敲 `level 0` / `level 5` / `level 99`（处理函数里直接写
match 表达式，数字由模式段 `level :n` 捕获），或者 `net set mynet`（两段式子命令 + `:ssid` 捕获）。
怎么跑、还能敲什么： [SANDBOX-命令台上手指南.md](../SANDBOX-命令台上手指南.md)、
[sandbox/README.md](../../sandbox/README.md)。

### 6.5 升级到更新的上游

步骤在 [PATCHES.md](../../matchit/PATCHES.md)「重新同步上游的步骤」（取新 `include/matchit.h` → 重放改动 1 的横幅 + 6 处替换、改动 2 的一行 → 更新"冻结提交 / sha256"两栏 → 跑测试）。我们要用到的那一小块（`app` / `some` / 通配 `_` / 字符串比较）由这些测试挡住回归：

| 跑什么 | 挡什么 |
|---|---|
| `.\tests\run_check.ps1` | 全绿才算同步成功 |
| `ecli patterns` 三条（宿主 / 嵌入式 / 裁剪） | 段判定、捕获注入、排序、help 前缀 |
| tests/matchit_manual_examples.cpp（本期新增） | 手册里那批模式写法（字面量 / 比较 / `or_` / `not_` / `meet` / `app`+`some` / `Id` / `ds` / `dsVia` / `ooo` / `when`）与三条报错行为 |

另外两个"改了要重新确认"的点：① 无异常构建（`-fno-exceptions`）—— 上游会不会新增 `throw`；② 上游那段 `int32_t` 自检 —— 别把改动 2 丢了。

### 6.6 许可与合规

- matchit.cpp 是 **Apache-2.0**：许可证原文随副本放在 `matchit/LICENSE`（原样复制，别删）。
- 我们只做**最小适配、保持可 diff**：文件头保留上游版权行，改动集中且清单化（PATCHES.md），重新同步时照单重放即可。
- 上游仓库地址写进了 `matchit/README.md` 与 `matchit/matchit.h` 的横幅 —— 分发时这些别丢。
