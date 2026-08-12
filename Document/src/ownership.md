# 所有权与移动

<!-- grammar_name: ownership, search_name: 所有权,移动,移动语义,drop,所有权与移动 -->

Lis 的所有权模型继承自 Rust：每个值有唯一所有者，移动是显式语义，离开作用域时自动释放
（drop）。本页描述移动规则与资源释放（drop glue）。

## 拷贝与移动

- **Copy 类型**：`i8`–`i64`、`f32`/`f64`、`bool`、`char`、`&T`/`&mut T`。
  赋值、传参、返回值都是值拷贝，原绑定继续可用。
- **Move 类型**：结构体、枚举、数组。赋值/传参/返回/按值 `self` 都**移动**值——
  移动后原绑定不可再使用（读取/借用/再次移动都是编译错误 E3005）。

```lis
let a = String::new();
let b = a;          // 移动 a → b
// print_str(a.to_cstr());  // 错误:use of moved value: 'a'
```

## 移动语义细节

### 整值移动

移动整个变量后，该变量在作用域内不可再使用，直到被重新赋值。

### 部分移动（字段移动）

非 Copy 字段可单独移动出结构体；该字段（及其祖先/后代路径）不可再用，
**其余字段仍可用**：

```lis
let p = Point { x: Inner::new(), y: 5 };
let ix = p.x;          // 移动 p.x
let y = p.y;           // OK:y 未移动
// let q = p;          // 错误:use of moved value: 'p' (partially moved)
```

### 重赋值释放旧值

对非 Copy 变量整体重新赋值时，先 **drop 旧值**再写入新值：

```lis
let mut x = Leaf { v: 1 };
x = Leaf { v: 2 };     // 先 drop(Leaf{1}),再写 Leaf{2}
```

### 循环携带移动

外层非 Copy 变量在循环体内被移动且未在循环内重赋值 → 编译错误
（下一次迭代会再次移动，造成 double-free）：

```lis
while c { let a = x; }        // 错误:value 'x' is moved inside this loop
while c { let a = x; x = make(); }  // OK:每次迭代重赋值
```

### 参数与返回

- 按值实参（非 Copy）从调用方移动到函数。
- 按值 `self` 消费接收者。
- `ret expr` 移动返回值给调用方。

## Drop 与资源释放

### Drop trait

```lis
impl Drop for String
{
    fn drop(self) { __free(self.data); }
}
```

实现 `Drop` 的类型在值离开作用域（或重赋值丢弃旧值）时调用 `drop(self)`。
调用后值不可再用。基本类型与引用从不 drop。

### drop glue（编译器生成的释放逻辑）

编译器在作用域出口按**逆声明序（LIFO）**释放所有仍持有的非 Copy 局部变量/临时值：

- 已移动的值不会重复 drop（`movedLocals` 跟踪）。
- **部分移动分解**：root 部分移动后，drop glue 只释放仍持有的字段（递归嵌套结构体）。
- **枚举 tag-aware**：按 `__tag` 只释放激活变体的非 Copy 载荷。
- 数组元素是 Copy → 数组不释放元素。
- `Drop::drop` 的按值 `self` 参数不生成 drop glue（否则 `X::drop → __drop_X → X::drop` 无限递归）。

```lis
fn main() -> i32
{
    {
        let z = Leaf { v: 1 };   // 块结束编译器插入 z.drop()
    }
    ret 0;
}
```

## 全局变量

- 初始化器必须是字面量（防静默清零）。
- 全局变量按符号种类判定为全局存储（引用来源 Global，见[借用检查](./borrow.md)）。
- 程序退出时不 drop 全局（已知限制）。

## 未初始化变量

`let x;`（无初始化）可解析；读取未初始化变量当前是未定义行为
（LLVM undef）。**规范决策：计划编译期拒绝读取未初始化变量**
（路线图，见[已知限制](./limitations.md)）。
