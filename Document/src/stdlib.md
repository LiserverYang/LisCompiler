# 标准库

<!-- grammar_name: stdlib, search_name: 标准库,Option,Iterator,String,math,char,Range -->

标准库是纯 Lis 源码（`Source/Std/*.lis`），编译时**自动预加载**（无 import 语句），
构建时复制到 `Build/Binaries/lstdlib`。类型名大写开头（2026-08 命名规范）。

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

`unwrap`/`expect` **故意不提供**（需要 panic 机制，见[已知限制](./limitations.md)）。

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
    pub data: &mut i8,   // 堆缓冲(C 字符串,null 结尾)
    pub len: i32,
    pub cap: i32
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

- 拥有堆缓冲，**永不 Copy**；`impl Drop` 释放缓冲。
- String 是**字节串**：中文按 UTF-8 字节计数（`len` 是字节数）。
- `to_cstr` 的返回借用编译器不追踪——调用方必须保证 owner 存活。
- OOM 不检查。

## math

- marker/算子 trait：`Numeric` `Integer` `Add` `Sub` `Mul` `Div` `Rem` `PartialEq`
  `PartialOrd` `BitAnd` `BitOr` `BitXor` `Shl` `Shr`（见[运算符](./operators.md)）。
- 函数：`min<T: Numeric>` `max<T: Numeric>` `clamp<T: Numeric>`、`abs(i32)` `fabs(f64)`
  （无一元负号，`0 - x` 实现）、`gcd` `lcm` `ipow` `is_even` `is_odd` `sign`
  `deg_to_rad` `rad_to_deg` `lerp`。

## char

`is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int`——
只用比较与 `as i32`（char 无算术）。

## 作用域限制

标准库与主文件合并进同一作用域：用户全局名若与标准库内部名（局部/参数）撞名会误报
「already exists」。这是已知设计限制（见[已知限制](./limitations.md)）。
