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
```

七个模块：`drop`（Drop trait）、`option`（Option<T>）、`result`（Result<T, E> + `?` 传播协议）、
`iterator`（Iterator/Range/for 协议）、`math`（Numeric/算子 trait + 数值函数）、`chars`
（字符分类）、`string`（String 堆字符串）。模块间依赖已显式声明（iterator 导入 option；
string 导入 drop 与 option）—— 只需导入你直接使用的模块。

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
- `for x in iterable { }` 的协议：见[语句](./statements.md)的 for 节。

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

## math

- marker/算子 trait：`Numeric` `Integer` `Add` `Sub` `Mul` `Div` `Rem` `PartialEq`
  `PartialOrd` `BitAnd` `BitOr` `BitXor` `Shl` `Shr`（见[运算符](./operators.md)）。
- 函数：`min<T: Numeric>` `max<T: Numeric>` `clamp<T: Numeric>`、`abs(i32)` `fabs(f64)`
  （无一元负号，`0 - x` 实现）、`gcd` `lcm` `ipow` `is_even` `is_odd` `sign`
  `deg_to_rad` `rad_to_deg` `lerp`。

## chars

`is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int`——
只用比较与 `as i32`（char 无算术）。

## 作用域与撞名

模块系统（2026-08-13）根治了旧的「单作用域合并」限制：标准库各模块有独立命名空间，
用户全局名不再与标准库内部名撞。若用户**显式选择性导入**的名字与自己的定义同名，
会报「selective import conflicts with an existing name」—— 这是冲突提示而非误报
（自己定义的名字优先，去掉该 import 即可）。
