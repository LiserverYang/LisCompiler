# 枚举与模式匹配

<!-- grammar_name: match, search_name: 枚举,模式匹配,match,变体 -->

## 枚举布局

枚举是带标签的联合。编译器把枚举实现为带合成字段的 fat tagged union：

```text
enum Option<T> { Some(T), None }
→
struct Option$T {
    __tag: i32,          // 判别值 = 变体下标(0, 1, ...)
    some_0: T,           // 每个变体一个载荷槽(并列,浪费空间但机制统一)
    // None 无载荷,无槽
}
```

构造变体时写 `__tag` 与该变体槽；drop glue 按 `__tag` **只释放激活变体**的非 Copy 载荷。

## 变体构造

<grammar>
variant_expression = identifier "::" variant_name ["(" argument_list ")"]
</grammar>

```lis
let o = Option::Some(5);
let n = Option::None;          // 单元变体
let c = color::green;
```

- 与静态调用语法相同，按类型名是否为枚举区分（编译单元内共享已知枚举集合）。
- 载荷实参数量、类型必须与变体声明匹配（E5001/E5002）。
- 泛型枚举的类型实参从载荷推断；无载荷实参（`Option::None`）时依赖
  **期望类型推断**（如返回类型）；无上下文时推断失败。

## match 规则

<grammar>
match_expression = "match" expression "{" match_arm* "}"
match_arm = pattern "=>" (expression | block) [","]
pattern = "_" | variant_name | variant_name "(" binding_list ")"
</grammar>

```lis
let got = match o {
    Some(v) => v + 1,      // 值臂:表达式
    None => 0,
};

match c {                  // 语句形态:块臂
    red => { ret 0; },
    _ => { ret 1; },
}
```

约束（编译期）：

1. **判别对象必须是枚举**（E5003）；匹配**消费**判别对象（整体移动）。
2. **穷尽性**：所有变体必须覆盖，或有一个 `_` 通配臂（E5004）。
3. **`_` 通配臂必须最后**；`_` 不能绑定值（E5005 系）。
4. 模式绑定数量必须与变体载荷一致（E5005）。
5. 值臂（`=> 表达式`）类型必须一致（`let y = match ...` 的结果类型）；
   **块臂与值臂不可混用**（E 系）。
6. 绑定名只在 match 臂内作用域可见。

## 载荷绑定与所有权

- Copy 载荷：从判别对象投影拷贝到绑定。
- 非 Copy 载荷：**移动**出判别对象，绑定为臂内 owned 帧的局部变量，
  臂体结束（或 ret 路径）恰好 drop 一次——无 double-free。
- 通配臂处理非 Copy 枚举：整体按 tag-aware glue 释放判别对象。

```lis
let o = Option::Some(Inner::new());
let x = match o { Some(v) => v.v, None => 0 };   // Inner 恰 drop 一次
```

## 当前限制

- 不能 `match &opt`（引用模式未实现）。
- 模式无嵌套、无 or 模式、无字面量模式。
- `let x = Option::None;`（无期望类型上下文）推断失败（E）。
- 见[已知限制](./limitations.md)与路线图。
