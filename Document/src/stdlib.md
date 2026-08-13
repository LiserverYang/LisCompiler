# 标准库

<!-- grammar_name: stdlib, search_name: 标准库,Option,Iterator,String,math,char,Range,import -->

标准库是纯 Lis 源码（`Source/Std/*.lis`），构建时复制到 `Build/Binaries/lstdlib`，
作为可导入的模块提供：**不再自动预加载**，用到的模块要显式 `impt`
（见[声明](./declarations.md)的模块与导入节）。类型名大写开头（2026-08 命名规范）。

```lis
impt math { max, abs };
impt option { Option, unwrap_or };
impt string { String };
```

六个模块：`drop`（Drop trait）、`option`（Option<T>）、`iterator`（Iterator/Range/for
协议）、`math`（Numeric/算子 trait + 数值函数）、`chars`（字符分类）、`string`
（String 堆字符串）。模块间依赖已显式声明（iterator 导入 option；string 导入
drop 与 option）—— 只需导入你直接使用的模块。

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

## chars

`is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int`——
只用比较与 `as i32`（char 无算术）。

## 作用域与撞名

模块系统（2026-08-13）根治了旧的「单作用域合并」限制：标准库各模块有独立命名空间，
用户全局名不再与标准库内部名撞。若用户**显式选择性导入**的名字与自己的定义同名，
会报「selective import conflicts with an existing name」—— 这是冲突提示而非误报
（自己定义的名字优先，去掉该 import 即可）。
