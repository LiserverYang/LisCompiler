# 错误信息

<!-- grammar_name: errors, search_name: 错误,错误码,诊断 -->

编译器诊断带错误码（`error[EXXXX]`）与源码位置。错误码分段：

| 段 | 范围 | 阶段 |
|---|---|---|
| E1xxx | 1001–1004 | 词法 |
| E2xxx | 2001–2017 | 语法 |
| E3xxx | 3001–3010 | 语义（HIR） |
| E4xxx | 4001–4007 | 借用检查 |
| E5xxx | 5001–5005 | 枚举 / match |

## E1xxx 词法

| ID | 消息 |
|---|---|
| 1001 | Unclosed string literal |
| 1002 | Unclosed char literal |
| 1003 | Unknown character |
| 1004 | Unclosed block comment |

## E2xxx 语法

| ID | 消息 |
|---|---|
| 2001 | Unexpected finish |
| 2002 | Expected an identifier |
| 2003 | Multiple-defined struct |
| 2004 | Expected `{` |
| 2005 | Undefined struct |
| 2006 | Expected `}` |
| 2007 | Expected `(` |
| 2008 | Expected `)` |
| 2009 | Expected `:` |
| 2010 | Undefined type |
| 2011 | Expected keyword |
| 2012 | Expected `;` |
| 2013 | Expected `=` |
| 2014 | Expected type |
| 2015 | Expected expression |
| 2016 | Invalid literal type（也用于数字字面量溢出） |
| 2017 | Multiple-defined trait |

解析器有错误恢复：一次编译报告多个语法错误（`synchronize()` 跳到安全重启点），
最终以非零退出码结束。

## E3xxx 语义

| ID | 消息（示例） |
|---|---|
| 3001 | Semantic error（通用） |
| 3002 | Type mismatch |
| 3003 | Undefined identifier（模块错误也归此码：cannot find module 'x' / circular import: module 'x' / module 'x' has no member 'y' / selective import of 'y' conflicts with an existing name） |
| 3004 | cannot assign to immutable variable 'x'（及字段/元素/共享引用变体） |
| 3005 | use of moved value: 'x'（含 partially moved / moved inside loop 变体） |

> 3006–3010（Arg mismatch / Generic / Trait / Return type / Cast）在 ErrorID.hpp 中定义，
> 但语义分析器实际未使用 —— 实参/泛型/trait/返回/cast 类错误均落到 3001/3002 或带消息
> 的 3001。E2016 用于字面量溢出（语义层）。

## E4xxx 借用检查

| ID | 消息 |
|---|---|
| 4001 | cannot borrow 'x' as mutable because it is already borrowed |
| 4002 | cannot borrow 'x' because it is already borrowed as mutable / cannot read 'x' because it is borrowed as mutable |
| 4003 | cannot assign to 'x' because it is borrowed |
| 4004 | cannot move out of 'x' because it is borrowed |
| 4005 | cannot borrow moved value 'x' |
| 4006 | cannot borrow 'x' as mutable because it is not mutable |
| 4007 | cannot return reference to 'x': it does not live long enough / cannot return struct: reference field 'f' does not live long enough |

## E5xxx 枚举 / match

| ID | 消息 |
|---|---|
| 5001 | unknown enum / enum 'X' has no variant 'Y' |
| 5002 | variant 'v' expects N payload argument(s), got M |
| 5003 | match scrutinee must be an enum (got '...') |
| 5004 | match is not exhaustive: variant 'v' is not covered (add an arm or a '_' wildcard) |
| 5005 | variant 'v' pattern expects N binding(s), got M |

## 其它

- 「useless cast from 'A' to 'B'」以 `info` 级别输出（非致命）。
- 字符串字面量 `&i8` 提示以 `warning` 级别输出（一次性）。
- `#[i_know]` 抑制的窄化转换以 `warning` 输出。
