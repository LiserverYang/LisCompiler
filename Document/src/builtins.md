# 内置函数

<!-- grammar_name: builtins, search_name: 内置函数,print,read,堆,to_string -->

编译器内置函数（不在标准库声明）。这些名字**保留**：用户 `fn` 与内置名或 libc 名同名
（`malloc`/`free`/`memcpy`/`strlen`/`sprintf`/`printf`/`fgets`/`strcspn`/`atoi`/`strtod`/`abort`）
会被拒绝（「function name 'X' is reserved by the compiler」）。
## 终止 panic

| 函数 | 签名 | 行为 |
|---|---|---|
| `panic(msg: &i8)` | `-> never` | 向 **stderr** 写 `panicked: <msg>`，随后调用 libc `abort()` 终止进程 |

`panic` 的返回类型是 **`never`**（uninhabited / bottom 类型，见[类型系统](./types.md)），
因此它可以出现在任何需要值的位置（`ret panic("...")`、实参、match 臂、`let` 初始化器、
赋值、数组元素），而这些位置都不会产生值。同一语句序列中 `panic` 之后的语句不可达。
反过来，**声明返回 `never` 的函数必须发散**：函数体里必须至少有一个发散调用（直接或间接
调用 `panic`），否则编译期报错。

注意：`abort()` **不会刷新 stdio**，所以 `panic` 之前打印到 stdout 的内容会丢失；panic 的
可观测输出只有 stderr。

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

**EOF 行为（2026-09-18 修）**：输入耗尽后 `read_line()` 返回**空串**、`read_int()`/`read_f64()`
返回 **0**。此前 `fgets` 的失败返回值被丢掉，缓冲区保持上一行内容，于是
`while true { read_line() }` 会永远读到同一行、永不结束。对 `read_line` 而言**空行与 EOF
不可区分**（与 Rust 的 `read_line` 返回 0 字节一致），所以结束条件写 `s.len() == 0`。

## 堆 __alloc 系

| 函数 | 签名 | 说明 |
|---|---|---|
| `__alloc(n: i32) -> *mut i8` | `malloc(n)`，返回可写堆缓冲 | |
| `__free(p: *i8) -> void` | `free(p)` | |
| `__memcpy(dst: *mut i8, src: *i8, n: i32) -> *mut i8` | `memcpy`，返回 dst | |
| `__strlen(s: *i8) -> i32` | `strlen` | |
| `__deref(p: *T) -> &T` | 把只读裸指针变成共享引用（stdlib 专用） | |
| `__deref_mut(p: *mut T) -> &mut T` | 把可写裸指针变成可变引用（stdlib 专用） | |

**这些是编译器的「不安全核心」，只能在标准库（`<bin>/lstdlib` 内的文件）里调用**，
否则编译错误 E3013（堆原语）/ E3014（裸指针操作）。它们直接落到 libc，
没有任何边界、生命周期或别名检查；把它们关在标准库里，是语言其余部分能够安全使用堆的前提。
用户要用堆，就走 `String`（将来还有堆集合）这类带检查的标准库 API。

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
