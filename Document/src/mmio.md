# 内存映射 IO：`#[volatile]` 与寄存器访问

<!-- grammar_name: mmio, search_name: 内存映射IO,寄存器,volatile,裸机,STM32,外设 -->

> **状态：已实现。** 本文是这套语义的权威说明；`Document/src/limitations.md` 的路线图记着还没做的部分
> （运行期地址通道、临界区原语、QEMU 上的实机验证）。

## 1. 问题：写寄存器原本写不了

裸机程序的第一步是往**固定地址**写值。在此之前这在 Lis 里表达不出来，而且是三处独立的原因，
缺一不可：

| 缺什么 | 现状 |
|---|---|
| 整数 → 指针 | `as` 的类型表里没有指针行，`0x40021000 as *mut i32` 报「integer can only be cast to primitive type」 |
| 写穿裸指针 | `__deref`/`__deref_mut`/`p[i]` 是标准库专属（E3013/E3014）；写穿 `*T` 被借用检查拒绝；`*mut T → &mut T` 明确禁止 |
| 副作用语义 | 没有 `volatile`：往普通指针存的值会被优化掉，也无法阻止与其它寄存器访问重排 |

**关键判断**：前两条不是「缺个开关」，而是语言刻意的安全设计——`&T`/`&mut T` 只能来自
真实存在的位置，裸指针的权限收敛在标准库内。所以做法不是「开个洞让用户写指针」，而是
**让寄存器成为一种普通的位置（place）**，让已有的所有权与借用规则原封不动地适用。

## 2. 语义：两条轴——**访问方式**与**存储位置**

`#[volatile]` 是**一个属性名、两种用法**，靠有没有参数区分：

- **`#[volatile]`（无参数）标在结构体上** = 经过这个**类型**的每一次访问都是 volatile 的。
  它是「访问方式」。标在类型上而不是位置上，是因为访问会穿过引用、方法接收者与形参——
  `impl GpioRegs { fn set_word(self: &mut Self, w: i32) { self.odr = w; } }` 里的那次写必须是
  volatile 的，而那一刻作用域里只有 `self`，没有全局。
- **`#[volatile(<地址>)]` / `#[volatile(sym = <名字>)]` 标在模块级 `let` 上** = 这个全局的
  **存储就是设备**：地址形式是编译期常量，符号形式由链接脚本 `PROVIDE(NAME = 0x...);` 决定。
  它是「存储位置」，并且**不分配任何存储**。

```lis
#[repr(C)]
#[volatile]
struct GpioRegs
{
    pub moder: i32,
    pub odr: i32,
}

#[volatile(0x4002_0000)]        // 地址形式：编译期常量，不需要链接脚本
let GPIOA: GpioRegs;

#[volatile(0x4002_3800)]        // 单个寄存器：类型就是标量
let RCC_AHB1ENR: i32;

#[volatile(sym = "GPIOA")]      // 符号形式：地址由链接器给（也给测试用）
let GPIOA2: GpioRegs;

impl GpioRegs
{
    fn set_word(self: &mut Self, word: i32)   // 方法体里的访问照样是 volatile 的
    {
        self.odr = word;
    }
}

fn main() -> i32
{
    GPIOA.moder = 0xAB;                    // 普通的字段写，下降成 volatile 存储
    GPIOA.set_word(32);
    RCC_AHB1ENR = RCC_AHB1ENR | 1;         // 读-改-写：读与写都是 volatile 的
    ret 0;
}
```

下降结果（`-o 2`，也就是优化之后——这是关键，`-o 0` 什么都还没优化）：

```llvm
store volatile i32 %load, ptr inttoptr (i32 1073872896 to ptr), align 4
%load3 = load volatile i32, ptr inttoptr (i32 1073887232 to ptr), align 4
```

## 3. 内存安全为什么仍然成立

逐条对照语言已经承诺的保证：

| 保证 | 为什么不受影响 |
|---|---|
| 不悬垂 | 存储是静态的、地址编译期固定，生命周期是 `'static`；不发生所有权转移 |
| 不 double-free / use-after-free | 它从不被分配、也从不被释放；`Drop` 与 `Copy` 都**被禁止**（E3024） |
| 不越界 | 字段访问是结构体投影；数组字段仍走既有的运行时边界检查 |
| 无非法别名（可变别名是 UB） | **它是位置**：`&mut GPIOA` 是独占借用，两个同时存活的可变借用照旧被拒（E4001）；`&GPIOA` 下写照旧被拒（E4003） |
| 无整数→指针逃逸 | 安全代码里**仍然拿不到**「从整数造指针」的能力：地址只出现在属性里，且属性产生的是一个**位置**，不是一个指针值 |

最后一条是支点：**唯一能碰到外设地址的地方是声明**。用户不能计算地址、不能偏移到另一个外设、
不能把地址存进变量。E3013/E3014（堆原语与裸指针操作仅标准库）一个字都没有改。

### 编译器实际拦下的四类写法

| 写法 | 为什么拒绝 | 错误码 |
|---|---|---|
| `#[volatile] struct R`（缺 `#[repr(C)]`） | 布局承诺是寄存器块可信的前提，字段白名单也挂在那里 | E3023 |
| `let g = R { a: 1 };`（构造寄存器块） | 寄存器块不是值，它已经存在于设备地址上 | E3024 |
| `let g = REGS;`（整块复制/移动） | 那会把设备映像搬进普通内存 | E3024 |
| `let p = &mut REGS.a;` / `&RCC`（对寄存器取引用） | 引用的所指类型不是 `#[volatile]`，于是经它访问就不再是 volatile 的——硬件写可能被优化掉 | E3023 |
| `impl Copy` / `impl Drop` for 寄存器块 | 它不拥有内存，也没有值语义 | E3024 |

对**整个寄存器块**取引用是允许的（`&GPIOA`），因为所指类型就是 `#[volatile]` 的：
`impl` 方法因此可以正常写（`GPIOA.set_word(32)`）。

**它不承诺的东西**（写清楚，别让读者以为它管这些）：

- **volatile ≠ 原子**。`GPIOA.odr = GPIOA.odr | 1;` 是 load-or-store，中断插进来就丢位。要原子就写
  单次存储的寄存器（STM32 的 `BSRR`/`BRR` 就是为此设计的），或屏蔽中断（§6）。
- **不做正确性检查**：写只读寄存器、读只写寄存器、位定义写错、时钟没使能——这些是数据手册的
  事。表达它们的正确位置是 HAL 类型上的 `impl`（只提供 `set_*`，不提供 `read_*`），而不是语言。
- **只读寄存器不是编译器保证的**：本语言的全局没有 `mut` 书写形式（赋值是它唯一的可变性机制），
  所以「这个寄存器只读」只能由 HAL 类型不提供写方法来表达。
- **C 侧仍可绕过 Lis 写同一个寄存器**（FFI 契约不变：C 不得保存指针、不得与 Lis 持有的借用别名）。

## 4. 与既有规则的交界

1. **「全局初始化器必须是字面量」与「非 Copy 绑定必须有初始化器」（E3012）各开一个口子**：
   `#[volatile(...)]` 的全局**没有**初始化器，因为它的存储不是我们的。反向堵住：给它写初始化器
   直接报 E3023。
2. **复用 `#[repr(C)]` 的字段白名单（E3021）**：外设类型必须是 `#[repr(C)]`，字段只能是
   标量 / 指针 / 标量数组；`bool`、`char`、`String`、`Vec`、enum、无承诺的嵌套 struct 照旧拒绝。
   `#[repr(C, packed)]` 同样可用（有些寄存器块是紧凑的）。
3. **`#[volatile]` 与 `--allow-ffi` 无关**：MMIO 不是 FFI，`#[repr(C)]` 本身也不受能力闸限制。

## 5. 代码生成

- **地址形式不生成 `GlobalVariable`**：没有符号、不占 `.bss`、不需要链接脚本。下降时该 base 直接
  是 `inttoptr` 常量，宽度取**目标** data layout 的指针宽度（thumbv7m 上是 `i32`，宿主机上是 `i64`，
  所以不能写死）。
- **符号形式**发一个 `external global`，地址由链接脚本 `PROVIDE(NAME = 0x...);` 提供；
  它同时是**可测试**的形态——测试夹具可以在 C 侧定义那个符号。
- **访问**：`loadPlace` / `storePlace` 带一个 volatile 标志 → `load volatile` / `store volatile`。
  GEP 本身不是访问，不加 volatile。
- **volatile 判定**在位置走查里累积，两个独立的理由任一成立即可：根是寄存器全局，或者走查经过
  了 `#[volatile]` 类型（Deref 之后 `currentTy` 就是 `GpioRegs`，所以方法里的 `self.odr` 自动成立）。
- **优化**：整个程序仍走 `-o 2` 的全程序管线；volatile 访问由 LLVM 保证不被删除、合并或与其它
  volatile 重排。**验收就钉在 `-o 2`**：`MmioVolatileStoreSurvivesO2` 断言优化之后那次存储还在。

## 6. 中断与并发（边界说清楚）

- volatile 保证**单次** load/store 的可见性，不保证跨访问的原子性。
- 裸机是单核中断模型：主循环与 ISR 共享寄存器时，读-改-写会丢更新。可用手段：写 `BSRR`/`BRR` 这类
  单次存储的寄存器（推荐）、在临界区屏蔽中断（需要一个「临界区执行」的原语，**尚未设计**）、
  或将来的原子内建。
- 语言**没有** `AtomicU32` 或内存序模型，`#[volatile]` 不改变这一点。

## 7. 运行期地址（尚未做）

有些外设地址只能在运行期确定（DMA 通道选择、外设实例数组、第二块相同外设）。这需要「从整数造出
一个位置」，也就是一项新的 unsafe 权力。设计上只允许它走标准库：`Std/mmio.lis` 的
`reg_at<T>(addr: i32) -> &mut T`——用户只见到 `&mut T`，借用检查照常生效，而「地址是个整数」这个
事实被关在标准库里。**这一条尚未实现，也需要单独拍板**：它把标准库的信任面从「堆」扩大到
「任意地址」。本文的静态形式不需要它。

## 8. 验收标准与现状

| # | 标准 | 现状 |
|---|---|---|
| 1 | `-o 0` 的 IR 里是 `store volatile`，且模块里**没有**该寄存器的 `GlobalVariable`，base 是 `inttoptr` 常量 | 已实现（`MmioFieldWriteIsVolatileAndAllocatesNoStorage`） |
| 2 | 同一个存储在 **`-o 2`** 下仍然存在 | 已实现（`MmioVolatileStoreSurvivesO2`） |
| 3 | 经 `&mut Self` 的访问也是 volatile | 已实现（`MmioAccessThroughAMethodIsVolatile`） |
| 4 | 借用规则照常：两个 `&mut` 是 E4001、共享借用下写是 E4003、共享读合法 | 已实现 |
| 5 | 安全闸门不放宽：`0x40020000 as *mut i32` 仍然不成立 | 已实现（`AbsoluteAddressPointerCastIsStillRejected`） |
| 6 | 误用全部报错（§3 表格里的五类 + 缺类型标注 + 带初始化器 + 类型不是 `#[volatile]`） | 已实现 |
| 7 | 运行时实证：C 拥有存储，Lis 写，C 读回 | 已实现（`MmioSymbolWriteReachesC`：`#[volatile(sym = "lis_test_regs")]` + C 侧 `volatile uint32_t`） |
| 8 | 同一份代码交叉编译到 `thumbv7m-none-eabi`，IR 里是 `inttoptr (i32 ... to ptr)` + `store volatile`，反汇编是 Thumb 的 `str` | 已验证（手工：`llvm-objdump -d --triple=thumbv7m-none-eabi`） |
| 9 | 在 QEMU 里跑到 `main` | **未做**：本机没有 `arm-none-eabi-gcc`/`qemu-system-arm`。配方见 §9 |

## 9. 在 QEMU 上验证（留给有工具链的环境）

```bash
# 1. 交叉编译（本仓库只产目标文件）
lisc board.lis --target thumbv7m-none-eabi -o 2 --out board.o
# 2. 用一段启动桩把它链成 ELF（QEMU 需要 reset handler + 向量表）
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostartfiles -T link.ld start.S board.o -o board.elf
# 3. 跑（netduinoplus2 是 STM32F405，同族）
qemu-system-arm -M netduinoplus2 -nographic -kernel board.elf
```

启动桩里把向量表与 `Reset_Handler` 写好、在其中调用 Lis 的 `main`（编译器发的就是 `i32 @main()`）；
`export fn` 定义的函数名就是 C 符号名，所以中断处理函数可以直接指向 Lis 函数。
