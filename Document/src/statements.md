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
let_statement = "let" ["mut"] identifier [":" type] ["=" expression] ";"
</grammar>

```lis
let x = 5;              // 类型推断
let mut y: i32 = 0;     // 显式类型 + 可变
let mut z: i32;         // 无初始化:仅 Copy 类型允许
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

## 赋值

<grammar>
assignment_statement = expression "=" expression ";"
</grammar>

```lis
x = 5;
a[i] = 40;
self.value = x;
```

- 目标是任意可写位置（变量/字段/数组元素）。
- 赋值会**丢弃（drop）目标旧值**（非 Copy 类型先释放再写入）。
- 无复合赋值（`+=` 等），写作 `s = s + x`。
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
for_statement = "for" identifier "in" expression statement
</grammar>

```lis
for x in Countdown::new(3) { print_int(x); }
```

- 迭代对象必须实现 `Iterator<T>`（`fn next(self: &mut Self) -> Option<T>`）。
- 语法等价于：

  ```lis
  let mut __it = <iterable>;
  while true {
      match __it.next() { Some(x) => <body>, _ => break }
  }
  ```

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
