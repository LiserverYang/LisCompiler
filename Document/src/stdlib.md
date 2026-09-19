# 标准库

<!-- grammar_name: stdlib, search_name: 标准库,Option,Iterator,String,math,char,Range,import -->

标准库是纯 Lis 源码（`Source/Std/*.lis`），构建时复制到 `Build/Binaries/lstdlib`，
作为可导入的模块提供：**不再自动预加载**，用到的模块要显式 `impt`
（见[声明](./declarations.md)的模块与导入节）。类型名大写开头（2026-08 命名规范）。

```lis
impt math { max, abs };
impt option { Option, unwrap_or };
impt result { Result, is_ok, is_err };
impt string { String };
impt vec { Vec };            // v[i] / v[i] = x 额外需要 Index / IndexMut，见下
```

八个模块：`drop`（Drop trait）、`option`（Option<T>）、`result`（Result<T, E> + `?` 传播协议）、
`iterator`（Iterator/Range/for 协议）、`math`（Numeric/算子 trait + 数值函数）、`chars`
（字符分类）、`string`（String 堆字符串）、`vec`（Vec<T> 堆数组 + Index/IndexMut trait）。
模块间依赖已显式声明（iterator 导入 option；string 导入 drop 与 option；
vec 导入 math/option/iterator/drop）—— 只需导入你直接使用的模块。

> `unwrap_or` 在 `option` 与 `result` 里**各有一个**。两者都做选择性导入会触发
> 「selective import conflicts with an existing name」——这是既有的冲突规则，不是 bug。
> 需要同时用两个时，用整模块导入 + 限定名：`impt result;` 然后 `result::unwrap_or(...)`。

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
| `to_cstr(self: &String)` | `-> &i8` | **返回借用接收者的引用**——owner 被 move/drop 后悬垂 |
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
- **`at_ref`/`at_mut` 的借用不被追踪**：与 `String::to_cstr` 同一类妥协。拿到引用后
  必须保证这个 Vec 存活且**没有被扩容**（`push`/`insert` 会 `__free` 旧缓冲），否则悬垂。
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
