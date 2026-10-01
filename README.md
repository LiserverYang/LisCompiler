# Lis Language Standard Compiler Lis语言标准编译器

这个项目实现了 lis 语言的编译器以及运行时标准库；广义地说，实现了 lis 语言。

## lis 语言

lis 语言的设计初衷是实现一个 rust 和 c++ 的融合体，通过引入 rust 的所有权和生命周期
机制保证了内存安全。所以可以说，**安全**同样也是 lis 语言的一个设计理念。

有关 lis 语言的具体例子，可以参考 `Examples` 目录下的所有 `.lis` 文件，这些示例都有
比较详尽的注释。完整、权威的语言参考在 `Document/`（mdBook，`mdbook build` 构建，
涵盖词法/类型/声明/语句/表达式/所有权/借用/泛型/match/运算符/内置/标准库/FFI/错误码/
已知限制/优化分工）。

我相信如果你有 c++ / rust 语言的基础，入门这个小编程语言是绰绰有余的，试试它吧！

## 项目现在的位置

语言已具备写真实多文件程序的能力：

| 能力 | 状态 |
|------|------|
| 类型系统 | struct、enum（fat tagged union + `match`）、泛型（单态化）、trait 与约束、引用 `&T`/`&mut T`、裸指针 `*T`/`*mut T`、函数指针、数组 `[T; N]`、String 堆字符串、**Vec<T> 堆数组（元素可为任意类型）** |
| 安全模型 | move 语义、borrow checker（NLL + 字段级精度）、悬垂引用检测（E4007）、**返回引用的借用溯源（`at_ref`/`at_mut`/`to_cstr`/`get_ref` 的借用绑定到接收者）**、drop glue（RAII，tag-aware）、数组越界运行时检查（abort）、**定值分析（未初始化读取是编译错误）**、**字段可见性 `pub` 强制（E3015）**、**`&mut T` 非 Copy + 隐式重借用** |
| 堆 | **裸指针类型 `*T`/`*mut T`**；堆原语与裸指针操作**只在标准库内可用**（E3013/E3014），`String.data` 是 `*mut i8`，用户代码只能走带检查的 stdlib API（String / Vec / Map / Set / HashMap） |
| 运算符 | 12 个运算符 trait 重载，泛型算子（`fn sum<T: Add>` 对 struct 与原语分流），**后缀 `?` 错误传播** |
| 错误处理 | `Result<T, E>` + `?`：`unwrap`/`expect`/`unwrap_or`/`is_ok`/`is_err`；`panic` + `never` |
| 标准库 | **15 个模块**（drop/chars/option/result/iterator/hash/set/fmt/io/map/hashmap/math/string/vec/ffi），**显式 `impt` 导入**；`print`/`println`/`read_*` 是 `io` 模块里的普通函数（`Display` 在 `fmt`）；**红黑树 Map/Set（有序，带序统计）与链式 HashMap/HashSet**；`Vec<T>` 带 `Index`/`IndexMut` trait，非 Copy 元素由 `at_ref`/`at_mut`/移动式 API 处理、容器逐元素析构 |
| **FFI（调用 C 与反向导出）** | `extern "C"` 调 C（`#[link_name]`、变参）、`#[repr(C)]` 布局见证、结构体**按值**过边界（Win64 / SysV ABI 适配）、`export fn` 反向导出、回调（`fn(A) -> R` 类型）、`into_raw`/`from_raw` + `lis_alloc`/`lis_free`、`#[link(name = "m")]`、`--shared` 出动态库。默认关闭，用户代码要 `--allow-ffi`（见 `Document/src/ffi.md`） |
| **模块系统** | `impt lib.nums;` / `impt math as m;` / `impt math { max };`，模块隔离命名空间、循环导入检测、搜索路径（lstdlib 优先 → `-I` → 主文件目录） |
| 内置 | `panic`/`assert`、字节流原语 `__read_byte`/`__write`/`__flush`、堆（`__alloc` 系）、`to_string_*`、`str_len`/`str_cmp`；`#[i_know]` 属性放行窄化 cast。**打印与读取不在这里——它们是标准库 `io` 模块** |
| 诊断 | GCC 风格带源码上下文的错误（E1xxx 词法 ~ E6xxx 错误传播），解析错误可恢复；编译器内部错误（ICE）带调用栈与 issue 链接 |
| 优化 | **不与 LLVM 重叠**：只把语言知道、LLVM 推不出来的事实写成 IR 事实（非入口函数 `internal` linkage、`&mut` 形参 `noalias`），其余交给 LLVM 的 O2 全管线（`-o 0..3`）。见 `Document/src/optimization.md` |
| 测试 | **1421 个 gtest 全绿**（运行时用例进程内 MCJIT 执行 + 按物理核分片）；**16 个 Examples** 输出为回归基线（borrow 55/iterator 23/match 8/method_ref 10/operator 41/result 42/…） |

**已完成**：panic/`never` → `Result<T, E>` + 后缀 `?` 错误传播 + 编译期定值分析 →
**堆安全化**（裸指针类型、堆原语/裸指针操作仅限标准库、`&mut T` 独占语义、字段可见性强制）→
**`Vec<T>` 堆集合**（`Index`/`IndexMut` trait、`__sizeof`、带类型的 `__alloc<T>`）→ **非 Copy 元素**
（`__drop` 逐元素析构、`at_ref`/`at_mut`、条件实现 `impl<T: Copy>`）→ **容器全家桶**（红黑树
`Map`/`Set` + 链式 `HashMap`/`HashSet`，带序统计）→ **FFI 三个阶段**（调 C → 数据/所有权边界 →
回调与 `export fn`，含按值聚合体的平台 ABI）→ **借用溯源与优化事实**（`at_ref`/`to_cstr`/`get_ref`
的返回借用绑定到接收者；非入口函数 `internal` linkage、`&mut` 形参 `noalias`）。

**下一步**（详见 `Document/src/limitations.md` 的路线图）：`Vec::with_capacity`/`reserve`、数组的
非 Copy 元素、引用模式 `match &opt`、`Neg`/`Not` 一元运算符重载、复合赋值用于重载类型、
String 的格式化/拼接。**明确不做**：与 LLVM 重叠的各类优化 pass（见 `Document/src/optimization.md`）。

## lis compiler 编译器

整个项目最核心的就是编译器，整个编译器差不多三万多行（不包含第三方库的情况下），
从词法分析、语法分析、语义分析、中间代码生成，使用 LLVM 作为后端，
实现了生命周期检查、借用检查、泛型、模块系统等高级语言功能，我认为是编译原理的生动实践，
同时如果你有编写编译器的志向，阅读我的代码应该是不错的，因为踩过的坑我基本都踩过了一遍，
这些我会放在个人博客里面讲解。

### 编译运行

项目没有使用 Makefile 或 CMake，使用的是我自己的一个编译系统 **LisBuilder**
（独立仓库 `github.com/LiserverYang/LisBuilder`，以 git submodule 挂在 `./Build/`，
构建引擎源码在 `F:\LisBuilder\BuildSystem\`）。编译的入口文件在 `./build.py`，
编译系统用 python 编写，我的设计理念就是足够的模块化和足够的好用。

```bash
# 常规构建（增量正确；测试随构建运行）
python3 build.py --llvm-position F:/LLVM/ --build-type Debug --enable-tests --threads 16

# 全量重建
python3 build.py --llvm-position F:/LLVM/ --build-type Debug --enable-tests --threads 16 --clean

# 运行示例（lisc.exe 依赖 MinGW 运行时 DLL，需 MinGW bin 在 PATH）
./Build/Binaries/lisc.exe Examples/print.lis && g++ -o a.exe a.o && ./a.exe
```

由于编译器使用了 LLVM 作为后端，你必须要保证安装了 llvm，并且在编译时使用
`--llvm-position xxx` 指定 llvm 的位置。LLVM 版本必须大于十五（十五以后才支持
不透明指针），推荐版本是十七。

`--format` 一键格式化代码（clang-format 20），CI 有格式检查。更多使用方式运行
`python3 build.py --help` 查阅。

### 源码结构

所有的编译器有关代码都放在了 `./Source/Compiler` 目录下，所有标准库放在了
`./Source/Std/` 目录下。`./Source/Main` 是入口文件。

`./Source/Compiler` 有两个子文件夹，public 用于存放头文件，private 用于存放源码文件，
同目录下还有一个 `Compiler.build.py`，它用来配置编译时的参数（Source 目录下其他
文件夹也都符合这个架构）。各子文件夹的含义：

- Core：编译器核心组件（Pass、Pipeline、SourcePosition、Context、模块注册表）
- Lexer：词法分析，含 Token 定义
- Logger：诊断日志（GCC 风格源码上下文）
- Parser：语法分析，含 AST 定义与 import 模块加载
- IR：最大的文件夹，HIR/MIR/LLVM IR 的定义与构建器、语义分析、泛型单态化
- Analysiser：类型、符号表、作用域
- Argparser：自研命令行参数解析器
- Tests：1421 个 gtest（词法/语法/借用/运行时端到端/FFI/IR 事实），测试源按 TU 并行编译、测试按物理核分片并行跑；运行时用例在进程内 MCJIT 执行，会 abort 的用例走子进程

这个项目的模块化做的很清晰，你看一眼文件夹的名字就会知道这个模块在干什么，
建议你从 `./Source/Compiler/Private/Core/CompilePipeline.cpp` 这个文件入手，会知道
整个编译器的流程，因为 `Main` 模块下的主函数只是简单的把 Pipeline 启动了而已。

### 编译器架构

采用了类似 rust 的路线：源代码（string）经 Lexer 解析为 TokenStream，经 Parser 生成
AST（Parser 会递归加载 `impt` 的模块文件），HIRBuilder 把 AST 转化为 HIR，
HIRSemanticAnalyzer 做语义分析、借用检查、脱糖与语义补全（推断返回类型、泛型参数），
MIRBuilder 生成 MIR，MIRMonomorphization 单态化全部泛型，LLVMIRBuilder 生成 LLVM IR，
Emitter 输出目标文件。

### 标准库

标准库位于 `./Source/Std/`（构建时复制到 `Build/Binaries/lstdlib/`），
**不再自动预加载 —— 用到的模块显式 `impt`**：

```lis
impt math { max, abs };
impt option { Option, unwrap_or };
impt string { String };
```

| 模块 | 内容 |
|------|------|
| `drop.lis` | `Drop` trait —— RAII 析构协议（值离开作用域时调用 `drop(self)`） |
| `option.lis` | `Option<T>` 枚举（`Some(T)` / `None`）+ `is_some` / `is_none` / `unwrap_or` / `and` / `or` |
| `result.lis` | `Result<T, E>` + 后缀 `?` 错误传播（`unwrap` / `expect` / `unwrap_or` / `is_ok` / `is_err`） |
| `fmt.lis` | `Display` trait —— 打印协议；原语由编译器播种，用户类型写普通 `impl` |
| `io.lis` | `print` / `println` / `flush` / `read_i32` `read_i64` `read_f64` / `read_word` `read_line` `read_rest` / `try_read_*` —— **打印与读取是 Lis 代码**，编译器只给字节流原语 |
| `math.lis` | `Numeric`/`Integer` marker trait、`Copy`、12 个运算符重载 trait（`Add` … `Shr`）、`min` `max` `clamp` `abs` `fabs` `gcd` `lcm` `ipow` `sign` `is_even` `is_odd` `deg_to_rad` `rad_to_deg` `lerp`、常量 `PI`/`TWO_PI`/`HALF_PI`、`sqrt` `floor` `ceil` `round` `trunc` `fract` `cos` `sin` `tan` `exp` `log` `pow` …（浮点族同时有 libm 绑定） |
| `chars.lis` | `is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int` |
| `iterator.lis` | `Iterator<T>` trait、`Range` 迭代器、`range` `sum` `count` `first` `last` `nth` `product` |
| `string.lis` | `String` 堆字符串（`new`/`from_lit`/`push_char`/`push_str`/`index`/`to_cstr`/`into_raw`/`from_raw`/`len`/`cap`，`impl Drop` 恰一次释放；`data`/`len`/`cap` **私有**） |
| `vec.lis` | `Vec<T>` 堆数组（`push`/`pop`/`insert`/`remove`/`resize`/`from_elem`/`at_ref`/`at_mut`/`iter`/`iter_mut`、`Index`/`IndexMut` trait、`impl Drop` 逐元素析构；**元素可为任意类型**） |
| `hash.lis` | `Hash` trait（哈希容器用；编译器给 i32/String 播种） |
| `map.lis` / `set.lis` | 红黑树 `Map<K, V>` / `Set<K>`（**有序**），带序统计 `count_less`/`rank`/`kth`/`pred`/`succ` |
| `hashmap.lis` | 链式 `HashMap<K, V>` / `HashSet<K>`（**无序**，桶序不保证） |
| `ffi.lis` | `lis_alloc` / `lis_free` —— 给 C 侧用的分配器（`export fn`） |

编译器只保留一组**不依赖标准库**的原语（这些名字保留，用户 `fn` 同名会被拒绝）：

- **字节流**：`__read_byte`（`fgetc(stdin)`）/ `__write`（`fwrite`）/ `__flush`（`fflush`）—— `io` 模块建在它们之上
- **堆**：`__alloc` / `__free` / `__memcpy` / `__strlen`（libc），**仅标准库可用**（E3013/E3014）
- **转换**：`to_string_i32/i64/f64/bool/char -> String` —— malloc + sprintf
- **终止与断言**：`panic` / `assert`；**C 串**：`str_len` / `str_cmp`

完整清单见 `Document/src/builtins.md`。

### 编译器的报错

借鉴了 gnu gcc 风格，带错误码与源码上下文：

```
./Examples/example.lis:73:18: info: useless cast from 'int32' to 'int32'.
    73 |     let x: i32 = fib(4) as i32;
       |                  ^~~~~~~~~~~~~
```

## 语言目前实现了什么

- **类型系统**：struct、enum（带载荷的 tagged union，配合 `match`）、泛型函数与泛型
  类型（单态化）、trait 与泛型约束（`T: Numeric` / `T: Iterator<i32>`）、引用
  `&T` / `&mut T`、**裸指针 `*T` / `*mut T`**、函数指针、数组 `[T; N]`；
  字段可见性 `pub` / 私有由编译器强制（E3015）
- **所有权与安全**：move 语义、borrow checker（NLL 非词法生命周期）、悬垂引用检测、
  字段级借用精度、**返回引用的借用溯源**（`at_ref`/`to_cstr`/`get_ref` 的借用绑定到接收者）、
  drop glue（RAII，含枚举 tag-aware 析构）、数组越界运行时 abort、`&mut T` 独占（非 Copy，
  配套隐式重借用）、堆的不安全操作收敛在标准库内
- **控制流**：`if` / `while` / `for`（走 `Iterator` trait）/ `match`（穷尽性检查、
  载荷绑定、match 表达式）
- **运算符重载**：12 个运算符 trait，`a + b` 对实现 `Add` 的 struct 自动改写为
  `a.add(b)`（泛型算子 `fn sum<T: Add>` 同时支持 struct 与原语）
- **模块系统**：显式 import、模块隔离命名空间、循环导入检测（见上文）
- **标准库**：15 个模块，含堆字符串 `String`、任意元素的 `Vec<T>`、有序 `Map`/`Set`、
  无序 `HashMap`/`HashSet`、`print`/`read_*` —— 打印与读取本身就是 Lis 代码
- **FFI**：`extern "C"` 调 C、`export fn` 被 C 调、回调、`#[repr(C)]` 与按值聚合体的平台 ABI、
  `--shared` 出动态库（默认关闭，用户代码要 `--allow-ffi`）
- **编译管线**：Lexer → Parser（含模块加载）→ HIR → MIR → 借用检查 → 泛型单态化 → LLVM IR

语言的具体用法请查阅 `Examples/` 目录下的示例文件与 `Document/` 语言参考。

lis 语言更多是我个人的一个编译原理实战，所以很多细节有待打磨。不过作为一个编译器的
入门项目我觉得已经足够拿上台面：完整的编译管线、Rust 式安全模型、模块系统、FFI、一千四百多个
测试——作为一个大型工程来说，不论是语言的设计还是编译的实现都还有很大的空间，
更多的限制是我的精力，因为整个编译器由我一个人开发，享受我的代码吧！
