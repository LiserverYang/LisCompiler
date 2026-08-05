# Lis Language Standard Compiler Lis语言标准编译器

这个项目实现了 lis 语言的编译器以及运行时标准库；广义地说，实现了 lis 语言。

## lis 语言

有关 lis 语言的具体例子，可以参考 `Examples` 目录下的所有 `.lis` 文件，这些示例都有比较详尽的注释以及编译器的行为。

lis 语言的设计初衷是实现一个 rust 和 c++ 的融合体，
通过引入 rust 的所有权和声明周期机制保证了安全。所以可以说，
安全同样也是 lis 语言的一个设计理念。

我相信如果你有 c++ / rust 语言的基础，入门这个小编程语言是绰绰有余的，试试它吧！

## lis compiler 编译器

整个项目最核心的就是编译器，整个编译器差不多两万多行（不包含第三方库的情况下），
从词法分析、语法分析、语义分析、中间代码生成，使用 LLVM 作为后端，
实现了生命周期检查、借用检查、泛型等高级语言功能，我认为是编译原理的生动实践，
同时如果你有编写编译器的志向，阅读我的代码应该是不错的，因为踩过的坑我基本都踩过了一遍，
这些我会放在个人博客里面讲解。

### 编译运行

项目没有使用 Makefile 或 CMake，使用的是我自己的一个编译系统，它在 `./Build/BuildSystem` 一千多行，
但是非常好用，编写过程从来没有遇到过耦合性或者编译、链接方面的问题，编译的入口文件在 `./build.py`，
显而易见，编译系统是用 python 写的，我的设计理念就是足够的模块化和足够的好用，所以我相信你简单看看就知道它的构成。

使用 `python3 build.py` 开始编译，可以通过 `--threads xxx` 来指定线程数量，

由于编译器使用了 LLVM 作为后端，所以你必须要保证安装了 llvm，并且在编译时使用
`--llvm-position xxx` 指定 llvm 的位置。

LLVM 版本必须大于十五，因为十五以后才开始支持不透明指针，同时推荐版本是十七。

一个最小可用的编译命令是 `python3 build.py --llvm-position C:/LLVM/`

更多的使用方式运行 `python3 build.py --help` 查阅。

### 源码结构

所有的编译器有关代码都放在了 `./Source/Compiler` 目录下，所有标准库放在了 `./Source/Std/` 目录下。
`./Source/Main` 是入口文件。

`./Source/Compiler` 有两个子文件夹，public 用于存放头文件，private 用于存放源码文件
，同目录下还有一个 `Compiler.build.py`，它用来配置编译时的参数（实际上 Source 目录下其他文件夹也都符合这个架构）

你可以看到 `./Source/Compiler` 下不论是 public 还是 private 都有若干个子文件夹，它们的含义分别是：

- Core：存储编译器的一些核心组件，例如 Pass、Pipeline、SoucrePosition 等类
- Lexer：存储了词法分析有关的代码，也包含了 Token 的定义
- Logger：日志有关的代码
- Parser：语法分析有关的代码，也包含了 AST（抽象语法树）的定义
- IR：目前最大的文件夹，存储了 HIR、MIR 的定义，以及 HIR、MIR、LLVMIR 的构建器，以及所有在 IR 上做的工作（语义分析、泛型单态化）
- Analysiser：存储了语义分析用到的类（例如运行时的类型、符号表、作用域），由于语义分析在 HIR，所以真正的语义分析器在 IR 目录下
- Argparser：自己写的一个类似 python 中 argparser 库的一个解析器

这个项目的模块化做的很清晰，你看一眼文件夹的名字就会知道这个模块在干什么，
建议你从 `./Source/Compiler/Private/Core/CompilePipeline.cpp` 这个文件入手，会知道整个编译器的流程，
因为 `Main` 模块下的主函数只是简单的把 Pipeline 启动了而已，这个文件接替了主入口的作用。 

### 编译器架构

采用了类似 rust 的路线，源代码->tokenstream->AST->HIR->MIR->LLVMIR的一条龙服务。
首先源代码（string）经过 Lexer 被解析为 TokenStream(`std::vector<Token>`)，接着经过 Parser 被解析为 AST（抽象语法树）。
AST 会首先被 HIRBuilder 转化为 HIR(High level IR)， HIRSemanticAnalyzer（HIR语义分析器）会对生成的 HIR 做语义分析、脱糖以及补全语义信息（如推断返回类型、变量类型、泛型参数）
随后 HIR 会被送到 MIRBuilder 生成 MIR，随后 MIRMonomorphization 会把代码中的所有泛型全部单态化，接着由 LLVMIRBuilder 生成 LLVMIR，送给 Emitter 生成二进制文件。

### 标准库

标准库位于 `./Source/Std/`（构建时复制到 `Build/Binaries/lstdlib/`，编译器启动时自动预加载，无需 import）。目前包含 5 个模块：

| 文件 | 内容 |
|------|------|
| `drop.lis` | `Drop` trait —— RAII 析构协议（值离开作用域时调用 `drop(self)`） |
| `option.lis` | `option<T>` 枚举（`some(T)` / `none`）+ `is_some` / `is_none` / `unwrap_or` / `and` / `or` |
| `math.lis` | `Numeric`/`Integer` marker trait、12 个运算符重载 trait（`Add` `Sub` `Mul` `Div` `Rem` `PartialEq` `PartialOrd` `BitAnd` `BitOr` `BitXor` `Shl` `Shr`）、`min` `max` `clamp` `abs` `fabs` `gcd` `lcm` `ipow` `sign` `is_even` `is_odd` `deg_to_rad` `rad_to_deg` `lerp` |
| `char.lis` | `is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int` |
| `iterator.lis` | `Iterator<T>` trait、`Range` 迭代器、`range` `sum` `count` `first` `last` `nth` `product` |

此外编译器内置一组 I/O 函数（无需声明即可直接调用）：

- **输出**：`print_str` `print_int` `print_float` `print_bool` `print_char` `println` —— 降为 libc `printf`
- **输入**：`read_line() -> &i8` `read_int() -> i32` `read_f64() -> f64` —— 内部 `fgets` + 解析，共享 256 字节缓冲（每次读取会覆盖上一次结果）

### 实现了什么、下一步干什么

语言层面目前已实现：

- **类型系统**：struct、enum（带载荷的 tagged union，配合 `match`）、泛型函数与泛型类型（单态化）、trait 与泛型约束（`T: Numeric` / `T: Iterator<i32>`）、引用 `&T` / `&mut T`、函数指针
- **所有权与安全**：move 语义、borrow checker（词法借用 + NLL 非词法生命周期）、悬垂引用检测、字段级借用精度、drop glue（RAII，含枚举 tag-aware 析构）
- **控制流**：`if` / `while` / `for`（走 `Iterator` trait）/ `match`（穷尽性检查、载荷绑定、match 表达式）
- **运算符重载**：12 个运算符 trait，`a + b` 对实现 `Add` 的 struct 自动改写为 `a.add(b)`（泛型算子 `fn sum<T: Add>` 同时支持 struct 与原语）
- **编译管线**：Lexer → Parser → HIR → MIR → 泛型单态化 → LLVM IR；语义分析、生命周期检查、借用检查全部在 HIR 阶段完成

编译器的报错提示借鉴了 gnu gcc，例如：

./Examples/example.lis:73:18: info: useless cast from 'int32' to 'int32'.
    73 |     let x: i32 = fib(4) as i32;
       |                  ^~~~~~~~~~~~~

语言的具体用法请查阅 `Examples/` 目录下的示例文件，每个示例都配有注释：

- `print.lis` —— 输出内置函数 + 数学助手
- `io.lis` —— 标准输入（`read_int` / `read_line`）
- `match.lis` —— enum 与模式匹配
- `operator.lis` —— 运算符重载
- `iterator.lis` —— 迭代器与 for 循环
- `borrow.lis` / `ownership.lis` / `method_ref.lis` / `drop.lis` / `example.lis` —— 所有权 / 借用 / 析构等

**下一步方向**：`to_string` / 格式化（需先实现 String 类型与堆分配）、`panic` / never 类型（解锁 `unwrap` / `expect`）、数组 / 堆 / `Vec`、引用模式 match、模块 / import。

lis 语言更多是我个人的一个编译原理实战，所以很多细节有待打磨。
不过作为一个编译器的入门项目我觉得已经足够拿上台面，作为一个大型工程来说，
不论是语言的设计还是编译的实现都是有待商榷的，更多的限制是我的精力，因为整个编译器由我一个人开发，
享受我的代码吧！