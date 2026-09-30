# 内置函数

<!-- grammar_name: builtins, search_name: 内置函数,print,read,堆,to_string -->

编译器内置函数。这些名字**保留**：用户 `fn` 与内置名或 libc 名同名
（`malloc`/`free`/`memcpy`/`strlen`/`strcmp`/`sprintf`/`printf`/`fgets`/`strcspn`/
`atoi`/`strtod`/`abort`/`fprintf`）会被拒绝（「function name 'X' is reserved by the compiler」）。

> **打印与读取不在这个列表里了（2026-09-25）**。`print`/`println`/`flush` 与 `read_*` 现在是
> **标准库 `io` 模块里的普通函数**（`Source/Std/io.lis`），`Display` 在 `fmt` 模块里。
> 编译器只为它们提供**字节流原语**（`__read_byte`/`__write`/`__flush`）与原语的 `Display`
> 下降，词法/数字解析/分行/格式化策略全部是 Lis 代码。见[内置 IO 原语](#内置-io-原语-read_byte--write--flush)
> 与[标准库](./stdlib.md)。
>
> **保留名有一个例外（2026-09-26）**：`extern "C"` 声明就是要绑定 libc 名，所以
> `extern "C" fn strlen(...)` 合法（`fn strlen(...)` 仍然拒绝）。FFI 默认关闭，
> 用户代码要加 `--allow-ffi`；标准库自己可以（`math.lis` 的 libm 绑定）。
> 见 [FFI：调用 C](./ffi.md)。

## 终止 panic

| 函数 | 签名 | 行为 |
|---|---|---|
| `panic(msg: &i8)` | `-> never` | 向 **stderr** 写 `panicked: <msg>`，随后调用 libc `abort()` 终止进程 |

`panic` 的返回类型是 **`never`**（uninhabited / bottom 类型，见[类型系统](./types.md)），
因此它可以出现在任何需要值的位置（`ret panic("...")`、实参、match 臂、`let` 初始化器、
赋值、数组元素），而这些位置都不会产生值。同一语句序列中 `panic` 之后的语句不可达。
反过来，**声明返回 `never` 的函数必须发散**：函数体里必须至少有一个发散调用（直接或间接
调用 `panic`），否则编译期报错。

注意：`abort()` 自己**不会刷新 stdio**，所以 panic 路径会先显式 `fflush(stdout)`（2026-09-25）：
崩溃之前打印的内容不会丢。数组越界（`idx_oob`）与 `assert_fail` 同理。

## 断言 assert

| 函数 | 签名 | 行为 |
|---|---|---|
| `assert(cond: bool)` | `-> void` | `cond` 为假时向 **stderr** 写 `文件:行: assertion failed: ` 后 `abort()` |
| `assert(cond: bool, msg: &i8)` | `-> void` | 同上，消息追加在冒号后 |

与 `panic` 的关键差别：`assert` 返回 **void**，条件成立时**继续执行**（不会把后续语句判成不可达），
所以它可以当作测试断言用。失败路径与 `panic` 完全一致（stderr + `abort()`，同样先 flush stdout），
并且**带上源码位置**——这是 `panic` 没有的。参数个数只接受 1 或 2 个；条件必须是 `bool`，
消息必须是 `&i8`。

```lis
assert(str_len(line) > 0, "empty input");
```

## C 字符串比较 str_len / str_cmp

| 函数 | 签名 | 行为 |
|---|---|---|
| `str_len(s: &i8) -> i32` | libc `strlen` | C 串长度（不含结尾 NUL） |
| `str_cmp(a: &i8, b: &i8) -> i32` | libc `strcmp` | `< 0` / `0` / `> 0`，与 libc 一致 |

`&i8` 是本语言的 C 串写法（`print`、`panic`、`String::from_lit` 都收它），所以长度与内容
比较就挂在它上面；在没有这两个内建之前，`strlen` 是保留名（标准库专用），用户想拿长度只能先
`String::from_lit` 复制一份。两个字符串字面量之间的 `==` / `!=` 会下降为 `str_cmp(a, b) == 0`
（见[表达式](./expression.md)），无需显式调用。

## 输出与输入：io 模块（2026-09-25）

`print_*` / `read_*` 一系列内置函数**已全部退役**。打印与读取现在是标准库 `io` 模块
（`Source/Std/io.lis`）里的普通 Lis 函数，编译器只提供下面的字节流原语与原语 `Display`
的下降。完整 API 表见[标准库](./stdlib.md)；这里只记设计。

```lis
impt fmt { Display };                       // 想给自己的类型实现打印时才需要
impt io  { print, println, read_i32, read_line };

print(42); print(' '); print(3.5);          // 一个入口，任意实现 Display 的类型
println();                                  // 换行 + flush（相当于 std::endl）
print("hi"); print("\n");                   // 普通换行：只入缓冲，不 flush
let n = read_i32();                         // 空白分隔的 token，任意长行
let s = read_line();                        // -> String，当前行的剩余部分
```

**为什么只有一个 `print`**：语言没有重载也没有变参，每加一种类型就加一个 `print_xxx` 是唯一
的扩张方式——于是有了 `Display` trait（`fmt` 模块）加一个泛型 `print<T: Display>(x: T)`。
原语（i8/i16/i32/i64/f32/f64/bool/char）与 `&i8`（C 串）由编译器**播种** `Display` 并由后端
下降（`__show_i32` 等，格式与旧 `print_int` 完全一致）；`String` 与用户类型用普通 `impl`
实现，`impl Display for P { fn show(self: &Self) { ... } }`。

**`println()` 是有代价的**：它是本语言的 `std::endl` —— 零参、写换行**并 flush**。日常换行
写 `print("\n")`（只入缓冲）；`print(x); println();` 是「值 + 换行」的合并写法。
语言没有重载，所以一参的 `println` 需要编译器特判名字，而"flush"正是让零参形式**不是**
`print("\n")` 同义词的理由。（交互题每行 `println()`；批量输出用 `print`。）

**输入是 token 化的**：`read_i32`/`read_i64`/`read_f64` 跳过任意空白（空格/制表/换行）并消费
**一个** token，所以 `"3 4"` 在同一行、一行一个、或者任意混合都对。`read_line() -> String`
返回**当前行的剩余部分**（保留空格，去掉 LF/CRLF），长度不限（旧实现经一个 256 字节静态缓冲，
长行会在 255 处截断并把余下字符留在流里，后续读取全部错位）。EOF 时数字读返回 0、`read_line`
返回空串；要区分「值 0」和「没有输入」用 `try_read_*`（返回 `Option`）。

## 内置 IO 原语 `__read_byte` / `__write` / `__flush`

| 函数 | 签名 | 下降 |
|---|---|---|
| `__read_byte() -> i32` | 读取下一个字节（EOF 为 `-1`） | libc `fgetc(stdin)` |
| `__write(buf: *i8, n: i32)` | 写出 n 个字节 | libc `fwrite(buf, 1, n, stdout)` |
| `__flush()` | 刷新 stdout | libc `fflush(stdout)` |

流缓冲由 libc 的 `stdin`/`stdout` 负责（进程退出自动 flush），所以标准库不需要自己维护缓冲区，
也就不需要指针类型的模块级状态。**与堆原语同一道闸：只能在标准库内调用**（E3013），
用户代码走 `io` 的安全 API。

## 堆 __alloc 系

| 函数 | 签名 | 说明 |
|---|---|---|
| `__alloc(n: i32) -> *mut i8` | `malloc(n)`，返回可写堆缓冲 | |
| `__alloc<T>(n: i32) -> *mut T` | 同上，但返回**带类型的**指针；`n` 仍是**字节数** | |
| `__sizeof(x: T) -> i32` | `sizeof(T)`，编译期常量，不求值参数 |
| `__drop(x)` | 就地析构一个**位置**上的值（容器析构元素的唯一手段） | |
| `__free(p: *T) -> void` | `free(p)`（任何指针类型） | |
| `__memcpy(dst: *mut T, src: *S, n: i32) -> *mut T` | `memcpy`，返回 dst（任何指针类型） | |
| `__strlen(s: *T) -> i32` | `strlen`（任何指针类型） | |
| `__deref(p: *T) -> &T` | 把只读裸指针变成共享引用（stdlib 专用） | |
| `__deref_mut(p: *mut T) -> &mut T` | 把可写裸指针变成可变引用（stdlib 专用） | |

**带类型的 `__alloc<T>`（2026-09-19）**：标准库容器需要一块「指向 T 的堆缓冲」，
而 `*mut i8` 无法索引出 T。`__alloc<T>(n)` 生成的仍是 `malloc(n)`（`n` 是**字节**，
由调用方用 `__sizeof(v) * count` 算），但返回 `*mut T`，于是 `p[i]` 得到干净的
`getelementptr T`。T 可以是尚未确定的泛型参数（`*mut T` 在单态化时才落地），
这正是 `Vec<T>` 的缓冲所依赖的。`__sizeof` 只接受一个**任意类型**的值并把
`DataLayout` 的 `getTypeAllocSize` 作为 i32 常量发出——参数不会被求值，所以
`__sizeof(v)` 不移动 `v`；泛型体内没有 T 的值可用时，就把大小**从调用点传进来**
（`Vec::push` 的 `grow(__sizeof(v))` 就是这么写的）。

**`__drop(x)`（2026-09-19）**：参数是一个**位置**（变量/字段/下标/解引用），语义是
「现在析构这个位置上的值，并把它当作已被移出」。下降就是编译器内部作用域末端的
`MIRStmtDrop`（局部的运行时 drop flag、部分移动分解、动态 drop 全部照旧），因此之后
作用域末端的清理不会再析构它一次；对**不需要析构**的类型（所有原语）它一条指令都不发。
这是标准库容器释放元素的方式：`Vec<T>` 的 `clear` 与 `Drop` 用它逐元素析构，
`Vec<Vec<i32>>` 这类嵌套容器就是靠它递归释放的。参数必须是位置（`__drop(1 + 2)` 报错）。

**这些是编译器的「不安全核心」，只能在标准库（`<bin>/lstdlib` 内的文件）里调用**，
否则编译错误 E3013（堆原语）/ E3014（裸指针操作）。它们直接落到 libc，
没有任何边界、生命周期或别名检查；把它们关在标准库里，是语言其余部分能够安全使用堆的前提。
用户要用堆，就走 `String`、`Vec<T>` 这类带检查的标准库 API。

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
