# 类型系统

<!-- grammar_name: types, search_name: 类型,类型系统,引用,数组 -->

## 拷贝与移动（Copy vs Move）

类型的拷贝/移动属性是整个语言安全模型的基础：

| 类别 | 类型 | 属性 |
|---|---|---|
| 基本类型 | `i8` `i16` `i32` `i64` `f32` `f64` `bool` `char` | **Copy** |
| 引用 | `&T` `&mut T` | **Copy** |
| 结构体 | `struct` | **Move**（除非全部字段 Copy 也不自动 Copy） |
| 枚举 | `enum` | **Move** |
| 数组 | `[T; N]` | **Move**（即使元素是 Copy——保持元素引用的单一所有权） |
| `void` | — | 无值 |

Copy 类型的赋值/传参是值拷贝；Move 类型是移动，移动后原绑定不可使用（见[所有权](./ownership.md)）。

## 基本类型

- 整数：`i8` `i16` `i32` `i64`（运行时宽度 8/16/32/64 位）
- 浮点：`f32` `f64`
- 布尔：`bool`（运行时 1 位，运算时扩展为 i32）
- 字符：`char`（**运行时是 i32**，存 Unicode 码点；无 char 算术，需 `as i32` 后运算）
- `void`：无值类型，仅用于函数返回

整数运算默认 `i32`；整数字面量类型为 `i32`，浮点字面量为 `f64`。

## 引用类型

<grammar>
reference_type = "&" ["mut"] type
</grammar>

- `&T`：共享引用，只读。
- `&mut T`：独占可变引用。

引用是 Copy 类型。类型兼容是**方向性**的：`&mut T` 可以当作 `&T` 读取
（可变引用可被读取为共享引用），反向（`&T` 传给期望 `&mut T` 的参数）被拒绝。

引用语义（借用检查）见[借用检查](./borrow.md)。写穿规则：

- 通过共享引用写（`r.x = 5`，其中 `r: &T`）→ 编译错误「cannot assign through a shared reference」。
- 通过 `&mut` 引用写穿字段/元素（`s.data[i] = 'x'`）→ 允许（不改动 `s` 本身）。

## 数组类型

<grammar>
array_type = "[" type ";" integer_literal "]"
</grammar>

约束（编译期）：

- 尺寸必须是十进制整数字面量，且 `1 <= N <= 1<<20`（1048576）；空数组/`[T; 0]` 拒绝。
- 元素必须是 Copy 类型（`[T; N]` 本身仍是 Move）。
- 元素不能是引用（`[&mut i8; 2]` 拒绝——数组内引用逃逸未支持）。
- **数组不能作函数参数或返回类型**（LLVM 数组非 first-class）；改用 `&[T; N]`。

运行时：

- 数组索引 `a[i]` 编译期发出 `i < 0 || i >= N` 检查，越界调用 libc `abort()`。
- 索引必须为 `i32`。
- 指针式索引（如 `s.data[i]`，`data: &mut i8`）**无长度检查**（无法知道长度）。

数组字面量 `[a, b, c]`：所有元素必须同类型，元素须 Copy 且非引用；空字面量 `[]` 拒绝
（无元素类型可推断）。

## 结构体

<grammar>
struct_type = identifier ["<" type_list ">"]
</grammar>

结构体是命名字段集合。字段默认私有（仅本结构体方法内可访问），`pub` 显式公开。
访问不存在的字段、对非结构体做成员访问都是编译错误。

## 枚举

<grammar>
enum_type = identifier ["<" type_list ">"]
</grammar>

枚举是带标签的联合（tagged union），变体可携带载荷。布局与匹配规则见
[枚举与模式匹配](./match.md)。

## trait 与 Self

<grammar>
trait_type = identifier ["<" type_list ">"]
</grammar>

`trait` 声明方法契约。类型通过 `impl Trait for Type` 实现 trait。
`T: Trait` 是泛型约束。**`Self`** 类型在 trait 方法签名中表示「实现该 trait 的类型本身」，
在 `impl` 方法体中表示「被实现的结构体」。`Self` 只能出现在 trait/impl 上下文。

## 泛型参数

<grammar>
generic_param = identifier [":" trait_bound]
trait_bound = trait_name ["<" type_list ">"] ["+" trait_bound]
</grammar>

泛型参数可用于函数、结构体、枚举、trait、方法、impl。约束（如 `T: Numeric`）在
调用/实例化时检查。详见[泛型](./generics.md)。

## 函数类型

函数类型（参数→返回）在编译器内部存在（`FunctionType`），支持**间接调用**
（函数指针：`let fp = foo; fp(x);`），但源码中**没有函数类型语法**——
函数指针通过函数名表达式隐式获得。

## 类型兼容与转换

- 赋值/传参要求类型兼容：同类型，或 `&mut T` → `&T` 宽化。
- **不存在隐式类型转换**；所有转换用 `as` 显式写出（规则见[运算符与类型转换](./operators.md)）。
