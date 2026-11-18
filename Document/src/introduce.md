# 介绍

Lis 是一门融合 Rust 与 C++ 设计理念的编程语言。它采用所有权与借用检查来防止常见的内存错误，
同时保留 C++ 风格的底层表达力。当前安全模型的边界见[借用检查](./borrow.md)和[已知限制](./limitations.md)。

本手册是 Lis 语言的规范文档，描述编译器的实际行为。凡与编译器实现不符的描述，
均以「未实现」标注；语义取舍以本手册为准，编译器以本手册为契约逐步对齐。

## 设计理念

- **所有权**：每个值有唯一所有者；非拷贝类型的移动是显式语义，移动后原绑定不可再使用。
- **借用**：`&T` 是共享引用，`&mut T` 是独占引用。编译器根据引用的实际使用范围检查
  借用冲突，也会在能判定来源时拒绝返回指向当前栈帧的引用。
- **拷贝的显式性**：只有基本类型与引用是拷贝类型，结构体/枚举/数组默认移动。
- **安全默认、显式承担**：破坏性操作（如整数窄化转换）默认报错，需要用户显式声明
  （`#[i_know]` 属性）才放行。

## 语言概览

```lis
struct Vec2 { pub x: i32, pub y: i32 }

impl Add for Vec2
{
    fn add(self, other: Self) -> Vec2
    {
        ret Vec2 { x: self.x + other.x, y: self.y + other.y };
    }
}

fn main() -> i32
{
    let a = Vec2 { x: 1, y: 2 };
    let b = Vec2 { x: 3, y: 4 };
    let c = a + b;              // 运算符重载:trait Add
    ret c.x;                    // 4
}
```

## 快速上手

```bash
export PATH="/c/MinGW/bin:$PATH"   # Windows 需要 MinGW 运行时 DLL
python build.py --llvm-position F:/LLVM/ --build-type Debug --enable-tests --threads 16

# 运行一个示例
./Build/Binaries/lisc.exe Examples/print.lis && g++ -o a.exe a.o && ./a.exe
```

编译器输出 LLVM IR 对象文件（默认 `a.o`），由系统 `g++`/`clang` 链接成可执行文件。

## 文档约定

- 语法用 EBNF 描述，`<grammar>` 包裹。
- 标注「未实现」的语法表示设计已确定、编译器尚未支持。
- 标注「待定」的语义表示取舍尚未定案，见[已知限制与路线图](./limitations.md)。
