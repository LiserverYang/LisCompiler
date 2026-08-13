# 声明

<!-- grammar_name: declarations, search_name: 声明,struct,enum,trait,impl,fn,import,模块 -->

程序由全局声明组成：`impt`、`struct`、`enum`、`trait`、`impl`、`fn`、全局 `let`。
类型名（struct/enum/trait）在**同一模块内**唯一，重复定义是编译错误（E2003/E2017）。

## 模块与导入 impt

每个源文件是一个模块。模块隔离命名空间：模块 A 的顶层名字对模块 B 不可见，
除非 B 显式导入。标准库**不再自动预加载** —— 用到的每个标准库模块都要显式
`impt`（`math`、`option`、`iterator`、`string`、`char`、`drop`）。

<grammar>
import_statement = "impt" module_path ["as" identifier | "{" identifier_list "}"] ";"
module_path = identifier { "." identifier }
</grammar>

```lis
impt math;                  // 全导入:math::max(...) 可用(裸名 max 不可见)
impt math as m;             // 别名:m::max(...)
impt math { max, min };     // 选择性导入:max(...) / min(...) 裸名可用
```

- 模块路径是点分路径（`impt lib.nums;` 绑定最后一段 `nums`），文件查找按搜索路径
  依次尝试 `<dir>/lib/nums.lis`。搜索路径顺序：**标准库目录优先** → `-I` 目录 →
  主文件目录（用户文件不能遮蔽同名标准库模块）。
- 全导入/别名导入不提升裸名 —— 成员只能经 `alias::member` 访问。
- 选择性导入把列出的名字提升为**当前模块的裸名**；与当前模块已有定义同名时报错
  （模块自己的定义优先于静默覆盖 —— 冲突是显式错误）。
- 循环导入（a 导入 b、b 导入 a）是编译错误；找不到模块文件报「cannot find module」。
- 模块内声明互相可见（无须导入）；内置函数（`print_*`/`read_*`/`__alloc` 等）与
  libc 保留名在所有模块裸名可用，且用户 `fn` 同名仍被拒绝。
- 导入模块的内部符号名带模块前缀（如 `math$max`），仅影响诊断与 codegen，
  源码中不可见。

## 结构体 struct

<grammar>
struct_declaration = "struct" identifier ["<" generic_params ">"] "{" struct_member_list "}"
struct_member = ["pub"] identifier ":" type ","
</grammar>

```lis
struct Vec2<T>
{
    pub x: i32,
    y: i32        // 私有字段:仅本结构体的方法内可访问
}
```

- 字段逗号分隔，允许尾逗号。
- **`pub` 只能用于结构体字段**；函数/方法/变体无可见性修饰。
- 泛型结构体见[泛型](./generics.md)。

## 枚举 enum

<grammar>
enum_declaration = "enum" identifier ["<" generic_params ">"] "{" enum_variant_list "}"
enum_variant = identifier ["(" type_list ")"]
</grammar>

```lis
enum Option<T>
{
    Some(T),
    None        // 单元变体
}
```

变体逗号分隔，允许尾逗号。载荷是类型列表。见[枚举与模式匹配](./match.md)。

## trait

<grammar>
trait_declaration = "trait" identifier ["<" generic_params ">"] "{" trait_method_list "}"
trait_method = "fn" method_signature ";"
</grammar>

```lis
trait Add
{
    fn add(self, other: Self) -> Self;
}

trait Numeric {}       // 空 marker trait(原语自动实现,结构体不能实现)
```

- trait 方法是签名（以 `;` 结尾），无函数体。
- `Self` 类型在签名中表示实现类型。

## impl

<grammar>
impl_declaration = "impl" ["<" generic_params ">"] (type ["<" type_list ">"]) ["for" trait ["<" type_list ">"]]
                   "{" method_list "}"
</grammar>

```lis
impl Vec2
{
    fn new(x: i32, y: i32) -> Vec2 { ret Vec2 { x: x, y: y }; }
}

impl Add for Vec2
{
    fn add(self, other: Self) -> Vec2 { /* ... */ }
}

impl Drop for String
{
    fn drop(self) { __free(self.data); }
}
```

- 固有 impl（`impl Type`）与方法 impl（`impl Trait for Type`）。
- 方法一致性检查：trait 方法的参数/返回类型必须与 impl 中的签名匹配
  （`Self` 替换为被实现的结构体）。
- 结构体重复实现同一 trait 是错误。

## 函数 fn

### 自由函数

<grammar>
function_declaration = "fn" identifier ["<" generic_params ">"] "(" parameter_list ")" ["->" type] block
parameter = identifier ":" type
</grammar>

```lis
fn max<T: Numeric>(u: T, v: T) -> T
{
    if u > v { ret u; }
    ret v;
}
```

- 省略 `-> type` 表示返回 `void`。
- 函数体必须是块。**默认参数值**（`a: i32 = 5`）**不支持**（2026-08-12 规范决定，解析即报错）。

### 方法

<grammar>
method_declaration = "fn" identifier ["<" generic_params ">"] "(" [self_receiver ["," parameter_list]] ")" ["->" type] (block | ";")
self_receiver = "self" | "self" ":" ("&" ["mut"] type) | "self" ":" type
</grammar>

```lis
impl Counter
{
    fn next(self: &mut Self) -> i32 { /* 独占可变接收者 */ }
    fn get(self: &Self) -> i32 { /* 共享接收者 */ }
    fn into_value(self) -> i32 { /* 按值接收者:消费 self */ }
    fn new() -> Counter { /* 静态方法(无 self) */ }
}
```

- 接收者四形态：`self`（按值）、`self: T`（按值，显式类型）、`self: &T`、`self: &mut T`。
- `mut self` 不支持。
- `&mut self` 要求调用方持有可变位置；`&self` 可被 `&mut` 位置满足（方向性宽化）。
- trait 签名中的方法以 `;` 结尾。

## 全局变量

<grammar>
global_let = "let" ["move"] identifier [":" type] "=" literal ";"
</grammar>

- **初始化器必须是字面量**（防静默清零）：`let g = [1, 2]`（非字面量）是错误。
- 全局变量隐式可变（无 `mut` 语法）。
- 全局 `let move` 的 `move` 标志当前**无效果**（所有非 Copy 值本就移动；`move` 的设计语义见[已知限制](./limitations.md)）。
