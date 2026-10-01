# 标准库

<!-- grammar_name: stdlib, search_name: 标准库,Option,Iterator,String,math,char,Range,import -->

标准库是纯 Lis 源码（`Source/Std/*.lis`），构建时复制到 `Build/Binaries/lstdlib`，
作为可导入的模块提供：**不再自动预加载**，用到的模块要显式 `impt`
（见[声明](./declarations.md)的模块与导入节）。类型名大写开头（2026-08 命名规范）。

```lis
impt io { print, println, read_i32, read_line };   // 输入输出（2026-09-25）
impt fmt { Display };                              // 想给自己的类型实现打印时才需要
impt math { max, abs };
impt option { Option, unwrap_or };
impt result { Result, is_ok, is_err };
impt string { String };
impt vec { Vec };            // v[i] / v[i] = x 额外需要 Index / IndexMut，见下
```

十四个模块：`fmt`（Display trait）、`io`（print/println/flush + read_* 与 try_read_*）、
`drop`（Drop trait）、`option`（Option<T>）、`result`（Result<T, E> + `?` 传播协议）、
`iterator`（Iterator/Range/for 协议）、`math`（Numeric/算子 trait + 数值函数）、`chars`
（字符分类）、`string`（String 堆字符串）、`vec`（Vec<T> 堆数组 + Index/IndexMut trait）、
`hash`（Hash trait）、`map`/`set`（红黑树有序容器）、`hashmap`（链式哈希表 + HashSet）——见下面的容器章节。
模块间依赖已显式声明（iterator 导入 option；string 导入 drop 与 option；
vec 导入 math/option/iterator/drop；io 导入 fmt/string/option）—— 只需导入你直接使用的模块。

> `unwrap_or` 在 `option` 与 `result` 里**各有一个**。两者都做选择性导入会触发
> 「selective import conflicts with an existing name」——这是既有的冲突规则，不是 bug。
> 需要同时用两个时，用整模块导入 + 限定名：`impt result;` 然后 `result::unwrap_or(...)`。

## io —— 输入输出（2026-09-25）

`print_*` / `read_*` 那一族编译器内置函数**已全部退役**：io 是普通 Lis 模块，编译器只提供
字节流原语（`__read_byte`/`__write`/`__flush`，stdlib 专用）与原语 `Display` 的下降。
设计说明见[内置函数](./builtins.md)，这里给 API。

| 输出 | 说明 |
|---|---|
| `print<T: Display>(x: T)` | 写 x，无换行、不 flush |
| `println()` | 写换行**并 flush**（本语言的 `std::endl`，交互题用） |
| `flush()` | 只 flush（不加换行） |

`print(x); println();` 是「值 + 换行」的合并写法：语言没有重载，零参 `println` 与一参形式
无法同名共存，而 flush 正是零参形式的独立语义。日常换行写 `print("\n")`（只入缓冲）。

| 输入 | 说明 |
|---|---|
| `read_i32() -> i32` / `read_i64() -> i64` / `read_f64() -> f64` | 跳过空白，消费**一个 token**（`"3 4"` 同一行可用；支持正负号与 `1e-9` 指数）；EOF 时返回 0 |
| `read_word() -> String` | 下一个空白分隔的词 |
| `read_line() -> String` | 当前行的**剩余部分**（保留空格，去掉 LF/CRLF），长度不限；EOF/行尾返回空串 |
| `read_rest() -> String` | 剩下的一切（含换行） |
| `try_read_i32/i64/f64/word/line() -> Option<...>` | 只有**已经**无输入才返回 `None`（`"0"` 是 `Some(0)`） |

`String` 的 `len()` 是字节数，所以"这一行多长"写 `read_line().len()`。

## fmt —— Display

```lis
trait Display { fn show(self: &Self); }
```

原语（i8/i16/i32/i64/f32/f64/bool/char）与 `&i8`（C 串）由编译器播种；`String` 在 io 里实现；
用户类型自己实现即可被 `print`/`println` 接受：

```lis
impt fmt { Display };
impt io  { print, println };
impl Display for Point { fn show(self: &Self) { print(self.x); print(','); print(self.y); } }
print(Point { x: 1, y: 2 });   // 1,2
```

注意 `print` **按值**收参数（`fn print<T: Display>(x: T)`），所以打印一个非 Copy 值会**移动**
它：`print(s); print(s);` 对 `String` 是 use-after-move。要重复打印就打印借用出来的 C 串：
`print(s.to_cstr())`（`&i8` 是 Copy）。

## math 的浮点函数（2026-09-26）

两类实现：**手写**的（`sqrt` 归一化 + 牛顿、`cos/sin/tan` 区间归约 + 12 项泰勒、取整族）与
**libm 绑定**的（`exp/log/pow/...`：`extern "C"` + `#[link_name]`，见 [FFI](./ffi.md)）。
两者精度同级（相对误差 ~1e-16）；手写的那批同时是绑定实现的对拍 oracle
（`RuntimeTest.MathLibmMatchesTaylor`）。

| 函数 | 说明 |
|---|---|
| `PI` / `TWO_PI` / `HALF_PI` | 模块级 f64 常量 |
| `sqrt(x)` | 平方根（`x < 0` → panic） |
| `floor/ceil/round/trunc/fract(x)` | 取整族（`round` 半数远离零；定义域 `|x| < 2^63`） |
| `cos/sin/tan(x)` | 三角函数（弧度，手写泰勒展开） |
| `exp/log/log2/log10(x)` | 指数与对数（libm） |
| `pow(x, y)` | 幂（libm） |
| `atan/atan2(y, x)/asin/acos(x)` | 反三角（弧度，libm） |
| `sinh/cosh/tanh(x)` | 双曲函数（libm） |

```lis
impt math { sqrt, floor, PI };
print(floor(sqrt(2.0) * 100.0));   // 141.000000
```

## `Copy`（`impl Copy for S {}`）

`math` 里的 `Copy` 是**真的语义标记**（2026-09-26）：实现它的类型获得原语般的复制语义，
可以作为数组元素、可以按值反复使用。字段必须全部 Copy，且不能同时实现 `Drop`。
见[类型系统](./types.md)。

## 所有权转移与 FFI 模块（2026-09-26）

| API | 说明 |
|---|---|
| `String::into_raw(&mut self) -> *mut i8` | 把缓冲区交给 C；源 String 变成合法的空串（Drop 仍安全） |
| `String::from_raw(data, len, cap) -> String` | 收养一个缓冲区（`cap` 含 NUL） |
| `Vec<T>::into_raw(&mut self) -> *mut T` | 交出元素缓冲区；源 Vec 变成合法的空 Vec |
| `Vec<T>::from_raw(data, len, cap) -> Vec<T>` | 收养；存活区间 `[0, len)`，析构按 T 自己的析构函数释放 |
| `impt ffi` 的 `lis_alloc(i32) -> *mut i8` / `lis_free(*mut i8)` | 给 C 用的分配器对（导出符号） |

细节与契约见 [FFI](./ffi.md)。

## Vec 的容量 API

| 方法 | 说明 |
|---|---|
| `Vec::from_elem(v, n)` | n 个 v 的副本（`vec![v; n]`） |
| `resize(n, v)` | 长度调整为 n：变长用 v 填充，变短截断（元素须 Copy） |
| `len()` / `cap()` / `is_empty()` / `clear()` | 现有容量/长度 API |

`with_capacity` 仍缺失：扩容需要**类型级**的元素大小，而 `__sizeof` 需要一个 T 的值；
`from_elem` 已覆盖"开一块 n 元素缓冲"的实际需要。

## Map / Set —— 红黑树有序容器（2026-09-26）

```lis
impt map { Map };            // Set 在 set.lis：impt set { Set };
impt option { Option, is_none };

let mut m = Map<i32, String>::new();
m.insert(2, String::from_lit("two"));
m.insert(1, String::from_lit("one"));
m.insert(2, String::from_lit("TWO"));      // 替换，返回旧的 Option<String>
for e in m { print(*e.key); }              // 12 —— 迭代按**键升序**
```

| API | 说明 |
|---|---|
| `Map<K, V>::new()` | 空表（不预分配） |
| `insert(k, v) -> Option<V>` | 插入或**替换**；返回被替换掉的旧值 |
| `get_ref(&k) -> Option<&V>` | 借出值（非 Copy 值的唯一只读通道） |
| `get_mut(&mut self, &k) -> Option<&mut V>` | 借出可写 |
| `remove(&k) -> Option<V>` | 摘除并**把值交出来**（取走非 Copy 值的正道） |
| `contains_key(&k)` / `len()` / `is_empty()` / `clear()` | |
| `iter() -> MapIter` | 中序遍历（`for e in m` 也走它；`e: EntryRef<K,V>`，字段 `key`/`value` 是借用） |
| `check_invariants() -> bool` | 红黑性质自检（**含子树大小**，测试/调试用） |
| `count_less(&k) -> i32` | 严格小于 k 的**键**个数（O(log n)） |
| `rank(&k) -> i32` | 排名 = count_less + 1（重复键取第一个的排名） |
| `kth(i) -> Option<&K>` | 第 i 小（1-based） |
| `pred(&k)` / `succ(&k) -> Option<&K>` | 严格前驱 / 严格后继 |

- **平衡树，不是哈希**：迭代有序，`find/insert/remove` 都是 O(log n)。
- **键的要求**：`K: Ord + PartialOrd`（编译器给 i32/String 播种；用户类型写
  `impl Ord for T { }` + `impl PartialOrd for T`）。缺了它**在实例化处**报
  「type 'K' does not implement trait ... required by 'Map'」。
- **没有按值 get**：按值交出只有 Copy 元素才安全，而这个约束**无法表达**（方法/自由函数上的
  bound 到不了 arena 的元素类型）。Copy 值用 `*m.get_ref(&k).unwrap()`，非 Copy 值用
  `get_ref` / `remove`。
- `get_ref` 返回的借用**被借用检查追踪**（2026-10-01）：`insert`（扩容）/ `remove` 与它冲突即 E4001。
- **键唯一：Map 不是多重集**。序统计数的是**键**。要「多重集 + 排名/第 k 小」，用竞赛标准技巧 ——
  每次出现给唯一 id，键取 `(值, id)` 按字典序比较：

  ```lis
  struct Pair { pub v: i32, pub id: i32 }
  impl Copy for Pair {}
  impl Ord for Pair {}
  impl PartialOrd for Pair { fn lt(self: &Pair, other: &Pair) -> bool {
      if self.v != other.v { ret self.v < other.v; } ret self.id < other.id; } /* gt/le/ge 同理 */ }

  let lo = Pair { v: x, id: 0 - 1 };        // 小于任何 (x, id>=0)
  let hi = Pair { v: x, id: 2147483647 };   // 大于任何 (x, id)
  m.count_less(&lo) + 1                     // x 的排名（小于 x 的元素个数 + 1）
  m.kth(k).unwrap().v                       // 第 k 小的元素
  m.pred(&lo).unwrap().v                    // 前驱
  m.succ(&hi).unwrap().v                    // 后继
  let victim = *m.kth(m.count_less(&lo) + 1).unwrap(); m.remove(&victim);  // 删掉一个
  ```

  实测 n = 5e5 的五种对抗数据（单调插入 / 锯齿 / 海量重复 / 随机混合 / 反复删除）与 GNU pbds
  序统计树**逐字节一致**，用时 **0.46x ` 0.66x**（Lis 更快）；完整题解见 `Examples/balanced_tree.lis`。

`Set<K>` 是同一棵树上的薄包装（值槽放 bool）：`insert(k) -> bool`（true = 之前不在）、
`contains` / `remove(&k) -> bool` / `len` / `is_empty` / `clear` / `iter` / `check_invariants`。

## HashMap / HashSet —— 链式哈希表（2026-09-26）

```lis
impt hashmap { HashMap, HashSet };   // HashSet 也在 hashmap.lis 里
let mut h = HashMap<String, i32>::new();
h.insert(String::from_lit("k"), 1);
let v = *h.get_ref(&String::from_lit("k")).unwrap();
```

API 与 Map 对齐（`new/insert/get_ref/get_mut/remove/contains_key/len/is_empty/clear/iter`），
但**迭代顺序是桶序、未定义**（不要依赖它）。键的要求是 `K: Hash + PartialEq`
（`hash.lis` 的 `Hash` trait + 相等比较）；容量按 2 的幂增长，桶号 = `hash & (cap - 1)`。

## Drop

```lis
trait Drop { fn drop(self); }
```

见[所有权与移动](./ownership.md)。

## Option

```lis
enum Option<T> { Some(T), None }
```

| 函数 | 签名 |
|---|---|
| `is_some<T>(o: Option<T>) -> bool` | |
| `is_none<T>(o: Option<T>) -> bool` | |
| `unwrap_or<T>(o: Option<T>, dflt: T) -> T` | Some 取载荷，None 取默认 |
| `and<T>(a: Option<T>, b: Option<T>) -> Option<T>` | Some 时取 b（丢弃 a 载荷） |
| `or<T>(a: Option<T>, b: Option<T>) -> Option<T>` | None 时取 b（保留载荷） |

方法（`impl Option`，按值消费接收者）：

| 方法 | 签名 | 说明 |
|---|---|---|
| `o.unwrap()` | `-> T` | 取 `Some` 载荷；`None` 时 `panic("called unwrap on a None value")` |
| `o.expect(msg: &i8)` | `-> T` | 同上，但用调用者传入的消息 panic |

两者都**消费** option（按值 `self`），并且只可能有两种结局：拿到载荷，或进程终止。
它们能实现的前提是 `panic` 的返回类型是 `never` —— `None` 臂不产生值，方法因此仍然
类型检查为返回 `T`。需要回退值时用 `unwrap_or`。

## Result

```lis
enum Result<T, E> { Ok(T), Err(E) }
```

| 函数 / 方法 | 签名 | 说明 |
|---|---|---|
| `is_ok<T, E>(r)` | `-> bool` | `Ok` 为真 |
| `is_err<T, E>(r)` | `-> bool` | `Err` 为真 |
| `unwrap_or<T, E>(r, dflt)` | `-> T` | `Ok` 取载荷，`Err` 取默认（错误值被丢弃） |
| `r.unwrap()` | `-> T` | 取 `Ok` 载荷；`Err` 时 `panic("called unwrap on an Err value")` |
| `r.expect(msg)` | `-> T` | 同上，用调用者消息 panic |

`Result` 是[后缀 `?` 运算符](./expression.md)的载体：`expr?` 在 `Ok` 时产出载荷，在 `Err` 时
立刻 `ret Result::Err(e)`。`?` 要求**外层函数返回 Result，且错误类型与操作数兼容**（不做任何
错误转换）。

## Iterator 与 Range

```lis
trait Iterator<T> { fn next(self: &mut Self) -> Option<T>; }
struct Range { pub start: i32, pub end: i32, pub current: i32 }
```

- `range(start, end)`：半开区间 `[start, end)`。
- 泛型助手（`T: Iterator<i32>`）：`sum` `count` `first` `last` `nth` `product`。
- `Vec<T>` 的**借用迭代器**（2026-09-19）：`struct VecIter<T> { pub src: &Vec<T>, pub pos: i32 }`
  与 `impl<T> Iterator<&T> for VecIter<T>`；`v.iter()` 返回它。注意 `Iterator<T>` 的 `T` 是
  **迭代步交出的类型**，所以这里是 `Iterator<&T>`——每步给一个元素引用。
- `for x in iterable { }` 的协议（借用/`move`/右值三种形态）：见[语句](./statements.md)的 for 节。

## String

```lis
struct String
{
    data: *mut i8,   // 堆缓冲(C 字符串,null 结尾)。私有
    len: i32,        // 私有
    cap: i32         // 私有
}
```

| 方法 | 签名 | 说明 |
|---|---|---|
| `new()` | `-> String` | 空串，容量 16 |
| `from_lit(s: &i8)` | `-> String` | 拷贝 C 字面量到堆 |
| `to_cstr(self: &String)` | `-> &i8` | **返回借用接收者的引用**——借用绑定到 owner，move/`push_char` 与它冲突即报错 |
| `push_char(self: &mut String, c: char)` | | 满时翻倍扩容；保持 null 结尾 |
| `push_str(self: &mut String, other: &i8)` | | 追加 C 字符串 |
| `index(self: &String, i: i32)` | `-> Option<char>` | 越界返回 None（两端检查） |
| `is_empty(self: &String)` | `-> bool` | |
| `len(self: &String)` | `-> i32` | 字节数（不含结尾 null） |
| `cap(self: &String)` | `-> i32` | 当前容量（字节） |

- 拥有堆缓冲，**永不 Copy**；`impl Drop` 释放缓冲。
- 三个字段都是**私有**的：`data`/`len`/`cap` 必须互相一致（这是类型的不变量），
  只有 `String` 自己的方法能保证；读长度/容量用 `len()` / `cap()`。
  字段可见性由编译器强制（E3015），不是约定。
- String 是**字节串**：中文按 UTF-8 字节计数（`len` 是字节数）。
- `to_cstr` 的返回借用编译器不追踪——调用方必须保证 owner 存活。
- OOM 不检查。

## Vec

```lis
trait Index<T>    { fn at(self: &Self, i: i32) -> T; }
trait IndexMut<T> { fn set(self: &mut Self, i: i32, v: T); }
struct Vec<T> { data: *mut T, len: i32, cap: i32, cursor: i32 }   // 字段全私有
```

**元素可以是任意类型**（2026-09-19 起）：Copy 元素能被按值读出（`v[i]`、`get`），
其余元素只能**移动**进出容器（`push`/`pop`/`remove`/`insert`/`for`）或被**出借**
（`at_ref`/`at_mut`）。容器自己拥有的元素会被析构——`clear` 与 `Drop` ——**每个恰好一次**。

| 方法 | 是否限 Copy | 说明 |
|---|---|---|
| `Vec<i32>::new()` | — | 空表，**不分配**（`malloc(0)`），首次 push 才要 4 个元素的空间 |
| `len` / `cap` / `is_empty` | — | **仍拥有的**元素数 / 已分配容量 / 是否为空 |
| `push(self: &mut Vec, v: T)` | — | 满时**翻倍**扩容（0 → 4 → 8 → ...）；元素被移动进来 |
| `pop(self: &mut Vec)` | — | `-> Option<T>`：取走最后一个**仍拥有**的元素 |
| `remove(self: &mut Vec, i: i32)` | — | `-> Option<T>`：删除并左移；越界 `None` |
| `insert(self: &mut Vec, i: i32, v: T)` | — | 越界 **panic**（`i == len` 等于追加） |
| `clear(self: &mut Vec)` | — | **析构**所有仍拥有的元素，长度归零，**保留容量** |
| `at_ref(self: &Vec, i: i32)` | — | `-> &T`：越界 panic；**出借**元素（见下面的警告） |
| `at_mut(self: &mut Vec, i: i32)` | — | `-> &mut T`：同上，可写穿 |
| `iter(self: &Vec)` | — | `-> VecIter<T>`：**借用**迭代器，每步交出 `&T` |
| `iter_mut(self: &mut Vec)` | — | `-> VecIterMut<T>`：**可变借用**迭代器，每步交出 `&mut T` |
| `for e in v` | — | **借用** v（走 `iter()`），`e` 是 `&T`；循环结束后 v 仍可用 |
| `for e in move v` | — | **消费** v，按**正序**逐个交出元素（Copy 复制、非 Copy 移动） |
| `get(self: &Vec, i: i32)` | ✔ | `-> Option<T>`：越界 `None`（**检查版**） |
| `v[i]` | ✔ | `Index::at`；越界 `panic("Vec index out of bounds")`（与数组一致） |
| `v[i] = x` | ✔ | `IndexMut::set`；越界 `panic` |

- **构造要写类型实参**：`Vec<i32>::new()`。本语言没有期望类型推断，`let v: Vec<i32> = Vec::new();`
  里的标注**不能**回推 T（与 `let x: Option<i32> = Option::None;` 同一条规则）。
- **按值交出元素的 API 限 Copy**：`v[i]`/`v[i] = x`/`get` 把元素**交出去**，非 Copy 元素
  会变成「容器仍然拥有、调用方也拥有」→ 双重析构。它们因此写成条件实现
  `impl<T: Copy> ... for Vec<T>`，`Vec<String>[0]` 在编译期报
  `type 'string$String' does not implement trait 'Copy' required by 'vec$Vec::at'`
  （impl 上写的泛型约束现在真的会被检查）。非 Copy 容器请用 `at_ref`/`at_mut` 或
  `pop`/`remove`/`for`。
- **`at_ref`/`at_mut` 的借用被追踪**（2026-10-01）：拿到引用后，这个 Vec 在它的最后一次
  使用之前保持借用，`push`/`insert`（会 `__free` 旧缓冲）与它冲突即 E4001。
- **`for` 的正序与游标**：`next()` 把元素移出，Vec 内部记下「已移出的前缀」；容器只拥有
  该前缀之后的元素，所以 `len()`、下标、`pop`/`remove` 都相对**存活区间**解释，
  `Drop`/`clear` 也只析构这一段——被移出的元素不会被析构第二次。
- **遍历**（2026-09-19）：`for e in v` 借用（`e: &T`，循环结束后容器可用，但循环体内
  不能改动容器——扩容会释放迭代器指着的缓冲，报 E4001）；`for e in move v` 消费（`e: T`）。
- **`iter_mut()`（2026-09-19 同日落地）**：`VecIterMut<T>` 每步交出 `&mut T`，可以就地改元素：
  `for r in v.iter_mut() { *r = *r + 1; }`。这是把借用检查下放到 MIR（按 CFG 数据流算活跃
  区间）之后才成立的 API——在此之前「从 `self` 派生并返回 `&mut`」会被 E4001/E3002 误拒，
  文档只能建议写 `while i < v.len() { v[i] = ...; }` 下标循环。
- 还没有 `with_capacity` / `reserve`：扩容需要元素大小，而 `__sizeof` 要一个 T 的**值**，
  所以大小由 `push`/`insert` 的实参带进来。

## math

- marker/算子 trait：`Numeric` `Integer` `Add` `Sub` `Mul` `Div` `Rem` `PartialEq`
  `PartialOrd` `BitAnd` `BitOr` `BitXor` `Shl` `Shr`（见[运算符](./operators.md)）。
- `Copy`（2026-09-19）：**空 marker trait**，声明「这个类型可以按位复制」。泛型容器用它
  写约束（`impl<T: Copy> Index<T> for Vec<T>`），于是泛型体里读一个 `T` 字段不再被判成
  「从引用后移动」（E3017），并按值交出元素也只对 Copy 类型开放。原语（整数/浮点/char/bool）
  在语义分析里被**播种**为自动实现 `Copy`，用户类型要显式 `impl Copy for X { }`。
- 函数：`min<T: Numeric>` `max<T: Numeric>` `clamp<T: Numeric>`、`abs(i32)` `fabs(f64)`
  （`abs`/`fabs` 写作 `0 - x`，一元 `-` 落地后实现未变）、`gcd` `lcm` `ipow` `is_even` `is_odd` `sign`
  `deg_to_rad` `rad_to_deg` `lerp`。

## chars

`is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int`——
只用比较与 `as i32`（char 无算术）。

## 作用域与撞名

模块系统（2026-08-13）根治了旧的「单作用域合并」限制：标准库各模块有独立命名空间，
用户全局名不再与标准库内部名撞。若用户**显式选择性导入**的名字与自己的定义同名，
会报「selective import conflicts with an existing name」—— 这是冲突提示而非误报
（自己定义的名字优先，去掉该 import 即可）。
