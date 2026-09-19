# 语句

<!-- grammar_name: statements, search_name: 语句,let,if,while,for,match,ret -->

<grammar>
statement = block_statement
          | let_statement
          | assignment_statement
          | return_statement
          | if_statement
          | while_statement
          | for_statement
          | break_statement
          | continue_statement
          | match_statement
          | expression_statement
</grammar>

语句的执行产生副作用，本身没有值（区别于[表达式](./expression.md)）。

## 块

<grammar>
block = "{" statement* "}"
</grammar>

块引入新的作用域。块内最后一个表达式不是隐式返回值（无尾表达式语义，需显式 `ret`）。

注意：**独立的空语句 `;` 不是合法语句**（会被当作表达式解析并报错）。

## let 声明

<grammar>
let_statement = "let" ["mut"] identifier [":" type] ["=" expression ["else" expression]] ";"
</grammar>

```lis
let x = 5;              // 类型推断
let mut y: i32 = 0;     // 显式类型 + 可变
let mut z: i32;         // 无初始化:仅 Copy 类型允许
let v = maybe() else 0; // 解包 Option/Result，None/Err 时取 else 分支
```

- `mut` 仅局部变量可用（全局无 `mut` 语法，全局隐式可变）。
- 同一作用域禁止重名声明（禁止 shadowing）。
- **无初始化 `let`（定值分析，2026-09-13 落地）**：
  - **Move 类型**（struct/enum/数组）**必须带初始化器**：否则报 E3012 —— 离开作用域时 drop glue
    会释放一个从未构造的值。
  - **Copy 类型**（基本类型/引用）允许 `let x;`，但**每次使用（读/借用/移动/写字段）前必须已定值**，
    否则报 E3011。定值状态按控制流合并：`if`/`else` 与 `match` 取各分支的**与**，循环体里的赋值
    不算（循环可能零次执行）；不能落空的分支（`ret`/`break`/`continue`/发散调用）不参与合并；
    发散语句之后的**不可达代码不做定值检查**。
  - 整变量赋值会满足定值；对 `let x;` 绑定**写字段**不满足（语言跟踪整个绑定，不做字段级定值），
    `x.f = 1;` 前必须先用整体赋值或初始化器定值。

## let ... else（2026-09-19）

`let x = <Option/Result> else <表达式>;` 绑定成功路径的**载荷**，失败（`None`/`Err`）时
求值 `else` 后的表达式并就地赋给 `x`：

```lis
let n = parse(s) else 0;                 // 失败时 n = 0
let n = find(v) else panic("missing");   // else 分支可以发散（never）
```

- 只接受 `Option<T>` / `Result<T, E>`（与后缀 `?` 同一限制）；载荷类型即绑定类型，
  且是**按值移动**（`Option<String>` 绑定出一个 `String`）。
- `else` 分支是**表达式**，不是块；类型必须等于载荷类型，发散类型 `never`
  （`panic(...)`、`ret`）兼容。
- 全局变量不能用：全局初始化器必须是字面量。
- 其它形态报错：非 Option/Result 报 `'let ... else' requires an 'Option' or 'Result' initializer`。

## 赋值

<grammar>
assignment_statement = expression ("=" | "+=" | "-=" | "*=" | "/=" | "%=") expression ";"
</grammar>

```lis
x = 5;
a[i] = 40;
self.value = x;
v[i] += 1;       // 复合赋值：等价于 v[i] = v[i] + 1，但位置只求值一次
```

- 目标是任意可写位置（变量/字段/数组元素/`Vec` 元素/`*p`/全局）。
- 赋值会**丢弃（drop）目标旧值**（非 Copy 类型先释放再写入）。
- **复合赋值**（2026-09-19）：`+= -= *= /= %=`，语义是 `x = x op y` 且目标位置
  **只求值一次**；规则见[运算符](./operators.md)的复合赋值一节。
- 对不可变变量/字段/元素赋值、写穿共享引用都是编译错误（E3004）。

## ret 返回

<grammar>
return_statement = "ret" [expression] ";"
</grammar>

- `ret expr;` 返回表达式的值（函数返回类型必须匹配）。
- 裸 `ret;` 用于 `void` 函数。
- 返回会移动非 Copy 的返回表达式（所有权转移给调用方）。

## if / else

<grammar>
if_statement = "if" expression statement ["else" statement]
</grammar>

```lis
if n == 0 || n == 1 { ret 1; }
if (u < v) { } else if (u > v) { } else { }
```

- 条件**不需要括号**（括号仅作分组）。
- `else if` 链是自然的（else 分支可以是任意语句）。
- **if 不是表达式**（不能 `let x = if ...`）；值产生用 [match 表达式](./match.md)。
- 歧义注意：`if some_struct_var { }` 中裸标识符后跟 `{` 会被当作结构体字面量——
  此时需要加括号 `if (some_struct_var) { }`（见[表达式](./expression.md)的消歧说明）。

## while

<grammar>
while_statement = "while" expression statement
</grammar>

```lis
while i < 4 { i = i + 1; }
```

循环体内的移动有流敏感检查：外层非 Copy 变量在循环体内被移动且未重赋值会报错
（下一次迭代会再次移动——double-free 风险）。

## for

<grammar>
for_statement = "for" identifier "in" ["move"] expression statement
</grammar>

```lis
for x in v { }            // v 是位置：默认**借用**，x 是 &T
for x in move v { }       // 消费 v，x 是 T
for i in range(1, 5) { }  // 右值（临时迭代器）：只能消费
```

三种形态（2026-09-19）：

| 写法 | 迭代对象 | `x` 的类型 | 循环之后 |
|---|---|---|---|
| `for x in v` | 位置（变量/字段/`*p`） | `&T` | 集合仍可用（循环期间被共享借用） |
| `for x in move v` | 同上 | `T` | 集合已消费，再用报 E3005 |
| `for x in f()` | 右值（`range(1,5)`、`Countdown::new(3)`） | `T` | 无（临时值用完即弃） |

- 位置形态要求该位置有 **`iter()`** 方法（标准库只有 `Vec<T>`：`iter()` 交出 `VecIter<T>`，
  每步给 `&T`）；右值/`move` 形态直接消费表达式，迭代对象要有
  `next(self: &mut Self) -> Option<T>`（`Iterator<T>`）。
- 脱糖：借用形态

  ```lis
  let __src = &<place>;
  let mut __it = __src.iter();
  while true { match __it.next() { Some(x) => <body>, _ => break } }
  // __src 的共享借用一直持续到循环结束
  ```

  消费形态没有 `__src`/`iter()`，直接 `let mut __it = <表达式>;` 后同样展开为 `while` + `match`。
- 借用形态下**循环体不能改动被借的集合**：`for e in v { v.push(2); }` 报 E4001（扩容会
  释放迭代器正指着的那块缓冲）。
- 已知边界：名字绑定的**纯迭代器**（`let r = range(1, 5); for i in r`）没有 `iter()`，
  要写 `for i in move r`；已经是引用的位置要写 `for x in *r`（没有运算级自动解引用）。
- 无 C 风格 `for`、无范围字面量；半开区间用标准库 `range(start, end)`。

## break / continue

`break;` 退出最内层循环；`continue;` 跳到最内层循环的下一次迭代。

## match 语句

<grammar>
match_statement = "match" expression "{" match_arm* "}"
</grammar>

match 是表达式（见[表达式](./expression.md)），作为语句时不需要尾分号。
规则见[枚举与模式匹配](./match.md)。

## 表达式语句

<grammar>
expression_statement = expression ";"
</grammar>

表达式后加分号即语句（丢弃表达式的值）。调用返回 `void` 或需要副作用时常见。

## 语句终止规则

- `if`/`while`/`for`/`match` 作为语句**不需要** `;`。
- `let`/`ret`/`break`/`continue`/赋值/表达式语句**需要** `;`。
