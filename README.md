# Lis Language Standard Compiler Lis语言标准编译器

这个项目实现了 lis 语言的编译器以及运行时标准库；广义地说，实现了 lis 语言。

## lis 语言

lis 语言的设计初衷是实现一个 rust 和 c++ 的融合体，通过引入 rust 的所有权和生命周期
机制保证了内存安全。所以可以说，**安全**同样也是 lis 语言的一个设计理念。

有关 lis 语言的具体例子，可以参考 `Examples` 目录下的所有 `.lis` 文件，这些示例都有
比较详尽的注释。完整、权威的语言参考在 `Document/`（mdBook，`mdbook build` 构建，
涵盖词法/类型/声明/语句/表达式/所有权/借用/泛型/match/运算符/内置/标准库/错误码/
已知限制）。

我相信如果你有 c++ / rust 语言的基础，入门这个小编程语言是绰绰有余的，试试它吧！

## 项目现在的位置（2026-08-13）

语言已具备写真实多文件程序的能力：

| 能力 | 状态 |
|------|------|
| 类型系统 | struct、enum（fat tagged union + `match`）、泛型（单态化）、trait 与约束、引用 `&T`/`&mut T`、函数指针、数组 `[T; N]`、String 堆字符串 |
| 安全模型 | move 语义、borrow checker（NLL + 字段级精度）、悬垂引用检测（E4007）、drop glue（RAII，tag-aware）、数组越界运行时检查（abort） |
| 运算符 | 12 个运算符 trait 重载，泛型算子（`fn sum<T: Add>` 对 struct 与原语分流） |
| 标准库 | 6 个模块（drop/option/iterator/math/string/chars），**显式 `impt` 导入** |
| **模块系统** | **2026-08-13 完成**：`impt lib.nums;` / `impt math as m;` / `impt math { max };`，模块隔离命名空间、循环导入检测、搜索路径（lstdlib 优先 → `-I` → 主文件目录） |
| 内置 | print/read/堆（`__alloc` 系）/`to_string_*`；`#[i_know]` 属性放行窄化 cast |
| 诊断 | GCC 风格带源码上下文的错误（E1xxx 词法 ~ E5xxx match），解析错误可恢复 |
| 测试 | **1035 个 gtest 全绿**；12 个 Examples 输出为回归基线（borrow 55/iterator 23/match 8/…） |

**下一步**（按可用性优先级，详见 `Document/src/limitations.md`）：panic/never 类型
（解锁 `unwrap`/`expect`/`Result`）→ 编译期拒绝未初始化读取 → `move` 语义 →
Vec/堆集合 → 数组精确索引路径 → extern/FFI → 一元运算符。

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
python build.py --llvm-position F:/LLVM/ --build-type Debug --enable-tests --threads 16

# 全量重建
python build.py --llvm-position F:/LLVM/ --build-type Debug --enable-tests --threads 16 --clean

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
- Tests：1035 个 gtest（词法/语法/借用/运行时端到端）

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
| `math.lis` | `Numeric`/`Integer` marker trait、12 个运算符重载 trait（`Add` … `Shr`）、`min` `max` `clamp` `abs` `fabs` `gcd` `lcm` `ipow` `sign` `is_even` `is_odd` `deg_to_rad` `rad_to_deg` `lerp` |
| `chars.lis` | `is_digit` `is_alpha` `is_alphanumeric` `is_whitespace` `digit_to_int` |
| `iterator.lis` | `Iterator<T>` trait、`Range` 迭代器、`range` `sum` `count` `first` `last` `nth` `product` |
| `string.lis` | `String` 堆字符串（`new`/`from_lit`/`push_char`/`push_str`/`index`/`to_cstr`，`impl Drop` 恰一次释放） |

此外编译器内置一组 I/O 函数（所有模块裸名可用，无需声明）：

- **输出**：`print_str` `print_int` `print_float` `print_bool` `print_char` `println` —— 降为 libc `printf`
- **输入**：`read_line() -> &i8` `read_int() -> i32` `read_f64() -> f64` —— 共享 256 字节缓冲（每次读取覆盖上一次结果）
- **堆**：`__alloc`/`__free`/`__memcpy`/`__strlen` —— libc malloc/free/memcpy/strlen
- **转换**：`to_string_i32/i64/f64/bool/char -> String` —— malloc + sprintf

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
  `&T` / `&mut T`、函数指针、数组 `[T; N]`
- **所有权与安全**：move 语义、borrow checker（NLL 非词法生命周期）、悬垂引用检测、
  字段级借用精度、drop glue（RAII，含枚举 tag-aware 析构）、数组越界运行时 abort
- **控制流**：`if` / `while` / `for`（走 `Iterator` trait）/ `match`（穷尽性检查、
  载荷绑定、match 表达式）
- **运算符重载**：12 个运算符 trait，`a + b` 对实现 `Add` 的 struct 自动改写为
  `a.add(b)`（泛型算子 `fn sum<T: Add>` 同时支持 struct 与原语）
- **模块系统**：显式 import、模块隔离命名空间、循环导入检测（见上文）
- **编译管线**：Lexer → Parser（含模块加载）→ HIR → MIR → 泛型单态化 → LLVM IR

语言的具体用法请查阅 `Examples/` 目录下的示例文件与 `Document/` 语言参考。

lis 语言更多是我个人的一个编译原理实战，所以很多细节有待打磨。不过作为一个编译器的
入门项目我觉得已经足够拿上台面：完整的编译管线、Rust 式安全模型、模块系统、一千多个
测试——作为一个大型工程来说，不论是语言的设计还是编译的实现都还有很大的空间，
更多的限制是我的精力，因为整个编译器由我一个人开发，享受我的代码吧！
