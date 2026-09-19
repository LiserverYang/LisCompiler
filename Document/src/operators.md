# 运算符与类型转换

<!-- grammar_name: operators, search_name: 运算符,运算符重载,类型转换,cast -->

## 运算符一览

| 类别 | 运算符 | 原语操作数 | 结构体重载 |
|---|---|---|---|
| 一元（2026-09-19） | `-` `!` `~` `+` | 整数/浮点（`-`）、仅 bool（`!`）、仅整数（`~`）、`+x` 被擦除 | 暂无（`Neg`/`Not` 未开放） |
| 算术 | `+ - * / %` | 整数/浮点 | `Add` `Sub` `Mul` `Div` `Rem` trait |
| 比较 | `== !=` | 同类型（含 bool/char） | `PartialEq` |
| 比较 | `< > <= >=` | 整数/浮点/char | `PartialOrd` |
| 逻辑 | `&& \|\|` | **仅 bool**，不可重载 | — |
| 位 | `& \|` | **仅整数** | `BitAnd` `BitOr` |
| 位（无 token） | `^ << >>` | 仅方法 `bitxor`/`shl`/`shr` | 同上 |
| 复合赋值（2026-09-19） | `+= -= *= /= %=` | 同左侧二元运算符 | 同左（暂不支持重载类型，见下） |

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
// 比较类 trait 用引用接收者（2026-09-19）：比较不应消费被比较的值。
trait PartialEq { fn eq(self: &Self, other: &Self) -> bool; fn ne(self: &Self, other: &Self) -> bool; }
trait PartialOrd { fn lt(self: &Self, other: &Self) -> bool; /* gt le ge */ }
trait BitAnd { fn bitand(self, other: Self) -> Self; }   // 以及 BitOr BitXor Shl Shr
```

```lis
impl Add for Vec2
{
    fn add(self, other: Self) -> Vec2 { ret Vec2 { x: self.x + other.x, y: self.y + other.y }; }
}
let c = a + b;    // 改写为 <Vec2>::add(a, b)

impl PartialEq for Vec2
{
    fn eq(self: &Self, other: &Self) -> bool { ret (self.x == other.x) && (self.y == other.y); }
    fn ne(self: &Self, other: &Self) -> bool { ret (self.x != other.x) || (self.y != other.y); }
}
let same = (p == q);   // 改写为 <Vec2>::eq(&p, &q)——两个操作数都只是被借走
```

- 结构体操作数实现了对应 trait → 运算改写为方法调用。
- **算术/位运算 trait 按值**（`self, other`），会消费两个操作数（与 Rust 的 `Add::add` 一致）；
  **比较类 trait 按引用**（`&Self`），操作数被自动取引用，`a == b` 之后 `a`、`b` 仍可用——
  这正是 `String` 能实现 `PartialEq` 的前提（按值签名会移动两个字符串）。
- 方法签名必须 `2` 个参数（self, other），操作数类型必须匹配（比较类允许「引用接收者 +
  值操作数」的自动取引用）。
- 泛型操作数：`T: Add` 约束下 `a + b` 解析为占位 `<T>::add`，单态化时分流
  （结构体 → 方法；原语 → 回退二元运算，见[泛型](./generics.md)）。

## 原语自动实现（播种）

trait 声明时原语自动播种（stdlib 声明这些 trait 即生效）：

| trait | 自动实现于 |
|---|---|
| `Numeric` / `Integer`（marker） | int + float(+ char for Numeric)/ int only |
| `Add`–`Rem` | 整数 + 浮点 |
| `PartialEq` / `PartialOrd` | 整数 + 浮点 + char + bool（原语按内置比较走，不经过 trait 调用） |
| `BitAnd`–`Shr` | 仅整数 |

`bool` 刻意不实现 `Numeric`（`true + false` 会对 i1 做加法）。结构体实现
`Numeric`/`Integer` 被拒绝（marker 原语专属）。

## 下标运算符 `v[i]`（2026-09-19）

`[]` 也是可重载的运算符，由**两个** trait 组成（声明在 `vec.lis`，任何类型都能实现）：

```lis
trait Index<T>    { fn at(self: &Self, i: i32) -> T; }        // v[i]     — 读
trait IndexMut<T> { fn set(self: &mut Self, i: i32, v: T); }  // v[i] = x — 写
```

- 索引必须是 `i32`；`at` 的接收者是 `&Self`，`set` 必须是 `&mut Self`（写要独占）。
- 两个 trait 都返回/接收**元素的值**，不返回 `&T`：返回借用需要编译器追踪借用存活期，
  而返回借用目前不追踪（见[已知限制](./limitations.md)）。**因此实现者得自己保证元素是 Copy**
  （`Vec` 写成 `impl<T: Copy> Index<T> for Vec<T>`：把元素按值交出去时容器不能再拥有它）。
  impl 上写的泛型约束是**调用点检查**的：`Vec<String>[0]` 报
  `type 'string$String' does not implement trait 'Copy' required by 'vec$Vec::at'`。
- **数组与裸指针不走这两个 trait**：数组的 `a[i]` 仍是内建投影（带越界检查），
  `*T` 的 `p[i]` 仍是 stdlib 专用的裸地址运算。用户类型没实现 trait 时，
  `v[i]` 报 "type 'X' is not indexable; implement the 'Index<T>' trait"。
- 借用检查把它当**方法调用**看：读不登记借用（只检查），写由赋值本身检查——
  见[借用检查](./borrow.md)的「下标运算符是调用」。
- 标准库的 `Vec<T>` 是第一个实现者，用法见[标准库](./stdlib.md)。

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

## 一元运算符（2026-09-19）

| 运算符 | 作用 | 合法操作数 | 结果 |
|---|---|---|---|
| `-x` | 取负 | 整数、浮点 | 与操作数同类型 |
| `!x` | 逻辑非 | 仅 `bool` | `bool` |
| `~x` | 按位取反 | 仅整数 | 与操作数同类型 |
| `+x` | 恒等 | 任意类型 | 与操作数同类型（不产生指令） |

```lis
let y = -x;          // 取负
let z = ~x;          // 按位取反：~x == -(x + 1)
if !flag { ret 0; }  // 逻辑非
```

- **右结合**、优先级高于所有二元运算符：`-a * b` 是 `(-a) * b`，`~a + 1` 是 `(~a) + 1`；
  连写合法（`- -x`、`!!b`）。
- `-<字面量>` 会**折叠成负字面量**，所以要求字面量的全局初始化器仍可用：`let g = -5;`。
- 操作数可以是任意表达式（位置、字段、下标、调用）：`-a[0]`、`-s.v`、`-f(3)`。
- **仅原语**：`char`、结构体、枚举都没有一元运算符——`-true`、`!5`、`~1.5`、`-'a'` 报
  `operator '-' cannot be applied to type 'bool'.`。
- **尚不可重载**：`Neg`/`Not` trait 未开放（原语直接下降为 `fneg` / `0 - x` / `xor`，
  `+x` 直接擦除）。
- 前缀 `*`（**解引用**，2026-09-19）同级但语义不同：只作用于引用，`*p` 是位置，可读可写，
  裸指针仍走 `__deref`；详见[表达式](./expression.md)。

## 复合赋值（2026-09-19）

`x op= y` 的语义是 `x = x op y`，但**目标位置只求值一次**：

```lis
v[f()] += 1;     // f() 只调用一次（展开写法 v[f()] = v[f()] + 1 会调用两次）
self.count *= 2;
a[i] %= 10;
```

- 五个运算符：`+=` `-=` `*=` `/=` `%=`（位运算/`<<`/`>>` 没有 token，故无复合形式）。
- 目标是任意可写位置：局部变量、字段（含 `&mut self`）、数组元素、`Vec` 元素、`*p`、
  模块级全局。
- 操作数规则与对应二元运算符一致：**两操作数同类型**、原语（或带 `Numeric`/运算符 trait
  约束的泛型参数）、目标必须可变（否则 E3004）。
- **重载类型暂不支持**：`impl Add` 的类型写 `+=` 报
  `compound assignment is not supported on a type that overloads the operator yet.`；
  但 `Vec` 的 `v[i] += x` **可用**——下标走 `Index`/`IndexMut` trait，与算术 trait 无关。
- 类型不同（`let mut x = 1; x += 1.5;`）报 `operands of '+=' must have the same type.`。
