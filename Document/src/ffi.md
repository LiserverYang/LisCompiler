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
| `bool` | ❌ | Lis 的 bool 是一位；传 i32（0/1） |
| `char` | ❌ | Lis 的 char 是 32 位，C 的是一字节 |
| `String` / `Vec` / struct / enum / 数组（按值） | ❌ | 拥有资源或布局未定；需要显式的裸指针 API |
| 函数类型（回调） | ❌ | 阶段 3 |

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

## 链接（Linux 的 libm）

编译器只产出 `.o`，链接在构建/判题侧完成。平台差异只出现在这一步：glibc 2.34 之前，
`exp/log/pow` 这些符号在 `libm` 里，链接时需要显式 `-lm`；之后它们已并入 `libc`，
不再需要。当前实现**不注入任何链接参数**（`#[link]` 属于后续阶段），所以

- 新发行版（glibc ≥ 2.34，含现代 LOJ 镜像）：``math.exp`` 直接可用。
- 旧发行版：可能出现 `undefined reference to 'exp'`，由构建/判题侧补 `-lm`，
  或实现 `!llvm.linker.options` 按需注入（届时由 `#[link]` 语法暴露）。

判题默认关闭 FFI，用户代码自身不会因为这条而受影响。

## 本阶段明确不做

回调（函数指针形参）、导出（`export fn` / cdylib）、所有权转移
（`into_raw` / `from_raw` / `lis_free`）、不透明句柄、`#[link]`/链接参数、
`#[repr(C)]` 与布局见证、线程与重入契约、C++ ABI。判题沙箱也**不会**开启 FFI。
