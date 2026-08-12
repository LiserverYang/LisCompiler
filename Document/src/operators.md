# 运算符与类型转换

<!-- grammar_name: operators, search_name: 运算符,运算符重载,类型转换,cast -->

## 运算符一览

| 类别 | 运算符 | 原语操作数 | 结构体重载 |
|---|---|---|---|
| 算术 | `+ - * / %` | 整数/浮点 | `Add` `Sub` `Mul` `Div` `Rem` trait |
| 比较 | `== !=` | 同类型（含 bool/char） | `PartialEq` |
| 比较 | `< > <= >=` | 整数/浮点/char | `PartialOrd` |
| 逻辑 | `&& \|\|` | **仅 bool**，不可重载 | — |
| 位 | `& \|` | **仅整数** | `BitAnd` `BitOr` |
| 位（无 token） | `^ << >>` | 仅方法 `bitxor`/`shl`/`shr` | 同上 |

> `^`、`<<`、`>>` **没有运算符 token**（词法不支持），只能作为方法调用
> （`x.bitxor(y)`）或泛型约束使用。逻辑 `&&`/`||` 永远不可重载。

运算规则：

- 两操作数类型必须相同（无隐式转换）。
- 逻辑运算要求 `bool`；位运算要求整数；算术/比较要求整数或浮点。
- 比较与逻辑结果 `bool`；算术/位结果与操作数同类型。
- `char` 运行时是 i32：可参与比较；无 char 算术（需 `as i32`）。

## 运算符重载（结构体）

trait 方法签名（标准库 `math.lis` 声明）：

```lis
trait Add { fn add(self, other: Self) -> Self; }
trait PartialEq { fn eq(self, other: Self) -> bool; fn ne(self, other: Self) -> bool; }
trait PartialOrd { fn lt(self, other: Self) -> bool; /* gt le ge */ }
trait BitAnd { fn bitand(self, other: Self) -> Self; }   // 以及 BitOr BitXor Shl Shr
```

```lis
impl Add for Vec2
{
    fn add(self, other: Self) -> Vec2 { ret Vec2 { x: self.x + other.x, y: self.y + other.y }; }
}
let c = a + b;    // 改写为 <Vec2>::add(a, b)
```

- 结构体操作数实现了对应 trait → 运算改写为方法调用（按值 self/other 消费两操作数）。
- 方法签名必须 `2` 个参数（self, other），操作数类型必须匹配。
- 泛型操作数：`T: Add` 约束下 `a + b` 解析为占位 `<T>::add`，单态化时分流
  （结构体 → 方法；原语 → 回退二元运算，见[泛型](./generics.md)）。

## 原语自动实现（播种）

trait 声明时原语自动播种（stdlib 声明这些 trait 即生效）：

| trait | 自动实现于 |
|---|---|
| `Numeric` / `Integer`（marker） | int + float(+ char for Numeric)/ int only |
| `Add`–`Rem`、`PartialEq`/`PartialOrd` | 整数 + 浮点(+ char/bool for 比较) |
| `BitAnd`–`Shr` | 仅整数 |

`bool` 刻意不实现 `Numeric`（`true + false` 会对 i1 做加法）。结构体实现
`Numeric`/`Integer` 被拒绝（marker 原语专属）。

## 类型转换（as）

<grammar>
cast_expression = expression "as" type
</grammar>

| 来源 | 目标 | 规则 |
|---|---|---|
| 整数（i8–i64） | 整数/浮点/char | 仅目标宽度 ≥ 来源（**窄化报错**，可用 `#[i_know]` 降为警告） |
| `f32` | `f64` | 仅此方向 |
| `f64` | — | 不可转换 |
| `bool`/`char` | 整数 | 仅此方向 |
| `void` | — | 不可转换 |
| 同类型 | — | 「useless cast」info（非致命） |

要点：

- `char` 运行时是 i32，故 `i8/i16/i32 → char` 是加宽/恒等（`s.data[i] as char`）。
  `i64 → char` 是窄化（截断），报错。
- 整数窄化按**显式位宽**判定（`integerBitWidth()`），不依赖枚举声明顺序。
- 无属性窄化是硬错误：`let t: i32 = big as i32;`（`big: i64`）→
  「cannot cast integer to a smaller integer type」。
- `#[i_know = "..."]` 语句属性把该语句内的窄化错误降级为警告
  （见[表达式](./expression.md)的 `#[i_know]` 节）。

## 一元运算符（未实现）

前置 `-`（取反）、`!`（逻辑非）、`~`（按位非）是**设计语法，尚未实现**。
当前负数写作 `0 - x`。
