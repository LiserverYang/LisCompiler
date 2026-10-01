# FFI：调用 C（阶段 0，2026-09-26）

> `extern "C"` 让 Lis 调用 C 函数（libc / libm / 自己的 C 库）。这是语言里**唯一**
> 编译器无法验证的边界，所以它被三件东西围起来：**能力闸**（默认拒绝）、**类型白名单**、
> **每个 C 符号一份签名**。边界之外的代码不会因为 FFI 而变弱。

## 语法

<grammar>
extern_declaration = "#[link_name = STRING]"? "extern" STRING "fn" IDENTIFIER "(" parameters ")" ("->" type)? ";"
parameters         = [ parameter ("," parameter)* ["," "..."] ]
</grammar>

```lis
extern "C" fn strlen(s: &i8) -> i64;                  // 名字既是 Lis 名也是 C 符号名
extern "C" fn snprintf(buf: &mut i8, n: i64, fmt: &i8, ...) -> i32;   // C 变参

#[link_name = "exp"] extern "C" fn cExp(x: f64) -> f64;   // Lis 名 ≠ C 符号名
fn exp(x: f64) -> f64 { ret cExp(x); }
```

- 只能出现在**顶层**，且**没有函数体**（有体会报错）。
- 不能是泛型；不能有默认参数。
- `#[link_name = "..."]` 指定 C 符号；缺省时用声明自己的名字。库包装 libc 时必须用它：
  模块内 `fn exp` 与 extern `exp` 同名会互相遮蔽，包装体将无限递归。

## 能力闸（fail-closed）

| 位置 | 能否声明 extern |
|---|---|
| 标准库（`Context::stdLibDirs` 之内的文件） | **可以**（平台自己的绑定：libm、libc） |
| 其他文件，编译时带 `--allow-ffi` | 可以（库作者 / 本地开发） |
| 其他文件，无该选项（**判题默认**） | 拒绝：**E3019** |

判题机上不开 `--allow-ffi`，选手因此拿得到 `pow()`（走标准库），拿不到 `system()`。

## 类型白名单（E3020）

| 形参 / 返回值 | 允许 | 说明 |
|---|---|---|
| `i8` `i16` `i32` `i64` `f32` `f64` | ✅ | 与 C 同宽 |
| `void` | ✅（仅返回） | 「不返回东西」 |
| `&T` / `&mut T` | ✅ | 下降为指针；**借用检查保证调用期间有效** |
| `*T` / `*mut T` | ✅ | 没有任何生命周期保证，所有权须自己讲清 |
| `&i8` | ✅ | 本语言的 C 串 |
| `bool`（标量） | ✅ | 按 C 的 `_Bool` 传：声明用 i8，调用点 zext/trunc（一位 vs 一字节的差异关在这里） |
| `bool` 作为 `#[repr(C)]` 字段 | ❌ | 结构体里一字节的差会挪动后面每个字段；存 i8 |
| `char` | ❌ | Lis 的 char 是 32 位，C 的是一字节；显式 `c as i8` |
| struct | ⚠️ 只能**指针** | `&T`/`&mut T`/`*T`/`*mut T`；**按值未实现**（见下节），空的 struct 就是 C 的 opaque 句柄 |
| `String` / `Vec` / enum / 数组（按值） | ❌ | 拥有资源或布局非 C；走 `into_raw`/`from_raw`（所有权）或指针 |
| 函数类型（回调） | ✅ | 只能当形参；签名自身也要过白名单（非变参） |

## 数据边界：`#[repr(C)]`、按值、不透明句柄

- **struct 只能通过指针过边界**。按值传递需要把平台聚合体 ABI 在 IR 层落下来：
  clang 会做这个强制转换（8 字节结构体变 i64、12 字节变 byval 指针），而 LLVM 后端**不会**
  从结构体类型自己推导。实测（本仓库 2026-09-26）：把结构体类型直接写进调用的 IR，
  C 侧**只收到第一个字段**——连 `{i32,i32}` 都是 `{1, 0}`——12 字节的结构体会崩。
  所以按值一律报 E3020 并给出可执行的替代：`&T`/`&mut T`（调用期间有效）或
  `*T`/`*mut T`（可保存）。
- **`#[repr(C)]`** 是**布局见证**：声明「C 可以通过指针读写这些字段」。编译器本来就按声明顺序、
  自然对齐、非 packed 布字段，所以它不是新的布局算法；它为真的是**字段白名单**（E3021）：
  `bool`（1 位 vs 1 字节）、`char`（32 位 vs 1 字节）、String/Vec、enum（本语言的 enum 是带标签联合）、
  以及**没有同样承诺的嵌套 struct** 都被拒绝并说明原因。贴错地方同样报错：
  enum、函数、泛型 struct、**空 struct**（没有布局可见证）。
- **不透明句柄**：空 struct（`struct File { }`）+ `*mut File`。它没有布局，
  所以只能当句柄用——`#[repr(C)]` 拒绝空 struct，这条规则让「句柄」在语言里是清晰的一类。
  典型用法见测试里的 `fopen/fputs/fgets/fclose`。
- **布局见证是许可，不是证明**：编译器验证的是可达性（字段类型），不是你手上的 C 头文件。
  标准做法是写一个 C 侧见证测试：让 C 报 `sizeof/offsetof` 并**按 C 的偏移读一遍字段**。

## 所有权转移：把缓冲区交给 C

```lis
let mut s = String::from_lit("hello");
let p = s.into_raw();          // 所有权离开语言；s 变成合法的空 String
lis_upper(p);                  // C 就地改写
let back = String::from_raw(p, 5, 6);   // 收养回来（len 字节 + NUL，cap 含 NUL）
```

- `String::into_raw(&mut self) -> *mut i8` / `String::from_raw(data, len, cap) -> String`；
  `Vec<T>::into_raw(&mut self) -> *mut T` / `Vec<T>::from_raw(data, len, cap) -> Vec<T>`。
- 源对象**不会**被遗忘而是变成合法的空值：它的 Drop 会无条件释放 `data`，
  留着旧指针就会二次释放。
- 收养的 Vec：存活区间是 `[0, len)`，析构时按 T 自己的析构函数释放元素——`Vec<String>`
  从 C 回来也能正确清理。
- 没有任何检查：指针必须来自 Lis 分配（或 C 为这个目的分配的块）。
  `__alloc` 就是 malloc，C 直接 `free()` 也能工作；但**推荐** `lis_free`
  （`impt ffi` 的导出符号），这样分配器留在边界之内，将来换实现不破 ABI。

## 反向边界：`export fn`

```lis
export fn add(a: i32, b: i32) -> i32 { ret a + b; }
#[link_name = "lis_mul"] export fn multiply(a: i32, b: i32) -> i32 { ret a * b; }
```

- 符号名 = `#[link_name]` 或函数本名，**不带模块前缀**（C 查的名字和语言调的名字是同一个串）。
- 与调用 C **同一套纪律**：能力闸（E3019，stdlib 或 `--allow-ffi`）、类型白名单（E3020）、
  **一个符号一份定义**（E3022：两处导出同名、或导出名撞上编译器自己发的符号如 `assert_fail`/`__show_*`）。
- C 用不了的东西在解析期就拒绝：泛型、变参尾、没有函数体。
- `impt ffi;` 提供 `lis_alloc(i32) -> *mut i8` 与 `lis_free(*mut i8)`。
- **cdylib 配方**：编译器只产 `.o`（输出名固定），所以不加产物选项：
  `g++ -shared -o libfoo.so foo.o`。
- panic 跨边界 = `abort()`（既有行为）；C 侧看不到 Lis 的异常。

## 回调

```lis
extern "C" fn qsort(base: *mut i32, n: i64, size: i64, f: fn(&i32, &i32) -> i32) -> void;
fn cmp(a: &i32, b: &i32) -> i32 { ret *a - *b; }
qsort(&mut xs[0], 6 as i64, 4 as i64, cmp);
```

- `fn(A, B) -> R` 是**类型语法**（缺箭头 = void）。函数类型的值一直有（`let f = foo;` 靠推断），
  这是第一次能把类型写出来——而 FFI 声明正需要它。
- 回调签名自己也要过白名单（形参、返回类型、非变参）：`fn(String) -> i32` 会被拒。
- **extern 名也可以是值**（把 C 函数当回调传给 C）：地址物化用的是它自己的签名。
- 契约：**C 可以长期保存回调指针**——Lis 函数是静态符号，安全；而借用的 `&T` 实参不是，
  这正是指针/引用之分已经表达的意思。

## 借用 vs 裸指针

- **借用形式**（`&T` / `&mut T`）：指针只活在这次调用里，**C 不得保存它**。这是文档契约，
  编译器保证的是「Lis 侧在这次调用期间不会动它」——包括 `&mut` 的独占性
  （`memcpy(&mut a, &a, n)` 这类自别名调用在 Lis 侧就编译不过）。
- **裸指针形式**（`*T` / `*mut T`）：要保存指针、或者要讲所有权，就必须写成裸指针，
  并自己保证合法性（读取仍需要标准库的 `__deref`，用户代码里不能解引用裸指针）。

## 变参

`...` 之后只能传 **i32 / i64 / f64 / 指针 / &i8**：C 的默认实参提升会把 `i8`/`i16` 变成
`int`、`f32` 变成 `double`，所以这两种在 `...` 里被**拒绝**并要求显式 `as`。
调用点只对固定形参做类型检查，变参部分逐个过白名单；数量上要求 ≥ 固定形参个数。

## 错误码

- **E3019** `extern "C"` 声明需要 FFI 能力（标准库之外且未加 `--allow-ffi`）。
- **E3020** 类型不能过 C 边界 / 变参实参被提升 / extern 被当作值使用 / 同一 C 符号的签名冲突。

## 例子

```lis
extern "C" fn strlen(s: &i8) -> i64;
extern "C" fn fmod(x: f64, y: f64) -> f64;

fn main() -> i32 {
    if strlen("hello") == 5 as i64 { print(fmod(7.5, 2.0)); }   // 1.500000
    ret 0;
}
```

标准库自己的用法见 `Source/Std/math.lis` 的 libm 一节：`#[link_name]` + 一层薄包装，
`exp/log/log2/log10/pow/atan/atan2/asin/acos/sinh/cosh/tanh` 都是这么来的。

## 链接

`#[link(name = "m")]` 是**文件级**属性：它把请求写进对象文件
（`llvm.linker.options` 命名元数据，`!llvm.linker.options = !{!0}`，`!0 = !{!"-lm"}`）。

- **lld 认它**；**GNU ld 不认**（MinGW 链接时可能顺带打一句
  `Warning: corrupt .drectve at end of def file`，无害）。
- 因此旧发行版上仍然可能看到 `undefined reference to 'exp'`：glibc 2.34 之前
  `exp/log/pow` 在 `libm` 里，需要在链接命令里补 `-lm`。判题默认关闭 FFI，
  用户代码不受影响；`math` 的 libm 绑定在现代镜像上直接可用。

## 仍未做的部分

- **按值传递聚合体**（Win64/SysV 的聚合体 ABI：整数化与 byval 指针）。这是独立的一段工作，
  需要按目标实现分类规则并做 C 侧差分测试；在此之前 struct 走指针。
- **C++ ABI**（`extern "C++"`、名字修饰、异常）、**线程与重入契约**。
- `#[repr(packed)]` / 自定义对齐、`#[repr(C)]` 的 enum 映射。
- 产物选项（`--emit-shared` 之类）：用 `g++ -shared` 自己链即可。
- **判题沙箱不会开启 FFI**：提交里的 `extern`/`export` 都需要 `--allow-ffi`。

## 按值传结构体（2026-09-26 落地）

`#[repr(C)]` 的结构体现在可以**按值**过边界。规则在 `IR/FfiAbi` 里，且必须在两处一致：
extern 声明与调用点（同一次分类）。

| 目标 | 规则 |
|---|---|
| Win64 x86-64 | 1/2/4/8 字节 → 等宽整数；其余 → **按引用**（调用方给一份副本的地址）；返回值走隐藏首指针（sret） |
| SysV x86-64 | >16 字节 → 同上；≤16 字节 → 逐八字节拆分：整块全是浮点则走 SSE（`float` / `double` / 两个 float 打成 `<2 x float>`），掺进整数就是 INTEGER（按**实际用到的字节数**取整，所以 `{f64,i32}` 是 `(double, i32)`）；一个实参可能变成**两个**形参，两部分的返回值是 `{ i64, i32 }` 这样的字面结构体 |

做法与 clang 一致：实参从它自己的位置**按另一种类型 load**，返回值把各部分**按字节偏移 store 回目标槽位**，
按引用时在入口块 alloca 一份副本（C 可以写它）。

- 验证：`RuntimeTest6.cpp` 的差分矩阵（每个形状都由 g++ 编译的 C 助手独立算同一组数，传参与返回两个方向），
  以及 `FfiSysvAggregateClassification`——它断言 clang 对 `x86_64-unknown-linux-gnu` 打印出的签名。
  本机跑不了 Linux 二进制，SysV 就是这样钉住的（WSL 里有 clang 18 / gcc 15，需要时可端到端再验一次）。
- **导出方向**（C 按值调 Lis）还没做：需要在被导出函数体内反向拆/装 ABI 实参，
  现在明确报 E3020，而不是让 C 误调一个不知道自己 ABI 的函数。
- packed 结构体永远走指针（字段没有对齐）。

## 线程与重入契约

- **导出函数可重入**：没有隐藏的运行时状态；堆分配器就是 malloc（线程安全）。
- **全局 `let` 是可变的普通存储**（语言里有对它的自增/赋值，见 `RuntimeTest.GlobalIncrementFunction`），
  而且**没有任何同步原语**：多线程同时写同一个全局量就是数据竞争，与 C 里一样。
- IO 经 CRT：单次调用线程安全，但多线程下输出会**交错**。
- `panic` / `assert` / 越界 = `abort()`：终结整个进程，不只是一个线程，也不是异常。

## 调用 C++（不做 mangler，也不做异常）

`#[link_name]` 接受任意符号，所以 C++ 函数用**修饰名**就能调：g++（Itanium ABI）把
`int cpp_add(int, int)` 修饰成 `_Z7cpp_addii`，于是

```lis
#[link_name = "_Z7cpp_addii"] extern "C" fn cpp_add(a: i32, b: i32) -> i32;
```

就调通了——测试 `FfiCppMangledName` 里 C++ 侧故意不用 `extern "C"`，并由它校验结果，
名字写错会链接失败而不是悄悄跑过。成员函数同理：把 `this` 显式写成第一个 `*mut T` 形参。

**不做**：自己算修饰名（不做 mangler）、虚表/继承布局、C++ 异常跨边界（在 C++ 侧写 `extern "C"` wrapper，
或 `-fno-exceptions`）。跨语言栈展开不在本语言的能力范围内。

## 产物

- `--out <path>`：对象文件写到哪里（默认 `./a.o`）。
- `--shared`：顺带用 g++ 把这个对象链成共享库（`<stem>.dll` / `<stem>.so`）。
  普通构建不变——判题仍然自己链接。
