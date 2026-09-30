# 按值聚合体 ABI：施工图（未完成）

> 状态：**未实现**。地基已就位（提交 ~c4bbab7~）：模块在 LLVMIRBuilder 阶段就带 triple +
> datalayout，~Context::targetTriple~ / ~Context::dataLayout~ 可直接用于分类；
> ~#[repr(C, packed)]~ 已落地并已把「真正创建结构体类型的地方」钉准。

## 1. 现状（2026-09-26 实测）

- 我们的 IR 把结构体类型直接写进调用：~call i32 @nested_val(%Nested %load)~。
- clang 会在 **IR 层**做 ABI 强制转换：~call i32 @nested_val(i64 %6)~（8 字节）与
  ~call i32 @arr_val(ptr noundef %3)~（12 字节，形参是 ~ptr byval(%struct.Arr3)~）。
- **LLVM 后端不会**从结构体类型自己推导平台聚合体规则。
- 后果（在 c4bbab7 之前的实测）：C 侧只收到第一个字段——~{i32,i32}~ 到 C 是 ~{1,0}~
  （~p2~ 返回 10 而不是 12），12 字节的 ~{[3 x i32]}~ 直接崩。

## 2. 规则（按 triple 分支）

**Win64 x86-64**（~triple.isOSWindows() && arch == x86_64~）
- 大小 ∈ {1,2,4,8} 字节 → 按等宽整数传/返（~i8/i16/i32/i64~）。
- 其他大小 → 实参与返回值都**按引用**：实参一个 ~ptr~、返回值走隐藏首指针（sret）。

**SysV x86-64**（~arch == x86_64 && !windows~）
- 大小 > 16 → 同上（按引用 / sret）。
- ≤ 16 且**只含整数/指针** → 逐 8 字节整数化（可能一个实参变两个：~i64, i64~；
  返回值同理，用一个 ~{i64, i64}~ 之类的小结构体承载）。
- ≤ 16 且含浮点 → **先拒绝**（~valid=false~，文案指向传指针）：SSE 分类与混合分类
  还没实现，宁可不做也不能静默误调。

## 3. 挂钩点（都已定位）

| 位置 | 改法 |
|---|---|
| ~LLVMIRBuilder::getOrDeclareExternFn~ | 形参/返回按上面的规则建类型（sret 时首参是 ~ptr~、返回 ~void~） |
| ~LLVMIRBuilder::lowerCall~ 的 extern 分支 | 实参：~CoerceInt~ 走「取位置的地址 + 按 iN 载入」（~lowerPlaceAsPtr~ 已有）；~ByVal~ 走 ~alloca~+store+传指针；sret 走「dest 的地址当首参、调用无返回值」 |
| 返回 | ~CoerceInt~ 用「把 iN 直接 store 进结构体槽位」（~lowerPlaceAsPtr(dest)~ 后 ~CreateStore~）；sret 无需 store |
| ~checkFfiSafeType~ | 允许 ~#[repr(C)]~（且非 packed）的 struct 按值，前提是 ~FfiAbi::classify(...).valid~；否则 E3020 带上 ~why~ |
| ~export fn~ | **本轮不做**：导出侧要反向拆/装（入参写回结构体局部、返回组装），先把调用方向做对 |

## 4. 已验证的坑（别再踩）

1. **所有 pass 对象在任何 pass 运行之前就构造完**：需要在 run 前拿到的数据（如 target）
   不能在构造函数里读 Context，要放到 ~run()~ / ~lowerProgram()~。
2. ~TargetRegistry::lookupTarget~ 需要先 ~InitializeNativeTarget()~；共用
   ~Core/TargetInit.hpp~ 的 ~initLLVMTargetsOnce()~。
3. **结构体类型的真正创建点是 ~LLVMIRBuilder::declareStructTypes~**，~TypeHelper.cpp~ 的
   Custom 分支只是惰性兜底：布局/打包这类属性两处都要设。
4. 子进程 stdout 是 CRLF（测试只比退出码，内容断言写在被编译的程序里）。
5. 形参名不能遮蔽已有定义；测试 prologue 不含 ~Vec~。

## 5. 验收（差分矩阵，新 TU ~RuntimeTest6.cpp~）

C 侧（g++ 编译）对每个形状提供「按字段求和」和「构造并返回」两个函数，Lis 侧独立算同样的值：

~{i8}~ ~{i16}~ ~{i32}~ ~{i64}~ ~{i32,i32}~ ~{f32,f32}~ ~{f64}~ ~{f64,f64}~ ~{i32,i64}~
~{i32,f64}~ ~{i32,i32,i32}~（12B） ~{[3 x i32]}~ ~{{i32,i32}}~ ~{i8,i64,f64,i32}~（24B） 64B

每个都测传参与返回。**SysV 的规则另外用 clang 的 IR 对拍**（本机可跑：
~clang++ --target=x86_64-linux-gnu -S -emit-llvm~ 无头文件即可，看 ~define~/~declare~ 的形参类型）——
本机无法运行 Linux 二进制，所以 SysV 只能这样验证，测试里要写明这一点。

## 6. 仍然不做（写进 ffi.md）

C++ 名字修饰/虚表/异常、SysV 的 ~_Complex~ / ~__int128~ / 位域、变参尾里的聚合体。
