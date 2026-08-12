# 内置函数

<!-- grammar_name: builtins, search_name: 内置函数,print,read,堆,to_string -->

编译器内置函数（不在标准库声明）。这些名字**保留**：用户 `fn` 与内置名或 libc 名同名
（`malloc`/`free`/`memcpy`/`strlen`/`sprintf`/`printf`/`fgets`/`strcspn`/`atoi`/`strtod`/`abort`）
会被拒绝（「function name 'X' is reserved by the compiler」）。

## 输出 print

| 函数 | 签名 | 输出 |
|---|---|---|
| `print_str(s: &i8)` | 打印 C 字符串 | `printf("%s")` |
| `print_int(x: i32)` | 打印整数 | `printf("%d")` |
| `print_float(x: f64)` | 打印浮点 | `printf("%f")` |
| `print_bool(b: bool)` | 打印 `0`/`1` | `printf("%d")` |
| `print_char(c: char)` | 打印字符 | `printf("%c")` |
| `println()` | 换行 | `printf("\n")` |

全部返回 `void`。

## 输入 read

| 函数 | 签名 | 说明 |
|---|---|---|
| `read_line() -> &i8` | 读一行（去 `\r\n`），返回缓冲指针 | |
| `read_int() -> i32` | `atoi` 解析当前输入 | |
| `read_f64() -> f64` | `strtod` 解析当前输入 | |

所有读操作共用**同一个 256 字节全局缓冲** `__lis_input_buf`：每次 read 覆盖上一次结果，
所以 `read_line()` 返回的 `&i8` 在下一次 read 后失效。无参数。

## 堆 __alloc 系

| 函数 | 签名 | 说明 |
|---|---|---|
| `__alloc(n: i32) -> &mut i8` | `malloc(n)`，返回可写缓冲 | |
| `__free(p: &i8) -> void` | `free(p)` | |
| `__memcpy(dst: &i8, src: &i8, n: i32) -> &mut i8` | `memcpy`，返回 dst | |
| `__strlen(s: &i8) -> i32` | `strlen` | |

**OOM 不检查**：`malloc` 失败时后续写会崩溃（语言无错误处理机制）。

## to_string

| 函数 | 签名 | 格式 |
|---|---|---|
| `to_string_i32(x: i32) -> String` | `%d` | |
| `to_string_i64(x: i64) -> String` | `%lld` | |
| `to_string_f64(x: f64) -> String` | `%f` | |
| `to_string_bool(b: bool) -> String` | `%d`（zext） | |
| `to_string_char(c: char) -> String` | `%c` | |

内部：`malloc(512)`（`TO_STRING_BUF_CAP`，足以容纳 `DBL_MAX` 的 `%f` 输出）+
`sprintf` + `strlen`，构造标准库 `String { data, len, cap=512 }`。
需要标准库 `String` 类型存在（stale lstdlib 会报错）。
