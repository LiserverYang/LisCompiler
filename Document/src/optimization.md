# 优化：语言事实与 LLVM 的分工

<!-- grammar_name: optimization, search_name: 优化,noalias,internal,linkage,属性 -->

Lis 把**整个程序**（标准库 + 用户代码）下降成**一个** LLVM module，再交给 LLVM 的 O2 全管线
（`Emitter::runOptPipeline`，`-o N` 控制，默认 2）。也就是说内联、常量折叠、CSE、LICM、
向量化、死代码、边界检查消除这些经典优化**已经在做**，而且因为是一个 module，等价于全量 LTO。

实测（`Examples/fft_bigint.lis`，`opt -passes='default<O2>'` 后逐函数展开属性组）：LLVM 的
`FunctionAttrs` 已经自己推出 `mustprogress`、`nounwind`、`willreturn`、`memory(...)`，以及
参数的 `nocapture` / `readonly` / `nonnull` / `noundef` / `dereferenceable`。

## 政策：只补 LLVM 推导不出来的

前端**不重复实现**任何 LLVM 已有的优化。只把「只有语言知道」的事实写成 IR 事实，一共两条。

### 1. 链接性：只有 `main` 与 `export fn` 是 C 可见的

语言事实：C 只能按名字拿到 `main` 和 `export fn` 的符号（`#[link_name]` 改名后也是这个名字）。
其余函数一律 `internal`（含编译器合成的 `__drop_*`）。

收益（同一份源码、`lisc -o 2`、改动前后两个编译器都在手边）：

| | 改前 | 改后 |
|---|---|---|
| 目标文件 | 25563 B | **7203 B** |
| `internal` 定义数 | 0 | 109（只剩 `main` 是外部） |
| 优化前 IR | 8201 行 | 8201 行（只差链接性） |
| 运行（15 万位输入，7 次取最小） | 123.9 ms | 121.3 ms |

道理是 `globaldce`：**internal 且没人调用**的函数可以被删掉，external 的不行。此前一个只用
几个标准库例程的程序会把上百个函数全部编进目标文件。程序输出逐字节不变。

### 2. `noalias`：`&mut T` 是进入该对象的唯一入口

借用检查证明的**独占性**是 LLVM 的别名分析无法重新推导的（实测：整个 module 里 `noalias`
只出现 3 次，且与形参无关）。

它是**对每个调用方**的承诺，所以四条规则缺一不可，每条都有反例测试
（`Source/Compiler/Tests/IrFactsTest.cpp`）：

| 规则 | 反例（都是合法 Lis 程序） |
|---|---|
| 只给 `&mut T`，**不给 `&T`** | `f(&v, &v)`：两个共享借用可以别名 |
| 该函数**只有一个**指针类形参，且就是它 | `f(&mut x, &x)`：二相借用让被预留的 `&mut x` 仍可被子弟实参共享借用 |
| 同上，判据**递归**（结构体字段、枚举载荷、数组元素） | `VecIter { src: &v }` 这类「值里夹带引用」的聚合体形参 |
| 函数体**不访问全局** | `fn f(a: &mut Vec<i32>) { G.push(1); }` 被 `f(&mut G)` 调用 |
| 函数**不是** `export fn` | C 调用方不受借用检查约束，可以传同一个指针两次 |

实测（同一份源码、改前=只有链接性、改后=链接性+`noalias`）：

| | 改前 | 改后 |
|---|---|---|
| `fft_bigint` 目标文件 | 7203 B | 7153 B |
| `fft_bigint` 运行（15 万位，7 次取最小） | 113.4 ms | 110.7 ms |
| `Vec<i32>` 2000 万次 push+求和 | 66.6 ms | 67.0 ms（噪声内） |

落点（`--print-llvmir` 可见）：`vec$Vec::push`、`vec$Vec::grow`、`string$String::push_char`、
`iterator$Range::next`、`string$String::into_raw` 的 `self` 参数。**收益约 2%，不是数量级的**，
因为热代码大多被内联、Vec 头也已经被 SROA 拆进寄存器；写在这里是为了诚实：这条事实正确且便宜，
但不是性能主力。

## 明确不做

| 不做 | 原因 |
|---|---|
| `mustprogress` / `nounwind` / `willreturn` | LLVM 已经自己推断（实测），再加是纯重叠 |
| `dereferenceable` | **仍不是事实**：穿过解引用的借用（标准库内部的 `self.field.at(i)`）与裸指针不受追踪，且该属性要对**每个**调用方成立（含 `export fn`） |
| `nonnull` / `noundef` | 已被部分推断，剩余场景收益≈0；`noundef` 还会被聚合体的 padding 破坏 |
| 前端自建常量折叠/CSE/内联/边界检查消除 | 与 LLVM 重叠，收益为负 |
| 全局变量的链接性 | 语义相同但暂无测量支撑，留待以后 |
