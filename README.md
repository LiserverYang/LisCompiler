# Lis Language Statnderd Compiler Lis语言标准编译器

这个项目实现了 lis 语言的编译器以及运行时标准库；广义地说，实现了 lis 语言。

## lis 语言

有关 lis 语言的具体例子，可以参考 `Examples` 目录下的所有 `.lis` 文件，这些示例都有比较详尽的注释以及编译器的行为。

lis 语言的设计初衷是实现一个 rust 和 c++ 的融合体，
通过引入 rust 的所有权和声明周期机制保证了安全。所以可以说，
安全同样也是 lis 语言的一个设计理念。

我相信如果你有 c++ / rust 语言的基础，入门这个小编程语言是绰绰有余的，试试它吧！

## lis compiler 编译器

整个项目最核心的就是编译器，整个编译器差不多一万三千多行（不包含第三方库的情况下），
从词法分析、语法分析、语义分析、中间代码生成，使用 LLVM 作为后端，我认为是编译原理
的生动实践，同时如果你有编写编译器的志向，阅读我的代码应该是不错的，因为踩过的坑我基本都踩过了一遍，
这些我会放在个人博客里面讲解。

### 编译运行

项目没有使用 Makefile 或 CMake，使用的是我自己的一个编译系统，它在 `./Build/BuildSystem` 一千多行，
但是非常好用，我一万多行编写过程从来没有遇到过耦合性或者编译、链接方面的问题，编译的入口文件在 `./build.py`，
显而易见，编译系统是用 python 写的，我的设计理念就是足够的模块化和足够的好用，所以我相信你简单看看就知道它的构成。

使用 `python build.py` 开始编译，可以通过 `--threads xxx` 来指定线程数量，

由于编译器使用了 LLVM 作为后端，所以你必须要保证安装了 llvm，并且在编译时使用
`--llvm-position xxx` 指定 llvm 的位置。

LLVM 版本必须大于十五，因为十五以后才开始支持不透明指针，同时推荐版本是十七。

一个最小可用的编译命令是 `python build.py --llvm-position C:/LLVM/`

更多的使用方式运行 `python build.py --help` 查阅。

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

### 实现了什么、下一步干什么

这一部分请查阅 `Example/example.lis`，它基本涵盖了目前语言的所有语法。

总的来说，目前实现了 struct、trait、函数 这些基本的语法，完整的类型分析和语义检查，简单的生命周期检测，泛型函数和泛型类型。

下一步是实现泛型 trait，随后补全标准库，挑战生命周期这个 boss。

lis 语言更多是我个人的一个编译原理实战，所以很多细节有待打磨。
不过作为一个编译器的入门项目我觉得已经足够拿上台面，作为一个大型工程来说，
不论是语言的设计还是编译的实现都是有待商榷的，更多的限制是我的精力，因为整个编译器由我一个人开发，享受我的代码吧！