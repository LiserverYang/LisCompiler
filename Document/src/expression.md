# 表达式

<!-- grammar_name: expression, search_name: 表达式 -->

<grammar>
expression = name_expression
           | calc_expression
           | function_call_expression
           | cast_expression
           | struct_init_expression
           | array_expression
           | index_expression
           | match_expression
           | paren_expression
           | borrow_expression
</grammar>

表达式是独立于语句的一类语法。它和语句一样具有副作用，但不同于语句，表达式同时一定具有一个值和类型
（当这个值的类型为 `void` 时，值视为空），这是它与语句的本质区别。

## 名称表达式

<!-- grammar_name: name_expression, search_name: 标识符 -->

<grammar>
name_expression = identifier_name_expression | field_access_name_expression
</grammar>

名称表达式即根据一个对象的名称获取值的表达式，它的唯一副作用是导致一个对象的移动
（当且仅当指向的名称是变量、且变量类型是非拷贝类型时，触发值移动），表达式的值即名称所对应的值，
也就是表达式的类型也是名称的类型。

### 标识符名称表达式

<grammar>
identifier_name_expression = identifier
</grammar>

标识符所对应的名称仅能对应变量名。由于 Lis 语言禁止作用域内标识符隐式覆盖，
一个标识符所对应的变量名是唯一、无二义性的。不允许引用未定义名称、函数、类型等非变量符号。

### 成员访问名称表达式

<grammar>
field_access_name_expression = expression "." identifier
</grammar>

用于获取结构体实例中某个字段的值。expression 的类型必须是结构体类型，且必须存在以 identifier
为名称的有效字段。私有字段只能在非静态成员函数上下文访问，否则字段必须为 `pub`。

## 运算表达式

<!-- grammar_name: calc_expression, search_name: 运算表达式 -->

<grammar>
calc_expression = binary_calc_expression
</grammar>

运算表达式的语义是将两个表达式的值进行运算，结果即运算表达式的值；副作用等同于其所有子表达式的副作用。

> **一元运算表达式（`-`、`!`、`~` 前置运算符）是设计语法，尚未实现。**
> 当前语言没有一元运算符：负数写作 `0 - x`，逻辑非/按位非暂无语法形式。

### 二元运算表达式

<grammar>
binary_calc_expression = expression binary_calc_operator expression
binary_calc_operator = "+" | "-" | "*" | "/" | "%" | "==" | "!=" | "<" | ">" | "<=" | ">=" | "&&" | "||" | "&" | "|"
</grammar>

二元运算符分为：

- 算术：`+ - * / %`
- 比较：`== != < > <= >=`
- 逻辑：`&& ||`
- 位：`& |`

左右子表达式类型必须相等，不存在隐式类型转换。操作数类型规则、运算符重载与
`^`/`<<`/`>>`（无 token，仅可调用 `bitxor`/`shl`/`shr` 方法）见[运算符与类型转换](./operators.md)。

### 运算符优先级

所有二元运算符**左结合**，优先级（高→低）：

| 优先级 | 运算符 |
|---|---|
| 8 | `*` `/` `%` |
| 7 | `+` `-` |
| 6 | `<` `<=` `>` `>=` |
| 5 | `==` `!=` |
| 4 | `&` |
| 3 | `\|` |
| 2 | `&&` |
| 1 | `\|\|` |

例如 `104 - 104 + 40` = `(104 - 104) + 40`；`a & b == c` = `(a & b) == c`。

## 函数调用表达式

<grammar>
function_call_expression = (identifier | expression "." identifier) ["<" type_list ">"] "(" argument_list ")"
                          | identifier "::" identifier ["<" type_list ">"] "(" argument_list ")"
</grammar>

函数调用表达式用于执行一个函数并获取其返回值。包括四种形态：

1. **自由函数调用**：`name(args)`，可带显式泛型参数 `name<T>(args)`（turbofish）。
2. **方法调用**：`obj.m(args)`、`obj.m<T>(args)`，接收者借用规则见[借用检查](./borrow.md)。
3. **静态调用**：`Type::m(args)`、`Type<T>::m(args)`。
4. **枚举变体构造**：`Enum::Variant(args)`、单元变体 `Enum::Variant`——与静态调用语法相同，
   按类型名是否为已注册枚举在 HIR 构建时区分。

约束：

1. 实参数量、类型必须与函数形参严格匹配（`&mut T` 实参可宽化为 `&T` 形参）；
2. 表达式的值为函数的返回值，类型为函数返回类型；
3. 若函数返回 `void`，则表达式值为空，类型为 `void`；
4. 副作用包含函数执行过程中的所有操作；
5. 按值实参/接收者被移动（所有权转移）。

## 类型转换表达式（cast）

<grammar>
cast_expression = expression "as" type
</grammar>

`expr as Type` 将表达式的值转换为目标类型。**不存在隐式类型转换**，所有转换必须显式写出。

`as` 在每次 primary 表达式（及其成员/调用链）之后立即解析，因此绑定强于所有二元运算符：
`a + b as i32` = `a + (b as i32)`。转换规则见[运算符与类型转换](./operators.md)。

### 属性 `#[i_know]`

<grammar>
attribute = "#[" "i_know" ["=" string_literal] "]"
</grammar>

整数窄化转换（如 `i64 as i32`，数据可能截断）默认是**编译错误**。
在语句前加属性 `#[i_know = "说明"]` 可将该语句内所有窄化转换的错误降级为**警告**
（用户显式承担责任，数据仍可能溢出）：

```lis
let big: i64 = 2147483647 as i64;
#[i_know = "high-to-low type translating can cause data overflow"]
let truncated: i32 = big as i32;   // warning,不是 error
```

目前只定义 `i_know` 一种属性；属性只作用于紧随其后的那一条语句。

## 结构体初始化表达式

<grammar>
struct_init_expression = identifier ["<" type_list ">"] "{" [field_init_list] "}"
field_init = identifier ":" expression ","
</grammar>

```lis
let p = Point { x: 1, y: 2 };
let q = Point { x: 1, y: 2, };   // 尾逗号允许
```

- 字段名必须存在且类型匹配；未初始化字段是错误（无默认值）。
- 泛型结构体的类型实参通常靠字段推断（`Box { v: 10 }`）；无法推断时须显式给出。
- 若 `Name` 是已注册枚举且语法为 `Name::Variant`，那是变体构造（见上），不是结构体初始化。

## 数组表达式

<grammar>
array_expression = "[" expression_list "]"
</grammar>

数组字面量：所有元素必须同类型、Copy 且非引用；空字面量 `[]` 被拒绝（无元素类型可推断）。
数组本身是 Move 类型。详见[类型系统](./types.md)。

## 索引表达式

<grammar>
index_expression = expression "[" expression "]"
</grammar>

`a[i]` 索引数组（编译期越界检查 + 运行时 `abort()`）或指针（`s.data[i]`，无检查）。
索引必须为 `i32`。通过 `&mut` 引用的元素写（`s.data[i] = 'x'`）允许。

## match 表达式

<grammar>
match_expression = "match" expression "{" match_arm* "}"
match_arm = pattern "=>" (expression | block) [","]
</grammar>

match 是值产生表达式：`let got = match f { Some(x) => x, None => 0 };`。
语句形态（所有臂是块）不需要尾分号。规则见[枚举与模式匹配](./match.md)。

## 括号表达式

<grammar>
paren_expression = "(" expression ")"
</grammar>

括号仅用于分组，不改变值/类型。注意 `if (cond) { }` 中的括号也只是分组，不是语法要求。

## 借用表达式

<grammar>
borrow_expression = "&" expression | "&" "mut" expression
</grammar>

`&x` 创建共享借用，`&mut x` 创建独占可变借用（要求 x 可写）。借用的生命周期与冲突规则见
[借用检查](./borrow.md)。注意 `&` 在表达式中的角色由位置决定：前缀是借用，中缀是位与。

## 语法消歧说明

1. **裸标识符 + `{`**：`Name { ... }` 优先解析为结构体字面量。在 `if`/`while` 条件与 `for`
   迭代式内部，仅当 `Name` 是已知类型时才按结构体字面量解析，否则 `{` 是块——
   因此 `if some_var { }` 需要括号（除非变量名与类型名撞名）。
2. **`Name::X` 双义**：按类型名是否为枚举区分变体构造与静态调用。
3. **`match` 臂形态**：`=> 表达式`（值臂）与 `=> { 块 }`（语句臂）不可混用。
