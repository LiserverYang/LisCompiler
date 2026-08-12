# 泛型

<!-- grammar_name: generics, search_name: 泛型,trait 约束,单态化,Self -->

泛型支持函数、结构体、枚举、trait、方法、impl。编译采用**单态化**（monomorphization）：
每个具体实例生成独立代码。

## 泛型参数与约束

<grammar>
generic_params = "<" generic_param_list ">"
generic_param = identifier [":" trait_bound]
trait_bound = trait_name ["<" type_list ">"] ["+" trait_bound]
</grammar>

```lis
fn max<T: Numeric>(u: T, v: T) -> T { ... }
fn next<T: Iterator<i32> + Clone>(...) { ... }   // 多约束
```

约束在调用/实例化时检查：类型必须实现约束中的 trait，否则编译错误
（「type 'X' does not implement trait 'Y' required by 'Z'」）。

## 泛型函数

- 显式类型实参：`value<i32>(10)`（turbofish 语法）。
- 自动推断：从实参类型推断（递归穿过引用与同源自定义类型）；推断失败须显式指定。
- 实例化名：`baseName_Mono_<arg1>_<arg2>`；若泛型参数逃过替换（未实例化）会报错。

## 泛型结构体 / 枚举

```lis
struct Box<T> { pub v: T }
let b = Box { v: 10 };               // 推断 T = i32
let b2 = Box::<i32>::new(10);        // 静态调用带类型实参
```

- 实例化名 `Name$i32`（mangle），只缓存完全具体的实例。
- 泛型结构体的方法在结构体泛型参数上也是泛型的（方法调用携带结构体实参）。
- 约束检查在实例化时进行。

## Self 类型

`Self` 在 trait 方法签名中表示实现类型，在 impl 方法体中表示被实现的结构体。
一致性检查把 trait 签名中的 `Self` 替换为被实现结构体后比对。

## trait 约束与实现

- `impl Trait for Type` 注册实现；约束满足 = 类型的 `implTrait` 集合包含相等 trait。
- 一致性检查：trait 方法必须全部实现，参数/返回类型匹配（`Self` 替换后）。
- 实现注册顺序无关（预注册再分析）。

## 单态化

收集所有泛型调用 → 按类型实参实例化函数体（替换泛型参数）→ 递归收集嵌套泛型调用。
泛型 `Drop` 方法由 `seedDropMethods` 单独实例化（无调用点触发）。

## 泛型算子分发

泛型函数中的算子（`fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }`）在单态化时按
具体类型分流：

- 具体类型是结构体 → 重定向到 `<Struct>::add`（该结构体的 Add 实现）。
- 具体类型是原语 → **回退为直接二元运算**（原语没有 add 方法，`sum(10, 20) = 30`）。

## 特殊 marker trait

- `Numeric` / `Integer`：**仅原语可自动实现**（int/float + char for Numeric；int only for Integer）。
  结构体 `impl Numeric` 被拒绝（E 提示：用算子 trait 重载）。
- 算子 trait（`Add` `Sub` `Mul` `Div` `Rem` `PartialEq` `PartialOrd` `BitAnd` `BitOr`
  `BitXor` `Shl` `Shr`）：原语自动实现 + 结构体可显式实现（运算符重载），
  规则见[运算符与类型转换](./operators.md)。

## 泛型参数接收者的方法调用

`it.next()`（`it: T: Iterator<i32>`）：从 T 的约束中解析方法，Self 映射为 T。
多约束同时定义同名方法 → 歧义错误；无约束定义 → 错误。占位调用 `<T>::next` 由单态化重定向。
