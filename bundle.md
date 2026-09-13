===== FILE: docs/appendix/prerequisites.md =====
# 附录 A：新手入门预备知识

> 本附录面向完全没有编译器开发经验的读者，介绍理解 tinycc 源码所需的 C 语言、
> 编译原理和计算机系统基础知识，并提供学习路径建议。

---

## 第一部分：C 语言基础回顾

### 1.1 你需要掌握的 C 语言核心概念

阅读 tinycc 源码需要扎实的 C 语言基础。以下是必须掌握的概念：

#### 指针与内存

```c
int x = 42;
int *p = &x;      // p 指向 x 的地址
*p = 100;          // 通过指针修改 x 的值

// 指针算术
int arr[5] = {10, 20, 30, 40, 50};
int *q = arr;      // q 指向 arr[0]
q++;               // q 现在指向 arr[1]

// 函数指针
int (*func_ptr)(int, int) = add;  // func_ptr 指向 add 函数
int result = func_ptr(3, 4);      // 通过函数指针调用
```

在 tinycc 中，指针无处不在。`Sym *s` 表示一个符号，`Section *sec` 表示一个 ELF 段，
`char *data` 表示一段数据缓冲区。

#### 结构体与联合体

```c
// 结构体：多个字段组合在一起
struct Point {
    int x;
    int y;
};
struct Point p = {10, 20};

// 联合体：多个字段共享同一块内存
union Value {
    int i;
    float f;
    char *s;
};
union Value v;
v.i = 42;     // 此时内存中存的是整数
v.f = 3.14;   // 此时内存中存的是浮点数（覆盖了 i）

// 位域：在结构体中精确控制每个字段占多少位
struct Flags {
    unsigned int is_const : 1;   // 1 bit
    unsigned int is_static : 1;  // 1 bit
    unsigned int type : 4;       // 4 bits
};
```

tinycc 中的 `CType` 结构体使用位域来紧凑地编码类型信息（见 04-语法分析文档）。

#### 位操作

```c
// 按位与 &：两个位都为 1 时结果为 1
int a = 0b1100 & 0b1010;  // 结果: 0b1000 = 8

// 按位或 |：任一位为 1 时结果为 1
int b = 0b1100 | 0b1010;  // 结果: 0b1110 = 14

// 按位异或 ^：两位不同时结果为 1
int c = 0b1100 ^ 0b1010;  // 结果: 0b0110 = 6

// 按位取反 ~：0 变 1，1 变 0
int d = ~0b1100;           // 结果: ...11110011

// 左移 << 和右移 >>
int e = 1 << 4;            // 结果: 16 (0b10000)
int f = 32 >> 3;           // 结果: 4

// 常见用法：标志位检查
#define VT_UNSIGNED  0x0010
#define VT_CONSTANT  0x0100

int type = VT_INT | VT_UNSIGNED | VT_CONSTANT;

if (type & VT_UNSIGNED)     // 检查是否设置了 unsigned 标志
    printf("unsigned\n");

int base_type = type & 0x0F;  // 取低 4 位作为基本类型
```

tinycc 大量使用位操作来编码和解码类型信息。`CType.t` 字段的不同比特位分别表示
基本类型、修饰符、存储类等信息。

#### 函数指针与回调

```c
// 回调函数模式
typedef void (*ErrorFunc)(void *opaque, const char *msg);

void set_error_handler(ErrorFunc func, void *data) {
    // 保存 func 和 data，出错时调用
}

void my_handler(void *opaque, const char *msg) {
    printf("Error: %s\n", msg);
}

// 使用
set_error_handler(my_handler, NULL);
```

tinycc 的 libtcc API 使用这种模式实现错误回调（见 07-libtcc 文档）。

#### 预处理器

```c
// 宏定义
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 条件编译
#ifdef TCC_TARGET_X86_64
    // x86_64 特定代码
#elif defined(TCC_TARGET_ARM64)
    // ARM64 特定代码
#endif

// 宏拼接 ##
#define MAKE_FUNC(name) void name##_init(void)
MAKE_FUNC(server);  // 展开为: void server_init(void)
```

tinycc 自身就是一个预处理器的实现（tccpp.c），同时也大量使用预处理器来管理
多平台代码。

### 1.2 C 语言中新手容易混淆的概念

| 概念 | 说明 |
|------|------|
| 声明 vs 定义 | 声明告诉编译器"有这个东西"，定义是"创建这个东西" |
| 左值 vs 右值 | 左值有地址（可以取地址），右值是临时值 |
| 数组 vs 指针 | 数组名在大多数上下文中退化为指针，但 `sizeof` 行为不同 |
| `const int *p` vs `int *const p` | 前者指针指向的内容不可变，后者指针本身不可变 |
| `void *` | 通用指针，可以与任何指针类型互转（C 语言中） |
| 位域 | 结构体中可以指定每个字段占多少位 |

---

## 第二部分：编译器基础知识

### 2.1 什么是编译器？

编译器是一个**翻译程序**，将人类可读的源代码翻译为计算机可执行的机器代码。

```
人类写的代码              计算机执行的指令
─────────────            ────────────────
int x = 1 + 2;    →     mov eax, 1
                          add eax, 2
                          mov [rbp-4], eax
```

### 2.2 编译的四个经典阶段

大多数编译器教科书将编译过程分为四个阶段：

```
┌─────────────┐
│  源代码      │    "int x = 1 + 2;"
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  词法分析    │    将字符流拆分为 token
│  (Lexer)    │    "int" "x" "=" "1" "+" "2" ";"
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  语法分析    │    将 token 流组织成语法树
│  (Parser)   │    声明(类型=int, 名=x, 初值=加法(1, 2))
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  语义分析    │    检查类型、作用域等
│  + 优化      │    常量折叠: 1+2 → 3
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  代码生成    │    生成目标机器代码
│  (CodeGen)  │    mov eax, 3; mov [rbp-4], eax
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  目标代码    │    可执行文件或目标文件
└─────────────┘
```

#### 阶段 1：词法分析 (Lexical Analysis)

**做什么**：将源代码的字符流切分为有意义的单元——**token**（词法单元）。

```
源代码: int x = 42 + y;

Token 序列:
  [关键字:int] [标识符:x] [运算符:=] [整数:42] [运算符:+] [标识符:y] [分号:;]
```

就像英语中把一句话拆分成单词和标点符号。

#### 阶段 2：语法分析 (Syntax Analysis)

**做什么**：根据语法规则将 token 组织成**语法树**（AST, Abstract Syntax Tree）。

```
       声明
      / | \
    int  x  =
            |
          加法
         /    \
       42      y
```

就像英语中分析句子的主语、谓语、宾语结构。

#### 阶段 3：语义分析 + 优化

**做什么**：检查语义正确性（类型匹配、变量是否声明等），并进行优化。

```
优化前: 42 + y
优化后: 如果 y 是常量 8，直接计算 42 + 8 = 50
```

#### 阶段 4：代码生成

**做什么**：将语法树翻译为目标机器的指令序列。

```
x86_64 汇编:
    mov  eax, 50          ; 将 50 放入寄存器 eax
    mov  [rbp-4], eax     ; 将 eax 的值存到栈上变量 x 的位置
```

### 2.3 tinycc 的特殊之处：单遍编译

传统编译器（如 GCC、Clang）会完整地构建语法树，然后遍历树来生成代码。
tinycc 则完全不同——它**边解析边生成代码**，没有中间的语法树：

```
传统编译器 (GCC/Clang):
  源码 → 词法分析 → 语法树 → 语义分析 → 优化 → 代码生成 → 目标代码

tinycc:
  源码 → 词法分析 → [解析+代码生成同时进行] → 目标代码
```

这就像同声传译（边听边翻译）vs 笔译（听完再翻译）。同声传译更快，但翻译质量可能不如笔译。

### 2.4 关键编译术语表

| 术语 | 英文 | 含义 |
|------|------|------|
| Token | Token | 词法单元，如关键字、标识符、运算符 |
| AST | Abstract Syntax Tree | 抽象语法树，源码的树形表示 |
| 类型系统 | Type System | 编译器如何理解和检查数据类型 |
| 符号表 | Symbol Table | 存储变量、函数等名称及其属性的表 |
| 作用域 | Scope | 变量可见的代码范围 |
| 常量折叠 | Constant Folding | 编译时计算常量表达式（如 1+2→3） |
| 寄存器分配 | Register Allocation | 决定哪些变量放在 CPU 寄存器中 |
| 重定位 | Relocation | 链接时修正地址引用 |
| 段/节 | Section | ELF 文件中的逻辑分区（代码、数据等） |
| 链接 | Linking | 将多个目标文件合并为一个可执行文件 |
| JIT | Just-In-Time | 运行时即时编译执行 |

### 2.5 一张图理解编译器的输入输出

```
                    编译器 (tcc)
                    ┌─────────────────────────────────────────┐
                    │                                         │
 hello.c ──────────►│  词法分析 → 语法分析 → 代码生成 → 链接  │──────► hello (可执行文件)
 printf.c ─────────►│                                         │
 libc.a ───────────►│                                         │
                    └─────────────────────────────────────────┘

 hello.c:  用户写的源代码
 printf.c: 标准库源代码（可选）
 libc.a:   预编译的标准库（静态库）
 hello:    最终的可执行文件（机器码）
```

---

## 第三部分：计算机系统基础

### 3.1 CPU、寄存器和内存

```
┌─────────────────────────────────────────────┐
│                 CPU                          │
│  ┌─────────────────────────────────────┐    │
│  │  寄存器 (Registers)                  │    │
│  │  rax: 通用寄存器 (常用于返回值)      │    │
│  │  rbx: 通用寄存器                     │    │
│  │  rcx: 通用寄存器 (常用于计数)        │    │
│  │  rdx: 通用寄存器                     │    │
│  │  rsi: 通用寄存器 (源索引)            │    │
│  │  rdi: 通用寄存器 (目标索引)          │    │
│  │  rsp: 栈指针 (指向栈顶)              │    │
│  │  rbp: 帧指针 (指向当前函数的栈帧)    │    │
│  │  rip: 指令指针 (指向下一条指令)      │    │
│  └─────────────────────────────────────┘    │
│  ┌─────────────────────────────────────┐    │
│  │  ALU (算术逻辑单元)                  │    │
│  │  执行加减乘除、位运算、比较          │    │
│  └─────────────────────────────────────┘    │
└────────────────────┬────────────────────────┘
                     │ 总线
┌────────────────────▼────────────────────────┐
│                 内存 (RAM)                    │
│  ┌─────────────────────────────────────┐    │
│  │  高地址                              │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  栈 (Stack)                  │    │    │
│  │  │  局部变量、函数调用信息       │    │    │
│  │  │  向下增长 ←                   │    │    │
│  │  └─────────────────────────────┘    │    │
│  │                                      │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  堆 (Heap)                   │    │    │
│  │  │  动态分配的内存               │    │    │
│  │  │  向上增长 →                   │    │    │
│  │  └─────────────────────────────┘    │    │
│  │                                      │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  .data (已初始化全局变量)    │    │    │
│  │  │  .bss  (未初始化全局变量)    │    │    │
│  │  │  .text (代码段)              │    │    │
│  │  │  .rodata (只读数据)          │    │    │
│  │  └─────────────────────────────┘    │    │
│  │  低地址                              │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

### 3.2 函数调用时栈帧的变化

```c
int add(int a, int b) {
    int result = a + b;
    return result;
}

int main() {
    int x = add(3, 4);
    return x;
}
```

```
调用 add(3, 4) 时的栈帧：

高地址
┌──────────────────────┐
│  main 的栈帧          │
│  ...                  │
│  x (未初始化)         │
├──────────────────────┤  ← 进入 add 后
│  返回地址 (main 中)   │  ← call 指令自动压入
│  旧的 rbp             │  ← push rbp
│  a = 3               │  ← 参数
│  b = 4               │  ← 参数
│  result = 7          │  ← 局部变量
├──────────────────────┤  ← rsp (栈指针)
低地址
```

x86_64 的函数调用约定（ABI）规定：
- 前 6 个整数参数通过寄存器传递：rdi, rsi, rdx, rcx, r8, r9
- 返回值放在 rax 中
- 调用者保存：rax, rcx, rdx, rsi, rdi, r8-r11
- 被调用者保存：rbx, rbp, r12-r15

### 3.3 ELF 文件格式

ELF (Executable and Linkable Format) 是 Linux 上的可执行文件格式：

```
┌─────────────────────┐
│  ELF Header          │  魔术数字、架构、入口地址
├─────────────────────┤
│  Program Headers     │  告诉操作系统如何加载（段信息）
├─────────────────────┤
│  .text               │  机器代码（可执行指令）
├─────────────────────┤
│  .rodata             │  只读数据（字符串常量等）
├─────────────────────┤
│  .data               │  已初始化的全局变量
├─────────────────────┤
│  .bss                │  未初始化的全局变量（不占文件空间）
├─────────────────────┤
│  .symtab             │  符号表（函数名、变量名）
├─────────────────────┤
│  .strtab             │  字符串表
├─────────────────────┤
│  .rel.text           │  代码段的重定位信息
├─────────────────────┤
│  .debug_*            │  调试信息（可选）
├─────────────────────┤
│  Section Headers     │  描述每个段的属性
└─────────────────────┘
```

你可以用 `readelf -a hello` 命令查看任何 ELF 文件的详细结构。

### 3.4 链接：将多个文件合并

```
main.c                    math.c
┌──────────────┐          ┌──────────────┐
│ int add();   │          │ int add(int a,│
│ int main() { │          │   int b) {    │
│   add(3,4);  │          │   return a+b; │
│ }            │          │ }             │
└──────┬───────┘          └──────┬────────┘
       │                         │
       ▼                         ▼
   main.o                    math.o
┌──────────────┐          ┌──────────────┐
│ 代码:        │          │ 代码:        │
│ call <未解析>│          │ add:         │
│              │          │  mov eax,edi │
│ 符号:        │          │  add eax,esi │
│ add (未定义) │          │  ret         │
│ main (导出)  │          │ 符号:        │
└──────┬───────┘          │ add (导出)   │
       │                  └──────┬────────┘
       │                         │
       └────────┬────────────────┘
                │
                ▼  链接器 (linker)
        ┌──────────────┐
        │ hello (可执行) │
        │ 代码:          │
        │ main:          │
        │  call add  ←── 解析了 add 的地址
        │ add:           │
        │  mov eax,edi   │
        │  add eax,esi   │
        │  ret           │
        └──────────────┘
```

链接器的工作：
1. **符号解析**：找到每个未定义符号（如 `add`）在哪个目标文件中定义
2. **重定位**：修正代码中对符号地址的引用

---

## 第四部分：编译器 vs 解释器

### 4.1 核心区别

```
编译器 (Compiler):
  源代码 ──编译──→ 机器码文件 ──执行──→ 结果
  (一次性翻译，之后可以反复运行)

解释器 (Interpreter):
  源代码 ──逐行读取、翻译、执行──→ 结果
  (边读边执行，每次运行都需要解释)

JIT 编译器 (Just-In-Time Compiler):
  源代码 ──编译到内存──→ 内存中的机器码 ──执行──→ 结果
  (运行时编译，tcc -run 就是这种模式)
```

### 4.2 对比表

| 特性 | 编译器 | 解释器 | JIT |
|------|--------|--------|-----|
| 执行速度 | 快（提前编译） | 慢（逐行解释） | 较快（运行时编译） |
| 启动速度 | 慢（需要先编译） | 快（直接执行） | 中等 |
| 代表语言 | C, C++, Rust | Python, Ruby | Java, JavaScript |
| tcc 对应 | `tcc -c` + 链接 | - | `tcc -run` |
| 错误发现 | 编译时发现所有错误 | 运行时才发现错误 | 混合 |

### 4.3 tcc 的独特定位

tcc 同时支持三种模式：

```bash
# 1. 传统编译模式：编译为可执行文件
tcc hello.c -o hello
./hello

# 2. JIT 模式：直接在内存中编译并执行
tcc -run hello.c

# 3. 嵌入模式：作为库嵌入到其他程序中
# 通过 libtcc API 动态编译和执行 C 代码
```

---

## 第五部分：理解 tinycc 源码的学习路径

### 5.1 推荐学习顺序

```
阶段 1：打基础（1-2 周）
├── 学习 C 语言指针、结构体、位操作
├── 了解 ELF 文件格式（readelf, objdump 命令）
├── 了解 x86_64 基本指令（mov, add, call, ret）
└── 阅读：docs/00-overview.md

阶段 2：理解编译流程（2-3 周）
├── 学习词法分析概念（正则表达式、DFA）
├── 学习语法分析概念（上下文无关文法、递归下降）
├── 阅读：docs/02-lexer.md，对照 tccpp.c 源码
├── 阅读：docs/03-preprocessor.md
└── 实践：用 tcc -E 观察预处理输出

阶段 3：深入代码（3-4 周）
├── 阅读：docs/04-parser.md，对照 tccgen.c 源码
├── 阅读：docs/05-codegen.md
├── 实践：用 tcc -S 生成汇编，对照源码理解
└── 实践：修改 tcc，添加一个简单的警告

阶段 4：系统级理解（2-3 周）
├── 阅读：docs/06-linker.md，理解 ELF 和链接
├── 阅读：docs/08-platforms.md，理解多架构支持
├── 实践：用 readelf 分析 tcc 生成的文件
└── 实践：编写使用 libtcc API 的小程序

阶段 5：专家级（持续）
├── 阅读所有文档，深入理解每个模块
├── 为 tcc 修复 bug 或添加特性
├── 参与 tcc 邮件列表讨论
└── 尝试为 tcc 添加新的语言特性
```

### 5.2 必备工具

```bash
# 编译和构建
gcc          # C 编译器（用于编译 tcc 自身）
make         # 构建工具
gdb          # 调试器（单步跟踪 tcc 执行）

# 二进制分析
readelf      # 查看 ELF 文件结构
objdump      # 反汇编目标文件
nm           # 查看符号表
strings      # 提取文件中的字符串
hexdump      # 查看文件的十六进制内容

# 代码阅读
grep/ripgrep # 在源码中搜索
ctags/cscope # 代码导航（可选）
```

### 5.3 动手实验建议

#### 实验 1：观察编译过程

```bash
# 查看预处理输出（词法分析 + 预处理的结果）
tcc -E hello.c

# 查看汇编输出（代码生成的结果）
tcc -S hello.c

# 查看目标文件（ELF 格式）
tcc -c hello.c
readelf -a hello.o

# 查看符号表
nm hello.o
```

#### 实验 2：用 GDB 跟踪 tcc 编译过程

```bash
# 用调试器运行 tcc 自身
gdb --args ./tcc -c hello.c

# 在关键函数设置断点
(gdb) break next          # 词法分析
(gdb) break decl          # 声明解析
(gdb) break unary         # 表达式解析
(gdb) break gen_op        # 代码生成
(gdb) run
(gdb) step                # 单步执行
(gdb) print tok           # 查看当前 token
(gdb) print vtop[0]       # 查看虚拟栈顶
```

#### 实验 3：修改 tcc 添加功能

```c
// 在 tccgen.c 的 decl() 函数中添加一行调试输出
// 当遇到 typedef 时打印消息
if (type.t & VT_TYPEDEF) {
    printf("Found typedef: %s\n", get_tok_str(v, NULL));
}
```

重新编译 tcc，然后编译一个包含 typedef 的 C 文件，观察输出。

#### 实验 4：使用 libtcc API

```c
// test_libtcc.c
#include "libtcc.h"
#include <stdio.h>

int main() {
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "int square(int x) { return x * x; }");

    tcc_relocate(s, NULL);

    typedef int (*func_t)(int);
    func_t square = (func_t)tcc_get_symbol(s, "square");

    printf("square(7) = %d\n", square(7));  // 49

    tcc_delete(s);
    return 0;
}

// 编译：
// gcc -o test_libtcc test_libtcc.c -I. -L. -ltcc -ldl -lpthread
```

### 5.4 推荐阅读资料

#### 书籍

| 书名 | 适合阶段 | 说明 |
|------|----------|------|
| 《C 程序设计语言》(K&R) | 阶段 1 | C 语言经典教材 |
| 《深入理解计算机系统》(CSAPP) | 阶段 1-2 | 系统级编程必读 |
| 《编译原理》(龙书) | 阶段 2 | 编译器理论权威教材 |
| 《自己动手构造编译器》 | 阶段 2-3 | 实践导向的编译器教程 |
| 《程序员的自我修养》 | 阶段 3-4 | 链接、装载与库 |
| 《ELF 文件格式分析》 | 阶段 4 | ELF 格式详解 |

#### 在线资源

| 资源 | 说明 |
|------|------|
| [tcc 官方邮件列表](https://lists.nongnu.org/mailman/listinfo/tinycc-devel) | tcc 开发者社区 |
| [Compiler Explorer](https://godbolt.org/) | 在线查看 C 代码对应的汇编输出 |
| [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf) | ELF 格式官方规范 |
| [x86_64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI) | x86_64 调用约定规范 |
| [Crafting Interpreters](https://craftinginterpreters.com/) | 免费的编译器/解释器教程 |

### 5.5 常见问题

**Q: 我需要懂汇编语言才能读懂 tinycc 吗？**

A: 不需要精通，但需要了解基本概念。tinycc 的代码生成部分（tccgen.c + xxx-gen.c）
确实涉及汇编指令的生成，但你可以先理解平台无关的部分（词法分析、语法分析、类型系统），
这些完全不需要汇编知识。需要汇编知识的部分主要是 05-代码生成 和 08-平台支持 文档。

**Q: tinycc 的代码质量如何？适合学习吗？**

A: tinycc 的代码风格紧凑，有些地方为了性能做了较复杂的优化（如 tccpp.c 的快速路径）。
但整体来说，它是学习编译器实现的优秀材料，因为：
- 代码量小（核心文件约 2 万行），可以完整阅读
- 单遍编译架构简单直接
- 没有复杂的优化遍，逻辑清晰
- 有完整的测试套件，可以验证修改

**Q: 从 tinycc 学到的知识可以应用到其他编译器吗？**

A: 可以，但需要注意差异：
- tinycc 的单遍架构在现代编译器中不常见（GCC/Clang 使用多遍）
- tinycc 不做优化，而生产编译器的优化器非常复杂
- tinycc 的类型系统和符号表设计是通用的编译器知识
- ELF 处理、链接器、JIT 等知识完全通用

**Q: 如何为 tinycc 贡献代码？**

A: 1) 先阅读所有文档，理解架构
   2) 订阅 tcc-devel 邮件列表
   3) 从修复简单的 bug 开始（查看 bug 追踪器）
   4) 运行测试套件确保不引入回归
   5) 提交补丁到邮件列表

---

## 第六部分：快速参考卡

### 6.1 x86_64 常用指令速查

```asm
; 数据传输
mov  rax, rbx      ; rax = rbx
mov  rax, [rbx]    ; rax = *rbx (从内存加载)
mov  [rbx], rax    ; *rbx = rax (存到内存)
lea  rax, [rbx+8]  ; rax = rbx + 8 (地址计算，不访问内存)

; 算术运算
add  rax, rbx      ; rax += rbx
sub  rax, rbx      ; rax -= rbx
imul rax, rbx      ; rax *= rbx
neg  rax           ; rax = -rax

; 位运算
and  rax, rbx      ; rax &= rbx
or   rax, rbx      ; rax |= rbx
xor  rax, rbx      ; rax ^= rbx
shl  rax, 4        ; rax <<= 4 (左移)
shr  rax, 4        ; rax >>= 4 (右移)

; 比较和跳转
cmp  rax, rbx      ; 比较 rax 和 rbx，设置标志位
je   label         ; 相等时跳转 (Jump if Equal)
jne  label         ; 不等时跳转
jl   label         ; 小于时跳转 (Jump if Less)
jmp  label         ; 无条件跳转

; 函数调用
call func          ; 调用函数（压入返回地址）
ret                ; 返回（弹出返回地址并跳转）
push rax           ; 压栈
pop  rax           ; 出栈
```

### 6.2 ELF 文件常用命令

```bash
readelf -h file       # 查看 ELF 头
readelf -S file       # 查看所有段（Section）
readelf -s file       # 查看符号表
readelf -r file       # 查看重定位信息
readelf -l file       # 查看程序头（Segment）

objdump -d file       # 反汇编代码段
objdump -t file       # 查看符号表
objdump -s file       # 以十六进制显示所有段

nm file               # 列出符号
file file             # 识别文件类型
size file             # 查看各段大小
```

### 6.3 GDB 调试速查

```bash
gdb ./program         # 启动调试
break main            # 在 main 设置断点
break tccgen.c:464    # 在指定文件行号设置断点
run                   # 开始运行
next (n)              # 单步（不进入函数）
step (s)              # 单步（进入函数）
continue (c)          # 继续运行
print expr            # 打印表达式
backtrace (bt)        # 查看调用栈
info locals           # 查看局部变量
list                  # 显示源代码
quit (q)              # 退出
```


===== FILE: docs/ch01/examples/hello.c =====
/*
 * hello.c - Hello World example for Chapter 1
 *
 * This is the simplest possible C program. It demonstrates:
 *   - #include directive (preprocessor)
 *   - Function definition (main)
 *   - Function call (printf)
 *   - String literal
 *   - Return statement
 *
 * Compile with:
 *   tcc hello.c -o hello          # compile to executable
 *   tcc -run hello.c              # compile and run directly
 *   tcc -E hello.c                # preprocess only
 *   tcc -S hello.c                # compile to assembly
 *   tcc -c hello.c                # compile to object file
 */

#include <stdio.h>

int main(void)
{
    printf("Hello, TinyCC!\n");
    return 0;
}


===== FILE: docs/ch01/examples/simple_math.c =====
/*
 * simple_math.c - Demonstrates functions, variables, and types
 *
 * This example exercises several C language features that the compiler
 * must handle:
 *   - Multiple function definitions
 *   - Local and global variables
 *   - Integer and floating-point types
 *   - Control flow (if/else, for loop)
 *   - Function calls with arguments and return values
 *   - Printf format strings with multiple types
 *
 * Compile with:
 *   tcc simple_math.c -o simple_math
 *   tcc -run simple_math.c
 */

#include <stdio.h>

/* Global constant */
#define MAX_FIB 20

/* Function: compute factorial recursively */
static int factorial(int n)
{
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}

/* Function: compute Fibonacci number iteratively */
static int fibonacci(int n)
{
    int a = 0, b = 1, i, temp;

    for (i = 0; i < n; i++) {
        temp = a + b;
        a = b;
        b = temp;
    }
    return a;
}

/* Function: compute the maximum of two integers */
static int max(int a, int b)
{
    return a > b ? a : b;
}

/* Function: compute power using repeated multiplication */
static double power(double base, int exp)
{
    double result = 1.0;
    int i;

    for (i = 0; i < exp; i++)
        result *= base;
    return result;
}

int main(void)
{
    int i;
    int fact_val = 10;
    double pi_approx;

    /* Demonstrate factorial */
    printf("Factorials:\n");
    for (i = 0; i <= fact_val; i++)
        printf("  %2d! = %d\n", i, factorial(i));

    /* Demonstrate Fibonacci sequence */
    printf("\nFibonacci sequence (first %d terms):\n", MAX_FIB);
    for (i = 0; i < MAX_FIB; i++)
        printf("  F(%2d) = %d\n", i, fibonacci(i));

    /* Demonstrate max function */
    printf("\nmax(42, 17) = %d\n", max(42, 17));
    printf("max(-3, 5)  = %d\n", max(-3, 5));

    /* Demonstrate floating-point power function */
    printf("\nPowers of 2:\n");
    for (i = 0; i <= 10; i++)
        printf("  2^%2d = %.0f\n", i, power(2.0, i));

    /* Approximate pi using Leibniz formula: pi/4 = 1 - 1/3 + 1/5 - 1/7 + ... */
    pi_approx = 0.0;
    for (i = 0; i < 100000; i++) {
        if (i % 2 == 0)
            pi_approx += 1.0 / (2 * i + 1);
        else
            pi_approx -= 1.0 / (2 * i + 1);
    }
    pi_approx *= 4.0;
    printf("\nPi approximation (Leibniz, 100000 terms): %.10f\n", pi_approx);

    return 0;
}


===== FILE: docs/ch01/examples/trace_compile.sh =====
#!/bin/bash
# trace_compile.sh - Trace a C program through all compilation stages
#
# This script demonstrates the four main stages of C compilation:
#   1. Preprocessing (-E): expand macros and includes
#   2. Compilation to assembly (-S): generate assembly code
#   3. Compilation to object (-c): generate object file
#   4. Linking: generate executable
#
# Usage:
#   ./trace_compile.sh [input.c]
#   (defaults to ../examples/hello.c if no argument given)

set -e

# Determine the input file
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INPUT="${1:-${SCRIPT_DIR}/../examples/hello.c}"

if [ ! -f "$INPUT" ]; then
    echo "Error: input file '$INPUT' not found."
    echo "Usage: $0 [input.c]"
    exit 1
fi

BASENAME=$(basename "$INPUT" .c)
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "============================================================"
echo "  Tracing compilation of: $INPUT"
echo "  Temporary directory:    $TMPDIR"
echo "============================================================"
echo ""

# Locate tcc binary
TCC=""
if command -v tcc &>/dev/null; then
    TCC="tcc"
elif [ -x "./tcc" ]; then
    TCC="./tcc"
elif [ -x "../tcc" ]; then
    TCC="../tcc"
else
    echo "Error: tcc not found. Build it first with: ./configure && make"
    exit 1
fi
echo "Using tcc: $TCC"
echo ""

# ---- Stage 1: Preprocessing ----
echo "============================================================"
echo "  STAGE 1: Preprocessing (tcc -E)"
echo "============================================================"
echo ""
echo "The preprocessor expands #include directives, #define macros,"
echo "and processes #if/#ifdef conditionals. The output is pure C"
echo "code with all macros expanded and all includes inlined."
echo ""
echo "--- Preprocessor output (first 40 lines) ---"
$TCC -E "$INPUT" 2>/dev/null | head -40
echo ""
echo "... (full output saved to ${TMPDIR}/${BASENAME}.i)"
$TCC -E "$INPUT" -o "${TMPDIR}/${BASENAME}.i" 2>/dev/null
echo "Preprocessor output: $(wc -l < ${TMPDIR}/${BASENAME}.i) lines"
echo ""

# ---- Stage 2: Compilation to Assembly ----
echo "============================================================"
echo "  STAGE 2: Compilation to Assembly (tcc -S)"
echo "============================================================"
echo ""
echo "The compiler parses the preprocessed C code and generates"
echo "target architecture assembly language."
echo ""
echo "--- Assembly output ---"
$TCC -S "$INPUT" -o "${TMPDIR}/${BASENAME}.s" 2>/dev/null
cat "${TMPDIR}/${BASENAME}.s"
echo ""
echo "Assembly output: $(wc -l < ${TMPDIR}/${BASENAME}.s) lines"
echo ""

# ---- Stage 3: Compilation to Object File ----
echo "============================================================"
echo "  STAGE 3: Compilation to Object File (tcc -c)"
echo "============================================================"
echo ""
echo "The assembler encodes assembly instructions into machine code"
echo "bytes and produces an object file (.o) in ELF format."
echo ""
$TCC -c "$INPUT" -o "${TMPDIR}/${BASENAME}.o" 2>/dev/null
echo "--- Object file info ---"
echo "Object file size: $(stat -c %s ${TMPDIR}/${BASENAME}.o 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME}.o 2>/dev/null) bytes"

if command -v file &>/dev/null; then
    echo "File type: $(file ${TMPDIR}/${BASENAME}.o)"
fi

if command -v readelf &>/dev/null; then
    echo ""
    echo "--- ELF Sections ---"
    readelf -S "${TMPDIR}/${BASENAME}.o" 2>/dev/null || true
    echo ""
    echo "--- Symbol Table ---"
    readelf -s "${TMPDIR}/${BASENAME}.o" 2>/dev/null | head -30 || true
fi
echo ""

# ---- Stage 4: Linking ----
echo "============================================================"
echo "  STAGE 4: Linking (tcc -o)"
echo "============================================================"
echo ""
echo "The linker combines object files and libraries, resolves"
echo "symbol references, and produces the final executable."
echo ""
$TCC "$INPUT" -o "${TMPDIR}/${BASENAME}" 2>/dev/null
echo "--- Executable info ---"
echo "Executable size: $(stat -c %s ${TMPDIR}/${BASENAME} 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME} 2>/dev/null) bytes"

if command -v file &>/dev/null; then
    echo "File type: $(file ${TMPDIR}/${BASENAME})"
fi

if command -v readelf &>/dev/null; then
    echo ""
    echo "--- Program Headers (loadable segments) ---"
    readelf -l "${TMPDIR}/${BASENAME}" 2>/dev/null || true
fi
echo ""

# ---- Stage 5: Execution ----
echo "============================================================"
echo "  STAGE 5: Execution"
echo "============================================================"
echo ""
echo "--- Program output ---"
"${TMPDIR}/${BASENAME}"
echo ""
echo "Exit code: $?"
echo ""

# ---- Summary ----
echo "============================================================"
echo "  COMPILATION STAGE SUMMARY"
echo "============================================================"
echo ""
echo "  Source (.c)       : $INPUT"
echo "  Preprocessed (.i) : ${TMPDIR}/${BASENAME}.i ($(wc -l < ${TMPDIR}/${BASENAME}.i) lines)"
echo "  Assembly (.s)     : ${TMPDIR}/${BASENAME}.s ($(wc -l < ${TMPDIR}/${BASENAME}.s) lines)"
echo "  Object (.o)       : ${TMPDIR}/${BASENAME}.o ($(stat -c %s ${TMPDIR}/${BASENAME}.o 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME}.o 2>/dev/null) bytes)"
echo "  Executable        : ${TMPDIR}/${BASENAME} ($(stat -c %s ${TMPDIR}/${BASENAME} 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME} 2>/dev/null) bytes)"
echo ""
echo "Done. Temporary files in $TMPDIR will be cleaned up."


===== FILE: docs/ch01/exercises/ex1_build.md =====
# 练习 1：从源码构建 TinyCC 并运行测试

## 目标

从源码构建 TinyCC 编译器，运行测试套件，并使用构建好的 tcc 编译和运行示例程序。

## 前提条件

- Linux 或 macOS 系统（或 Windows 上的 MinGW/MSYS2）
- GCC 或 Clang 编译器（用于编译 tcc）
- make 工具
- 基本的 shell 命令行操作能力

## 步骤

### 第一步：获取源码

如果你还没有 TinyCC 源码：

```bash
# 方法 A：从 Git 克隆
git clone https://repo.or.cz/tinycc.git
cd tinycc

# 方法 B：解压 tarball
tar xjf tcc-0.9.28.tar.bz2
cd tcc-0.9.28
```

### 第二步：配置

```bash
./configure
```

**问题 1.1**：运行 `./configure --help`，列出至少 5 个可用的配置选项，并简述它们的作用。

### 第三步：编译

```bash
make
```

**问题 1.2**：记录 `make` 的输出。编译过程中生成了哪些文件？列出你观察到的 `.o` 文件。

**问题 1.3**：查看生成的 `tcc` 可执行文件大小。与你系统上的 `gcc` 可执行文件大小做对比（使用 `ls -lh` 或 `du -h`）。差距有多大？

```bash
ls -lh tcc
ls -lh $(which gcc)    # 或 $(which cc)
```

### 第四步：运行测试

```bash
make test
```

**问题 1.4**：记录测试结果。有多少测试通过？有多少失败？如果存在失败的测试，尝试分析失败原因。

### 第五步：编译和运行示例程序

```bash
# 使用构建好的 tcc 编译 hello.c
./tcc examples/hello.c -o /tmp/hello_tcc

# 使用 tcc 直接运行（-run 模式）
./tcc -run examples/hello.c

# 使用 tcc 编译 simple_math.c
./tcc book/ch01-intro/examples/simple_math.c -o /tmp/simple_math
/tmp/simple_math
```

**问题 1.5**：`-run` 模式和正常编译有什么区别？（提示：查看是否生成了文件。）

### 第六步：性能对比

```bash
# 测试 tcc 编译速度
time ./tcc examples/hello.c -o /tmp/hello_tcc

# 测试 gcc 编译速度（无优化）
time gcc -O0 examples/hello.c -o /tmp/hello_gcc

# 测试 gcc 编译速度（带优化）
time gcc -O2 examples/hello.c -o /tmp/hello_gcc_o2
```

**问题 1.6**：记录三种编译方式的耗时。tcc 比 gcc -O0 快多少倍？

### 第七步：自举测试

```bash
# 用 tcc 编译 tcc 自身
./tcc -o tcc_bootstrap tcc.c

# 验证自举的 tcc 能正常工作
./tcc_bootstrap -run examples/hello.c
```

**问题 1.7**：自举编译成功说明了什么？如果自举编译出的程序输出与原始 tcc 不同，意味着什么？

## 提交

回答所有 **问题 1.x**，并附上关键命令的输出截图或日志。


===== FILE: docs/ch01/exercises/ex2_trace.md =====
# 练习 2：追踪程序的编译阶段

## 目标

使用 TinyCC 的命令行选项，观察一个 C 程序在预处理、编译、汇编、链接各阶段的输出，加深对编译器各阶段功能的理解。

## 前提条件

- 已构建 TinyCC（参见练习 1）
- 对 C 语言有基本了解

## 被编译的程序

使用以下程序作为实验对象（也可以使用 `examples/simple_math.c`）：

```c
#define SQUARE(x) ((x) * (x))

static int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int x = 5;
    int y = SQUARE(x + 1);
    int z = add(y, 3);
    return z;
}
```

将此代码保存为 `/tmp/trace.c`。

## 步骤

### 阶段 1：预处理（-E）

```bash
tcc -E /tmp/trace.c -o /tmp/trace.i
```

**问题 2.1**：查看 `/tmp/trace.i` 的内容。回答以下问题：
- `SQUARE(x + 1)` 被展开成了什么？注意括号的作用。
- 文件中有多少行？为什么比原始源码多那么多行？
- 你是否看到了 `#` 开头的行标记？它们的格式是什么？有什么作用？

**提示**：`-P` 选项可以去掉行标记：`tcc -E -P /tmp/trace.c`

### 阶段 2：汇编代码生成（-S）

```bash
tcc -S /tmp/trace.c -o /tmp/trace.s
```

**问题 2.2**：查看 `/tmp/trace.s` 的内容。回答以下问题：
- 识别 `main` 函数对应的汇编代码段。
- `add` 函数是否出现在汇编输出中？为什么？（提示：`static` 关键字。）
- `SQUARE(x + 1)` 的计算对应哪些汇编指令？
- 如果你是 x86-64 平台，函数参数通过哪些寄存器传递？

### 阶段 3：目标文件生成（-c）

```bash
tcc -c /tmp/trace.c -o /tmp/trace.o
```

**问题 2.3**：使用工具分析目标文件：

```bash
# 查看目标文件类型
file /tmp/trace.o

# 查看节头（ELF section headers）
readelf -S /tmp/trace.o

# 查看符号表
readelf -s /tmp/trace.o

# 查看重定位条目（如果有）
readelf -r /tmp/trace.o
```

回答：
- 目标文件中有哪些节（section）？
- 符号表中列出了哪些符号？哪些是局部的（local），哪些是全局的（global）？
- `add` 函数出现在符号表中吗？它的绑定（bind）属性是什么？
- 有没有重定位条目？如果有，它们引用了什么符号？

### 阶段 4：链接（生成可执行文件）

```bash
tcc /tmp/trace.c -o /tmp/trace
```

**问题 2.4**：使用工具分析可执行文件：

```bash
# 查看文件类型
file /tmp/trace

# 查看程序头（program headers，描述如何加载到内存）
readelf -l /tmp/trace

# 查看动态段（dynamic section）
readelf -d /tmp/trace

# 查看使用了哪些动态库
ldd /tmp/trace
```

回答：
- 可执行文件的入口点地址是什么？
- 有哪些可加载段（LOAD segments）？它们的权限是什么（R, W, E）？
- 程序依赖哪些动态库？
- ELF 解释器（interpreter）的路径是什么？

### 阶段 5：运行

```bash
/tmp/trace
echo "Exit code: $?"
```

**问题 2.5**：程序的返回值是什么？为什么？（提示：追踪 `SQUARE(5+1)` 和 `add()` 的计算过程。）

### 综合分析

**问题 2.6**：填写下表，比较各阶段的输出规模：

| 阶段 | 选项 | 输出格式 | 输出大小（字节） | 输出行数 |
|:-----|:-----|:---------|:----------------|:---------|
| 预处理 | `-E` | C 源码 | ? | ? |
| 编译 | `-S` | 汇编代码 | ? | ? |
| 汇编 | `-c` | 目标文件 (ELF) | ? | N/A |
| 链接 | `-o` | 可执行文件 (ELF) | ? | N/A |

**问题 2.7**：使用 `trace_compile.sh` 脚本自动完成上述分析：

```bash
./book/ch01-intro/examples/trace_compile.sh /tmp/trace.c
```

比较脚本输出与你手动分析的结果是否一致。

## 提交

回答所有 **问题 2.x**，并附上关键命令的输出。


===== FILE: docs/ch01/exercises/ex3_modify.md =====
# 练习 3：修改 tcc 源码

## 目标

通过修改 TinyCC 编译器的源码并重新构建，验证你对编译器代码结构的理解。这是"自举"思维的入门练习——你将修改编译器本身。

## 前提条件

- 已构建 TinyCC（参见练习 1）
- 能够使用文本编辑器修改 C 源码
- 理解 `main()` 函数的基本结构

## 任务

修改 `tcc.c` 中的 `main()` 函数，在编译开始时打印一条自定义消息。

## 步骤

### 第一步：定位 main() 函数

打开 `tcc.c`，找到 `main()` 函数。它的原型是：

```c
int main(int argc, char **argv)
```

函数大约在文件的第 280 行附近（行号可能因版本略有差异）。

在 `main()` 函数体的开头，你会看到变量声明，然后是对 `tcc_new()` 的调用：

```c
int main(int argc, char **argv)
{
    TCCState *s, *s1;
    int ret, opt, n = 0, t = 0, done;
    unsigned start_time = 0, end_time = 0;
    const char *first_file;
    // ...

redo:
    argc = argc0, argv = argv0;
    s = s1 = tcc_new();
    // ...
```

### 第二步：添加 printf 语句

在 `tcc_new()` 调用之后（即 `s = s1 = tcc_new();` 这一行之后），添加一行 `printf` 调用：

```c
    s = s1 = tcc_new();
    printf("=== Hello from modified tcc! ===\n");
```

**注意**：`<stdio.h>` 已经通过 `tcc.h` 被间接包含，所以不需要额外添加 `#include`。

### 第三步：重新编译 tcc

```bash
make clean
make
```

### 第四步：验证修改

```bash
# 运行修改后的 tcc
./tcc --version
```

你应该在输出的开头看到：

```
=== Hello from modified tcc! ===
```

**注意**：`--version` 会触发 `main()` 中的正常流程，你的消息应该出现在版本信息之前。如果用 `-v` 选项（单个 `v`），流程略有不同（它在 `tcc_new()` 后就直接打印版本并返回），你的消息可能不会出现。请使用 `--version` 或正常编译来验证。

### 第五步：编译一个程序

```bash
./tcc -run book/ch01-intro/examples/hello.c
```

观察输出。你的自定义消息是否也出现了？为什么？

### 第六步：修改消息内容

将消息改为包含命令行参数信息：

```c
    s = s1 = tcc_new();
    printf("=== tcc modified: compiling with %d arguments ===\n", argc);
```

重新编译并测试：

```bash
make
./tcc book/ch01-intro/examples/hello.c -o /tmp/hello_test
./tcc -run book/ch01-intro/examples/hello.c
```

**问题 3.1**：`argc` 的值在不同调用方式下是多少？记录并解释。

### 第七步（进阶）：添加编译统计

在 `tcc.c` 的 `main()` 函数中，找到 `tcc_print_stats()` 的调用（在 `-bench` 选项的处理路径中）。研究 `TCCState` 中以下字段的含义：

```c
s->total_idents    // 编译期间遇到的标识符总数
s->total_lines     // 编译的源码行数
s->total_bytes     // 编译的源码字节数
```

在 `tcc_delete(s)` 之前，无条件打印一条编译统计消息：

```c
    printf("=== tcc stats: %d lines, %d idents, %u bytes ===\n",
           s->total_lines, s->total_idents, s->total_bytes);
    tcc_delete(s);
```

重新编译并使用不同大小的输入文件测试：

```bash
make
./tcc -run book/ch01-intro/examples/hello.c
./tcc -run book/ch01-intro/examples/simple_math.c
```

**问题 3.2**：两个程序的编译统计分别是多少？

## 思考题

**问题 3.3**：`main()` 函数中的 `redo:` 标签和 `goto redo;` 语句有什么作用？在什么情况下会触发重新编译？

**问题 3.4**：如果你在 `tcc_new()` 之前添加 printf，会发生什么？为什么？

**问题 3.5**：`tcc_delete(s)` 做了什么？如果注释掉这行会导致什么问题？

## 提交

- 回答所有 **问题 3.x**
- 提交你修改的 `tcc.c` 的 diff（使用 `git diff`）
- 附上运行修改后 tcc 的输出截图


===== FILE: docs/ch01/index.md =====
# 第一章：编译器导论与 TinyCC 概览

> *"Any sufficiently complicated C or Fortran program contains an ad hoc,
> informally-specified, bug-ridden, slow implementation of half of Common Lisp."*
> — Greenspun's Tenth Rule

> *"C is quirky, flawed, and an enormous success."*
> — Dennis Ritchie

---

## 1.1 什么是编译器

### 1.1.1 从源码到机器码

编译器（compiler）是一种将高级编程语言编写的**源程序**翻译为目标机器可直接执行的**低级代码**的程序。这个翻译过程不是简单的逐字替换——它涉及对源程序语法结构和语义含义的深层理解，以及对目标机器指令集的精确映射。

从形式化角度而言，编译器是一个函数：

```
compile : SourceCode -> TargetCode
```

其中 `SourceCode` 是满足某编程语言语法和语义约束的字符串集合，`TargetCode` 是目标机器指令编码的集合。

### 1.1.2 编译器、解释器与 JIT

程序执行的三种主要范式各有不同的权衡：

```
┌─────────────────────────────────────────────────────────────────────┐
│                    程序执行模型对比                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  编译器 (Compiler)                                                   │
│  ═══════════════                                                     │
│  源码 ──[编译]──> 目标文件 ──[链接]──> 可执行文件 ──[运行]──> 结果     │
│                                                                     │
│  特点: 编译一次, 运行多次。运行时无额外开销。                           │
│  代表: GCC, Clang, TinyCC                                            │
│                                                                     │
│  ┌─────────┐    ┌─────────┐    ┌──────────┐    ┌────────┐           │
│  │ hello.c │───>│ compiler│───>│ hello.o  │───>│ hello  │──> 输出    │
│  └─────────┘    └─────────┘    └──────────┘    └────────┘           │
│                                                                     │
│                                                                     │
│  解释器 (Interpreter)                                                │
│  ═══════════════════                                                 │
│  源码 ──[逐行解释执行]──> 结果                                        │
│                                                                     │
│  特点: 无需编译步骤, 但每次执行都需要重新解析源码, 运行速度慢。          │
│  代表: Python (CPython), Ruby (MRI), Bash                            │
│                                                                     │
│  ┌─────────┐    ┌─────────────┐                                     │
│  │ hello.py│───>│ interpreter │──> 逐行执行并输出                     │
│  └─────────┘    └─────────────┘                                     │
│                                                                     │
│                                                                     │
│  即时编译器 (JIT Compiler)                                            │
│  ═════════════════════════                                           │
│  源码/字节码 ──[运行时编译热点代码]──> 机器码 ──[执行]──> 结果          │
│                                                                     │
│  特点: 兼顾跨平台与运行速度, 首次执行较慢, 热点代码加速。               │
│  代表: Java (HotSpot), JavaScript (V8), .NET ( RyuJIT)               │
│                                                                     │
│  ┌─────────┐    ┌──────────┐    ┌─────────────┐    ┌────────┐       │
│  │ Foo.java│───>│ javac    │───>│ Foo.class   │───>│  JVM   │       │
│  └─────────┘    │ (前端)   │    │ (字节码)    │    │  JIT   │       │
│                 └──────────┘    └─────────────┘    │ 编译   │──>结果│
│                                                    │ 执行   │       │
│                                                    └────────┘       │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

三者的核心区别在于**编译时机**：

| 特性         | 编译器         | 解释器         | JIT 编译器       |
|:------------|:--------------|:--------------|:----------------|
| 编译时机      | 程序运行前      | 边运行边解释    | 运行时动态编译    |
| 运行速度      | 快             | 慢            | 快（热身后）      |
| 启动速度      | 慢（需编译）    | 快            | 快               |
| 错误检测时机   | 编译时         | 运行时         | 运行时            |
| 跨平台性      | 需重新编译      | 需要解释器      | 需要虚拟机        |

TinyCC 是一个**纯编译器**：它将 C 源码翻译为本地机器码，生成标准的 ELF（Linux）、PE（Windows）或 Mach-O（macOS）目标文件。但 TinyCC 同时支持 `-run` 选项，可以像脚本解释器一样直接运行 C 源码——这赋予了它类似解释器的便利性，但内部机制仍然是先编译再执行。

### 1.1.3 为什么学习编译器

理解编译器的工作原理有三个重要的实际意义：

1. **写出更高效的代码**：理解编译器如何翻译代码，可以写出更利于优化的程序。
2. **调试更有效率**：理解汇编输出和调试信息的生成方式，有助于定位底层 bug。
3. **掌握计算基础设施**：编译器是整个软件栈的根基；理解它，就理解了从源码到执行的完整链路。

选择 TinyCC 作为学习对象，是因为它的代码量小（约 7 万行 C 代码），架构清晰，且是**真正可用的编译器**——不是教学用的玩具。

---

## 1.2 编译器的经典阶段

一个编译器的内部工作通常被分解为若干**阶段**（phase）。虽然不同编译器的实现细节各异，但经典框架如下：

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      编译器的经典阶段                                      │
│                                                                          │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌───────────┐             │
│  │ 源代码    │──>│ 词法分析  │──>│ 语法分析  │──>│ 语义分析   │             │
│  │ (文本)   │   │ (Lexer)  │   │ (Parser) │   │ (Semantic)│             │
│  └──────────┘   └──────────┘   └──────────┘   └───────────┘             │
│                      │              │               │                     │
│                      v              v               v                     │
│                   token流       语法树/            带类型标注的             │
│                               递归下降             中间表示                │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 中间代码生成  │        │
│                                                  │ (IR Gen)     │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 代码优化     │        │
│                                                  │ (Optimize)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 目标代码生成  │        │
│                                                  │ (Code Gen)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 汇编/链接     │        │
│                                                  │ (Asm/Link)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                    可执行文件             │
└──────────────────────────────────────────────────────────────────────────┘
```

下面我们逐一讲解每个阶段。

### 1.2.1 词法分析（Lexical Analysis）

词法分析器（lexer / scanner / tokenizer）的任务是将源代码字符流转换为**记号流**（token stream）。

源代码在文件中表现为一连串的字符（字节）。词法分析器按照语言的**词法规则**（通常用正则表达式描述）将这些字符切分为有意义的基本单元——记号。例如，C 语言源码：

```c
int main(void) {
    return 0;
}
```

经过词法分析后，产生如下记号序列：

```
TOK_INT    "int"
TOK_IDENT  "main"
'('        "("
TOK_VOID   "void"
')'        ")"
'{'        "{"
TOK_RETURN "return"
TOK_CINT   0
';'        ";"
'}'        "}"
TOK_EOF
```

每个记号（token）是一个二元组 `<记号类型, 属性值>`。记号类型是一个枚举整数（如 `TOK_INT`），属性值是记号的具体内容（如标识符的名字、整数常量的值）。

词法分析需要处理的技术细节包括：

- **空白和注释的跳过**：空格、制表符、换行符和注释不产生记号。
- **预处理指令**：在 C 语言中，`#include`、`#define` 等预处理指令在词法阶段就需要被处理。
- **关键字与标识符的区分**：`int` 是关键字（`TOK_INT`），而 `integer` 是标识符（`TOK_IDENT`）。
- **数字常量的解析**：需要处理十进制、八进制（`0` 前缀）、十六进制（`0x` 前缀）、浮点数、后缀（`L`, `U`, `f`）等。
- **字符串常量的解析**：处理转义序列（`\n`, `\t`, `\\`）和拼接。

在 TinyCC 中，词法分析由 `tccpp.c`（预处理器 + 词法分析器）完成。核心函数是 `next()` 和 `next_nomacro()`，它们从输入缓冲区读取字符并返回下一个记号。记号的类型定义在 `tcctok.h` 中，通过 `DEF(id, str)` 宏展开为枚举值。

### 1.2.2 语法分析（Syntax Analysis）

语法分析器（parser）的任务是根据语言的**语法规则**（通常用上下文无关文法描述）将记号序列组织为**语法结构**。

在传统编译器中，语法分析通常产生一棵**抽象语法树**（Abstract Syntax Tree, AST）。例如：

```
                    ┌───────────┐
                    │ FunctionDecl│
                    │ name: main  │
                    │ ret: int    │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  Compound  │
                    │  Stmt      │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  Return    │
                    │  Stmt      │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  IntLit    │
                    │  val: 0    │
                    └───────────┘
```

语法分析器需要解决的核心问题包括：

- **表达式的优先级和结合性**：`a + b * c` 应被解析为 `a + (b * c)`。
- **二义性消除**：`if (a) if (b) s1 else s2` 中的 `else` 归属问题（悬垂 else 问题）。
- **函数声明与函数调用的区分**：`f(x)` 在不同上下文中含义不同。

### 1.2.3 语义分析（Semantic Analysis）

语义分析器在语法分析的基础上检查程序的**语义正确性**——即程序的含义是否符合语言规范。语法正确的程序不一定语义正确。例如：

```c
int x = "hello";    // 语法正确, 语义错误: 类型不兼容
int f() { return; } // 语法正确, 语义错误: 缺少返回值
int a[3]; a[5];     // 语法正确, 语义上可能有数组越界
```

语义分析的核心工作包括：

1. **类型检查**：验证表达式的类型是否合法，如赋值语句两侧的类型兼容性。
2. **类型推导/转换**：确定隐式类型转换规则（如 `int` 到 `float` 的提升）。
3. **符号表管理**：维护变量、函数、类型的作用域和绑定关系。
4. **声明匹配**：检查函数的声明与定义是否一致。
5. **常量求值**：计算编译期可确定的常量表达式。

### 1.2.4 中间代码生成与优化

在经典编译器（如 GCC、LLVM/Clang）中，经过语义分析后，源程序会被翻译为一种**中间表示**（Intermediate Representation, IR）。IR 是一种与源语言和目标机器都无关的抽象表示。

常见的 IR 形式包括：

- **三地址码**（Three-Address Code）：每条指令最多三个操作数。如 `t1 = a + b; t2 = t1 * c;`
- **SSA**（Static Single Assignment）：每个变量只被赋值一次。如 `x_1 = a_1 + b_1; x_2 = x_1 * c_1;`
- **LLVM IR**：LLVM 使用的带类型的汇编语言。

基于 IR，编译器可以执行各种**优化**，如常量折叠、死代码消除、循环不变量外提等。

### 1.2.5 目标代码生成

代码生成器将 IR（或直接从语义分析的结果）翻译为目标机器的汇编代码或机器码。这一步需要解决**寄存器分配**（哪些变量放在寄存器中）、**指令选择**（选择哪种机器指令来实现语义）、**指令调度**（指令排列顺序以利用流水线）等问题。

### 1.2.6 汇编与链接

最后阶段将汇编代码编码为二进制机器码（汇编器的工作），并将多个目标文件和库组合为最终的可执行文件（链接器的工作）。链接器解决的核心问题是**符号解析**——将函数调用和全局变量引用绑定到其定义所在的地址。

### 1.2.7 多遍编译 vs 单遍编译

上述阶段的组织方式分为两大流派：

**多遍编译**（multi-pass）：如 GCC 和 Clang，将编译过程明确分为多个独立的阶段，每阶段之间通过数据结构（AST、IR）传递信息。优点是每个阶段可以独立优化，便于支持多种源语言和目标机器。缺点是内存占用大，编译速度较慢。

**单遍编译**（single-pass）：如传统的 C 编译器（包括早期的 `cc`）和 TinyCC，在读取源代码的同时直接生成目标代码，不构建完整的 AST。优点是内存占用小、编译速度极快。缺点是优化能力有限，某些语言特性（如需要前向引用的特性）实现较复杂。

TinyCC 采用的是**单遍编译**架构。这是一个核心设计选择，深刻影响了整个代码库的组织方式。我们将在 1.4 节详细讨论。

---

## 1.3 TinyCC 项目简介

### 1.3.1 历史与作者

TinyCC（简称 tcc）由法国程序员 **Fabrice Bellard** 于 2001 年创建。Bellard 是计算机科学界的传奇人物，他同时还创建了：

- **QEMU**：广泛使用的开源机器模拟器和虚拟化平台。
- **FFmpeg**：多媒体处理框架（联合创始人）。
- **圆周率计算记录**：2009 年使用个人计算机计算了 2.7 万亿位圆周率。

TinyCC 的设计目标在项目的 `README` 文件中有清晰的陈述：

> **SMALL!** You can compile and execute C code everywhere, for example on rescue disks.
>
> **FAST!** tcc generates machine code for i386, x86_64, arm, aarch64 or riscv64. Compiles and links about 10 times faster than `gcc -O0`.
>
> **UNLIMITED!** Any C dynamic library can be used directly. TCC is heading toward full ISOC99 compliance. TCC can of course compile itself.

这三个词——小（Small）、快（Fast）、无限（Unlimited）——精确概括了 TinyCC 的设计哲学。

### 1.3.2 设计目标与特性

TinyCC 的设计目标可以概括为以下几个方面：

1. **极小的二进制大小**：tcc 可执行文件仅约 100-200KB，可以在软盘、救援磁盘等极度受限的环境中使用。
2. **极快的编译速度**：编译速度比 `gcc -O0` 快约 10 倍。这主要归功于单遍编译架构和零优化策略。
3. **C 脚本模式**：支持 `#!/usr/local/bin/tcc -run` 的 shebang 行，允许将 C 程序作为脚本直接运行。
4. **内嵌运行时**：支持 `-run` 选项在内存中编译并执行 C 代码，无需生成磁盘文件。
5. **边界检查**：可选的 `-b` 选项启用运行时内存边界检查，用于调试。
6. **自举能力**：tcc 可以编译自身——这是编译器正确性的有力证明。
7. **跨平台支持**：支持 Linux、Windows、macOS、FreeBSD 等操作系统，支持 i386、x86_64、ARM、AArch64、RISC-V 64 等目标架构。

### 1.3.3 版本与社区

本书基于 **TinyCC 0.9.28** 版本。该版本的主要更新包括：

- 新增 RISC-V 64 位目标架构支持。
- 原生 macOS（Darwin）支持。
- ARM 和 RISC-V 汇编器。
- `_Static_assert()` 和 `__attribute__((cleanup()))` 支持。
- `stdatomic.h` 支持。
- `asm goto` 支持。
- DWARF 调试信息格式支持。

TinyCC 以 **LGPL v2** 许可证发布。项目的邮件列表和 bug 跟踪在 `https://lists.nongnu.org/mailman/listinfo/tinycc-devel`。

### 1.3.4 tcc 能做什么，不能做什么

**tcc 能做的**：

- 编译大多数符合 C99 标准的 C 程序。
- 直接链接系统动态库（如 libc、libm）。
- 生成标准 ELF/PE/Mach-O 格式的目标文件和可执行文件。
- 作为 C "解释器"直接运行 C 源码。
- 生成调试信息（stabs 和 DWARF 格式）。
- 处理 GCC 的大量扩展语法（`__attribute__`、`__builtin_*` 等）。

**tcc 不擅长的**：

- 代码优化：tcc 几乎不做任何优化，直接生成简单的、逐语句对应的机器码。
- 完整的 C11/C23 支持：tcc 持续跟踪标准，但某些边缘特性可能未实现。
- 大型项目的并行编译：tcc 是单线程编译器。
- C++ 支持：tcc 仅支持 C 语言。

这些限制恰恰是 TinyCC 的设计选择：用优化能力换取编译速度和代码简洁性。

---

## 1.4 tcc 的单遍编译架构

### 1.4.1 什么是单遍编译

单遍编译（single-pass compilation）是指编译器在**一次扫描源代码的过程中**完成所有的分析和代码生成工作。与多遍编译器不同，单遍编译器不构建中间的抽象语法树（AST），不进行独立的优化阶段，而是在解析语法结构的同时直接输出目标代码。

```
多遍编译器 (GCC / Clang):

源码 ──> [词法分析] ──> token流 ──> [语法分析] ──> AST ──> [语义分析]
                                                              │
                                                              v
                                                       [IR 生成]
                                                              │
                                                              v
                                                        [IR 优化]
                                                              │
                                                              v
                                                       [代码生成]
                                                              │
                                                              v
                                                           机器码


单遍编译器 (TinyCC):

源码 ──> [词法分析 + 语法分析 + 语义分析 + 代码生成] ──> 机器码
              │
              └── 全部在一次扫描中完成, 不构建 AST
```

### 1.4.2 TinyCC 的单遍架构

在 TinyCC 中，单遍编译的具体实现方式是**边解析边生成**（parse-and-generate）。核心流程在 `tccgen.c` 的 `tccgen_compile()` 函数中：

```c
// tccgen.c 第 400 行
ST_FUNC int tccgen_compile(TCCState *s1)
{
    // ... 初始化 ...
    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();           // 读取第一个 token
    decl(VT_CONST);   // 从顶层声明开始递归下降解析
    gen_inline_functions(s1);
    // ... 收尾 ...
    return 0;
}
```

这里 `decl()` 函数是整个编译的核心递归下降入口。它处理顶层声明（变量声明、函数定义等）。当遇到函数定义时，`decl()` 调用 `block()` 来解析函数体，`block()` 又调用 `expr_eq()` 等表达式解析函数，而表达式解析函数在解析表达式的同时直接调用代码生成函数（如 `gadd()`, `gv()` 等）向当前代码段（`cur_text_section`）写入机器码。

整个过程的关键全局变量：

- `tok`：当前 token 的类型。
- `tokc`：当前 token 的值（如果是常量）。
- `vtop`：虚拟栈栈顶指针。TinyCC 使用一个**虚拟栈**来追踪表达式求值过程中的中间值。
- `ind`：当前代码段的写入位置（字节偏移）。
- `loc`：当前函数的局部变量栈帧偏移。

### 1.4.3 虚拟栈

TinyCC 引入了一个精巧的抽象——**虚拟栈**（virtual stack），用 `SValue` 结构数组实现。在解析表达式时，操作数和中间结果被推入虚拟栈；在需要时，代码生成器将虚拟栈上的值"溢出"（spill）到实际的机器寄存器或栈上。

例如，表达式 `a + b * c` 的处理过程：

```
解析 a:    vpush_sym(a)         -> 虚拟栈: [a]
解析 b:    vpush_sym(b)         -> 虚拟栈: [a, b]
解析 c:    vpush_sym(c)         -> 虚拟栈: [a, b, c]
解析 *:    gen_op(TOK_STAR)     -> 虚拟栈: [a, b*c]
解析 +:    gen_op(TOK_PLUS)     -> 虚拟栈: [a+b*c]
```

`gen_op()` 在两个操作数都是编译期常量时可以直接计算结果（常量折叠），否则生成相应的机器指令。

### 1.4.4 与 GCC / Clang 的对比

| 维度           | TinyCC               | GCC                  | Clang/LLVM           |
|:--------------|:---------------------|:---------------------|:---------------------|
| 编译遍数       | 单遍                  | 多遍                  | 多遍                 |
| 中间表示       | 无 AST，虚拟栈        | GIMPLE (SSA IR)       | LLVM IR (SSA)        |
| 优化级别       | 无优化                | O0-O3, Os, Ofast      | O0-O3, Os, Oz        |
| 编译速度       | 极快 (~10x gcc -O0)   | 慢                    | 中等                  |
| 输出代码质量   | 低（逐语句翻译）       | 高                    | 高                    |
| 代码量         | ~7万行                | ~1500万行             | ~500万行              |
| 适用场景       | 快速编译、脚本、嵌入式  | 生产环境、高性能计算   | 生产环境、开发调试    |
| 支持语言       | C                     | C/C++/Fortran/Go/...  | C/C++/Obj-C/...      |

GCC 的编译过程经过多次变换：源码 -> AST -> GENERIC -> GIMPLE -> SSA GIMPLE -> RTL -> 机器码。每一步都可以进行独立的优化。这种架构的优势在于优化能力和模块化，代价是巨大的代码量和较慢的编译速度。

TinyCC 选择了一条完全不同的路：放弃优化能力，换取极致的编译速度和极小的代码量。对于不需要优化的场景（快速原型、C 脚本、教学、嵌入式启动代码），TinyCC 是更好的选择。

### 1.4.5 单遍编译的代价

单遍架构也带来了固有的限制：

1. **前向引用问题**：在单遍扫描中，当遇到一个函数调用时，如果被调用函数尚未定义，编译器不知道其参数类型和返回类型。TinyCC 的解决方法是：对未声明的函数调用发出隐式声明警告，并假设返回 `int`（C89 的传统行为）。

2. **无法进行跨语句优化**：由于没有全局视图，无法进行循环优化、公共子表达式消除等优化。

3. **类型信息的延迟处理**：结构体的大小在遇到完整定义之前可能未知，TinyCC 使用 `Sym` 链表来延迟处理前向引用的结构体。

---

## 1.5 源码文件地图

TinyCC 0.9.28 的源码树由约 7 万行 C 代码组成。以下是每个文件的功能说明和代码行数。理解这些文件的职责，是深入阅读源码的第一步。

### 1.5.1 核心编译器文件

| 文件 | 行数 | 职责 |
|:-----|-----:|:-----|
| `tcc.h` | 2032 | **主头文件**。定义了所有核心数据结构（`TCCState`, `Sym`, `CType`, `SValue`, `Section` 等）、类型编码常量（`VT_INT`, `VT_FUNC` 等）、token 编码、以及所有模块的函数声明。所有 `.c` 文件都包含此头文件。 |
| `tccpp.c` | 3961 | **预处理器和词法分析器**。实现 `next()` 和 `next_nomacro()` 函数，负责将源代码字符流分割为 token。同时实现 C 预处理器的全部功能：`#include`、`#define`（含宏展开）、`#if`/`#ifdef` 条件编译、`#pragma` 等。 |
| `tccgen.c` | 8986 | **语法分析器 + 语义分析器 + 代码生成器**。这是 tcc 最大的单个文件。实现递归下降解析器，直接将 C 语法结构翻译为目标机器代码。核心函数包括 `decl()`（声明解析）、`block()`（语句块解析）、`expr_eq()`（表达式解析）、`unary()`（一元表达式）等。 |
| `tccasm.c` | 1525 | **内联汇编支持**。处理 `asm()` / `__asm__()` 语句，解析 AT&T 或 Intel 格式的汇编语法，将其翻译为目标机器码。 |
| `tccelf.c` | 4186 | **ELF 文件格式处理**。负责生成和读取 ELF 格式的目标文件和可执行文件，包括节（section）管理、符号表管理、重定位处理、动态链接支持（GOT/PLT）。 |
| `libtcc.c` | 2267 | **库接口和初始化**。提供 `tcc_new()`、`tcc_delete()`、`tcc_compile()`、`tcc_run()` 等公共 API。当以 ONE_SOURCE 模式编译时，此文件 `#include` 所有其他 `.c` 文件，形成单一编译单元。 |
| `tccdbg.c` | 2676 | **调试信息生成**。生成 stabs 和 DWARF 格式的调试信息，使编译产物可用 `gdb` 等调试器调试。 |
| `tccrun.c` | 1586 | **运行时执行引擎**。实现 `-run` 选项的内存编译和执行功能。将编译后的机器码加载到可执行内存中并跳转执行。 |
| `tccpe.c` | 2313 | **PE 文件格式处理**（Windows）。生成 PE 格式的 `.exe` 和 `.dll` 文件。 |
| `tccmacho.c` | 2477 | **Mach-O 文件格式处理**（macOS）。生成 Mach-O 格式的可执行文件和动态库。 |
| `tcctools.c` | 651 | **辅助工具**。实现 `tcc -ar`（静态库创建）和 `tcc -impdef`（导入库定义文件生成）功能。 |

### 1.5.2 目标架构后端

每种目标架构有三个文件：代码生成器（`-gen.c`）、链接器辅助（`-link.c`）和汇编器（`-asm.c`）。编译时只包含目标架构对应的文件。

| 文件 | 行数 | 目标架构 | 职责 |
|:-----|-----:|:---------|:-----|
| `i386-gen.c` | 1326 | Intel 32 位 x86 | 生成 32 位 x86 机器码。调用者负责寄存器分配。 |
| `i386-link.c` | 360 | Intel 32 位 x86 | 处理 32 位 x86 的重定位类型。 |
| `i386-asm.c` | 1750 | Intel 32 位 x86 | 解析 32 位 x86 内联汇编。 |
| `x86_64-gen.c` | 2332 | AMD64 / x86-64 | 生成 64 位 x86 机器码。比 i386 复杂，需处理 SSE 寄存器。 |
| `x86_64-link.c` | 452 | AMD64 / x86-64 | 处理 64 位重定位类型。 |
| `arm-gen.c` | 2369 | ARM (32 位) | 生成 ARMv4+ 机器码。 |
| `arm-link.c` | 472 | ARM (32 位) | ARM 重定位处理。 |
| `arm-asm.c` | 3092 | ARM (32 位) | ARM 内联汇编解析器。 |
| `arm64-gen.c` | 2353 | AArch64 (ARM 64 位) | 生成 ARMv8 64 位机器码。 |
| `arm64-link.c` | 406 | AArch64 | AArch64 重定位处理。 |
| `arm64-asm.c` | 2276 | AArch64 | AArch64 内联汇编解析器。 |
| `riscv64-gen.c` | 1480 | RISC-V 64 位 | 生成 RISC-V 64 位机器码。 |
| `riscv64-link.c` | 440 | RISC-V 64 位 | RISC-V 重定位处理。 |
| `riscv64-asm.c` | 3059 | RISC-V 64 位 | RISC-V 内联汇编解析器。 |
| `c67-gen.c` | 2543 | TMS320C67xx (DSP) | 生成 TI C67 DSP 机器码。 |
| `c67-link.c` | 125 | TMS320C67xx | C67 重定位处理。 |
| `il-gen.c` | 657 | .NET IL | .NET 中间语言代码生成（实验性）。 |
| `tcccoff.c` | 951 | COFF 格式 | COFF 目标文件格式处理（C67 使用）。 |

### 1.5.3 头文件

| 文件 | 行数 | 职责 |
|:-----|-----:|:-----|
| `libtcc.h` | 116 | 公共 API 头文件。定义 `tcc_new()` 等函数原型，供外部程序使用 libtcc 库。 |
| `tcclib.h` | 82 | 简化的 libc 头文件，用于在软盘等受限环境中替代标准头文件。 |
| `tcctok.h` | 441 | Token 定义文件。使用 `DEF(id, str)` 宏定义 C 关键字和内置函数名。 |
| `tccdefs_.h` | 329 | 内置宏定义。定义 `__SIZE_TYPE__`、`__INT_MAX__` 等编译器内置宏。 |
| `config.h` | 18 | 构建配置。通常由 `configure` 脚本生成。 |
| `elf.h` | 3325 | ELF 文件格式定义（数据结构和常量）。 |
| `coff.h` | 446 | COFF 文件格式定义。 |
| `dwarf.h` | 1046 | DWARF 调试信息格式定义。 |
| `stab.h` | 17 | Stabs 调试信息格式定义。 |
| `stab.def` | — | Stabs 类型定义。 |
| `i386-asm.h` | 490 | i386 汇编指令和寄存器定义。 |
| `x86_64-asm.h` | 559 | x86-64 汇编指令和寄存器定义。 |
| `i386-tok.h` | 332 | i386 汇编 token 定义。 |
| `arm-tok.h` | 406 | ARM 汇编 token 定义。 |
| `arm64-tok.h` | 840 | AArch64 汇编 token 定义。 |
| `riscv64-tok.h` | 612 | RISC-V 汇编 token 定义。 |
| `il-opcodes.h` | 251 | .NET IL 操作码定义。 |

### 1.5.4 运行时库和构建文件

| 文件/目录 | 职责 |
|:----------|:-----|
| `lib/` | 运行时支持库源码。包含 `libtcc1.c`（基本运行时函数如 `__udivdi3`）、`bcheck.c`（边界检查运行时）、`alloca.S`（栈分配实现）、`atomic.S`（原子操作）等。编译后生成 `libtcc1.a`。 |
| `include/` | tcc 自带的 C 标准头文件子集。包含 `stdarg.h`、`stddef.h`、`float.h`、`stdbool.h`、`tccdefs.h` 等。 |
| `Makefile` | 构建系统主文件。 |
| `configure` | 配置脚本，检测系统环境并生成 `config.mak`。 |
| `tests/` | 测试套件。包含 `tcctest.c`（主测试文件）等。 |
| `tcc-doc.texi` | Texinfo 格式的完整文档。 |
| `VERSION` | 版本号文件（当前内容：`0.9.28rc`）。 |

---

## 1.6 核心数据结构预览

理解 TinyCC 的关键在于理解其核心数据结构。本节对五个最重要的结构体做初步介绍，后续章节将深入分析。

### 1.6.1 TCCState：编译器全局状态

`TCCState`（定义在 `tcc.h` 中）是 TinyCC 最重要的结构体。它封装了编译器的**所有状态**——包括命令行选项、搜索路径、错误处理、节管理、符号表等。每个编译实例对应一个 `TCCState` 实例。

```c
// tcc.h 中 TCCState 的关键成员（简化）
struct TCCState {
    // --- 命令行选项 ---
    unsigned char verbose;          // -v 详细输出
    unsigned char nostdinc;         // -nostdinc 不搜索标准头文件路径
    unsigned char nostdlib;         // -nostdlib 不链接标准库
    unsigned char char_is_unsigned; // -funsigned-char
    unsigned char gnu_ext;          // GNU 扩展语法
    unsigned char tcc_ext;          // TinyCC 扩展语法
    int output_type;                // 输出类型: TCC_OUTPUT_EXE / _OBJ / _DLL / _MEMORY / _PREPROCESS
    unsigned int cversion;          // C 标准版本 (199901, 201112, ...)

    // --- 搜索路径 ---
    char **include_paths;           // -I 用户头文件搜索路径
    int nb_include_paths;
    char **sysinclude_paths;        // 系统头文件搜索路径
    int nb_sysinclude_paths;
    char **library_paths;           // -L 库搜索路径
    int nb_library_paths;

    // --- 预处理器状态 ---
    BufferedFile *include_stack[INCLUDE_STACK_SIZE]; // #include 文件栈
    int ifdef_stack[IFDEF_STACK_SIZE];                // #ifdef 条件栈

    // --- 节 (Section) 管理 ---
    Section **sections;             // 所有节的数组
    int nb_sections;
    Section *text_section;          // 代码段 (.text)
    Section *data_section;          // 数据段 (.data)
    Section *rodata_section;        // 只读数据段 (.rodata)
    Section *bss_section;           // 未初始化数据段 (.bss)
    Section *symtab_section;        // 符号表 (.symtab)
    Section *cur_text_section;      // 当前正在写入的代码段

    // --- 错误处理 ---
    int nb_errors;                  // 编译错误计数
    jmp_buf error_jmp_buf;          // 错误恢复点

    // --- 文件列表 ---
    struct filespec **files;        // 命令行输入文件列表
    int nb_files;
    char *outfile;                  // 输出文件名

    // ... 更多成员 (调试, PE/Mach-O 特定, 运行时, 性能统计等)
};
```

`TCCState` 通过 `tcc_new()` 创建，通过 `tcc_delete()` 销毁。使用 libtcc API 的典型模式：

```c
TCCState *s = tcc_new();
tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
tcc_add_file(s, "hello.c");
tcc_run(s, 0, NULL);
tcc_delete(s);
```

### 1.6.2 Sym：符号表条目

`Sym`（symbol）是 TinyCC 中表示所有程序实体的核心结构——变量、函数、类型、标签、枚举常量、宏定义等，全部用 `Sym` 表示。

```c
// tcc.h 中 Sym 的定义（简化）
typedef struct Sym {
    int v;             // 符号的 token 编号 (标识符在符号表中的唯一 ID)
    unsigned short r;  // 关联的寄存器或位置 (VT_CONST / VT_LOCAL / ...)
    struct SymAttr a;  // 符号属性 (对齐、packed、weak、visibility 等)

    union {
        struct {
            int c;     // 关联数值: 栈帧偏移 / ELF 符号索引 / 枚举值
            union {
                int sym_scope;       // 局部变量的作用域层级
                struct FuncAttr f;   // 函数属性 (调用约定、noreturn 等)
            };
        };
        long long enum_val;          // 枚举常量的值 (当 IS_ENUM_VAL 时)
        int *d;                      // 宏定义的 token 流 (当为宏时)
    };

    CType type;        // 符号的类型信息
    struct Sym *prev;  // 指向前一个同名符号 (作用域栈)
    struct Sym *prev_tok; // 指向同一 token 的前一个定义 (哈希链)
} Sym;
```

符号通过两条链管理：

- **作用域栈**（`prev` 链）：将所有符号链接为一个栈。内层作用域的符号在外层作用域同名符号之前。查找时从栈顶向下搜索。
- **token 哈希链**（`prev_tok` 链）：同一标识符名的所有定义通过此链连接，用于宏展开和符号查找。

全局符号维护在 `global_stack`，局部符号维护在 `local_stack`。进入新作用域时压栈，退出时弹栈。

### 1.6.3 CType：类型表示

`CType` 是 TinyCC 的类型描述结构，极其紧凑——只有两个字段：

```c
typedef struct CType {
    int t;            // 类型编码 (位域组合)
    struct Sym *ref;  // 引用的符号 (struct/union/enum 的定义, 或函数的参数列表)
} CType;
```

类型编码 `t` 是一个 32 位整数，通过位域组合来编码复杂的类型信息：

```
  31                    20  19   16 15  12 11    8 7  6  5 4 3  0
 ┌────────────────────────┬───────┬──────┬───────┬───┬──┬─┬──────┐
 │  bitfield shift/size   │storage│qualif│attrib │VLA│BF│A│ base │
 │  (struct fields only)  │       │      │       │   │  │R│ type │
 └────────────────────────┴───────┴──────┴───────┴───┴──┴─┴──────┘

 基本类型 (VT_BTYPE, 低 4 位):
   0  = VT_VOID      void
   1  = VT_BYTE      signed char
   2  = VT_SHORT     short
   3  = VT_INT       int
   4  = VT_LLONG     long long
   5  = VT_PTR       pointer (ref 指向所指类型)
   6  = VT_FUNC      function (ref 指向参数链表)
   7  = VT_STRUCT    struct/union (ref 指向字段链表)
   8  = VT_FLOAT     float
   9  = VT_DOUBLE    double
  10  = VT_LDOUBLE   long double
  11  = VT_BOOL      _Bool

 修饰符 (高位标志):
   VT_UNSIGNED  = 0x0010   unsigned
   VT_ARRAY     = 0x0040   array (同时有 VT_PTR)
   VT_CONSTANT  = 0x0100   const
   VT_VOLATILE  = 0x0200   volatile
   VT_EXTERN    = 0x1000   extern
   VT_STATIC    = 0x2000   static
   VT_TYPEDEF   = 0x4000   typedef
```

例如，`const unsigned int *` 的类型编码为：

```
VT_PTR | VT_CONSTANT | VT_UNSIGNED | VT_INT = 0x0040 | 0x0100 | 0x0010 | 3 = 0x0153
ref -> CType { t = VT_UNSIGNED | VT_INT = 0x0013, ref = NULL }
```

### 1.6.4 SValue：虚拟栈元素

`SValue` 表示虚拟栈上的一个值。它记录了值的类型、存放位置、以及关联的符号。

```c
typedef struct SValue {
    CType type;           // 值的类型
    unsigned short r;     // 存放位置 (寄存器编号 / VT_CONST / VT_LOCAL / VT_CMP / VT_JMP)
    unsigned short r2;    // 第二个寄存器 (用于 long long 等双寄存器值)
    union {
        struct { int jtrue, jfalse; };  // 条件跳转的正向引用链 (VT_JMP/VT_JMPI)
        CValue c;                        // 常量值 (VT_CONST)
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; // 比较操作信息 (VT_CMP)
        struct Sym *sym;                            // 关联的符号 (VT_SYM | VT_CONST)
    };
} SValue;
```

`r` 字段的编码决定了值的含义：

| `r` 值 | 含义 |
|:--------|:-----|
| 0-15 | 值在物理寄存器中（寄存器编号） |
| `VT_CONST` (0x30) | 值是常量（在 `c` 字段中） |
| `VT_LOCAL` (0x32) | 值在栈帧中（偏移量在 `c.i` 中） |
| `VT_CMP` (0x33) | 值是条件比较的结果 |
| `VT_JMP` (0x34) | 值来自条件跳转（真分支） |
| `VT_JMPI` (0x35) | 值来自条件跳转（假分支） |

虚拟栈（`vstack` 数组）和栈顶指针（`vtop`）是 TinyCC 代码生成的核心机制。每次解析一个操作数时，一个新的 `SValue` 被压入栈；每次应用一个运算符时，栈顶的一个或两个值被消费，结果被压回栈。

### 1.6.5 Section：ELF 节

`Section` 表示目标文件中的一个**节**（section），如 `.text`（代码）、`.data`（已初始化数据）、`.rodata`（只读数据）、`.bss`（未初始化数据）等。

```c
typedef struct Section {
    unsigned long data_offset;    // 当前写入偏移
    unsigned char *data;          // 节的数据缓冲区
    unsigned long data_allocated; // 已分配的缓冲区大小
    TCCState *s1;                 // 所属的编译器状态

    // ELF 节头字段
    int sh_name;           // 节名在字符串表中的索引
    int sh_num;            // 节编号
    int sh_type;           // 节类型 (SHT_PROGBITS, SHT_SYMTAB, ...)
    int sh_flags;          // 节标志 (SHF_ALLOC, SHF_WRITE, SHF_EXECINSTR, ...)
    unsigned long sh_size; // 节大小

    struct Section *link;  // 关联的节 (如符号表关联字符串表)
    struct Section *reloc; // 对应的重定位节
    char name[1];          // 节名 (变长, 如 ".text")
} Section;
```

`TCCState` 中维护了一个预定义的节集合：

```c
// TCCState 中的预定义节
Section *text_section;      // .text   - 可执行代码
Section *data_section;      // .data   - 已初始化可写数据
Section *rodata_section;    // .rodata - 只读数据 (字符串常量等)
Section *bss_section;       // .bss    - 未初始化数据
Section *symtab_section;    // .symtab - 符号表
Section *cur_text_section;  // 当前正在写入的代码段
Section *got;               // .got    - 全局偏移表 (动态链接)
Section *plt;               // .plt    - 过程链接表 (动态链接)
```

代码生成器向 `cur_text_section` 写入机器码字节，语义分析器向 `data_section` 写入全局变量的初始值，字符串常量被放入 `rodata_section`。

---

## 1.7 编译一个简单程序的完整流程

本节通过一个具体的 "hello world" 程序，追踪它在 TinyCC 内部经历的完整编译过程。

### 1.7.1 示例程序

```c
// hello.c
#include <stdio.h>

int main(void)
{
    printf("Hello, TinyCC!\n");
    return 0;
}
```

### 1.7.2 第一步：初始化

当我们在命令行执行 `tcc hello.c -o hello` 时，`tcc.c` 的 `main()` 函数首先被调用：

```c
// tcc.c main() 核心流程（简化）
int main(int argc, char **argv)
{
    TCCState *s;
    s = tcc_new();                    // 1. 创建编译器状态
    tcc_parse_args(s, &argc, &argv);  // 2. 解析命令行参数
    set_environment(s);               // 3. 设置环境变量 (C_INCLUDE_PATH 等)
    tcc_set_output_type(s, TCC_OUTPUT_EXE); // 4. 设置输出类型

    // 5. 编译所有输入文件
    while (/* 每个输入文件 */) {
        tcc_add_file(s, f->name);     // 编译单个文件
    }

    // 6. 输出可执行文件
    tcc_output_file(s, s->outfile);

    tcc_delete(s);                    // 7. 清理
    return ret;
}
```

`tcc_new()` 调用链：

```
tcc_new()                          // libtcc.c
  ├── tcc_mallocz(sizeof(TCCState)) // 分配并清零 TCCState
  ├── tccelf_new(s)                 // 初始化 ELF 相关
  └── tccpp_new(s)                  // 初始化预处理器
```

### 1.7.3 第二步：文件处理入口

`tcc_add_file()` 最终调用 `tcc_add_file_internal()`，该函数根据文件扩展名决定处理方式：

```c
// libtcc.c（简化逻辑）
ST_FUNC int tcc_add_file_internal(TCCState *s1, const char *filename, int flags)
{
    // 检测文件类型
    if (是 C 源码 (.c)) {
        // 预处理 + 编译
        tcc_compile(s1);      // 调用预处理器和编译器
    } else if (是汇编 (.S/.s)) {
        // 汇编器
    } else if (是目标文件 (.o)) {
        // 直接加载目标文件
        tcc_load_object_file(s1, fd, 0);
    } else if (是库 (.a/.so/.dll)) {
        // 加载库
        tcc_add_library(s1, filename);
    }
}
```

### 1.7.4 第三步：预处理

对于 C 源文件，`tccpp.c` 的预处理器首先被激活：

```c
// tccpp.c: preprocess_start() 设置预处理器
void preprocess_start(TCCState *s1, int filetype)
{
    // 初始化内置宏
    //   __TINYC__, __STDC__, __linux__, __x86_64__ 等
    //   定义 __SIZE_TYPE__, __INT_MAX__ 等类型相关宏

    // 处理命令行 -D/-U 选项
    // 处理命令行 -include 选项

    // 打开第一个文件
    tcc_open_bf(s1, filename, IO_BUF_SIZE);
}
```

当预处理器遇到 `#include <stdio.h>` 时：

1. 在 `sysinclude_paths` 中搜索 `stdio.h`。
2. 找到后，创建新的 `BufferedFile`，压入 `include_stack`。
3. 继续处理新文件的内容。
4. `stdio.h` 中可能包含更多的 `#include`，递归处理。
5. 遇到 `#define` 时，在 `define_stack` 中注册宏定义。
6. 遇到 `#ifdef`/`#endif` 时，维护 `ifdef_stack`。

### 1.7.5 第四步：词法分析

预处理完成后，源码被转换为 token 流。`next()` 函数（`tccpp.c`）是词法分析的核心入口：

```c
// tccpp.c: next() 的工作流程（概念性描述）
void next(void)
{
    // 1. 从输入缓冲区读取下一个非空白字符
    // 2. 根据首字符判断 token 类型:
    //    - 字母或 _   -> 标识符或关键字
    //    - 数字        -> 数字常量
    //    - "           -> 字符串常量
    //    - #           -> 预处理指令
    //    - 运算符符号  -> 运算符 token
    // 3. 将结果存入全局变量 tok 和 tokc
    // 4. 处理宏展开 (如果当前 token 是宏名)
}
```

对于 `printf("Hello, TinyCC!\n")`，词法分析产生的 token 序列是：

```
TOK_IDENT  "printf"
'('
TOK_STR    "Hello, TinyCC!\n"
')'
';'
```

### 1.7.6 第五步：语法分析 + 代码生成

`tccgen.c` 的 `decl()` 函数开始解析。对于 `#include <stdio.h>` 中声明的函数和 `main` 函数的定义：

```
decl(VT_CONST) 被调用
  │
  ├── 解析 #include 中的外部声明
  │   遇到 "int printf(const char *, ...);" 等
  │   → sym_push2(&global_stack, TOK_PRINTF, ...) 注册符号
  │   → 在 symtab_section 中添加 ELF 符号
  │
  ├── 遇到 "int main(void) {"
  │   ├── 识别 return type: int
  │   ├── 识别 function name: main
  │   ├── 注册函数符号到 global_stack
  │   ├── 创建新作用域 (local_scope++)
  │   └── 调用 block() 解析函数体
  │       │
  │       ├── printf("Hello, TinyCC!\n");
  │       │   ├── 解析 printf 为函数调用
  │       │   ├── 参数 "Hello, TinyCC!\n" 被放入 rodata_section
  │       │   ├── 生成: lea rdi, [字符串地址]   (x86-64 第一个参数)
  │       │   ├── 生成: call printf@plt         (调用 printf)
  │       │   └── 生成: (结果在 eax 中, 但被丢弃)
  │       │
  │       └── return 0;
  │           ├── 将 0 加载到 eax (返回值寄存器)
  │           └── 生成: ret
  │
  └── 解析完毕, 退出
```

具体的机器码生成过程（以 x86-64 为例）：

```c
// x86_64-gen.c 中的关键函数（概念性描述）

// 将立即数加载到寄存器
static void load(int r, SValue *v) {
    if (v->r == VT_CONST) {
        // mov $imm, %reg
        // 或 lea addr(%rip), %reg (对于全局变量)
    }
}

// 函数调用
static void gfunc_call(int nb_args) {
    // 按照 System V AMD64 ABI:
    // 参数依次放入 rdi, rsi, rdx, rcx, r8, r9 (整数参数)
    // 浮点参数放入 xmm0-xmm7
    // 多余参数压栈
    // 调用目标地址
}
```

### 1.7.7 第六步：输出 ELF 文件

`tccelf.c` 的 `tcc_output_file()` 将所有编译结果输出为 ELF 可执行文件：

```
tcc_output_file(s, "hello")
  │
  ├── 1. 解析所有未解析的符号
  │   查找 printf 等外部符号是否在链接的库中
  │
  ├── 2. 处理重定位
  │   将 .text 中的 call 指令目标地址从符号引用
  │   转换为 PLT 条目地址
  │
  ├── 3. 生成 ELF 头
  │   e_ident, e_type (ET_EXEC), e_machine (EM_X86_64), ...
  │
  ├── 4. 生成程序头表 (Program Header Table)
  │   描述内存段的加载方式:
  │   PT_LOAD (text, 只读+可执行)
  │   PT_LOAD (data, 可读+可写)
  │   PT_DYNAMIC (动态链接信息)
  │   PT_INTERP (/lib64/ld-linux-x86-64.so.2)
  │
  ├── 5. 写入各节内容
  │   .text, .rodata, .data, .bss
  │   .symtab, .strtab, .shstrtab
  │   .rela.text, .plt, .got, .got.plt
  │   .dynamic, .dynsym, .dynstr
  │
  └── 6. 生成节头表 (Section Header Table)
```

### 1.7.8 完整流程总结

```
tcc hello.c -o hello
    │
    ├── main() [tcc.c]
    │   ├── tcc_new()                 创建编译器状态
    │   ├── tcc_parse_args()          解析 "-o hello" 等参数
    │   └── tcc_add_file("hello.c")   开始编译
    │
    ├── tcc_add_file_internal() [libtcc.c]
    │   ├── 识别为 C 源文件
    │   └── tcc_compile()
    │
    ├── preprocess_start() [tccpp.c]
    │   ├── 初始化内置宏 (__TINYC__ 等)
    │   └── 打开 hello.c
    │
    ├── tccgen_compile() [tccgen.c]
    │   ├── next() -> #include <stdio.h>
    │   │   └── 预处理器: 加载 stdio.h, 注册宏和声明
    │   ├── decl(VT_CONST)
    │   │   └── 遇到 int main(void)
    │   │       ├── sym_push() 注册 main 符号
    │   │       └── block() 解析函数体
    │   │           ├── printf("Hello, TinyCC!\n");
    │   │           │   ├── 字符串放入 .rodata
    │   │           │   └── 生成 call printf@plt
    │   │           └── return 0;
    │   │               └── 生成 mov $0, %eax; ret
    │   └── gen_inline_functions() 编译延迟的内联函数
    │
    ├── tcc_output_file("hello") [tccelf.c]
    │   ├── 符号解析和重定位处理
    │   ├── 生成 ELF 头、程序头表
    │   ├── 写入 .text, .rodata, .plt, .got 等
    │   └── 生成节头表
    │
    └── tcc_delete() 清理所有内存
```

整个过程在一台现代计算机上耗时通常不到 10 毫秒。

---

## 1.8 构建和测试 tcc

### 1.8.1 获取源码

TinyCC 源码可以通过多种方式获取：

```bash
# 从 Git 仓库克隆
git clone https://repo.or.cz/tinycc.git

# 或者下载发布版本的 tarball
wget https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.28.tar.bz2
tar xjf tcc-0.9.28.tar.bz2
```

### 1.8.2 配置

```bash
cd tinycc
./configure
```

`configure` 脚本检测系统的编译器、头文件位置、库路径等，生成 `config.mak` 文件。常用选项：

```bash
# 查看所有配置选项
./configure --help

# 常用选项
./configure --prefix=/usr/local        # 安装路径 (默认)
./configure --cc=gcc                   # 用于编译 tcc 的编译器
./configure --enable-cross             # 构建交叉编译器
./configure --targetarm-...            # 设置交叉编译目标
./configure --extra-cflags="-O2"       # 额外的编译选项
./configure --disable-static           # 构建动态库版 libtcc
```

`configure` 会生成 `config.mak`，其中包含：

```makefile
# config.mak 示例 (x86-64 Linux)
CC=gcc
AR=ar
CONFIG_TCCDIR="/usr/local/lib/tcc"
CONFIG_SYSROOT=""
CONFIG_TCC_CRTPREFIX="/usr/lib"
CONFIG_TCC_ELFINTERP="/lib64/ld-linux-x86-64.so.2"
# ...
```

### 1.8.3 编译

```bash
make
```

这个命令执行以下操作：

1. **编译 libtcc.a**：将 `libtcc.c`（以及它 include 的 `tccpp.c`、`tccgen.c` 等）编译为静态库。
2. **编译 tcc**：将 `tcc.c` 链接 `libtcc.a` 生成 `tcc` 可执行文件。
3. **编译 libtcc1.a**：将 `lib/` 中的运行时支持代码编译为目标文件并打包为静态库。

在 ONE_SOURCE 模式（默认）下，`libtcc.c` 通过 `#include` 将所有核心 `.c` 文件组合为一个编译单元，这简化了构建过程并允许编译器进行更好的优化（虽然 tcc 本身不做优化）。

### 1.8.4 测试

```bash
make test
```

这会运行 `tests/` 目录下的测试套件。主要测试包括：

- **tcctest.c**：覆盖面广泛的 C 语言特性测试，包括边界条件和微妙的行为。
- **libtcc_test.c**：测试 libtcc API 的正确性。
- **boundtest.c**：边界检查功能测试。
- **abitest.c**：ABI（应用程序二进制接口）兼容性测试。
- **asmtest.S**：汇编器测试。

测试通过后，输出类似：

```
total: 8 passed, 0 failed
```

### 1.8.5 安装

```bash
make install
```

这会将以下文件安装到系统中：

```
/usr/local/bin/tcc              # 编译器可执行文件
/usr/local/lib/tcc/             # tcc 运行时目录
/usr/local/lib/tcc/libtcc1.a    # 运行时支持库
/usr/local/lib/tcc/include/     # 自带头文件
/usr/local/lib/libtcc.a         # libtcc 静态库
/usr/local/include/libtcc.h     # libtcc 公共头文件
/usr/local/man/man1/tcc.1       # 手册页
```

### 1.8.6 验证安装

```bash
# 检查版本
tcc -v

# 编译并运行一个简单程序
echo '#include <stdio.h>
int main() { printf("hello\n"); return 0; }' > /tmp/hello.c
tcc -run /tmp/hello.c

# 或者编译为可执行文件
tcc /tmp/hello.c -o /tmp/hello
/tmp/hello
```

### 1.8.7 用 tcc 编译 tcc（自举测试）

自举（bootstrapping）是编译器正确性的终极测试。TinyCC 可以编译自身：

```bash
# 用系统编译器 (gcc) 先构建 tcc
make clean
./configure
make

# 用刚构建的 tcc 重新编译自身
./tcc -o tcc2 tcc.c

# 验证新的 tcc 能正常工作
./tcc2 -run tests/ex1.c
```

如果 `tcc2` 能正确编译和运行程序，说明 tcc 生成的代码是正确的——它能够正确地编译一个与自身功能等价的编译器。

---

## 1.9 本章小结与练习

### 1.9.1 本章小结

本章介绍了编译器的基本概念和 TinyCC 的总体架构。核心要点：

1. **编译器**是将高级语言翻译为低级代码的程序，与解释器（逐行执行）和 JIT 编译器（运行时编译）有本质区别。

2. **编译的经典阶段**包括词法分析、语法分析、语义分析、中间代码生成、优化和目标代码生成。不同编译器的阶段划分和组织方式差异很大。

3. **TinyCC** 由 Fabrice Bellard 于 2001 年创建，以极小的代码量（约 7 万行）、极快的编译速度（10 倍于 gcc -O0）和 ANSI C99 合规性为目标。当前版本 0.9.28 支持 i386、x86_64、ARM、AArch64、RISC-V 64 等架构。

4. **单遍编译**是 TinyCC 最重要的架构特征：在一次扫描源码的过程中，边解析边生成代码，不构建 AST，不做优化。这带来了极致的编译速度，代价是代码质量不如多遍编译器。

5. **核心数据结构**包括 `TCCState`（编译器全局状态）、`Sym`（符号表条目）、`CType`（类型编码）、`SValue`（虚拟栈元素）和 `Section`（ELF 节）。它们是理解后续章节的基础。

6. **源码文件地图**展示了每个 `.c` 和 `.h` 文件的职责，为后续深入阅读提供导航。

7. **构建和测试**只需 `./configure && make && make test && make install` 四个命令。tcc 支持自举（编译自身）。

### 1.9.2 练习

**练习 1：构建与验证**（基础）

从源码构建 TinyCC，运行测试套件，并使用 tcc 编译运行 `examples/hello.c`。记录编译耗时并与 GCC 对比。

（详见 `exercises/ex1_build.md`）

**练习 2：编译阶段追踪**（进阶）

使用 tcc 的 `-E`、`-S`、`-c` 选项，分别查看一个 C 程序在预处理、汇编、目标文件阶段的输出。分析每个阶段输出的内容和含义。

（详见 `exercises/ex2_trace.md`）

**练习 3：修改 tcc 源码**（高阶）

修改 `tcc.c` 中的 `main()` 函数，在编译开始时输出一条自定义消息。重新构建 tcc 并验证修改生效。

（详见 `exercises/ex3_modify.md`）

### 1.9.3 推荐阅读

1. Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools* (2nd ed.). Pearson.（"龙书"，编译器理论的经典教材。）
2. Bellard, F. (2002). "TCC: Tiny C Compiler." https://bellard.org/tcc/
3. TinyCC 官方文档：源码中的 `tcc-doc.texi`。
4. Fabrice Bellard 的个人主页：https://bellard.org/ （包含 QEMU、FFmpeg 等项目的链接。）


===== FILE: docs/ch02/examples/test_tokens.c =====
/*
 * test_tokens.c - 包含各种类型 token 的测试文件
 *
 * 这个文件的设计目的是覆盖 C 语言中尽可能多的 token 类型。
 * 可以配合 token_dump.c 使用，也可以直接用 tcc -E 查看预处理输出。
 *
 * 使用方法:
 *   tcc -E test_tokens.c          # 查看预处理输出
 *   tcc -E -dM test_tokens.c      # 查看预定义宏
 */

#include <stdio.h>
#include <wchar.h>

/* ===== 2.1 控制流关键字 ===== */
void test_control_flow(void)
{
    int i = 0;

    if (i == 0) {
        i = 1;
    } else {
        i = 2;
    }

    while (i < 10) {
        i++;
        if (i == 5) continue;
        if (i == 8) break;
    }

    for (i = 0; i < 10; i++) {
        /* loop body */
    }

    do {
        i--;
    } while (i > 0);

    switch (i) {
    case 0:
        i = -1;
        break;
    default:
        i = 0;
        break;
    }

    goto label;
label:
    return;
}

/* ===== 2.2 类型关键字 ===== */
void test_types(void)
{
    /* 基本类型 */
    char c = 'a';
    signed char sc = -1;
    unsigned char uc = 255;
    short s = 1;
    unsigned short us = 2;
    int i = 42;
    unsigned int ui = 100u;
    long l = 1000L;
    unsigned long ul = 2000UL;
    long long ll = 100000LL;
    unsigned long long ull = 200000ULL;
    float f = 3.14f;
    double d = 2.718;
    long double ld = 1.234L;
    void *p = (void *)0;

    /* C99 类型 */
    _Bool b = 1;

    /* 结构体和联合体 */
    struct Point { int x, y; };
    struct Point pt = { 10, 20 };

    union Data { int i; float f; };
    union Data data;
    data.i = 42;

    /* 枚举 */
    enum Color { RED, GREEN, BLUE };
    enum Color color = RED;

    /* sizeof 和 typedef */
    typedef int MyInt;
    MyInt mi = sizeof(int);
    int sz = sizeof(struct Point);

    /* 使用变量以避免警告 */
    (void)c; (void)sc; (void)uc; (void)s; (void)us;
    (void)ui; (void)l; (void)ul; (void)ll; (void)ull;
    (void)f; (void)d; (void)ld; (void)p; (void)b;
    (void)pt; (void)data; (void)color; (void)mi; (void)sz;
}

/* ===== 2.3 存储类和修饰符 ===== */
extern int extern_var;
static int static_var = 0;
const int const_var = 100;
volatile int vol_var;
register int reg_var;

/* ===== 2.4 运算符 ===== */
int test_operators(int a, int b)
{
    int result = 0;

    /* 算术运算符 */
    result = a + b;
    result = a - b;
    result = a * b;
    result = a / b;
    result = a % b;

    /* 一元运算符 */
    result = -a;
    result = !a;
    result = ~a;
    a++;
    a--;
    ++a;
    --a;

    /* 关系运算符 */
    result = (a == b);
    result = (a != b);
    result = (a < b);
    result = (a > b);
    result = (a <= b);
    result = (a >= b);

    /* 逻辑运算符 */
    result = (a && b);
    result = (a || b);

    /* 位运算符 */
    result = a & b;
    result = a | b;
    result = a ^ b;
    result = a << 2;
    result = a >> 3;

    /* 复合赋值运算符 */
    result += a;
    result -= a;
    result *= a;
    result /= a;
    result %= a;
    result &= a;
    result |= a;
    result ^= a;
    result <<= 1;
    result >>= 1;

    /* 三元运算符 */
    result = (a > b) ? a : b;

    /* 逗号运算符 */
    result = (a++, b++, a + b);

    /* 结构体成员访问 */
    struct S { int x; };
    struct S s = { 42 };
    struct S *sp = &s;
    result = s.x;
    result = sp->x;

    /* 指针操作 */
    int *ptr = &a;
    result = *ptr;

    return result;
}

/* ===== 2.5 字符串和字符常量 ===== */
void test_strings(void)
{
    /* 普通字符串 */
    const char *s1 = "hello world";
    const char *s2 = "line1\nline2\ttab";
    const char *s3 = "escape: \\ \" \' \? \a \b \f \v";

    /* 十六进制转义 */
    const char *s4 = "\x48\x65\x6C\x6C\x6F";  /* "Hello" */

    /* 八进制转义 */
    const char *s5 = "\110\145\154\154\157";   /* "Hello" */

    /* 宽字符串 */
    const wchar_t *ws = L"wide string";

    /* 字符常量 */
    char c1 = 'A';
    char c2 = '\n';
    char c3 = '\x41';
    wchar_t wc = L'Z';

    /* 多字节字符串拼接 */
    const char *s6 = "part1 " "part2 " "part3";

    (void)s1; (void)s2; (void)s3; (void)s4; (void)s5;
    (void)ws; (void)c1; (void)c2; (void)c3; (void)wc;
    (void)s6;
}

/* ===== 2.6 数字常量 ===== */
void test_numbers(void)
{
    /* 十进制整数 */
    int dec1 = 0;
    int dec2 = 42;
    int dec3 = 2147483647;

    /* 八进制整数 */
    int o1 = 0777;
    int o2 = 0123;

    /* 十六进制整数 */
    int h1 = 0xFF;
    int h2 = 0xDEADBEEF;
    int h3 = 0X1a2B;

    /* 二进制整数 (GCC 扩展) */
    int b1 = 0b1010;
    int b2 = 0B11110000;

    /* 带后缀整数 */
    unsigned int u1 = 100u;
    unsigned int u2 = 100U;
    long l1 = 100l;
    long l2 = 100L;
    unsigned long ul1 = 100ul;
    unsigned long ul2 = 100UL;
    long long ll1 = 100ll;
    long long ll2 = 100LL;
    unsigned long long ull1 = 100ull;
    unsigned long long ull2 = 100ULL;

    /* 混合后缀 */
    unsigned long ul3 = 100LU;
    unsigned long long ull3 = 100ULL;

    /* 十进制浮点数 */
    float f1 = 3.14f;
    float f2 = 3.14F;
    double d1 = 3.14;
    double d2 = .5;
    double d3 = 5.;
    double d4 = 1e10;
    double d5 = 1.5e-3;
    double d6 = 1.5E+3;
    long double ld1 = 3.14L;

    /* 十六进制浮点数 */
    double hf1 = 0x1.0p1;
    double hf2 = 0x1.8p+10;
    double hf3 = 0xAp-4;

    (void)dec1; (void)dec2; (void)dec3;
    (void)d1; (void)d2; (void)d3;
    (void)o1; (void)o2;
    (void)h1; (void)h2; (void)h3;
    (void)b1; (void)b2;
    (void)u1; (void)u2; (void)l1; (void)l2;
    (void)ul1; (void)ul2; (void)ll1; (void)ll2;
    (void)ull1; (void)ull2; (void)ul3; (void)ull3;
    (void)f1; (void)f2; (void)d4; (void)d5; (void)d6;
    (void)ld1;
    (void)hf1; (void)hf2; (void)hf3;
}

/* ===== 2.7 预处理器指令 ===== */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STRINGIFY(x) #x
#define CONCAT(a, b) a ## b
#define EMPTY

#ifdef __linux__
static const char *platform = "linux";
#elif defined(__APPLE__)
static const char *platform = "apple";
#else
static const char *platform = "other";
#endif

#ifndef HEADER_GUARD
#define HEADER_GUARD
#endif

/* 行号指令 */
#line 1000 "synthetic.c"

/* 条件编译嵌套 */
#if defined(__x86_64__)
#  if __SIZEOF_POINTER__ == 8
static int arch_bits = 64;
#  else
static int arch_bits = 32;
#  endif
#elif defined(__i386__)
static int arch_bits = 32;
#else
static int arch_bits = 0;
#endif

/* ===== 2.8 GCC 扩展语法 ===== */
/* Note: __int128 is a GCC extension; tcc supports it in expressions but not in typedef */
long long big_val = 0;

static inline __attribute__((always_inline))
int always_inline_func(int x)
{
    return x * 2;
}

__attribute__((noreturn))
static void never_returns(void)
{
    while (1) {}
}

struct __attribute__((packed)) PackedStruct {
    char c;
    int i;
};

struct __attribute__((aligned(16))) AlignedStruct {
    int data[4];
};

typedef int __attribute__((mode(SI))) int32_mode;

/* typeof */
static int test_typeof(int x)
{
    typeof(x) y = x + 1;
    return y;
}

/* __label__ */
static int test_local_label(int x)
{
    __label__ done;
    if (x < 0) goto done;
    x = x * 2;
done:
    return x;
}

/* statement expression */
static int test_stmt_expr(int x)
{
    return ({
        int _tmp = x * x;
        _tmp + 1;
    });
}

/* ===== 2.9 内联汇编 ===== */
static void test_asm(void)
{
    int result;
    /* x86 内联汇编 */
    __asm__ __volatile__ (
        "movl $42, %0"
        : "=r"(result)
        :
        : "memory"
    );
    (void)result;
}

/* ===== 2.10 C11 特性 ===== */
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");

/* _Generic */
#define type_name(x) _Generic((x),    \
    int: "int",                         \
    float: "float",                     \
    double: "double",                   \
    default: "other"                    \
)

static void test_generic(void)
{
    const char *name = type_name(42);
    (void)name;
}

/* _Alignas and _Alignof */
static _Alignas(64) int aligned_var;
static int align_test = _Alignof(long long);

/* _Atomic */
static _Atomic int atomic_var;

/* _Noreturn */
_Noreturn void exit_forever(int code);

/* ===== 2.11 内建函数 ===== */
static void test_builtins(void)
{
    /* __builtin_constant_p */
    int is_const = __builtin_constant_p(42);

    /* __builtin_expect */
    long val = __builtin_expect(1, 1);

    /* __builtin_types_compatible_p */
    int compat = __builtin_types_compatible_p(int, int);

    /* __builtin_choose_expr */
    int chosen = __builtin_choose_expr(1, 42, 0);

    /* __builtin_frame_address */
    void *frame = __builtin_frame_address(0);

    /* __builtin_return_address */
    void *ret = __builtin_return_address(0);

    (void)is_const; (void)val; (void)compat;
    (void)chosen; (void)frame; (void)ret;
}

/* ===== 2.12 特殊标识符 ===== */
static void test_special_ident(void)
{
    const char *func_name = __func__;
    const char *func_name2 = __FUNCTION__;
    int line = __LINE__;
    const char *file = __FILE__;
    (void)func_name; (void)func_name2;
    (void)line; (void)file;
}

/* ===== 2.13 注释样式 ===== */
/* This is a C-style comment */
// This is a C++-style comment
/* Multi
   line
   comment */
// Multi \
   line \
   C++ comment with backslash continuation

/* ===== 2.14 空白和续行 ===== */
int    spaced_out    =    42   ;

int \
continued \
= \
100;

/* ===== main ===== */
int main(void)
{
    test_control_flow();
    test_types();
    test_strings();
    test_numbers();
    always_inline_func(1);
    test_typeof(42);
    test_local_label(1);
    test_stmt_expr(3);
    test_asm();
    test_generic();
    test_builtins();
    test_special_ident();
    return 0;
}


===== FILE: docs/ch02/examples/token_dump.c =====
/*
 * token_dump.c - 使用 tcc 库 API 从输入文件中提取并打印所有 token
 *
 * 编译方法:
 *   gcc -o token_dump token_dump.c -I../.. -L../../ -ltcc -ldl
 *
 * 使用方法:
 *   ./token_dump test_tokens.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tcc 库头文件 */
#include "libtcc.h"

/*
 * token_category - 返回 token 编号所属的类别描述
 */
static const char *token_category(int tok)
{
    if (tok == -1)                  return "EOF";
    if (tok == 10)                  return "LINEFEED";
    if (tok >= 32 && tok < 127)     return "SINGLE_CHAR";
    if (tok >= 0x80 && tok <= 0x8F) return "INTERNAL_OP";
    if (tok >= 0x90 && tok <= 0x9F) return "CONDITIONAL";
    if (tok >= 0xA0 && tok <= 0xAF) return "MULTI_CHAR";
    if (tok >= 0xB0 && tok <= 0xB9) return "ASSIGN_OP";
    if (tok >= 0xC0 && tok <= 0xCF) return "CONSTANT";
    if (tok >= 256)                 return "IDENT_OR_KW";
    return "OTHER";
}

/*
 * main - 主函数
 *
 * 使用 libtcc 的 tcc_preprocess() 接口获取预处理后的 token 流。
 * 注意: 这是一个演示程序，展示 tcc token 系统的基本用法。
 * 完整的 token dump 需要直接链接 tcc 内部符号。
 */
int main(int argc, char **argv)
{
    TCCState *s;
    FILE *fp;
    char *buf;
    long file_size;
    const char *filename;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.c>\n", argv[0]);
        fprintf(stderr, "\nDump all tokens from a C source file.\n");
        fprintf(stderr, "Requires libtcc to be installed.\n");
        return 1;
    }

    filename = argv[1];

    /* Read input file into memory */
    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s'\n", filename);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buf = (char *)malloc(file_size + 1);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(fp);
        return 1;
    }
    fread(buf, 1, file_size, fp);
    buf[file_size] = '\0';
    fclose(fp);

    /* Create a TCC state for preprocessing */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Error: tcc_new() failed\n");
        free(buf);
        return 1;
    }

    /* Output preprocessed tokens */
    {
        tcc_set_output_type(s, TCC_OUTPUT_PREPROCESS);
        tcc_output_file(s, "/dev/stdout");
    }

    printf("\n--- Demonstrating tcc keyword token system ---\n\n");

    /*
     * Demonstrates the tcctok.h DEF() macro pattern:
     * Each DEF(id, str) associates a token ID with a string.
     * In the real compiler, these are enumerated starting at TOK_IDENT (256).
     */
    printf("Sample keyword tokens (from tcctok.h):\n");
    printf("  %-30s  %s\n", "TOKEN NAME", "STRING");
    printf("  %-30s  %s\n", "------------------------------", "------");
    printf("  %-30s  %s\n", "TOK_IF",    "if");
    printf("  %-30s  %s\n", "TOK_ELSE",  "else");
    printf("  %-30s  %s\n", "TOK_WHILE", "while");
    printf("  %-30s  %s\n", "TOK_FOR",   "for");
    printf("  %-30s  %s\n", "TOK_RETURN","return");
    printf("  %-30s  %s\n", "TOK_INT",   "int");
    printf("  %-30s  %s\n", "TOK_VOID",  "void");
    printf("  %-30s  %s\n", "TOK_STRUCT","struct");

    printf("\nSample operator tokens:\n");
    printf("  %-6s  %-14s  %s\n", "VALUE", "CATEGORY", "OPERATOR");
    printf("  %-6s  %-14s  %s\n", "------", "--------------", "--------");
    printf("  0x%-3x  %-14s  %s\n", 0x94, "CONDITIONAL", "==");
    printf("  0x%-3x  %-14s  %s\n", 0x95, "CONDITIONAL", "!=");
    printf("  0x%-3x  %-14s  %s\n", 0x9e, "CONDITIONAL", "<=");
    printf("  0x%-3x  %-14s  %s\n", 0x90, "CONDITIONAL", "&&");
    printf("  0x%-3x  %-14s  %s\n", 0x91, "CONDITIONAL", "||");
    printf("  0x%-3x  %-14s  %s\n", 0x82, "INTERNAL_OP", "++");
    printf("  0x%-3x  %-14s  %s\n", 0x80, "INTERNAL_OP", "--");
    printf("  0x%-3x  %-14s  %s\n", 0xa0, "MULTI_CHAR",  "->");
    printf("  0x%-3x  %-14s  %s\n", 0xb0, "ASSIGN_OP",   "+=");
    printf("  0x%-3x  %-14s  %s\n", 0xb8, "ASSIGN_OP",   "<<=");

    printf("\nSample constant tokens:\n");
    printf("  %-6s  %-14s  %s\n", "VALUE", "CATEGORY", "TYPE");
    printf("  %-6s  %-14s  %s\n", "------", "--------------", "----");
    printf("  0x%-3x  %-14s  %s\n", 0xc0, "CONSTANT", "char constant");
    printf("  0x%-3x  %-14s  %s\n", 0xc2, "CONSTANT", "int constant");
    printf("  0x%-3x  %-14s  %s\n", 0xc3, "CONSTANT", "unsigned int");
    printf("  0x%-3x  %-14s  %s\n", 0xc8, "CONSTANT", "string");
    printf("  0x%-3x  %-14s  %s\n", 0xca, "CONSTANT", "float");
    printf("  0x%-3x  %-14s  %s\n", 0xcb, "CONSTANT", "double");
    printf("  0x%-3x  %-14s  %s\n", 0xcd, "CONSTANT", "PP number");
    printf("  0x%-3x  %-14s  %s\n", 0xce, "CONSTANT", "PP string");

    tcc_delete(s);
    free(buf);

    return 0;
}


===== FILE: docs/ch02/exercises/ex1_tokens.md =====
# 练习 2.1：识别 Token

## 目标

通过手写分析，深入理解 tcc 的 token 分类和编号系统。

## 任务

对于以下 C 代码片段，识别每一个 token 并填写其 token 编号和类别。

### 代码片段

```c
int main(void) {
    int x = 42;
    int *p = &x;
    if (*p != 0) {
        printf("x = %d\n", x);
    }
    return 0;
}
```

### 参考表格

| Token 编号范围 | 类别 |
|---------------|------|
| -1 | TOK_EOF |
| 0x20-0x7F | 单字符（ASCII 值即编号） |
| 0x80-0x8F | 内部操作符 |
| 0x90-0x9F | 条件/比较操作符 |
| 0xA0-0xAF | 多字符操作符 |
| 0xB0-0xB9 | 复合赋值操作符 |
| 0xC0-0xCF | 带值常量 |
| >= 256 | 标识符/关键字 |

### 答题模板

请为每个 token 填写以下信息：

| 序号 | Token 文本 | Token 编号 | 类别 |
|------|-----------|-----------|------|
| 1 | `int` | ??? | ??? |
| 2 | `main` | ??? | ??? |
| 3 | `(` | ??? | ??? |
| 4 | `void` | ??? | ??? |
| 5 | `)` | ??? | ??? |
| 6 | `{` | ??? | ??? |
| 7 | `int` | ??? | ??? |
| 8 | `x` | ??? | ??? |
| 9 | `=` | ??? | ??? |
| 10 | `42` | ??? | ??? |
| 11 | `;` | ??? | ??? |
| 12 | `int` | ??? | ??? |
| 13 | `*` | ??? | ??? |
| 14 | `p` | ??? | ??? |
| 15 | `=` | ??? | ??? |
| 16 | `&` | ??? | ??? |
| 17 | `x` | ??? | ??? |
| 18 | `;` | ??? | ??? |
| 19 | `if` | ??? | ??? |
| 20 | `(` | ??? | ??? |
| 21 | `*` | ??? | ??? |
| 22 | `p` | ??? | ??? |
| 23 | `!=` | ??? | ??? |
| 24 | `0` | ??? | ??? |
| 25 | `)` | ??? | ??? |
| 26 | `{` | ??? | ??? |
| 27 | `printf` | ??? | ??? |
| 28 | `(` | ??? | ??? |
| 29 | `"x = %d\n"` | ??? | ??? |
| 30 | `,` | ??? | ??? |
| 31 | `x` | ??? | ??? |
| 32 | `)` | ??? | ??? |
| 33 | `;` | ??? | ??? |
| 34 | `}` | ??? | ??? |
| 35 | `return` | ??? | ??? |
| 36 | `0` | ??? | ??? |
| 37 | `;` | ??? | ??? |
| 38 | `}` | ??? | ??? |

## 提示

1. 单字符 token 的编号就是其 ASCII 值（十进制）
2. 关键字的编号 >= 256，具体值取决于 `tcctok.h` 中的顺序
3. `!=` 的编号是 0x95（TOK_NE）
4. `42` 在预处理阶段是 TOK_PPNUM，在解析阶段是 TOK_CINT (0xC2)
5. `"x = %d\n"` 在预处理阶段是 TOK_PPSTR，在解析阶段是 TOK_STR (0xC8)

## 思考题

1. 为什么 `int` 和 `x` 的 token 编号都 >= 256，但一个是关键字一个是标识符？
2. `*` 在 `int *p` 和 `*p != 0` 中的 token 编号相同吗？tcc 如何区分这两种语义？
3. 如果代码中有多字节 UTF-8 标识符（如 `int 变量 = 1;`），tcc 如何处理？

## 参考答案

### 关键答案提示

- 单字符 token：`(` = 40, `)` = 41, `{` = 123, `}` = 125, `;` = 59,
  `=` = 61, `*` = 42, `&` = 38, `,` = 44
- `!=` = 0x95 (TOK_NE, 十进制 149)
- 关键字：`int` = TOK_INT, `void` = TOK_VOID, `if` = TOK_IF,
  `return` = TOK_RETURN（具体编号取决于 tcctok.h 中的顺序）
- 标识符：`main`、`x`、`p`、`printf` 的编号 >= 256，
  每个标识符首次出现时分配新编号
- 常量：预处理阶段 `42` -> TOK_PPNUM (0xCD)，
  `"`...`"` -> TOK_PPSTR (0xCE)

### 关于思考题

1. **关键字 vs 标识符**：关键字和标识符共享同一个编号空间（>= 256）。
   区分方法是检查编号是否在 `[TOK_IDENT, TOK_UIDENT)` 范围内。
   `TOK_UIDENT` 等于 `TOK_DEFINE`，即 `tcctok.h` 中第一个预处理器关键字的编号。

2. **`*` 的歧义**：`*` 的 token 编号始终是 42（ASCII 值）。
   语义歧义由解析器（parser）通过上下文解决，而不是词法分析器。

3. **UTF-8 标识符**：tcc 在 `tccpp_new()` 中将 128-255 范围的所有字节
   标记为 `IS_ID`，因此 UTF-8 多字节序列可以作为标识符的一部分。


===== FILE: docs/ch02/exercises/ex2_hash.md =====
# 练习 2.2：手算标识符哈希

## 目标

理解 tcc 的哈希函数 `TOK_HASH_FUNC` 的工作原理，并手动计算标识符的哈希值。

## 背景

tcc 使用以下哈希函数：

```c
#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) + ((h) << 5) + ((h) >> 27) + (c))
```

其中：
- `h` 是当前的哈希值
- `c` 是当前字符的 ASCII 值（作为 unsigned char）
- `<<` 是左移，`>>` 是右移（都是无符号逻辑移位）
- 初始值 `TOK_HASH_INIT = 1`

最终的桶索引为：`h & (TOK_HASH_SIZE - 1)`，其中 `TOK_HASH_SIZE = 16384`。

## 任务

### 任务 A：计算 `"main"` 的哈希值

请按照 `TOK_HASH_FUNC` 的定义，逐步计算字符串 `"main"` 的哈希值。

**提示**：`m` = 109, `a` = 97, `i` = 105, `n` = 110

请填写以下表格（每步 32 位无符号整数运算）：

| 步骤 | 字符 | c (ASCII) | h (旧值) | h << 5 | h >> 27 | 新 h = h + (h<<5) + (h>>27) + c |
|------|------|-----------|----------|--------|---------|--------------------------------|
| 0    | (初始) | - | 1 | - | - | 1 |
| 1    | `m`  | 109 | ??? | ??? | ??? | ??? |
| 2    | `a`  |  97 | ??? | ??? | ??? | ??? |
| 3    | `i`  | 105 | ??? | ??? | ??? | ??? |
| 4    | `n`  | 110 | ??? | ??? | ??? | ??? |

最终哈希值：`h = ???`

桶索引：`h & 0x3FFF = ???`（即 `h % 16384`）

### 任务 B：计算 `"printf"` 的哈希值

使用相同的方法计算 `"printf"` 的哈希值。

**提示**：`p` = 112, `r` = 114, `i` = 105, `n` = 110, `t` = 116, `f` = 102

| 步骤 | 字符 | c (ASCII) | h (旧值) | h << 5 | h >> 27 | 新 h |
|------|------|-----------|----------|--------|---------|------|
| 0    | (初始) | - | 1 | - | - | 1 |
| 1    | `p`  | 112 | ??? | ??? | ??? | ??? |
| 2    | `r`  | 114 | ??? | ??? | ??? | ??? |
| 3    | `i`  | 105 | ??? | ??? | ??? | ??? |
| 4    | `n`  | 110 | ??? | ??? | ??? | ??? |
| 5    | `t`  | 116 | ??? | ??? | ??? | ??? |
| 6    | `f`  | 102 | ??? | ??? | ??? | ??? |

### 任务 C：编写验证程序

编写一个 C 程序来验证你的手算结果：

```c
#include <stdio.h>

#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) + ((h) << 5) + ((h) >> 27) + (c))
#define TOK_HASH_SIZE 16384

unsigned int compute_hash(const char *str, int len)
{
    unsigned int h = TOK_HASH_INIT;
    int i;
    for (i = 0; i < len; i++)
        h = TOK_HASH_FUNC(h, (unsigned char)str[i]);
    return h;
}

int main(void)
{
    unsigned int h;

    h = compute_hash("main", 4);
    printf("\"main\"  : hash = 0x%08x (%u), bucket = %u\n",
           h, h, h & (TOK_HASH_SIZE - 1));

    h = compute_hash("printf", 6);
    printf("\"printf\": hash = 0x%08x (%u), bucket = %u\n",
           h, h, h & (TOK_HASH_SIZE - 1));

    return 0;
}
```

编译并运行：`gcc -o hash_verify hash_verify.c && ./hash_verify`

## 分析

### 任务 A 的计算过程（详细）

让我们逐步计算 `"main"` 的哈希值。所有运算都是 32 位无符号整数。

**初始值**：`h = 1`

**步骤 1：处理字符 `m`（ASCII 109）**

```
h       = 1
h << 5  = 1 * 32 = 32
h >> 27 = 1 / 134217728 = 0
新 h    = 1 + 32 + 0 + 109 = 142
```

**步骤 2：处理字符 `a`（ASCII 97）**

```
h       = 142
h << 5  = 142 * 32 = 4544
h >> 27 = 142 / 134217728 = 0
新 h    = 142 + 4544 + 0 + 97 = 4783
```

**步骤 3：处理字符 `i`（ASCII 105）**

```
h       = 4783
h << 5  = 4783 * 32 = 153056
h >> 27 = 4783 / 134217728 = 0
新 h    = 4783 + 153056 + 0 + 105 = 157944
```

**步骤 4：处理字符 `n`（ASCII 110）**

```
h       = 157944
h << 5  = 157944 * 32 = 5054208
h >> 27 = 157944 / 134217728 = 0
新 h    = 157944 + 5054208 + 0 + 110 = 5212262
```

**最终结果**：

```
哈希值 = 5212262 = 0x4F8746
桶索引 = 5212262 & 0x3FFF = 5212262 & 16383 = 678
```

### 哈希函数的特性分析

`TOK_HASH_FUNC(h, c) = h + (h << 5) + (h >> 27) + c` 可以改写为：

```
新 h = h * 33 + (h >> 27) + c
```

这是因为 `h + (h << 5) = h + h * 32 = h * 33`。

- **乘数 33**：这是一个经典的字符串哈希乘数（DJB2 使用 33，Java 的 `String.hashCode()` 使用 31）
- **`h >> 27` 的作用**：提供额外的位混合，减少哈希冲突。如果不加这个项，`h * 33 + c` 在高位信息上可能会丢失
- **为什么是 27**：32 位整数中，右移 27 位留下高 5 位的信息，恰好与左移 5 位互补

### 思考题

1. 为什么 tcc 选择 16384（2^14）作为哈希表大小？如果改为 10000 会怎样？
2. `TOK_HASH_INIT` 为什么是 1 而不是 0？如果用 0 作为初始值，对空字符串 `""` 的哈希结果是什么？
3. 在 `next_nomacro()` 的快速路径中，哈希值是在扫描标识符的同时计算的。这比先扫描完再计算哈希有什么优势？

### 验证答案

运行任务 C 的程序，应该得到：

```
"main"  : hash = 0x004f8746 (5212262), bucket = 678
"printf": hash = ???, bucket = ???
```

（`"printf"` 的值请通过程序验证。）


===== FILE: docs/ch02/exercises/ex3_modify.md =====
# 练习 2.3：为 tcc 添加新关键字

## 目标

通过修改 tcc 源码，为 C 语言添加一个新的关键字。这个练习将帮助你理解：

1. `tcctok.h` 中 `DEF` 宏的工作机制
2. 关键字 token 的分配流程
3. 预处理器和解析器如何识别关键字
4. token 编号系统的设计

## 背景

tcc 的关键字系统基于 `tcctok.h` 文件中的 `DEF(id, str)` 宏。每个 `DEF` 条目自动获得一个从 `TOK_IDENT`（256）开始递增的 token 编号。关键字和普通标识符共享同一个编号空间，区分它们的方法是检查编号是否在 `[TOK_IDENT, TOK_UIDENT)` 范围内。

`tccpp_new()` 函数在初始化时遍历 `tcc_keywords` 字符串表，将每个关键字注册到哈希表中。当词法分析器扫描到一个标识符时，它在哈希表中查找，如果匹配到一个 `TokenSym` 条目，就使用该条目的 token 编号——这个编号可能是关键字的编号，也可能是之前注册的标识符的编号。

## 任务

为 tcc 添加一个新的关键字 `__assert`，它类似于 `static_assert` 但用于运行时断言。

### 步骤 1：在 tcctok.h 中添加关键字定义

在 `tcctok.h` 的关键字部分（C 语言控制流关键字之后，预处理器关键字之前）添加：

```c
DEF(TOK_ASSERT, "__assert")
```

**注意**：添加的位置很重要！它必须在 `TOK_ASM3` 之后、`TOK_EXTERN` 之前（或者在其他合适的位置），因为 token 编号是按顺序分配的。

> **警告**：添加关键字会改变后续所有关键字的 token 编号。这意味着如果你有依赖特定编号的代码（硬编码的数字常量），它们会出错。在实际工程中，这是一个重要的兼容性考虑。

### 步骤 2：验证关键字注册

编译 tcc 后，使用 `tcc -E` 预处理以下代码来验证关键字是否被识别：

```c
__assert(sizeof(int) == 4, "int must be 4 bytes");
```

如果关键字被正确识别，它应该在预处理输出中保持原样（因为 `__assert` 不是预处理器指令）。

### 步骤 3：在解析器中处理新关键字（可选，进阶）

在 `tccgen.c` 的解析器中添加对 `__assert` 的处理。例如：

```c
case TOK_ASSERT:
    next();  /* 跳过 __assert */
    skip('(');
    {
        /* 解析断言表达式 */
        int saved_expr_type = parse_flags;
        /* ... 表达式解析 ... */
    }
    skip(',');
    /* 解析消息字符串 */
    /* ... */
    skip(')');
    break;
```

### 步骤 4：编写测试代码

```c
int main(void)
{
    __assert(sizeof(int) >= 4, "int must be at least 4 bytes");
    return 0;
}
```

## 验证清单

- [ ] `tcctok.h` 中添加了 `DEF(TOK_ASSERT, "__assert")`
- [ ] tcc 编译成功
- [ ] `tcc -E test.c` 输出中 `__assert` 被识别为关键字
- [ ] （可选）解析器正确处理 `__assert` 语句

## 思考题

1. 如果你想添加一个**上下文相关关键字**（只在特定上下文中是关键字，其他时候是普通标识符），应该如何设计？（提示：查看 `__attribute__` 的处理方式。）

2. 为什么 tcc 使用枚举（`enum tcc_token`）而不是 `#define` 来定义关键字编号？枚举方式有什么优缺点？

3. 如果你在 `tcctok.h` 的**末尾**（汇编指令之后）添加关键字，而不是在中间添加，会有什么不同？这对兼容性有什么影响？

4. 考虑以下问题：如果用户代码中已经有一个名为 `__assert` 的宏定义（`#define __assert(...)`），你的新关键字会如何与它交互？提示：思考 `next()` 函数中宏展开和关键字识别的先后顺序。

## 提示

### 关键源文件

| 文件 | 作用 |
|------|------|
| `tcctok.h` | 关键字和标识符定义 |
| `tccpp.c` | 词法分析器（`next_nomacro()`、`next()`、`tccpp_new()`） |
| `tcc.h` | Token 编号定义、数据结构 |
| `tccgen.c` | 语法解析器（处理关键字的语义） |

### 调试技巧

1. 在 `next_nomacro()` 函数末尾添加调试输出：
   ```c
   printf("token = %d (%s)\n", tok, get_tok_str(tok, &tokc));
   ```

2. 使用 `tcc -E -P` 查看预处理输出，验证关键字是否被正确识别。

3. 使用 `get_tok_str()` 函数将 token 编号转换为可读字符串。

### 完整的关键字添加示例

以下是一个完整的示例，展示如何在 `tcctok.h` 的 C 关键字区域末尾添加新关键字：

```tcctok.h
/* ... 现有的关键字 ... */
DEF(TOK_ASM1, "asm")
DEF(TOK_ASM2, "__asm")
DEF(TOK_ASM3, "__asm__")

/* 新增关键字 */
DEF(TOK_ASSERT, "__assert")

DEF(TOK_EXTERN, "extern")
/* ... */
```

添加后，`TOK_ASSERT` 将自动获得一个递增的编号（例如，如果 `TOK_ASM3` 是 270，则 `TOK_ASSERT` 是 271）。后续的 `TOK_EXTERN` 及其后的所有关键字编号都会相应后移。

## 延伸阅读

- GCC 关键字扩展文档：https://gcc.gnu.org/onlinedocs/gcc/Keyword-Index.html
- C11 标准中的 `_Static_assert`：ISO/IEC 9899:2011, 6.7.10
- tcc 源码中 `parse_define()` 函数（`tccpp.c`）展示了 `#define` 的完整解析流程


===== FILE: docs/ch02/index.md =====
# 第二章 词法分析器（Lexer）

> "词法分析是编译器的第一道工序。它将字符流转换为记号流，为后续的语法分析奠定基础。"
> —— Alfred V. Aho, *Compilers: Principles, Techniques, and Tools*

本章深入剖析 TinyCC（以下简称 tcc）词法分析器的完整实现。词法分析器（lexer 或 scanner）位于 `tccpp.c` 文件中，是整个编译器中最底层、调用最频繁的模块。我们将从理论基础出发，逐步深入到 tcc 的具体实现细节。

---

## 2.1 词法分析的理论基础

### 2.1.1 正则语言与正则表达式

词法分析处理的语言是**正则语言**（regular language），它是乔姆斯基谱系中最简单的一类。正则语言可以用三种等价的形式描述：

1. **正则表达式**（Regular Expression）：用代数符号描述字符串模式
2. **有限自动机**（Finite Automaton）：用状态转移图描述识别过程
3. **正则文法**（Regular Grammar）：用产生式规则描述语法结构

在 C 语言中，各类词法单元（token）的模式可以用正则表达式描述：

```
标识符   →  [a-zA-Z_][a-zA-Z_0-9]*
十进制数 →  [1-9][0-9]*
八进制数 →  0[0-7]*
十六进制 →  0[xX][0-9a-fA-F]+
浮点数   →  [0-9]+\.[0-9]*([eE][+-]?[0-9]+)?
字符串   →  "([^"\\]|\\.)*"
字符常量 →  '([^'\\]|\\.)*'
```

### 2.1.2 有限自动机

有限自动机分为两类：

- **确定性有限自动机**（DFA）：每个状态对每个输入符号恰好有一个转移
- **非确定性有限自动机**（NFA）：每个状态对同一个输入符号可以有零个、一个或多个转移

tcc 的词法分析器本质上是一个**手写的 DFA**。与自动生成的词法分析器（如 lex/flex）不同，tcc 使用一个巨大的 `switch` 语句来实现状态转移。这种手写方式的优点是：

1. **更高的执行效率**：避免了查表和间接跳转的开销
2. **更灵活的控制流**：可以方便地处理上下文相关的词法问题（如预处理指令只在行首出现）
3. **更小的代码体积**：不需要生成巨大的转移表

### 2.1.3 Token 的分类

在 tcc 中，一个 token 由两部分信息组成：

1. **token 编号**（token number）：一个整数，标识 token 的类型
2. **token 值**（token value）：一个 `CValue` 联合体，存储 token 的附加数据（如常量的数值、字符串的内容）

token 编号的分配遵循精心设计的编码方案，这是下一节的主题。

---

## 2.2 tcc 的 Token 类型系统

tcc 的 token 编号系统是一套紧凑而高效的编码方案。所有 token 编号被划分为若干区间，每个区间有明确的语义。理解这套编码方案是阅读 tcc 预处理器和解析器代码的基础。

### 2.2.1 总览

```
┌─────────────────────────────────────────────────────────┐
│                    Token 编号空间                        │
├──────────────┬──────────────────────────────────────────┤
│  -1          │  TOK_EOF（文件结束）                      │
│   0          │  空 token（宏展开结束标记）                │
│   10         │  TOK_LINEFEED（换行符）                   │
│  0x20-0x7F   │  单字符 token（ASCII 字符本身）           │
│  0x80-0xAF   │  内部操作符和多字符操作符                  │
│  0xB0-0xB9   │  复合赋值操作符                           │
│  0xC0-0xCF   │  带值常量 token                           │
│  >= 256      │  标识符和关键字                            │
└──────────────┴──────────────────────────────────────────┘
```

### 2.2.2 单字符 Token（0x20-0x7F）

对于 ASCII 值在 0x20 到 0x7F 之间的单字符 token，tcc 直接使用字符的 ASCII 值作为 token 编号。这是一种极其高效的设计——不需要额外的编号分配。

```c
/* 源码位置: tccpp.c next_nomacro() */
case '(':  case ')':
case '[':  case ']':
case '{':  case '}':
case ',':
case ';':
case ':':
case '?':
case '~':
parse_simple:
    tok = c;    /* 直接使用 ASCII 值作为 token 编号 */
    p++;
    break;
```

这意味着 `(` 的 token 编号就是 40（ASCII 值），`{` 就是 123，以此类推。在解析器中，可以直接用字符常量来匹配：

```c
/* 解析器中的典型用法 */
if (tok == '(') {
    /* 处理左括号 */
}
```

**单字符 token 完整列表：**

| 字符 | ASCII (十六进制) | 语义 |
|------|------------------|------|
| ` `  | 0x20 | 空格（当 `PARSE_FLAG_SPACES` 启用时返回） |
| `!`  | 0x21 | 逻辑非 / `!=` 的起始 |
| `%`  | 0x25 | 取模 / `%=` 的起始 |
| `&`  | 0x26 | 按位与 / `&&` / `&=` 的起始 |
| `(`  | 0x28 | 左圆括号 |
| `)`  | 0x29 | 右圆括号 |
| `*`  | 0x2A | 乘法 / `*=` 的起始 |
| `+`  | 0x2B | 加法 / `++` / `+=` 的起始 |
| `,`  | 0x2C | 逗号 |
| `-`  | 0x2D | 减法 / `--` / `->` / `-=` 的起始 |
| `.`  | 0x2E | 点 / `...` / `..` 的起始 |
| `/`  | 0x2F | 除法 / 注释 / `/=` 的起始 |
| `:`  | 0x3A | 冒号 |
| `;`  | 0x3B | 分号 |
| `<`  | 0x3C | 小于 / `<=` / `<<` 的起始 |
| `=`  | 0x3D | 赋值 / `==` 的起始 |
| `>`  | 0x3E | 大于 / `>=` / `>>` 的起始 |
| `?`  | 0x3F | 问号 |
| `[`  | 0x5B | 左方括号 |
| `]`  | 0x5D | 右方括号 |
| `^`  | 0x5E | 按位异或 / `^=` 的起始 |
| `{`  | 0x7B | 左花括号 |
| `\|` | 0x7C | 按位或 / `\|\|` / `\|=` 的起始 |
| `}`  | 0x7D | 右花括号 |
| `~`  | 0x7E | 按位取反 |

### 2.2.3 内部操作符（0x80-0xAF）

这个区间包含两种类型的操作符：**一元/二元操作符**和**多字符操作符**。

**一元和内部操作符（0x80-0x8F）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_DEC` | 0x80 | `--`（自减） |
| `TOK_MID` | 0x81 | 增减操作中间值，也用于一元负号 (`TOK_NEG`) |
| `TOK_INC` | 0x82 | `++`（自增） |
| `TOK_UDIV` | 0x83 | 无符号除法（内部使用） |
| `TOK_UMOD` | 0x84 | 无符号取模（内部使用） |
| `TOK_PDIV` | 0x85 | 指针除法（内部使用） |
| `TOK_UMULL` | 0x86 | 无符号 32x32->64 乘法（内部使用） |
| `TOK_ADDC1` | 0x87 | 带进位加法（生成进位） |
| `TOK_ADDC2` | 0x88 | 带进位加法（使用进位） |
| `TOK_SUBC1` | 0x89 | 带借位减法（生成借位） |
| `TOK_SUBC2` | 0x8A | 带借位减法（使用借位） |
| `TOK_SHR`  | 0x8B | 无符号右移（内部使用） |

注意：`TOK_SHL` 和 `TOK_SAR` 被特殊处理——它们分别被定义为 `'<'` 和 `'>'`（即 0x3C 和 0x3E），这是 tcc 的一个巧妙设计，使得移位操作可以复用比较操作的某些代码路径。

**条件和比较操作符（0x90-0x9F）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_LAND` | 0x90 | `&&`（逻辑与） |
| `TOK_LOR`  | 0x91 | `\|\|`（逻辑或） |
| `TOK_ULT`  | 0x92 | 无符号小于（内部） |
| `TOK_UGE`  | 0x93 | 无符号大于等于（内部） |
| `TOK_EQ`   | 0x94 | `==`（相等） |
| `TOK_NE`   | 0x95 | `!=`（不等） |
| `TOK_ULE`  | 0x96 | 无符号小于等于（内部） |
| `TOK_UGT`  | 0x97 | 无符号大于（内部） |
| `TOK_Nset` | 0x98 | 位测试置位（内部） |
| `TOK_Nclear`| 0x99 | 位测试清除（内部） |
| `TOK_LT`   | 0x9C | `<`（小于） |
| `TOK_GE`   | 0x9D | `>=`（大于等于） |
| `TOK_LE`   | 0x9E | `<=`（小于等于） |
| `TOK_GT`   | 0x9F | `>`（大于） |

> **设计洞察**：0x90-0x9F 区间同时包含**源码级操作符**（如 `==`、`!=`）和**内部操作符**（如 `TOK_ULT`、`TOK_UGE`）。内部操作符只在代码生成阶段出现，它们用于区分有符号和无符号比较——这在 x86 汇编中对应不同的条件跳转指令。`TOK_ISCOND(t)` 宏用于判断一个 token 是否属于条件操作符。

**其他多字符操作符（0xA0-0xAF）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_ARROW`    | 0xA0 | `->`（结构体成员访问） |
| `TOK_DOTS`     | 0xA1 | `...`（可变参数） |
| `TOK_TWODOTS`  | 0xA2 | `..`（C++ 兼容） |
| `TOK_TWOSHARPS`| 0xA3 | `##`（预处理记号拼接） |
| `TOK_PLCHLDR`  | 0xA4 | 占位符 token（C99） |
| `TOK_PPJOIN`   | 0xA3\|SYM_FIELD | 宏展开中的 `##` 拼接标记 |
| `TOK_SOTYPE`   | 0xA7 | `sizeof(type)` 中 `(` 的别名 |

### 2.2.4 复合赋值操作符（0xB0-0xB9）

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_A_ADD` | 0xB0 | `+=` |
| `TOK_A_SUB` | 0xB1 | `-=` |
| `TOK_A_MUL` | 0xB2 | `*=` |
| `TOK_A_DIV` | 0xB3 | `/=` |
| `TOK_A_MOD` | 0xB4 | `%=` |
| `TOK_A_AND` | 0xB5 | `&=` |
| `TOK_A_OR`  | 0xB6 | `\|=` |
| `TOK_A_XOR` | 0xB7 | `^=` |
| `TOK_A_SHL` | 0xB8 | `<<=` |
| `TOK_A_SAR` | 0xB9 | `>>=` |

赋值操作符的设计非常紧凑。`TOK_ASSIGN(t)` 宏通过范围检查判断一个 token 是否是赋值操作符：

```c
#define TOK_ASSIGN(t) (t >= TOK_A_ADD && t <= TOK_A_SAR)
```

`TOK_ASSIGN_OP(t)` 宏则从赋值操作符反推出对应的二元操作符字符：

```c
#define TOK_ASSIGN_OP(t) ("+-*/%&|^<>"[t - TOK_A_ADD])
```

这个查找表利用了赋值操作符编号的连续性，将 `0xB0-0xB9` 映射到 `"+-*/%&|^<>"` 中的对应字符。

### 2.2.5 带值常量 Token（0xC0-0xCF）

这些 token 在 `tokc`（类型为 `CValue`）中携带附加的值信息。`TOK_HAS_VALUE(t)` 宏用于判断一个 token 是否需要额外的值解析。

| Token | 编号 | 值存储位置 | 说明 |
|-------|------|-----------|------|
| `TOK_CCHAR`   | 0xC0 | `tokc.i` | 字符常量 `'a'` |
| `TOK_LCHAR`   | 0xC1 | `tokc.i` | 宽字符常量 `L'a'` |
| `TOK_CINT`     | 0xC2 | `tokc.i` | 整数常量 |
| `TOK_CUINT`    | 0xC3 | `tokc.i` | 无符号整数常量 |
| `TOK_CLLONG`   | 0xC4 | `tokc.i` | long long 常量 |
| `TOK_CULLONG`  | 0xC5 | `tokc.i` | unsigned long long 常量 |
| `TOK_CLONG`    | 0xC6 | `tokc.i` | long 常量 |
| `TOK_CULONG`   | 0xC7 | `tokc.i` | unsigned long 常量 |
| `TOK_STR`      | 0xC8 | `tokc.str` | 字符串常量 |
| `TOK_LSTR`     | 0xC9 | `tokc.str` | 宽字符串常量 |
| `TOK_CFLOAT`   | 0xCA | `tokc.f` | float 常量 |
| `TOK_CDOUBLE`  | 0xCB | `tokc.d` | double 常量 |
| `TOK_CLDOUBLE` | 0xCC | `tokc.ld` | long double 常量 |
| `TOK_PPNUM`    | 0xCD | `tokc.str` | 预处理数字（未解析） |
| `TOK_PPSTR`    | 0xCE | `tokc.str` | 预处理字符串（未解析） |
| `TOK_LINENUM`  | 0xCF | `tokc.i` | 行号信息（宏展开内部使用） |

> **关键区分**：`TOK_PPNUM` 和 `TOK_PPSTR` 是预处理器阶段使用的"原始" token。在预处理阶段，数字和字符串以原始文本形式存储在 `tokc.str` 中。只有当 `PARSE_FLAG_TOK_NUM` 或 `PARSE_FLAG_TOK_STR` 标志启用时，`next()` 函数才会将它们转换为具体的 `TOK_CINT`、`TOK_STR` 等类型。

### 2.2.6 标识符和关键字（>= 256）

所有标识符和关键字的 token 编号都 >= `TOK_IDENT`（256）。它们通过 `table_ident` 数组管理，每个标识符对应一个 `TokenSym` 结构体。

关键字的编号从 `TOK_IDENT` 之后开始，通过 `tcctok.h` 中的 `DEF` 宏按顺序分配：

```c
/* tcc.h */
enum tcc_token {
    TOK_LAST = TOK_IDENT - 1
#define DEF(id, str) ,id
#include "tcctok.h"
#undef DEF
};
```

这意味着 `tcctok.h` 中第一个 `DEF` 的编号是 256，第二个是 257，以此类推。关键字和标识符共享同一个编号空间，区分它们的方法是检查编号是否在 `[TOK_IDENT, TOK_UIDENT)` 范围内：

```c
#define TOK_UIDENT TOK_DEFINE  /* 第一个非关键字标识符的编号 */
```

如果 `tok >= TOK_IDENT && tok < TOK_UIDENT`，则 `tok` 是关键字；否则是用户标识符。

---

## 2.3 tcctok.h 关键字定义

### 2.3.1 DEF 宏的巧妙设计

`tcctok.h` 不是一个普通的头文件——它是一个被多次包含的"X-macro"文件。每次包含时，`DEF` 宏的定义不同，从而产生不同的效果：

**第一次包含**：生成关键字字符串表

```c
static const char tcc_keywords[] =
#define DEF(id, str) str "\0"
#include "tcctok.h"
#undef DEF
;
```

这将所有关键字字符串连接成一个以 `\0` 分隔的大字符串：`"if\0else\0while\0for\0..."`

**第二次包含**：生成 token 编号枚举

```c
enum tcc_token {
    TOK_LAST = TOK_IDENT - 1
#define DEF(id, str) ,id
#include "tcctok.h"
#undef DEF
};
```

这将 `TOK_IF`、`TOK_ELSE` 等依次定义为枚举常量，从 `TOK_IDENT`(256) 开始递增。

**第三次包含**：在 `tccpp_new()` 中注册关键字

```c
p = tcc_keywords;
while (*p) {
    r = p;
    for(;;) {
        c = *r++;
        if (c == '\0')
            break;
    }
    tok_alloc(p, r - p - 1);  /* 注册关键字到哈希表 */
    p = r;
}
```

### 2.3.2 关键字分类

`tcctok.h` 中的条目按功能分为以下几大类：

**（1）C 语言控制流关键字**

```
if, else, while, for, do, continue, break, return, goto,
switch, case, default
```

**（2）asm 关键字（三种变体）**

```
asm, __asm, __asm__          /* TOK_ASM1, TOK_ASM2, TOK_ASM3 */
```

tcc 支持三种 asm 语法变体以兼容不同的 C 方言和 GCC 扩展。

**（3）存储类和类型修饰符**

```
extern, static, auto, register, typedef
const, __const, __const__         /* 三种变体 */
volatile, __volatile, __volatile__
signed, __signed, __signed__
unsigned
inline, __inline, __inline__
restrict, __restrict, __restrict__
__extension__
_Atomic, _Thread_local, __thread
```

> **设计说明**：几乎所有 GCC 风格的关键字都有双下划线变体。`__const` 和 `__const__` 都是 `const` 的同义词——这是 GCC 的惯例，允许在系统头文件中使用 `__const` 避免与用户定义的宏冲突。

**（4）类型关键字**

```
void, char, int, float, double
_Bool, _Complex                 /* C99/C11 类型 */
short, long
struct, union, enum
sizeof, _Alignof, __alignof, __alignof__
_Alignas
typeof, __typeof, __typeof__   /* GCC 扩展 */
__attribute, __attribute__
__label__
_Generic, _Static_assert       /* C11 特性 */
```

**（5）预处理器关键字**

这些不是 C 语言的关键字，而是预处理器指令中使用的标识符：

```
define, include, include_next, ifdef, ifndef, elif,
endif, defined, undef, error, warning, line, pragma
```

以及预定义宏：

```
__LINE__, __FILE__, __DATE__, __TIME__,
__FUNCTION__, __VA_ARGS__, __COUNTER__,
__has_include, __has_include_next
```

**（6）属性标识符**

GCC 的 `__attribute__` 机制需要识别大量的属性名称：

```
section, __section__           /* 函数/变量段 */
aligned, __aligned__           /* 对齐 */
packed, __packed__             /* 紧凑结构 */
weak, __weak__                 /* 弱符号 */
alias, __alias__               /* 符号别名 */
used, __used__                 /* 防止未使用警告 */
unused, __unused__             /* 标记未使用 */
format, __format__             /* printf/scanf 格式检查 */
cdecl, __cdecl, __cdecl__     /* 调用约定 */
stdcall, __stdcall, __stdcall__
fastcall, __fastcall, __fastcall__
noreturn, __noreturn__, _Noreturn
visibility, __visibility__
constructor, __constructor__
destructor, __destructor__
always_inline, __always_inline__
cleanup, __cleanup__
```

**（7）内建函数标识符**

```
__builtin_types_compatible_p
__builtin_choose_expr
__builtin_constant_p
__builtin_frame_address
__builtin_return_address
__builtin_expect
__builtin_unreachable
```

以及平台相关的内建函数（如 `__builtin_va_start`、`__builtin_va_arg` 等）。

**（8）原子操作标识符**

```
__atomic_store, __atomic_load, __atomic_exchange,
__atomic_compare_exchange,
__atomic_fetch_add, __atomic_fetch_sub,
__atomic_fetch_or, __atomic_fetch_xor,
__atomic_fetch_and, __atomic_fetch_nand,
__atomic_add_fetch, __atomic_sub_fetch,
__atomic_or_fetch, __atomic_xor_fetch,
__atomic_and_fetch, __atomic_nand_fetch
```

这些通过 `DEF_ATOMIC` 宏批量定义：

```c
#define DEF_ATOMIC(ID) DEF(TOK_##__##ID, "__"#ID)
```

**（9）汇编器指令**

`tcctok.h` 的末尾还包含汇编器（Tiny Assembler）使用的指令和关键字，通过 `DEF_ASM` 和 `DEF_ASMDIR` 宏定义：

```c
#define DEF_ASM(x) DEF(TOK_ASM_ ## x, #x)
#define DEF_ASMDIR(x) DEF(TOK_ASMDIR_ ## x, "." #x)
```

汇编指令包括 `.byte`、`.word`、`.align`、`.text`、`.data`、`.bss` 等。

此外，各目标架构还有自己的关键字文件（如 `i386-tok.h`、`arm-tok.h`、`arm64-tok.h`），通过条件 `#include` 加入。

---

## 2.4 核心数据结构

### 2.4.1 TokenSym —— 标识符符号表条目

```c
typedef struct TokenSym {
    struct TokenSym *hash_next;    /* 哈希链表的下一个节点（解决冲突） */
    struct Sym *sym_define;        /* 指向 #define 定义 */
    struct Sym *sym_label;         /* 指向标号定义 */
    struct Sym *sym_struct;        /* 指向结构体/联合体/枚举定义 */
    struct Sym *sym_identifier;    /* 指向标识符定义（变量/函数/typedef） */
    int tok;                       /* token 编号 */
    int len;                       /* 标识符名称长度 */
    char str[1];                   /* 标识符名称（柔性数组成员） */
} TokenSym;
```

`TokenSym` 是 tcc 符号表的核心结构。每个标识符（包括关键字）在 `table_ident` 数组中都有一个对应的 `TokenSym` 条目。它同时扮演两个角色：

1. **词法符号表**：通过 `tok` 编号和 `str` 名称标识一个词法单元
2. **语义符号表入口**：通过四个 `Sym*` 指针直接链接到各种语义定义

这种"一个结构体同时服务于词法分析和语义分析"的设计避免了额外的查找开销。当解析器遇到一个标识符时，可以通过 `TokenSym` 直接访问它的所有定义信息，无需再次哈希查找。

**柔性数组成员** `str[1]` 是 C 语言的一个技巧。`TokenSym` 实际分配的内存大小为 `sizeof(TokenSym) + len`，`str` 字段的地址就是字符串的起始位置，避免了额外的指针间接访问。

### 2.4.2 CValue —— 常量值联合体

```c
typedef union CValue {
    long double ld;                /* long double 常量 */
    double d;                      /* double 常量 */
    float f;                       /* float 常量 */
    uint64_t i;                    /* 整数常量 */
    struct {
        char *data;                /* 字符串数据指针 */
        int size;                  /* 字符串大小（含尾部 \0） */
    } str;                         /* 字符串/预处理数字 */
    int tab[LDOUBLE_WORDS];        /* 按 int 数组访问（用于序列化） */
} CValue;
```

`CValue` 是一个联合体，用于存储 token 的附加值。它的设计考虑了以下几个因素：

- **类型多样性**：需要存储整数、浮点数、字符串等不同类型
- **平台一致性**：`LDOUBLE_WORDS` 根据 `sizeof(long double)` 动态计算，确保在不同平台上正确工作
- **序列化支持**：`tab` 成员允许按 `int` 数组访问整个联合体，用于 token 字符串的序列化（`tok_get` 函数）

使用示例：

```c
/* 存储整数常量 42 */
tokc.i = 42;        /* tok = TOK_CINT */

/* 存储浮点常量 3.14 */
tokc.d = 3.14;      /* tok = TOK_CDOUBLE */

/* 存储字符串 "hello" */
tokc.str.data = "hello";
tokc.str.size = 6;  /* 含 \0 */
/* tok = TOK_STR */
```

### 2.4.3 BufferedFile —— 文件缓冲区

```c
typedef struct BufferedFile {
    uint8_t *buf_ptr;              /* 当前读取位置 */
    uint8_t *buf_end;              /* 缓冲区有效数据的末尾 */
    int fd;                        /* 文件描述符 */
    struct BufferedFile *prev;     /* include 栈中的上一个文件 */
    int line_num;                  /* 当前行号 */
    int line_ref;                  /* tcc -E: 上次输出的行号 */
    int ifndef_macro;              /* #ifndef 宏 / #endif 搜索 */
    int ifndef_macro_saved;        /* 保存的 ifndef_macro */
    int *ifdef_stack_ptr;          /* 文件开始时的 ifdef_stack 值 */
    int include_next_index;        /* 下一个搜索路径 */
    int prev_tok_flags;            /* 保存的 tok_flags */
    char filename[1024];           /* 文件名 */
    char *true_filename;           /* 未被 #line 修改的真实文件名 */
    unsigned char unget[4];        /* 回退字符缓冲区 */
    unsigned char buffer[1];       /* I/O 缓冲区（柔性数组成员） */
} BufferedFile;
```

`BufferedFile` 是 tcc 文件 I/O 的核心结构。它将文件读取缓冲和文件元信息（行号、include 栈等）整合在一个结构体中。

**关键字段详解：**

- `buf_ptr` / `buf_end`：这对指针定义了缓冲区中有效的数据范围。`buf_ptr` 指向下一个要读取的字节，`buf_end` 指向有效数据的末尾。当 `buf_ptr == buf_end` 时，需要从文件重新读取。

- `unget[4]`：这是一个小型的"回退缓冲区"。当词法分析器需要"放回"一个字符时（例如，它多读了一个字符但发现不属于当前 token），它通过将 `buf_ptr` 递减来实现。`unget` 数组确保即使 `buf_ptr` 回退到 `buffer` 之前的位置，也有合法的内存可以写入。

- `buffer[1]`：柔性数组成员。实际分配时，`BufferedFile` 的大小为 `sizeof(BufferedFile) + IO_BUF_SIZE`，其中 `IO_BUF_SIZE = 8192`。

- `prev`：形成一个栈结构。当处理 `#include` 时，新的 `BufferedFile` 被压入栈；当 include 文件结束时，从栈中弹出。

- `ifndef_macro` / `ifndef_macro_saved`：用于 `#ifndef` 保护头文件的优化。如果一个头文件以 `#ifndef _GUARD_H` 开头并以 `#endif` 结尾，tcc 可以缓存这个结果，避免重复包含时的文件 I/O 和解析开销。

### 2.4.4 TokenString —— Token 序列

```c
typedef struct TokenString {
    int *str;                      /* token 序列数据 */
    int len;                       /* 当前长度（以 int 为单位） */
    int need_spc;                  /* 是否需要空格分隔 */
    int allocated_len;             /* 已分配的长度 */
    int last_line_num;             /* 最后一行号 */
    int save_line_num;             /* 保存的行号（用于宏展开） */
    struct TokenString *prev;      /* 链接到上一个 TokenString */
    const int *prev_ptr;           /* 上一个 macro_ptr 值 */
    char alloc;                    /* 分配类型：0=静态, 1=动态, 2=不释放 */
} TokenString;
```

`TokenString` 用于存储 token 序列，主要服务于以下场景：

1. **宏定义存储**：`#define` 的替换体被存储为 `TokenString`
2. **宏展开结果**：宏展开的输出 token 序列
3. **unget 缓冲**：`unget_tok()` 函数将 token 推回

`str` 数组中的每个元素是一个 `int`，但带值的 token（`TOK_HAS_VALUE` 为真的 token）会占用多个 `int`：第一个是 token 编号，后续的是 `CValue` 的序列化形式。`tok_get()` 函数负责从 `TokenString` 中读取一个完整的 token（编号 + 值）。

### 2.4.5 isidnum_table —— 字符分类表

```c
static unsigned char isidnum_table[256 - CH_EOF];
```

这是一个 257 字节的查找表（因为 `CH_EOF = -1`，所以索引范围是 `-1` 到 `255`，即 `c - CH_EOF` 的范围是 `0` 到 `256`）。每个字节的位标志定义了对应字符的类别：

| 标志位 | 值 | 含义 |
|--------|-----|------|
| `IS_SPC` | 0x01 | 空白字符（空格、制表符等） |
| `IS_ID`  | 0x02 | 可以出现在标识符中的字符（字母、下划线、高位字符） |
| `IS_NUM` | 0x04 | 数字字符 |

初始化代码：

```c
/* tccpp_new() */
for(i = CH_EOF; i < 128; i++)
    set_idnum(i,
        is_space(i) ? IS_SPC
        : isid(i)   ? IS_ID
        : isnum(i)  ? IS_NUM
        : 0);

for(i = 128; i < 256; i++)
    set_idnum(i, IS_ID);  /* 高位字节视为标识符字符（UTF-8 支持） */
```

> **性能关键**：在 `next_nomacro()` 的标识符扫描循环中，`isidnum_table` 查找是内层循环的主要操作。使用表查找而非条件判断（`if (isalpha(c) || isdigit(c) || c == '_')`）可以显著提高性能，因为它将多次比较转化为一次内存访问。

`set_idnum()` 函数允许运行时修改字符分类，例如：
- 当 `dollars_in_identifiers` 启用时，`$` 被标记为 `IS_ID`
- 在汇编模式下，`.` 被标记为 `IS_ID`

---

## 2.5 文件 I/O 与缓冲

### 2.5.1 缓冲区结构

tcc 使用固定大小的缓冲区（`IO_BUF_SIZE = 8192` 字节）来读取源文件。缓冲区的结构如下：

```
BufferedFile.buffer:
┌──────────────────────────────────────────┬──────┐
│          有效数据 (0 ~ 8191)              │ CH_EOB│
└──────────────────────────────────────────┴──────┘
                                    ↑       ↑
                                  buf_ptr  buf_end
```

缓冲区末尾始终放置一个 `CH_EOB`（值为 `'\\'`，即 0x5C）哨兵字符。这个设计的精妙之处在于：

1. `CH_EOB` 的值恰好是反斜杠 `\`，这意味着正常情况下缓冲区末尾不会出现这个值（除非源文件中真的有 `\`）
2. 哨兵字符使得词法分析器的内层循环不需要在每次迭代时检查 `buf_ptr < buf_end`

### 2.5.2 handle_eob() —— 缓冲区重填

当 `buf_ptr` 到达 `buf_end` 时，`handle_eob()` 被调用：

```c
static int handle_eob(void)
{
    BufferedFile *bf = file;
    int len;

    if (bf->buf_ptr >= bf->buf_end) {
        if (bf->fd >= 0) {
            len = read(bf->fd, bf->buffer, IO_BUF_SIZE);
            if (len < 0)
                len = 0;
        } else {
            len = 0;
        }
        total_bytes += len;
        bf->buf_ptr = bf->buffer;
        bf->buf_end = bf->buffer + len;
        *bf->buf_end = CH_EOB;  /* 放置哨兵 */
    }
    if (bf->buf_ptr < bf->buf_end) {
        return bf->buf_ptr[0];
    } else {
        bf->buf_ptr = bf->buf_end;
        return CH_EOF;
    }
}
```

**工作流程：**

1. 检查 `buf_ptr >= buf_end`（真正到达缓冲区末尾）
2. 调用 `read()` 从文件描述符读取 `IO_BUF_SIZE` 字节
3. 重置 `buf_ptr` 到 `buffer` 起始位置
4. 设置 `buf_end` 到实际读取的数据末尾
5. 在 `buf_end` 位置放置 `CH_EOB` 哨兵
6. 如果读取了数据，返回第一个字节；否则返回 `CH_EOF`

### 2.5.3 next_c() —— 读取下一个字符

```c
static int next_c(void)
{
    int ch = *++file->buf_ptr;
    if (ch == CH_EOB && file->buf_ptr >= file->buf_end)
        ch = handle_eob();
    return ch;
}
```

`next_c()` 是最底层的字符读取函数。它的设计非常精巧：

1. **先递增再读取**：`*++file->buf_ptr` 先将指针前移，再读取。这意味着 `buf_ptr` 始终指向"刚读取的字符"，而不是"下一个要读取的字符"。这是为了与 `next_nomacro()` 中的 `PEEKC` 宏配合。

2. **哨兵检查**：只有当读取到 `CH_EOB` **且** `buf_ptr >= buf_end` 时，才调用 `handle_eob()`。如果 `CH_EOB` 出现在缓冲区中间（源文件中真的有 `\`），则不会触发重填。

### 2.5.4 行拼接（Line Splicing）

C 语言允许用 `\` 结尾的行来续行：

```c
int a = 1 + \
        2 + 3;
```

tcc 在 `handle_stray_noerror()` 函数中处理行拼接：

```c
static int handle_stray_noerror(int err)
{
    int ch;
    while ((ch = next_c()) == '\\') {
        ch = next_c();
        if (ch == '\n') {
    newl:
            file->line_num++;     /* 续行：跳过 \ 和 \n */
        } else {
            if (ch == '\r') {     /* 处理 \r\n 行尾 */
                ch = next_c();
                if (ch == '\n')
                    goto newl;
                *--file->buf_ptr = '\r';
            }
            if (err)
                tcc_error("stray '\\' in program");
            return *--file->buf_ptr = '\\';
        }
    }
    return ch;
}
```

这个函数通过 `while` 循环处理连续的续行（`\\\n\\\n\\\n...`）。当遇到 `\` 后面跟着 `\n` 时，跳过这两个字符并递增行号；否则，将 `\` 放回缓冲区并返回。

> **注意 `\r\n` 的处理**：Windows 风格的行尾 `\r\n` 也被正确处理。当 `\` 后面是 `\r` 时，再读取一个字符检查是否是 `\n`。

---

## 2.6 next_nomacro_spc() 底层扫描器

`next_nomacro()` 是 tcc 词法分析器的核心函数。它从输入缓冲区读取字符并识别出下一个 token，但**不进行宏展开**。函数实现为一个巨大的 `switch` 语句，每个 `case` 处理一种类型的 token。

### 2.6.1 函数签名和入口

```c
static void next_nomacro(void)
{
    int t, c, is_long, len;
    TokenSym *ts;
    uint8_t *p, *p1;
    unsigned int h;

    p = file->buf_ptr;
 redo_no_start:
    c = *p;
    switch(c) {
    /* ... */
    }
    tok_flags = 0;
keep_tok_flags:
    file->buf_ptr = p;
}
```

注意函数使用本地指针 `p` 来遍历缓冲区，而不是每次都通过 `file->buf_ptr` 间接访问。这是一个重要的性能优化——编译器可以将 `p` 保持在寄存器中。函数结束时才将 `p` 写回 `file->buf_ptr`。

`redo_no_start` 标签用于在不重置 `tok_flags` 的情况下重新开始扫描（例如跳过空白后）。

### 2.6.2 空白和换行处理

```c
case ' ':
case '\t':
    tok = c;
    p++;
maybe_space:
    if (parse_flags & PARSE_FLAG_SPACES)
        goto keep_tok_flags;     /* -E 模式：保留空格 token */
    while (isidnum_table[*p - CH_EOF] & IS_SPC)
        ++p;
    goto redo_no_start;          /* 跳过所有空白，重新开始 */

case '\f':
case '\v':
case '\r':
    p++;
    goto redo_no_start;          /* 无条件跳过这些空白字符 */

case '\n':
    file->line_num++;
    p++;
maybe_newline:
    tok_flags |= TOK_FLAG_BOL;  /* 标记行首 */
    if (0 == (parse_flags & PARSE_FLAG_LINEFEED))
        goto redo_no_start;      /* 默认不返回换行 token */
    tok = TOK_LINEFEED;
    goto keep_tok_flags;
```

**设计要点：**

- 空格和制表符在默认模式下被跳过；只有当 `PARSE_FLAG_SPACES` 启用时（`tcc -E` 预处理输出模式），它们才作为 token 返回
- 换行符总是递增行号，但只有当 `PARSE_FLAG_LINEFEED` 启用时才返回 `TOK_LINEFEED` token
- `TOK_FLAG_BOL` 标记用于预处理器——`#` 指令只在行首有效

### 2.6.3 标识符的快速路径

这是 `next_nomacro()` 中最热的代码路径之一：

```c
case 'a': case 'b': case 'c': case 'd':
case 'e': case 'f': case 'g': case 'h':
case 'i': case 'j': case 'k': case 'l':
case 'm': case 'n': case 'o': case 'p':
case 'q': case 'r': case 's': case 't':
case 'u': case 'v': case 'w': case 'x':
case 'y': case 'z':
case 'A': case 'B': case 'C': case 'D':
case 'E': case 'F': case 'G': case 'H':
case 'I': case 'J': case 'K':
case 'M': case 'N': case 'O': case 'P':
case 'Q': case 'R': case 'S': case 'T':
case 'U': case 'V': case 'W': case 'X':
case 'Y': case 'Z':
case '_':
parse_ident_fast:
    p1 = p;
    h = TOK_HASH_INIT;                    /* h = 1 */
    h = TOK_HASH_FUNC(h, c);             /* 计算第一个字符的哈希 */
    while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
        h = TOK_HASH_FUNC(h, c);         /* 在扫描的同时计算哈希 */
    len = p - p1;
    if (c != '\\') {
        /* 快速路径：没有续行符，直接在哈希表中查找 */
        TokenSym **pts;
        h &= (TOK_HASH_SIZE - 1);        /* 取模（TOK_HASH_SIZE 是 2 的幂） */
        pts = &hash_ident[h];
        for(;;) {
            ts = *pts;
            if (!ts)
                break;
            if (ts->len == len && !memcmp(ts->str, p1, len))
                goto token_found;
            pts = &(ts->hash_next);
        }
        ts = tok_alloc_new(pts, (char *) p1, len);
    token_found: ;
    } else {
        /* 慢速路径：有续行符，需要处理行拼接 */
        cstr_reset(&tokcstr);
        cstr_cat(&tokcstr, (char *) p1, len);
        p--;
        PEEKC(c, p);
        while (isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
        {
            cstr_ccat(&tokcstr, c);
            PEEKC(c, p);
        }
        ts = tok_alloc(tokcstr.data, tokcstr.size);
    }
    tok = ts->tok;
    break;
```

**快速路径的关键优化：**

1. **哈希计算与扫描同步**：在逐字符读取标识符的同时计算哈希值，避免了单独的哈希计算遍历
2. **直接内存比较**：使用 `memcmp(ts->str, p1, len)` 比较标识符，而不是逐字符比较
3. **位运算取模**：`h &= (TOK_HASH_SIZE - 1)` 代替 `h % TOK_HASH_SIZE`，因为 `TOK_HASH_SIZE` 是 2 的幂（16384）
4. **避免续行检查**：在常见情况下（没有 `\`），跳过续行处理

> **注意 `case 'L'` 的特殊处理**：字母 `L` 单独处理，因为它可能是宽字符/字符串的前缀（`L'x'` 或 `L"string"`）。如果不是宽字符串前缀，则跳转到 `parse_ident_fast`。

### 2.6.4 数字扫描

```c
case '0': case '1': case '2': case '3':
case '4': case '5': case '6': case '7':
case '8': case '9':
    t = c;
    PEEKC(c, p);
parse_num:
    cstr_reset(&tokcstr);
    for(;;) {
        cstr_ccat(&tokcstr, t);
        if (!((isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
              || c == '.'
              || ((c == '+' || c == '-')
                  && (((t == 'e' || t == 'E')
                        && !(parse_flags & PARSE_FLAG_ASM_FILE
                            && ((char*)tokcstr.data)[0] == '0'
                            && toup(((char*)tokcstr.data)[1]) == 'X'))
                      || t == 'p' || t == 'P'))))
            break;
        t = c;
        PEEKC(c, p);
    }
    cstr_ccat(&tokcstr, '\0');
    tokc.str.size = tokcstr.size;
    tokc.str.data = tokcstr.data;
    tok = TOK_PPNUM;
    break;
```

在预处理阶段，数字被识别为 `TOK_PPNUM`——一个"未解析"的数字 token。原始的数字文本被保存在 `tokc.str` 中。这种"延迟解析"的设计有以下好处：

1. 预处理器不需要理解数字的语义（如进制、类型后缀）
2. `#if` 表达式中的数字可能有不同的解析规则
3. 简化了 `next_nomacro()` 的逻辑

数字的真正解析（进制判断、类型推断等）在 `parse_number()` 中完成，由 `next()` 在需要时调用。

**数字扫描的边界条件：**

数字 token 的扫描需要处理许多边界情况：
- `0x1e+2` 在汇编模式下是三个 token（`0x1e`、`+`、`2`），而在 C 模式下是一个浮点数
- `1.23e-4` 中的 `-` 是指数的一部分，不是减法操作符
- `.` 可以是数字的开始（如 `.5`）

### 2.6.5 字符串扫描

```c
case '\'':
case '\"':
    is_long = 0;
str_const:
    cstr_reset(&tokcstr);
    if (is_long)
        cstr_ccat(&tokcstr, 'L');
    cstr_ccat(&tokcstr, c);           /* 保存引号 */
    p = parse_pp_string(p, c, &tokcstr);  /* 扫描字符串内容 */
    cstr_ccat(&tokcstr, c);           /* 保存闭合引号 */
    cstr_ccat(&tokcstr, '\0');
    tokc.str.size = tokcstr.size;
    tokc.str.data = tokcstr.data;
    tok = TOK_PPSTR;
    break;
```

与数字类似，字符串在预处理阶段也以原始形式（`TOK_PPSTR`）保存。`parse_pp_string()` 负责扫描字符串内容，处理转义序列和续行，但不解析转义序列的值。

### 2.6.6 多字符操作符

tcc 使用两种机制识别多字符操作符：

**机制一：`tok_two_chars` 查找表**

```c
static const unsigned char tok_two_chars[] = {
    '<','=', TOK_LE,
    '>','=', TOK_GE,
    '!','=', TOK_NE,
    '&','&', TOK_LAND,
    '|','|', TOK_LOR,
    '+','+', TOK_INC,
    '-','-', TOK_DEC,
    '=','=', TOK_EQ,
    '<','<', TOK_SHL,
    '>','>', TOK_SAR,
    '+','=', TOK_A_ADD,
    '-','=', TOK_A_SUB,
    '*','=', TOK_A_MUL,
    '/','=', TOK_A_DIV,
    '%','=', TOK_A_MOD,
    '&','=', TOK_A_AND,
    '^','=', TOK_A_XOR,
    '|','=', TOK_A_OR,
    '-','>', TOK_ARROW,
    '.','.', TOK_TWODOTS,
    '#','#', TOK_TWOSHARPS,
    0
};
```

这个表在 `get_tok_str()` 中用于将 token 编号转换回字符串表示。

**机制二：手动的 `switch`/`if` 链**

实际的扫描使用手动编码的判断逻辑，以处理三字符操作符（如 `<<=`）和上下文相关的歧义：

```c
case '<':
    PEEKC(c, p);
    if (c == '=') {
        p++;
        tok = TOK_LE;        /* <= */
    } else if (c == '<') {
        PEEKC(c, p);
        if (c == '=') {
            p++;
            tok = TOK_A_SHL;  /* <<= */
        } else {
            tok = TOK_SHL;    /* << */
        }
    } else {
        tok = TOK_LT;         /* < */
    }
    break;
```

同样，`=` 的处理需要区分 `=`（赋值）和 `==`（比较），`!` 需要区分 `!`（逻辑非）和 `!=`（不等）等。

tcc 还使用了一个宏 `PARSE2` 来简化两字符操作符的处理：

```c
#define PARSE2(c1, tok1, c2, tok2)    \
    case c1:                           \
        PEEKC(c, p);                   \
        if (c == c2) {                 \
            p++;                       \
            tok = tok2;                \
        } else {                       \
            tok = tok1;                \
        }                              \
        break;
```

使用示例：

```c
PARSE2('!', '!', '=', TOK_NE)    /* ! 或 != */
PARSE2('=', '=', '=', TOK_EQ)    /* = 或 == */
PARSE2('*', '*', '=', TOK_A_MUL) /* * 或 *= */
```

### 2.6.7 注释处理

```c
case '/':
    PEEKC(c, p);
    if (c == '*') {
        p = parse_comment(p);       /* C 风格注释 */
        tok = ' ';
        goto maybe_space;
    } else if (c == '/') {
        p = parse_line_comment(p);  /* C++ 风格注释 */
        tok = ' ';
        goto maybe_space;
    } else if (c == '=') {
        p++;
        tok = TOK_A_DIV;           /* /= */
    } else {
        tok = '/';                  /* 除法 */
    }
    break;
```

注释被替换为空格 token，然后跳转到 `maybe_space` 继续跳过后续的空白。这确保了注释不会意外地连接两个 token（例如 `a/* comment */b` 不会变成 `ab`）。

`parse_comment()` 函数处理 `/* ... */` 风格的注释，内部也使用了快速跳过循环和行拼接处理。

### 2.6.8 PEEKC 宏

```c
#define PEEKC(c, p)                    \
{                                      \
    c = *++p;                          \
    if (c == '\\')                     \
        c = handle_stray(&p);          \
}
```

`PEEKC` 是一个"预读"宏。它读取下一个字符，如果遇到 `\` 则调用 `handle_stray()` 处理可能的行拼接。这个宏在多字符操作符和数字扫描中广泛使用。

---

## 2.7 next() 带宏展开的扫描器

`next()` 是 tcc 对外提供的主要扫描接口。它在 `next_nomacro()` 的基础上增加了**宏展开**功能。

### 2.7.1 宏展开栈

tcc 使用一个全局变量 `macro_ptr` 和一个栈结构 `macro_stack` 来管理宏展开：

```c
ST_DATA const int *macro_ptr;    /* 当前正在读取的 TokenString 位置 */
static TokenString *macro_stack; /* 宏展开栈 */
```

当一个宏被展开时，展开结果被压入栈中，`macro_ptr` 指向展开结果的起始位置。`next()` 优先从 `macro_ptr` 读取 token，只有当 `macro_ptr == NULL`（栈为空）时才调用 `next_nomacro()` 从文件读取。

### 2.7.2 next() 的完整逻辑

```c
ST_FUNC void next(void)
{
    int t;
    while (macro_ptr) {
redo:
        t = *macro_ptr;
        if (TOK_HAS_VALUE(t)) {
            tok_get(&tok, &macro_ptr, &tokc);
            if (t == TOK_LINENUM) {
                file->line_num = tokc.i;
                goto redo;
            }
            goto convert;
        } else if (t == 0) {
            end_macro();          /* 宏展开结束 */
            continue;
        } else if (t == TOK_EOF) {
            /* 什么都不做 */
        } else {
            ++macro_ptr;
            t &= ~SYM_FIELD;     /* 移除 nosubst 标记 */
            if (t == '\\') {
                if (!(parse_flags & PARSE_FLAG_ACCEPT_STRAYS))
                    tcc_error("stray '\\' in program");
            }
        }
        tok = t;
        return;
    }

    /* 宏栈为空，从文件读取 */
    next_nomacro();
    t = tok;
    if (t >= TOK_IDENT && (parse_flags & PARSE_FLAG_PREPROCESS)) {
        Sym *s = define_find(t);  /* 查找是否有宏定义 */
        if (s) {
            Sym *nested_list = NULL;
            macro_subst_tok(&tokstr_buf, &nested_list, s);
            tok_str_add(&tokstr_buf, 0);
            begin_macro(&tokstr_buf, 0);  /* 将展开结果压入栈 */
            goto redo;                     /* 重新从栈中读取 */
        }
        return;
    }

convert:
    /* 将预处理 token 转换为 C token */
    if (t == TOK_PPNUM) {
        if (parse_flags & PARSE_FLAG_TOK_NUM)
            parse_number(tokc.str.data);
    } else if (t == TOK_PPSTR) {
        if (parse_flags & PARSE_FLAG_TOK_STR)
            parse_string(tokc.str.data, tokc.str.size - 1);
    }
}
```

### 2.7.3 TOK_LINENUM 机制

`TOK_LINENUM` 是一个特殊的内部 token。在宏展开过程中，tcc 会在展开结果中插入 `TOK_LINENUM` token 来记录行号变化。当 `next()` 遇到 `TOK_LINENUM` 时，它更新 `file->line_num` 但不返回这个 token——而是继续读取下一个真正的 token。

这确保了编译器错误信息中的行号始终是正确的。

### 2.7.4 begin_macro() 和 end_macro()

```c
ST_FUNC void begin_macro(TokenString *str, int alloc)
{
    str->alloc = alloc;
    str->prev = macro_stack;
    str->prev_ptr = macro_ptr;
    str->save_line_num = file->line_num;
    macro_ptr = str->str;
    macro_stack = str;
}

ST_FUNC void end_macro(void)
{
    TokenString *str = macro_stack;
    macro_stack = str->prev;
    macro_ptr = str->prev_ptr;
    file->line_num = str->save_line_num;
    if (str->alloc == 0) {
        str->len = str->need_spc = 0;
    } else {
        if (str->alloc == 2)
            str->str = NULL;
        tok_str_free(str);
    }
}
```

`begin_macro()` 将一个 `TokenString` 压入宏展开栈。`end_macro()` 弹出栈顶并恢复之前的上下文（`macro_ptr` 和行号）。

### 2.7.5 define_find() —— 宏查找

```c
ST_INLN Sym *define_find(int v)
{
    Sym *s;
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    s = table_ident[v]->sym_define;
    return s;
}
```

`define_find()` 通过 `TokenSym` 的 `sym_define` 指针直接查找宏定义。这是一个 O(1) 操作——不需要遍历符号表。

### 2.7.6 Token 转换

`next()` 的最后一步是将预处理 token 转换为 C 语言 token：

- `TOK_PPNUM` -> `parse_number()` -> `TOK_CINT`/`TOK_CUINT`/`TOK_CFLOAT`/`TOK_CDOUBLE` 等
- `TOK_PPSTR` -> `parse_string()` -> `TOK_CCHAR`/`TOK_STR`/`TOK_LSTR` 等

这个转换只在 `PARSE_FLAG_TOK_NUM` 和 `PARSE_FLAG_TOK_STR` 标志启用时发生。在纯预处理模式下（`tcc -E`），token 保持为 `TOK_PPNUM` 和 `TOK_PPSTR`。

---

## 2.8 标识符哈希与查找

### 2.8.1 哈希函数

tcc 使用的哈希函数定义如下：

```c
#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) + ((h) << 5) + ((h) >> 27) + (c))
```

这是一个**乘法哈希**的变体。`h + (h << 5) + (h >> 27)` 等价于 `h * 33 + (h >> 27)`，其中 `h >> 27` 提供了额外的混合。乘以 33 是一个经典的字符串哈希技巧（与 DJB2 哈希函数类似）。

> **为什么选择 33？** 这个乘数在实践中表现良好，它是一个奇数且不是 2 的幂，能够有效地将输入的模式扩散到哈希值的各个位。Paul Larson 在 1988 年的研究中证明了乘以小奇数的哈希函数在字符串哈希中具有良好的分布特性。

### 2.8.2 哈希表结构

```c
#define TOK_HASH_SIZE 16384  /* 必须是 2 的幂 */
static TokenSym *hash_ident[TOK_HASH_SIZE];
```

哈希表使用链地址法解决冲突。每个桶是一个 `TokenSym` 链表，通过 `hash_next` 指针连接。

```
hash_ident[0]  -> TokenSym -> TokenSym -> NULL
hash_ident[1]  -> NULL
hash_ident[2]  -> TokenSym -> NULL
...
hash_ident[16383] -> TokenSym -> TokenSym -> TokenSym -> NULL
```

### 2.8.3 tok_alloc() —— 标识符查找与注册

```c
ST_FUNC TokenSym *tok_alloc(const char *str, int len)
{
    TokenSym *ts, **pts;
    int i;
    unsigned int h;

    h = TOK_HASH_INIT;
    for(i = 0; i < len; i++)
        h = TOK_HASH_FUNC(h, ((unsigned char *)str)[i]);
    h &= (TOK_HASH_SIZE - 1);

    pts = &hash_ident[h];
    for(;;) {
        ts = *pts;
        if (!ts)
            break;
        if (ts->len == len && !memcmp(ts->str, str, len))
            return ts;
        pts = &(ts->hash_next);
    }
    return tok_alloc_new(pts, str, len);
}
```

**工作流程：**

1. 计算字符串的哈希值
2. 在哈希桶中线性搜索
3. 如果找到匹配的 `TokenSym`（长度和内容都相同），返回它
4. 如果没找到，调用 `tok_alloc_new()` 创建新的条目

### 2.8.4 tok_alloc_new() —— 创建新条目

```c
static TokenSym *tok_alloc_new(TokenSym **pts, const char *str, int len)
{
    TokenSym *ts;
    ts = tal_realloc(&toksym_alloc, NULL,
                     sizeof(TokenSym) + len);
    ts->tok = tok_ident++;       /* 分配新的 token 编号 */
    *table_ident_ptr(ts) = ts;   /* 添加到 table_ident 数组 */
    ts->sym_define = NULL;
    ts->sym_label = NULL;
    ts->sym_struct = NULL;
    ts->sym_identifier = NULL;
    ts->len = len;
    ts->hash_next = NULL;
    memcpy(ts->str, str, len);
    ts->str[len] = '\0';
    *pts = ts;
    return ts;
}
```

新条目的 token 编号从 `TOK_IDENT`（256）开始递增分配。使用 `tal_realloc` 进行内存分配（来自 tcc 的自定义分配器），分配大小为 `sizeof(TokenSym) + len`。

### 2.8.5 table_ident 数组

```c
ST_DATA TokenSym **table_ident;
```

`table_ident` 是一个动态数组，将 token 编号映射到 `TokenSym` 指针。给定 token 编号 `tok`（>= `TOK_IDENT`），对应的 `TokenSym` 为 `table_ident[tok - TOK_IDENT]`。

---

## 2.9 数字和字符串解析

### 2.9.1 parse_number() —— 数字解析

`parse_number()` 将 `TOK_PPNUM` 的原始文本解析为具体的数字 token。它处理以下情况：

**（1）进制判断**

```c
b = 10;
if (t == '0') {
    if (ch == 'x' || ch == 'X') {
        b = 16;      /* 十六进制: 0x... */
    } else if (tcc_state->tcc_ext && (ch == 'b' || ch == 'B')) {
        b = 2;       /* 二进制 (GCC 扩展): 0b... */
    }
}
```

如果没有 `0x` 或 `0b` 前缀，且数字以 `0` 开头，则在后续的整数解析阶段将基数从 10 改为 8（八进制）。

**（2）浮点数检测**

```c
if (ch == '.' ||
    ((ch == 'e' || ch == 'E') && b == 10) ||
    ((ch == 'p' || ch == 'P') && (b == 16 || b == 2))) {
    /* 浮点数解析 */
}
```

浮点数的判定条件：
- 包含小数点 `.`
- 十进制数包含 `e`/`E` 指数
- 十六进制或二进制数包含 `p`/`P` 指数

**（3）十六进制和二进制浮点数**

tcc 手动实现十六进制和二进制浮点数的解析，使用 128 位大数运算（`BN_SIZE = 4`）来避免精度损失：

```c
frac_bits = 0;
bn_zero(bn);
q = token_buf;
while (1) {
    t = *q++;
    /* ... 将字符转换为数值 ... */
    frac_bits -= bn_lshift(bn, shift, t);
}
/* 计算浮点值 */
d = (long double)bn[3] * 79228162514264337593543950336.0L +
    (long double)bn[2] * 18446744073709551616.0L +
    (long double)bn[1] * 4294967296.0L +
    (long double)bn[0];
d = ldexpl(d, exp_val - frac_bits);
```

**（4）类型后缀解析**

```c
/* 浮点后缀 */
if (t == 'F')      tok = TOK_CFLOAT;    /* float */
else if (t == 'L') tok = TOK_CLDOUBLE;  /* long double */
else               tok = TOK_CDOUBLE;   /* double (默认) */

/* 整数后缀 */
/* 解析 l/ll/u 的组合 */
lcount = ucount = 0;
for(;;) {
    t = toup(ch);
    if (t == 'L')      lcount++;
    else if (t == 'U') ucount++;
    else break;
    ch = *p++;
}

/* 根据后缀确定类型 */
tok = TOK_CINT;
if (lcount) {
    tok = TOK_CLONG;
    if (lcount == 2) tok = TOK_CLLONG;
}
if (ucount) ++tok;  /* TOK_CINT->TOK_CUINT, TOK_CLONG->TOK_CULONG, ... */
```

> **巧妙的 `++tok` 设计**：tcc 将有符号和无符号类型的 token 编号安排为连续的奇偶对：
> - `TOK_CINT` (0xC2) / `TOK_CUINT` (0xC3)
> - `TOK_CLLONG` (0xC4) / `TOK_CULLONG` (0xC5)
> - `TOK_CLONG` (0xC6) / `TOK_CULONG` (0xC7)
>
> 因此 `++tok` 就能将有符号类型转换为对应的无符号类型。

### 2.9.2 parse_string() —— 字符串解析

`parse_string()` 将 `TOK_PPSTR` 的原始文本解析为 `TOK_CCHAR`、`TOK_STR` 或 `TOK_LSTR`：

```c
static void parse_string(const char *s, int len)
{
    uint8_t buf[1000], *p = buf;
    int is_long, sep;

    if ((is_long = *s == 'L'))
        ++s, --len;
    sep = *s++;                  /* 引号字符 (' 或 ") */
    len -= 2;                    /* 去掉两端的引号 */
    /* ... */
    parse_escape_string(&tokcstr, p, is_long);

    if (sep == '\'') {
        tok = is_long ? TOK_LCHAR : TOK_CCHAR;
        /* 将字符值存入 tokc.i */
    } else {
        tok = is_long ? TOK_LSTR : TOK_STR;
        /* 将字符串数据存入 tokc.str */
    }
}
```

### 2.9.3 parse_escape_string() —— 转义序列解析

`parse_escape_string()` 负责解析字符串中的转义序列：

| 转义序列 | 解析方式 | 结果 |
|----------|---------|------|
| `\0` - `\7` | 最多 3 位八进制数 | 字符值 |
| `\xHH` | 十六进制数（任意位数） | 字符值 |
| `\uHHHH` | 4 位十六进制 Unicode | UTF-8 编码 |
| `\UHHHHHHHH` | 8 位十六进制 Unicode | UTF-8 编码 |
| `\a` | 固定值 0x07 | 响铃 |
| `\b` | 固定值 0x08 | 退格 |
| `\f` | 固定值 0x0C | 换页 |
| `\n` | 固定值 0x0A | 换行 |
| `\r` | 固定值 0x0D | 回车 |
| `\t` | 固定值 0x09 | 制表符 |
| `\v` | 固定值 0x0B | 垂直制表 |
| `\e` | 固定值 27（仅 GCC 扩展） | ESC |
| `\\`, `\'`, `\"`, `\?` | 保持原样 | 对应字符 |

Unicode 转义（`\u` 和 `\U`）的处理特别值得注意。对于非宽字符串，Unicode 码点被转换为 UTF-8 编码存入字符串；对于宽字符串，直接存储码点值。

```c
case 'x': i = 0; goto parse_hex_or_ucn;
case 'u': i = 4; goto parse_hex_or_ucn;
case 'U': i = 8; goto parse_hex_or_ucn;
parse_hex_or_ucn:
    p++;
    n = 0;
    do {
        c = *p;
        /* ... 将十六进制字符转换为数值 ... */
        n = (unsigned) n * 16 + c;
        p++;
    } while (--i);
    if (is_long) {
        c = n;
        goto add_char_nonext;     /* 宽字符串：直接存储码点 */
    }
    cstr_u8cat(outstr, n);        /* 普通字符串：UTF-8 编码 */
    continue;
```

---

## 2.10 性能优化技巧

tcc 的词法分析器虽然代码量不大，但包含了大量精心设计的性能优化。

### 2.10.1 缓冲区内的快速路径

最内层的循环（如标识符扫描的 `while` 循环）直接通过指针遍历缓冲区，不需要在每次迭代时检查缓冲区边界：

```c
while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
    h = TOK_HASH_FUNC(h, c);
```

这得益于缓冲区末尾的 `CH_EOB` 哨兵。如果标识符跨越缓冲区边界，哨兵字符 `\\` 不满足 `IS_ID|IS_NUM` 条件，循环自然终止。随后的 `handle_stray()` 调用会检测到这是缓冲区边界而非真正的 `\`，触发缓冲区重填。

### 2.10.2 哈希计算与扫描同步

在大多数编译器实现中，标识符的扫描和哈希计算是两个独立的步骤。tcc 将它们合并为一步：

```c
p1 = p;
h = TOK_HASH_INIT;
h = TOK_HASH_FUNC(h, c);
while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
    h = TOK_HASH_FUNC(h, c);
/* 此时 h 已经计算完成，可以直接用于查找 */
```

这避免了对标识符字符串的第二次遍历，对于长标识符（如 `__builtin_types_compatible_p`）效果尤为明显。

### 2.10.3 isidnum_table 查找优化

使用位标志的查找表代替条件判断：

```c
/* 条件判断方式（慢） */
if (isalpha(c) || isdigit(c) || c == '_')

/* 表查找方式（快） */
if (isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
```

表查找方式只需要一次内存访问和一次位与操作，而条件判断方式需要多次函数调用和逻辑或操作。

### 2.10.4 直接使用 ASCII 值作为 Token 编号

对于单字符 token，直接使用 ASCII 值作为 token 编号，避免了额外的查找或映射：

```c
tok = c;  /* 直接使用字符值 */
```

这意味着在解析器中匹配这些 token 也不需要额外的开销：

```c
if (tok == '(')  /* 直接比较，不需要查表 */
```

### 2.10.5 TokenSym 的直接指针访问

`TokenSym` 中的四个 `Sym*` 指针（`sym_define`、`sym_label`、`sym_struct`、`sym_identifier`）使得语义查找成为 O(1) 操作。大多数编译器需要在符号表中进行哈希查找来获取这些信息。

### 2.10.6 自定义内存分配器

tcc 使用 `TinyAlloc` 分配器来管理 `TokenSym` 和 `TokenString` 的内存。`TinyAlloc` 是一个简单的块分配器：

```c
#define TOKSYM_TAL_SIZE (256 * 1024)  /* 256KB 块 */
#define TOKSTR_TAL_SIZE (256 * 1024)
```

它一次性分配大块内存，然后在块内线性分配小对象。这避免了 `malloc`/`free` 的系统调用开销和内存碎片。

### 2.10.7 PEEKC 宏的延迟检查

`PEEKC` 宏只在遇到 `\` 时才调用 `handle_stray()`：

```c
#define PEEKC(c, p)                    \
{                                      \
    c = *++p;                          \
    if (c == '\\')                     \
        c = handle_stray(&p);          \
}
```

在大多数情况下（没有续行），这只是一次指针递增和一次条件分支（不跳转），开销极小。

---

## 2.11 本章小结与练习

### 本章小结

本章详细分析了 tcc 词法分析器的实现。以下是关键要点：

1. **Token 编码方案**：tcc 使用精心设计的编号空间，将 0x00-0x7F 分配给单字符 token，0x80-0xCF 分配给内部操作符和常量，>=256 分配给标识符和关键字。这种设计使得大部分 token 的处理可以简化为整数比较。

2. **缓冲区 I/O**：使用固定大小的缓冲区（8192 字节）和 `CH_EOB` 哨兵字符，使得内层循环不需要边界检查。`BufferedFile` 结构体整合了缓冲区和文件元信息。

3. **两阶段扫描**：`next_nomacro()` 负责底层扫描，`next()` 在此基础上添加宏展开。预处理阶段数字和字符串以原始形式（`TOK_PPNUM`/`TOK_PPSTR`）保存，在需要时才解析。

4. **哈希标识符查找**：在扫描标识符的同时计算哈希值，避免了额外的遍历。使用 16384 桶的哈希表和链地址法。

5. **性能优化**：包括缓冲区内快速路径、哈希与扫描同步、位标志字符分类表、直接 ASCII 值映射、直接指针语义查找、自定义内存分配器等多层次优化。

6. **X-macro 模式**：`tcctok.h` 通过不同的 `DEF` 宏定义，一次编写多次使用，生成字符串表、枚举常量和符号注册代码。

### 练习

**练习 2.1**：给定一段 C 代码，识别其中的所有 token 及其编号。参见 `exercises/ex1_tokens.md`。

**练习 2.2**：手算 `TOK_HASH_FUNC` 对字符串 `"main"` 的哈希值。参见 `exercises/ex2_hash.md`。

**练习 2.3**：修改 tcc 源码，添加一个新的关键字。参见 `exercises/ex3_modify.md`。

### 延伸阅读

1. Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools* (2nd ed.). Chapter 3: Lexical Analysis.
2. Bellard, F. (2002). "TCC: The Smallest ANSI C Compiler." *Dr. Dobb's Journal*.
3. Kernighan, B. W., & Ritchie, D. M. (1988). *The C Programming Language* (2nd ed.). Appendix A: C Reference Manual.
4. Larson, P. (1988). "Dynamic Hash Tables." *Communications of the ACM*, 31(4), 446-457.


===== FILE: docs/ch03/examples/conditional.c =====
/*
 * conditional.c - Demonstrates conditional compilation in TinyCC
 *
 * Compile with:
 *   tcc -E conditional.c                    # see preprocessed output
 *   tcc -DDEBUG_MODE -o cond conditional.c  # define DEBUG_MODE
 *   tcc -o cond conditional.c               # without DEBUG_MODE
 */

#include <stdio.h>

/* ============================================================
 * 1. #ifdef / #ifndef - basic include guard pattern
 * ============================================================ */

/* Simulated include guard (normally in a header file) */
#ifndef MY_HEADER_H
#define MY_HEADER_H

#define HEADER_VERSION 1

#endif /* MY_HEADER_H */

/* ============================================================
 * 2. #if / #elif / #else / #endif - value-based branching
 * ============================================================ */

/* Feature flags (for use in conditional compilation examples below) */
#define FEATURE_A 1
/* FEATURE_B intentionally not defined */

#define PLATFORM_LINUX 1
#define PLATFORM_WINDOWS 2
#define PLATFORM_MACOS 3

/* Detect platform */
#if defined(__linux__)
    #define CURRENT_PLATFORM PLATFORM_LINUX
    #define PLATFORM_NAME "Linux"
#elif defined(_WIN32)
    #define CURRENT_PLATFORM PLATFORM_WINDOWS
    #define PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
    #define CURRENT_PLATFORM PLATFORM_MACOS
    #define PLATFORM_NAME "macOS"
#else
    #define CURRENT_PLATFORM 0
    #define PLATFORM_NAME "Unknown"
#endif

/* ============================================================
 * 3. Nested conditional compilation
 * ============================================================ */

#define FEATURE_A 1
#define FEATURE_B 0
#define FEATURE_C 1

/* ============================================================
 * 4. #if with arithmetic expressions
 * ============================================================ */

#define VERSION_MAJOR 2
#define VERSION_MINOR 5
#define VERSION_PATCH 3

#define VERSION_CODE  (VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_PATCH)

/* ============================================================
 * 5. defined() operator
 * ============================================================ */

/* defined() can be used with or without parentheses */
#if defined FEATURE_A && !defined FEATURE_B
    #define FEATURE_A_ONLY 1
#endif

/* ============================================================
 * 6. Conditional debug macros
 * ============================================================ */

#ifdef DEBUG_MODE
    #define DBG_LOG(fmt, ...) \
        fprintf(stderr, "[DEBUG %s:%d] " fmt "\n", \
                __FILE__, __LINE__, ##__VA_ARGS__)
    #define DBG_ASSERT(cond) \
        do { \
            if (!(cond)) { \
                fprintf(stderr, "[ASSERT FAILED] %s (%s:%d)\n", \
                        #cond, __FILE__, __LINE__); \
            } \
        } while(0)
#else
    #define DBG_LOG(fmt, ...)   /* nothing */
    #define DBG_ASSERT(cond)    /* nothing */
#endif

/* ============================================================
 * 7. Header guard with #ifndef at file beginning
 *    (TinyCC's CachedInclude optimization detects this pattern)
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#define MAX_CONNECTIONS 128
#define TIMEOUT_SECONDS 30

#endif /* CONFIG_H */

/* ============================================================
 * 8. Multiple #elif chains
 * ============================================================ */

#if VERSION_CODE >= 30000
    #define VERSION_STR "3.x or later"
#elif VERSION_CODE >= 20000
    #define VERSION_STR "2.x"
#elif VERSION_CODE >= 10000
    #define VERSION_STR "1.x"
#else
    #define VERSION_STR "0.x (pre-release)"
#endif

/* ============================================================
 * 9. Conditional compilation with enum/struct
 * ============================================================ */

#if FEATURE_C
typedef struct {
    int x;
    int y;
    int z;
} Point3D;
#else
typedef struct {
    int x;
    int y;
} Point2D;
#endif

int main(void)
{
    printf("=== Conditional Compilation Demo ===\n\n");

    /* Platform detection */
    printf("Platform: %s (code=%d)\n", PLATFORM_NAME, CURRENT_PLATFORM);

    /* Version info */
    printf("Version code: %d\n", VERSION_CODE);
    printf("Version range: %s\n", VERSION_STR);
    printf("Header guard version: %d\n", HEADER_VERSION);

    /* Feature flags */
    printf("\nFeature A: %s\n", FEATURE_A ? "enabled" : "disabled");
    printf("Feature B: %s\n", FEATURE_B ? "enabled" : "disabled");
    printf("Feature C: %s\n", FEATURE_C ? "enabled" : "disabled");
    printf("Feature A only: %s\n",
#ifdef FEATURE_A_ONLY
           FEATURE_A_ONLY ? "yes" : "no"
#else
           "no (FEATURE_A_ONLY not defined)"
#endif
           );

    /* Debug macros */
    DBG_LOG("starting main");
    DBG_ASSERT(1 + 1 == 2);
    DBG_ASSERT(VERSION_CODE > 0);
    printf("Debug mode: %s\n",
#ifdef DEBUG_MODE
           "enabled (compile with -DDEBUG_MODE)"
#else
           "disabled (compile without -DDEBUG_MODE)"
#endif
    );

    /* Conditional struct */
    printf("\nStruct size: %zu bytes\n",
#if FEATURE_C
           sizeof(Point3D)
#else
           sizeof(Point2D)
#endif
    );

    /* Nested conditions */
#if FEATURE_A
    #if FEATURE_B
        printf("Both A and B are enabled\n");
    #elif FEATURE_C
        printf("A and C are enabled, B is disabled\n");
    #else
        printf("Only A is enabled\n");
    #endif
#else
    printf("A is disabled\n");
#endif

    /* Platform-specific code */
    printf("\nPlatform-specific paths:\n");
#if CURRENT_PLATFORM == PLATFORM_LINUX
    printf("  Config: /etc/myapp.conf\n");
#elif CURRENT_PLATFORM == PLATFORM_WINDOWS
    printf("  Config: %%APPDATA%%\\myapp\\config.ini\n");
#elif CURRENT_PLATFORM == PLATFORM_MACOS
    printf("  Config: ~/Library/Preferences/myapp.plist\n");
#else
    printf("  Config: ./myapp.conf\n");
#endif

    /* Test __has_include (C23 feature, supported by TinyCC) */
#if __has_include(<stdint.h>)
    printf("\n<stdint.h> is available\n");
#endif

    printf("\nAll conditional compilation demos completed.\n");
    return 0;
}


===== FILE: docs/ch03/examples/macro_demo.c =====
/*
 * macro_demo.c - Demonstrates all macro features in TinyCC preprocessor
 *
 * Compile with:
 *   tcc -E macro_demo.c          # see preprocessed output
 *   tcc -o macro_demo macro_demo.c && ./macro_demo
 */

#include <stdio.h>

/* ============================================================
 * 1. Object-like macros (MACRO_OBJ)
 * ============================================================ */

#define PI 3.14159265358979
#define MAX_BUFFER_SIZE 1024
#define GREETING "Hello, TinyCC!"
#define EMPTY_MACRO   /* empty body */

/* ============================================================
 * 2. Function-like macros (MACRO_FUNC)
 * ============================================================ */

#define SQUARE(x)       ((x) * (x))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define CLAMP(val, lo, hi)  MIN(MAX((val), (lo)), (hi))

/* ============================================================
 * 3. Stringification (#)
 * ============================================================ */

#define STR(x)          #x
#define STR_EXPAND(x)   STR(x)   /* double-expand to stringify macro values */

/* ============================================================
 * 4. Token pasting (##)
 * ============================================================ */

#define CONCAT(a, b)        a ## b
#define CONCAT3(a, b, c)    a ## b ## c
#define MAKE_FUNC(prefix, name)  prefix ## _ ## name

/* ============================================================
 * 5. Variadic macros (__VA_ARGS__)
 * ============================================================ */

/* Standard C99 variadic macro */
#define LOG(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

/* Variadic macro with count */
#define VA_COUNT(...)   (sizeof((int[]){__VA_ARGS__}) / sizeof(int))

/* ============================================================
 * 6. Self-referential macro (prevented from infinite recursion)
 * ============================================================ */

#define SELF SELF   /* macro_subst() prevents infinite expansion */

/* ============================================================
 * 7. Macro with macro arguments
 * ============================================================ */

#define APPLY(fn, x)   fn(x)
#define DOUBLE(x)      ((x) + (x))

/* ============================================================
 * 8. Special built-in macros
 * ============================================================ */

#define WHERE  __FILE__ ":" STR_EXPAND(__LINE__)

/* ============================================================
 * Demo functions
 * ============================================================ */

/* Token pasting creates new identifier */
#define MAKE_VAR(n)   var_##n

static int MAKE_VAR(x) = 10;    /* expands to: int var_x = 10; */
static int MAKE_VAR(y) = 20;    /* expands to: int var_y = 20; */

/* Function created by token pasting */
MAKE_FUNC(int, add)(int a, int b) { return a + b; }
/* expands to: int add(int a, int b) { return a + b; } */

int main(void)
{
    int result;

    /* --- Object macros --- */
    printf("=== Object-like Macros ===\n");
    printf("PI = %f\n", PI);
    printf("MAX_BUFFER_SIZE = %d\n", MAX_BUFFER_SIZE);
    printf("GREETING = %s\n", GREETING);

    /* --- Function macros --- */
    printf("\n=== Function-like Macros ===\n");
    result = SQUARE(5);
    printf("SQUARE(5) = %d\n", result);

    result = SQUARE(2 + 3);
    printf("SQUARE(2+3) = %d  (note: safe due to parens)\n", result);

    printf("MAX(10, 20) = %d\n", MAX(10, 20));
    printf("MIN(10, 20) = %d\n", MIN(10, 20));
    printf("CLAMP(15, 0, 10) = %d\n", CLAMP(15, 0, 10));

    /* --- Stringification --- */
    printf("\n=== Stringification (#) ===\n");
    printf("STR(hello) = %s\n", STR(hello));
    printf("STR(1 + 2) = %s\n", STR(1 + 2));
    /* STR_EXPAND expands the macro first, then stringifies */
    printf("STR_EXPAND(PI) = %s\n", STR_EXPAND(PI));
    printf("STR_EXPAND(__LINE__) = %s\n", STR_EXPAND(__LINE__));

    /* --- Token pasting --- */
    printf("\n=== Token Pasting (##) ===\n");
    printf("var_x = %d\n", var_x);
    printf("var_y = %d\n", var_y);
    printf("int_add(3, 4) = %d\n", int_add(3, 4));

    /* CONCAT creates a new pp-token */
    result = CONCAT(12, 34);
    printf("CONCAT(12, 34) = %d\n", result);

    /* --- Variadic macros --- */
    printf("\n=== Variadic Macros (__VA_ARGS__) ===\n");
    LOG("system ready, value=%d", 42);
    LOG("no extra args");

    /* --- Built-in macros --- */
    printf("\n=== Built-in Macros ===\n");
    printf("__LINE__ = %d\n", __LINE__);
    printf("__FILE__ = %s\n", __FILE__);
    printf("__DATE__ = %s\n", __DATE__);
    printf("__TIME__ = %s\n", __TIME__);
    printf("__STDC__ = %d\n", __STDC__);
    printf("__TINYC__ = %d\n", __TINYC__);
    printf("Location: %s\n", WHERE);

    /* --- Macro expansion with nested macros --- */
    printf("\n=== Nested Macro Expansion ===\n");
    /* APPLY(DOUBLE, 5) -> DOUBLE(5) -> ((5) + (5)) -> 10 */
    result = APPLY(DOUBLE, 5);
    printf("APPLY(DOUBLE, 5) = %d\n", result);

    /* --- Self-referential macro --- */
    /* SELF expands to SELF; macro_subst() marks it as nosubst */
    /* We can't directly print it, but tcc -E shows SELF remains */
    printf("\n=== Self-referential Macro ===\n");
    printf("SELF does not infinitely expand (see tcc -E output)\n");

    /* --- Empty __VA_ARGS__ with ## --- */
    printf("\n=== Empty __VA_ARGS__ ===\n");
    /* LOG("simple") has empty __VA_ARGS__, ## eats the comma */
    LOG("no varargs here");

    printf("\nAll macro demos completed.\n");
    return 0;
}


===== FILE: docs/ch03/exercises/ex1_expand.md =====
# 练习 1：宏展开追踪

## 目标

手动追踪 TinyCC 预处理器中宏展开的完整过程，理解三阶段展开机制。

## 背景

TinyCC 的宏展开分为三个阶段：
1. `macro_subst_tok()` — 函数宏参数收集
2. `macro_arg_subst()` — 参数替换、字符串化、拼接
3. `macro_subst()` — 递归展开，防止无限递归

## 任务

对于以下每个宏展开示例，请逐步写出展开过程。标注每一步涉及的函数。

### 示例 A：简单对象宏

```c
#define N 100
int x = N;
```

**展开过程：**

1. `next()` 读取标识符 `N`，调用 `define_find(N)` 找到定义。
2. `macro_subst_tok()` 发现 `s->d` 非空且不是 `MACRO_FUNC`。
3. 无 `##` 操作，直接调用 `macro_subst(tok_str, nested_list, s->d)`。
4. `macro_subst()` 从宏体中读取 `TOK_PPNUM(100)`，输出到 `tok_str`。
5. 最终 token：`int x = 100;`

---

### 示例 B：函数宏与括号保护

```c
#define SQUARE(x) ((x) * (x))
int y = SQUARE(3 + 4);
```

**展开过程：**

1. `next()` 读取 `SQUARE`，`define_find()` 找到函数宏定义。
2. `macro_subst_tok()` 进入函数宏分支。
3. `next_argstream()` 前瞻读取 `(`，确认是函数调用。
4. 参数收集：
   - 形参 `x` 对应实参 `3 + 4`（注意：`+` 在括号匹配中不是分隔符）。
   - `args->d = [3, ' ', +, ' ', 4]`。
5. `macro_arg_subst()` 替换宏体中的 `x`：
   - 宏体：`(( x ) * ( x ))`
   - 替换后：`(( 3 + 4 ) * ( 3 + 4 ))`
6. `macro_subst()` 递归展开结果（无更多宏）。
7. 最终 token：`int y = (( 3 + 4 ) * ( 3 + 4 ));`

**请解答：** 为什么 `SQUARE(x)` 的定义中 `x` 要用括号包围？如果不加括号（`#define SQUARE(x) x * x`），`SQUARE(3+4)` 会得到什么？

---

### 示例 C：字符串化

```c
#define STR(x) #x
#define XSTR(x) STR(x)
#define VERSION 2
const char *v1 = STR(VERSION);
const char *v2 = XSTR(VERSION);
```

**请分别追踪 `v1` 和 `v2` 的展开过程：**

**v1 的展开：**

1. `next()` 读取 `STR`。
2. `macro_subst_tok()` 收集参数：实参为 `VERSION`（注意：`#` 操作符的参数不预先展开）。
3. `macro_arg_subst()` 遇到 `#`，将 `VERSION` 字符串化为 `"VERSION"`。
4. 结果：`const char *v1 = "VERSION";`

**v2 的展开（请自行完成）：**

提示：`XSTR(VERSION)` 中 `VERSION` 先被 `XSTR` 的参数替换（无 `#`），然后再传给 `STR`。

---

### 示例 D：Token 拼接

```c
#define CONCAT(a, b) a ## b
#define MAKE_VAR(n) var_ ## n
int CONCAT(hello, world) = 42;
int MAKE_VAR(count) = 0;
```

**展开过程（请自行完成）：**

提示：
- `macro_subst_tok()` 中，`MACRO_JOIN` 标志触发 `macro_twosharps()`。
- `macro_twosharps()` 将 `a` 和 `b` 的文本拼接，创建临时文件 `:paste:`，重新词法分析。

---

### 示例 E：可变参数与空 VA_ARGS

```c
#define LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
LOG("hello");
LOG("x=%d", 42);
```

**请分别追踪两次 `LOG` 调用的展开过程：**

**LOG("hello")：**

1. 参数收集：`fmt = "hello"`，`__VA_ARGS__ = []`（空）。
2. `macro_arg_subst()` 中，`##__VA_ARGS__` 前有 `,`，且 `__VA_ARGS__` 为空。
3. GNU 扩展：删除 `,` 和 `##`。
4. 结果：`fprintf(stderr, "hello" "\n")`

**LOG("x=%d", 42)（请自行完成）：**

---

### 示例 F：嵌套展开与自引用防止

```c
#define A B
#define B A + 1
int x = A;
```

**展开过程：**

1. `next()` 读取 `A`，`define_find()` 找到定义 `B`。
2. `macro_subst_tok()` 将 `A` 压入 `nested_list`，调用 `macro_subst()` 展开 `B`。
3. `macro_subst()` 读取 `B`，`define_find()` 找到定义 `A + 1`。
4. 检查 `nested_list`：`A` 已在其中！标记 `A` 为 `SYM_FIELD`（不展开）。
5. 输出 `A`（不再展开）和 `1`。
6. `macro_subst_tok()` 弹出 `A`。
7. 最终 token：`int x = A + 1;`

**请解答：** 为什么 `A` 在展开 `B` 的过程中不再展开，但之后可以继续展开？这与 C 标准的哪一条规定对应？

---

## 验证方法

使用 `tcc -E` 验证你的展开结果：

```bash
tcc -E your_file.c
```

使用 `tcc -E -dD` 可以同时看到宏定义和展开结果。

## 思考题

1. 为什么 `macro_subst()` 需要 `nosubst` 标志？它在什么场景下使用？
2. `TOK_PLCHLDR` 占位符在 `##` 操作中起什么作用？
3. 为什么 `__COUNTER__` 宏的参数只能展开一次（`s->e` 缓存机制）？


===== FILE: docs/ch03/exercises/ex2_include.md =====
# 练习 2：#include 搜索路径追踪

## 目标

理解 TinyCC 中 `#include` 指令的文件搜索机制，包括搜索顺序、`CachedInclude` 优化和 `#pragma once`。

## 背景

TinyCC 的 `parse_include()` 函数（`tccpp.c:1314`）处理 `#include` 指令。搜索路径按以下顺序：

1. **绝对路径**（`i == 0`）：如果文件名以 `/` 开头。
2. **当前文件目录**（`i == 1`）：仅对 `""` 形式有效。
3. **用户包含路径**（`-I` 指定）：`s1->include_paths[]`。
4. **系统包含路径**（`-isystem` 指定）：`s1->sysinclude_paths[]`。

## 场景

假设有以下目录结构：

```
/home/user/project/
├── main.c
├── myheader.h
├── sub/
│   ├── helper.h
│   └── internal.h
└── third_party/
    └── lib/
        └── lib.h
```

`main.c` 的内容：

```c
#include "myheader.h"
#include "sub/helper.h"
#include <stdio.h>
#include "lib.h"
```

编译命令：

```bash
tcc -I /home/user/project/third_party -c main.c
```

## 任务

### 任务 1：追踪 `#include "myheader.h"` 的搜索路径

逐步列出 `parse_include()` 中的搜索过程：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | （绝对路径检查） | — | 跳过（不是绝对路径） |
| 2 | 1 | 当前文件目录 `/home/user/project/` | `/home/user/project/myheader.h` | **找到** |

---

### 任务 2：追踪 `#include "sub/helper.h"` 的搜索路径

填写下表：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | |
| 3 | 2 | | | |

---

### 任务 3：追踪 `#include <stdio.h>` 的搜索路径

填写下表（注意 `<>` 形式跳过当前文件目录）：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | （为什么跳过？） |
| 3 | 2 | | | |
| ... | | | | |

提示：TinyCC 的默认系统包含路径通常是 `/usr/include` 和 `/usr/local/lib/tcc/include`。

---

### 任务 4：追踪 `#include "lib.h"` 的搜索路径

填写下表：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | |
| 3 | 2 | | | |

---

### 任务 5：CachedInclude 优化分析

考虑以下头文件 `config.h`：

```c
#ifndef CONFIG_H
#define CONFIG_H

#define MAX_SIZE 1024
#define VERSION 2

#endif
```

**问题：**

1. TinyCC 如何检测这是一个 include guard？提示：关注 `preprocess()` 中 `is_bof` 参数和 `file->ifndef_macro` 字段。

2. 当 `main.c` 中第二次 `#include "config.h"` 时，TinyCC 如何利用 `CachedInclude` 跳过重复解析？

3. `CachedInclude` 结构体中 `ifndef_macro` 字段存储的是什么值？`search_cached_include()` 中如何使用它？

---

### 任务 6：#pragma once

```c
// singleton.h
#pragma once

static int counter = 0;
```

**问题：**

1. `#pragma once` 在 TinyCC 中是如何实现的？（提示：查看 `pragma_parse()` 中 `TOK_once` 分支。）

2. `#pragma once` 和 include guard 模式（`#ifndef/#define/#endif`）在 TinyCC 中的实现有何异同？

3. 如果一个文件同时使用了 `#pragma once` 和 include guard，TinyCC 的行为是什么？

---

### 任务 7：#include_next

`#include_next` 是一个 GNU 扩展，用于在搜索路径的"下一个"位置查找文件。

假设编译命令为：

```bash
tcc -I /path/a -I /path/b -I /path/c -c main.c
```

文件 `/path/a/header.h` 中包含：

```c
#include_next "header.h"
```

**问题：**

1. `#include_next` 的搜索从哪个路径开始？（提示：查看 `file->include_next_index`。）

2. 这个特性在什么场景下有用？（提示：考虑"覆盖式"头文件。）

---

## 验证方法

使用 `tcc -vv` 查看文件包含的详细信息：

```bash
tcc -vv -I /home/user/project/third_party -c main.c
```

输出会显示每个文件的包含路径和跳过信息。

## 思考题

1. 为什么 `<>` 形式不搜索当前文件目录？这有什么安全考虑？
2. `search_cached_include()` 中的 `normalized_PATHCMP()` 是做什么的？为什么需要它？
3. `INCLUDE_STACK_SIZE` 设为 32，如果超过这个限制会发生什么？为什么需要这个限制？


===== FILE: docs/ch03/exercises/ex3_pragma.md =====
# 练习 3：#pragma pack 与结构体布局

## 目标

通过实验理解 `#pragma pack` 对结构体内存布局的影响，并分析 TinyCC 中 pack 栈的实现机制。

## 背景

`#pragma pack` 控制结构体成员的对齐方式。TinyCC 使用一个栈（`pack_stack`）来管理嵌套的 pack 设置：

```c
/* TCCState 中的定义 */
int pack_stack[PACK_STACK_SIZE];
int *pack_stack_ptr;
```

对齐值必须是 1 到 16 之间的 2 的幂（或 0 表示默认对齐）。

## 实验代码

创建文件 `pack_demo.c`，内容如下：

```c
#include <stdio.h>
#include <stddef.h>

/* 默认对齐 */
struct DefaultAlign {
    char  a;    /* 1 byte */
    int   b;    /* 4 bytes */
    char  c;    /* 1 byte */
    short d;    /* 2 bytes */
};

/* pack(1)：无填充 */
#pragma pack(push, 1)
struct Pack1 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* pack(2)：2 字节对齐 */
#pragma pack(push, 2)
struct Pack2 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* pack(4)：4 字节对齐 */
#pragma pack(push, 4)
struct Pack4 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* 嵌套 pack 示例 */
#pragma pack(push, 1)
struct Outer {
    char x;
    #pragma pack(push, 4)
    struct Inner {
        char  a;
        int   b;
    } inner;
    #pragma pack(pop)
    char y;
};
#pragma pack(pop)

#define PRINT_OFFSET(type, member) \
    printf("  %-20s offset=%-3zu size=%zu\n", \
           #member, offsetof(type, member), sizeof(((type*)0)->member))

int main(void)
{
    printf("=== Structure Layout Analysis ===\n\n");

    printf("DefaultAlign (size=%zu):\n", sizeof(struct DefaultAlign));
    PRINT_OFFSET(struct DefaultAlign, a);
    PRINT_OFFSET(struct DefaultAlign, b);
    PRINT_OFFSET(struct DefaultAlign, c);
    PRINT_OFFSET(struct DefaultAlign, d);

    printf("\nPack1 (size=%zu):\n", sizeof(struct Pack1));
    PRINT_OFFSET(struct Pack1, a);
    PRINT_OFFSET(struct Pack1, b);
    PRINT_OFFSET(struct Pack1, c);
    PRINT_OFFSET(struct Pack1, d);

    printf("\nPack2 (size=%zu):\n", sizeof(struct Pack2));
    PRINT_OFFSET(struct Pack2, a);
    PRINT_OFFSET(struct Pack2, b);
    PRINT_OFFSET(struct Pack2, c);
    PRINT_OFFSET(struct Pack2, d);

    printf("\nPack4 (size=%zu):\n", sizeof(struct Pack4));
    PRINT_OFFSET(struct Pack4, a);
    PRINT_OFFSET(struct Pack4, b);
    PRINT_OFFSET(struct Pack4, c);
    PRINT_OFFSET(struct Pack4, d);

    printf("\nOuter with nested pack (size=%zu):\n", sizeof(struct Outer));
    PRINT_OFFSET(struct Outer, x);
    PRINT_OFFSET(struct Outer, inner);
    PRINT_OFFSET(struct Outer, inner.a);
    PRINT_OFFSET(struct Outer, inner.b);
    PRINT_OFFSET(struct Outer, y);

    return 0;
}
```

## 任务

### 任务 1：预测结构体布局

在编译和运行之前，手动计算每个结构体的大小和成员偏移量。画出内存布局图。

**DefaultAlign（默认对齐，通常 4 或 8 字节对齐）：**

```
偏移:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
内容:  [a][pad ][pad ][pad ][b             ][c][pad ][d    ][pad ]
```

请填写下表：

| 结构体 | a 的偏移 | b 的偏移 | c 的偏移 | d 的偏移 | 总大小 |
|--------|---------|---------|---------|---------|--------|
| DefaultAlign | 0 | | | | |
| Pack1 | 0 | | | | |
| Pack2 | 0 | | | | |
| Pack4 | 0 | | | | |

---

### 任务 2：编译运行

```bash
tcc -o pack_demo pack_demo.c && ./pack_demo
```

将实际输出与你的预测对比。如有差异，分析原因。

---

### 任务 3：pack 栈的行为分析

TinyCC 的 `#pragma pack` 使用栈管理。分析以下代码序列中 `pack_stack` 的状态变化：

```c
#pragma pack(push, 1)      /* 栈状态: [1] */
struct A { char a; int b; };
#pragma pack(push, 4)      /* 栈状态: [1, 4] */
struct B { char a; int b; };
#pragma pack(pop)           /* 栈状态: [1] */
struct C { char a; int b; };
#pragma pack(pop)           /* 栈状态: [] (默认) */
struct D { char a; int b; };
```

**问题：**

1. 每个 `#pragma pack(push, N)` 执行后，`pack_stack_ptr` 指向哪里？
2. `#pragma pack(pop)` 执行后，当前对齐值恢复为什么？
3. 如果 `pop` 的次数超过 `push` 的次数，会发生什么？（提示：查看 `pragma_parse()` 中的 `stk_error` 检查。）

---

### 任务 4：pack(push) 与 pack(push, N) 的区别

```c
/* 场景 A */
int current_align = 8;  /* 假设当前对齐为 8 */
#pragma pack(push)       /* 只压栈，不改变值 */
/* 此时对齐值是多少？ */

/* 场景 B */
#pragma pack(push, 2)    /* 压栈并设置为 2 */
/* 此时对齐值是多少？ */
```

分析 TinyCC 源码中 `pragma_parse()` 的实现，解释两者的区别。

---

### 任务 5：pack() 重置

```c
#pragma pack(2)
struct Packed { char a; int b; };
#pragma pack()             /* 重置为默认 */
struct Normal { char a; int b; };
```

**问题：**

1. `#pragma pack()`（无参数）在 TinyCC 中是如何实现的？
2. 它将对齐值设为什么？（提示：查看 `pragma_parse()` 中 `val` 的默认值。）

---

### 任务 6：嵌套 pack 与内部结构体

分析 `Outer` 结构体的布局：

```c
#pragma pack(push, 1)
struct Outer {
    char x;               /* 外层 pack(1) */
    #pragma pack(push, 4)
    struct Inner {
        char  a;          /* 内层 pack(4) */
        int   b;
    } inner;
    #pragma pack(pop)
    char y;               /* 恢复外层 pack(1) */
};
#pragma pack(pop)
```

**问题：**

1. `x` 和 `inner` 之间有填充吗？为什么？
2. `inner.a` 和 `inner.b` 之间有填充吗？为什么？
3. `inner.b` 和 `y` 之间有填充吗？为什么？
4. `Outer` 的总大小是多少？

---

### 任务 7：实现 pack 值验证

TinyCC 要求 pack 值满足以下条件：
- 范围：1 到 16
- 必须是 2 的幂

```c
if (val < 1 || val > 16 || (val & (val - 1)) != 0)
    goto pragma_err;
```

**问题：**

1. `(val & (val - 1)) != 0` 如何检测 2 的幂？为什么这有效？
2. 为什么 pack 值限制在 1-16 范围内？有什么实际原因？
3. 如果传入 `#pragma pack(3)`，TinyCC 会如何处理？

---

## 验证方法

1. 编译运行：`tcc -o pack_demo pack_demo.c && ./pack_demo`
2. 查看预处理输出：`tcc -E pack_demo.c`（观察 `#pragma` 如何被处理）
3. 使用 `offsetof()` 宏验证偏移量

## 思考题

1. `#pragma pack` 对性能有什么影响？在什么场景下应该使用 `pack(1)`？
2. 网络协议解析通常使用 `pack(1)`，为什么？有什么替代方案？
3. TinyCC 的 `PACK_STACK_SIZE` 设为多少？如果超过这个限制会发生什么？


===== FILE: docs/ch03/index.md =====
# 第3章 预处理器

预处理器是 C 编译器中最古老、最"原始"的子系统之一。它工作在词法分析器之上，对源文本进行词法级的变换——宏展开、文件包含、条件编译——然后将变换后的 token 流交给真正的语法分析器。TinyCC 的预处理器实现在 `tccpp.c` 中，约 4000 行 C 代码，是整个编译器中体量最大的单个文件。本章将逐函数、逐数据结构地拆解这一实现。

---

## 3.1 预处理器的理论与标准

### 3.1.1 C 标准的翻译阶段

ISO/IEC 9899（C11 §5.1.1.2）规定了源文件从字符序列到可执行程序的八个翻译阶段（translation phases）。预处理器负责其中前四个阶段：

| 阶段 | 说明 | TinyCC 实现位置 |
|------|------|-----------------|
| 1 | 物理源文件字符映射：将源文件字符集映射到基本源字符集。处理三字符组（trigraph）。 | `tcc_open()` / `handle_bs()` |
| 2 | 行接续：将反斜杠+换行（`\` `\n`）合并为逻辑行。 | `handle_bs()` 在 `tccpp.c` 中 |
| 3 | 词法分析：将字符流分解为预处理 token（pp-token）。 | `next_nomacro()`、`parse_number()`、`parse_string()` |
| 4 | 预处理指令执行：展开宏、处理 `#include`、`#if` 等。 | `preprocess()`、`macro_subst()` |
| 5 | 字符字面量与字符串字面量的字符集转换。 | `parse_escape_string()` |
| 6 | 相邻字符串字面量拼接。 | `parse_string()` |
| 7 | 语法分析与语义分析。 | `tccgen.c` |
| 8 | 链接。 | `tccelf.c`、`tcclink.c` |

TinyCC 的关键设计选择是**将阶段 1-6 全部集成在词法分析器 `tccpp.c` 中**，而不是像 GCC/Clang 那样分成独立的 libcpp 模块。这体现了 TinyCC "单一文件、单一职责"的极简哲学。

### 3.1.2 预处理 token vs 语法 token

C 标准区分两类 token：

- **预处理 token（pp-token）**：词法分析阶段 3 的产物，包括 `pp-number`、`pp-string` 等尚未完全解析的形式。
- **语法 token**：阶段 7 使用的最终 token，如整数常量 `TOK_CINT`、浮点常量 `TOK_CFLOAT`。

在 TinyCC 中，这一区分体现在 `TOK_PPNUM`（预处理数字）与 `TOK_CINT`/`TOK_CFLOAT`（已解析的数值常量）之间。预处理器内部只处理 pp-token，只有当 token 流交给语法分析器时才通过 `next()` → `convert` 标签进行转换：

```c
/* next() 末尾的转换逻辑 */
convert:
    if (t == TOK_PPNUM) {
        if (parse_flags & PARSE_FLAG_TOK_NUM)
            parse_number(tokc.str.data);
    } else if (t == TOK_PPSTR) {
        if (parse_flags & PARSE_FLAG_TOK_STR)
            parse_string(tokc.str.data, tokc.str.size - 1);
    }
```

### 3.1.3 TinyCC 预处理器的整体架构

TinyCC 预处理器的核心数据流如下：

```
源文件字符流
    │
    ▼
next_nomacro()  ── 词法分析，产生单个 pp-token
    │
    ▼
preprocess()    ── 分发 # 指令（#define, #include, #if 等）
    │
    ▼
next()          ── 宏展开，产生最终 token 流
    │
    ▼
tcc_preprocess() ── -E 模式输出
    │
    ▼
tccgen.c        ── 语法分析与代码生成
```

`next_nomacro()` 是"不展开宏"的词法分析器；`next()` 是"带宏展开"的词法分析器。预处理器指令通过 `preprocess()` 分发，宏展开通过 `macro_subst()` 递归完成。

---

## 3.2 预处理指令入口 preprocess()

`preprocess()` 函数（`tccpp.c:1792`）是所有 `#` 指令的总入口。每当词法分析器在行首遇到 `#` 时，就调用此函数。它是一个大型的 `switch` 分发表：

```c
ST_FUNC void preprocess(int is_bof)
{
    TCCState *s1 = tcc_state;
    int c, n, saved_parse_flags;
    char buf[1024], *q;
    Sym *s;

    saved_parse_flags = parse_flags;
    parse_flags = PARSE_FLAG_PREPROCESS
        | PARSE_FLAG_TOK_NUM
        | PARSE_FLAG_TOK_STR
        | PARSE_FLAG_LINEFEED
        | (parse_flags & PARSE_FLAG_ASM_FILE);

    next_nomacro();
redo:
    switch(tok) {
    case TOK_DEFINE:    /* #define */
    case TOK_UNDEF:     /* #undef */
    case TOK_INCLUDE:   /* #include */
    case TOK_INCLUDE_NEXT: /* #include_next */
    case TOK_IFNDEF:    /* #ifndef */
    case TOK_IF:        /* #if */
    case TOK_IFDEF:     /* #ifdef */
    case TOK_ELSE:      /* #else */
    case TOK_ELIF:      /* #elif */
    case TOK_ENDIF:     /* #endif */
    case TOK_LINE:      /* #line */
    case TOK_ERROR:     /* #error */
    case TOK_WARNING:   /* #warning */
    case TOK_PRAGMA:    /* #pragma */
    ...
    }
}
```

### 3.2.1 parse_flags 的作用

进入 `preprocess()` 时，函数会设置一组 `parse_flags` 标志位，控制后续词法分析的行为：

- `PARSE_FLAG_PREPROCESS`：表示当前处于预处理模式，标识符不做宏展开（由 `preprocess()` 自己控制）。
- `PARSE_FLAG_TOK_NUM`：允许将 pp-number 转换为数值常量。
- `PARSE_FLAG_TOK_STR`：允许解析字符串转义序列。
- `PARSE_FLAG_LINEFEED`：保留换行 token（`TOK_LINEFEED`），用于检测指令结束。
- `PARSE_FLAG_ASM_FILE`：汇编模式，保留 `.` 作为标识符字符。

指令处理完毕后，`parse_flags` 恢复为调用前的值，确保不影响后续的正常词法分析。

### 3.2.2 指令分发表详解

| 指令 | Token | 处理函数 | 说明 |
|------|-------|----------|------|
| `#define` | `TOK_DEFINE` | `parse_define()` | 定义宏 |
| `#undef` | `TOK_UNDEF` | `define_undef()` | 取消宏定义 |
| `#include` | `TOK_INCLUDE` | `parse_include()` | 包含头文件 |
| `#include_next` | `TOK_INCLUDE_NEXT` | `parse_include()` | 从下一个搜索路径包含 |
| `#ifdef` | `TOK_IFDEF` | `define_find()` | 测试宏是否已定义 |
| `#ifndef` | `TOK_IFNDEF` | `define_find()` | 测试宏是否未定义 |
| `#if` | `TOK_IF` | `expr_preprocess()` | 条件表达式求值 |
| `#elif` | `TOK_ELIF` | `expr_preprocess()` | else-if 分支 |
| `#else` | `TOK_ELSE` | — | else 分支 |
| `#endif` | `TOK_ENDIF` | — | 结束条件编译块 |
| `#line` | `TOK_LINE` | — | 设置行号和文件名 |
| `#error` | `TOK_ERROR` | — | 输出错误信息并终止 |
| `#warning` | `TOK_WARNING` | — | 输出警告信息 |
| `#pragma` | `TOK_PRAGMA` | `pragma_parse()` | 编译器特定指令 |

### 3.2.3 `is_bof` 参数

`preprocess()` 接收一个 `is_bof`（begin of file）参数。这个参数只有一个用途：当 `#ifndef` 出现在文件的第一行时，TinyCC 会记录这个宏名（`file->ifndef_macro`），用于 `CachedInclude` 优化（详见 3.9 节）。这是 GCC 等编译器广泛采用的 include guard 优化策略。

---

## 3.3 宏定义 #define

### 3.3.1 parse_define() 总体流程

`parse_define()`（`tccpp.c:1519`）负责解析 `#define` 之后的内容。其工作可分为三个阶段：

1. **解析宏名**：读取下一个 token 作为宏名（必须是标识符，不能是 `defined`）。
2. **解析参数列表**：如果宏名后紧跟 `(`，则解析为函数宏；否则为对象宏。
3. **收集宏体**：将行内剩余 token 收集到 `TokenString` 中，直到行尾。

```c
ST_FUNC void parse_define(void)
{
    Sym *s, *first, **ps;
    int v, t, varg, is_vaargs, t0;
    int saved_parse_flags = parse_flags;
    TokenString str;

    v = tok;                          /* 宏名 */
    if (v < TOK_IDENT || v == TOK_DEFINED)
        tcc_error("invalid macro name '%s'", get_tok_str(tok, &tokc));
    first = NULL;
    t = MACRO_OBJ;                    /* 默认为对象宏 */

    parse_flags = ((parse_flags & ~PARSE_FLAG_ASM_FILE)
                    | PARSE_FLAG_SPACES);
    next_nomacro();
    parse_flags &= ~PARSE_FLAG_SPACES;
    is_vaargs = 0;

    if (tok == '(') {                 /* 函数宏 */
        ...
    }

    /* 收集宏体 */
    ...
    define_push(v, t, str.str, first);
}
```

### 3.3.2 对象宏与函数宏的区分

对象宏（object-like macro）和函数宏（function-like macro）的区分依据是：宏名之后的下一个非空白 token 是否为 `(`。注意 `(` 必须**紧随**宏名，中间不能有空白。这是 C 标准的明确要求。

TinyCC 通过先调用 `next_nomacro()`（在设置了 `PARSE_FLAG_SPACES` 的状态下）读取下一个 token 来实现这一检查：

```c
parse_flags = ((parse_flags & ~PARSE_FLAG_ASM_FILE) | PARSE_FLAG_SPACES);
next_nomacro();
parse_flags &= ~PARSE_FLAG_SPACES;

if (tok == '(') {
    /* 函数宏：解析参数列表 */
    ...
    t = MACRO_FUNC;
}
```

`PARSE_FLAG_SPACES` 的作用是让空白字符也被返回为 token，从而确保 `(` 确实紧随宏名。如果中间有空白，`tok` 会先被读为空格 token 而不是 `(`。

### 3.3.3 函数宏参数解析

函数宏的参数解析在一个循环中完成：

```c
if (tok == '(') {
    int dotid = set_idnum('.', 0);   /* 汇编模式下 '.' 不作为 ID 字符 */
    next_nomacro();
    ps = &first;
    if (tok != ')') for (;;) {
        varg = tok;
        next_nomacro();
        is_vaargs = 0;
        if (varg == TOK_DOTS) {       /* C23: #define f(...) */
            varg = TOK___VA_ARGS__;
            is_vaargs = 1;
        } else if (tok == TOK_DOTS && gnu_ext) {  /* GNU: #define f(a, ...) */
            is_vaargs = 1;
            next_nomacro();
        }
        if (varg < TOK_IDENT)
            tcc_error("bad macro parameter list");
        s = sym_push2(&define_stack, varg | SYM_FIELD, is_vaargs, 0);
        *ps = s;
        ps = &s->next;
        if (tok == ')') break;
        if (tok != ',' || is_vaargs)
            tcc_error("bad macro parameter list");
        next_nomacro();
    }
    ...
    t = MACRO_FUNC;
    set_idnum('.', dotid);
}
```

参数存储为 `Sym` 链表，每个参数的 `v` 字段是参数名的 token ID，`type.t` 字段标记是否为可变参数（`is_vaargs`）。

### 3.3.4 可变参数宏 __VA_ARGS__

C99 引入了可变参数宏（variadic macros），语法为 `#define f(a, b, ...)`，其中 `...` 对应 `__VA_ARGS__`。TinyCC 的支持体现在两个地方：

1. **解析阶段**：`TOK_DOTS`（`...`）被识别并映射为 `TOK___VA_ARGS__`。
2. **展开阶段**：在 `macro_subst_tok()` 中，可变参数匹配所有剩余实参（逗号后的所有内容）；在 `macro_arg_subst()` 中，`__VA_ARGS__` 的空值触发前导逗号删除。

### 3.3.5 宏体收集

宏体的收集使用 `TokenString`（一个动态增长的 int 数组）：

```c
parse_flags |= PARSE_FLAG_ACCEPT_STRAYS
             | PARSE_FLAG_SPACES
             | PARSE_FLAG_LINEFEED;
tok_str_new(&str);
t0 = 0;
while (tok != TOK_LINEFEED && tok != TOK_EOF) {
    if (is_space(tok)) {
        str.need_spc |= 1;
    } else {
        if (TOK_TWOSHARPS == tok) {
            if (0 == t0) goto bad_twosharp;
            tok = TOK_PPJOIN;
            t |= MACRO_JOIN;
        }
        tok_str_add2_spc(&str, tok, &tokc);
        t0 = tok;
    }
    next_nomacro();
}
parse_flags = saved_parse_flags;
tok_str_add(&str, 0);    /* 以 0 结尾 */
if (t0 == TOK_PPJOIN)
    tcc_error("'##' cannot appear at either end of macro");
define_push(v, t, str.str, first);
```

关键细节：

- `TOK_TWOSHARPS`（`##`）在宏体中被替换为 `TOK_PPJOIN`，这是内部表示。
- 宏体中遇到 `##` 时，设置 `MACRO_JOIN` 标志到 `t` 中，标记此宏包含拼接操作。
- 宏体以 `0`（NULL terminator）结尾，以 `TOK_EOF` 标记参数引用的结束。
- `##` 不能出现在宏体的开头或结尾（`bad_twosharp` 错误）。

---

## 3.4 宏存储与查找

### 3.4.1 数据结构概览

TinyCC 的宏定义存储涉及三个核心数据结构：

```
table_ident[]          Sym (define_stack)       TokenString
┌──────────────┐       ┌──────────────┐       ┌──────────────┐
│ TokenSym[0]  │       │ v = TOK_ID   │       │ int *str     │
│  sym_define ─┼──┐    │ type.t = ... │       │ len          │
│              │  │    │ d ───────────┼───▶   │ [token list] │
├──────────────┤  │    │ next ────────┼──▶    │ 0 (end)      │
│ TokenSym[1]  │  │    │ (first arg)  │       └──────────────┘
│  sym_define  │  │    └──────────────┘
│              │  │
├──────────────┤  │
│     ...      │  │
└──────────────┘  │
                  │
    define_find(v)┘
```

- **`table_ident[]`**：标识符表，每个 `TokenSym` 有一个 `sym_define` 指针，指向当前生效的宏定义。
- **`define_stack`**：全局链表，按定义顺序存储所有宏定义 `Sym`。支持嵌套作用域（如 `#pragma push_macro`）。
- **`Sym.d`**：指向 `TokenString`（即 `int *`），存储宏体的 token 序列。
- **`Sym.next`**：函数宏的参数链表。

### 3.4.2 define_push()

`define_push()`（`tccpp.c:1248`）将一个新宏定义压入定义栈：

```c
ST_INLN void define_push(int v, int macro_type, int *str, Sym *first_arg)
{
    Sym *s, *o;

    o = define_find(v);                          /* 查找旧定义 */
    s = sym_push2(&define_stack, v, macro_type, 0);  /* 压入新定义 */
    s->d = str;                                  /* 宏体 */
    s->next = first_arg;                         /* 参数链表 */
    table_ident[v - TOK_IDENT]->sym_define = s;  /* 更新查找表 */

    if (o && !macro_is_equal(o->d, s->d))
        tcc_warning("%s redefined", get_tok_str(v, NULL));  /* 警告重定义 */
}
```

注意 `sym_push2()` 使用 `define_stack` 作为栈顶，这意味着宏定义是按嵌套顺序存储的。当遇到 `#undef` 时，宏只是从查找表中移除，但 `define_stack` 链表中仍然保留记录（用于 `#pragma pop_macro` 恢复）。

### 3.4.3 define_find()

`define_find()`（`tccpp.c:1278`）是宏查找的核心函数，极其简洁：

```c
ST_INLN Sym *define_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_define;
}
```

它直接通过 `table_ident[]` 数组索引，O(1) 时间完成查找。`sym_define` 指针指向 `define_stack` 链表中的 `Sym` 节点。

### 3.4.4 define_undef()

`define_undef()`（`tccpp.c:1267`）取消宏定义：

```c
ST_FUNC void define_undef(Sym *s)
{
    int v = s->v;
    if (v >= TOK_IDENT && v < tok_ident)
        table_ident[v - TOK_IDENT]->sym_define = NULL;
}
```

它只是将 `table_ident[]` 中的 `sym_define` 指针置为 NULL，并不释放 `Sym` 节点。这是因为 `Sym` 节点仍在 `define_stack` 链表中，可能被 `#pragma pop_macro` 使用。

### 3.4.5 free_defines()

`free_defines()`（`tccpp.c:1283`）释放从当前栈顶到指定边界 `b` 之间的所有宏定义：

```c
ST_FUNC void free_defines(Sym *b)
{
    while (define_stack != b) {
        Sym *top = define_stack;
        define_stack = top->prev;
        tok_str_free_str(top->d);       /* 释放宏体 */
        define_undef(top);              /* 从查找表移除 */
        sym_free(top);                  /* 释放 Sym 节点 */
    }
}
```

这在文件包含结束时调用，用于清理该文件中定义的所有宏。

---

## 3.5 宏展开三阶段详解

宏展开是预处理器中最复杂的部分。TinyCC 将其分为三个阶段：

1. **`macro_subst_tok()`**：函数宏参数收集与初步替换。
2. **`macro_arg_subst()`**：参数替换、字符串化、拼接。
3. **`macro_subst()`**：递归展开，防止无限递归。

### 3.5.1 Stage 1: macro_subst_tok() — 函数宏参数收集

`macro_subst_tok()`（`tccpp.c:3237`）是宏展开的入口。当 `next()` 或 `macro_subst()` 遇到一个已定义的标识符时，调用此函数。

#### 核心流程

```c
static int macro_subst_tok(
    TokenString *tok_str,
    Sym **nested_list,
    Sym *s)
{
    int t;
    int v = s->v;

    if (s->d) {  /* 有宏体 */
        if (s->type.t & MACRO_FUNC) {
            /* 函数宏：需要收集参数 */
            ...
        }
        /* 对象宏或函数宏参数收集完毕后 */
        /* 处理 ## 拼接 */
        jstr = mstr;
        if (s->type.t & MACRO_JOIN)
            jstr = macro_twosharps(mstr);

        /* 递归展开 */
        sa = sym_push2(nested_list, v, 0, 0);
        ret = macro_subst(tok_str, nested_list, jstr);
        ...
    } else {
        /* 内置宏：__LINE__, __FILE__ 等 */
        ...
    }
}
```

#### 函数宏的 '(' 前瞻

函数宏展开的关键问题是：宏名后面是否紧跟 `(`。TinyCC 使用 `next_argstream()` 进行前瞻：

```c
t = next_argstream(nested_list, &str);
if (t != '(') {
    /* 不是函数调用，恢复原始 token */
    parse_flags = saved_parse_flags;
    tok_str_add2_spc(tok_str, v, 0);
    /* 恢复空白 */
    for (i = 0; i < str.len; i++)
        tok_str_add(tok_str, str.str[i]);
    return 0;
}
```

`next_argstream()` 会沿着宏栈向上查找，直到到达文件层的 `peek_file()`。如果下一个非空白 token 不是 `(`，则此次"展开"退化为普通标识符输出。

#### 参数收集的括号匹配

一旦确认是函数调用，进入参数收集循环：

```c
args = NULL;
sa = s->next;  /* 参数链表头 */
i = 2;         /* 跳过 '(' */
for(;;) {
    do {
        t = next_argstream(nested_list, NULL);
    } while (t == ' ' || --i);

    if (!sa) {
        if (t == ')') break;         /* f() 情况 */
        tcc_error("too many args");
    }
empty_arg:
    tok_str_new(&str);
    parlevel = 0;
    while (parlevel > 0 || (t != ')' && (t != ',' || sa->type.t))) {
        if (t == '(') parlevel++;
        if (t == ')') parlevel--;
        if (t == ' ')
            str.need_spc |= 1;
        else
            tok_str_add2_spc(&str, t, &tokc);
        t = next_argstream(nested_list, NULL);
    }
    tok_str_add(&str, TOK_EOF);
    sa1 = sym_push2(&args, sa->v & ~SYM_FIELD, sa->type.t, 0);
    sa1->d = str.str;
    sa = sa->next;
    if (t == ')') {
        if (!sa) break;
        if (sa->type.t && gnu_ext) goto empty_arg;  /* GNU 扩展：空可变参数 */
        tcc_error("too few args");
    }
    i = 1;
}
```

关键设计点：

- `parlevel` 跟踪括号嵌套深度，确保 `f(a, (b, c))` 正确解析为两个参数。
- `sa->type.t` 非零表示可变参数，逗号不再作为分隔符。
- GNU 扩展允许省略可变参数：`f(a)` 中 `__VA_ARGS__` 为空。

### 3.5.2 Stage 2: macro_arg_subst() — 参数替换

`macro_arg_subst()`（`tccpp.c:2986`）将宏体中的形参替换为实参值。这是 `#` 字符串化和 `##` 前后标记处理的核心场所。

#### 参数替换主循环

```c
static int *macro_arg_subst(Sym **nested_list, const int *macro_str, Sym *args)
{
    TokenString str;
    tok_str_new(&str);
    t0 = t1 = 0;
    while(1) {
        TOK_GET(&t, &macro_str, &cval);
        if (!t) break;

        if (t == '#') {
            /* 字符串化操作 */
            ...
        } else if (t >= TOK_IDENT) {
            s = sym_find2(args, t);
            if (s) {
                /* 检查前后是否有 ## */
                if (t2 == TOK_PPJOIN || t1 == TOK_PPJOIN) {
                    /* 不展开，直接插入原始 token */
                    ...
                } else {
                    /* 正常替换：先展开参数，再插入 */
                    macro_subst(&str2, nested_list, st);
                    ...
                }
            }
        }
    }
}
```

#### # 字符串化

当宏体中出现 `#` 后跟参数名时，执行字符串化操作：

```c
if (t == '#') {
    do t = *macro_str++; while (t == ' ');
    s = sym_find2(args, t);
    if (s) {
        cstr_reset(&tokcstr);
        cstr_ccat(&tokcstr, '\"');
        st = s->d;
        while (*st != TOK_EOF) {
            TOK_GET(&t, &st, &cval);
            /* 将每个 token 的文本拼接 */
            ...
        }
        cstr_ccat(&tokcstr, '\"');
        cstr_ccat(&tokcstr, '\0');
        /* 生成 TOK_PPSTR token */
        tok_str_add2(&str, TOK_PPSTR, &cval);
    }
}
```

字符串化的规则（C11 §6.10.3.2）：
- 参数的每个 token 之间用单个空格分隔。
- 字符串字面量中的 `"` 和 `\` 需要转义。
- 前导和尾随空白被删除。

#### ## 与空 __VA_ARGS__ 的逗号删除

当 `##` 出现在宏体中且 `__VA_ARGS__` 为空时，GNU 扩展会删除前导逗号：

```c
if (t1 == TOK_PPJOIN && t0 == ',' && gnu_ext && s->type.t) {
    int c = str.str[str.len - 1];
    while (str.str[--str.len] != ',')
        ;
    if (*st == TOK_EOF) {
        /* __VA_ARGS__ 为空：删除 ',' 和 '##' */
    } else {
        /* __VA_ARGS__ 非空：删除 '##'，保留变量 */
        str.len++;
        goto add_var;
    }
}
```

例如 `#define dbg(fmt, ...) printf(fmt, ##__VA_ARGS__)` 中，当 `__VA_ARGS__` 为空时，`##` 前的逗号被删除。

### 3.5.3 Stage 3: macro_subst() — 递归展开

`macro_subst()`（`tccpp.c:3408`）是宏展开的最外层循环，负责递归展开并防止无限递归。

```c
static int macro_subst(
    TokenString *tok_str,
    Sym **nested_list,
    const int *macro_str)
{
    Sym *s;
    int t, nosubst = 0;
    CValue cval;

    while (1) {
        TOK_GET(&t, &macro_str, &cval);
        if (t == 0 || t == TOK_EOF) break;

        if (t >= TOK_IDENT) {
            s = define_find(t);
            if (s == NULL || nosubst) goto no_subst;
            /* 嵌套检查：防止无限递归 */
            if (sym_find2(*nested_list, t)) {
                t |= SYM_FIELD;  /* 标记为不展开 */
                goto no_subst;
            }
            str = tok_str_alloc();
            str->str = (int*)macro_str;
            begin_macro(str, 2);
            nosubst = macro_subst_tok(tok_str, nested_list, s);
            ...
        } else {
no_subst:
            tok_str_add2_spc(tok_str, t, &cval);
            if (nosubst && t != '(')
                nosubst = 0;
            if (t == TOK_DEFINED && pp_expr)
                nosubst = 1;
        }
    }
}
```

#### nested_list 自递归防止

`nested_list` 是一个 `Sym` 链表，记录当前展开路径中已经进入过的宏。当 `macro_subst_tok()` 开始展开宏 `X` 时，它将 `X` 压入 `nested_list`：

```c
sa = sym_push2(nested_list, v, 0, 0);
ret = macro_subst(tok_str, nested_list, jstr);
if (sa == *nested_list)
    *nested_list = sa->prev, sym_free(sa);
```

后续在 `macro_subst()` 中遇到 `X` 时，`sym_find2(*nested_list, t)` 返回非 NULL，从而跳过展开。展开完成后，`X` 从 `nested_list` 中弹出。

这确保了 C 标准要求的行为：**宏在其自身的展开过程中不被再次展开**，但在后续的展开中可以被展开。

#### nosubst 标志

`nosubst` 标志用于处理 `#defined` 操作符：在 `#if defined(X)` 中，`X` 不应被展开。当 `macro_subst()` 遇到 `TOK_DEFINED` 且处于 `pp_expr`（预处理表达式）模式时，设置 `nosubst = 1`，使后续标识符不做宏展开。

---

## 3.6 Token 拼接 ##

### 3.6.1 macro_twosharps() 的设计

`macro_twosharps()`（`tccpp.c:3106`）处理宏体中所有 `##` 操作符。它的输入是经过 `macro_arg_subst()` 替换后的 token 序列，输出是拼接后的新 token 序列。

```c
static inline int *macro_twosharps(const int *ptr0)
{
    int t1, t2, n, l;
    CValue cv1, cv2;
    TokenString macro_str1;
    const int *ptr;

    tok_str_new(&macro_str1);
    cstr_reset(&tokcstr);
    for (ptr = ptr0;;) {
        TOK_GET(&t1, &ptr, &cv1);
        if (t1 == 0) break;

        for (;;) {
            n = 0;
            while ((t2 = ptr[n]) == ' ')
                ++n;
            if (t2 != TOK_PPJOIN) break;
            ptr += n;
            while ((t2 = *++ptr) == ' ' || t2 == TOK_PPJOIN)
                ;
            TOK_GET(&t2, &ptr, &cv2);
            if (t2 == TOK_PLCHLDR) continue;
            if (t1 != TOK_PLCHLDR) {
                cstr_cat(&tokcstr, get_tok_str(t1, &cv1), -1);
                t1 = TOK_PLCHLDR;
            }
            cstr_cat(&tokcstr, get_tok_str(t2, &cv2), -1);
        }
        if (tokcstr.size) {
            /* 拼接结果需要重新词法分析 */
            cstr_ccat(&tokcstr, 0);
            tcc_open_bf(tcc_state, ":paste:", tokcstr.size);
            memcpy(file->buffer, tokcstr.data, tokcstr.size);
            tok_flags = 0;
            for (n = 0;; n = l) {
                next_nomacro();
                tok_str_add2(&macro_str1, tok, &tokc);
                if (*file->buf_ptr == 0) break;
                tok_str_add(&macro_str1, ' ');
                l = file->buf_ptr - file->buffer;
                tcc_warning("pasting ... does not give a valid token");
            }
            tcc_close();
            cstr_reset(&tokcstr);
        }
        if (t1 != TOK_PLCHLDR)
            tok_str_add2(&macro_str1, t1, &cv1);
    }
    tok_str_add(&macro_str1, 0);
    return macro_str1.str;
}
```

### 3.6.2 拼接的执行流程

1. **收集连续的 `##` 操作数**：将 `a ## b ## c` 中所有操作数的文本拼接到 `tokcstr` 中。
2. **创建临时文件 `:paste:`**：使用 `tcc_open_bf()` 创建一个虚拟的内存文件，内容为拼接后的字符串。
3. **重新词法分析**：调用 `next_nomacro()` 对拼接结果进行词法分析，生成新的 token。
4. **警告多 token 结果**：如果拼接后产生多个 token，输出警告（如 `pasting "i" and "n" does not give a valid preprocessing token`）。
5. **清理临时文件**：调用 `tcc_close()` 关闭虚拟文件。

### 3.6.3 TOK_PLCHLDR 占位符

`TOK_PLCHLDR`（placeholder）是 `##` 操作中的特殊标记。当参数被 `##` 包围且展开为空时，使用占位符代替：

```c
if (*st == TOK_EOF)
    tok_str_add(&str, TOK_PLCHLDR);
```

在 `macro_twosharps()` 中，占位符被跳过（`if (t2 == TOK_PLCHLDR) continue`），确保拼接只发生在非空操作数之间。

### 3.6.4 拼接示例

考虑 `#define CONCAT(a, b) a ## b` 展开 `CONCAT(hello, world)`：

1. 参数替换后宏体为：`hello ## world`。
2. `macro_twosharps()` 将 `"hello"` 和 `"world"` 拼接为 `"helloworld"`。
3. 创建临时文件 `:paste:`，内容为 `helloworld`。
4. `next_nomacro()` 将其词法分析为单个标识符 token `helloworld`。
5. 输出 `helloworld`。

---

## 3.7 字符串化 #

### 3.7.1 字符串化的实现

字符串化（stringification）在 `macro_arg_subst()` 中实现。当宏体中出现 `#` 后跟参数名时，该参数的值被转换为字符串字面量。

实现逻辑：

1. 检测到 `#` token。
2. 读取下一个 token（跳过空格），在参数链表中查找。
3. 遍历参数的 token 序列，将每个 token 的文本表示拼接。
4. 用双引号包裹，生成 `TOK_PPSTR` token。

```c
if (t == '#') {
    do t = *macro_str++; while (t == ' ');
    s = sym_find2(args, t);
    if (s) {
        cstr_reset(&tokcstr);
        cstr_ccat(&tokcstr, '\"');
        st = s->d;
        while (*st != TOK_EOF) {
            TOK_GET(&t, &st, &cval);
            s = get_tok_str(t, &cval);
            while (*s) {
                if (t == TOK_PPSTR && *s != '\'')
                    add_char(&tokcstr, *s);   /* 转义内部引号 */
                else
                    cstr_ccat(&tokcstr, *s);
                ++s;
            }
        }
        cstr_ccat(&tokcstr, '\"');
        cstr_ccat(&tokcstr, '\0');
        cval.str.size = tokcstr.size;
        cval.str.data = tokcstr.data;
        tok_str_add2(&str, TOK_PPSTR, &cval);
    }
}
```

### 3.7.2 字符串化规则

C 标准（C11 §6.10.3.2）规定的字符串化规则：

1. 参数中每个 pp-token 之间插入一个空格。
2. 字符串字面量中的 `\` 和 `"` 需要额外转义（`\\"` 和 `\\"`）。
3. 前导和尾随空白被删除。
4. 空参数展开为空字符串 `""`。

TinyCC 的实现中，`add_char()` 函数处理特殊字符的转义，`get_tok_str()` 返回 token 的标准文本表示。

### 3.7.3 字符串化示例

```c
#define STR(x) #x
STR(hello world)    /* 展开为 "hello world" */
STR("hello")        /* 展开为 "\"hello\"" */
STR()               /* 展开为 "" */
```

---

## 3.8 条件编译

### 3.8.1 条件编译指令族

TinyCC 支持完整的 C 标准条件编译指令族：

| 指令 | 功能 | 关键函数 |
|------|------|----------|
| `#ifdef MACRO` | 测试宏是否已定义 | `define_find()` |
| `#ifndef MACRO` | 测试宏是否未定义 | `define_find()` |
| `#if expr` | 常量表达式求值 | `expr_preprocess()` |
| `#elif expr` | else-if 分支 | `expr_preprocess()` |
| `#else` | else 分支 | — |
| `#endif` | 结束条件块 | — |

### 3.8.2 ifdef_stack

条件编译的嵌套状态通过 `ifdef_stack` 管理：

```c
/* TCCState 中的定义 */
int ifdef_stack[IFDEF_STACK_SIZE];    /* 最大 64 层嵌套 */
int *ifdef_stack_ptr;                  /* 当前栈顶指针 */
```

每个条件块在栈中占一个 int 值，编码如下：

- bit 0：当前条件是否为真（0 = 假，1 = 真）。
- bit 1：是否已经进入过某个分支（用于检测 `#else` 之后的重复 `#else`）。

### 3.8.3 #if 的处理

`#if` 指令的处理流程：

```c
case TOK_IF:
    c = expr_preprocess(s1);   /* 求值条件表达式 */
    goto do_if;

do_if:
    if (s1->ifdef_stack_ptr >= s1->ifdef_stack + IFDEF_STACK_SIZE)
        tcc_error("memory full (ifdef)");
    *s1->ifdef_stack_ptr++ = c;
    goto test_skip;

test_skip:
    if (!(c & 1)) {            /* 条件为假 */
        skip_to_eol(1);
        preprocess_skip();     /* 跳过整个 false 分支 */
        is_bof = 0;
        goto redo;             /* 重新处理 #else/#elif/#endif */
    }
    break;
```

### 3.8.4 expr_preprocess() 与 defined()

`expr_preprocess()`（`tccpp.c:1435`）负责将 `#if` 后的预处理表达式求值为整数常量。

```c
static int expr_preprocess(TCCState *s1)
{
    TokenString *str;
    str = tok_str_alloc();
    pp_expr = 1;

    while (1) {
        next();  /* 带宏展开 */
        if (tok == TOK_DEFINED) {
            /* defined(MACRO) 或 defined MACRO */
            parse_flags &= ~PARSE_FLAG_PREPROCESS;  /* 临时禁止宏展开 */
            next();
            t = tok;
            if (t == '(') next();
            parse_flags |= PARSE_FLAG_PREPROCESS;
            c = define_find(tok) ? 1 : 0;
            if (t == '(') { next(); /* ')' */ }
            tok = TOK_CLLONG; tokc.i = c;
        } else if (tok == TOK___HAS_INCLUDE ||
                   tok == TOK___HAS_INCLUDE_NEXT) {
            /* __has_include() 支持 */
            ...
        } else if (tok >= TOK_IDENT) {
            /* 未定义宏替换为 0 */
            c = 0;
            tok = TOK_CLLONG; tokc.i = c;
        }
        tok_str_add_tok(str);
    }

    /* 使用 expr_const() 对 token 流求值 */
    begin_macro(str, 1);
    next();
    c = expr_const();
    ...
    return c != 0;
}
```

关键设计点：

- `defined` 操作符需要**临时禁止宏展开**，因为 `defined(X)` 中的 `X` 不应被展开。
- 未定义的标识符被替换为 `0`（C 标准要求）。
- `__has_include()` 是 C23 特性，TinyCC 已提前支持。

### 3.8.5 preprocess_skip() — 跳过 false 分支

当条件为假时，需要跳过整个分支直到匹配的 `#else`、`#elif` 或 `#endif`。`preprocess_skip()`（`tccpp.c:874`）直接在字符级别扫描：

```c
static void preprocess_skip(void)
{
    int a, start_of_line, c;
    uint8_t *p;

    p = file->buf_ptr;
    a = 0;
redo_start:
    start_of_line = 1;
    for(;;) {
        c = *p;
        switch(c) {
        case '\n':    /* 换行 */
            file->line_num++;
            p++;
            goto redo_start;
        case '\\':    /* 行接续 */
            ...
        case '\"':    /* 跳过字符串 */
        case '\'':
            p = parse_pp_string(p, c, NULL);
            break;
        case '/':     /* 跳过注释 */
            ...
        case '#':
            p++;
            if (start_of_line) {
                file->buf_ptr = p;
                next_nomacro();
                if (a == 0 &&
                    (tok == TOK_ELSE || tok == TOK_ELIF || tok == TOK_ENDIF))
                    goto the_end;
                if (tok == TOK_IF || tok == TOK_IFDEF || tok == TOK_IFNDEF)
                    a++;           /* 嵌套 #if */
                else if (tok == TOK_ENDIF)
                    a--;           /* 匹配的 #endif */
            }
            break;
        default:
            p++;
            break;
        }
        start_of_line = 0;
    }
the_end:
    file->buf_ptr = p;
}
```

注意 `a` 变量跟踪嵌套的 `#if`/`#endif` 对。只有当 `a == 0` 时遇到的 `#else`/`#elif`/`#endif` 才是当前层的。

### 3.8.6 #else 与 #elif

```c
case TOK_ELSE:
    next_nomacro();
    if (s1->ifdef_stack_ptr[-1] & 2)
        tcc_error("#else after #else");
    c = (s1->ifdef_stack_ptr[-1] ^= 3);  /* 切换 bit 0，设置 bit 1 */
    goto test_else;

case TOK_ELIF:
    c = s1->ifdef_stack_ptr[-1];
    if (c > 1)
        tcc_error("#elif after #else");
    if (c == 1) {           /* 前面的分支已为真 */
        skip_to_eol(0);
        c = 0;              /* 跳过此 #elif */
    } else {
        c = expr_preprocess(s1);
        s1->ifdef_stack_ptr[-1] = c;
    }
```

`^= 3` 的位操作巧妙地实现了：如果当前 bit 0 为 1（前面的分支为真），则翻转为 0（当前分支为假）；同时设置 bit 1（已进入过分支）。

---

## 3.9 文件包含 #include

### 3.9.1 parse_include() 总体流程

`parse_include()`（`tccpp.c:1314`）处理 `#include` 和 `#include_next` 指令。

```c
static int parse_include(TCCState *s1, int do_next, int test)
{
    int c, i;
    char name[1024], buf[1024], *p;
    CachedInclude *e;

    /* 1. 解析文件名 */
    c = skip_spaces();
    if (c == '<' || c == '\"') {
        /* 标准形式：#include <file.h> 或 #include "file.h" */
        ...
    } else {
        /* 计算形式：#include MACRO_EXPANDED */
        parse_flags = PARSE_FLAG_PREPROCESS | ...;
        for (;;) {
            next();  /* 带宏展开 */
            ...
        }
    }

    /* 2. 搜索文件 */
    i = do_next ? file->include_next_index : -1;
    for (;;) {
        ++i;
        if (i == 0) {
            /* 绝对路径 */
            if (!IS_ABSPATH(name)) continue;
        } else if (i == 1) {
            /* "file.h" 形式：先搜索当前文件目录 */
            if (c != '\"') continue;
            ...
        } else {
            /* 搜索 include_paths[] 和 sysinclude_paths[] */
            ...
        }

        /* 3. 检查缓存 */
        e = search_cached_include(s1, buf, 0);
        if (e && (define_find(e->ifndef_macro) || e->once))
            return 1;  /* 已包含，跳过 */

        /* 4. 打开文件 */
        if (tcc_open(s1, buf) >= 0) break;
    }

    /* 5. 压入 include_stack */
    if (s1->include_stack_ptr >= s1->include_stack + INCLUDE_STACK_SIZE)
        tcc_error("#include recursion too deep");
    *s1->include_stack_ptr++ = file->prev;
    ...
}
```

### 3.9.2 搜索顺序

`#include <file.h>` 和 `#include "file.h"` 的搜索顺序不同：

**`#include <file.h>`**：
1. 绝对路径（如果 `name` 以 `/` 开头）。
2. 系统包含路径（`-isystem` 指定）。
3. 标准包含路径（`-I` 指定）。

**`#include "file.h"`**：
1. 绝对路径。
2. 当前文件所在目录。
3. 系统包含路径。
4. 标准包含路径。

**`#include_next`**（GNU 扩展）：
从当前文件的下一个搜索路径开始搜索，用于实现"覆盖式"头文件。

### 3.9.3 CachedInclude 优化

TinyCC 实现了 GCC 风格的 include guard 优化。当文件以 `#ifndef MACRO` 开头、以 `#endif` 结尾时，如果 `MACRO` 已定义，则跳过整个文件。

数据结构：

```c
typedef struct CachedInclude {
    int ifndef_macro;     /* #ifndef 中的宏 token ID */
    int once;             /* #pragma once 标志 */
    int hash_next;
    char filename[1];
} CachedInclude;
```

检测机制（在 `preprocess()` 中）：

1. 遇到 `#ifndef MACRO` 且 `is_bof` 为真时，记录 `file->ifndef_macro = tok`。
2. 遇到匹配的 `#endif` 且 `ifdef_stack_ptr` 回到文件起始位置时，记录 `file->ifndef_macro_saved`。
3. 下次包含同一文件时，检查 `ifndef_macro` 对应的宏是否已定义。

```c
case TOK_ENDIF:
    ...
    if (file->ifndef_macro &&
        s1->ifdef_stack_ptr == file->ifdef_stack_ptr) {
        file->ifndef_macro_saved = file->ifndef_macro;
        file->ifndef_macro = 0;
        tok_flags |= TOK_FLAG_ENDIF;
    }
    break;
```

### 3.9.4 include_stack 管理

```c
/* TCCState 中的定义 */
BufferedFile *include_stack[INCLUDE_STACK_SIZE];  /* 最大 32 层 */
BufferedFile **include_stack_ptr;
```

每次 `#include` 成功打开文件后，将当前文件压入栈：

```c
*s1->include_stack_ptr++ = file->prev;
```

当被包含的文件读取完毕（遇到 EOF）时，`tcc_close()` 关闭文件并恢复 `file` 指针。

### 3.9.5 #pragma once

`#pragma once` 的实现极其简洁：

```c
} else if (tok == TOK_once) {
    search_cached_include(s1, file->true_filename, 1)->once = 1;
}
```

它在 `CachedInclude` 中设置 `once` 标志。下次尝试包含同一文件时，`parse_include()` 检查此标志并跳过。

---

## 3.10 #pragma 指令

### 3.10.1 pragma_parse() 总览

`pragma_parse()`（`tccpp.c:1649`）处理所有 `#pragma` 指令。TinyCC 支持以下 pragma：

| pragma | 功能 |
|--------|------|
| `#pragma once` | 防止重复包含 |
| `#pragma pack(N)` | 设置结构体对齐 |
| `#pragma pack(push)` | 压入对齐设置 |
| `#pragma pack(pop)` | 弹出对齐设置 |
| `#pragma push_macro("M")` | 保存宏定义 |
| `#pragma pop_macro("M")` | 恢复宏定义 |
| `#pragma comment(lib, "name")` | 链接库（Windows） |
| `#pragma comment(option, "opts")` | 编译选项 |

### 3.10.2 #pragma pack

`#pragma pack` 控制结构体成员的对齐方式。TinyCC 使用一个栈（`pack_stack`）来管理嵌套的 pack 设置：

```c
} else if (tok == TOK_pack) {
    next();
    skip('(');
    if (tok == TOK_ASM_pop) {
        next();
        if (s1->pack_stack_ptr <= s1->pack_stack)
            tcc_error("out of pack stack");
        s1->pack_stack_ptr--;
    } else {
        int val = 0;
        if (tok != ')') {
            if (tok == TOK_ASM_push) {
                next();
                if (s1->pack_stack_ptr >= s1->pack_stack + PACK_STACK_SIZE - 1)
                    tcc_error("out of pack stack");
                val = *s1->pack_stack_ptr++;
                if (tok != ',') goto pack_set;
                next();
            }
            if (tok != TOK_CINT) goto pragma_err;
            val = tokc.i;
            if (val < 1 || val > 16 || (val & (val - 1)) != 0)
                goto pragma_err;
            next();
        }
    pack_set:
        *s1->pack_stack_ptr = val;
    }
}
```

pack 栈的设计：

```c
/* TCCState 中 */
int pack_stack[PACK_STACK_SIZE];
int *pack_stack_ptr;
```

- `pack(1)`：设置对齐为 1 字节。
- `pack()`：重置为默认对齐。
- `pack(push)`：压入当前值。
- `pack(push, N)`：压入当前值并设置为 N。
- `pack(pop)`：恢复上一个值。

对齐值必须是 1 到 16 之间的 2 的幂。

### 3.10.3 #pragma push_macro / pop_macro

`push_macro` 和 `pop_macro` 允许临时修改宏定义并恢复：

```c
if (tok == TOK_push_macro || tok == TOK_pop_macro) {
    int t = tok, v;
    Sym *s;

    if (next(), tok != '(') goto pragma_err;
    if (next(), tok != TOK_STR) goto pragma_err;
    v = tok_alloc(tokc.str.data, tokc.str.size - 1)->tok;
    if (next(), tok != ')') goto pragma_err;

    if (t == TOK_push_macro) {
        while (NULL == (s = define_find(v)))
            define_push(v, MACRO_OBJ, NULL, NULL);  /* 压入空定义 */
        s->type.ref = s;    /* 标记 push 边界 */
    } else {
        /* pop_macro: 恢复到 push 边界 */
        for (s = define_stack; s; s = s->prev)
            if (s->v == v && s->type.ref == s) {
                s->type.ref = NULL;
                break;
            }
        if (s)
            table_ident[v - TOK_IDENT]->sym_define = s->d ? s : NULL;
    }
}
```

`type.ref` 字段用作 push/pop 的边界标记。`push_macro` 时设置 `type.ref = s`，`pop_macro` 时查找该边界并恢复 `table_ident` 中的指针。

### 3.10.4 #pragma comment

`#pragma comment(lib, "name")` 将库名添加到链接列表：

```c
} else if (tok == TOK_comment) {
    char *p; int t;
    next(); skip('(');
    t = tok;
    next(); skip(',');
    if (tok != TOK_STR) goto pragma_err;
    p = tcc_strdup(tokc.str.data);
    next();
    if (tok != ')') goto pragma_err;
    if (t == TOK_lib) {
        dynarray_add(&s1->pragma_libs, &s1->nb_pragma_libs, p);
    } else if (t == TOK_option) {
        tcc_set_options(s1, p);
        tcc_free(p);
    }
}
```

---

## 3.11 内置宏

### 3.11.1 预定义宏的注册

在 `tccpp_new()` 中，TinyCC 注册了五个特殊宏作为"虚拟定义"：

```c
ST_FUNC void tccpp_new(TCCState *s)
{
    ...
    define_push(TOK___LINE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___FILE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___DATE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___TIME__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___COUNTER__, MACRO_OBJ, NULL, NULL);
}
```

注意这些定义的 `d` 字段为 `NULL`——它们没有宏体。展开逻辑在 `macro_subst_tok()` 的 `else` 分支中特殊处理。

### 3.11.2 内置宏的展开

```c
} else {
    CValue cval;
    char buf[32], *cstrval = buf;

    if (v == TOK___LINE__ || v == TOK___COUNTER__) {
        t = v == TOK___LINE__ ? file->line_num : pp_counter++;
        snprintf(buf, sizeof(buf), "%d", t);
        t = TOK_PPNUM;
        goto add_cstr1;

    } else if (v == TOK___FILE__) {
        cstrval = file->filename;
        goto add_cstr;

    } else if (v == TOK___DATE__ || v == TOK___TIME__) {
        time_t ti;
        struct tm *tm;
        time(&ti);
        tm = localtime(&ti);
        if (v == TOK___DATE__) {
            static char const ab_month_name[12][4] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
            };
            snprintf(buf, sizeof(buf), "%s %2d %d",
                ab_month_name[tm->tm_mon], tm->tm_mday,
                tm->tm_year + 1900);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                tm->tm_hour, tm->tm_min, tm->tm_sec);
        }
    add_cstr:
        t = TOK_STR;
    add_cstr1:
        cval.str.size = strlen(cstrval) + 1;
        cval.str.data = cstrval;
        tok_str_add2_spc(tok_str, t, &cval);
    }
    return 0;
}
```

### 3.11.3 内置宏一览

| 宏 | 展开值 | 类型 | 说明 |
|----|--------|------|------|
| `__LINE__` | `file->line_num` | `TOK_PPNUM` | 当前行号 |
| `__FILE__` | `file->filename` | `TOK_STR` | 当前文件名 |
| `__DATE__` | `"Jun 10 2026"` | `TOK_STR` | 编译日期 |
| `__TIME__` | `"14:30:00"` | `TOK_STR` | 编译时间 |
| `__COUNTER__` | `0, 1, 2, ...` | `TOK_PPNUM` | 每次展开递增 |
| `__STDC__` | `1` | — | 在 `tcc_predefs()` 中定义 |
| `__STDC_VERSION__` | `201112L` 等 | — | C 标准版本 |
| `__STDC_HOSTED__` | `0` 或 `1` | — | 是否为 hosted 实现 |
| `__TINYC__` | `9xx` | — | TinyCC 版本号 |
| `__SIZEOF_POINTER__` | `4` 或 `8` | — | 指针大小 |
| `__SIZEOF_LONG__` | `4` 或 `8` | — | long 大小 |

### 3.11.4 平台相关宏

在 `tcc_predefs()` 中，TinyCC 根据目标平台定义一组宏：

```c
static const char * const target_os_defs =
#ifdef TCC_TARGET_PE
    "_WIN32\0"
# if PTR_SIZE == 8
    "_WIN64\0"
# endif
#else
# if defined TCC_TARGET_MACHO
    "__APPLE__\0"
# elif TARGETOS_FreeBSD
    "__FreeBSD__ 12\0"
# else
    "__linux__\0"
    "__linux\0"
# endif
    "__unix__\0"
    "__unix\0"
#endif
;
```

这些宏通过 `putdef()` 转换为 `#define` 字符串，注入到预处理输入流的开头。

---

## 3.12 -E 模式

### 3.12.1 tcc_preprocess() 函数

`tcc_preprocess()`（`tccpp.c:3891`）实现了 `tcc -E` 命令行选项，将预处理后的 token 流输出到标准输出（或指定文件）。

```c
ST_FUNC int tcc_preprocess(TCCState *s1)
{
    BufferedFile **iptr;
    int token_seen, spcs, level;
    const char *p;
    char white[400];

    parse_flags = PARSE_FLAG_PREPROCESS
                | (parse_flags & PARSE_FLAG_ASM_FILE)
                | PARSE_FLAG_LINEFEED
                | PARSE_FLAG_SPACES
                | PARSE_FLAG_ACCEPT_STRAYS;

    token_seen = TOK_LINEFEED, spcs = 0, level = 0;
    if (file->prev)
        pp_line(s1, file->prev, level++);
    pp_line(s1, file, level);

    for (;;) {
        iptr = s1->include_stack_ptr;
        next();
        if (tok == TOK_EOF) break;

        /* 输出 #line 指令（如需要） */
        level = s1->include_stack_ptr - iptr;
        if (level) {
            if (level > 0) pp_line(s1, *iptr, 0);
            pp_line(s1, file, level);
        }

        /* 调试输出 */
        if (s1->dflag & 7) {
            pp_debug_defines(s1);
            if (s1->dflag & 4) continue;
        }

        /* 处理空白和换行 */
        if (is_space(tok)) {
            if (spcs < sizeof white - 1) white[spcs++] = tok;
            continue;
        } else if (tok == TOK_LINEFEED) {
            spcs = 0;
            if (token_seen == TOK_LINEFEED) continue;
            ++file->line_ref;
        } else if (token_seen == TOK_LINEFEED) {
            pp_line(s1, file, 0);
        } else if (spcs == 0 && pp_need_space(token_seen, tok)) {
            white[spcs++] = ' ';
        }

        /* 输出 token */
        white[spcs] = 0;
        fputs(white, s1->ppfp), spcs = 0;
        fputs(p = get_tok_str(tok, &tokc), s1->ppfp);
        token_seen = pp_check_he0xE(tok, p);
    }
    return 0;
}
```

### 3.12.2 输出格式控制

`-E` 模式支持多种输出格式标志：

- `-P`：不输出 `#line` 指令。
- `-P1`：输出 `#line` 而非 `#`。
- `-dD`：输出宏定义信息。
- `-dM`：只输出宏定义（不处理源文件）。

`pp_line()` 函数根据 `s1->Pflag` 的值选择输出格式：

```c
static void pp_line(TCCState *s1, BufferedFile *f, int level)
{
    int d = f->line_num - f->line_ref;
    if (s1->Pflag == LINE_MACRO_OUTPUT_FORMAT_NONE) {
        ;   /* 不输出 */
    } else if (level == 0 && f->line_ref && d < 8) {
        while (d > 0)
            fputs("\n", s1->ppfp), --d;   /* 用空行代替 */
    } else if (s1->Pflag == LINE_MACRO_OUTPUT_FORMAT_STD) {
        fprintf(s1->ppfp, "#line %d \"%s\"\n", f->line_num, f->filename);
    } else {
        fprintf(s1->ppfp, "# %d \"%s\"%s\n", f->line_num, f->filename,
            level > 0 ? " 1" : level < 0 ? " 2" : "");
    }
    f->line_ref = f->line_num;
}
```

### 3.12.3 pp_need_space() — 空白插入

`pp_need_space()` 决定两个相邻 token 之间是否需要插入空格，以避免产生新的合并 token：

```c
static int pp_need_space(int a, int b)
{
    return 'E' == a ? '+' == b || '-' == b
        : '+' == a ? TOK_INC == b || '+' == b
        : '-' == a ? TOK_DEC == b || '-' == b
        : a >= TOK_IDENT || a == TOK_PPNUM ? b >= TOK_IDENT || b == TOK_PPNUM
        : 0;
}
```

例如：`1e` 后跟 `+` 需要空格（否则变成 `1e+`），`a` 后跟 `b` 需要空格（否则变成 `ab`）。

---

## 3.13 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 预处理器的完整实现。核心要点：

1. **架构选择**：TinyCC 将预处理器集成在词法分析器 `tccpp.c` 中，而不是独立模块。
2. **两级词法分析**：`next_nomacro()` 不展开宏，`next()` 展开宏。预处理器指令通过 `preprocess()` 分发。
3. **宏存储**：`table_ident[]` 提供 O(1) 查找，`define_stack` 链表支持作用域嵌套。
4. **宏展开三阶段**：`macro_subst_tok()`（参数收集）→ `macro_arg_subst()`（参数替换）→ `macro_subst()`（递归展开）。
5. **## 拼接**：创建虚拟文件 `:paste:`，重新词法分析。
6. **条件编译**：`ifdef_stack` 管理嵌套状态，`preprocess_skip()` 在字符级别跳过 false 分支。
7. **文件包含**：`CachedInclude` 优化 include guard 模式。
8. **内置宏**：`__LINE__` 等特殊宏在 `macro_subst_tok()` 中直接生成值，不经过宏体替换。

### 练习

1. **宏展开追踪**（见 `exercises/ex1_expand.md`）：手动追踪复杂宏的展开过程。
2. **文件包含搜索**（见 `exercises/ex2_include.md`）：分析 `#include` 的搜索路径。
3. **pragma pack 实验**（见 `exercises/ex3_pragma.md`）：观察不同对齐设置对结构体布局的影响。

---

## 参考文献

1. ISO/IEC 9899:2011 (C11), §5.1.1.2 Translation phases, §6.10 Preprocessing directives.
2. ISO/IEC 9899:2018 (C18), same sections (no changes to preprocessing).
3. GCC Manual, Chapter 10: C Preprocessor Internals.
4. TinyCC source code: `tccpp.c`, `tcc.h`, `tcctok.h`.


===== FILE: docs/ch04/examples/scope_demo.c =====
/*
 * scope_demo.c - Demonstrates scope, shadowing, and symbol table behavior
 *
 * This file illustrates how tcc's symbol table (Sym, sym_push, sym_pop,
 * sym_link, prev_tok) manages names across nested scopes. Compile with:
 *   tcc -c scope_demo.c
 * Use with Chapter 4, Exercise 3 to trace symbol table changes.
 */

/* ============================================================
 * Section 1: Global scope basics
 * ============================================================ */

int global_x = 10;              /* pushed to global_stack */
int global_y = 20;              /* pushed to global_stack */
static int global_z = 30;       /* pushed to global_stack, VT_STATIC */

/* ============================================================
 * Section 2: File-scope struct and typedef
 * ============================================================ */

struct point {
    int x;
    int y;
};

typedef int my_int;

/* ============================================================
 * Section 3: Function definition with parameter scope
 * ============================================================ */

int compute(int a, int b)
{
    /* At this point:
     *   local_stack: [sentinel] -> [param b] -> [param a]
     *   table_ident['a']->sym_identifier = param a (scope 1)
     *   table_ident['b']->sym_identifier = param b (scope 1)
     *   global_x, global_y still in global_stack
     */
    int result;                  /* pushed to local_stack, scope 1 */

    result = a + b;
    return result;
}

/* ============================================================
 * Section 4: Name shadowing with blocks
 * ============================================================ */

int shadow_demo(int x)
{
    /* Scope 1: parameter x is visible */
    int y = x * 2;              /* y pushed to local_stack, scope 1 */

    {
        /* Scope 2: new_scope() */
        int x = 100;           /* SHADOWS parameter x!
         * sym_push creates new Sym for 'x' at scope 2
         * sym_link(new_x, 1) sets:
         *   table_ident['x']->sym_identifier = new_x
         *   new_x->prev_tok = old_x (the parameter)
         */

        int z = x + y;         /* 'x' resolves to scope-2 x (value 100) */

        /* At this point:
         *   table_ident['x']->sym_identifier chain:
         *     scope-2 x -> scope-1 x (parameter) -> global (if any)
         */

        {
            /* Scope 3: nested new_scope() */
            int x = 999;       /* SHADOWS scope-2 x!
             * table_ident['x']->sym_identifier = scope-3 x
             * scope-3 x->prev_tok = scope-2 x
             */

            /* 'x' resolves to scope-3 x (value 999) */
            y = x + z;         /* y = 999 + 101 = 1100 */
        }
        /* prev_scope() for scope 3:
         * sym_pop pops scope-3 x
         * sym_link(scope-3 x, 0) restores:
         *   table_ident['x']->sym_identifier = scope-2 x
         */

        /* 'x' resolves to scope-2 x again (value 100) */
        y = y + x;             /* y = 1100 + 100 = 1200 */
    }
    /* prev_scope() for scope 2:
     * sym_pop pops scope-2 x and scope-2 z
     * sym_link(scope-2 x, 0) restores:
     *   table_ident['x']->sym_identifier = parameter x
     */

    /* 'x' resolves to parameter x again */
    return x + y;               /* x + 1200 */
}

/* ============================================================
 * Section 5: Local types (struct/enum in block scope)
 * ============================================================ */

int local_type_demo(int flag)
{
    int val = 0;

    if (flag) {
        /* new_scope_s() — simplified scope for if/while/switch */
        struct inner {
            int data;
        };

        struct inner s;        /* local variable using local struct */
        s.data = 42;
        val = s.data;
    }
    /* prev_scope_s() pops local symbols but not types in newer tcc versions */

    /* Note: struct inner is NOT visible here in C99+ strict mode.
     * tcc may or may not enforce this depending on version. */

    return val;
}

/* ============================================================
 * Section 6: for-loop scope (C99 declarations)
 * ============================================================ */

int for_scope_demo(void)
{
    int sum = 0;

    for (int i = 0; i < 10; i++) {
        /* new_scope() for the entire for statement
         * 'i' is pushed at the for-init scope
         */
        sum += i;
    }
    /* prev_scope() pops 'i'
     * table_ident['i']->sym_identifier is restored
     * 'i' is no longer accessible here
     */

    /* for (int i = 0; i < 5; i++) { }  -- OK, 'i' is in a new scope */

    return sum;
}

/* ============================================================
 * Section 7: Typedef shadowing
 * ============================================================ */

typedef int my_type;

int typedef_shadow_demo(void)
{
    my_type a = 10;             /* my_type resolves to global typedef (int) */

    {
        /* Scope 2 */
        typedef double my_type; /* SHADOWS global my_type!
         * sym_push creates new Sym for 'my_type' with VT_TYPEDEF|VT_DOUBLE
         * table_ident['my_type']->sym_identifier = new typedef
         * new->prev_tok = global my_type
         */

        my_type b = 3.14;      /* my_type resolves to double */
        (void)b;
    }
    /* prev_scope() restores global my_type */

    my_type c = 20;            /* my_type resolves to int again */
    return a + c;
}

/* ============================================================
 * Section 8: Enum constants in scope
 * ============================================================ */

enum status { OK = 0, ERROR = -1 };

int enum_scope_demo(int mode)
{
    /* OK and ERROR are visible here (scope 0/global) */
    int result = OK;

    {
        enum status { PENDING = 1, DONE = 2 };
        /* PENDING and DONE are pushed at scope 1
         * Note: 'status' tag may or may not be visible depending on tcc behavior
         */
        result = PENDING;
    }
    /* PENDING is no longer visible */

    if (mode) {
        result = ERROR;         /* ERROR is still visible (global enum) */
    }

    return result;
}

/* ============================================================
 * Section 9: Label scope (__label__ extension)
 * ============================================================ */

int label_scope_demo(int x)
{
    __label__ done;             /* local label declaration (GCC extension) */

    if (x > 0) {
        x = 1;
        goto done;
    }
    x = 0;

done:
    return x;
}

/* ============================================================
 * Section 10: Complex shadowing scenario
 * ============================================================ */

int outer = 1;

int complex_shadow(void)
{
    int result;

    {
        int outer = 2;         /* shadows global 'outer' */
        {
            int outer = 3;     /* shadows scope-2 'outer' */
            {
                int outer = 4; /* shadows scope-3 'outer' */
                result = outer;/* result = 4 */
            }
            /* restore scope-3 outer */
            result += outer;   /* result = 4 + 3 = 7 */
        }
        /* restore scope-2 outer */
        result += outer;       /* result = 7 + 2 = 9 */
    }
    /* restore global outer */
    result += outer;           /* result = 9 + 1 = 10 */

    return result;             /* 10 */
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void)
{
    int r;

    r = compute(3, 4);
    r = shadow_demo(5);
    r = local_type_demo(1);
    r = for_scope_demo();
    r = typedef_shadow_demo();
    r = enum_scope_demo(0);
    r = label_scope_demo(1);
    r = complex_shadow();

    (void)r;
    return 0;
}


===== FILE: docs/ch04/examples/types_demo.c =====
/*
 * types_demo.c - Demonstrates all C type constructs
 *
 * This file covers every major type construct that tcc's parser
 * and type system must handle. Compile with:
 *   tcc -c types_demo.c
 * Use with Chapter 4 exercises to trace how tcc encodes each type.
 */

/* ============================================================
 * Section 1: Basic integer types
 * ============================================================ */

char                a1;         /* VT_BYTE (signed by default on most targets) */
signed char         a2;         /* VT_BYTE | VT_DEFSIGN */
unsigned char       a3;         /* VT_BYTE | VT_UNSIGNED | VT_DEFSIGN */

short               b1;         /* VT_SHORT */
signed short        b2;         /* VT_SHORT | VT_DEFSIGN */
unsigned short      b3;         /* VT_SHORT | VT_UNSIGNED | VT_DEFSIGN */
short int           b4;         /* VT_SHORT | VT_INT (normalized to VT_SHORT) */

int                 c1;         /* VT_INT */
signed int          c2;         /* VT_INT | VT_DEFSIGN */
unsigned int        c3;         /* VT_INT | VT_UNSIGNED | VT_DEFSIGN */

long                d1;         /* VT_INT|VT_LONG (32-bit) or VT_LLONG|VT_LONG (64-bit) */
long int            d2;         /* same as long */
unsigned long       d3;         /* VT_INT|VT_LONG|VT_UNSIGNED or VT_LLONG|VT_LONG|VT_UNSIGNED */

long long           e1;         /* VT_LLONG | VT_LONG */
long long int       e2;         /* same */
unsigned long long  e3;         /* VT_LLONG | VT_LONG | VT_UNSIGNED */

/* ============================================================
 * Section 2: Floating-point types
 * ============================================================ */

float               f1;         /* VT_FLOAT */
double              f2;         /* VT_DOUBLE */
long double         f3;         /* VT_LDOUBLE (encoded directly, not VT_LONG|VT_DOUBLE) */

/* ============================================================
 * Section 3: Boolean and void
 * ============================================================ */

_Bool               g1;         /* VT_BOOL */
/* void cannot be used as a variable type */

/* ============================================================
 * Section 4: Pointer types
 * ============================================================ */

int                *h1;         /* VT_PTR -> Sym{type.t=VT_INT} */
char               *h2;         /* VT_PTR -> Sym{type.t=VT_BYTE} */
void               *h3;         /* VT_PTR -> Sym{type.t=VT_VOID} */
double             *h4;         /* VT_PTR -> Sym{type.t=VT_DOUBLE} */
int               **h5;         /* VT_PTR -> Sym{type.t=VT_PTR -> Sym{type.t=VT_INT}} */
const int          *h6;         /* VT_PTR -> Sym{type.t=VT_INT|VT_CONSTANT} */
int *const          h7;         /* VT_PTR|VT_CONSTANT -> Sym{type.t=VT_INT} */
volatile int       *h8;         /* VT_PTR -> Sym{type.t=VT_INT|VT_VOLATILE} */

/* ============================================================
 * Section 5: Array types
 * ============================================================ */

int                 i1[10];     /* VT_ARRAY|VT_PTR -> Sym{c=10, type.t=VT_INT} */
char                i2[100];    /* VT_ARRAY|VT_PTR -> Sym{c=100, type.t=VT_BYTE} */
int                 i3[3][5];   /* VT_ARRAY|VT_PTR -> Sym{c=3,
                                     type.t=VT_ARRAY|VT_PTR -> Sym{c=5, type.t=VT_INT}} */
const char         *i4[4];     /* VT_ARRAY|VT_PTR -> Sym{c=4,
                                     type.t=VT_PTR -> Sym{type.t=VT_BYTE|VT_CONSTANT}} */

/* Incomplete array (extern or with initializer) */
extern int          i5[];       /* VT_ARRAY|VT_PTR -> Sym{c=-1, type.t=VT_INT} */

/* ============================================================
 * Section 6: Function types
 * ============================================================ */

/* Simple function declaration */
int foo(int x, double y);       /* VT_FUNC -> Sym{type.t=VT_INT, f.func_type=FUNC_NEW,
                                     next -> Sym{v='x', type.t=VT_INT},
                                     next -> Sym{v='y', type.t=VT_DOUBLE}} */

/* Variadic function */
int bar(int count, ...);        /* VT_FUNC, f.func_type=FUNC_ELLIPSIS */

/* Old-style function (K&R) */
int baz();                      /* VT_FUNC, f.func_type=FUNC_OLD */

/* Function returning pointer */
int *ret_ptr(int n);            /* VT_FUNC -> Sym{type.t=VT_PTR -> Sym{type.t=VT_INT}} */

/* Function returning void */
void do_nothing(void);          /* VT_FUNC -> Sym{type.t=VT_VOID} */

/* ============================================================
 * Section 7: Function pointer types
 * ============================================================ */

/* Pointer to function taking int, returning int */
int   (*fp1)(int);              /* VT_PTR -> Sym{type.t=VT_FUNC, ...} */

/* Pointer to variadic function */
int   (*fp2)(int, ...);         /* VT_PTR -> Sym{type.t=VT_FUNC, f.func_type=FUNC_ELLIPSIS} */

/* Pointer to function returning pointer */
int *(*fp3)(void);              /* VT_PTR -> Sym{type.t=VT_FUNC, type.ref->type=VT_PTR->VT_INT} */

/* Array of function pointers */
int (*fp4[5])(double);          /* VT_ARRAY|VT_PTR -> Sym{c=5,
                                     type.t=VT_PTR -> Sym{type.t=VT_FUNC, ...}} */

/* ============================================================
 * Section 8: Struct and union types
 * ============================================================ */

struct point {
    int x;                      /* SYM_FIELD, type.t=VT_INT, c=0 (offset) */
    int y;                      /* SYM_FIELD, type.t=VT_INT, c=4 (offset) */
};

struct rect {
    struct point top_left;      /* SYM_FIELD, type.t=VT_STRUCT, c=0 */
    struct point bottom_right;  /* SYM_FIELD, type.t=VT_STRUCT, c=8 */
};

/* Anonymous struct member (C11) */
struct container {
    int id;
    struct {                    /* Anonymous struct */
        int a;
        int b;
    };
};

union data {
    int    i;                   /* SYM_FIELD, c=0 */
    float  f;                   /* SYM_FIELD, c=0 (same offset as i) */
    char   str[8];              /* SYM_FIELD, c=0 */
};

/* Bitfield type */
struct flags {
    unsigned int active   : 1;  /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=0, width=1 */
    unsigned int mode     : 3;  /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=1, width=3 */
    unsigned int color    : 12; /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=4, width=12 */
    int          reserved : 16; /* VT_BITFIELD | VT_INT, shift=16, width=16 */
};

/* ============================================================
 * Section 9: Enum types
 * ============================================================ */

enum color {
    RED,                        /* VT_ENUM_VAL, enum_val=0 */
    GREEN = 5,                  /* VT_ENUM_VAL, enum_val=5 */
    BLUE,                       /* VT_ENUM_VAL, enum_val=6 */
    ALPHA = 255                 /* VT_ENUM_VAL, enum_val=255 */
};

enum color current_color;       /* VT_INT (enum stored as int), ref -> enum color Sym */

/* ============================================================
 * Section 10: Typedef types
 * ============================================================ */

typedef int                     my_int;         /* VT_TYPEDEF | VT_INT */
typedef unsigned long           size_t_demo;    /* VT_TYPEDEF | VT_INT|VT_LONG|VT_UNSIGNED */
typedef struct point            point_t;        /* VT_TYPEDEF | VT_STRUCT */
typedef int (*callback_t)(int); /* VT_TYPEDEF | VT_PTR -> Sym{type.t=VT_FUNC} */
typedef void (*sighandler_t)(int);

/* ============================================================
 * Section 11: Storage class specifiers
 * ============================================================ */

int             sc1;            /* no storage specifier */
static int      sc2;            /* VT_STATIC | VT_INT */
extern int      sc3;            /* VT_EXTERN | VT_INT */
static int      sc4 = 42;      /* VT_STATIC | VT_INT, has initializer */

/* ============================================================
 * Section 12: Type qualifiers
 * ============================================================ */

const int           q1 = 10;    /* VT_CONSTANT | VT_INT */
volatile int        q2;         /* VT_VOLATILE | IT */
const volatile int  q3;         /* VT_CONSTANT | VT_VOLATILE | VT_INT */
int const           q4;         /* same as const int (order doesn't matter) */

/* ============================================================
 * Section 13: Inline functions
 * ============================================================ */

static inline int max(int a, int b)
{
    return a > b ? a : b;
}

/* ============================================================
 * Section 14: Compound literals (C99)
 * ============================================================ */

struct point origin(void)
{
    /* Compound literal: creates a temporary struct */
    return (struct point){ 0, 0 };
}

/* ============================================================
 * Section 15: Complex declaration examples
 * ============================================================ */

/* Signal handler: pointer to function (int) -> void */
void (*signal_handler)(int);

/* Array of 10 pointers to functions (int) -> int */
int (*dispatch_table[10])(int);

/* Function returning pointer to array of 5 ints */
int (*get_row(int idx))[5];

/* Pointer to array of 3 pointers to functions
   taking (double, ...) -> char* */
char *(*(*pafp[3])(double, ...));

/* const pointer to volatile int */
volatile int * const volatile_ptr = 0;

/* Array of const pointers to functions */
typedef void (*event_handler_t)(int);
const event_handler_t handlers[8];

/* ============================================================
 * Section 16: Main function using some of the above
 * ============================================================ */

int main(void)
{
    struct point p;
    enum color c;
    int (*fn_ptr)(int) = 0;
    int arr[5];

    p.x = 10;
    p.y = 20;
    c = RED;
    arr[0] = max(p.x, p.y);

    return 0;
}


===== FILE: docs/ch04/exercises/ex1_types.md =====
# 练习 4.1：类型位域编码

## 目标

掌握 tcc 中 `CType.t` 的位域编码方式，能够将任意 C 类型声明翻译为 tcc 内部的十六进制表示。

## 参考：VT_* 宏定义

以下是 `tcc.h` 中的关键宏定义（假设 `int` 为 32 位，64 位系统 `long` 为 8 字节）：

### 基本类型（bit 0-3，掩码 `VT_BTYPE = 0x000f`）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_VOID` | 0 | void |
| `VT_BYTE` | 1 | signed char |
| `VT_SHORT` | 2 | short |
| `VT_INT` | 3 | int |
| `VT_LLONG` | 4 | long long |
| `VT_PTR` | 5 | 指针 |
| `VT_FUNC` | 6 | 函数 |
| `VT_STRUCT` | 7 | struct/union |
| `VT_FLOAT` | 8 | float |
| `VT_DOUBLE` | 9 | double |
| `VT_LDOUBLE` | 10 | long double |
| `VT_BOOL` | 11 | _Bool |

### 类型修饰符（bit 4-11）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_UNSIGNED` | `0x0010` | unsigned |
| `VT_DEFSIGN` | `0x0020` | 显式 signed/unsigned |
| `VT_ARRAY` | `0x0040` | 数组 |
| `VT_BITFIELD` | `0x0080` | 位域 |
| `VT_CONSTANT` | `0x0100` | const |
| `VT_VOLATILE` | `0x0200` | volatile |
| `VT_VLA` | `0x0400` | 变长数组 |
| `VT_LONG` | `0x0800` | long 修饰符 |

### 存储类（bit 12-16）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_EXTERN` | `0x1000` | extern |
| `VT_STATIC` | `0x2000` | static |
| `VT_TYPEDEF` | `0x4000` | typedef |
| `VT_INLINE` | `0x8000` | inline |
| `VT_TLS` | `0x10000` | _Thread_local |

---

## 题目

### A 部分：基本类型编码

对于以下声明，写出变量 `v` 的 `CType.t` 的十六进制值。只需写出 `t` 的值，不需要考虑 `ref`。

1. `int x;`
2. `unsigned int x;`
3. `long x;`
4. `unsigned long long x;`
5. `char x;`
6. `signed char x;`
7. `unsigned char x;`
8. `short x;`
9. `float x;`
10. `double x;`
11. `long double x;`
12. `_Bool x;`

### B 部分：类型修饰符

13. `const int x;`
14. `volatile unsigned char x;`
15. `const volatile int *p;`（写出指针所指向的类型的 `t`）
16. `static int x;`
17. `extern int x;`
18. `typedef int my_type;`

### C 部分：复合类型

对于以下声明，写出变量的 `CType.t` 值。如果涉及 `ref`，用 `->` 表示指向的 `Sym` 的 `type.t`。

19. `int *p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
20. `const int *p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
21. `int *const p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
22. `int arr[10];` — 写出 `arr` 的 `t` 和 `arr->ref->type.t` 和 `arr->ref->c`
23. `char *argv[];` — 写出 `argv` 的 `t` 和 `argv->ref->type.t`（注意：`argv->ref->type.t` 本身也是一个 `VT_PTR`）

### D 部分：挑战题

24. 给定以下声明，写出完整的类型链（从变量到最内层类型）：

```c
const int * const * volatile pp;
```

25. 给定以下位域声明，写出各成员的 `t` 值（包含 `VT_BITFIELD` 和位域编码信息）：

```c
struct {
    unsigned int flag : 1;
    int value : 12;
};
```

---

## 参考答案

### A 部分

| # | 声明 | `t` 的十六进制 | 计算过程 |
|---|------|---------------|----------|
| 1 | `int x` | `0x0003` | `VT_INT` = 3 |
| 2 | `unsigned int x` | `0x0033` | `VT_INT \| VT_UNSIGNED \| VT_DEFSIGN` = 3 \| 0x10 \| 0x20 |
| 3 | `long x` (64-bit) | `0x0807` | `VT_INT \| VT_LONG` = 3 \| 0x800, 但注意 64 位系统 long 是 `VT_LLONG\|VT_LONG` = 4 \| 0x800 = `0x0804`。实际上 tcc 中 long 在 64 位 Linux 上编码为 `VT_LLONG\|VT_LONG` |

**注意**：3 题和 4 题的答案取决于目标平台。在 64 位 Linux 上 `LONG_SIZE == 8`，`long` 编码为 `VT_LLONG | VT_LONG` = `0x0804`。在 32 位系统上为 `VT_INT | VT_LONG` = `0x0803`。

详细答案请参考 `tcc.h` 中 `VT_*` 定义以及 `parse_btype()` 中对 `TOK_LONG` 的处理逻辑。

### B 部分提示

- `const int x;` → `VT_INT | VT_CONSTANT` = `0x0103`
- `static int x;` → `VT_INT | VT_STATIC` = `0x2003`
- `typedef int my_type;` → `VT_INT | VT_TYPEDEF` = `0x4003`

### C 部分提示

- `int *p;` → `t = VT_PTR` = `0x0005`，`ref->type.t = VT_INT` = `0x0003`
- `const int *p;` → `t = VT_PTR` = `0x0005`，`ref->type.t = VT_INT | VT_CONSTANT` = `0x0103`
- `int *const p;` → `t = VT_PTR | VT_CONSTANT` = `0x0105`，`ref->type.t = VT_INT` = `0x0003`
- `int arr[10];` → `t = VT_ARRAY | VT_PTR` = `0x0045`，`ref->type.t = VT_INT` = `0x0003`，`ref->c = 10`

---

## 思考题

1. 为什么 tcc 将所有指针类型共用 `VT_PTR`，而不是为每种指针分配不同的基本类型编号？

2. tcc 中 `VT_DEFSIGN` 位的作用是什么？为什么不能只用 `VT_UNSIGNED` 来区分 signed 和 unsigned？

3. 数组类型同时设置 `VT_ARRAY` 和 `VT_PTR` 的设计有什么好处？这如何简化了数组到指针的退化（array-to-pointer decay）操作？


===== FILE: docs/ch04/exercises/ex2_parse.md =====
# 练习 4.2：复杂声明解析追踪

## 目标

通过手动追踪 tcc 的 `parse_btype()` 和 `type_decl()` 函数的执行过程，理解 C 声明的解析机制，并画出最终的 `Sym` 链表结构。

## 背景

C 语言的声明语法遵循"声明模仿使用"（declaration mimics use）的原则。tcc 的声明解析分为两步：

1. `parse_btype()`：解析类型说明符（如 `static const int`）
2. `type_decl()`：解析声明符（如 `*arr[3]`），包括指针、数组、函数参数

`type_decl()` 的关键规则：
- 遇到 `*`：调用 `mk_pointer(type)`，在类型外面包裹一层指针
- 遇到 `(`：可能是嵌套声明符（递归调用 `type_decl`）或函数参数列表（`post_type`）
- 遇到标识符：记录变量名
- 遇到 `[`：`post_type` 处理数组维度，递归处理多维
- 遇到 `(` 参数列表 `)`：`post_type` 创建函数原型 Sym

---

## 题目

### 题目 1：函数指针数组

追踪以下声明的解析过程：

```c
int (*handlers[5])(double, int);
```

**要求**：
1. 写出 `parse_btype()` 的执行步骤（识别了哪些记号，`t` 的最终值）
2. 写出 `type_decl()` 的执行步骤（每一步遇到的记号和执行的操作）
3. 画出最终的 `CType` + `Sym` 链表结构（从 `handlers` 变量开始）

**提示**：解析顺序
```
parse_btype: 'int' → t = VT_INT

type_decl:
  无 * 前缀
  遇到 '(' → 尝试 post_type 失败（因为是嵌套声明符）
  递归 type_decl:
    无 * 前缀
    遇到 '*' → mk_pointer: t = VT_PTR -> {VT_INT}
    遇到 'handlers' → v = 'handlers'
  跳过 ')'
  遇到 '[' → post_type:
    解析 '5' → n = 5
    跳过 ']'
    创建 Sym: c=5, type=VT_PTR->{VT_INT}
    type->t = VT_ARRAY|VT_PTR
  遇到 '(' → post_type:
    解析参数 'double' → Sym{type=VT_DOUBLE}
    跳过 ','
    解析参数 'int' → Sym{type=VT_INT}
    跳过 ')'
    创建函数原型 Sym: type=VT_INT, func_type=FUNC_NEW
    type->t = VT_FUNC
```

### 题目 2：const 指针的指针

追踪以下声明的解析过程：

```c
static const char *(* const table[4])(void);
```

**要求**：
1. 写出 `parse_btype()` 的完整执行过程
2. 写出 `type_decl()` 的完整执行过程
3. 画出最终的类型链结构

**提示**：注意 `* const` 中的 `const` 修饰的是指针本身（通过 `type_decl` 中的 `qualifiers` 收集），而 `const char` 中的 `const` 修饰的是被指向的类型（通过 `parse_btype` 设置）。

### 题目 3：变参函数指针

追踪以下声明的解析过程：

```c
int (* (*get_printer(void))(const char *, ...))(int);
```

这是一个函数 `get_printer`，无参数，返回一个函数指针，该函数指针指向一个变参函数（接受 `const char *` 和 `...`），该变参函数返回另一个函数指针（接受 `int`，返回 `int`）。

**要求**：
1. 分步写出解析过程
2. 画出完整的类型链
3. 用通俗语言描述这个类型

### 题目 4：简化练习

对于以下每个声明，直接画出最终的 `CType` + `Sym` 结构图：

(a) `double *p;`

(b) `int a[3][4];`

(c) `void (*callback)(int, void *);`

(d) `const struct { int x; int y; } *p;`

(e) `int (*ap[2])(char);`

---

## 解题模板

对于每个声明，请按以下格式记录解析过程：

```
=== parse_btype ===
遇到 'int':
  t = VT_INT (0x0003)
  type_found = 1
跳出循环

=== type_decl ===
遇到 '*':
  mk_pointer: 创建 Sym_A, type->t = VT_PTR (0x0005)
  Sym_A.type.t = VT_INT
  ret = pointed_type(type) → Sym_A

遇到 '(':
  post_type 返回 0（不是参数列表，是嵌套括号）
  递归 type_decl:
    遇到 'varname':
      *v = 'varname'
  跳过 ')'

遇到 '[':
  post_type:
    解析 'N' → n = N
    创建 Sym_B: c=N, type.t = VT_PTR (指向 Sym_A)
    type->t = VT_ARRAY | VT_PTR (0x0045)
    type->ref = Sym_B

=== 最终结构 ===
varname 的 CType:
  t = VT_ARRAY | VT_PTR (0x0045)
  ref = Sym_B { c=N, type.t = VT_ARRAY|VT_PTR -> ... }
```

---

## 思考题

1. 为什么 `type_decl()` 需要返回 `ret`（最内层类型的指针）？这个返回值在哪里被使用？

2. `type_decl()` 中 `post` 和 `ret` 两个指针的区别是什么？在什么情况下它们指向同一个 `CType`，什么情况下不同？

3. 解析 `int (*p)[10]` 和 `int *p[10]` 的区别在哪里？`()` 的存在如何改变了解析结果？

4. tcc 的 `post_type()` 如何区分"函数参数列表"和"嵌套括号声明符"？它使用了什么试探方法？


===== FILE: docs/ch04/exercises/ex3_scope.md =====
# 练习 4.3：符号表变化追踪

## 目标

通过手动追踪 tcc 编译过程中的 `sym_push`、`sym_pop`、`sym_link` 操作，深入理解符号栈和记号表的交互机制，以及名称遮蔽（name shadowing）的实现原理。

## 背景：关键数据结构和操作

### 数据结构

```
global_stack:   Sym* → Sym* → ... → NULL  (全局符号链表，通过 prev 链接)
local_stack:    Sym* → Sym* → ... → NULL  (局部符号链表，通过 prev 链接)
table_ident[]:  TokenSym* 数组，以记号编号为索引
  每个 TokenSym 有:
    sym_identifier → 当前可见的同名变量/函数
    sym_struct     → 当前可见的同名结构体标签
```

### 关键操作

- **`sym_push(v, type, r, c)`**：
  1. 分配新 Sym `s`
  2. `s->prev = *ps`（压入栈顶，ps 指向 local_stack 或 global_stack）
  3. `*ps = s`
  4. 调用 `sym_link(s, 1)`：`s->prev_tok = *sym_id_ptr; *sym_id_ptr = s`
  5. 设置 `s->sym_scope = local_scope`

- **`sym_link(s, 1)`**（使符号可见）：
  ```
  s->prev_tok = table_ident[idx]->sym_identifier
  table_ident[idx]->sym_identifier = s
  ```

- **`sym_link(s, 0)`**（使符号不可见，恢复旧定义）：
  ```
  table_ident[idx]->sym_identifier = s->prev_tok
  ```

- **`sym_pop(stack, boundary, keep)`**：
  弹出从栈顶到 boundary 的所有符号，对每个符号调用 `sym_link(s, 0)`

---

## 题目

### 题目 1：简单作用域

追踪以下代码的符号表变化。为每个全局标识符（`x`、`f`）计算 `TOK_IDENT` 偏移后的索引，记为 `idx_x`、`idx_f`。

```c
int x = 10;

int f(int a) {
    int b = a + x;
    {
        int x = 100;
        b = b + x;
    }
    return b + x;
}
```

**追踪格式**：对每个操作，记录：
1. 操作名称和参数
2. `local_stack` 的变化（列出栈中所有符号，从顶到底）
3. `global_stack` 的变化（如果有）
4. `table_ident[idx_x]->sym_identifier` 的变化（链表状态）

**追踪开始**：

```
初始状态:
  global_stack = NULL
  local_stack = NULL
  table_ident[idx_x]->sym_identifier = NULL

=== 1. 解析 'int x = 10' (全局) ===
  parse_btype: t = VT_INT
  type_decl: v = 'x'
  has_init = 1, l = VT_CONST

  sym_push('x', VT_INT, VT_CONST|VT_SYM, 0):
    分配 Sym_G_x
    Sym_G_x.prev = NULL (global_stack 为空)
    global_stack = Sym_G_x
    sym_link(Sym_G_x, 1):
      Sym_G_x.prev_tok = NULL
      table_ident[idx_x]->sym_identifier = Sym_G_x
      Sym_G_x.sym_scope = 0

  状态:
    global_stack: [Sym_G_x] → NULL
    local_stack: NULL
    table_ident[idx_x]->sym_identifier: [Sym_G_x] → NULL

  decl_initializer_alloc: x = 10 初始化

=== 2. 解析 'int f(int a)' 函数头 ===
  parse_btype: t = VT_INT
  type_decl: 解析函数声明符

  post_type (函数参数):
    解析参数 'int a':
      sym_push('a', VT_INT, VT_LOCAL|VT_LVAL, 0):
        分配 Sym_P_a
        local_stack = [Sym_P_a]
        sym_link(Sym_P_a, 1):
          Sym_P_a.prev_tok = NULL (a 是新名)
          table_ident[idx_a]->sym_identifier = Sym_P_a
          Sym_P_a.sym_scope = 1

  创建函数原型 Sym，然后 sym_pop 弹出参数符号

  decl 遇到 '{'，调用 gen_function:
    sym_push2(local_stack, SYM_FIELD, 0, 0):  // 哨兵
      local_stack = [Sentinel] → NULL

    sym_push_params:
      重新压入参数 a:
        local_stack = [Sym_P_a2] → [Sentinel] → NULL
        table_ident[idx_a]->sym_identifier = Sym_P_a2

=== 3. 解析 'int b = a + x' (函数体) ===

  请继续追踪...
```

**你的任务**：继续完成步骤 3-6 的追踪，包括：
- `sym_push('b', ...)` 的效果
- 内层作用域中 `sym_push('x', ...)` 的遮蔽效果
- `prev_scope` 弹出内层符号后的恢复效果
- 函数结束时 `sym_pop` 清理所有局部符号

### 题目 2：多重遮蔽

追踪以下代码中每个 `printf` 语句处 `x` 的值和它在符号表中的位置：

```c
int x = 1;

void demo(void) {
    int x = 2;              /* 遮蔽全局 x */
    {
        int x = 3;          /* 遮蔽函数级 x */
        printf("%d\n", x);  /* 输出? */
    }
    printf("%d\n", x);      /* 输出? */
    {
        int x = 4;          /* 再次遮蔽 */
        printf("%d\n", x);  /* 输出? */
    }
    printf("%d\n", x);      /* 输出? */
}
```

**要求**：
1. 对每个 `{` / `}` 标记作用域的进入和退出
2. 记录 `table_ident[idx_x]->sym_identifier` 在每个 `printf` 处指向哪个 Sym
3. 画出 `prev_tok` 链在最内层时的状态

### 题目 3：typedef 与变量的交互

追踪以下代码的符号表变化。注意 typedef 和变量使用同一个标识符命名空间：

```c
typedef int T;

int test(void) {
    T a;                   /* T 解析为 int */
    {
        typedef float T;   /* 遮蔽全局 T */
        T b;              /* T 解析为 float */
        a = (int)b;
    }
    T c;                   /* T 解析为? */
    return a + c;
}
```

**要求**：
1. 说明 `typedef float T` 如何通过 `sym_push` 遮蔽全局的 `typedef int T`
2. 说明 `prev_scope` 如何恢复全局 typedef
3. 第二个 `T c;` 处的 `T` 解析为什么类型？

### 题目 4：函数原型中的参数作用域

追踪以下声明中参数符号的生命周期：

```c
int process(int n, int data[n]);
```

**要求**：
1. 说明 `post_type` 中函数参数解析的步骤
2. 参数 `n` 何时被压入 `local_stack`？
3. 数组维度中的 `n` 如何被解析？（提示：注意 `local_scope` 的递增和 `sym_push_params` 的时序）
4. 参数符号何时被弹出？

---

## 解题工具

### 符号状态记录表

对每个关键位置，填写下表：

| 位置 | local_stack（顶到底） | table_ident[idx]->sym_identifier |
|------|----------------------|----------------------------------|
| 函数入口 | ... | ... |
| 声明 b 之后 | ... | ... |
| 进入内层 { | ... | ... |
| 声明内层 x 之后 | ... | ... |
| 离开内层 } | ... | ... |
| 函数出口 | ... | ... |

### prev_tok 链记录

对每个标识符名，画出 `prev_tok` 链在不同时刻的状态：

```
时刻 T1 (进入内层作用域后):
  table_ident[idx_x]->sym_identifier → [内层 x, scope=2]
    → prev_tok → [函数参数 x, scope=1]
      → prev_tok → [全局 x, scope=0]
        → prev_tok → NULL
```

---

## 思考题

1. 为什么 tcc 使用 `sym_link` 而不是遍历符号栈来查找符号？这种设计的时间复杂度优势是什么？

2. `sym_pop` 的 `keep` 参数在什么场景下会被设置为非零？为什么这些场景不能直接释放符号？

3. 如果两个变量在不同的作用域中同名但类型不同，tcc 如何确保在内层作用域中类型检查使用的是内层的类型？

4. `local_scope` 计数器的值在函数体外、函数体内、嵌套块中分别是多少？这个计数器在 `sym_push` 的重定义检测中起什么作用？


===== FILE: docs/ch04/index.md =====
# 第四章 语法分析器与类型系统

> "编译器的心脏不在于它能生成多高效的代码，而在于它如何理解程序的结构。"

本章深入剖析 TinyCC 的语法分析器与类型系统。我们将看到，tcc 采用了一种极其精简的设计：没有抽象语法树（AST），解析与代码生成交织进行，类型信息压缩在单个整数的位域中。这种设计使得编译器在保持 C 语言完整支持的同时，代码量仅为 GCC 的百分之一。

## 4.1 语法分析理论

在深入 tcc 的实现之前，我们先回顾语法分析的基本理论。语法分析器（parser）的任务是将词法分析器产出的记号流（token stream）组织成符合语言文法的结构。

### 4.1.1 上下文无关文法

C 语言的语法可以用上下文无关文法（Context-Free Grammar, CFG）来描述。一个 CFG 是四元组 $G = (V, \Sigma, R, S)$，其中：

- $V$ 是非终结符集合（如 `expression`, `statement`, `declaration`）
- $\Sigma$ 是终结符集合（即词法记号：标识符、关键字、运算符等）
- $R$ 是产生式规则集合
- $S$ 是起始符号

以 C 语言的表达式为例，其文法规则可以写成：

```
expression     -> assignment_expr (',' assignment_expr)*
assignment_expr -> conditional_expr
                 | unary_expr ('=' | '+=' | '-=' | ...) assignment_expr
conditional_expr -> logor_expr ('?' expression ':' conditional_expr)?
logor_expr     -> logand_expr ('||' logand_expr)*
logand_expr    -> or_expr ('&&' or_expr)*
or_expr        -> xor_expr ('|' xor_expr)*
xor_expr       -> and_expr ('^' and_expr)*
and_expr       -> eq_expr ('&' eq_expr)*
eq_expr        -> rel_expr (('==' | '!=') rel_expr)*
rel_expr       -> shift_expr (('<' | '>' | '<=' | '>=') shift_expr)*
shift_expr     -> add_expr (('<<' | '>>') add_expr)*
add_expr       -> mul_expr (('+' | '-') mul_expr)*
mul_expr       -> unary_expr (('*' | '/' | '%') unary_expr)*
unary_expr     -> postfix_expr
               | ('++' | '--') unary_expr
               | ('&' | '*' | '+' | '-' | '~' | '!') unary_expr
               | 'sizeof' unary_expr
               | '(' type_name ')' unary_expr
postfix_expr   -> primary_expr
                 | postfix_expr '[' expression ']'
                 | postfix_expr '(' argument_list? ')'
                 | postfix_expr '.' identifier
                 | postfix_expr '->' identifier
                 | postfix_expr '++'
                 | postfix_expr '--'
```

这个层次结构精确地编码了 C 语言的运算符优先级和结合性。每下降一层，优先级升高一级。

### 4.1.2 递归下降分析

递归下降（recursive descent）是最直观的语法分析方法：为文法中的每个非终结符编写一个函数，函数体直接对应产生式的右部。遇到非终结符时递归调用对应函数；遇到终结符时匹配当前记号。

递归下降的优点是实现简单、错误信息精确、调试方便。其局限在于：

1. **左递归问题**：直接左递归（如 `expr -> expr '+' term`）会导致无限递归。解决方法是改写为右递归或使用循环。
2. **回溯问题**：当同一非终结符有多个产生式共享前缀时，需要前瞻（lookahead）或回溯（backtracking）来决定走哪条路。

tcc 的旧版表达式解析器采用了典型的递归下降方式，通过 `expr_prod()` -> `expr_sum()` -> `expr_shift()` -> ... -> `expr_lor()` 的调用链来体现优先级层次（见 `tccgen.c` 第 6389-6495 行）：

```c
/* 旧版递归下降解析器（非 precedence_parser 模式） */
static void expr_prod(void)
{
    int t;
    unary();
    while ((t = tok) == '*' || t == '/' || t == '%') {
        next();
        unary();
        gen_op(t);
    }
}

static void expr_sum(void)
{
    int t;
    expr_prod();
    while ((t = tok) == '+' || t == '-') {
        next();
        expr_prod();
        gen_op(t);
    }
}
/* ... expr_shift -> expr_cmp -> expr_cmpeq -> expr_and ->
       expr_xor -> expr_or -> expr_land -> expr_lor */
```

每个函数负责一个优先级层次：先调用更高优先级的函数解析操作数，然后循环处理当前优先级的运算符。这正是将左递归改写为循环的经典手法。

### 4.1.3 优先级爬升

优先级爬升（precedence climbing）是一种更紧凑的表达式解析算法。它用单个函数配合优先级参数来替代一连串递归函数。核心思想是：给定当前优先级 `p`，解析所有优先级 >= `p` 的运算符。

tcc 在定义了 `precedence_parser` 宏之后，采用了优先级爬升算法。其核心实现如下（`tccgen.c` 第 6501-6556 行）：

```c
static int precedence(int tok)
{
    switch (tok) {
        case TOK_LOR: return 1;
        case TOK_LAND: return 2;
        case '|': return 3;
        case '^': return 4;
        case '&': return 5;
        case TOK_EQ: case TOK_NE: return 6;
        case TOK_ULT: case TOK_UGE: return 7;
        case TOK_SHL: case TOK_SAR: return 8;
        case '+': case '-': return 9;
        case '*': case '/': case '%': return 10;
        default:
            if (tok >= TOK_ULE && tok <= TOK_GT)
                return 7;  /* 关系运算符 */
            return 0;      /* 非二元运算符 */
    }
}
```

优先级映射表如下：

| 优先级 | 运算符 | 含义 |
|--------|--------|------|
| 1 | `\|\|` | 逻辑或 |
| 2 | `&&` | 逻辑与 |
| 3 | `\|` | 按位或 |
| 4 | `^` | 按位异或 |
| 5 | `&` | 按位与 |
| 6 | `==`, `!=` | 等性比较 |
| 7 | `<`, `>`, `<=`, `>=` | 关系比较 |
| 8 | `<<`, `>>` | 移位 |
| 9 | `+`, `-` | 加减 |
| 10 | `*`, `/`, `%` | 乘除模 |

`expr_infix()` 函数实现了优先级爬升的核心逻辑：

```c
static void expr_infix(int p)
{
    int t = tok, p2;
    while ((p2 = precedence(t)) >= p) {
        if (t == TOK_LOR || t == TOK_LAND) {
            expr_landor(t);       /* 短路求值特殊处理 */
        } else {
            next();
            unary();              /* 解析右操作数 */
            if (precedence(tok) > p2)
                expr_infix(p2 + 1); /* 右结合递归 */
            gen_op(t);            /* 生成运算指令 */
        }
        t = tok;
    }
}
```

这个算法的关键观察：当右操作数后面紧跟的运算符优先级高于当前运算符时，需要递归进入更高优先级。这保证了 `1 + 2 * 3` 被正确解析为 `1 + (2 * 3)`。对于左结合运算符（如 `+`），循环自然处理；对于右结合运算符（如赋值），递归时传入 `p2 + 1` 而非 `p2`。

为了加速查表，tcc 在初始化时将 `precedence()` 的结果缓存到一个 256 字节的数组中：

```c
static unsigned char prec[256];
static void init_prec(void)
{
    int i;
    for (i = 0; i < 256; i++)
        prec[i] = precedence(i);
}
#define precedence(i) ((unsigned)i < 256 ? prec[i] : 0)
```

这样，对于 ASCII 范围内的运算符记号（`+`, `-`, `*`, `<` 等），查表只需一次数组访问。超出 256 的记号（如 `TOK_LAND`、`TOK_EQ` 等）走 switch 路径。

### 4.1.4 LL(1) 与回溯

严格来说，C 语言的文法不是 LL(1) 的。最典型的歧义场景是声明与表达式的区分：

```c
(x) - y    /* 表达式：x 减 y */
(int)x     /* 类型转换 */
```

两者都以 `(` 开头，需要前瞻多个记号才能区分。tcc 的解决策略是：

1. 在 `unary()` 中遇到 `(` 时，先尝试将其解析为类型名（调用 `parse_btype()`）。
2. 如果 `parse_btype()` 成功，则按类型转换或复合字面量处理。
3. 如果失败，则回退为普通表达式。

这种"尝试-回退"的方式在 `tccgen.c` 的 `unary()` 函数中体现得很清楚：

```c
case '(':
    next();
    /* 尝试解析为类型转换 */
    if (parse_btype(&type, &ad, 0)) {
        type_decl(&type, &ad, &n, TYPE_ABSTRACT);
        skip(')');
        /* 检查是否为 C99 复合字面量 */
        if (tok == '{') {
            /* 处理复合字面量 */
        } else {
            /* 类型转换 */
            unary();
            gen_cast(&type);
        }
    } else if (tok == '{') {
        /* GCC 语句表达式 */
    } else {
        /* 普通括号表达式 */
        gexpr();
        skip(')');
    }
```

同样，声明与语句的区分也存在歧义：

```c
x * y;   /* 表达式语句：x 乘以 y */
T *p;    /* 声明：T 类型的指针 p */
```

tcc 在 `decl()` 函数中先调用 `parse_btype()` 尝试解析类型说明符。如果成功，则进入声明解析路径；否则将当前行作为表达式语句处理。

### 4.1.5 错误恢复

tcc 的错误恢复策略非常简单粗暴：调用 `tcc_error()` 直接终止编译，不尝试恢复。这与 GCC 和 Clang 的"尽力继续"策略形成鲜明对比。tcc 的哲学是：快速报告第一个错误，让用户修复后重新编译。对于一个追求编译速度的编译器来说，这是一个合理的权衡。

`skip()` 函数是最常用的错误检测手段：

```c
ST_FUNC void skip(int c)
{
    if (tok != c) {
        char tmp[40];
        pstrcpy(tmp, sizeof tmp, get_tok_str(c, &tokc));
        tcc_error("'%s' expected (got '%s')", tmp, get_tok_str(tok, &tokc));
    }
    next();
}
```

它在期望某个特定记号时调用，如果当前记号不匹配就报错退出。

## 4.2 tcc 的单遍解析架构

### 4.2.1 无 AST 的设计

传统编译器通常分为多个阶段：

```
源代码 -> 词法分析 -> 语法分析 -> AST -> 语义分析 -> IR -> 优化 -> 目标代码
```

tcc 跳过了 AST 和 IR 阶段，直接在解析的同时生成目标代码：

```
源代码 -> 词法分析 -> 解析 + 代码生成 -> 目标代码
```

这种单遍（single-pass）架构意味着：

1. **没有中间表示**：不构建 AST，不生成中间代码。
2. **即时代码生成**：每解析完一个表达式或语句，立即生成对应的机器指令。
3. **符号驱动**：类型信息完全存储在符号表中，通过 `Sym` 结构的 `type` 字段传递。

### 4.2.2 值栈（vstack）机制

既然没有 AST 来存储中间结果，tcc 如何处理复合表达式？答案是**值栈**（`vstack`）。每个 `SValue` 结构包含：

```c
typedef struct SValue {
    CType type;          /* 类型信息 */
    unsigned short r;    /* 值的位置：寄存器号、VT_CONST、VT_LOCAL 等 */
    unsigned short r2;   /* 第二个寄存器（用于 long long 等双字类型） */
    union {
        struct { int jtrue, jfalse; };  /* 前向跳转链 */
        CValue c;                        /* 常量值 */
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; /* VT_CMP 比较操作 */
        struct Sym *sym;                           /* 关联的符号 */
    };
} SValue;
```

解析表达式 `a + b * c` 时：

1. `unary()` 解析 `a`，将一个 `SValue` 压入 `vstack`。
2. 遇到 `+`，调用 `expr_infix(9)`。
3. `unary()` 解析 `b`，压栈。
4. 遇到 `*`（优先级 10 > 9），递归进入 `expr_infix(10)`。
5. `unary()` 解析 `c`，压栈。
6. `*` 处理完毕，`gen_op('*')` 从栈顶弹出两个值，生成乘法指令，将结果压回。
7. 回到 `+`，`gen_op('+')` 生成加法指令。

`vtop` 指针始终指向栈顶的有效值。整个表达式求值过程就是在值栈上的操作。

### 4.2.3 交织解析与代码生成

以 `if` 语句为例，看解析与代码生成如何交织（`tccgen.c` 第 7183 行）：

```c
if (t == TOK_IF) {
    new_scope_s(&o);
    skip('(');
    gexpr_decl();                    /* 解析条件表达式 */
    a = gvtst(1, 0);                /* 生成条件跳转，a 记录跳转补丁位置 */
    skip(')');
    block(0);                        /* 解析 then 分支 */
    if (tok == TOK_ELSE) {
        d = gjmp(0);                /* 生成无条件跳转（跳过 else） */
        gsym(a);                     /* 补丁 then 跳转目标到此处 */
        next();
        block(0);                    /* 解析 else 分支 */
        gsym(d);                     /* 补丁 else 跳转目标到此处 */
    } else {
        gsym(a);                     /* 补丁条件跳转目标到此处 */
    }
    prev_scope_s(&o);
}
```

注意这里没有"先构建 AST 再遍历生成代码"的过程。解析 `if` 关键字的同时，条件跳转指令已经被生成到代码段中。`a = gvtst(1, 0)` 返回一个待补丁的跳转位置；当 then 分支的代码生成完毕后，`gsym(a)` 将跳转目标补丁为当前位置。

这种"前向引用 + 延迟补丁"的技术贯穿整个编译器。

### 4.2.4 优势与局限

**优势：**

1. **极快的编译速度**：单遍扫描，无需构建和遍历中间数据结构。
2. **极低的内存消耗**：不需要存储 AST 节点，只需维护符号表和值栈。
3. **代码简洁**：整个编译器核心（`tccgen.c`）不到 9000 行。

**局限：**

1. **无法进行全局优化**：没有 IR 意味着无法做公共子表达式消除、循环优化等。
2. **前向引用受限**：变量必须在使用前声明（符合 C89 要求）。函数的前向声明通过 `external_global_sym` 创建外部符号来处理。
3. **代码质量受限**：生成的代码质量远不如 GCC `-O2`，但足以"足够快地编译出能跑的程序"。
4. **某些语义分析受限**：例如无法在看到整个函数体后再决定是否内联。

### 4.2.5 编译入口

整个编译过程从 `tccgen_compile()` 开始（`tccgen.c` 第 306 行）：

```c
ST_FUNC int tccgen_compile(TCCState *s1)
{
    funcname = "";
    func_ind = -1;
    anon_sym = SYM_FIRST_ANOM;
    nocode_wanted = DATA_ONLY_WANTED;  /* 函数外不生成代码 */
    /* ... 初始化调试、覆盖率等 ... */

    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();                            /* 读取第一个记号 */
    decl(VT_CONST);                    /* 顶层声明解析循环 */
    gen_inline_functions(s1);          /* 生成所有被引用的内联函数 */
    check_vstack();                    /* 检查值栈平衡 */
    return 0;
}
```

`decl(VT_CONST)` 是顶层解析的入口。它循环调用 `parse_btype()` + `type_decl()` 来解析声明，遇到 `{` 时调用 `gen_function()` 生成函数体。整个翻译单元（translation unit）就在这个循环中被处理完毕。

## 4.3 类型系统设计

C 语言的类型系统是出了名的复杂：基本类型、派生类型（指针、数组、函数）、结构体/联合体、枚举、类型限定符、存储类说明符……tcc 如何在单个整数中编码这一切？

### 4.3.1 CType 结构

tcc 的类型表示只有两个字段（`tcc.h` 第 530 行）：

```c
typedef struct CType {
    int t;            /* 类型编码位域 */
    struct Sym *ref;  /* 附加信息指针 */
} CType;
```

`t` 是一个 32 位整数，通过位域编码基本类型、类型修饰符和存储类。`ref` 是一个指向 `Sym` 结构的指针，用于存储结构体/联合体的成员列表、函数的参数列表、数组的元素个数等无法用位域表达的信息。

这种设计的精妙之处在于：绝大多数类型操作只需位运算。例如判断一个类型是否为 `const`：

```c
if (type->t & VT_CONSTANT) { /* 是 const */ }
```

判断是否为指针：

```c
if ((type->t & VT_BTYPE) == VT_PTR) { /* 是指针 */ }
```

### 4.3.2 基本类型编码（VT_BTYPE）

`t` 的最低 4 位（bit 0-3）编码基本类型，由 `VT_BTYPE`（值为 `0x000f`）掩码提取：

```c
#define VT_BTYPE       0x000f   /* 基本类型掩码 */
#define VT_VOID             0   /* void */
#define VT_BYTE             1   /* signed char */
#define VT_SHORT            2   /* short */
#define VT_INT              3   /* int */
#define VT_LLONG            4   /* long long (64 位整数) */
#define VT_PTR              5   /* 指针（所有指针类型共享此值） */
#define VT_FUNC             6   /* 函数 */
#define VT_STRUCT           7   /* struct/union */
#define VT_FLOAT            8   /* IEEE 单精度浮点 */
#define VT_DOUBLE           9   /* IEEE 双精度浮点 */
#define VT_LDOUBLE         10   /* IEEE 长双精度浮点 */
#define VT_BOOL            11   /* _Bool (C99) */
#define VT_QLONG           13   /* 128 位整数（仅 x86-64 ABI） */
#define VT_QFLOAT          14   /* 128 位浮点（仅 x86-64 ABI） */
```

注意几个设计决策：

1. **所有指针共享 `VT_PTR`**：`int *` 和 `char *` 的 `VT_BTYPE` 都是 `VT_PTR`，区别在于 `ref` 指向的 `Sym` 中存储的被指向类型。
2. **数组也是指针**：数组类型同时设置 `VT_ARRAY` 和 `VT_PTR` 位。`ref->c` 存储数组长度，`ref->type` 存储元素类型。
3. **struct/union/enum 共享 `VT_STRUCT`**：通过高位进一步区分。

### 4.3.3 类型修饰符位域

基本类型之上，通过设置 `t` 中更高的位来添加修饰符：

```c
#define VT_UNSIGNED    0x0010   /* bit 4:  unsigned */
#define VT_DEFSIGN     0x0020   /* bit 5:  显式 signed/unsigned */
#define VT_ARRAY       0x0040   /* bit 6:  数组 */
#define VT_BITFIELD    0x0080   /* bit 7:  位域 */
#define VT_CONSTANT    0x0100   /* bit 8:  const */
#define VT_VOLATILE    0x0200   /* bit 9:  volatile */
#define VT_VLA         0x0400   /* bit 10: 变长数组（VLA） */
#define VT_LONG        0x0800   /* bit 11: long 修饰符 */
```

`VT_DEFSIGN` 标记是否显式指定了 `signed` 或 `unsigned`。这在类型检查中用于区分 `char`（由实现定义符号性）和 `signed char`（显式有符号）。

`VT_LONG` 是一个辅助位，用于区分 `long int` 和 `long long int`。具体来说：

- `long int`：`VT_INT | VT_LONG`
- `long long int`：`VT_LLONG | VT_LONG`
- `long double`：`VT_LDOUBLE | VT_LONG`（实际上 `parse_btype` 中直接设置为 `VT_LDOUBLE`）

### 4.3.4 存储类编码

存储类说明符占据 `t` 的 bit 12-16：

```c
#define VT_EXTERN  0x00001000   /* bit 12: extern */
#define VT_STATIC  0x00002000   /* bit 13: static */
#define VT_TYPEDEF 0x00004000   /* bit 14: typedef */
#define VT_INLINE  0x00008000   /* bit 15: inline */
#define VT_TLS     0x00010000   /* bit 16: _Thread_local */

#define VT_STORAGE (VT_EXTERN | VT_STATIC | VT_TYPEDEF | VT_INLINE | VT_TLS)
```

`VT_STORAGE` 掩码可以一次性提取所有存储类信息。在类型推导中，经常需要剥离存储类：

```c
type->t & ~VT_STORAGE   /* 去掉存储类，保留纯类型信息 */
```

### 4.3.5 结构体/联合体/枚举编码

结构体、联合体和枚举通过 `t` 的高位（bit 20 起）进一步区分：

```c
#define VT_STRUCT_SHIFT 20
/* VT_STRUCT 的高位编码 */
#define VT_UNION    (1 << VT_STRUCT_SHIFT | VT_STRUCT)   /* 联合体 */
#define VT_ENUM     (2 << VT_STRUCT_SHIFT)               /* 枚举类型 */
#define VT_ENUM_VAL (3 << VT_STRUCT_SHIFT)               /* 枚举常量值 */
```

枚举常量值（`VT_ENUM_VAL`）比较特殊：它的 `Sym.ref` 指向所属枚举类型，`Sym.enum_val` 存储常量的整数值。

### 4.3.6 位域编码

位域信息存储在 `t` 的高位中（bit 20-31），通过 `VT_BITFIELD` 位标识：

```c
#define VT_BITFIELD    0x0080
#define VT_STRUCT_SHIFT 20
#define VT_STRUCT_MASK (((1U << (6+6)) - 1) << VT_STRUCT_SHIFT | VT_BITFIELD)
```

位域编码包含两个 6 位字段：位域在存储单元中的偏移量和位域的宽度。这使得位域的访问可以在编译时计算出移位和掩码操作。

### 4.3.7 完整类型示例

让我们看几个复杂类型在 tcc 中如何表示：

**示例 1：`const char *p`**

```
p 的类型:
  t = VT_PTR
  ref -> Sym:
    type.t = VT_BYTE | VT_CONSTANT
    type.ref = NULL

编码: VT_PTR, 指向 const signed char
```

**示例 2：`int (*callback)(double, ...)`**

```
callback 的类型:
  t = VT_PTR
  ref -> Sym:                    (*指针指向的函数*)
    type.t = VT_FUNC
    type.ref -> Sym:             (*函数的参数列表头部*)
      type.t = VT_INT           (*返回类型*)
      f.func_type = FUNC_ELLIPSIS (*变参函数*)
      f.func_call = FUNC_CDECL
      next -> Sym:              (*第一个参数*)
        type.t = VT_DOUBLE
        v = SYM_FIELD
        next -> NULL
```

**示例 3：`struct point { int x, y; }`**

```
struct point 的类型:
  t = VT_STRUCT
  ref -> Sym:                    (*struct 定义*)
    v = 'point' | SYM_STRUCT    (*结构体标签*)
    type.t = VT_STRUCT
    next -> Sym:                 (*成员 y*)
      v = 'y' | SYM_FIELD
      type.t = VT_INT
      c = 4                     (*偏移量*)
      next -> Sym:              (*成员 x*)
        v = 'x' | SYM_FIELD
        type.t = VT_INT
        c = 0                   (*偏移量*)
        next -> NULL
```

注意成员列表是逆序存储的（`y` 在 `x` 前面），这是由 `sym_push` 的栈式插入方式决定的。

**示例 4：`int arr[10]`**

```
arr 的类型:
  t = VT_ARRAY | VT_PTR
  ref -> Sym:
    type.t = VT_INT             (*元素类型*)
    c = 10                      (*数组长度*)
```

数组类型同时设置了 `VT_ARRAY` 和 `VT_PTR`。`ref->c` 存储元素个数。这种编码使得数组到指针的退化（array-to-pointer decay）只需清除 `VT_ARRAY` 位：

```c
type->t &= ~VT_ARRAY;  /* 数组退化为指针 */
```

**示例 5：`volatile unsigned long long *restrict p`**

```
p 的类型:
  t = VT_PTR | VT_VOLATILE      (*指针本身是 volatile 的——不对，这里要小心*)

实际上 restrict 被 tcc 忽略（直接 next() 跳过），
而 volatile 修饰的是被指向的类型，所以：

p 的类型:
  t = VT_PTR
  ref -> Sym:
    type.t = VT_LLONG | VT_UNSIGNED | VT_VOLATILE
```

在 `type_decl()` 中，`*` 之后的限定符（const、volatile、restrict）修饰的是指针所指向的类型：

```c
while (tok == '*') {
    qualifiers = 0;
redo:
    next();
    switch(tok) {
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
        qualifiers |= VT_CONSTANT;
        goto redo;
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
        qualifiers |= VT_VOLATILE;
        goto redo;
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
        goto redo;  /* restrict 被忽略 */
    }
    mk_pointer(type);
    type->t |= qualifiers;  /* 限定符加到指针类型上 */
}
```

### 4.3.8 mk_pointer：构造指针类型

`mk_pointer()` 是一个关键的辅助函数，它将任意类型转换为指向该类型的指针：

```c
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR;
    type->ref = s;
}
```

它创建一个新的匿名 `Sym` 来保存原类型，然后将 `type` 修改为 `VT_PTR`，`ref` 指向这个新 `Sym`。这个操作是可嵌套的——对指针类型再次调用 `mk_pointer` 就得到指向指针的指针（`VT_PTR` 的 `ref->type` 也是 `VT_PTR`）。

### 4.3.9 类型兼容性检查

`tcc` 通过 `is_compatible_types()` 和 `is_compatible_unqualified_types()` 进行类型兼容性检查。核心逻辑是：

1. 如果 `VT_BTYPE` 不同，不兼容（特殊处理指针和整数的兼容性）。
2. 对于指针，递归检查被指向的类型。
3. 对于函数，检查返回类型、参数数量和类型、调用约定。
4. 对于结构体，比较 `ref` 指针——同一结构体定义共享同一个 `Sym`，所以只需比较指针即可。

## 4.4 符号表系统

符号表是编译器的核心数据结构之一。tcc 的符号表设计精巧而高效，通过链表栈和记号表的双向链接实现了快速查找和作用域管理。

### 4.4.1 Sym 结构详解

`Sym` 结构是符号表的基本单元（`tcc.h` 第 558 行）：

```c
typedef struct Sym {
    int v;                    /* 符号记号（token identifier） */
    unsigned short r;         /* 关联的寄存器或位置类型 */
    struct SymAttr a;         /* 符号属性 */
    union {
        struct {
            int c;            /* 关联数值或 ELF 符号索引 */
            union {
                int sym_scope;    /* 局部变量的作用域层级 */
                int jnext;        /* 下一个跳转标签 */
                int jind;         /* 标签位置 */
                struct FuncAttr f; /* 函数属性 */
                int auxtype;      /* 位域访问类型 */
            };
        };
        long long enum_val;   /* 枚举常量值（当 IS_ENUM_VAL 时） */
        int *d;               /* 宏定义的记号流 */
        struct Sym *cleanup_func;
    };

    CType type;               /* 类型信息 */
    union {
        struct Sym *next;     /* 下一个相关符号（用于结构体成员和匿名符号） */
        int *e;               /* 宏展开后的记号流 */
        int asm_label;        /* 关联的汇编标签 */
        /* ... 其他用途 ... */
    };
    struct Sym *prev;         /* 栈中前一个符号 */
    union {
        struct Sym *prev_tok; /* 同名的前一个符号（作用域链） */
        /* ... 其他用途 ... */
    };
} Sym;
```

各字段详解：

**`v`（符号记号）**：这是符号的"名字"，实际上是一个记号编号。每个标识符在词法分析阶段被分配一个唯一的编号（从 `TOK_IDENT` 开始递增）。`v` 的高位用于标记特殊含义：

```c
#define SYM_STRUCT     0x40000000  /* 结构体/联合体/枚举符号空间 */
#define SYM_FIELD      0x20000000  /* 结构体成员符号空间 */
#define SYM_FIRST_ANOM 0x10000000  /* 第一个匿名符号的编号 */
```

`SYM_STRUCT` 和 `SYM_FIELD` 使得同一个标识符名可以同时用于变量和结构体标签（C 语言的结构体标签和变量名在不同的命名空间中）。

**`r`（寄存器/位置）**：指示符号值的存放位置。对于局部变量，通常是 `VT_LOCAL | VT_LVAL`（栈上的左值）；对于全局变量，是 `VT_CONST | VT_SYM`（符号引用的常量地址）。

**`a`（符号属性）**：`SymAttr` 位域结构：

```c
struct SymAttr {
    unsigned short
    aligned     : 5,  /* 对齐（log2+1，0 表示未指定） */
    packed      : 1,  /* __attribute__((packed)) */
    weak        : 1,  /* __attribute__((weak)) */
    visibility  : 2,  /* 符号可见性 */
    dllexport   : 1,  /* PE DLL 导出 */
    nodecorate  : 1,  /* PE 无修饰 */
    dllimport   : 1,  /* PE DLL 导入 */
    addrtaken   : 1,  /* 地址被取过 */
    nodebug     : 1,  /* 无调试信息 */
    xxxx        : 2;  /* 保留 */
};
```

**`c`（数值/索引）**：根据上下文有不同含义：
- 对于局部变量：栈偏移量
- 对于全局变量：ELF 符号表中的索引
- 对于数组：元素个数（当作为 `type.ref` 时）
- 对于函数参数：累计参数大小

**`f`（函数属性）**：`FuncAttr` 位域：

```c
struct FuncAttr {
    unsigned
    func_call   : 3,  /* 调用约定（cdecl, stdcall, fastcall 等） */
    func_type   : 2,  /* FUNC_NEW/FUNC_OLD/FUNC_ELLIPSIS */
    func_noreturn : 1, /* __attribute__((noreturn)) */
    func_ctor   : 1,  /* __attribute__((constructor)) */
    func_dtor   : 1,  /* __attribute__((destructor)) */
    func_args   : 8,  /* PE __stdcall 参数字节数 */
    func_alwinl : 1,  /* __attribute__((always_inline)) */
    xxxx        : 15;
};
```

**`type`（类型信息）**：`CType` 结构，如前所述。

**`next`（相关符号链）**：用于链接结构体的成员列表、函数的参数列表。`next` 链接的是语义上相关的符号，而非作用域上的邻居。

**`prev`（栈前驱）**：链接到同一符号栈中的前一个符号。这是符号栈的核心链接方式——`global_stack` 和 `local_stack` 都是通过 `prev` 字段形成的链表。

**`prev_tok`（同名前驱）**：链接到同名符号的上一个定义。这是实现名称遮蔽（name shadowing）的关键。

### 4.4.2 三个符号栈

tcc 维护三个全局符号栈（`tccgen.c` 第 36 行）：

```c
ST_DATA Sym *global_stack;    /* 全局符号栈 */
ST_DATA Sym *local_stack;     /* 局部符号栈 */
ST_DATA Sym *define_stack;    /* 预处理器宏定义栈 */
```

此外还有两个标签栈：

```c
ST_DATA Sym *global_label_stack;  /* 全局标签栈 */
ST_DATA Sym *local_label_stack;   /* 局部标签栈 */
```

**global_stack**：存储全局变量和函数声明。在整个编译过程中持续存在，不随函数结束而清除。

**local_stack**：存储当前函数的局部变量和参数。函数编译开始时为空，编译结束时全部弹出。`local_stack` 非空也作为"当前是否在函数体内"的标志——`sym_push()` 根据它决定将符号压入哪个栈：

```c
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    /* ... */
}
```

**define_stack**：存储预处理器宏定义。每个宏定义对应一个 `Sym`，其 `d` 字段指向宏的替换记号流。

### 4.4.3 符号查找：记号表与双向链接

tcc 的符号查找不是遍历符号栈，而是通过记号表（`TokenSym`）直接定位。每个标识符在词法分析阶段就被分配了一个 `TokenSym` 结构：

```c
typedef struct TokenSym {
    struct TokenSym *hash_next;  /* 哈希链 */
    struct Sym *sym_define;      /* 指向宏定义 */
    struct Sym *sym_label;       /* 指向标签 */
    struct Sym *sym_struct;      /* 指向结构体定义 */
    struct Sym *sym_identifier;  /* 指向变量/函数定义 */
    int tok;                     /* 记号编号 */
    int len;                     /* 名称长度 */
    char str[1];                 /* 名称字符串（柔性数组） */
} TokenSym;
```

四个 `Sym*` 指针分别指向该标识符在四个命名空间中的当前定义。查找一个标识符只需一次数组访问：

```c
ST_INLN Sym *sym_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}
```

`table_ident` 是一个全局数组，以记号编号为索引。`v - TOK_IDENT` 就是数组下标。

### 4.4.4 sym_push 与 sym_pop：栈操作

**`sym_push()`** 将一个新符号压入当前栈（`tccgen.c` 第 715 行）：

```c
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
    s->r = r;
    /* 非匿名符号才记录到记号表 */
    if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        sym_link(s, 1);  /* 将 s 链接到 table_ident */
        /* 检查同作用域重定义 */
        if (s->prev_tok && sym_scope_ex(s->prev_tok) == local_scope)
            tcc_error("redeclaration of '%s'", get_tok_str(s->v, NULL));
    }
    return s;
}
```

关键步骤：
1. `sym_push2()` 分配新 `Sym`，压入栈顶。
2. `sym_link(s, 1)` 将符号链接到 `table_ident` 中对应的 `sym_identifier`（或 `sym_struct`）。

**`sym_link()`** 的实现揭示了名称遮蔽的机制：

```c
static inline void sym_link(Sym *s, int yes)
{
    TokenSym *ts = table_ident[(s->v & ~SYM_STRUCT) - TOK_IDENT];
    Sym **ps;
    if (s->v & SYM_STRUCT)
        ps = &ts->sym_struct;
    else
        ps = &ts->sym_identifier;
    if (yes) {
        s->prev_tok = *ps, *ps = s;  /* 新符号成为栈顶 */
        s->sym_scope = local_scope;
    } else {
        *ps = s->prev_tok;           /* 恢复旧定义 */
    }
}
```

当 `sym_link(s, 1)` 时，新符号 `s` 被插入到 `sym_identifier` 链的头部，`s->prev_tok` 保存旧的头部。这样，`sym_find()` 总是找到最近的定义——这就是名称遮蔽的实现。

当 `sym_link(s, 0)` 时（符号被弹出），旧的定义通过 `prev_tok` 恢复。

**`sym_pop()`** 弹出符号直到指定的边界（`tccgen.c` 第 762 行）：

```c
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep)
{
    Sym *s, *ss;
    int v;
    s = *ptop;
    while(s != b) {
        ss = s->prev;
        v = s->v;
        /* 从记号表中移除符号 */
        if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_link(s, 0);
        if (!keep)
            sym_free(s);
        s = ss;
    }
    if (!keep)
        *ptop = b;
}
```

`keep` 参数在处理语句表达式（statement expression）和 `for` 循环中的 VLA 时使用：这些场景下符号需要从查找表中移除（`sym_link(s, 0)`），但不能从栈中释放（因为还有引用）。

### 4.4.5 作用域管理

tcc 的作用域管理由 `struct scope` 结构和 `local_scope` 计数器共同完成（`tccgen.c` 第 121 行）：

```c
static struct scope {
    struct scope *prev;              /* 外层作用域 */
    struct { int loc, locorig, num; } vla;  /* VLA 管理 */
    struct { Sym *s; int n; } cl;    /* cleanup 函数链 */
    int *bsym;                       /* break 跳转链 */
    int *csym;                       /* continue 跳转链 */
    Sym *lstk;                       /* 进入作用域时的 local_stack 栈顶 */
    Sym *llstk;                      /* 进入作用域时的 local_label_stack 栈顶 */
} *cur_scope, *loop_scope, *root_scope;

static int local_scope;  /* 当前作用域深度，函数外为 0 */
```

**`new_scope()`** 进入一个新的复合语句作用域（`tccgen.c` 第 7073 行）：

```c
static void new_scope(struct scope *o)
{
    *o = *cur_scope;      /* 继承外层作用域的 bsym、csym 等 */
    o->prev = cur_scope;
    cur_scope = o;
    cur_scope->vla.num = 0;
    o->lstk = local_stack;        /* 记录当前栈顶 */
    o->llstk = local_label_stack; /* 记录当前标签栈顶 */
    ++local_scope;
}
```

**`prev_scope()`** 离开作用域（`tccgen.c` 第 7087 行）：

```c
static void prev_scope(struct scope *o, int is_expr)
{
    vla_leave(o->prev);
    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);   /* 调用 __attribute__((cleanup)) */
    label_pop(&local_label_stack, o->llstk, is_expr);
    sym_pop(&local_stack, o->lstk, is_expr);  /* 弹出局部符号 */
    cur_scope = o->prev;
    --local_scope;
}
```

`sym_pop(&local_stack, o->lstk, is_expr)` 弹出本作用域内声明的所有局部符号。由于 `sym_pop` 内部调用 `sym_link(s, 0)`，这些符号从记号表中移除，外层同名符号（如果有）自动恢复可见性——名称遮蔽就这样自动解除了。

**`new_scope_s()` / `prev_scope_s()`** 是简化版本，用于 `if`、`while`、`switch` 等不会引入新变量声明（但可能有结构体/枚举定义）的语句：

```c
static void new_scope_s(struct scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(struct scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}
```

### 4.4.6 符号内存管理

tcc 使用池化分配器管理 `Sym` 对象，避免频繁的 `malloc`/`free` 调用（`tccgen.c` 第 670 行）：

```c
static Sym *sym_free_first;  /* 空闲链表头 */
static void **sym_pools;     /* 内存池数组 */
static int nb_sym_pools;     /* 内存池数量 */

static inline Sym *sym_malloc(void)
{
    Sym *sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();  /* 分配新的内存池 */
    sym_free_first = sym->next;
    return sym;
}
```

`SYM_POOL_NB` 个 `Sym` 对象被一次性分配在一个内存池中，然后串成空闲链表。分配时从链表头取出，释放时放回链表头。这种方式既快速又避免了内存碎片。

## 4.5 表达式解析

表达式解析是编译器中最频繁执行的代码路径。tcc 的表达式解析器将源代码中的表达式转化为值栈上的操作和目标代码中的指令。

### 4.5.1 优先级爬升的完整流程

以表达式 `a + b * c == d && e || f` 为例，追踪 `expr_infix` 的执行过程。

入口调用：`expr_lor()` -> `unary()` 解析 `a`，然后 `expr_infix(1)`（优先级 1 开始）。

```
expr_infix(1):
  t = '+', p2 = 9, 9 >= 1: 进入循环
    非 &&/||，next()，unary() 解析 b
    precedence('*') = 10 > 9，递归 expr_infix(10)
      expr_infix(10):
        t = '*', p2 = 10, 10 >= 10: 进入循环
          next()，unary() 解析 c
          precedence('==') = 6, 6 > 10? 否，不递归
          gen_op('*')  -> b*c 压栈
        t = '==', p2 = 6, 6 >= 10? 否，退出循环
      返回
    gen_op('+')  -> a+b*c 压栈
  t = '==', p2 = 6, 6 >= 1: 继续循环
    非 &&/||，next()，unary() 解析 d
    precedence('&&') = 2, 2 > 6? 否，不递归
    gen_op('==')  -> a+b*c==d 压栈
  t = '&&', p2 = 2, 2 >= 1: 继续循环
    是 &&，进入 expr_landor(TOK_LAND)  -> 短路求值处理
  ...
```

### 4.5.2 短路求值

`&&` 和 `||` 的处理由 `expr_landor()` 完成（`tccgen.c` 第 6565 行），它实现了 C 语言规定的短路求值语义：

```c
static void expr_landor(int op)
{
    int t = 0, cc = 1, f = 0, i = op == TOK_LAND, c;
    for(;;) {
        c = f ? i : condition_3way();  /* 尝试静态求值 */
        if (c < 0)
            save_regs(1), cc = 0;      /* 无法静态求值，保存寄存器 */
        else if (c != i)
            nocode_wanted++, f = 1;    /* 短路：结果已确定 */
        if (tok != op)
            break;
        if (c < 0)
            t = gvtst(i, t);           /* 生成条件跳转 */
        else
            vpop();
        next();
        expr_landor_next(op);
    }
    if (cc || f) {
        vpop();
        vpushi(i ^ f);                 /* 布尔结果 */
        gsym(t);                       /* 补丁跳转目标 */
        nocode_wanted -= f;
    } else {
        gvtst_set(i, t);              /* 保留跳转链供后续使用 */
    }
}
```

对于 `a && b && c`：

1. 解析 `a`，`condition_3way()` 尝试判断是否为常量。
2. 如果 `a` 的值在编译时未知（`c < 0`），调用 `save_regs(1)` 确保当前值已存入寄存器，然后 `gvtst(1, 0)` 生成"如果为假则跳转"的指令。跳转目标暂时未知，记为 `t`。
3. 解析 `b`，同样处理。
4. 解析 `c`。
5. 所有子表达式处理完后，`gsym(t)` 将跳转链补丁为当前代码位置。

对于 `a || b`：

1. 解析 `a`，生成"如果为真则跳转"的指令（跳过 `b` 的求值）。
2. 解析 `b`。
3. 结果在跳转链上合并。

`condition_3way()` 函数尝试在编译时确定条件的真假：

```c
static int condition_3way(void)
{
    int c = -1;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
        (!(vtop->r & VT_SYM) || !vtop->sym->a.weak)) {
        vdup();
        gen_cast_s(VT_BOOL);
        c = vtop->c.i;
        vpop();
    }
    return c;  /* -1: 未知, 0: 假, 1: 真 */
}
```

如果条件是编译时常量，可以直接决定短路，避免生成无用的跳转指令。

### 4.5.3 unary()：一元表达式解析

`unary()` 是表达式解析的核心函数（`tccgen.c` 第 5591 行），处理所有"原子"表达式和后缀操作。它是一个约 700 行的 switch-case 语句。

**字面量处理：**

```c
case TOK_CINT:
case TOK_CCHAR:
    t = VT_INT;
    type.t = t;
    vsetc(&type, VT_CONST, &tokc);  /* 将常量压入值栈 */
    next();
    break;
case TOK_CLLONG:
    t = VT_LLONG;
    goto push_tokc;
case TOK_CFLOAT:
    t = VT_FLOAT;
    goto push_tokc;
/* ... */
```

字面量直接作为 `VT_CONST` 压入值栈，不生成任何代码。

**字符串字面量：**

```c
case TOK_STR:
    t = char_type.t;
    type.t = t;
    mk_pointer(&type);
    type.t |= VT_ARRAY;
    memset(&ad, 0, sizeof(AttributeDef));
    ad.section = rodata_section;
    decl_initializer_alloc(&type, &ad, VT_CONST, 2, 0, 0);
    break;
```

字符串字面量被分配到 `.rodata` 段，生成一个指向该段的指针常量。

**标识符处理：**

```c
default:
tok_identifier:
    if (tok < TOK_UIDENT)
        tcc_error("expression expected before '%s'", get_tok_str(tok, &tokc));
    t = tok;
    next();
    s = sym_find(t);
    if (!s || IS_ASM_SYM(s)) {
        const char *name = get_tok_str(t, NULL);
        if (tok != '(')
            tcc_error("'%s' undeclared", name);
        /* 隐式函数声明：兼容 K&R C */
        if (!func_old)
            tcc_warning_c(warn_implicit_function_declaration)(
                "implicit declaration of function '%s'", name);
        s = external_global_sym(t, &func_old_type);
    }

    r = s->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;  /* 寄存器变量 */

    vset(&s->type, r, s->c);
    vtop->sym = s;

    if (r & VT_SYM) {
        vtop->c.i = 0;
    } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
        vtop->c.i = s->enum_val;  /* 枚举常量 */
    }
    break;
```

标识符查找的关键路径：
1. `sym_find(t)` 在记号表中查找。
2. 如果未找到且下一个记号是 `(`，按隐式函数声明处理（`int()` 类型）。
3. 否则报错"未声明"。
4. 将符号的类型和位置信息压入值栈。

**sizeof 和 alignof：**

```c
case TOK_SIZEOF:
case TOK_ALIGNOF1: case TOK_ALIGNOF2: case TOK_ALIGNOF3:
    t = tok;
    next();
    if (tok == '(')
        tok = TOK_SOTYPE;      /* 特殊标记，区分 sizeof(type) 和 sizeof expr */
    expr_type(&type, unary);   /* 在 nocode_wanted 模式下求类型 */
    if (t == TOK_SIZEOF) {
        vpush_type_size(&type, &align);
        gen_cast_s(VT_SIZE_T);
    } else {
        type_size(&type, &align);
        vpushs(align);
    }
    break;
```

`expr_type()` 在 `nocode_wanted` 模式下调用 `unary()`，这样可以获取表达式的类型而不生成任何代码。

**一元运算符：**

```c
case '*':     /* 解引用 */
    next(); unary(); indir(); break;
case '&':     /* 取地址 */
    next(); unary();
    if ((vtop->type.t & VT_BTYPE) != VT_FUNC &&
        !(vtop->type.t & (VT_ARRAY | VT_VLA)))
        test_lvalue();
    if (vtop->sym)
        vtop->sym->a.addrtaken = 1;
    mk_pointer(&vtop->type);
    gaddrof();
    break;
case '!':     /* 逻辑非 */
    next(); unary(); gen_test_zero(TOK_EQ); break;
case '~':     /* 按位取反 */
    next(); unary(); vpushi(-1); gen_op('^'); break;
case '-':     /* 一元负号 */
    next(); unary();
    if (is_float(vtop->type.t))
        gen_opif(TOK_NEG);
    else {
        vpushi(0); vswap(); gen_op('-');
    }
    break;
case TOK_INC:  /* 前缀 ++ */
case TOK_DEC:  /* 前缀 -- */
    t = tok; next(); unary(); inc(0, t); break;
```

### 4.5.4 后缀操作：数组下标、成员访问、函数调用

`unary()` 的后半部分处理所有后缀操作（`tccgen.c` 第 6238 行）：

```c
/* 后缀操作循环 */
while (1) {
    if (tok == TOK_INC || tok == TOK_DEC) {
        inc(1, tok);  /* 后缀 ++/-- */
        next();
    } else if (tok == '.' || tok == TOK_ARROW) {
        /* 成员访问 */
        if (tok == TOK_ARROW)
            indir();              /* -> 先解引用 */
        qualifiers = vtop->type.t & (VT_CONSTANT | VT_VOLATILE);
        test_lvalue();
        next();
        s = find_field(&vtop->type, tok, &cumofs);  /* 查找成员 */
        gaddrof();                /* 取地址 */
        vtop->type = char_pointer_type;  /* 转为 char* */
        vpushi(cumofs);           /* 压入偏移量 */
        gen_op('+');              /* 地址 + 偏移量 */
        vtop->type = s->type;    /* 改为成员类型 */
        vtop->type.t |= qualifiers;
        if (!(vtop->type.t & VT_ARRAY))
            vtop->r |= VT_LVAL;
        next();
    } else if (tok == '[') {
        /* 数组下标 */
        next(); gexpr();
        gen_op('+');   /* 指针 + 偏移量 */
        indir();       /* 解引用 */
        skip(']');
    } else if (tok == '(') {
        /* 函数调用 */
        /* ... 见下文 ... */
    } else break;
}
```

成员访问（`.` 和 `->`）的实现巧妙地利用了指针算术：先取结构体地址（`gaddrof`），转为 `char *`，加上成员偏移量，再转为成员类型。这避免了为结构体成员访问生成专门的指令。

数组下标 `a[i]` 被等价转换为 `*(a + i)`。

### 4.5.5 函数调用的代码生成

函数调用是 `unary()` 中最复杂的后缀操作（`tccgen.c` 第 6286 行）。核心流程：

```c
} else if (tok == '(') {
    SValue ret;
    Sym *sa;
    int nb_args, ret_nregs, ret_align, regsize, variadic;

    /* 检查是否为函数类型（或函数指针） */
    if ((vtop->type.t & VT_BTYPE) != VT_FUNC) {
        if ((vtop->type.t & (VT_BTYPE | VT_ARRAY)) == VT_PTR) {
            vtop->type = *pointed_type(&vtop->type);
            if ((vtop->type.t & VT_BTYPE) != VT_FUNC)
                goto error_func;
        } else {
            error_func:
            expect("function pointer");
        }
    }

    s = vtop->type.ref;     /* 函数符号（含参数列表） */
    next();
    sa = s->next;            /* 第一个参数 */
    nb_args = 0;

    /* 处理结构体返回值的隐式参数 */
    if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
        variadic = (s->f.func_type == FUNC_ELLIPSIS);
        ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                               &ret_align, &regsize);
        if (ret_nregs <= 0) {
            /* 通过隐式指针返回结构体 */
            size = type_size(&s->type, &align);
            loc = (loc - size) & -align;
            ret.type = s->type;
            ret.r = VT_LOCAL | VT_LVAL;
            vseti(VT_LOCAL, loc);
            ret.c = vtop->c;
            nb_args++;
        }
    }

    /* 解析实参 */
    if (tok != ')') {
        for(;;) {
            expr_eq();
            gfunc_param_typed(s, sa);  /* 类型检查和转换 */
            nb_args++;
            if (sa) sa = sa->next;
            if (tok == ')') break;
            skip(',');
        }
    }
    skip(')');

    /* 生成调用指令 */
    gfunc_call(nb_args);

    /* 处理返回值 */
    if (ret_nregs == 0) {
        vset(&ret.type, ret.r, ret.c.i);
        vtop->sym = NULL;
    } else {
        /* 从返回寄存器中取值 */
        vset(&ret.type, VT_CONST, 0);
        PUT_R_RET(vtop, ret.type.t);
    }
}
```

函数调用的关键步骤：

1. **解析函数表达式**：可能是标识符（直接调用）或指针（间接调用）。
2. **处理结构体返回**：如果函数返回结构体，需要在栈上分配空间并传递隐式指针。
3. **解析实参**：逐个解析参数表达式，调用 `gfunc_param_typed()` 进行类型检查和隐式转换（如 `float` 提升为 `double`）。
4. **生成调用**：`gfunc_call()` 将参数加载到正确的寄存器或栈位置，然后生成 `call` 指令。
5. **处理返回值**：将返回寄存器的值压入值栈。

### 4.5.6 赋值与逗号表达式

`expr_eq()` 处理赋值（`tccgen.c` 第 6733 行）：

```c
static void expr_eq(void)
{
    int t;
    expr_cond();
    if ((t = tok) == '=' || TOK_ASSIGN(t)) {
        test_lvalue();
        next();
        if (t == '=') {
            expr_eq();              /* 右递归：右结合 */
        } else {
            vdup();                 /* 复制左值 */
            expr_eq();
            gen_op(TOK_ASSIGN_OP(t)); /* 复合赋值：如 += 变成 + */
        }
        vstore();                   /* 存储到左值 */
    }
}
```

赋值是右结合的（`a = b = c` 等价于 `a = (b = c)`），所以右边用 `expr_eq()` 递归而非 `expr_cond()`。

`gexpr()` 处理逗号表达式：

```c
ST_FUNC void gexpr(void)
{
    expr_eq();
    if (tok == ',') {
        do {
            vpop();      /* 丢弃前一个表达式的值 */
            next();
            expr_eq();
        } while (tok == ',');
    }
}
```

逗号运算符的实现极其简洁：左侧表达式求值后直接丢弃（`vpop`），只保留最后一个表达式的值。

## 4.6 语句解析：block()

`block()` 函数是语句解析的核心（`tccgen.c` 第 7172 行），它通过一个大型的 `if-else if` 链处理所有 C 语句类型。我们逐一分析。

### 4.6.1 if/else 语句

```c
if (t == TOK_IF) {
    new_scope_s(&o);
    skip('(');
    gexpr_decl();                /* 解析条件（支持 C2y 声明） */
    a = gvtst(1, 0);            /* 条件为假时跳转，a = 待补丁位置 */
    skip(')');
    block(0);                    /* then 分支 */
    if (tok == TOK_ELSE) {
        d = gjmp(0);            /* 跳过 else 分支，d = 待补丁位置 */
        gsym(a);                 /* 补丁：假跳转目标 = 此处 */
        next();
        block(0);                /* else 分支 */
        gsym(d);                 /* 补丁：跳过 else 的跳转目标 = 此处 */
    } else {
        gsym(a);                 /* 补丁：假跳转目标 = 此处 */
    }
    prev_scope_s(&o);
}
```

生成的代码结构：

```
    [条件求值]
    jz .L_then_end      ; a: 条件为假跳转
    [then 分支代码]
    jmp .L_if_end        ; d: 跳过 else
.L_then_end:            ; gsym(a) 补丁
    [else 分支代码]
.L_if_end:              ; gsym(d) 补丁
```

`gvtst(inv, t)` 是一个关键的辅助函数。当 `inv=1` 时，它在条件为假时跳转；当 `inv=0` 时，在条件为真时跳转。它能智能地利用比较指令的条件码（`VT_CMP`）和已有的跳转链（`VT_JMP`/`VT_JMPI`），避免冗余的比较和跳转。

### 4.6.2 while 循环

```c
} else if (t == TOK_WHILE) {
    new_scope_s(&o);
    d = gind();                  /* 循环起始位置（回边目标） */
    skip('(');
    gexpr();                     /* 条件表达式 */
    a = gvtst(1, 0);            /* 条件为假时跳出循环 */
    skip(')');
    b = 0;                       /* break 链初始化 */
    lblock(&a, &b);              /* 循环体，设置 bsym/csym */
    gjmp_addr(d);                /* 无条件跳回循环顶部 */
    gsym_addr(b, d);             /* break 跳转目标 = 循环之后 */
    gsym(a);                     /* 条件假跳转目标 = 循环之后 */
    prev_scope_s(&o);
}
```

`lblock()` 设置循环的 `bsym`（break 跳转链）和 `csym`（continue 跳转链）：

```c
static void lblock(int *bsym, int *csym)
{
    struct scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}
```

while 循环中 `csym` 为 NULL（continue 也跳到循环顶部，由 `gjmp_addr(d)` 处理），而 `do-while` 和 `for` 循环需要显式的 `csym`。

### 4.6.3 do-while 循环

```c
} else if (t == TOK_DO) {
    new_scope_s(&o);
    a = b = 0;
    d = gind();                  /* 循环体起始位置 */
    lblock(&a, &b);              /* 循环体 */
    gsym(b);                     /* continue 目标 = 条件求值处 */
    skip(TOK_WHILE);
    skip('(');
    gexpr();                     /* 条件表达式 */
    c = gvtst(0, 0);            /* 条件为真时跳回循环顶部 */
    skip(')');
    skip(';');
    gsym_addr(c, d);             /* 补丁：真跳转目标 = 循环体起始 */
    gsym(a);                     /* break 目标 = 循环之后 */
    prev_scope_s(&o);
}
```

注意 do-while 的 `gvtst(0, 0)`（`inv=0`）：条件为真时跳回，与 while 的 `gvtst(1, 0)`（条件为假时跳出）相反。

### 4.6.4 for 循环

```c
} else if (t == TOK_FOR) {
    new_scope(&o);               /* 注意：用 new_scope 而非 new_scope_s */

    skip('(');
    if (tok != ';') {
        /* C99: for 循环初始化可以是声明 */
        if (!decl(VT_JMP)) {
            gexpr();             /* 普通初始化表达式 */
            vpop();
        }
    }
    skip(';');
    a = b = 0;
    c = d = gind();              /* 条件检查位置 */
    if (tok != ';') {
        gexpr();
        a = gvtst(1, 0);        /* 条件为假时跳出 */
    }
    skip(';');
    if (tok != ')') {
        e = gjmp(0);            /* 跳到循环体 */
        d = gind();              /* 自增表达式位置 */
        gexpr();
        vpop();
        gjmp_addr(c);            /* 跳回条件检查 */
        gsym(e);                 /* 补丁：跳到循环体 */
    }
    skip(')');
    lblock(&a, &b);              /* 循环体 */
    gjmp_addr(d);                /* 跳到自增表达式（或条件检查） */
    gsym_addr(b, d);             /* continue 目标 = 自增表达式 */
    gsym(a);                     /* break 目标 */
    prev_scope(&o, 0);
}
```

for 循环使用 `new_scope`（而非 `new_scope_s`），因为 C99 允许在初始化部分声明变量。`decl(VT_JMP)` 尝试解析声明；如果成功（返回非零），声明的变量在 for 循环的作用域内有效，离开 `prev_scope` 时自动弹出。

生成的代码结构：

```
    [初始化]
L_cond:                         ; c: 条件检查位置
    [条件求值]
    jz .L_end                   ; a: 条件为假跳转
    jmp .L_body                 ; e: 跳到循环体
L_incr:                         ; d: 自增表达式位置
    [自增表达式]
    jmp L_cond                  ; 跳回条件检查
L_body:                         ; gsym(e) 补丁
    [循环体]
    jmp L_incr                  ; 跳到自增
L_end:                          ; gsym(a) 补丁
```

### 4.6.5 switch 语句

switch 语句的实现是 `block()` 中最复杂的部分。tcc 使用排序 + 二分查找来生成 case 分支的跳转表。

```c
} else if (t == TOK_SWITCH) {
    struct switch_t *sw;
    sw = tcc_mallocz(sizeof *sw);
    sw->bsym = &a;
    sw->scope = cur_scope;
    sw->prev = cur_switch;
    cur_switch = sw;             /* 链入 switch 栈 */

    new_scope_s(&o);
    skip('(');
    gexpr_decl();                /* switch 表达式 */
    skip(')');
    sw->sv = *vtop--;           /* 保存 switch 值 */
    a = 0;
    b = gjmp(0);                /* 跳到第一个 case */
    lblock(&a, NULL);            /* switch 体（收集 case 标签） */
    a = gjmp(a);                 /* 隐式 break */

    gsym(b);                     /* 跳到 case 查找 */
    prev_scope_s(&o);

    /* 对 case 值排序 */
    case_sort(sw);

    /* 生成二分查找代码 */
    vpushv(&sw->sv);
    gv(RC_INT);                  /* 将 switch 值加载到寄存器 */
    d = gcase(sw->p, sw->n, 0); /* 二分查找 */
    vpop();

    if (sw->def_sym)
        gsym_addr(d, sw->def_sym);  /* 有 default：跳到 default */
    else
        gsym(d);                     /* 无 default：跳出 switch */

    gsym(a);                     /* break 目标 */
    end_switch();
}
```

**case 收集**：每个 `case` 标签在解析时创建一个 `struct case_t`，添加到 `cur_switch->p` 数组中：

```c
} else if (t == TOK_CASE) {
    struct case_t *cr;
    cr = tcc_malloc(sizeof(struct case_t));
    dynarray_add(&cur_switch->p, &cur_switch->n, cr);
    cr->v1 = cr->v2 = value64(expr_const64(), t);
    if (tok == TOK_DOTS && gnu_ext) {
        next();
        cr->v2 = value64(expr_const64(), t);  /* GCC case 范围扩展 */
    }
    cr->ind = gind();            /* case 代码的起始位置 */
    skip(':');
}
```

**case 排序**（`case_sort()`）：按 case 值排序，检测重复值，合并相邻的连续 case：

```c
static void case_sort(struct switch_t *sw)
{
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    /* 合并相邻的连续 case（如 case 1: case 2: case 3:） */
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            p[1]->v1 = p[0]->v1;  /* 合并为范围 */
            /* ... */
        }
    }
}
```

**二分查找代码生成**（`gcase()`）：递归地生成二分查找逻辑（`tccgen.c` 第 6933 行）：

```c
static int gcase(struct case_t **base, int len, int dsym)
{
    while (len) {
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        if (l2 == 0 && p->v1 == p->v2) {
            /* 单个 case 值：直接比较 */
            gen_op(TOK_EQ);
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case 范围：v1 <= x <= v2 */
            gen_op(TOK_GT);      /* > v2 则跳过 */
            /* ... */
            gen_op(TOK_GE);      /* >= v1 则跳入 */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);  /* 递归处理左半部分 */
        }
        ++l2, base += l2, len -= l2;
    }
    return gjmp(dsym);
}
```

当 case 数量超过 8 个时使用二分查找，否则使用线性查找。这比生成跳转表简单得多，适合 tcc 追求简洁的设计哲学。

### 4.6.6 return 语句

```c
} else if (t == TOK_RETURN) {
    b = (func_vt.t & VT_BTYPE) != VT_VOID;
    if (tok != ';') {
        gexpr();
        if (b) {
            gen_assign_cast(&func_vt);  /* 转换为函数返回类型 */
        } else {
            if (vtop->type.t != VT_VOID)
                tcc_warning("void function returns a value");
            vtop--;
        }
    } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
        vpushi(0);              /* 老式函数默认返回 0 */
    } else if (b) {
        tcc_warning("'return' with no value");
        b = 0;
    }
    leave_scope(root_scope);    /* 执行所有 cleanup */
    if (b)
        gfunc_return(&func_vt); /* 生成返回指令 */
    skip(';');
    if (tok != '}' || local_scope != 1)
        rsym = gjmp(rsym);      /* 跳到函数末尾（如果不是最后一条语句） */
    CODE_OFF();                 /* 关闭代码生成（不可达代码） */
}
```

`gfunc_return()` 将值栈顶的值加载到返回寄存器中。对于结构体返回值，处理方式因 ABI 而异。

`rsym` 是返回跳转链：所有 `return` 语句都跳到同一个位置（函数末尾），由 `gsym(rsym)` 在 `gen_function()` 中统一补丁。

### 4.6.7 break 与 continue

```c
} else if (t == TOK_BREAK) {
    if (!cur_scope->bsym)
        tcc_error("cannot break");
    if (cur_switch && cur_scope->bsym == cur_switch->bsym)
        leave_scope(cur_switch->scope);
    else
        leave_scope(loop_scope);
    *cur_scope->bsym = gjmp(*cur_scope->bsym);  /* 加入 break 跳转链 */
    skip(';');

} else if (t == TOK_CONTINUE) {
    if (!cur_scope->csym)
        tcc_error("cannot continue");
    leave_scope(loop_scope);
    *cur_scope->csym = gjmp(*cur_scope->csym);  /* 加入 continue 跳转链 */
    skip(';');
}
```

`bsym` 和 `csym` 是跳转链的头指针。每次 `break`/`continue` 生成一个前向跳转，跳转目标通过链表传递给外层的循环/switch 结构体，最终由 `gsym()` 统一补丁。

`leave_scope()` 负责在跳出前执行必要的清理工作（如 VLA 的栈恢复和 `__attribute__((cleanup))` 函数调用）。

### 4.6.8 goto 语句与标签

```c
} else if (t == TOK_GOTO) {
    vla_restore(cur_scope->vla.locorig);
    if (tok == '*' && gnu_ext) {
        /* 计算 goto（GCC 扩展） */
        next(); gexpr();
        ggoto();
    } else if (tok >= TOK_UIDENT) {
        s = label_find(tok);
        if (!s)
            s = label_push(&global_label_stack, tok, LABEL_FORWARD);
        else if (s->r == LABEL_DECLARED)
            s->r = LABEL_FORWARD;

        if (s->r & LABEL_FORWARD) {
            /* 前向引用：加入跳转链 */
            s->jnext = gjmp(s->jnext);
        } else {
            /* 后向引用：直接跳到已知位置 */
            gjmp_addr(s->jind);
        }
        next();
    }
    skip(';');
```

标签的定义：

```c
if (tok == ':' && t >= TOK_UIDENT) {
    next();
    s = label_find(t);
    if (s) {
        if (s->r == LABEL_DEFINED)
            tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
        if (s->r == LABEL_FORWARD) {
            /* 补丁所有前向 goto */
            gsym(s->jnext);
            s->r = LABEL_DEFINED;
        }
    } else {
        s = label_push(&global_label_stack, t, LABEL_DEFINED);
    }
    s->jind = gind();  /* 记录标签位置 */
}
```

标签管理使用 `LABEL_DEFINED`、`LABEL_FORWARD`、`LABEL_DECLARED` 三种状态。前向 goto 创建一个跳转链（通过 `jnext` 字段），标签定义时统一补丁。这种前向引用 + 延迟补丁的模式在编译器中极为常见。

## 4.7 声明解析

声明解析是 C 语言编译器中最复杂的部分之一，因为 C 的声明语法天然地将类型信息嵌入在变量名周围。tcc 的声明解析分为三个层次：`decl()` -> `parse_btype()` + `type_decl()`。

### 4.7.1 decl()：顶层声明解析

`decl()` 是声明解析的入口（`tccgen.c` 第 8733 行），参数 `l` 指定声明的上下文：

- `VT_CONST`：全局作用域（文件作用域）
- `VT_LOCAL`：局部作用域（函数体内）
- `VT_JMP`：for 循环初始化（C99 声明）
- `VT_CMP`：旧式函数参数声明

```c
static int decl(int l)
{
    int v, has_init, r, oldint;
    CType type, btype;
    Sym *sym;
    AttributeDef ad, adbase;

    while (1) {
        oldint = 0;
        if (!parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP) return 0;     /* for 循环中无声明 */
            if (tok == ';' && l != VT_CMP) {
                next(); continue;           /* 跳过空声明 */
            }
            if (l != VT_CONST) break;       /* 局部区域：不是声明 */
            if (tok >= TOK_UIDENT) {
                btype.t = VT_INT;           /* K&R 隐式 int */
                oldint = 1;
            } else {
                break;
            }
        }

        /* 解析声明中的每个变量 */
        while (1) {
            type = btype;
            ad = adbase;
            type_decl(&type, &ad, &v, ...); /* 解析声明符 */

            if ((type.t & VT_BTYPE) == VT_FUNC) {
                /* 函数声明或定义 */
                if (tok == '{') {
                    /* 函数定义 */
                    sym = external_sym(v, &type, 0, &ad);
                    if (sym->type.t & VT_INLINE) {
                        /* inline 函数：保存记号流，稍后按需生成 */
                        skip_or_save_block(&fn->func_str);
                    } else {
                        cur_text_section = ...;
                        gen_function(sym);
                    }
                    break;
                }
            }

            if (type.t & VT_TYPEDEF) {
                /* typedef */
                sym = sym_push(v, &type, 0, 0);
            } else if ((type.t & VT_EXTERN) || ...) {
                /* 外部声明 */
                type.t |= VT_EXTERN;
                external_sym(v, &type, r, &ad);
            } else {
                /* 变量定义（可能有初始化器） */
                decl_initializer_alloc(&type, &ad, r, has_init, v, l);
            }

            if (tok != ',') {
                skip(';');
                break;
            }
            next();  /* 逗号：继续下一个声明符 */
        }
    }
    return 0;
}
```

`decl()` 的主循环不断尝试解析声明，直到遇到非声明内容（如语句或文件结束）。每个声明可能包含多个用逗号分隔的声明符（如 `int a, *p, arr[10];`）。

### 4.7.2 parse_btype()：基本类型解析

`parse_btype()` 用一个 `while(1)` 循环配合 `switch` 语句来解析类型说明符和限定符（`tccgen.c` 第 4719 行）。这个循环之所以必要，是因为 C 语言允许类型说明符以任意顺序出现：

```c
unsigned long long int x;   /* 合法 */
long int unsigned long y;   /* 也合法（虽然不推荐） */
const static volatile int z; /* 限定符和存储类可以交错 */
```

`parse_btype()` 的核心逻辑：

```c
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label)
{
    int t, u, bt, st, type_found, typespec_found;
    type_found = 0;
    t = VT_INT;           /* 默认类型 */
    bt = st = -1;         /* bt: 基本类型，st: short/long */
    type->ref = NULL;

    while(1) {
        switch(tok) {
        /* 基本类型 */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    tcc_error("too many basic types");
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;

        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                /* long double */
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                /* long long */
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;

        case TOK_INT:    u = VT_INT;   goto basic_type;
        case TOK_SHORT:  u = VT_SHORT; goto basic_type;
        case TOK_VOID:   u = VT_VOID;  goto basic_type;
        case TOK_FLOAT:  u = VT_FLOAT; goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;

        /* 结构体/联合体/枚举 */
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;

        /* 类型限定符 */
        case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_VOLATILE);
            t = type->t;
            next();
            break;

        /* 符号性 */
        case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3:
            t |= VT_DEFSIGN;
            next(); typespec_found = 1; break;
        case TOK_UNSIGNED:
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next(); typespec_found = 1; break;

        /* 存储类 */
        case TOK_EXTERN:  g = VT_EXTERN;  goto storage;
        case TOK_STATIC:  g = VT_STATIC;  goto storage;
        case TOK_TYPEDEF: g = VT_TYPEDEF; goto storage;
        storage:
            t |= g;
            next(); break;

        case TOK_INLINE1: case TOK_INLINE2: case TOK_INLINE3:
            t |= VT_INLINE;
            next(); break;

        /* 属性 */
        case TOK_ATTRIBUTE1: case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            break;

        default:
            goto the_end;
        }
        type_found = 1;
    }
the_end:
    type->t = t;
    return type_found;
}
```

`long` 的处理特别精巧：

- 第一次遇到 `long`：设 `st = VT_LONG`，`t` 包含 `VT_LONG` 位。
- 如果后面跟 `long`（即 `long long`）：清除 `VT_LONG` 位，设置 `VT_LLONG`。
- 如果后面跟 `double`（即 `long double`）：设置 `VT_LDOUBLE`。

`parse_btype_qualify()` 函数处理限定符的嵌套应用——当类型已经是数组时，限定符需要穿透到元素类型：

```c
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    type->t |= qualifiers;
}
```

### 4.7.3 type_decl()：声明符解析

`type_decl()` 解析 C 声明符（declarator）——即变量名周围的 `*`、`()`、`[]` 等（`tccgen.c` 第 5243 行）。

C 声明符的解析遵循"螺旋规则"（spiral rule），从变量名开始，先右后左地解析。tcc 的实现通过递归调用来处理嵌套：

```c
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td)
{
    CType *post, *ret;
    int qualifiers, storage;

    storage = type->t & VT_STORAGE;
    type->t &= ~VT_STORAGE;
    post = ret = type;

    /* 解析指针前缀 */
    while (tok == '*') {
        qualifiers = 0;
    redo:
        next();
        switch(tok) {
        case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
            qualifiers |= VT_CONSTANT; goto redo;
        case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
            qualifiers |= VT_VOLATILE; goto redo;
        case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
            goto redo;
        }
        mk_pointer(type);
        type->t |= qualifiers;
        if (ret == type)
            ret = pointed_type(type);  /* 记录最内层类型 */
    }

    /* 解析嵌套声明符或函数参数 */
    if (tok == '(') {
        if (!post_type(type, ad, 0, td)) {
            /* 不是函数参数列表，是嵌套括号 */
            parse_attribute(ad);
            post = type_decl(type, ad, v, td);  /* 递归 */
            skip(')');
        } else
            goto abstract;
    } else if (tok >= TOK_IDENT && (td & TYPE_DIRECT)) {
        *v = tok;     /* 变量名 */
        next();
    } else {
    abstract:
        if (!(td & TYPE_ABSTRACT))
            expect("identifier");
        *v = 0;       /* 抽象声明符（无名） */
    }

    /* 解析后缀：数组和函数参数 */
    post_type(post, ad, ...);
    parse_attribute(ad);
    type->t |= storage;
    return ret;
}
```

**解析 `int (*callback)(double)` 的过程：**

1. `parse_btype` 识别 `int`，`type->t = VT_INT`。
2. `type_decl` 进入，看到 `*`，调用 `mk_pointer`：`type->t = VT_PTR`，`ref->type = VT_INT`。
3. 看到 `(`，调用 `post_type` 返回 0（因为是 `(callback)` 而非参数列表）。
4. 递归调用 `type_decl`，看到 `callback`，`*v = 'callback'`。
5. 跳过 `)`。
6. 外层 `post_type` 处理 `(double)`：创建函数参数 Sym，`type->t = VT_FUNC`，`ref->next` 指向参数 `double`。
7. 最终返回最内层类型指针 `ret`。

### 4.7.4 post_type()：后缀类型解析

`post_type()` 处理函数参数列表和数组维度（`tccgen.c` 第 5026 行）：

```c
static int post_type(CType *type, AttributeDef *ad, int storage, int td)
{
    if (tok == '(') {
        /* 函数参数列表 */
        next();
        /* ... 解析参数列表 ... */
        /* 创建匿名 Sym 作为函数原型 */
        sr = sym_push2(ps, SYM_FIELD, 0, 0);
        /* ... 解析每个参数 ... */
        sr->type = *type;
        sr->f = ad->f;
        sr->next = first;       /* 参数链 */
        type->t = VT_FUNC;
        type->ref = sr;
        /* ... */
    } else if (tok == '[') {
        /* 数组维度 */
        next();
        if (tok != ']') {
            n = expr_const();   /* 全局：常量表达式 */
            /* 或 gexpr()       局部：可能有 VLA */
        }
        skip(']');
        post_type(type, ad, storage, ...);  /* 递归处理多维数组 */

        s = sym_push(SYM_FIELD, type, 0, n);
        type->t = VT_ARRAY | VT_PTR;
        type->ref = s;
    }
}
```

对于多维数组 `int a[3][5]`，`post_type` 递归处理：

1. 外层 `type_decl` 看到 `a[3]`：创建 `Sym{c=3, type=...}`，`type->t = VT_ARRAY|VT_PTR`。
2. 递归的 `post_type` 看到 `[5]`：在已有的数组类型上再包一层，创建 `Sym{c=5, type=VT_INT}`。
3. 最终结构：`VT_ARRAY|VT_PTR` -> `Sym{c=3}` -> `type = VT_ARRAY|VT_PTR` -> `Sym{c=5}` -> `type = VT_INT`。

### 4.7.5 gen_function()：函数定义的代码生成

当 `decl()` 遇到函数定义（`tok == '{'`）时，调用 `gen_function()` 生成函数体的代码（`tccgen.c` 第 8570 行）：

```c
static void gen_function(Sym *sym)
{
    struct scope f = { 0 };
    cur_scope = root_scope = &f;
    nocode_wanted = 0;                    /* 开启代码生成 */

    ind = cur_text_section->data_offset;
    funcname = get_tok_str(sym->v, NULL);
    func_ind = ind;
    func_vt = sym->type.ref->type;       /* 返回类型 */
    func_var = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
    func_old = sym->type.ref->f.func_type == FUNC_OLD;

    put_extern_sym(sym, cur_text_section, ind, 0);

    tcc_debug_funcstart(tcc_state, sym);

    /* 初始化局部符号栈 */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);  /* 哨兵 */
    local_scope = 1;
    sym_push_params(sym->type.ref);       /* 将参数压入局部栈 */

    local_scope = 0;
    rsym = 0;                             /* 返回跳转链初始化 */

    gfunc_prolog(sym);                    /* 生成函数序言（栈帧建立） */
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);                    /* 处理 VLA 参数 */
    block(0);                             /* 解析函数体 */
    gsym(rsym);                           /* 补丁所有 return 跳转 */
    nocode_wanted = 0;
    gfunc_epilog();                       /* 生成函数尾声（栈帧销毁） */

    tcc_debug_funcend(tcc_state, ind - func_ind);

    elfsym(sym)->st_size = ind - func_ind; /* 记录函数大小 */
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, NULL, 0);       /* 清理局部符号栈 */
    label_pop(&global_label_stack, NULL, 0);
    local_scope = 0;

    cur_text_section = NULL;
    funcname = "";
    nocode_wanted = DATA_ONLY_WANTED;     /* 恢复为全局模式 */
    check_vstack();                       /* 检查值栈平衡 */
    next();
}
```

函数编译的完整流程：

1. **初始化**：设置 `funcname`、`func_vt`（返回类型）、`func_var`（是否变参）等全局状态。
2. **参数入栈**：`sym_push_params()` 将函数参数从原型的 Sym 链表压入 `local_stack`。
3. **函数序言**：`gfunc_prolog()` 生成 `push %rbp; mov %rsp, %rbp; sub $N, %rsp` 等指令。
4. **解析函数体**：`block(0)` 递归解析所有语句和局部声明。
5. **补丁 return**：`gsym(rsym)` 将所有 `return` 语句的跳转目标补丁到此处。
6. **函数尾声**：`gfunc_epilog()` 生成 `leave; ret` 指令。
7. **清理**：弹出所有局部符号，恢复全局状态。

## 4.8 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 的语法分析器和类型系统，揭示了以下关键设计决策：

1. **单遍架构**：tcc 不构建 AST，而是在解析的同时直接生成目标代码。这种设计牺牲了优化能力，但换来了极快的编译速度和极低的内存消耗。

2. **值栈机制**：`SValue` 栈（`vstack`）替代了 AST 作为表达式的中间表示。每个表达式操作都是对栈顶值的变换。

3. **位域类型编码**：`CType.t` 用一个 32 位整数编码了基本类型、类型修饰符、存储类等所有类型信息，使得类型操作可以用高效的位运算完成。

4. **记号表双向链接**：`TokenSym` 中的 `sym_identifier`/`sym_struct` 指针与 `Sym` 中的 `prev_tok` 链接形成双向结构，实现了 O(1) 的符号查找和自动的名称遮蔽/恢复。

5. **前向引用与延迟补丁**：跳转指令的目标在生成时未知，通过链表（`rsym`、`bsym`、`csym`、`jnext`）记录待补丁位置，在目标确定后统一补丁。

6. **优先级爬升**：表达式解析使用优先级爬升算法，通过单个 `expr_infix()` 函数替代了传统递归下降的一系列函数，代码更紧凑。

7. **声明解析的三层次**：`decl()` -> `parse_btype()`（类型说明符）+ `type_decl()`（声明符），清晰地分离了"什么类型"和"叫什么名字"两个问题。

### 练习

**练习 4.1**（类型位域编码）：给定以下类型声明，写出 tcc 中 `CType.t` 的十六进制值（假设 `int` 为 32 位，`long` 为 64 位系统）。提示：参考 `tcc.h` 中的 `VT_*` 宏定义。详见 `exercises/ex1_types.md`。

**练习 4.2**（复杂声明解析）：追踪 `static const char *(* const arr[3])(int, ...)` 的解析过程，画出 Sym 链表结构。详见 `exercises/ex2_parse.md`。

**练习 4.3**（符号表变化追踪）：给定一段包含嵌套作用域的 C 代码，追踪每一步 `sym_push`/`sym_pop`/`sym_link` 操作后 `local_stack` 和 `table_ident[x]->sym_identifier` 的状态。详见 `exercises/ex3_scope.md`。

---

> **进一步阅读**
>
> - Aho, Lam, Sethi, Ullman. *Compilers: Principles, Techniques, and Tools* (2nd ed.), Chapter 4: Syntax Analysis.
> - Fabrice Bellard. "TCC: Tiny C Compiler". https://bellard.org/tcc/
> - tcc 源码 `tccgen.c` 中的 `#define precedence_parser` 及相关代码。
> - cdecl.org — 交互式 C 声明解析器，有助于理解复杂的声明语法。


===== FILE: docs/ch05/examples/asm_output.S =====
	.file	"vstack_trace.c"
	.text
	.globl	compute
	.type	compute, @function
/*
 * compute(int a, int b, int c)
 *
 * 函数序言 (gfunc_prolog + gfunc_epilog 回填):
 *   push %rbp; mov %rsp, %rbp; sub $32, %rsp
 *
 * 参数保存:
 *   a (edi) → [rbp-8]
 *   b (esi) → [rbp-16]
 *   c (edx) → [rbp-24]
 *
 * 局部变量 x 分配在 [rbp-32]
 */
compute:
	pushq	%rbp                       # 保存帧指针
	movq	%rsp, %rbp                 # 建立新帧
	subq	$32, %rsp                  # 分配局部空间 (4 × 8 字节对齐)
	movl	%edi, -8(%rbp)             # 保存参数 a
	movl	%esi, -16(%rbp)            # 保存参数 b
	movl	%edx, -24(%rbp)            # 保存参数 c

	# --- int x = a + b * c ---

	# STEP 4: b * c
	#   gv(RC_INT) for c: get_reg → rax
	movl	-24(%rbp), %eax            # load c → %eax (TREG_RAX)
	#   gv(RC_INT) for b: get_reg → rcx
	movl	-16(%rbp), %ecx            # load b → %ecx (TREG_RCX)
	#   gen_opi('*'): imul
	imull	%ecx, %eax                # %eax = b * c

	# STEP 5: a + (b * c)
	#   gv(RC_INT) for b*c: 已在 %eax, r_ok=1, 无需操作
	#   gv(RC_INT) for a: get_reg → rcx (rax 已被占用)
	movl	-8(%rbp), %ecx             # load a → %ecx
	#   gen_opi('+'): add
	addl	%ecx, %eax                 # %eax = a + b * c

	# STEP 6: x = result
	#   store(TREG_RAX, &x)
	movl	%eax, -32(%rbp)            # 存储结果到 x

	# STEP 7: return x
	#   gv(RC_IRET=RC_RAX): load x → %eax
	movl	-32(%rbp), %eax            # 加载返回值
	leave                              # mov %rbp, %rsp; pop %rbp
	ret
	.size	compute, .-compute


	.globl	logic_example
	.type	logic_example, @function
/*
 * logic_example(int a, int b, int c)
 *
 * 展示短路求值: (a > 0) && (b > 0)
 *
 * 跳转链追踪:
 *   TOK_LAND 的处理:
 *     1. 求值 a > 0 → 设置条件码
 *     2. gvtst(false, 0): 如果 a <= 0, 跳转到 .L_false
 *     3. 求值 b > 0 → 设置条件码
 *     4. gsym(.L_false): 回填跳转目标
 */
logic_example:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp
	movl	%edi, -4(%rbp)             # 保存 a
	movl	%esi, -8(%rbp)             # 保存 b
	movl	%edx, -12(%rbp)            # 保存 c

	# --- (a > 0) && (b > 0) ---

	# 求值 a > 0
	movl	-4(%rbp), %eax             # load a
	cmpl	$0, %eax                   # cmp $0, a
	setg	%al                        # setg %al (1 if a > 0)
	movzbl	%al, %ecx                 # 零扩展到 ecx

	# 短路检查: 如果 a <= 0, 直接跳到结果为 0
	testl	%ecx, %ecx
	je	.L_false                       # a <= 0 → 短路

	# 求值 b > 0
	movl	-8(%rbp), %eax             # load b
	cmpl	$0, %eax                   # cmp $0, b
	setg	%al                        # setg %al (1 if b > 0)
	movzbl	%al, %eax                 # 零扩展到 eax

	# 跳过 false 分支
	jmp	.L_done

.L_false:
	movl	$0, %eax                   # 结果为 0

.L_done:
	leave
	ret
	.size	logic_example, .-logic_example


	.globl	call_example
	.type	call_example, @function
/*
 * call_example(int a, int b)
 *
 * 展示函数调用: compute(a, b, a + b)
 *
 * SysV AMD64 调用约定:
 *   参数 0 → %rdi (a)
 *   参数 1 → %rsi (b)
 *   参数 2 → %rdx (a + b)
 *   返回值在 %eax
 *
 * gfunc_call 的处理顺序:
 *   1. save_regs(): 保存所有活跃寄存器
 *   2. 计算第三个参数 a + b (需要先于寄存器分配)
 *   3. 分配参数到寄存器:
 *      arg 0 (a)    → %edi
 *      arg 1 (b)    → %esi
 *      arg 2 (a+b)  → %edx
 *   4. 函数地址 → %rax
 *   5. call *%rax
 */
call_example:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	%edi, -4(%rbp)             # 保存 a
	movl	%esi, -8(%rbp)             # 保存 b

	# --- compute(a, b, a + b) ---

	# 计算第三个参数: a + b
	movl	-4(%rbp), %eax             # load a
	movl	-8(%rbp), %ecx             # load b
	addl	%ecx, %eax                 # a + b
	movl	%eax, %edx                 # 第三个参数 → %rdx

	# 准备前两个参数
	movl	-4(%rbp), %edi             # 第一个参数 a → %rdi
	movl	-8(%rbp), %esi             # 第二个参数 b → %rsi

	# 调用 compute
	# 注意: TCC 可能通过 GOT 间接加载函数地址
	call	compute                    # 直接调用 (同编译单元内)

	leave
	ret
	.size	call_example, .-call_example


	.globl	bitfield_example
	.type	bitfield_example, @function
/*
 * bitfield_example(struct Flags *f)
 *
 * 展示位域提取: f->mode (bit 1-3)
 *
 * struct Flags {
 *     unsigned int enabled : 1;   // bit 0
 *     unsigned int mode    : 3;   // bit 1-3
 *     unsigned int level   : 4;   // bit 4-7
 * };
 *
 * gv() 处理位域的过程:
 *   1. bit_pos = 1, bit_size = 3
 *   2. 加载完整字: mov (%rdi), %eax
 *   3. 右移 bit_pos 位: shr $1, %eax
 *      (现在 mode 在最低 3 位)
 *   4. 左移 (32 - bit_pos - bit_size) 位: shl $28, %eax
 *      (清除高位，保留符号位用于有符号位域)
 *   5. 算术右移 (32 - bit_size) 位: sar $29, %eax
 *      (符号扩展回 int)
 *
 * 注意: 这里 mode 是 unsigned，所以实际上使用
 *       movzbl 或 movl + and 掩码的方式更常见。
 *       TCC 的具体实现取决于类型标志。
 */
bitfield_example:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)             # 保存指针 f (64 位)

	# --- return f->mode ---

	# 加载 f 指向的字
	movq	-8(%rbp), %rax             # 加载指针 f
	movl	(%rax), %eax               # 解引用: 加载完整的 unsigned int

	# 提取位域 mode (bit 1-3, 无符号)
	# 方法: 右移 1 位, 然后与掩码 0x7 做 AND
	shrl	$1, %eax                   # 右移 bit_pos=1 位
	andl	$7, %eax                   # 掩码: (1 << 3) - 1 = 0x7

	leave
	ret
	.size	bitfield_example, .-bitfield_example


===== FILE: docs/ch05/examples/vstack_trace.c =====
/*
 * vstack_trace.c
 *
 * 第五章配套示例：带注释的 vstack 状态追踪
 *
 * 本文件展示 TinyCC 代码生成器在处理表达式 "int x = a + b * c;"
 * 时，虚拟栈（vstack）在每个步骤的状态变化。
 *
 * 目标平台：x86-64 (SysV ABI)
 * 假设 a, b, c 为 int 类型局部变量，x 为新声明的局部变量。
 *
 * 编译并查看汇编输出：
 *   tcc -S -o asm_output.S vstack_trace.c
 *
 * 本文件中的注释模拟了 TinyCC 内部代码生成器的操作序列。
 * 每个 STEP N 标记对应一次对 vstack 的操作。
 */

/* ===================================================================
 * 示例函数：我们将追踪此函数的代码生成过程
 * =================================================================== */

int compute(int a, int b, int c) {
    int x = a + b * c;
    return x;
}

/* ===================================================================
 * vstack 状态追踪
 *
 * SValue 结构体关键字段说明：
 *   type.t  : 类型标志 (VT_INT=3, VT_LLONG=4, VT_FLOAT=8, ...)
 *   r       : 值的位置 (VT_CONST=0x30, VT_LOCAL=0x32, VT_LVAL=0x100,
 *             物理寄存器 0-15)
 *   r2      : 第二个寄存器（long long 高字），未用时为 VT_CONST
 *   c.i     : 常量值或栈帧偏移量
 *   sym     : 符号引用（局部变量名）
 *
 * 位置编码速查：
 *   r & 0x3f = 0x00  → TREG_RAX  (物理寄存器 rax)
 *   r & 0x3f = 0x01  → TREG_RCX  (物理寄存器 rcx)
 *   r & 0x3f = 0x02  → TREG_RDX  (物理寄存器 rdx)
 *   r & 0x3f = 0x30  → VT_CONST  (编译时常量)
 *   r & 0x3f = 0x32  → VT_LOCAL  (栈帧偏移)
 *   r & 0x100        → VT_LVAL   (左值标志，需要解引用)
 *
 * 初始状态：
 *   vtop 指向 vstack[-1]（栈空）
 *   函数参数已通过 gfunc_prolog() 保存到栈帧：
 *     a → [rbp - 8]
 *     b → [rbp - 16]
 *     c → [rbp - 24]
 *   局部变量 x 将分配在 [rbp - 32]
 *
 * =================================================================== */


/*
 * === STEP 1: 解析标识符 'a' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 a 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=VT_LOCAL|VT_LVAL(0x132), c.i=-8, sym=a
 *       含义: a 是栈帧偏移 -8 处的 int 左值
 *
 * vtop → [0]
 */


/*
 * === STEP 2: 解析标识符 'b' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 b 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=0x132, c.i=-16, sym=b    ← vtop
 *   [1] type=VT_INT, r=0x132, c.i=-8,  sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 3: 解析标识符 'c' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 c 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=0x132, c.i=-24, sym=c    ← vtop
 *   [1] type=VT_INT, r=0x132, c.i=-16, sym=b
 *   [2] type=VT_INT, r=0x132, c.i=-8,  sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 4: 执行乘法 'b * c' ===
 * 调用链: gen_op('*') → gen_opic('*') → gen_opi('*')
 *
 * 4a. gen_op() 入口:
 *     - combine_types(): 两个 int → 结果 int
 *     - 不是指针操作，进入 std_op 路径
 *     - 非无符号，不修改操作符
 *     - gen_cast_s(VT_INT): 两个操作数已经是 int，无操作
 *     - 调用 gen_opic('*')
 *
 * 4b. gen_opic('*'):
 *     - c1 = 0 (b 不是常量), c2 = 0 (c 不是常量)
 *     - 不满足任何优化条件
 *     - 调用 gen_opi('*')
 *
 * 4c. gen_opi('*'):
 *     - ll = 0 (不是 long long)
 *     - cc = 0 (vtop 不是常量)
 *     - 调用 gv2(RC_INT, RC_INT)
 *
 * 4d. gv2(RC_INT, RC_INT):
 *     i. gv(RC_INT) for vtop[0] (c):
 *        - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0 (是左值)
 *        - get_reg(RC_INT) → 扫描寄存器:
 *          rax(0): 不在 vstack 中 → 分配 TREG_RAX
 *        - load(TREG_RAX, vtop):
 *          生成: mov -24(%rbp), %eax
 *        - vtop[0].r = TREG_RAX (0x0000)
 *
 *     ii. gv(RC_INT) for vtop[-1] (b):
 *         - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *         - get_reg(RC_INT) → 扫描寄存器:
 *           rax(0): 被 vtop[0] 占用 → 跳过
 *           rcx(1): 不在 vstack 中 → 分配 TREG_RCX
 *         - load(TREG_RCX, vtop[-1]):
 *           生成: mov -16(%rbp), %ecx
 *         - vtop[-1].r = TREG_RCX (0x0001)
 *
 * 4e. gen_opi('*') 生成乘法:
 *     生成: imul %ecx, %eax
 *     vtop--: 弹出 c
 *
 * vstack:
 *   [0] type=VT_INT, r=TREG_RAX(0x0000), c.i=0  ← vtop (b*c 的结果在 eax)
 *   [1] type=VT_INT, r=0x132, c.i=-8, sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 5: 执行加法 'a + (b*c)' ===
 * 调用链: gen_op('+') → gen_opic('+') → gen_opi('+')
 *
 * 5a. gen_op() 入口:
 *     - combine_types(): int + int → int
 *     - 非无符号
 *     - gen_cast_s(VT_INT): 无操作
 *     - 调用 gen_opic('+')
 *
 * 5b. gen_opic('+'):
 *     - c1 = 0 (a 不是常量), c2 = 0 (b*c 不是常量)
 *     - 不满足优化条件
 *     - 调用 gen_opi('+')
 *
 * 5c. gen_opi('+'):
 *     - ll = 0, cc = 0
 *     - opc = 0 (add 的操作码扩展)
 *     - 调用 gv2(RC_INT, RC_INT)
 *
 * 5d. gv2(RC_INT, RC_INT):
 *     i. gv(RC_INT) for vtop[0] (b*c 结果):
 *        - r = TREG_RAX (0x0000)
 *        - r_ok = !(VT_LVAL) && (0 < VT_CONST) && (reg_classes[0] & RC_INT)
 *        - r_ok = 1 → 已经在正确的整数寄存器中，无需操作
 *
 *     ii. gv(RC_INT) for vtop[-1] (a):
 *         - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *         - get_reg(RC_INT) → 扫描寄存器:
 *           rax(0): 被 vtop[0] 占用 → 跳过
 *           rcx(1): 不在 vstack 中（注意：STEP 4 中 rcx 仅临时使用，
 *                   vtop[-1] 已被弹出更新）→ 分配 TREG_RCX
 *         - load(TREG_RCX, vtop[-1]):
 *           生成: mov -8(%rbp), %ecx
 *         - vtop[-1].r = TREG_RCX (0x0001)
 *
 * 5e. gen_opi('+') 生成加法:
 *     cc = 0 → 使用寄存器-寄存器路径:
 *     orex(ll=0, r=TREG_RAX, fr=TREG_RCX, 0x01):  → 无 REX 前缀
 *     o(0xc0 + REG_VALUE(TREG_RAX) + REG_VALUE(TREG_RCX) * 8)
 *     = o(0xc0 + 0 + 1*8) = o(0xc8)
 *     完整指令: 01 c8 → add %ecx, %eax
 *     vtop--: 弹出 a
 *
 * vstack:
 *   [0] type=VT_INT, r=TREG_RAX(0x0000), c.i=0  ← vtop (a+b*c 在 eax)
 *
 * vtop → [0]
 */


/*
 * === STEP 6: 赋值 'x = a + b * c' ===
 * 调用链: vstore()
 *
 * 6a. 目标（vtop[-1]）是 x 的左值:
 *     vpushsym() 已将 x 压入:
 *     vtop[-1] = { type=VT_INT, r=VT_LOCAL|VT_LVAL(0x132), c.i=-32, sym=x }
 *
 * vstack (赋值前):
 *   [0] type=VT_INT, r=TREG_RAX(0x0000)      ← vtop (a+b*c 的结果)
 *   [1] type=VT_INT, r=0x132, c.i=-32, sym=x
 *
 * 6b. vstore():
 *     - sbt = VT_INT, dbt = VT_INT → 标量存储路径
 *     - delayed_cast 检查: dbt 不是 char/short → 无延迟转换
 *     - gen_cast(&vtop[-1].type): int → int，无操作
 *     - gv(RC_INT): vtop 已在 TREG_RAX 中，r_ok = 1
 *     - 检查 vtop[-1] (x 的左值):
 *       r = VT_LOCAL|VT_LVAL, c.i = -32
 *       不是 VT_LLOCAL → 不需要额外加载地址
 *     - store(TREG_RAX, vtop[-1]):
 *       生成: mov %eax, -32(%rbp)
 *       (使用 orex(0,0,r,0x89) + gen_modrm(r, VT_LOCAL, NULL, -32))
 *     - vswap(); vtop--: 清理栈
 *
 * vstack: 空
 * vtop → vstack[-1]
 */


/*
 * === STEP 7: return x ===
 * 调用链: greturn() → vpushsym() → gv(RC_IRET) → gfunc_epilog()
 *
 * 7a. vpushsym() 将 x 的左值压入:
 *     vstack[0] = { type=VT_INT, r=0x132, c.i=-32, sym=x }
 *
 * 7b. gv(RC_IRET):  # RC_IRET = RC_RAX
 *     - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *     - get_reg(RC_RAX) → TREG_RAX (0)
 *     - load(TREG_RAX, vtop):
 *       生成: mov -32(%rbp), %eax
 *     - vtop->r = TREG_RAX
 *
 * 7c. gfunc_epilog():
 *     生成: leave; ret
 */


/* ===================================================================
 * 汇编输出总结（由 tcc -S 生成的实际输出）
 * ===================================================================
 *
 * compute:
 *     push    %rbp
 *     mov     %rsp, %rbp
 *     sub     $32, %rsp
 *     mov     %edi, -8(%rbp)       # 保存参数 a (STEP prolog)
 *     mov     %esi, -16(%rbp)      # 保存参数 b
 *     mov     %edx, -24(%rbp)      # 保存参数 c
 *     mov     -24(%rbp), %eax      # STEP 4d.i: 加载 c → eax
 *     mov     -16(%rbp), %ecx      # STEP 4d.ii: 加载 b → ecx
 *     imul    %ecx, %eax           # STEP 4e: b * c
 *     mov     -8(%rbp), %ecx       # STEP 5d.ii: 加载 a → ecx
 *     add     %ecx, %eax           # STEP 5e: a + (b*c)
 *     mov     %eax, -32(%rbp)      # STEP 6b: 存储到 x
 *     mov     -32(%rbp), %eax      # STEP 7b: 加载 x 作为返回值
 *     leave                        # STEP 7c: 恢复帧指针
 *     ret                          # STEP 7c: 返回
 */


/* ===================================================================
 * 辅助：更复杂的表达式示例
 * =================================================================== */

/* 示例 2: 条件表达式与短路求值 */
int logic_example(int a, int b, int c) {
    /*
     * 表达式: a > 0 && b > 0
     *
     * STEP 1: vpushsym(a) → vstack[0] = {a 的左值}
     * STEP 2: vpushi(0)   → vstack[0] = {常量 0}, vstack[1] = {a}
     * STEP 3: gen_op('>') → gen_opic → gen_opi
     *   - gv(RC_INT): 加载 a 到 eax
     *   - gv(RC_INT): 0 → 已经是 VT_CONST
     *   - 生成: cmp $0, %eax  (实际上是比较的反向)
     *   - vtop->r = VT_CMP, cmp_op = TOK_GT
     *   - vstack[0] = {r=VT_CMP, cmp_op=TOK_GT, jtrue=0, jfalse=0}
     *
     * STEP 4: 处理 && (TOK_LAND)
     *   - gvtst(false, 0): 如果 a > 0 为假，跳转到短路点
     *   - 生成: jle forward_label
     *   - vstack 清空
     *
     * STEP 5: vpushsym(b) → vstack[0] = {b 的左值}
     * STEP 6: vpushi(0)   → vstack[0] = {常量 0}, vstack[1] = {b}
     * STEP 7: gen_op('>')
     *   - 类似 STEP 3
     *   - vstack[0] = {r=VT_CMP, cmp_op=TOK_GT}
     *
     * STEP 8: 最终结果
     *   - gvtst 解析跳转链
     *   - 结果类型为 int (0 或 1)
     */
    return (a > 0) && (b > 0);
}

/* 示例 3: 函数调用 */
int call_example(int a, int b) {
    /*
     * 表达式: compute(a, b, a + b)
     *
     * STEP 1: vpushsym(compute) → 函数地址
     * STEP 2: vpushsym(a)       → 第一个参数
     * STEP 3: vpushsym(b)       → 第二个参数
     * STEP 4: vpushsym(a)       → 第三个参数的一部分
     * STEP 5: vpushsym(b)       → 第三个参数的一部分
     * STEP 6: gen_op('+')       → 计算 a + b
     *         生成: 加载 a 和 b，add
     * STEP 7: gfunc_call(3)
     *   - save_regs(): 保存所有活跃寄存器
     *   - 参数 0 (a): gv(RC_INT) → mov -8(%rbp), %edi  (arg_regs[0]=rdi)
     *   - 参数 1 (b): gv(RC_INT) → mov -16(%rbp), %esi  (arg_regs[1]=rsi)
     *   - 参数 2 (a+b): gv(RC_INT) → %edx  (arg_regs[2]=rdx)
     *   - 函数地址: gv(RC_INT) → %rax
     *   - 生成: call *%rax
     *   - 返回值在 %eax 中
     */
    return compute(a, b, a + b);
}

/* 示例 4: 位域操作 */
struct Flags {
    unsigned int enabled : 1;
    unsigned int mode    : 3;
    unsigned int level   : 4;
};

int bitfield_example(struct Flags *f) {
    /*
     * 表达式: f->mode
     *
     * STEP 1: vpushsym(f)      → 指针值
     * STEP 2: 解引用 → 加上 mode 的偏移
     * STEP 3: gv() 处理位域:
     *   - bit_pos = 1, bit_size = 3
     *   - 类型标记 VT_BITFIELD
     *   - 加载完整字: mov (%rdi), %eax
     *   - 右移 1 位: shr $1, %eax
     *   - 左移 28 位: shl $28, %eax  (32 - 1 - 3 = 28)
     *   - 算术右移 29 位: sar $29, %eax  (32 - 3 = 29)
     *   - 结果在 eax 中，类型为 int
     */
    return f->mode;
}

/* ===================================================================
 * main - 验证所有示例函数可正确执行
 * =================================================================== */
#include <stdio.h>

int main(void)
{
    struct Flags flags = { 1, 5 };

    printf("=== vstack_trace 示例运行 ===\n");
    printf("compute(2, 3, 4) = %d  (期望: 2+3*4=14)\n", compute(2, 3, 4));
    printf("logic_example(1, 2, 3) = %d\n", logic_example(1, 2, 3));
    printf("call_example(3, 4) = %d  (期望: compute(3,4,7)=34)\n", call_example(3, 4));
    printf("bitfield_example(&flags) = %d  (期望: mode=5)\n", bitfield_example(&flags));
    return 0;
}


===== FILE: docs/ch05/exercises/ex1_vstack.md =====
# 练习 5.1：vstack 状态追踪

## 目标

手动追踪 TinyCC 代码生成器在处理复杂表达式时，虚拟栈（vstack）的完整状态变化。通过此练习，深入理解 `SValue` 结构体的语义和代码生成的延迟求值机制。

## 前置知识

- `SValue` 结构体的 `type`、`r`、`r2`、`c.i`、`sym` 字段（参见 5.2.1 节）
- `r` 字段编码：`VT_CONST`(0x30)、`VT_LOCAL`(0x32)、`VT_LVAL`(0x100)、物理寄存器(0-15)（参见 5.2.2 节）
- `gv()` 函数的物化逻辑（参见 5.4 节）
- `gen_op()` 的类型提升和常量折叠逻辑（参见 5.5 节）

## 题目

给定以下 C 代码，目标平台为 x86-64 (SysV ABI)：

```c
int expr(int a, int b) {
    int result = (a + 1) * (b - 2);
    return result;
}
```

已知条件：
- 参数 `a` 通过 `%edi` 传入，由 `gfunc_prolog()` 保存到 `[rbp-8]`
- 参数 `b` 通过 `%esi` 传入，保存到 `[rbp-16]`
- 局部变量 `result` 分配在 `[rbp-24]`
- x86-64 上 `VT_INT` 为 4 字节，`PTR_SIZE` 为 8

## 要求

逐步追踪以下操作序列，填写每一步的 vstack 状态。对每个 SValue 条目，记录 `type.t`、`r`（含标志位）、`c.i`、`sym` 四个关键字段。

### 追踪模板

每一步请按以下格式填写：

```
STEP N: <操作描述>
  调用链: ...
  生成的指令: ...
  vstack:
    [0] type=..., r=..., c.i=..., sym=...    ← vtop
    [1] type=..., r=..., c.i=..., sym=...
    ...
```

### 步骤序列

**STEP 1**: 解析标识符 `a`（`vpushsym`）

**STEP 2**: 解析整数常量 `1`（`vpushi(1)`）

**STEP 3**: 执行加法 `a + 1`（`gen_op('+')`）

> 提示：注意 `gen_opic('+')` 中的常量折叠逻辑——当一个操作数是常量时，它会尝试交换操作数使常量在右侧。但这里两个操作数都不是纯常量（`a` 是 `VT_LOCAL|VT_LVAL`），所以不会触发代数优化。

**STEP 4**: 解析标识符 `b`（`vpushsym`）

**STEP 5**: 解析整数常量 `2`（`vpushi(2)`）

**STEP 6**: 执行减法 `b - 2`（`gen_op('-')`）

**STEP 7**: 执行乘法 `(a+1) * (b-2)`（`gen_op('*')`）

> 提示：此时 `gv2(RC_INT, RC_INT)` 被调用。注意 `vtop[0]`（`b-2` 的结果）和 `vtop[-1]`（`a+1` 的结果）都需要从寄存器中确认或加载。思考 `gv()` 的 `r_ok` 判断逻辑。

**STEP 8**: 赋值 `result = ...`（`vstore`）

**STEP 9**: 返回 `result`（`gv(RC_IRET)` + `gfunc_epilog`）

## 参考答案

<details>
<summary>点击展开参考答案</summary>

### STEP 1: 解析标识符 `a`

```
调用链: expr_primary() → vpushsym()
生成的指令: 无（仅压栈）

vstack:
  [0] type=VT_INT(0x3), r=VT_LOCAL|VT_LVAL(0x132), c.i=-8, sym=&a    ← vtop
```

### STEP 2: 解析整数常量 `1`

```
调用链: vpushi(1) → vpush64(VT_INT, 1) → vsetc()
生成的指令: 无（仅压栈）

vstack:
  [0] type=VT_INT(0x3), r=VT_CONST(0x30), c.i=1, sym=NULL    ← vtop
  [1] type=VT_INT(0x3), r=0x132, c.i=-8, sym=&a
```

### STEP 3: 执行加法 `a + 1`

```
调用链: gen_op('+') → gen_opic('+') → gen_opi('+')

gen_op('+'):
  - combine_types(): int + int → int
  - 非无符号，不修改操作符
  - gen_cast_s(VT_INT): 无操作
  - gen_opic('+'):
    - c1 = (vtop[-1].r & mask) == VT_CONST → 检查 a: r=0x132 → c1=0
    - c2 = (vtop.r & mask) == VT_CONST → 检查 1: r=0x30 → c2=1
    - c2=1 且 op='+'，不满足零消除/恒等消除条件
    - 进入 general_case → gen_opi('+')

gen_opi('+'):
  - cc = (vtop.r & mask) == VT_CONST → cc=1（1 是常量）
  - 进入常量路径:
    vswap(): 交换栈顶（使 a 在 vtop，1 在 vtop[-1]）
    gv(RC_INT) for vtop (a):
      - r=0x132 (VT_LOCAL|VT_LVAL) → r_ok=0
      - get_reg(RC_INT) → TREG_RAX (0)
      - load(TREG_RAX, a): 生成 mov -8(%rbp), %eax
      - vtop->r = 0 (TREG_RAX)
    vswap(): 交换回来（1 在 vtop，a 在 vtop[-1]）
    c = vtop->c.i = 1
    c == (signed char)c → 使用 imm8 路径:
    orex(ll=0, r=TREG_RAX, 0, 0x83): 无 REX
    o(0xc0 | (0 << 3) | 0): o(0xc0)  → add $1, %eax
    g(1): 立即数 1
    完整指令: 83 c0 01 → add $1, %eax

  vtop--: 弹出常量 1

vstack:
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL    ← vtop
       含义: a+1 的结果在 %eax 中

生成的指令:
    mov -8(%rbp), %eax       # 加载 a
    add $1, %eax             # a + 1
```

### STEP 4: 解析标识符 `b`

```
调用链: vpushsym()
生成的指令: 无

vstack:
  [0] type=VT_INT(0x3), r=0x132, c.i=-16, sym=&b    ← vtop
  [1] type=VT_INT(0x3), r=0x0, c.i=0, sym=NULL
```

### STEP 5: 解析整数常量 `2`

```
调用链: vpushi(2)
生成的指令: 无

vstack:
  [0] type=VT_INT(0x3), r=VT_CONST(0x30), c.i=2, sym=NULL    ← vtop
  [1] type=VT_INT(0x3), r=0x132, c.i=-16, sym=&b
  [2] type=VT_INT(0x3), r=0x0, c.i=0, sym=NULL
```

### STEP 6: 执行减法 `b - 2`

```
调用链: gen_op('-') → gen_opic('-') → gen_opi('-')

gen_opic('-'):
  - c1=0 (b), c2=1 (常量 2)
  - '-' 不是交换律操作，不交换
  - c2=1, l2=2: 不满足零消除条件（op='-' 不在列表中）
  - 进入 general_case → gen_opi('-')

gen_opi('-'):
  - cc=1（vtop=2 是常量）
  - opc=5 (sub 的操作码扩展)
  - 进入常量路径:
    vswap(): 使 b 在 vtop
    gv(RC_INT) for vtop (b):
      - r=0x132 → r_ok=0
      - get_reg(RC_INT):
        rax(0): 被 vstack[2] 占用 (a+1 的结果) → 跳过
        rcx(1): 空闲 → 分配 TREG_RCX
      - load(TREG_RCX, b): 生成 mov -16(%rbp), %ecx
      - vtop->r = 1 (TREG_RCX)
    vswap(): 使 2 在 vtop
    c = 2
    c == (signed char)c → imm8 路径:
    orex(0, TREG_RCX, 0, 0x83): 无 REX
    o(0xc0 | (5 << 3) | 1): o(0xe9) → sub $2, %ecx
    g(2): 立即数 2
    完整指令: 83 e9 02 → sub $2, %ecx

  vtop--: 弹出常量 2

vstack:
  [0] type=VT_INT(0x3), r=TREG_RCX(0x1), c.i=0, sym=NULL    ← vtop
       含义: b-2 的结果在 %ecx 中
  [1] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL
       含义: a+1 的结果在 %eax 中

生成的指令:
    mov -16(%rbp), %ecx      # 加载 b
    sub $2, %ecx             # b - 2
```

### STEP 7: 执行乘法 `(a+1) * (b-2)`

```
调用链: gen_op('*') → gen_opic('*') → gen_opi('*')

gen_opi('*'):
  - ll=0, cc=0（vtop 不是常量）
  - 调用 gv2(RC_INT, RC_INT)

gv2(RC_INT, RC_INT):
  i. gv(RC_INT) for vtop[0] (b-2, 在 TREG_RCX):
     - r = TREG_RCX (0x1)
     - r_ok = !(VT_LVAL) && (1 < VT_CONST) && (reg_classes[1] & RC_INT)
     - r_ok = 1 → 已在正确寄存器中，无需操作

  ii. gv(RC_INT) for vtop[-1] (a+1, 在 TREG_RAX):
      - r = TREG_RAX (0x0)
      - r_ok = !(VT_LVAL) && (0 < VT_CONST) && (reg_classes[0] & RC_INT)
      - r_ok = 1 → 已在正确寄存器中，无需操作

gen_opi('*') 生成乘法:
  - r = vtop[-1].r = TREG_RAX (0)
  - fr = vtop[0].r = TREG_RCX (1)
  - orex(ll=0, fr=TREG_RCX, r=TREG_RAX, 0xaf0f):
    无 REX 前缀
  - o(0xc0 + REG_VALUE(TREG_RCX) + REG_VALUE(TREG_RAX)*8)
    = o(0xc0 + 1 + 0*8) = o(0xc1)
  - 完整指令: 0f af c1 → imul %ecx, %eax

  vtop--: 弹出 b-2

vstack:
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL    ← vtop
       含义: (a+1)*(b-2) 的结果在 %eax 中

生成的指令:
    imul %ecx, %eax          # (a+1) * (b-2)
```

### STEP 8: 赋值 `result = (a+1)*(b-2)`

```
调用链: vstore()

vstack (赋值前, result 的左值已压入):
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0       ← vtop (乘法结果)
  [1] type=VT_INT(0x3), r=0x132, c.i=-24, sym=&result

vstore():
  - sbt = VT_INT, dbt = VT_INT → 标量存储
  - delayed_cast: dbt 不是 char/short → 无
  - gen_cast: int → int，无操作
  - gv(RC_INT): 已在 TREG_RAX, r_ok=1
  - vtop[-1].r = 0x132 (VT_LOCAL|VT_LVAL), 不是 VT_LLOCAL
  - store(TREG_RAX, &result):
    生成: mov %eax, -24(%rbp)
  - vswap(); vtop--: 清理栈

vstack: 空

生成的指令:
    mov %eax, -24(%rbp)      # 存储到 result
```

### STEP 9: 返回 `result`

```
调用链: greturn() → vpushsym(result) → gv(RC_IRET) → gfunc_epilog()

gv(RC_IRET=RC_RAX):
  - r = 0x132 → r_ok=0
  - get_reg(RC_RAX) → TREG_RAX (0)
  - load(TREG_RAX, result):
    生成: mov -24(%rbp), %eax
  - vtop->r = TREG_RAX

gfunc_epilog():
  生成: leave; ret

vstack: 空（返回值已物化到 %eax）

生成的指令:
    mov -24(%rbp), %eax      # 加载返回值
    leave
    ret
```

### 完整汇编输出

```asm
expr:
    push    %rbp
    mov     %rsp, %rbp
    sub     $24, %rsp
    mov     %edi, -8(%rbp)       # 保存 a
    mov     %esi, -16(%rbp)      # 保存 b
    mov     -8(%rbp), %eax       # STEP 3: 加载 a
    add     $1, %eax             # STEP 3: a + 1
    mov     -16(%rbp), %ecx      # STEP 6: 加载 b
    sub     $2, %ecx             # STEP 6: b - 2
    imul    %ecx, %eax           # STEP 7: (a+1) * (b-2)
    mov     %eax, -24(%rbp)      # STEP 8: 存储到 result
    mov     -24(%rbp), %eax      # STEP 9: 返回值
    leave
    ret
```

</details>

## 思考题

1. 在 STEP 7 中，为什么 `gv2()` 不需要生成任何 `mov` 指令？这与 STEP 3 和 STEP 6 有什么不同？

2. 如果将表达式改为 `(a + 1) * (b - 2) + a`，在最后的加法步骤中，`gv()` 需要对 `a` 做什么操作？为什么？

3. 假设 `a` 和 `b` 是 `char` 类型而非 `int`，`gv()` 在加载时会有什么不同？提示：考虑 `VT_MUSTCAST` 标志和 `movsbl` 指令。


===== FILE: docs/ch05/exercises/ex2_codegen.md =====
# 练习 5.2：汇编预测与验证

## 目标

通过手动预测 TinyCC 生成的 x86-64 汇编代码，然后与实际输出对比，加深对代码生成器行为的理解。

## 前置知识

- TinyCC 代码生成的基本流程（参见 5.1 节）
- `gv()` 的物化逻辑（参见 5.4 节）
- `gen_opi()` 的指令生成（参见 5.10.4 节）
- `gfunc_prolog()` / `gfunc_epilog()` 生成的序言/尾声（参见 5.10.3 节）
- 条件跳转的处理（参见 5.8 节）

## 环境准备

确保已安装 TinyCC：

```bash
# 如果尚未安装
cd /path/to/tinycc
./configure && make
# tcc 现在在当前目录
```

## 题目 1：基本条件分支

给定以下 C 代码：

```c
int abs_diff(int a, int b) {
    int d = a - b;
    if (d < 0)
        d = -d;
    return d;
}
```

### 任务 A：手动预测

根据你对 TinyCC 代码生成器的理解，预测它会生成的 x86-64 汇编。回答以下问题：

1. 函数序言需要分配多少栈空间？为什么？
2. `d = a - b` 生成哪些指令？
3. `if (d < 0)` 如何生成条件跳转？是用 `testl %eax, %eax; js` 还是 `cmpl $0, %eax; jl`？
4. `d = -d` 如何实现？是用 `neg` 指令还是 `0 - d`？
5. `return d` 需要额外的 `mov` 指令吗？

请在下方写出你预测的完整汇编：

```asm
; 你的预测
abs_diff:
    ; ...
```

### 任务 B：实际验证

使用 TinyCC 生成汇编并与你的预测对比：

```bash
# 将上述代码保存为 abs_diff.c，然后：
./tcc -S -o abs_diff.s abs_diff.c

# 查看生成的汇编
cat abs_diff.s
```

### 任务 C：差异分析

对比你的预测和实际输出，回答：

1. 有哪些差异？
2. TinyCC 使用了你没预料到的指令吗？
3. TinyCC 的寄存器分配与你预期的一致吗？

---

## 题目 2：循环

给定以下 C 代码：

```c
int sum_array(int *arr, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += arr[i];
    }
    return sum;
}
```

### 任务 A：手动预测

预测 TinyCC 生成的汇编。特别关注：

1. 循环条件 `i < n` 的比较和跳转指令
2. `arr[i]` 的地址计算：`arr + i * sizeof(int)` 如何翻译？
3. `sum += arr[i]` 的指令序列
4. 循环的跳转结构（前向跳转还是后向跳转？）

### 任务 B：验证

```bash
# 保存为 sum_array.c
./tcc -S -o sum_array.s sum_array.c
cat sum_array.s
```

### 任务 C：分析

1. TinyCC 是否对 `arr[i]` 的地址计算使用了 `lea` 还是 `imul`？
2. 循环的跳转指令是 `jmp`（无条件回跳）还是 `jcc`（条件回跳）？
3. 与 GCC -O0 的输出相比，有什么结构性差异？

---

## 题目 3：类型转换

给定以下 C 代码：

```c
double int_to_double(int x) {
    double result = (double)x;
    return result;
}

int double_to_int(double x) {
    int result = (int)x;
    return result;
}

int mixed_arithmetic(int a, float b) {
    return a + (int)b;
}
```

### 任务 A：手动预测

对每个函数预测汇编输出。特别关注：

1. `int → double`：使用什么指令？（`cvtsi2sd`）
2. `double → int`：使用什么指令？（`cvttsd2si`）
3. `float` 参数通过什么寄存器传入？（`xmm0` 还是通用寄存器？）
4. 浮点返回值使用什么寄存器？（`xmm0`）

### 任务 B：验证

```bash
./tcc -S -o type_conv.s type_conv.c
cat type_conv.s
```

---

## 题目 4：结构体操作

给定以下 C 代码：

```c
struct Point {
    int x;
    int y;
};

struct Point add_points(struct Point a, struct Point b) {
    struct Point result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}
```

### 任务 A：手动预测

根据 SysV AMD64 ABI：

1. `struct Point`（8 字节）如何作为参数传递？是通过寄存器还是栈？
2. 如果通过寄存器，使用哪些寄存器？
3. 返回值如何传递？是通过 `rax` 还是通过隐式指针参数？
4. `a.x` 和 `b.x` 如何从寄存器中提取？

### 任务 B：验证

```bash
./tcc -S -o struct.s struct.c
cat struct.s
```

### 评分标准

| 评分项 | 满分 | 说明 |
|--------|------|------|
| 函数序言/尾声正确 | 20 | push/sub/mov 序列、leave/ret |
| 参数加载正确 | 20 | 寄存器→栈的保存指令 |
| 算术运算正确 | 20 | 正确的指令和操作数 |
| 控制流正确 | 20 | 条件跳转的类型和目标 |
| 寄存器分配合理 | 20 | 使用了正确的寄存器 |

每题满分 100 分，共 4 题。总分 400 分。

## 提示

1. TinyCC 不做优化，所以每个 C 语句通常对应独立的指令序列。不要期望看到跨语句的寄存器复用优化。
2. TinyCC 的函数序言总是使用 `push %rbp; mov %rsp, %rbp; sub $N, %rsp`，参数总是先保存到栈帧。
3. 局部变量的地址相对于 `%rbp` 是负偏移。
4. 条件表达式 `if (x < 0)` 可能被编译为 `cmpl $0, %eax; jge`（跳过 if 体）或 `testl %eax, %eax; jns`（测试符号位）。
5. 结构体赋值可能使用 `memmove` 调用而非逐字段复制。


===== FILE: docs/ch05/exercises/ex3_register.md =====
# 练习 5.3：寄存器分配跟踪

## 目标

手动跟踪 TinyCC 的 `get_reg()` 寄存器分配器在函数代码生成过程中的每次调用，理解寄存器复用、溢出（spill）和临时变量机制。

## 前置知识

- `get_reg(int rc)` 的三步策略：复用 → 空闲 → 溢出（参见 5.9.1 节）
- `save_reg(int r)` / `save_reg_upstack(int r, int n)` 的溢出逻辑（参见 5.9.2 节）
- `gv(int rc)` 中的 `r_ok` 判断（参见 5.4.2 节）
- x86-64 的寄存器集合和分类（参见 5.10.1 节）
- `reg_classes[]` 数组的含义

## 背景

TinyCC 的寄存器分配是**按需**（on-demand）的：没有预先的活跃性分析或图着色。每次需要寄存器时，`get_reg()` 扫描 vstack 寻找空闲寄存器，找不到就溢出最老的值。

### 可用寄存器（RC_INT 类别）

在 x86-64 上，`RC_INT` 类别的寄存器包括：

| 编号 | 名称 | `reg_classes` |
|------|------|---------------|
| 0 | rax | RC_INT \| RC_RAX |
| 1 | rcx | RC_INT \| RC_RCX |
| 2 | rdx | RC_INT \| RC_RDX |
| 8 | r8  | RC_R8 |
| 9 | r9  | RC_R9 |
| 10 | r10 | RC_R10 |
| 11 | r11 | RC_R11 |

注意：r8-r11 的 `reg_classes` **不包含** `RC_INT`，它们有自己的独立类别。当 `gv(RC_INT)` 调用 `get_reg(RC_INT)` 时，只会在 rax(0)、rcx(1)、rdx(2) 中选择。

### 关键规则

1. `gv(RC_INT)` 调用 `get_reg(RC_INT)` 时，只会分配 rax, rcx, rdx 三个寄存器之一。
2. 如果三个寄存器都被 vstack 中的条目占用，`get_reg()` 会溢出**栈底**（最老）的值。
3. `save_reg_upstack(r, 1)` 只溢出 vtop 以下的条目——这是 `gv()` 中使用的变体，因为 vtop 即将被覆盖。
4. 溢出的值被存储到函数栈帧的临时变量区域，其 vstack 条目的 `r` 字段被更新为 `VT_LVAL | VT_LOCAL`。

## 题目

给定以下 C 函数：

```c
int heavy(int a, int b, int c) {
    int d = a + b;
    int e = b + c;
    int f = d * e;
    int g = f - a;
    int h = g + b;
    int i = h * c;
    return i;
}
```

假设：
- 只有 rax(0)、rcx(1)、rdx(2) 三个寄存器可用于 `RC_INT` 分配
- 参数 a, b, c 已保存在栈帧中：
  - a → [rbp-8]
  - b → [rbp-16]
  - c → [rbp-24]
- 局部变量 d-i 分别分配在 [rbp-32] 到 [rbp-72]

## 任务

逐步跟踪每个表达式的代码生成过程，记录：

1. **每次 `get_reg()` 调用**：哪个寄存器被分配？是空闲分配还是溢出分配？
2. **每次 `save_reg()` 调用**：哪个寄存器被溢出？溢出到哪里？哪些 vstack 条目受影响？
3. **每次 `gv()` 调用**：`r_ok` 的值是多少？是否需要加载？

### 追踪模板

```
操作: <表达式>
  vstack (操作前):
    [0] r=<位置>, ...   ← vtop
    [1] r=<位置>, ...
    ...

  get_reg(RC_INT): 分配 rax/rcx/rdx
    - 空闲检查: rax=占用/空闲, rcx=..., rdx=...
    - 结果: 分配 <寄存器>
    - [如有溢出] save_reg(<寄存器>): 溢出 vstack[N] 到 [rbp-offset]

  vstack (操作后):
    [0] r=<位置>, ...   ← vtop
    ...
```

## 第一部分：手动跟踪

### `int d = a + b`

**操作前 vstack**（a 和 b 的左值已压入）：

```
[0] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-16 (b)   ← vtop
[1] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-8  (a)
```

**gen_op('+') → gen_opi('+') → gv2(RC_INT, RC_INT)**

请填写以下过程：

```
gv(RC_INT) for vtop[0] (b):
  r = VT_LOCAL|VT_LVAL → r_ok = ?
  get_reg(RC_INT):
    rax: ?  rcx: ?  rdx: ?
    分配: ?
  load(?, b): 生成指令 ?
  vtop[0].r = ?

gv(RC_INT) for vtop[-1] (a):
  r = VT_LOCAL|VT_LVAL → r_ok = ?
  get_reg(RC_INT):
    rax: ?  rcx: ?  rdx: ?
    分配: ?
  load(?, a): 生成指令 ?
  vtop[-1].r = ?

gen_opi('+'): 生成指令 ?
vtop--: 弹出 b

结果 vstack:
  [0] r=? (d 的结果在 ? 中)
```

### `int e = b + c`

此时 vstack 中有 d 的结果。继续跟踪：

```
操作前 vstack:
  [0] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-24 (c)   ← vtop
  [1] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-16 (b)
  [2] type=VT_INT, r=? (d 的结果)
```

请填写 `b + c` 的处理过程。

### `int f = d * e`

此时 vstack 中有 e 和 d 的结果。

```
操作前 vstack:
  [0] r=? (e 的结果)   ← vtop
  [1] r=? (d 的结果)
```

**关键问题**：`gen_opi('*')` 调用 `gv2(RC_INT, RC_INT)` 时：
- `gv(RC_INT)` 对 vtop（e）的 `r_ok` 是多少？
- `gv(RC_INT)` 对 vtop[-1]（d）的 `r_ok` 是多少？
- 是否需要 `save_reg()`？

### `int g = f - a`

```
操作前 vstack:
  [0] r=VT_LOCAL|VT_LVAL, c.i=-8 (a)   ← vtop
  [1] r=? (f 的结果)
```

**关键问题**：此时 rax, rcx, rdx 中有多少个被占用？加载 a 时是否需要溢出？

### `int h = g + b`

### `int i = h * c`

### `return i`

## 第二部分：完整汇编预测

根据你的跟踪结果，预测 TinyCC 生成的完整汇编代码。特别注意溢出导致的额外 `mov` 指令。

```asm
heavy:
    push    %rbp
    mov     %rsp, %rbp
    sub     $?, %rsp
    ; 保存参数
    mov     %edi, -8(%rbp)     # a
    mov     %esi, -16(%rbp)    # b
    mov     %edx, -24(%rbp)    # c
    ; d = a + b
    ; ...
    ; e = b + c
    ; ...
    ; f = d * e
    ; ...
    ; g = f - a
    ; ...
    ; h = g + b
    ; ...
    ; i = h * c
    ; ...
    ; return i
    leave
    ret
```

## 第三部分：验证

```bash
# 保存上述 C 代码为 heavy.c
./tcc -S -o heavy.s heavy.c
cat heavy.s
```

## 参考答案

<details>
<summary>点击展开参考答案</summary>

### `d = a + b`

```
gv(RC_INT) for b:
  r=0x132 → r_ok=0
  get_reg(RC_INT): rax 空闲 → 分配 rax(0)
  load(rax, b): mov -16(%rbp), %eax
  vtop[0].r = rax(0)

gv(RC_INT) for a:
  r=0x132 → r_ok=0
  get_reg(RC_INT):
    rax: 被 vtop[0] 占用 → 跳过
    rcx: 空闲 → 分配 rcx(1)
  load(rcx, a): mov -8(%rbp), %ecx
  vtop[-1].r = rcx(1)

gen_opi('+'): add %ecx, %eax
vtop--
```

**操作后 vstack**（加上 result 左值）：

```
store(rax, &d): mov %eax, -32(%rbp)
vstack 清空（赋值后弹出）
```

### `e = b + c`

```
vpushsym(b): [0] r=0x132, c.i=-16
vpushsym(c): [0] r=0x132, c.i=-24

gv(RC_INT) for c:
  get_reg: rax 空闲 → rax
  mov -24(%rbp), %eax

gv(RC_INT) for b:
  get_reg: rax 占用(c), rcx 空闲 → rcx
  mov -16(%rbp), %ecx

add %ecx, %eax
store(rax, &e): mov %eax, -40(%rbp)
```

注意：此时 vstack 中没有 d 的结果（d 已在上一步 store 后弹出）。TinyCC 没有跨语句的寄存器保活——每个语句的结果被独立存储到栈帧。

### `f = d * e`

```
vpushsym(d): [0] r=0x132, c.i=-32
vpushsym(e): [0] r=0x132, c.i=-40

gv(RC_INT) for e:
  get_reg: rax 空闲 → rax
  mov -40(%rbp), %eax

gv(RC_INT) for d:
  get_reg: rax 占用, rcx 空闲 → rcx
  mov -32(%rbp), %ecx

imul %ecx, %eax
store(rax, &f): mov %eax, -48(%rbp)
```

### `g = f - a`

```
vpushsym(f): [0] r=0x132, c.i=-48
vpushsym(a): [0] r=0x132, c.i=-8

gv(RC_INT) for a:
  get_reg: rax 空闲 → rax
  mov -8(%rbp), %eax

gv(RC_INT) for f:
  get_reg: rax 占用, rcx 空闲 → rcx
  mov -48(%rbp), %ecx

subl %eax, %ecx  (注意: f - a, 不是 a - f)
  实际上 vswap 可能发生，取决于操作数顺序
  gen_opi('-') 中 cc=0, gv2(RC_INT, RC_INT):
    vtop[0] = a (在 rax), vtop[-1] = f (需要加载)
    gv for vtop[0](a): rax, r_ok=1
    gv for vtop[-1](f): get_reg → rcx
    load rcx from -48(%rbp)
  sub %eax, %ecx  → ecx = f - a

store(rcx, &g): mov %ecx, -56(%rbp)
```

### `h = g + b`

```
类似前面的模式，rax 和 rcx 足够，不需要溢出。
mov -56(%rbp), %eax  # load g
mov -16(%rbp), %ecx  # load b
add %ecx, %eax
mov %eax, -64(%rbp)  # store h
```

### `i = h * c`

```
同样不需要溢出。
mov -64(%rbp), %eax  # load h
mov -24(%rbp), %ecx  # load c
imul %ecx, %eax
mov %eax, -72(%rbp)  # store i
```

### `return i`

```
mov -72(%rbp), %eax  # load i → rax (返回值寄存器)
leave
ret
```

### 关键发现

在这个特定例子中，**没有发生寄存器溢出**。原因有两个：

1. 每个表达式的结果在 store 后立即从 vstack 弹出，不会占用寄存器。
2. 每个二元操作只需要 2 个寄存器（rax 和 rcx），而我们有 3 个可用寄存器。

要触发溢出，需要更复杂的表达式链，例如在**单个表达式**中需要超过 3 个寄存器的情况：

```c
int trigger_spill(int a, int b, int c, int d) {
    return (a + b) * (c - d) + (a - c) * (b + d);
}
```

在这个表达式中，子表达式 `(a+b)` 的结果需要保留在寄存器中，同时还要计算 `(c-d)`、`(a-c)`、`(b+d)`，总共需要 4-5 个寄存器，就会触发溢出。

### 完整汇编输出

```asm
heavy:
    push    %rbp
    mov     %rsp, %rbp
    sub     $72, %rsp
    mov     %edi, -8(%rbp)
    mov     %esi, -16(%rbp)
    mov     %edx, -24(%rbp)
    # d = a + b
    mov     -8(%rbp), %ecx       # load a
    mov     -16(%rbp), %eax      # load b
    add     %ecx, %eax           # (注意: 实际顺序可能因 vswap 而不同)
    mov     %eax, -32(%rbp)      # store d
    # e = b + c
    mov     -16(%rbp), %ecx      # load b
    mov     -24(%rbp), %eax      # load c
    add     %ecx, %eax
    mov     %eax, -40(%rbp)      # store e
    # f = d * e
    mov     -32(%rbp), %ecx      # load d
    mov     -40(%rbp), %eax      # load e
    imull   %ecx, %eax
    mov     %eax, -48(%rbp)      # store f
    # g = f - a
    mov     -48(%rbp), %ecx      # load f
    mov     -8(%rbp), %eax       # load a
    subl    %eax, %ecx           # f - a
    mov     %ecx, -56(%rbp)      # store g
    # h = g + b
    mov     -56(%rbp), %ecx      # load g
    mov     -16(%rbp), %eax      # load b
    addl    %ecx, %eax
    mov     %eax, -64(%rbp)      # store h
    # i = h * c
    mov     -64(%rbp), %ecx      # load h
    mov     -24(%rbp), %eax      # load c
    imull   %ecx, %eax
    mov     %eax, -72(%rbp)      # store i
    # return i
    mov     -72(%rbp), %eax
    leave
    ret
```

</details>

## 扩展练习：触发溢出

修改上面的函数，使代码生成过程中**确实发生**寄存器溢出。提示：

```c
int trigger_spill(int a, int b, int c, int d) {
    /* 这个单表达式需要同时保持 a+b 和 c-d 的结果，
       再计算 a-c 和 b+d，总共需要超过 3 个寄存器 */
    return (a + b) * (c - d) + (a - c) * (b + d);
}
```

1. 追踪此表达式的 vstack 状态
2. 找到 `save_reg()` 被调用的位置
3. 记录溢出发生在哪个寄存器上
4. 验证溢出后的汇编输出

## 思考题

1. 为什么 `get_reg()` 从栈底（`vstack`）开始扫描溢出候选，而不是从栈顶？
2. 如果增加更多可用寄存器（例如允许 r8, r9, r10, r11 参与通用分配），`get_reg()` 的逻辑需要什么修改？这对 `gv()` 有什么影响？
3. `save_reg_upstack(r, 1)` 与 `save_reg(r)` 的区别是什么？为什么 `gv()` 使用前者？
4. TinyCC 的寄存器分配策略在什么情况下会产生最差的代码质量？能否构造一个极端例子？


===== FILE: docs/ch05/index.md =====
# 第五章 代码生成

## 概述

在前四章中，我们依次讨论了 TinyCC 的词法分析、语法分析与语义分析。本章进入编译器的最后核心阶段——**代码生成（Code Generation）**。与许多现代编译器不同，TinyCC 采用一种极为精简的策略：**不构建中间表示（IR），直接从语法树生成目标机器码**。这一设计决策使得 TinyCC 的代码生成器既短小又高效，但也意味着所有平台相关的优化必须在后端中完成。

本章将系统地剖析 TinyCC 代码生成器的架构。我们首先介绍其独特的"虚拟栈"机制（5.1–5.2），然后讨论后端接口的抽象层（5.3），接着深入分析几个关键的平台无关函数——`gv()`、`gen_op()`、`vstore()`、`gen_cast()`（5.4–5.7），以及条件跳转优化和寄存器分配策略（5.8–5.9）。随后，我们以 x86-64 后端为具体实例，展示平台相关代码如何实现这些接口（5.10）。最后讨论常量折叠（5.11）、代码抑制机制（5.12），并通过一个完整的端到端示例将所有概念串联起来（5.13）。

---

## 5.1 代码生成策略概述

### 5.1.1 直接代码生成

传统编译器通常采用三阶段架构：前端（解析）→ 中间表示（IR）→ 后端（目标代码生成）。GCC 使用 RTL（Register Transfer Language），LLVM 使用 SSA 形式的 IR。这些中间表示为各种优化 pass 提供了统一的操作平台。

TinyCC 的设计目标是**编译速度**而非代码质量，因此它选择了一条激进的捷径：

```
源代码 → 词法/语法分析 → 直接生成目标机器码
```

没有独立的 IR 阶段，没有优化 pass。语法分析器在归约（reduce）产生式的同时，就调用代码生成函数向目标代码段（`cur_text_section`）追加字节。

### 5.1.2 虚拟栈架构

虽然没有 IR，但 TinyCC 并非直接将每个表达式节点翻译为机器指令。它引入了一个精巧的中间层——**虚拟操作数栈（Virtual Value Stack，简称 vstack）**。其核心思想是：

- 每个表达式节点的求值结果被表示为一个 `SValue` 结构体，压入 vstack。
- 运算操作从栈顶弹出操作数，将结果压回栈顶。
- 只有当值真正需要进入物理寄存器时（例如作为函数参数、需要参与不支持的操作），才调用 `gv()` 将其"物化"到寄存器中。

这种设计允许 TinyCC 在表达式求值过程中保持一种**延迟求值**的姿态：常量可以在编译时折叠，寄存器分配被推迟到必要时刻，从而在不进行显式优化 pass 的情况下获得一定的代码质量。

### 5.1.3 与栈式虚拟机的比较

读者可能注意到 vstack 与栈式虚拟机（如 JVM、WebAssembly）的相似性。两者的关键区别在于：

| 特性 | 栈式虚拟机 | TinyCC vstack |
|------|-----------|---------------|
| 值的生命周期 | 运行时 | 编译时 |
| 栈的位置 | 运行时内存 | 编译器内部数组 |
| 目标 | 解释执行 | 生成原生机器码 |
| `SValue.r` 字段 | 无 | 记录值的物理位置 |

vstack 中的每个条目都可能代表一个**尚未物化**的值——一个常量、一个内存地址、甚至一个条件码状态。代码生成器的核心工作就是在正确时机将这些虚拟值转换为物理寄存器中的值。

---

## 5.2 虚拟栈（Value Stack）

### 5.2.1 SValue 结构体

vstack 的核心数据结构定义在 `tcc.h` 中：

```c
/* value on stack */
typedef struct SValue {
    CType type;          /* 类型信息 */
    unsigned short r;    /* 寄存器 + 标志位 */
    unsigned short r2;   /* 第二个寄存器（用于 long long） */
    union {
        struct { int jtrue, jfalse; }; /* 前向跳转链 */
        CValue c;         /* 常量值（当 r 为 VT_CONST 时） */
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; /* VT_CMP 操作 */
        struct Sym *sym;  /* 符号引用 */
    };
} SValue;
```

**字段详解：**

- **`type`**：CType 结构体，记录该值的 C 类型（基本类型、指针、结构体等）。类型信息对后续的类型转换、算术运算的宽度选择至关重要。

- **`r`**：这是 SValue 中最核心的字段。它编码了**值当前的物理位置**以及若干标志位。低 6 位（`VT_VALMASK = 0x003f`）编码位置，高位编码标志（`VT_LVAL`、`VT_SYM` 等）。

- **`r2`**：对于需要两个寄存器的类型（如 x86-32 上的 `long long`），`r2` 记录高 32 位所在的寄存器。未使用时设为 `VT_CONST`。

- **`c` / `jtrue, jfalse`**：联合体。当值为常量时，`c` 存储常量值（CValue 联合体，可以是 int、float、double、long double）。当值为条件跳转结果时，`jtrue` 和 `jfalse` 记录前向跳转链。

- **`sym` / `cmp_op, cmp_r`**：联合体。当值包含符号引用时（如全局变量地址），`sym` 指向符号表条目。当值存储在条件码中时（`VT_CMP`），`cmp_op` 记录比较操作符，`cmp_r` 记录浮点比较的特殊寄存器状态。

### 5.2.2 `r` 字段编码

`r` 字段的低 6 位（`VT_VALMASK`）编码值的当前位置。以下是所有可能的编码：

```
值          编码    含义
────────────────────────────────────────────────────
0-15        0x00-0x0f   物理寄存器编号（如 TREG_RAX=0, TREG_RCX=1, ...）
VT_CONST    0x0030      值为编译时常量，存储在 c 字段中
VT_LLOCAL   0x0031      左值，地址在栈帧的临时变量区域
VT_LOCAL    0x0032      值在栈帧中，偏移量存储在 c 字段中（相对于 %rbp）
VT_CMP      0x0033      值存储在 CPU 条件标志中（如 ZF, CF）
VT_JMP      0x0034      值是条件跳转"真"分支的结果（偶数）
VT_JMPI     0x0035      值是条件跳转"假"分支的结果（奇数）
```

**高位标志：**

```
标志          位      含义
────────────────────────────────────────────────────
VT_LVAL      0x0100   值是一个左值（内存地址），需要解引用才能使用
VT_SYM       0x0200   值包含符号引用，c 中的常量是相对于符号的偏移
VT_MUSTCAST  0x0C00   值需要延迟类型转换（char/short 存储在 int 寄存器中）
VT_NONCONST  0x1000   虽然当前为常量，但不是 C 标准的整数常量表达式
```

**编码示例：**

假设 `vtop->r = 0x0132`，则：
- 低 6 位 `0x32 = VT_LOCAL`：值在栈帧中
- 位 8（`0x0100`）= `VT_LVAL`：这是一个左值

这意味着该值是一个**栈上的局部变量的左值**——要使用它的值，需要先从栈中加载。

再如 `vtop->r = 0x0000`：
- 低 6 位 `0x00 = TREG_RAX`：值在 rax 寄存器中
- 无高位标志：这是一个右值

### 5.2.3 栈操作函数

vstack 通过全局指针 `vtop` 访问栈顶，底层是一个大小为 `VSTACK_SIZE`（512）的 `SValue` 数组 `_vstack[]`。以下是主要的栈操作函数：

**`vpushv(SValue *v)`**——将一个 SValue 压入栈顶：

```c
ST_FUNC void vpushv(SValue *v)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vtop++;
    *vtop = *v;
}
```

**`vpushi(int v)`**——压入整数常量：

```c
ST_FUNC void vpushi(int v)
{
    vpush64(VT_INT, v);
}
```

内部调用 `vpush64`，后者构造一个 `CValue` 并调用 `vsetc` 将类型设为 `VT_INT`，位置设为 `VT_CONST`。

**`vpop()`**——弹出栈顶值：

```c
ST_FUNC void vpop(void)
{
    int v;
    v = vtop->r & VT_VALMASK;
    if (v == TREG_ST0) {
        o(0xd8dd); /* fstp %st(0) — x87 浮点栈必须显式弹出 */
    } else if (v == VT_CMP) {
        /* 需要将悬空的跳转链解析到当前位置 */
        gsym(vtop->jtrue);
        gsym(vtop->jfalse);
    }
    vtop--;
}
```

注意 `vpop()` 的两个特殊处理：
1. x87 浮点寄存器栈（`TREG_ST0`）需要显式弹出指令。
2. `VT_CMP` 状态需要解析悬挂的跳转标签。

**`vswap()`**——交换栈顶两个元素：

```c
static void vswap(void)
{
    SValue tmp;
    vcheck_cmp();
    tmp = vtop[0];
    vtop[0] = vtop[-1];
    vtop[-1] = tmp;
}
```

调用 `vcheck_cmp()` 是因为如果栈顶下方有 `VT_CMP` 值，必须先将其物化到寄存器中，否则交换后条件码信息可能失效。

**`vrotb(int n)`**——将位置 n-1 的元素旋转到栈顶：

```c
ST_FUNC void vrotb(int n)
{
    SValue tmp;
    if (--n < 1) return;
    vcheck_cmp();
    tmp = vtop[-n];
    memmove(vtop - n, vtop - n + 1, sizeof *vtop * n);
    vtop[0] = tmp;
}
```

**`vrott(int n)`**——将栈顶元素旋转到位置 n-1：

```c
ST_FUNC void vrott(int n)
{
    SValue tmp;
    if (--n < 1) return;
    vcheck_cmp();
    tmp = vtop[0];
    memmove(vtop - n + 1, vtop - n, sizeof *vtop * n);
    vtop[-n] = tmp;
}
```

这两个旋转函数在函数调用参数准备时频繁使用——需要将函数地址旋转到参数之后。

**`vdup()`**——复制栈顶：

```c
static void vdup(void)
{
    vpushv(vtop);
}
```

---

## 5.3 后端接口

TinyCC 的代码生成器分为**平台无关层**（`tccgen.c`）和**平台相关层**（如 `x86_64-gen.c`、`arm64-gen.c`、`i386-gen.c`）。平台无关层通过一组函数指针/宏调用平台相关层的实现。

### 5.3.1 寄存器-内存传输

**`load(int r, SValue *sv)`**——将值 `sv` 加载到寄存器 `r` 中：

这是后端必须实现的核心函数。对于 x86-64，它根据 `sv` 的位置（常量、栈偏移、另一个寄存器）生成相应的 `mov` 指令。关键的分支逻辑：

- `VT_CONST + VT_SYM`：生成 RIP 相对寻址的 `mov` 或 GOT 间接访问
- `VT_CONST`（纯常量）：生成 `mov $imm, %r` 或 `movabs $imm64, %r`
- `VT_LOCAL`：生成 `lea offset(%rbp), %r`
- `VT_LVAL`（需要解引用）：生成 `mov (%base), %r`，使用 ModR/M 编码
- `VT_CMP`：生成 `setcc %al; movzbl %al, %r`
- `VT_JMP / VT_JMPI`：生成 `mov $1, %r; jmp ...; mov $0, %r`

**`store(int r, SValue *v)`**——将寄存器 `r` 中的值存储到 `v` 指定的左值位置：

与 `load()` 类似，根据目标位置生成相应的 `mov` 指令。对于浮点类型使用不同的指令（`movd`、`movq`、`fstpt`）。

### 5.3.2 函数调用约定

**`gfunc_call(int nb_args)`**——生成函数调用代码：

在调用前，函数地址和所有参数已按序压入 vstack。此函数负责：
1. 保存被调用者可能破坏的寄存器（`save_regs`）
2. 将参数移动到正确的寄存器或栈位置
3. 生成 `call` 指令
4. 清理 vstack（弹出参数和函数地址）
5. 将返回值寄存器信息压入 vstack

**`gfunc_prolog(Sym *func_sym)`**——生成函数序言（prologue）：

生成函数入口代码，包括：
1. 保存帧指针（`push %rbp; mov %rsp, %rbp`）
2. 分配局部变量空间（`sub $N, %rsp`）
3. 将传入的寄存器参数保存到栈帧中

**`gfunc_epilog(void)`**——生成函数尾声（epilogue）：

生成函数退出代码，包括：
1. 释放局部变量空间
2. 恢复帧指针（`leave`）
3. 返回（`ret`）

### 5.3.3 算术运算

**`gen_opi(int op)`**——生成整数二元运算：

接收 vstack 顶上的两个整数操作数，生成相应的算术/逻辑/移位指令。结果留在一个寄存器中，弹出一个操作数。

**`gen_opl(int op)`**——生成 long long 运算（在 x86-64 上等同于 `gen_opi`）。

**`gen_opf(int op)`**——生成浮点二元运算：

对于 SSE 浮点类型（float/double），使用 XMM 寄存器和 SSE 指令。对于 long double，使用 x87 FPU 栈。

### 5.3.4 控制流

**`gjmp(int t)`**——生成无条件跳转：

```c
int gjmp(int t)
{
    return gjmp2(0xe9, t);  /* jmp rel32 */
}
```

参数 `t` 是前向跳转链的头部。返回值是新的链头（新生成的跳转指令的待回填位置）。

**`gjmp_cond(int op, int t)`**——生成条件跳转：

根据比较操作符 `op`（如 `TOK_EQ`、`TOK_LT` 等）和条件码状态，生成 `jcc rel32` 指令。

**`gsym(int t)`**——解析前向跳转链：

将跳转链 `t` 中所有跳转指令的目标地址回填为当前位置。这是实现前向跳转的标准技术——跳转指令在生成时目标未知，先链在一起，待目标确定后统一回填。

**`gjmp_addr(int a)`**——生成跳转到固定地址：

尝试使用短跳转（`jmp rel8`，2 字节），如果偏移量超出范围则使用长跳转（`jmp rel32`，5 字节）。

---

## 5.4 `gv()` — 值到寄存器

`gv()` 是 TinyCC 代码生成器中**最关键**的函数。它的职责是：确保 vstack 栈顶的值被"物化"到一个指定类别的物理寄存器中，并返回该寄存器的编号。

### 5.4.1 函数签名与返回值

```c
ST_FUNC int gv(int rc)
```

参数 `rc` 是**寄存器类别**（register class），如 `RC_INT`（任意整数寄存器）、`RC_FLOAT`（任意 SSE 寄存器）、`RC_RAX`（必须是 rax）等。返回值是分配到的寄存器编号。

### 5.4.2 核心逻辑

`gv()` 的处理流程可以概括为以下步骤：

**第一步：处理位域（Bit Field）**

```c
if (vtop->type.t & VT_BITFIELD) {
    bit_pos = BIT_POS(vtop->type.t);
    bit_size = BIT_SIZE(vtop->type.t);
    vtop->type.t &= ~VT_STRUCT_MASK;  /* 移除位域信息避免循环 */
    /* ... 调用 adjust_bf() 或生成移位操作 ... */
    /* 递归调用 gv() 处理去位域化后的值 */
    r = gv(rc);
}
```

位域值需要特殊的处理：先从内存中加载完整的字，然后通过移位和掩码操作提取出位域的值。

**第二步：处理浮点常量**

```c
if (is_float(vtop->type.t) &&
    (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
    /* CPU 通常不能直接使用浮点常量，需要存储到数据段 */
    offset = section_add(rodata_section, size, align);
    vpush_ref(&vtop->type, rodata_section, offset, size);
    vswap();
    init_putv(&p, &vtop->type, offset);
    vtop->r |= VT_LVAL;
}
```

整数常量可以直接编码到指令中（如 `mov $42, %eax`），但浮点常量不行。TinyCC 将浮点常量放入只读数据段（`.rodata`），然后通过内存引用访问。

**第三步：判断是否需要重新加载**

```c
r = vtop->r & VT_VALMASK;
r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

if (!r_ok || !r2_ok) {
    /* 需要加载/移动 */
}
```

`r_ok` 为真的条件是：值不是左值、已经在寄存器中（`r < VT_CONST`）、且寄存器属于要求的类别。如果任何一个条件不满足，就需要加载。

**第四步：分配寄存器并加载**

```c
if (!r_ok) {
    if (r < VT_CONST && (reg_classes[r] & rc) && !rc2)
        save_reg_upstack(r, 1);  /* 可以复用，先保存其他引用 */
    else
        r = get_reg(rc);         /* 分配新寄存器 */
}
load(r, vtop);  /* 调用后端 load() */
```

**第五步：更新 vstack**

```c
vtop->r = r;  /* 标记值现在在寄存器 r 中 */
```

### 5.4.3 双字类型处理

对于需要两个寄存器的类型（如 32 位平台上的 `long long`），`gv()` 需要额外处理：

```c
if (rc2) {
    /* 加载低字到 r */
    load(r, vtop);
    vtop->r = r;
    /* 分配第二个寄存器 */
    r2 = get_reg(rc2);
    /* 加载高字到 r2 */
    load(r2, vtop);
    vtop->r2 = r2;
}
```

在 x86-64 上，由于指针和 long 都是 64 位，大部分操作只需要一个寄存器。`r2` 主要用于 `__int128`（`VT_QLONG`）类型。

---

## 5.5 `gen_op()` — 二元操作

`gen_op()` 是处理所有二元运算的入口函数。它负责类型提升、指针算术特殊处理、常量折叠，以及最终调用后端的算术生成函数。

### 5.5.1 函数入口

```c
ST_FUNC void gen_op(int op)
{
    int t1, t2, bt1, bt2, t;
    CType type1, combtype;
    int op_class = op;

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op))
        op_class = CMP_OP;
```

操作符被分为三类：普通算术、移位、比较。移位操作的右操作数不需要与左操作数类型匹配；比较操作的结果总是 `int`。

### 5.5.2 函数指针到指针的转换

```c
    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
        /* 函数名退化为函数指针 */
        if (bt2 == VT_FUNC) {
            mk_pointer(&vtop->type);
            gaddrof();
        }
        /* ... */
        goto redo;
    }
```

### 5.5.3 类型组合

```c
    if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
        tcc_error("invalid operand types for binary operation");
    }
```

`combine_types()` 实现 C 语言的"usual arithmetic conversions"——找到两个操作数的公共类型。

### 5.5.4 指针算术

当至少一个操作数是指针时，需要特殊处理：

```c
    if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op_class == CMP_OP)
            goto std_op;  /* 指针比较直接进行 */

        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            /* 两个指针相减：结果是 ptrdiff_t */
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);         /* 计算差值 */
            vtop->type.t = VT_PTRDIFF_T;
            vswap();
            gen_op(TOK_PDIV);     /* 除以元素大小 */
        } else {
            /* 指针 ± 整数：乘以元素大小后相加 */
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');           /* 偏移量 × sizeof(element) */
            gen_opic(op);         /* 指针 + 偏移量 */
            vtop->type = type1;   /* 恢复指针类型 */
        }
    }
```

### 5.5.5 标准算术路径

对于非指针操作：

```c
    t = t2 = combtype.t;
    /* 移位操作的右操作数保持为 int */
    if (op_class == SHIFT_OP)
        t2 = VT_INT;

    /* 无符号操作的特殊处理 */
    if (t & VT_UNSIGNED) {
        if (op == TOK_SAR) op = TOK_SHR;
        if (op == '/') op = TOK_UDIV;
        if (op == '%') op = TOK_UMOD;
        /* ... 比较操作也类似 ... */
    }

    /* 类型转换 */
    vswap();
    gen_cast_s(t);
    vswap();
    gen_cast_s(t2);

    /* 调用常量折叠或后端 */
    if (is_float(t))
        gen_opif(op);    /* 浮点常量折叠 + 后端 */
    else
        gen_opic(op);    /* 整数常量折叠 + 后端 */

    /* 设置结果类型 */
    if (op_class == CMP_OP)
        vtop->type.t = VT_INT;  /* 比较结果总是 int */
    else
        vtop->type.t = t;
```

---

## 5.6 `vstore()` — 存储操作

`vstore()` 将 vstack 栈顶的值存储到次栈顶指定的左值位置。它是赋值操作（`=`、`+=` 等复合赋值的最终步骤）的核心。

### 5.6.1 标量存储

对于基本类型（int、float、指针等），`vstore()` 的核心路径是：

```c
    gv(RC_TYPE(dbt));  /* 将值物化到寄存器 */
    store(r, vtop - 1); /* 调用后端 store() */
```

**延迟类型转换优化**：对于 `char` 和 `short` 类型的目标，`vstore()` 会尝试延迟类型转换：

```c
    if ((dbt == VT_BYTE || dbt == VT_SHORT) && is_integer_btype(sbt)) {
        delayed_cast = 1;
    }
```

这样可以避免先将 `int` 截断为 `char` 再存储（需要额外的 `movzbl` 指令），而是直接存储低字节，让 CPU 的字节/字存储指令自然完成截断。

### 5.6.2 结构体赋值

结构体赋值不能简单地用 `mov` 完成，需要逐字节复制：

```c
    if (sbt == VT_STRUCT) {
        size = type_size(&vtop->type, &align);
        /* 获取目标地址 */
        vpushv(vtop - 1);
        vtop->type.t = VT_PTR;
        gaddrof();
        /* 获取源地址 */
        vswap();
        vtop->type.t = VT_PTR;
        gaddrof();
        /* 生成 memcpy/memmove 调用 */
        vpushi(size);
        vpush_helper_func(TOK_memmove);
        vrott(4);
        gfunc_call(3);
    }
```

注意使用 `memmove` 而非 `memcpy`，因为源和目标可能重叠（如 `a = a`）。

在某些平台上，TinyCC 提供了优化的 `gen_struct_copy()` 路径，直接生成内联的 `rep movsb` 或逐字 `mov` 指令，避免函数调用开销。

### 5.6.3 位域存储

位域存储是最复杂的情况，需要**读-改-写**（read-modify-write）序列：

```c
    if (ft & VT_BITFIELD) {
        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);

        /* 1. 保存左值作为表达式结果（支持链式赋值 s.b = s.a = n;） */
        vdup();
        vtop[-1] = vtop[-2];

        /* 2. 掩码源值 */
        vpushi((1ULL << bit_size) - 1);
        gen_op('&');

        /* 3. 移位到位域位置 */
        vpushi(bit_pos);
        gen_op(TOK_SHL);

        /* 4. 加载目标字 */
        vdup();
        vrott(3);

        /* 5. 清除目标位域区域 */
        vpushi(~((unsigned)mask << bit_pos));
        gen_op('&');

        /* 6. 合并 */
        gen_op('|');

        /* 7. 存储回内存 */
        vstore();
        vpop();
    }
```

这个序列生成的汇编大致如下（假设位域在 bit 4-7）：

```asm
    movl    (%rdi), %eax      # 加载原始字
    andl    $0xffffff0f, %eax  # 清除 bit 4-7
    shll    $4, %ecx           # 新值左移到 bit 4-7
    orl     %ecx, %eax         # 合并
    movl    %eax, (%rdi)       # 写回
```

---

## 5.7 `gen_cast()` — 类型转换

`gen_cast()` 实现 C 语言的类型转换语义。它处理从 vstack 栈顶值到目标类型的所有转换情况。

### 5.7.1 常量折叠

当源值是编译时常量时，转换在编译时完成，不生成任何代码：

```c
    c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c) {
        /* 整数 → 浮点 */
        if (df) {
            if (sbt_bt == VT_LLONG)
                vtop->c.ld = vtop->c.i;
            else
                vtop->c.ld = (uint32_t)vtop->c.i;
            if (dbt == VT_FLOAT)
                vtop->c.f = (float)vtop->c.ld;
            else if (dbt == VT_DOUBLE)
                vtop->c.d = (double)vtop->c.ld;
        }
        /* 浮点 → 整数 */
        else if (sf) {
            if (dbt & VT_UNSIGNED)
                vtop->c.i = (uint64_t)vtop->c.ld;
            else
                vtop->c.i = (int64_t)vtop->c.ld;
        }
        /* 整数 → 整数：截断或符号扩展 */
        else {
            if (dbt_bt == VT_BYTE)
                vtop->c.i &= 0xff;
            else if (dbt_bt == VT_SHORT)
                vtop->c.i &= 0xffff;
            /* ... */
        }
        goto done;
    }
```

### 5.7.2 运行时转换

非常量值需要生成实际的转换指令：

**整数扩展（int → long long）**：

```c
    if (sbt_bt == VT_INT && dbt_bt == VT_LLONG) {
        gv(RC_INT);
        /* 生成 movslq（有符号扩展）或 movl（零扩展，实际上
           x86-64 的 movl 自动零扩展到 64 位） */
        gen_cvt_csti(dbt, vtop, 0);
    }
```

**浮点 ↔ 整数**：

```c
    if (sf && !df) {
        /* 浮点 → 整数 */
        gen_cvt_ftoi1(dbt);  /* 生成 cvttss2si / cvttsd2si */
    } else if (!sf && df) {
        /* 整数 → 浮点 */
        gen_cvt_itof1(dbt);  /* 生成 cvtsi2ss / cvtsi2sd */
    }
```

**float ↔ double**：

```c
    if (sf && df) {
        if (dbt == VT_DOUBLE)
            gen_cvt_ftof(VT_DOUBLE);  /* cvtss2sd */
        else
            gen_cvt_ftof(VT_FLOAT);   /* cvtss2sd → cvtsd2ss */
    }
```

### 5.7.3 VT_MUSTCAST 延迟转换

TinyCC 有一个重要的优化：当从内存加载 `char` 或 `short` 值时，CPU 的 `movsbl`/`movzbl` 指令已经完成了符号扩展/零扩展。但如果后续操作需要完整的 `int` 类型，可能还需要额外的转换。

为避免冗余转换，`gv()` 在加载时设置 `VT_MUSTCAST` 标志，表示"这个值需要在使用前进行延迟转换"。`gen_cast()` 在入口处检查此标志：

```c
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();
```

---

## 5.8 条件跳转优化

### 5.8.1 `gvtst()` — 条件测试生成

`gvtst()` 是条件跳转的核心函数。它的职责是：根据 vstack 栈顶的值，生成跳转到指定标签的条件分支。

```c
static int gvtst(int inv, int t)
{
    int op, x, u;

    gvtst_set(inv, t);
    t = vtop->jtrue, u = vtop->jfalse;
    if (inv)
        x = u, u = t, t = x;
    op = vtop->cmp_op;

    /* 根据操作类型生成跳转 */
    if (op > 1)
        t = gjmp_cond(op ^ inv, t);  /* 条件跳转 */
    else if (op != inv)
        t = gjmp(t);                  /* 无条件跳转 */

    /* 解析互补跳转到当前位置 */
    gsym(u);

    vtop--;
    return t;
}
```

### 5.8.2 避免冗余比较

`gvtst()` 的关键优化是：如果栈顶值已经是 `VT_CMP` 状态（即 CPU 条件标志已经设置好），就**不需要再生成比较指令**。

例如，对于 `if (a > b)`，`gen_op(TOK_GT)` 已经生成了 `cmp` 指令并设置了条件码。`gvtst()` 直接使用这些条件码生成 `jg` 跳转，避免了 `cmp` + `test` 的冗余序列。

### 5.8.3 短路求值

对于逻辑与（`&&`）和逻辑或（`||`），TinyCC 使用**短路求值**：

**`a && b`** 的处理：

```
1. 求值 a
2. gvtst(false, end_label)  — 如果 a 为假，跳转到 end
3. 求值 b
4. gsym(end_label)          — 回填跳转目标
```

**`a || b`** 的处理：

```
1. 求值 a
2. gvtst(true, end_label)   — 如果 a 为真，跳转到 end
3. 求值 b
4. gsym(end_label)          — 回填跳转目标
```

这通过 `vtop->jtrue` 和 `vtop->jfalse` 两个前向跳转链实现。当一个操作数的值确定后，相应的跳转链被解析到正确的位置。

### 5.8.4 `vcheck_cmp()` — 条件码保护

当需要对栈顶下方有 `VT_CMP` 值的栈进行操作时（如 `vswap()`、`vrotb()`），必须先将 `VT_CMP` 物化：

```c
static void vcheck_cmp(void)
{
    /* 不能在有后续指令生成时保留 CPU 标志。
       也不能让 VT_JMP 出现在栈顶以外的位置。 */
    if (vtop->r == VT_CMP || vtop->r == VT_JMP || vtop->r == VT_JMPI) {
        if (!nocode_wanted)
            gv(RC_INT);  /* 物化到整数寄存器 */
    }
}
```

---

## 5.9 寄存器分配

### 5.9.1 `get_reg()` — 寄存器分配器

TinyCC 使用一个简单的**线性扫描**寄存器分配策略：

```c
ST_FUNC int get_reg(int rc)
{
    int r;
    SValue *p;

    /* 第一步：寻找空闲寄存器 */
    for (r = 0; r < NB_REGS; r++) {
        if (reg_classes[r] & rc) {
            if (nocode_wanted)
                return r;  /* 代码抑制模式下随便选一个 */
            for (p = vstack; p <= vtop; p++) {
                if ((p->r & VT_VALMASK) == r || p->r2 == r)
                    goto notfound;
            }
            return r;  /* 找到空闲寄存器 */
        }
    notfound: ;
    }

    /* 第二步：没有空闲寄存器，溢出一个 */
    for (p = vstack; p <= vtop; p++) {
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc))
            goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);  /* 溢出到栈上 */
            return r;
        }
    }
    return -1;  /* 不应到达这里 */
}
```

**分配策略总结：**

1. **复用（Reuse）**：如果值已经在正确类别的寄存器中（`gv()` 检查 `r_ok`），直接使用。
2. **空闲（Free）**：遍历所有寄存器，找到一个不在 vstack 中使用的寄存器。
3. **溢出（Spill）**：如果所有寄存器都被占用，从栈底开始找到第一个使用目标类别寄存器的 vstack 条目，将其溢出到栈上。

**从栈底开始溢出**是一个重要细节：`gen_opi()` 等函数在调用 `gv2()` 后，结果在 `vtop[-1]` 中，`vtop` 被弹出。从栈底溢出确保不会溢出刚刚分配的寄存器。

### 5.9.2 `save_reg()` / `save_reg_upstack()`

```c
ST_FUNC void save_reg_upstack(int r, int n)
{
    for (p = vstack, p1 = vtop - n; p <= p1; p++) {
        if ((p->r & VT_VALMASK) == r || p->r2 == r) {
            if (!l) {
                /* 第一次发现引用：分配临时栈空间并存储 */
                l = get_temp_local_var(size, align, &r2);
                store(r, &sv);
            }
            /* 标记该 vstack 条目已被溢出 */
            p->r = VT_LVAL | VT_LOCAL;
            p->c.i = l;
            p->r2 = r2;
        }
    }
}
```

`save_reg(r)` 溢出所有引用寄存器 `r` 的 vstack 条目。`save_reg_upstack(r, n)` 只溢出 `vtop - n` 以下的条目——这在 `gv()` 中使用，因为栈顶条目即将被覆盖。

### 5.9.3 `get_temp_local_var()`

溢出的寄存器需要临时存储空间。`get_temp_local_var()` 在当前函数的栈帧中分配临时变量：

```c
static int get_temp_local_var(int size, int align, int *r2)
{
    /* 首先尝试复用已不再使用的临时变量 */
    for (i = 0; i < nb_temp_local_vars; i++) {
        temp_var = &arr_temp_local_vars[i];
        if (!(used & (1 << i)) && temp_var->size >= size
            && temp_var->align >= align) {
            *r2 = (VT_CONST + 1) + i;
            return temp_var->location;
        }
    }
    /* 没有可复用的，分配新的 */
    loc = (loc - size) & -align;
    /* ... */
    return loc;
}
```

这避免了每次溢出都增长栈帧，通过复用机制减少了栈空间的浪费。

---

## 5.10 x86-64 后端详解

本节以 x86-64（SysV ABI，Linux）为例，详细展示后端接口的具体实现。源码位于 `x86_64-gen.c`。

### 5.10.1 寄存器集合

x86-64 后端定义了 25 个逻辑寄存器：

```c
#define NB_REGS  25

enum {
    TREG_RAX = 0,   TREG_RCX = 1,   TREG_RDX = 2,
    /* 3 = rbx (不使用) */
    TREG_RSP = 4,   /* 栈指针（不用于通用分配） */
    /* 5 = rbp (帧指针，不使用) */
    TREG_RSI = 6,   TREG_RDI = 7,
    TREG_R8  = 8,   TREG_R9  = 9,
    TREG_R10 = 10,  TREG_R11 = 11,
    /* 12-15 = r12-r15 (callee-saved，不使用) */
    TREG_XMM0 = 16, TREG_XMM1 = 17, TREG_XMM2 = 18, TREG_XMM3 = 19,
    TREG_XMM4 = 20, TREG_XMM5 = 21, TREG_XMM6 = 22, TREG_XMM7 = 23,
    TREG_ST0 = 24,  /* x87 栈顶（用于 long double） */
};
```

**寄存器分类：**

| 类别 | 寄存器 | 用途 |
|------|--------|------|
| `RC_INT` | rax, rcx, rdx, r8-r11 | 通用整数运算 |
| `RC_RAX` | rax | 乘法/除法的隐含操作数 |
| `RC_RCX` | rcx | 移位计数 |
| `RC_RDX` | rdx | 除法的高位 |
| `RC_FLOAT` | xmm0-xmm7 | SSE 浮点运算 |
| `RC_ST0` | st0 | x87 long double |

```c
ST_DATA const int reg_classes[NB_REGS] = {
    /* eax */  RC_INT | RC_RAX,
    /* ecx */  RC_INT | RC_RCX,
    /* edx */  RC_INT | RC_RDX,
    0,                        /* rbx — 不使用 */
    0,                        /* rsp — 栈指针 */
    0,                        /* rbp — 帧指针 */
    RC_RSI,                   /* rsi — 仅参数传递 */
    RC_RDI,                   /* rdi — 仅参数传递 */
    RC_R8,  RC_R9,  RC_R10,  RC_R11,
    0, 0, 0, 0,              /* r12-r15 — callee-saved，不使用 */
    RC_FLOAT | RC_XMM0,      /* xmm0 */
    RC_FLOAT | RC_XMM1,      /* xmm1 */
    RC_FLOAT | RC_XMM2,      /* xmm2 */
    RC_FLOAT | RC_XMM3,      /* xmm3 */
    RC_FLOAT | RC_XMM4,      /* xmm4 */
    RC_FLOAT | RC_XMM5,      /* xmm5 */
    RC_XMM6,                 /* xmm6 — callee-saved on Windows */
    RC_XMM7,                 /* xmm7 — callee-saved on Windows */
    RC_ST0                   /* st0 — x87 */
};
```

注意 rsi 和 rdi **不在** `RC_INT` 中——它们仅用于参数传递，不参与通用整数运算。这是因为 `gv(RC_INT)` 需要找到一个可以自由使用的寄存器，而 rsi/rdi 在某些上下文中可能已被 `gfunc_call()` 用于参数传递。

### 5.10.2 SysV AMD64 调用约定

**参数传递：**

```c
#define REGN 6
static const uint8_t arg_regs[REGN] = {
    TREG_RDI, TREG_RSI, TREG_RDX, TREG_RCX, TREG_R8, TREG_R9
};
```

整数/指针参数按顺序使用 rdi, rsi, rdx, rcx, r8, r9。浮点参数使用 xmm0-xmm7。超出寄存器数量的参数通过栈传递。

**返回值：**

```c
#define REG_IRET  TREG_RAX    /* 整数返回值 */
#define REG_IRE2  TREG_RDX    /* 第二个整数返回值（128 位） */
#define REG_FRET  TREG_XMM0  /* 浮点返回值 */
#define REG_FRE2  TREG_XMM1  /* 第二个浮点返回值 */
```

### 5.10.3 `gfunc_prolog()` — 函数序言

```c
void gfunc_prolog(Sym *func_sym)
{
    /* 跳过序言空间（稍后回填） */
    ind += FUNC_PROLOG_SIZE;  /* FUNC_PROLOG_SIZE = 11 */
    func_sub_sp_offset = ind;

    /* 如果函数返回结构体，添加隐式指针参数 */
    if (ret_mode == x86_64_mode_memory) {
        push_arg_reg(reg_param_index);
        func_vc = loc;
        reg_param_index++;
    }

    /* 处理每个参数 */
    while ((sym = sym->next) != NULL) {
        if (reg_param_index < REGN) {
            if (is_sse_float(type->t)) {
                /* 浮点参数：movq xmmN, loc(%rbp) */
                o(0xd60f66);
                gen_modrm(reg_param_index, VT_LOCAL, NULL, addr);
            } else {
                /* 整数参数：mov rN, loc(%rbp) */
                gen_modrm64(0x89, arg_regs[reg_param_index],
                           VT_LOCAL, NULL, addr);
            }
        }
        addr += 8;
        reg_param_index++;
    }

    /* 可变参数函数：保存所有寄存器参数到栈上 */
    while (reg_param_index < REGN) {
        if (func_var) {
            gen_modrm64(0x89, arg_regs[reg_param_index],
                       VT_LOCAL, NULL, addr);
            addr += 8;
        }
        reg_param_index++;
    }
}
```

**`gfunc_epilog()` — 函数尾声：**

```c
void gfunc_epilog(void)
{
    v = (-loc + 15) & -16;  /* 对齐到 16 字节 */
    saved_ind = ind;

    /* 回填序言代码 */
    ind = func_sub_sp_offset - FUNC_PROLOG_SIZE;
    o(0xe5894855);  /* push %rbp; mov %rsp, %rbp */
    o(0xec8148);    /* sub $v, %rsp */
    gen_le32(v);
    ind = saved_ind;

    /* 生成尾声 */
    o(0xc9);  /* leave */
    o(0xc3);  /* ret */
}
```

**生成的代码结构：**

```asm
func:
    push    %rbp                # 保存帧指针
    mov     %rsp, %rbp          # 建立新帧
    sub     $N, %rsp            # 分配局部变量空间
    mov     %rdi, -8(%rbp)      # 保存第一个参数
    mov     %rsi, -16(%rbp)     # 保存第二个参数
    ; ... 函数体 ...
    leave                       # 等价于 mov %rbp,%rsp; pop %rbp
    ret
```

注意 `gfunc_epilog()` 使用**回填**技术：函数序言的 `push %rbp; mov %rsp, %rbp; sub $N, %rsp` 指令在函数体生成之前无法知道 `N` 的值（因为局部变量在解析过程中陆续分配）。因此先跳过 11 字节，生成函数体，最后在 `gfunc_epilog()` 中回填序言。

### 5.10.4 `load()` — 寄存器加载

`load()` 是 x86-64 后端最复杂的函数之一，它处理从任意 SValue 位置到指定寄存器的加载。

**从常量加载：**

```c
if (v == VT_CONST) {
    if (fr & VT_SYM) {
        /* 符号引用：lea sym(%rip), %r */
        orex(1, 0, r, 0x8d);
        o(0x05 + REG_VALUE(r) * 8);
        gen_addrpc32(fr, sv->sym, fc);
    } else if (is64_type(ft)) {
        if (sv->c.i >> 32) {
            /* 64 位常量：movabs $imm64, %r */
            orex(1, r, 0, 0xb8 + REG_VALUE(r));
            gen_le64(sv->c.i);
        } else {
            /* 32 位常量（零扩展到 64 位）：mov $imm32, %r */
            orex(0, r, 0, 0xb8 + REG_VALUE(r));
            gen_le32(sv->c.i);
        }
    }
}
```

**从栈帧加载（VT_LOCAL）：**

```c
else if (v == VT_LOCAL) {
    /* lea offset(%rbp), %r */
    orex(1, 0, r, 0x8d);
    gen_modrm(r, VT_LOCAL, sv->sym, fc);
}
```

**从内存加载（VT_LVAL）：**

```c
if (fr & VT_LVAL) {
    /* 根据类型选择指令 */
    if ((ft & VT_BTYPE) == VT_FLOAT) {
        b = 0x6e0f66;    /* movd mem, xmm */
    } else if ((ft & VT_BTYPE) == VT_DOUBLE) {
        b = 0x7e0ff3;    /* movq mem, xmm */
    } else if ((ft & VT_TYPE) == VT_BYTE) {
        b = 0xbe0f;      /* movsbl mem, %r (符号扩展) */
    } else if ((ft & VT_TYPE) == (VT_BYTE | VT_UNSIGNED)) {
        b = 0xb60f;      /* movzbl mem, %r (零扩展) */
    } else if ((ft & VT_TYPE) == VT_SHORT) {
        b = 0xbf0f;      /* movswl mem, %r */
    } else {
        b = 0x8b;        /* mov mem, %r */
    }
    gen_modrm64(b, r, fr, sv->sym, fc);
}
```

**ModR/M 编码：**

`gen_modrm()` 和 `gen_modrm64()` 是 x86-64 后端的基础设施，负责生成 ModR/M 字节和 SIB 字节：

```c
static void gen_modrm_impl(int op_reg, int r, Sym *sym, int c, int is_got)
{
    op_reg = REG_VALUE(op_reg) << 3;
    if ((r & VT_VALMASK) == VT_CONST) {
        if (!(r & VT_SYM)) {
            /* 绝对地址：[disp32] */
            o(0x04 | op_reg);
            oad(0x25, c);
        } else {
            /* RIP 相对：(%rip)+disp32 */
            o(0x05 | op_reg);
            gen_addrpc32(r, sym, c);
        }
    } else if ((r & VT_VALMASK) == VT_LOCAL) {
        /* rbp 相对 */
        if (c == (signed char)c) {
            o(0x45 | op_reg);  /* disp8 */
            g(c);
        } else {
            oad(0x85 | op_reg, c);  /* disp32 */
        }
    } else {
        /* 寄存器间接：(%reg) */
        g(0x00 | op_reg | REG_VALUE(r));
    }
}
```

ModR/M 字节的编码格式：

```
  7   6   5   4   3   2   1   0
+---+---+---+---+---+---+---+---+
|  mod  |    reg    |    r/m    |
+---+---+---+---+---+---+---+---+

mod=00: [r/m]              (寄存器间接寻址)
mod=01: [r/m + disp8]      (8 位偏移)
mod=10: [r/m + disp32]     (32 位偏移)
mod=11: r/m (寄存器直接)

reg:    操作码扩展或目标寄存器
r/m:    基址寄存器
```

对于 `VT_LOCAL`（即 `(%rbp)` + 偏移），mod=01 或 mod=10，r/m=101（rbp 的编码）。

### 5.10.5 `store()` — 存储到内存

```c
void store(int r, SValue *v)
{
    if (bt == VT_FLOAT) {
        o(0x7e0f66);     /* movd xmm, mem */
    } else if (bt == VT_DOUBLE) {
        o(0xd60f66);     /* movq xmm, mem */
    } else if (bt == VT_LDOUBLE) {
        o(0xc0d9);       /* fld %st(0) */
        o(0xdb);         /* fstpt mem */
    } else if (bt == VT_BYTE || bt == VT_BOOL) {
        orex(0, 0, r, 0x88);  /* movb %r, mem */
    } else if (is64_type(bt)) {
        op64 = 0x89;           /* movq %r, mem */
    } else {
        orex(0, 0, r, 0x89);  /* movl %r, mem */
    }
    gen_modrm64(op64, r, v->r, v->sym, fc);
}
```

### 5.10.6 `gfunc_call()` — 函数调用

SysV ABI 的函数调用生成是最复杂的后端函数之一：

```c
void gfunc_call(int nb_args)
{
    save_regs(nb_args);  /* 保存所有活跃寄存器 */

    /* 分类每个参数 */
    for (i = 0; i < nb_args; i++) {
        mode = classify_x86_64_arg(&sv->type, ...);
        switch (mode) {
        case x86_64_mode_integer:
            /* 整数参数 → 寄存器或栈 */
            break;
        case x86_64_mode_sse:
            /* 浮点参数 → xmm 寄存器或栈 */
            break;
        case x86_64_mode_memory:
            /* 大结构体 → 通过栈传递（复制） */
            break;
        }
    }

    /* 分配整数参数到寄存器 */
    gen_reg = 0;
    for (i = 0; i < nb_args; i++) {
        if (onstack[i] == 1) {  /* 整数寄存器参数 */
            gv(RC_INT);
            if (gen_reg < REGN) {
                d = arg_regs[gen_reg];
                orex(1, d, r, 0x89);  /* mov %r, %arg_reg */
                o(0xc0 + REG_VALUE(r) * 8 + REG_VALUE(d));
            }
            gen_reg++;
        }
    }

    /* 分配 SSE 参数到 xmm 寄存器 */
    sse_reg = 0;
    for (i = 0; i < nb_args; i++) {
        if (onstack[i] == 2) {  /* SSE 寄存器参数 */
            gv(RC_FLOAT);
            if (sse_reg < 8) {
                /* 确保在正确的 xmmN 中 */
            }
            sse_reg++;
        }
    }

    /* 调用目标 */
    vrotb(nb_args + 1);  /* 将函数地址旋转到栈顶 */
    r = gv(RC_INT);      /* 加载函数地址到寄存器 */
    o(0xff);             /* call *%r */
    o(0xd0 + REG_VALUE(r));

    /* 处理返回值 */
    vtop -= nb_args + 1;  /* 弹出参数和函数地址 */
    vpushi(0);
    PUT_R_RET(vtop, func_vt->type.t);  /* 设置返回值寄存器 */
}
```

---

## 5.11 常量折叠

### 5.11.1 `gen_opic()` — 整数常量折叠

`gen_opic()` 在调用后端 `gen_opi()` 之前，尝试在编译时计算结果：

```c
static void gen_opic(int op)
{
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;

    if (c1 && c2) {
        /* 两个操作数都是常量：完全折叠 */
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '*': l1 *= l2; break;
        case '&': l1 &= l2; break;
        case '|': l1 |= l2; break;
        case '^': l1 ^= l2; break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_EQ:  l1 = (l1 == l2); break;
        /* ... */
        }
        v1->c.i = value64(l1, v1->type.t);
        vtop--;  /* 弹出一个操作数，结果留在 v1 中 */
    }
```

**单操作数优化**：当只有一个操作数是常量时，`gen_opic()` 执行多种代数简化：

```c
    else {
        /* 交换律：将常量放到右侧 */
        if (c1 && (op == '+' || op == '&' || op == '^' || op == '|' || op == '*'))
            vswap();

        /* 零消除 */
        if (c2 && l2 == 0 && (op == '+' || op == '-' || op == '|' || op == '^'))
            vtop--;  /* x + 0 = x */

        /* 恒等消除 */
        if (c2 && l2 == 1 && (op == '*' || op == '/' || op == TOK_PDIV))
            vtop--;  /* x * 1 = x */

        /* 乘法强度削减：2 的幂次乘法 → 移位 */
        if (c2 && (op == '*') && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            int n = 0;
            while (l2 > 1) { l2 >>= 1; n++; }
            vtop->c.i = n;
            op = TOK_SHL;  /* x * 8 → x << 3 */
            goto general_case;
        }

        /* 除法强度削减：2 的幂次除法 → 移位 */
        if (c2 && (op == TOK_PDIV) && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            /* ... 类似处理，转换为 SAR ... */
        }

        /* 2 的幂次取模 → 掩码 */
        if (c2 && (op == TOK_UMOD) && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            vtop->c.i = l2 - 1;
            op = '&';  /* x % 8 → x & 7 */
            goto general_case;
        }
    }
}
```

### 5.11.2 `gen_opif()` — 浮点常量折叠

```c
static void gen_opif(int op)
{
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;

    if (c1 && c2) {
        /* 提取浮点值 */
        if (bt == VT_FLOAT) {
            f1 = v1->c.f; f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d; f2 = v2->c.d;
        } else {
            f1 = v1->c.ld; f2 = v2->c.ld;
        }

        /* 只对有限数进行常量折叠（排除 NaN 和 Infinity） */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;

        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/':
            if (f2 == 0.0 && !CONST_WANTED)
                goto general_case;  /* 除零需要运行时异常 */
            f1 /= f2;
            break;
        }
        /* 存储结果 */
        if (bt == VT_FLOAT) v1->c.f = f1;
        else if (bt == VT_DOUBLE) v1->c.d = f1;
        else v1->c.ld = f1;
        vtop--;
    }
}
```

注意浮点常量折叠的一个重要限制：对于 NaN 和 Infinity，折叠只在常量求值上下文（`CONST_WANTED`）中进行。这是因为运行时的浮点运算可能产生 IEEE 754 异常信号，编译时折叠会丢失这些信号。

---

## 5.12 代码抑制 `nocode_wanted`

### 5.12.1 机制概述

`nocode_wanted` 是一个全局整数变量，用于控制代码生成的抑制。它的不同位有不同的含义：

```c
ST_DATA int nocode_wanted;

#define NODATA_WANTED   (nocode_wanted > 0)          /* 不输出静态数据 */
#define DATA_ONLY_WANTED 0x80000000                   /* 函数外部/静态初始化器 */
#define CODE_OFF_BIT     0x20000000                   /* 不可达代码（如 if(0) 的分支） */
#define NOEVAL_MASK      0x0000FFFF                   /* sizeof/typeof 等不求值上下文 */
#define NOEVAL_WANTED    (nocode_wanted & NOEVAL_MASK)
#define CONST_WANTED_BIT 0x00010000                   /* 常量表达式求值 */
#define CONST_WANTED     (nocode_wanted & CONST_WANTED_MASK)
```

### 5.12.2 各标志的使用场景

**`DATA_ONLY_WANTED`（0x80000000）**：

在函数体外部设置。此时编译器处理全局变量声明和静态初始化器，不应生成可执行代码，但需要生成数据段内容。

**`CODE_OFF_BIT`（0x20000000）**：

在不可达代码路径中设置。例如：

```c
if (0) {
    /* 这段代码的 CODE_OFF_BIT 被设置 */
    x = 1;  /* 不生成任何代码 */
}
```

由 `CODE_OFF()` 和 `CODE_ON()` 宏控制。

**`NOEVAL_MASK`（0x0000FFFF）**：

在 `sizeof()`、`typeof()` 等不产生代码的上下文中，通过 `nocode_wanted++` 递增。这是一个计数器，支持嵌套：

```c
/* sizeof(arr[func()]) — func() 不应被调用 */
nocode_wanted++;   /* 进入 sizeof */
/* 解析 arr[func()] — gen_op 不生成代码 */
nocode_wanted--;   /* 离开 sizeof */
```

**`CONST_WANTED_BIT`（0x00010000）**：

在常量表达式求值中设置（如数组大小、case 标签、静态初始化器）。此时即使在 `nocode_wanted` 上下文中，也需要对常量进行求值。

### 5.12.3 代码抑制的效果

当 `nocode_wanted` 非零时，多个关键函数跳过代码生成：

- `get_reg()`：直接返回第一个匹配的寄存器，不检查是否被占用
- `save_reg()` / `save_reg_upstack()`：直接返回，不生成存储指令
- `gv()`：跳过寄存器分配和加载逻辑（在某些路径上）
- `gfunc_call()`：不生成调用指令
- 各种 `o()`、`g()` 输出函数：在 `NODATA_WANTED` 时不输出字节

### 5.12.4 标签解析与代码抑制的交互

```c
/* 在前向标签处清除 nocode_wanted */
static void gsym(int t) {
    while (t) {
        unsigned char *ptr = cur_text_section->data + t;
        uint32_t n = read32le(ptr);
        /* ... 回填跳转 ... */
        t = n;
    }
    /* 如果标签被使用，清除 CODE_OFF_BIT */
    if (ind)
        nocode_wanted &= ~CODE_OFF_BIT;
}
```

当一个前向跳转的目标标签被解析时，如果跳转可能到达当前位置，`CODE_OFF_BIT` 被清除。这正确处理了如下情况：

```c
if (0)
    goto label;
/* CODE_OFF_BIT 设置 */
x = 1;  /* 不生成代码 */
label:
y = 2;  /* CODE_OFF_BIT 清除，恢复代码生成 */
```

---

## 5.13 完整示例：从 C 代码到 x86-64 汇编

### 5.13.1 示例代码

考虑以下 C 函数：

```c
int compute(int a, int b, int c) {
    int x = a + b * c;
    return x;
}
```

我们将逐步跟踪 TinyCC 代码生成器如何将这个函数翻译为 x86-64 汇编。

### 5.13.2 函数序言

解析到函数定义时，`gfunc_prolog()` 被调用：

```
gfunc_prolog 生成:
    ind += 11  (跳过序言空间)

参数保存:
    mov %rdi, -8(%rbp)     # a → 栈帧偏移 -8
    mov %rsi, -16(%rbp)    # b → 栈帧偏移 -16
    mov %rdx, -24(%rbp)    # c → 栈帧偏移 -24

局部变量 x 分配在偏移 -32
```

序言代码在函数尾声时回填：

```asm
    push    %rbp
    mov     %rsp, %rbp
    sub     $32, %rsp           # 4 个 int 参数/局部变量 × 8 字节
```

### 5.13.3 表达式 `a + b * c` 的 vstack 跟踪

**步骤 1：解析标识符 `a`**

```
vpushsym() 将 a 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
    含义：a 是栈帧偏移 -8 处的 int 左值
```

**步骤 2：解析标识符 `b`**

```
vpushsym() 将 b 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-16, sym=&b_sym }
    vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
```

**步骤 3：解析标识符 `c`**

```
vpushsym() 将 c 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-24, sym=&c_sym }
    vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-16, sym=&b_sym }
    vstack[2]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
```

**步骤 4：执行 `b * c`（gen_op('*')）**

```
gen_op('*'):
  → combine_types: 结果类型为 INT
  → gen_opic('*'):
    - c1=0, c2=0（都不是常量）
    - 不满足任何优化条件
    → 调用 gen_opi('*')
      → gv2(RC_INT, RC_INT):
        - gv(RC_INT) for vtop (c):
          - r = VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → 返回 TREG_RAX (0)
          - load(0, vtop): mov -24(%rbp), %eax
          - vtop->r = 0 (TREG_RAX)
        - gv(RC_INT) for vtop[-1] (b):
          - 注意：vtop 已经改变了，现在 vtop[-1] 是 b
          - r = VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → 返回 TREG_RCX (1)
          - load(1, vtop[-1]): mov -16(%rbp), %ecx
          - vtop[-1].r = 1 (TREG_RCX)
      → 生成指令: imul %ecx, %eax
      → vtop--: 弹出 c，结果留在 vstack[0]

vstack[0]: { type=INT, r=TREG_RAX (0), c.i=0 }  (b*c 的结果在 eax)
vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8 }  (a 仍是左值)
```

**步骤 5：执行 `a + (b*c)`（gen_op('+')）**

```
gen_op('+'):
  → combine_types: 结果类型为 INT
  → gen_opic('+'):
    - c1=0, c2=0
    → 调用 gen_opi('+')
      → cc=0（vtop 不是常量）
      → gv2(RC_INT, RC_INT):
        - gv(RC_INT) for vtop (b*c 结果):
          - r=0 (TREG_RAX), 不是左值, r < VT_CONST, reg_classes[0] & RC_INT ✓
          - r_ok = 1 → 已经在正确寄存器中
        - gv(RC_INT) for vtop[-1] (a):
          - r=VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → TREG_RCX (1) 仍然空闲
          - load(1, vtop[-1]): mov -8(%rbp), %ecx
          - vtop[-1].r = 1 (TREG_RCX)
      → 生成指令: add %ecx, %eax
      → vtop--: 弹出操作数，结果在 TREG_RAX

vstack[0]: { type=INT, r=TREG_RAX (0) }  (a+b*c 的结果在 eax)
```

**步骤 6：赋值给 `x`（vstore）**

```
vstore():
  - 目标（vtop[-1]）是 x 的左值：VT_LOCAL|VT_LVAL, c.i=-32
  - 源（vtop）已在 TREG_RAX 中
  → store(TREG_RAX, vtop[-1]): mov %eax, -32(%rbp)

vstack 清空（vpop）
```

**步骤 7：`return x`**

```
vpushsym() 将 x 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-32 }

gv(RC_IRET):  # RC_IRET = RC_RAX
  - 加载 x 到 rax
  - load(TREG_RAX, vtop): mov -32(%rbp), %eax
  - vtop->r = TREG_RAX

gfunc_epilog():
  - leave
  - ret
```

### 5.13.4 最终汇编输出

```asm
compute:
    push    %rbp
    mov     %rsp, %rbp
    sub     $32, %rsp
    mov     %edi, -8(%rbp)       # 保存参数 a
    mov     %esi, -16(%rbp)      # 保存参数 b
    mov     %edx, -24(%rbp)      # 保存参数 c
    mov     -24(%rbp), %eax      # 加载 c
    mov     -16(%rbp), %ecx      # 加载 b
    imul    %ecx, %eax           # b * c
    mov     -8(%rbp), %ecx       # 加载 a
    add     %ecx, %eax           # a + (b * c)
    mov     %eax, -32(%rbp)      # 存储到 x
    mov     -32(%rbp), %eax      # 加载 x 作为返回值
    leave
    ret
```

### 5.13.5 优化观察

读者可能注意到上述汇编存在冗余：`b * c` 的结果已经在 `%eax` 中，但被存储到 `-32(%rbp)` 后又立即加载回来。这是因为 TinyCC 的代码生成器不做**寄存器分配全局优化**——每个语句的结果被独立处理，不跟踪跨语句的值流。

一个优化的编译器（如 GCC -O2）会生成：

```asm
compute:
    mov     %edx, %eax           # eax = c
    imul    %esi, %eax           # eax = b * c
    add     %edi, %eax           # eax = a + b * c
    ret
```

这需要活跃性分析和全局寄存器分配——正是 TinyCC 为追求编译速度而放弃的优化。

---

## 5.14 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 的代码生成器，核心要点如下：

1. **直接代码生成**：TinyCC 不构建中间表示，直接从语法树生成目标机器码。这一设计以牺牲代码质量为代价，换取了极高的编译速度。

2. **虚拟栈（vstack）**：SValue 栈是代码生成器的核心数据结构。每个 SValue 记录值的类型（`type`）、物理位置（`r`, `r2`）和常量/符号信息（`c`, `sym`）。`r` 字段的编码允许值以多种虚拟形态存在：常量、栈偏移、条件码、跳转结果等。

3. **延迟物化**：`gv()` 函数是值从虚拟形态到物理寄存器的"物化"关口。它处理常量加载、内存解引用、位域提取、寄存器类别匹配等所有情况。

4. **后端接口**：平台无关层通过 `load()`/`store()`、`gfunc_call()`/`gfunc_prolog()`/`gfunc_epilog()`、`gen_opi()`/`gen_opf()`、`gjmp()`/`gjmp_cond()`/`gsym()` 等函数与后端交互。

5. **常量折叠**：`gen_opic()` 和 `gen_opif()` 在调用后端之前尝试编译时求值，包括零消除、恒等消除、乘法强度削减等代数优化。

6. **代码抑制**：`nocode_wanted` 机制通过位标志控制代码生成的开关，支持 sizeof/typeof 不求值、不可达代码消除、常量表达式求值等场景。

7. **寄存器分配**：采用简单的线性扫描策略——复用 → 空闲 → 溢出。溢出使用临时栈变量并支持复用。

8. **x86-64 后端**：使用 rax, rcx, rdx, r8-r11 作为通用整数寄存器，xmm0-xmm7 作为浮点寄存器。SysV ABI 使用 rdi, rsi, rdx, rcx, r8, r9 传递前 6 个整数参数。ModR/M 编码是 x86 指令生成的核心机制。

### 练习

**练习 5.1**：vstack 跟踪

给定以下 C 代码，手动跟踪 vstack 状态在每一步的变化（包括 SValue 的 `type`、`r`、`c.i` 字段），假设目标平台为 x86-64：

```c
int result = (a + 1) * (b - 2);
```

其中 `a` 和 `b` 是 `int` 类型的局部变量，分别位于栈帧偏移 -8 和 -16 处。

参考：[exercises/ex1_vstack.md](exercises/ex1_vstack.md)

**练习 5.2**：汇编预测与验证

编写以下 C 函数，手动预测 TinyCC 生成的 x86-64 汇编，然后使用 `tcc -S` 验证你的预测：

```c
int abs_diff(int a, int b) {
    int d = a - b;
    if (d < 0)
        d = -d;
    return d;
}
```

参考：[exercises/ex2_codegen.md](exercises/ex2_codegen.md)

**练习 5.3**：寄存器分配跟踪

对于以下函数，详细跟踪 `get_reg()` 的每次调用，记录哪些寄存器被分配、哪些被溢出：

```c
int foo(int a, int b, int c, int d) {
    int e = a + b;
    int f = c + d;
    int g = e * f;
    return g + a;
}
```

假设只有 3 个可用的通用整数寄存器（rax, rcx, rdx），分析溢出发生的时机和位置。

参考：[exercises/ex3_register.md](exercises/ex3_register.md)

---

## 参考文献

1. Bellard, F. "TCC: Tiny C Compiler." https://bellard.org/tcc/
2. System V Application Binary Interface, AMD64 Architecture Processor Supplement.
3. Intel Corporation. "Intel 64 and IA-32 Architectures Software Developer's Manual."
4. TinyCC 源码：`tccgen.c`（平台无关代码生成），`x86_64-gen.c`（x86-64 后端），`tcc.h`（核心数据结构定义）。


===== FILE: docs/ch06/examples/read_elf.c =====
/*
 * read_elf.c - A minimal ELF reader that parses headers
 *
 * This program reads and displays the ELF header, section headers,
 * and symbol table of an ELF file. It demonstrates the structures
 * and concepts discussed in Chapter 6.
 *
 * Compile: tcc -o read_elf read_elf.c
 * Usage:   ./read_elf <elf_file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ELF magic bytes */
#define ELFMAG      "\177ELF"
#define SELFMAG     4

/* ELF class */
#define ELFCLASS32  1
#define ELFCLASS64  2

/* ELF data encoding */
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/* ELF type */
#define ET_NONE     0
#define ET_REL      1
#define ET_EXEC     2
#define ET_DYN      3
#define ET_CORE     4

/* ELF machine */
#define EM_386      3
#define EM_X86_64   62
#define EM_AARCH64  183
#define EM_RISCV    243

/* Section types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_DYNSYM      11
#define SHT_INIT_ARRAY  14
#define SHT_FINI_ARRAY  15
#define SHT_GNU_HASH    0x6ffffff6
#define SHT_GNU_verdef  0x6ffffffd
#define SHT_GNU_verneed 0x6ffffffe
#define SHT_GNU_versym  0x6fffffff

/* Section flags */
#define SHF_WRITE       (1 << 0)
#define SHF_ALLOC       (1 << 1)
#define SHF_EXECINSTR   (1 << 2)
#define SHF_TLS         (1 << 10)

/* Symbol binding */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2

/* Symbol type */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6

/* Special section indices */
#define SHN_UNDEF   0
#define SHN_ABS     0xfff1
#define SHN_COMMON  0xfff2

/* ELF64 header */
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

/* ELF64 section header */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

/* ELF64 symbol table entry */
typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

/* ELF64 relocation entry (with addend) */
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define ELF64_ST_BIND(i)    ((i) >> 4)
#define ELF64_ST_TYPE(i)    ((i) & 0xf)
#define ELF64_R_SYM(i)      ((i) >> 32)
#define ELF64_R_TYPE(i)     ((i) & 0xffffffff)

/* Helper: get human-readable type name */
static const char *elf_type_name(uint16_t type)
{
    switch (type) {
    case ET_NONE: return "NONE (No file type)";
    case ET_REL:  return "REL (Relocatable file)";
    case ET_EXEC: return "EXEC (Executable file)";
    case ET_DYN:  return "DYN (Shared object file)";
    case ET_CORE: return "CORE (Core file)";
    default:      return "Unknown";
    }
}

static const char *elf_machine_name(uint16_t machine)
{
    switch (machine) {
    case EM_386:     return "Intel 80386";
    case EM_X86_64:  return "AMD x86-64";
    case EM_AARCH64: return "AArch64";
    case EM_RISCV:   return "RISC-V";
    default:         return "Unknown";
    }
}

static const char *sht_name(uint32_t type)
{
    switch (type) {
    case SHT_NULL:        return "NULL";
    case SHT_PROGBITS:    return "PROGBITS";
    case SHT_SYMTAB:      return "SYMTAB";
    case SHT_STRTAB:      return "STRTAB";
    case SHT_RELA:        return "RELA";
    case SHT_HASH:        return "HASH";
    case SHT_DYNAMIC:     return "DYNAMIC";
    case SHT_NOTE:        return "NOTE";
    case SHT_NOBITS:      return "NOBITS";
    case SHT_REL:         return "REL";
    case SHT_DYNSYM:      return "DYNSYM";
    case SHT_INIT_ARRAY:  return "INIT_ARRAY";
    case SHT_FINI_ARRAY:  return "FINI_ARRAY";
    case SHT_GNU_HASH:    return "GNU_HASH";
    case SHT_GNU_verdef:  return "GNU_verdef";
    case SHT_GNU_verneed: return "GNU_verneed";
    case SHT_GNU_versym:  return "GNU_versym";
    default:              return "UNKNOWN";
    }
}

static const char *stb_name(uint8_t bind)
{
    switch (bind) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK:   return "WEAK";
    default:         return "UNKNOWN";
    }
}

static const char *stt_name(uint8_t type)
{
    switch (type) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    case STT_COMMON:  return "COMMON";
    case STT_TLS:     return "TLS";
    default:          return "UNKNOWN";
    }
}

static const char *shndx_name(uint16_t shndx)
{
    switch (shndx) {
    case SHN_UNDEF:  return "UND";
    case SHN_ABS:    return "ABS";
    case SHN_COMMON: return "COM";
    default:         return NULL;
    }
}

static const char *reloc_type_name_x86_64(uint32_t type)
{
    switch (type) {
    case 0:  return "R_X86_64_NONE";
    case 1:  return "R_X86_64_64";
    case 2:  return "R_X86_64_PC32";
    case 4:  return "R_X86_64_PLT32";
    case 5:  return "R_X86_64_COPY";
    case 6:  return "R_X86_64_GLOB_DAT";
    case 7:  return "R_X86_64_JUMP_SLOT";
    case 8:  return "R_X86_64_RELATIVE";
    case 9:  return "R_X86_64_GOTPCREL";
    case 10: return "R_X86_64_32";
    case 11: return "R_X86_64_32S";
    default: return "UNKNOWN";
    }
}

static void print_flags(uint64_t flags)
{
    if (flags & SHF_WRITE)     printf("WRITE ");
    if (flags & SHF_ALLOC)     printf("ALLOC ");
    if (flags & SHF_EXECINSTR) printf("EXECINSTR ");
    if (flags & SHF_TLS)       printf("TLS ");
}

/* Read ELF header */
static int read_elf_header(FILE *f, Elf64_Ehdr *ehdr)
{
    if (fread(ehdr, sizeof(*ehdr), 1, f) != 1)
        return -1;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Error: not an ELF file\n");
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        fprintf(stderr, "Error: not a 64-bit ELF file\n");
        return -1;
    }
    return 0;
}

/* Display ELF header */
static void print_elf_header(const Elf64_Ehdr *ehdr)
{
    printf("=== ELF Header ===\n");
    printf("  Magic:   ");
    for (int i = 0; i < 16; i++)
        printf("%02x ", ehdr->e_ident[i]);
    printf("\n");
    printf("  Class:                             ELF64\n");
    printf("  Data:                              %s\n",
           ehdr->e_ident[5] == ELFDATA2LSB ? "Little-endian" : "Big-endian");
    printf("  Version:                           %d\n", ehdr->e_ident[6]);
    printf("  OS/ABI:                            %d\n", ehdr->e_ident[7]);
    printf("  Type:                              %s\n", elf_type_name(ehdr->e_type));
    printf("  Machine:                           %s\n", elf_machine_name(ehdr->e_machine));
    printf("  Entry point:                       0x%lx\n", (unsigned long)ehdr->e_entry);
    printf("  Program header offset:             %ld (0x%lx)\n",
           (long)ehdr->e_phoff, (unsigned long)ehdr->e_phoff);
    printf("  Section header offset:             %ld (0x%lx)\n",
           (long)ehdr->e_shoff, (unsigned long)ehdr->e_shoff);
    printf("  Flags:                             0x%x\n", ehdr->e_flags);
    printf("  ELF header size:                   %d\n", ehdr->e_ehsize);
    printf("  Program header entry size:         %d\n", ehdr->e_phentsize);
    printf("  Program header entry count:        %d\n", ehdr->e_phnum);
    printf("  Section header entry size:         %d\n", ehdr->e_shentsize);
    printf("  Section header entry count:        %d\n", ehdr->e_shnum);
    printf("  Section header string table index: %d\n", ehdr->e_shstrndx);
    printf("\n");
}

/* Read section headers */
static Elf64_Shdr *read_section_headers(FILE *f, const Elf64_Ehdr *ehdr)
{
    Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr) * ehdr->e_shnum);
    if (!shdr) return NULL;
    fseek(f, ehdr->e_shoff, SEEK_SET);
    if (fread(shdr, sizeof(Elf64_Shdr), ehdr->e_shnum, f) != ehdr->e_shnum) {
        free(shdr);
        return NULL;
    }
    return shdr;
}

/* Read a string from a string table section */
static const char *read_string(FILE *f, const Elf64_Shdr *strtab, uint32_t offset)
{
    static char buf[256];
    long saved = ftell(f);
    fseek(f, strtab->sh_offset + offset, SEEK_SET);
    size_t i = 0;
    int c;
    while (i < sizeof(buf) - 1 && (c = fgetc(f)) != EOF && c != 0)
        buf[i++] = c;
    buf[i] = 0;
    fseek(f, saved, SEEK_SET);
    return buf;
}

/* Display section headers */
static void print_section_headers(FILE *f, const Elf64_Ehdr *ehdr, Elf64_Shdr *shdr)
{
    const Elf64_Shdr *shstrtab = &shdr[ehdr->e_shstrndx];

    printf("=== Section Headers ===\n");
    printf("  %-5s %-18s %-12s %-18s %-10s %-10s %-5s %-5s %-5s %-5s\n",
           "Nr", "Name", "Type", "Addr", "Off", "Size", "ES", "Flg", "Lk", "Inf");
    printf("  %-5s %-18s %-12s %-18s %-10s %-10s %-5s %-5s %-5s %-5s\n",
           "---", "----", "----", "----", "---", "----", "--", "---", "---", "---");

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char *name = read_string(f, shstrtab, shdr[i].sh_name);
        printf("  [%2d] %-18s %-12s %016lx %08lx %08lx %4lx ",
               i, name, sht_name(shdr[i].sh_type),
               (unsigned long)shdr[i].sh_addr,
               (unsigned long)shdr[i].sh_offset,
               (unsigned long)shdr[i].sh_size,
               (unsigned long)shdr[i].sh_entsize);
        print_flags(shdr[i].sh_flags);
        printf(" %3d %4d\n", shdr[i].sh_link, shdr[i].sh_info);
    }
    printf("\n");
}

/* Display symbol table */
static void print_symbol_table(FILE *f, const Elf64_Shdr *symtab,
                                const Elf64_Shdr *strtab, const Elf64_Shdr *shdr,
                                int shnum)
{
    int nsyms = symtab->sh_size / sizeof(Elf64_Sym);
    Elf64_Sym *syms = malloc(symtab->sh_size);
    if (!syms) return;

    fseek(f, symtab->sh_offset, SEEK_SET);
    if (fread(syms, sizeof(Elf64_Sym), nsyms, f) != nsyms) {
        free(syms);
        return;
    }

    printf("=== Symbol Table (.symtab) ===\n");
    printf("  %-6s %-18s %-18s %-8s %-8s %-8s %-8s\n",
           "Num", "Value", "Size", "Type", "Bind", "Vis", "Ndx");
    printf("  %-6s %-18s %-18s %-8s %-8s %-8s %-8s\n",
           "---", "-----", "----", "----", "----", "---", "---");

    for (int i = 0; i < nsyms; i++) {
        const char *name = read_string(f, strtab, syms[i].st_name);
        uint8_t bind = ELF64_ST_BIND(syms[i].st_info);
        uint8_t type = ELF64_ST_TYPE(syms[i].st_info);

        const char *ndx_str = shndx_name(syms[i].st_shndx);
        char ndx_buf[16];
        if (!ndx_str) {
            snprintf(ndx_buf, sizeof(ndx_buf), "%d", syms[i].st_shndx);
            ndx_str = ndx_buf;
        }

        printf("  %6d %-18s %016lx %-8s %-8s %-8s %-8s\n",
               i, name,
               (unsigned long)syms[i].st_value,
               stt_name(type), stb_name(bind),
               "DEFAULT", ndx_str);
    }
    printf("\n");
    free(syms);
}

/* Display relocation entries */
static void print_relocations(FILE *f, const Elf64_Shdr *rela,
                               const Elf64_Shdr *symtab,
                               const Elf64_Shdr *strtab)
{
    int nrela = rela->sh_size / sizeof(Elf64_Rela);
    Elf64_Rela *relas = malloc(rela->sh_size);
    if (!relas) return;

    fseek(f, rela->sh_offset, SEEK_SET);
    if (fread(relas, sizeof(Elf64_Rela), nrela, f) != nrela) {
        free(relas);
        return;
    }

    /* Read the target section name from section headers */
    printf("=== Relocation Section ===\n");
    printf("  %-18s %-18s %-12s %-8s %-18s\n",
           "Offset", "Addend", "Type", "SymIdx", "Symbol");
    printf("  %-18s %-18s %-12s %-8s %-18s\n",
           "------", "------", "----", "------", "------");

    Elf64_Sym *syms = NULL;
    int nsyms = 0;
    if (symtab) {
        nsyms = symtab->sh_size / sizeof(Elf64_Sym);
        syms = malloc(symtab->sh_size);
        if (syms) {
            fseek(f, symtab->sh_offset, SEEK_SET);
            fread(syms, sizeof(Elf64_Sym), nsyms, f);
        }
    }

    for (int i = 0; i < nrela; i++) {
        uint32_t sym_idx = ELF64_R_SYM(relas[i].r_info);
        uint32_t type = ELF64_R_TYPE(relas[i].r_info);
        const char *sym_name = "(no symtab)";
        if (syms && sym_idx < nsyms) {
            sym_name = read_string(f, strtab, syms[sym_idx].st_name);
        }
        printf("  %016lx %+ld %-12s %6d   %s\n",
               (unsigned long)relas[i].r_offset,
               (long)relas[i].r_addend,
               reloc_type_name_x86_64(type),
               sym_idx, sym_name);
    }
    printf("\n");
    free(relas);
    free(syms);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    Elf64_Ehdr ehdr;
    if (read_elf_header(f, &ehdr) < 0) {
        fclose(f);
        return 1;
    }
    print_elf_header(&ehdr);

    Elf64_Shdr *shdr = read_section_headers(f, &ehdr);
    if (!shdr) {
        fprintf(stderr, "Error: cannot read section headers\n");
        fclose(f);
        return 1;
    }
    print_section_headers(f, &ehdr, shdr);

    /* Find .symtab and .strtab */
    const Elf64_Shdr *shstrtab = &shdr[ehdr.e_shstrndx];
    const Elf64_Shdr *symtab = NULL;
    const Elf64_Shdr *strtab = NULL;

    for (int i = 0; i < ehdr.e_shnum; i++) {
        const char *name = read_string(f, shstrtab, shdr[i].sh_name);
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = &shdr[i];
            strtab = &shdr[shdr[i].sh_link];
        }
    }

    if (symtab && strtab)
        print_symbol_table(f, symtab, strtab, shdr, ehdr.e_shnum);

    /* Print relocation sections */
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdr[i].sh_type == SHT_RELA) {
            /* Find the associated symtab (through sh_link) */
            const Elf64_Shdr *rel_symtab = &shdr[shdr[i].sh_link];
            const Elf64_Shdr *rel_strtab = &shdr[rel_symtab->sh_link];
            const char *name = read_string(f, shstrtab, shdr[i].sh_name);
            printf("  Section: %s\n", name);
            print_relocations(f, &shdr[i], rel_symtab, rel_strtab);
        }
    }

    free(shdr);
    fclose(f);
    return 0;
}


===== FILE: docs/ch06/exercises/ex1_elf.md =====
# 练习 1：使用 readelf 分析 tcc 输出

## 目标

通过 `readelf`、`objdump` 和本章提供的 `read_elf.c` 工具，分析 TinyCC 编译输出的 ELF 文件，加深对 ELF 格式和链接过程的理解。

## 前置准备

```bash
# 确保 tcc 已编译
cd /home/faust/tinycc
make

# 编译本章的 read_elf 工具
./tcc -o book/ch06-linker/examples/read_elf book/ch06-linker/examples/read_elf.c
```

## 任务 1：分析目标文件（.o）

### 1.1 编写并编译测试程序

创建 `test1.c`：

```c
int shared_var = 100;

static int helper(int x) {
    return x * 2;
}

int compute(int a, int b) {
    return helper(a) + b + shared_var;
}
```

编译：

```bash
tcc -c -o test1.o test1.c
```

### 1.2 使用 readelf 分析

```bash
# 查看 ELF 头
readelf -h test1.o

# 回答以下问题：
# Q1: e_type 是什么？为什么？
# Q2: e_entry 是多少？为什么？
# Q3: e_machine 的值是多少？对应什么架构？
```

```bash
# 查看节头表
readelf -S test1.o

# 回答以下问题：
# Q4: 有多少个节？列出所有节名。
# Q5: .text 节的 sh_flags 是什么？这些标志的含义是什么？
# Q6: .bss 节的 sh_type 是什么？它在文件中占多少空间？
# Q7: 哪些节具有 SHF_ALLOC 标志？哪些没有？
```

```bash
# 查看符号表
readelf -s test1.o

# 回答以下问题：
# Q8: 哪些符号是 STB_LOCAL？哪些是 STB_GLOBAL？
# Q9: shared_var 的 st_shndx 是什么？为什么指向 .data？
# Q10: helper 函数的符号绑定是什么？为什么不是 GLOBAL？
```

```bash
# 查看重定位表
readelf -r test1.o

# 回答以下问题：
# Q11: 有哪些重定位条目？分别是什么类型？
# Q12: 如果没有外部函数调用，为什么可能没有重定位条目？
```

### 1.3 使用 read_elf 工具分析

```bash
book/ch06-linker/examples/read_elf test1.o
```

对比 `readelf` 和 `read_elf` 的输出，确认两者是否一致。

## 任务 2：分析可执行文件

### 2.1 编译为可执行文件

创建 `test2.c`：

```c
#include <stdio.h>

int main(void) {
    printf("hello\n");
    return 0;
}
```

```bash
tcc -o test2 test2.c
```

### 2.2 对比 .o 和可执行文件的差异

```bash
# 查看可执行文件的 ELF 头
readelf -h test2

# 回答以下问题：
# Q13: e_type 变成了什么？
# Q14: e_entry 现在是多少？这个地址对应哪个函数？
# Q15: 有多少个程序头（e_phnum）？
```

```bash
# 查看程序头
readelf -l test2

# 回答以下问题：
# Q16: 有几个 PT_LOAD 段？它们的权限分别是什么？
# Q17: PT_INTERP 段的内容是什么？它指定了什么？
# Q18: PT_GNU_RELRO 段保护了哪些节？
```

```bash
# 查看动态节
readelf -d test2

# 回答以下问题：
# Q19: DT_NEEDED 条目列出了哪些共享库？
# Q20: DT_FLAGS 的值是什么？DF_BIND_NOW 意味着什么？
```

```bash
# 查看节头
readelf -S test2

# 回答以下问题：
# Q21: 与 test1.o 相比，新增了哪些节？
# Q22: .got 和 .plt 节分别是什么类型？什么标志？
# Q23: .dynsym 和 .symtab 有什么区别？
```

```bash
# 查看动态符号表
readelf --dyn-syms test2

# 回答以下问题：
# Q24: 动态符号表中有哪些符号？
# Q25: printf 的 st_shndx 是什么？
```

## 任务 3：使用 objdump 反汇编

```bash
# 反汇编 .text 节
objdump -d test1.o

# 回答以下问题：
# Q26: compute 函数中，shared_var 的访问使用了什么寻址模式？
# Q27: helper 函数是直接调用还是通过 PLT？
```

```bash
# 反汇编可执行文件的 main 函数
objdump -d test2 | grep -A 30 '<main>:'

# 回答以下问题：
# Q28: printf 的调用是直接 call 还是 call 到 PLT？
# Q29: PLT 条目的指令序列是什么？
```

## 任务 4：使用 objdump 查看重定位

```bash
# 查看 .o 文件的重定位信息
objdump -r test1.o

# 回答以下问题：
# Q30: 每个重定位条目引用了哪个符号？
# Q31: 如果有 R_X86_64_PLT32 类型，说明该符号是什么性质的？
```

## 任务 5：对比不同优化级别

```bash
# 使用 -O0 和 -O2 分别编译
tcc -c -o test1_O0.o test1.c
tcc -c -o test1_O2.o -O2 test1.c

# 对比两者的 .text 节大小
readelf -S test1_O0.o | grep .text
readelf -S test1_O2.o | grep .text

# 反汇编对比
objdump -d test1_O0.o
objdump -d test1_O2.o
```

## 思考题

1. 为什么可重定位文件（.o）的 e_entry 是 0？
2. 为什么 .bss 节的 sh_type 是 SHT_NOBITS 而不是 SHT_PROGBITS？
3. `static` 函数在符号表中的绑定类型是什么？这与 `static` 变量有何不同？
4. 为什么 TinyCC 的 .data.ro 节（只读数据）不叫 .rodata？
5. 在可执行文件中，.text 和 .plt 都是可执行的，它们有什么区别？

## 提交要求

将以上所有问题的回答整理为一份报告，附上关键命令的输出截图或文本。


===== FILE: docs/ch06/exercises/ex2_reloc.md =====
# 练习 2：追踪外部函数调用的重定位过程

## 目标

编写一个包含外部函数调用的 C 程序，使用 TinyCC 编译为目标文件，然后手动追踪重定位条目的含义和链接器的修补过程。

## 前置知识

- ELF 重定位条目结构（`Elf64_Rela`）
- x86-64 重定位类型（`R_X86_64_PLT32`、`R_X86_64_GOTPCREL`、`R_X86_64_PC32`）
- PC 相对寻址的计算方式

## 任务 1：创建测试程序

创建 `reloc_test.c`：

```c
#include <stdio.h>

int global_arr[4] = {1, 2, 3, 4};

int sum(int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += arr[i];
    return s;
}

int main(void) {
    int result = sum(global_arr, 4);
    printf("sum = %d\n", result);
    return 0;
}
```

编译为目标文件：

```bash
tcc -c -o reloc_test.o reloc_test.c
```

## 任务 2：分析 .text 节中的占位符

### 2.1 反汇编 .text 节

```bash
objdump -d -r reloc_test.o
```

`-r` 标志会在每条指令旁边显示关联的重定位条目。

### 2.2 识别占位符

回答以下问题：

**Q1**: 在 `main` 函数中，有多少条指令含有重定位占位符（即 `00 00 00 00` 操作数）？分别是什么指令？

**Q2**: `sum` 函数中的 `for` 循环访问 `arr[i]` 时，是否有重定位？为什么？

**Q3**: `global_arr` 在 `main` 中是如何被引用的？是直接 PC 相对还是通过 GOT？

## 任务 3：逐条分析重定位条目

### 3.1 查看重定位表

```bash
readelf -r reloc_test.o
```

### 3.2 分析每条重定位

对于每个重定位条目，完成以下分析表：

```
条目 0:
  r_offset = ________
  r_info   = sym=______, type=______
  r_addend = ________
  
  对应的 .text 指令地址: ________
  对应的指令: ________________
  
  修补公式: *ptr = ________________
  
  如果目标符号的最终地址是 0x401234，
  而指令地址是 0x401050，那么修补后的值是：
  ________________________________________
```

对所有重定位条目重复以上分析。

### 3.3 PC 相对偏移的计算

对于 `R_X86_64_PLT32` 类型的重定位：

```
修补值 = 目标地址 + addend - 重定位位置地址
       = S + A - P
```

注意：x86-64 的 PC 相对寻址是从**下一条指令**开始计算的，
但 ELF 重定位中的 P 是指**当前重定位位置的地址**（即操作数的地址，不是下一条指令的地址）。
由于操作数在指令中位于 `call` 之后的 4 字节处，而 `call` 本身占 5 字节，
所以 P 实际上等于"下一条指令地址 - 1"。

**Q4**: 验证上面的公式。假设 `main` 的地址是 0x00，`sum` 的地址是 0x1d，
`call` 指令在 0x3d，操作数在 0x3e，计算修补后的值是否等于 `0x1d + (-4) - 0x3e = -0x25`？
验证 `call -0x25` 是否确实会跳转到 0x1d。

## 任务 4：追踪链接过程

### 4.1 两步链接

```bash
# 第一步：编译为 .o
tcc -c -o reloc_test.o reloc_test.c

# 第二步：链接为可执行文件
tcc -o reloc_test reloc_test.o
```

### 4.2 验证修补结果

```bash
# 反汇编可执行文件
objdump -d reloc_test | grep -A 40 '<main>:'
```

**Q5**: `main` 函数中，原来 `call 0` 的占位符现在变成了什么目标地址？

**Q6**: `global_arr` 的引用方式是否改变了？（提示：在 .o 中可能是 GOTPCREL，在可执行文件中可能直接是 PC32）

**Q7**: 如果使用 `tcc -static` 链接，`printf` 的调用方式会有什么变化？

### 4.3 对比动态链接和静态链接

```bash
# 动态链接（默认）
tcc -o reloc_dyn reloc_test.o

# 静态链接
tcc -static -o reloc_sta reloc_test.o

# 对比两者的 main 函数反汇编
objdump -d reloc_dyn | grep -A 40 '<main>:'
objdump -d reloc_sta | grep -A 40 '<main>:'
```

**Q8**: 在动态链接版本中，`printf` 是通过什么地址调用的？PLT 条目的地址是什么？

**Q9**: 在静态链接版本中，`printf` 是直接调用还是通过 PLT？

## 任务 5：手动模拟链接器

### 5.1 编写两文件程序

`a.c`：
```c
extern int multiply(int x, int y);
int factor = 5;

int main(void) {
    return multiply(factor, 3);
}
```

`b.c`：
```c
int multiply(int x, int y) {
    return x * y;
}
```

```bash
tcc -c -o a.o a.c
tcc -c -o b.o b.c
```

### 5.2 分析各自的重定位

```bash
readelf -r a.o
readelf -r b.o
readelf -s a.o
readelf -s b.o
```

**Q10**: `a.o` 中有哪些未定义符号？它们分别在哪个文件中定义？

**Q11**: `b.o` 中有重定位条目吗？为什么？

### 5.3 手动计算链接结果

假设链接后：
- `.text` 节基地址 = 0x401000
- `a.o` 的 `.text` 在 0x401000，大小 0x20
- `b.o` 的 `.text` 在 0x401020，大小 0x10
- `.data` 节基地址 = 0x402000
- `a.o` 的 `.data` 在 0x402000，`factor` 在 0x402000

**Q12**: `multiply` 的最终地址是什么？

**Q13**: `a.o` 中调用 `multiply` 的 `call` 指令在 0x40101x 处，计算修补后的相对偏移。

**Q14**: 验证你的计算：`call` 目标 = 当前 PC + 偏移 = 0x40102x（应该等于 multiply 的地址）。

## 思考题

1. 为什么 `global_arr` 在动态链接时需要通过 GOT 访问，而在静态链接时可以直接 PC 相对访问？
2. `R_X86_64_PLT32` 和 `R_X86_64_PC32` 有什么区别？为什么 tcc 对内部函数也使用 PLT32？
3. 重定位条目中的 `r_addend` 字段有什么作用？为什么 tcc 生成的 addend 经常是 -4？
4. 如果一个函数既被调用（call）又被取地址（&func），会产生几种重定位？

## 提交要求

1. 完成所有 Q1-Q14 的回答
2. 附上关键的反汇编输出
3. 对于 Q13-Q14，展示完整的计算过程


===== FILE: docs/ch06/exercises/ex3_jit.md =====
# 练习 3：使用 tcc -run 观察 JIT 行为

## 目标

使用 TinyCC 的 `-run` 选项执行 C 程序，通过 `/proc/[pid]/maps` 和其他工具观察 JIT 运行时的内存布局和行为。

## 前置知识

- `tcc_relocate_ex()` 的内存分配策略
- `mmap` 和 `mprotect` 系统调用
- `/proc/[pid]/maps` 文件格式

## 任务 1：基本 JIT 执行

### 1.1 编写测试程序

创建 `jit_test.c`：

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    for (int i = 0; i <= 10; i++)
        printf("factorial(%d) = %d\n", i, factorial(i));
    return 0;
}
```

### 1.2 正常编译执行 vs JIT 执行

```bash
# 方式 1：正常编译为可执行文件再运行
tcc -o jit_test jit_test.c
./jit_test

# 方式 2：JIT 执行
tcc -run jit_test.c
```

**Q1**: 两种方式的输出是否相同？执行速度是否有可感知的差异？

### 1.3 使用 time 对比

```bash
# 编译 + 执行
time (tcc -o jit_test jit_test.c && ./jit_test)

# JIT 执行
time tcc -run jit_test.c
```

**Q2**: JIT 模式的总时间与"编译+执行"相比如何？JIT 模式省去了哪些步骤？

## 任务 2：观察 JIT 内存布局

### 2.1 使用 /proc/self/maps

创建 `jit_maps.c`：

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    /* 先打印地址空间布局 */
    printf("=== Before reading maps ===\n");
    system("cat /proc/self/maps | head -30");
    
    /* 打印一个函数的地址 */
    printf("\nmain is at: %p\n", (void*)main);
    
    return 0;
}
```

```bash
# 正常执行
tcc -o jit_maps jit_maps.c
./jit_maps

# JIT 执行
tcc -run jit_maps.c
```

**Q3**: 在 JIT 模式下，`main` 函数的地址在什么范围内？这个地址对应的内存映射条目的权限是什么？

**Q4**: 在正常执行模式下，`main` 的地址在哪里？与 JIT 模式有何不同？

### 2.2 深入分析 JIT 内存

创建 `jit_memory.c`：

```c
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* 打印当前进程的内存映射 */
static void print_maps(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[256];
    printf("=== Memory Maps ===\n");
    while (fgets(line, sizeof(line), f)) {
        /* 只显示包含 tcc 或 rwx 的条目 */
        if (strstr(line, "rwx") || strstr(line, "[stack]"))
            printf("%s", line);
    }
    fclose(f);
}

int test_func(int x) {
    return x * x + 1;
}

int main(void) {
    printf("test_func at: %p\n", (void*)test_func);
    printf("main at:      %p\n", (void*)main);
    
    print_maps();
    
    /* 验证代码可执行 */
    int result = test_func(5);
    printf("test_func(5) = %d\n", result);
    
    return 0;
}
```

```bash
# JIT 执行
tcc -run jit_memory.c
```

**Q5**: 是否存在权限为 `rwx`（读+写+执行）的内存映射？这对应 `tcc_relocate_ex()` 中的哪种内存分配策略？

**Q6**: 如果系统启用了 SELinux，`tcc_relocate_ex()` 使用什么策略来分配内存？（提示：参考 `CONFIG_SELINUX`）

### 2.3 使用 -bench 选项

```bash
tcc -bench -run jit_test.c
```

**Q7**: `-bench` 选项输出了什么信息？这些信息分别对应代码中的哪些节？

## 任务 3：符号解析观察

### 3.1 JIT 模式下的外部符号

创建 `jit_extern.c`：

```c
#include <stdio.h>
#include <math.h>

int main(void) {
    double pi = acos(-1.0);
    printf("pi = %.10f\n", pi);
    printf("sin(pi/2) = %.10f\n", sin(pi / 2));
    return 0;
}
```

```bash
# JIT 执行（需要链接 libm）
tcc -lm -run jit_extern.c
```

**Q8**: 在 JIT 模式下，`acos` 和 `sin` 这些外部函数是如何被解析的？
（提示：参考 `relocate_syms()` 中 `do_resolve=1` 的分支）

**Q9**: 如果去掉 `-lm` 选项，会发生什么？错误信息是什么？

### 3.2 使用 tcc_get_symbol

创建 `jit_symbol.c`，使用 libtcc API：

```c
/* 此程序需要使用 libtcc API 编译 */
/* 编译: tcc -o jit_symbol jit_symbol.c -ltcc -ldl */
#include <stdio.h>
#include "libtcc.h"

const char *code = 
    "int add(int a, int b) { return a + b; }\n"
    "int mul(int a, int b) { return a * b; }\n";

int main(void) {
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    
    if (tcc_compile_string(s, code) == -1) {
        fprintf(stderr, "Compilation failed\n");
        return 1;
    }
    
    if (tcc_relocate(s) == -1) {
        fprintf(stderr, "Relocation failed\n");
        return 1;
    }
    
    /* 获取编译后的函数地址 */
    int (*add)(int, int) = tcc_get_symbol(s, "add");
    int (*mul)(int, int) = tcc_get_symbol(s, "mul");
    
    if (add && mul) {
        printf("add(3, 4) = %d\n", add(3, 4));
        printf("mul(3, 4) = %d\n", mul(3, 4));
    }
    
    tcc_delete(s);
    return 0;
}
```

**Q10**: `tcc_get_symbol()` 返回的函数指针指向的地址在哪个内存区域？

**Q11**: 调用 `tcc_delete()` 后再调用这些函数指针会发生什么？为什么？

## 任务 4：JIT 与普通链接的对比

### 4.1 对比 GOT/PLT 的使用

创建 `jit_vs_normal.c`：

```c
#include <stdio.h>

int global_val = 42;

int get_val(void) {
    return global_val;
}

int main(void) {
    printf("val = %d\n", get_val());
    return 0;
}
```

```bash
# 普通编译
tcc -o jvn_normal jit_vs_normal.c
objdump -d jvn_normal | grep -A 20 '<main>:'
objdump -d jvn_normal | grep -A 5 '<get_val>:'

# 注意：JIT 模式下无法直接 objdump，但可以通过反汇编
# 或使用 GDB 观察
```

**Q12**: 在普通编译的可执行文件中，`get_val()` 访问 `global_val` 使用了什么指令序列？

**Q13**: 在 JIT 模式下，同样的访问方式是否相同？（提示：JIT 模式下 `build_got_entries()` 仍然会为 GOTPCREL 创建 GOT 条目）

### 4.2 GDB 观察 JIT 代码

```bash
# 编译带调试信息的 JIT 程序
# 使用 gdb 附加到 tcc -run 进程
# 或使用以下技巧：

# 创建一个在 main 中暂停的程序
cat > jit_gdb.c << 'EOF'
#include <stdio.h>

int main(void) {
    volatile int x = 42;
    printf("x = %d, &x = %p\n", x, &x);
    return 0;
}
EOF

# 在 GDB 中运行
# gdb --args tcc -run jit_gdb.c
# (gdb) break main
# (gdb) run
# (gdb) info registers
# (gdb) x/10i $rip
# (gdb) info proc mappings
```

**Q14**: 在 GDB 中，`main` 函数的地址在什么范围？这个范围对应 `/proc/maps` 中的哪个条目？

## 任务 5：性能和限制

### 5.1 JIT 编译大文件

创建一个包含多个函数的较大 C 文件，测试 JIT 编译时间：

```bash
# 生成一个较大的 C 文件
cat > jit_large.c << 'EOF'
#include <stdio.h>

int fib(int n) {
    if (n <= 1) return n;
    return fib(n-1) + fib(n-2);
}

int collatz(int n) {
    int steps = 0;
    while (n != 1) {
        n = (n % 2) ? 3*n+1 : n/2;
        steps++;
    }
    return steps;
}

int main(void) {
    printf("fib(30) = %d\n", fib(30));
    int max_steps = 0, max_n = 0;
    for (int i = 1; i <= 1000; i++) {
        int s = collatz(i);
        if (s > max_steps) { max_steps = s; max_n = i; }
    }
    printf("longest collatz under 1000: %d (%d steps)\n", max_n, max_steps);
    return 0;
}
EOF

# JIT 执行
time tcc -run jit_large.c
```

**Q15**: JIT 模式下，递归函数（如 `fib`）的性能与普通编译相比如何？

### 5.2 JIT 的限制

```bash
# 尝试在 JIT 模式下使用内联汇编
cat > jit_asm.c << 'EOF'
#include <stdio.h>

int main(void) {
    int result;
    __asm__ ("movl $42, %0" : "=r"(result));
    printf("result = %d\n", result);
    return 0;
}
EOF

tcc -run jit_asm.c
```

**Q16**: JIT 模式是否支持内联汇编？如果有问题，是什么问题？

## 思考题

1. `tcc_relocate_ex()` 中的三遍策略（计算大小、复制数据、设置权限）为什么要分三次？能否合并？
2. JIT 模式下，`dlsym(RTLD_DEFAULT, name)` 用于解析未定义符号。这意味着 JIT 程序可以访问哪些符号？
3. 为什么 JIT 模式需要将代码和数据分离到不同的内存页？如果全部放在可写可执行的页中会有什么安全问题？
4. TinyCC 的 JIT 模式与 LuaJIT 的 JIT 编译器有什么本质区别？
5. 如果要在生产环境中使用 `libtcc` 实现一个嵌入式脚本引擎，需要考虑哪些安全和性能问题？

## 提交要求

1. 完成所有 Q1-Q16 的回答
2. 附上关键命令的输出
3. 对于思考题，写一段 200-300 字的分析


===== FILE: docs/ch06/index.md =====
# 第六章 链接器与 ELF 处理

链接器是编译器工具链中最关键的组件之一。它的职责是将多个独立编译的目标文件（object files）合并为一个可执行程序或共享库。TinyCC 的链接器实现在 `tccelf.c` 中，针对 x86-64 平台的重定位处理在 `x86_64-link.c` 中，而 JIT 运行时支持则在 `tccrun.c` 中。本章将逐层剖析 TinyCC 链接器的设计与实现。

---

## 6.1 ELF 文件格式详解

ELF（Executable and Linkable Format）是 UNIX 系统上可执行文件、目标文件和共享库的标准格式。理解 ELF 格式是理解链接器的前提。ELF 文件由三个核心部分组成：ELF 头、程序头表（Program Header Table）和节头表（Section Header Table）。

### 6.1.1 ELF 头（ELF Header）

ELF 头位于文件的最开始，大小固定为 64 字节（64 位格式）。在 TinyCC 中，其定义位于 `elf.h`：

```c
typedef struct {
  unsigned char e_ident[EI_NIDENT]; /* 魔数和其他信息 */
  Elf64_Half    e_type;             /* 目标文件类型 */
  Elf64_Half    e_machine;          /* 体系结构 */
  Elf64_Word    e_version;          /* 目标文件版本 */
  Elf64_Addr    e_entry;            /* 入口点虚拟地址 */
  Elf64_Off     e_phoff;            /* 程序头表文件偏移 */
  Elf64_Off     e_shoff;            /* 节头表文件偏移 */
  Elf64_Word    e_flags;            /* 处理器特定标志 */
  Elf64_Half    e_ehsize;           /* ELF 头大小（字节） */
  Elf64_Half    e_phentsize;        /* 程序头表条目大小 */
  Elf64_Half    e_phnum;            /* 程序头表条目数量 */
  Elf64_Half    e_shentsize;        /* 节头表条目大小 */
  Elf64_Half    e_shnum;            /* 节头表条目数量 */
  Elf64_Half    e_shstrndx;         /* 节头字符串表索引 */
} Elf64_Ehdr;
```

**字节布局（x86-64，Little-endian）：**

```
偏移  大小  字段            说明
0x00  16    e_ident         7f 45 4c 46 02 01 01 00 ... (魔数 + 类别 + 编码 + 版本 + OS/ABI)
0x10  2     e_type          01 00 = ET_REL (可重定位), 02 00 = ET_EXEC, 03 00 = ET_DYN
0x12  2     e_machine       3e 00 = EM_X86_64 (62)
0x14  4     e_version       01 00 00 00 = EV_CURRENT
0x18  8     e_entry         入口点地址（.o 文件为 0）
0x20  8     e_phoff         程序头表偏移（.o 文件通常为 0）
0x28  8     e_shoff         节头表偏移
0x30  4     e_flags         处理器标志
0x34  2     e_ehsize        40 00 = 64 字节
0x36  2     e_phentsize     38 00 = 56 字节
0x38  2     e_phnum         程序头数量
0x3a  2     e_shentsize     40 00 = 64 字节
0x3c  2     e_shnum         节头数量
0x3e  2     e_shstrndx      节名字符串表的索引
```

`e_ident` 数组的前 4 字节是魔数 `\x7fELF`，用于标识 ELF 文件。第 5 字节（`EI_CLASS`）区分 32 位（`ELFCLASS32=1`）和 64 位（`ELFCLASS64=2`）。第 6 字节（`EI_DATA`）指定字节序：`ELFDATA2LSB=1`（小端）、`ELFDATA2MSB=2`（大端）。

在 TinyCC 中，通过宏 `ELFCLASSW` 根据目标平台的指针大小自动选择 ELF32 或 ELF64：

```c
#if PTR_SIZE == 8
# define ELFCLASSW ELFCLASS64
# define ElfW(type) Elf##64##_##type
#else
# define ELFCLASSW ELFCLASS32
# define ElfW(type) Elf##32##_##type
#endif
```

### 6.1.2 程序头（Program Header）

程序头描述了一个段（Segment），告诉操作系统如何将文件映射到内存。程序头仅在可执行文件和共享库中有意义，在可重定位目标文件（`.o`）中通常为空。

```c
typedef struct {
  Elf64_Word  p_type;    /* 段类型 */
  Elf64_Word  p_flags;   /* 段标志 */
  Elf64_Off   p_offset;  /* 文件偏移 */
  Elf64_Addr  p_vaddr;   /* 虚拟地址 */
  Elf64_Addr  p_paddr;   /* 物理地址 */
  Elf64_Xword p_filesz;  /* 文件中大小 */
  Elf64_Xword p_memsz;   /* 内存中大小 */
  Elf64_Xword p_align;   /* 对齐 */
} Elf64_Phdr;
```

常见的段类型包括：

| 类型 | 值 | 说明 |
|------|-----|------|
| `PT_NULL` | 0 | 未使用 |
| `PT_LOAD` | 1 | 可加载段 |
| `PT_DYNAMIC` | 2 | 动态链接信息 |
| `PT_INTERP` | 3 | 程序解释器路径 |
| `PT_GNU_RELRO` | 0x6474e552 | 重定位后只读 |
| `PT_GNU_STACK` | 0x6474e551 | 栈可执行性标记 |

### 6.1.3 节头（Section Header）

节头描述文件中的一个节（Section）。与段面向运行时不同，节面向链接过程。

```c
typedef struct {
  Elf64_Word  sh_name;      /* 节名（字符串表索引） */
  Elf64_Word  sh_type;      /* 节类型 */
  Elf64_Xword sh_flags;     /* 节标志 */
  Elf64_Addr  sh_addr;      /* 虚拟地址 */
  Elf64_Off   sh_offset;    /* 文件偏移 */
  Elf64_Xword sh_size;      /* 节大小 */
  Elf64_Word  sh_link;      /* 关联节索引 */
  Elf64_Word  sh_info;      /* 附加信息 */
  Elf64_Xword sh_addralign; /* 对齐 */
  Elf64_Xword sh_entsize;   /* 条目大小 */
} Elf64_Shdr;
```

**关键节类型（`sh_type`）：**

| 类型 | 值 | 说明 |
|------|-----|------|
| `SHT_NULL` | 0 | 未使用 |
| `SHT_PROGBITS` | 1 | 程序数据（代码、数据） |
| `SHT_SYMTAB` | 2 | 符号表 |
| `SHT_STRTAB` | 3 | 字符串表 |
| `SHT_RELA` | 4 | 含 addend 的重定位表 |
| `SHT_HASH` | 5 | 符号哈希表 |
| `SHT_DYNAMIC` | 6 | 动态链接信息 |
| `SHT_NOBITS` | 8 | BSS 节（不占文件空间） |
| `SHT_REL` | 9 | 不含 addend 的重定位表 |
| `SHT_DYNSYM` | 11 | 动态符号表 |

**关键节标志（`sh_flags`）：**

| 标志 | 值 | 说明 |
|------|-----|------|
| `SHF_WRITE` | 0x1 | 可写 |
| `SHF_ALLOC` | 0x2 | 运行时占用内存 |
| `SHF_EXECINSTR` | 0x4 | 可执行 |
| `SHF_TLS` | 0x400 | 线程局部存储 |

TinyCC 还定义了两个内部标志，不出现在 ELF 文件中：

```c
#define SHF_PRIVATE  0x80000000  /* 不参与链接输出的私有节 */
#define SHF_DYNSYM   0x40000000  /* 标记节为动态符号表 */
```

### 6.1.4 段与节的关系

一个 ELF 文件中，多个节可以映射到同一个段。典型的映射关系：

```
PT_LOAD (RX):  .interp .dynsym .dynstr .hash .gnu.hash .rela.plt .plt .text
PT_LOAD (RW):  .dynamic .got .got.plt .data .bss
PT_DYNAMIC:    .dynamic
PT_GNU_RELRO:  .dynamic .got
```

TinyCC 的 `sort_sections()` 函数（6.10 节详解）负责决定节的排列顺序，`layout_sections()` 函数根据排列结果生成程序头。

---

## 6.2 Section 管理

TinyCC 在内存中用 `Section` 结构体表示 ELF 节。这个结构体是链接器的核心数据结构。

### 6.2.1 Section 结构体

定义在 `tcc.h` 第 561 行：

```c
typedef struct Section {
    unsigned long data_offset;     /* 当前数据偏移 */
    unsigned char *data;           /* 节数据 */
    unsigned long data_allocated;  /* 已分配空间（用于 realloc） */
    TCCState *s1;                  /* 所属编译状态 */
    int sh_name;                   /* ELF 节名（仅输出时使用） */
    int sh_num;                    /* ELF 节编号 */
    int sh_type;                   /* ELF 节类型 */
    int sh_flags;                  /* ELF 节标志 */
    int sh_info;                   /* ELF 节信息 */
    int sh_addralign;              /* ELF 节对齐 */
    int sh_entsize;                /* 条目大小 */
    unsigned long sh_size;         /* 节大小（仅输出时使用） */
    addr_t sh_addr;                /* 重定位后的虚拟地址 */
    unsigned long sh_offset;       /* 文件偏移 */
    int nb_hashed_syms;            /* 哈希表中的符号数 */
    struct Section *link;          /* 关联节（如符号表关联字符串表） */
    struct Section *reloc;         /* 对应的重定位节 */
    struct Section *hash;          /* 符号哈希表节 */
    struct Section *prev;          /* 节栈上的前一个节 */
    char name[1];                  /* 节名（柔性数组） */
} Section;
```

注意 `name[1]` 是 C 语言中常用的"柔性数组"技巧：`Section` 结构体在分配时会额外分配 `strlen(name)` 字节，将节名直接存储在结构体末尾，避免额外的指针间接访问。

### 6.2.2 创建新节：`new_section()`

`new_section()` 是创建节的核心函数，位于 `tccelf.c` 第 229 行：

```c
ST_FUNC Section *new_section(TCCState *s1, const char *name,
                             int sh_type, int sh_flags)
{
    Section *sec;
    sec = tcc_mallocz(sizeof(Section) + strlen(name));
    sec->s1 = s1;
    strcpy(sec->name, name);
    sec->sh_type = sh_type;
    sec->sh_flags = sh_flags;
    switch(sh_type) {
    case SHT_GNU_versym:
        sec->sh_addralign = 2;
        break;
    case SHT_HASH: case SHT_GNU_HASH:
    case SHT_REL:  case SHT_RELA:
    case SHT_DYNSYM: case SHT_SYMTAB:
    case SHT_DYNAMIC: case SHT_GNU_verneed: case SHT_GNU_verdef:
        sec->sh_addralign = PTR_SIZE;
        break;
    case SHT_STRTAB:
        sec->sh_addralign = 1;
        break;
    default:
        sec->sh_addralign = PTR_SIZE;
        break;
    }
    if (sh_flags & SHF_PRIVATE) {
        dynarray_add(&s1->priv_sections, &s1->nb_priv_sections, sec);
    } else {
        sec->sh_num = s1->nb_sections;
        dynarray_add(&s1->sections, &s1->nb_sections, sec);
    }
    return sec;
}
```

关键设计点：

1. **私有节 vs 公共节**：带 `SHF_PRIVATE` 标志的节不会输出到 ELF 文件，仅用于编译器内部的临时数据（如私有符号表 `.dynsymtab`）。
2. **自动对齐**：根据节类型自动设置默认对齐，符号表和哈希表按指针大小对齐，字符串表按 1 字节对齐。
3. **零初始化**：`tcc_mallocz` 确保所有字段初始为零。

### 6.2.3 节数据操作

TinyCC 提供三个函数操作节中的数据：

**`section_add()`**（第 312 行）——按对齐预留空间：

```c
ST_FUNC size_t section_add(Section *sec, addr_t size, int align)
{
    size_t offset, offset1;
    offset = (sec->data_offset + align - 1) & -align;
    offset1 = offset + size;
    if (sec->sh_type != SHT_NOBITS && offset1 > sec->data_allocated)
        section_realloc(sec, offset1);
    sec->data_offset = offset1;
    if (align > sec->sh_addralign)
        sec->sh_addralign = align;
    return offset;
}
```

对齐计算 `(sec->data_offset + align - 1) & -align` 是经典的向上对齐公式。对于 `SHT_NOBITS` 类型的节（如 `.bss`），只更新偏移不分配内存，因为 BSS 节在文件中不占空间。

**`section_ptr_add()`**（第 325 行）——预留空间并返回指针：

```c
ST_FUNC void *section_ptr_add(Section *sec, addr_t size)
{
    size_t offset = section_add(sec, size, 1);
    return sec->data + offset;
}
```

**`section_realloc()`**（第 298 行）——按指数增长策略扩容：

```c
ST_FUNC void section_realloc(Section *sec, unsigned long new_size)
{
    unsigned long size;
    unsigned char *data;
    size = sec->data_allocated;
    if (size == 0) size = 1;
    while (size < new_size) size = size * 2;
    data = tcc_realloc(sec->data, size);
    memset(data + sec->data_allocated, 0, size - sec->data_allocated);
    sec->data = data;
    sec->data_allocated = size;
}
```

指数增长（每次翻倍）确保了摊还 O(1) 的插入时间复杂度，与标准库 `std::vector` 的策略相同。

### 6.2.4 标准节的创建

在 `tccelf_new()`（第 62 行）中，TinyCC 创建了一组标准节：

```c
text_section    = new_section(s, ".text",    SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
data_section    = new_section(s, ".data",    SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
rodata_section  = new_section(s, ".data.ro", SHT_PROGBITS, SHF_ALLOC);
bss_section     = new_section(s, ".bss",     SHT_NOBITS,   SHF_ALLOC | SHF_WRITE);
tdata_section   = new_section(s, ".tdata",   SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
tbss_section    = new_section(s, ".tbss",    SHT_NOBITS,   SHF_ALLOC | SHF_WRITE | SHF_TLS);
common_section  = new_section(s, ".common",  SHT_NOBITS,   SHF_PRIVATE);
common_section->sh_num = SHN_COMMON;
```

注意：
- `.data.ro` 是 TinyCC 对只读数据节的命名（区别于标准的 `.rodata`），使用 `SHF_ALLOC` 但不含 `SHF_WRITE`。
- `.common` 是一个私有节，用于收集 COMMON 符号，其 `sh_num` 被设为 `SHN_COMMON`（0xfff2）。
- TLS 节（`.tdata`/`.tbss`）具有 `SHF_TLS` 标志。

---

## 6.3 ELF 符号

符号（Symbol）是链接的基本单位。函数名、全局变量名、节名等都通过符号表来表示。

### 6.3.1 Elf64_Sym 结构体

```c
typedef struct {
  Elf64_Word    st_name;   /* 符号名（字符串表索引） */
  unsigned char st_info;   /* 符号类型和绑定 */
  unsigned char st_other;  /* 符号可见性 */
  Elf64_Section st_shndx;  /* 节索引 */
  Elf64_Addr    st_value;  /* 符号值 */
  Elf64_Xword   st_size;   /* 符号大小 */
} Elf64_Sym;
```

注意 64 位版本与 32 位版本的字段顺序不同——64 位版本将 `st_info` 和 `st_other` 提前，以保证 `st_value` 的 8 字节对齐。

`st_info` 字段编码了两个信息：

```c
#define ELF64_ST_BIND(val)     ((val) >> 4)         /* 高 4 位：绑定 */
#define ELF64_ST_TYPE(val)     ((val) & 0xf)        /* 低 4 位：类型 */
#define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
```

### 6.3.2 符号绑定（Binding）

| 绑定 | 值 | 说明 |
|------|-----|------|
| `STB_LOCAL` | 0 | 局部符号，仅在本文件可见 |
| `STB_GLOBAL` | 1 | 全局符号，所有文件可见 |
| `STB_WEAK` | 2 | 弱符号，可被全局符号覆盖 |

TinyCC 在 `set_elf_sym()`（第 700 行）中处理符号冲突的规则：

- **GLOBAL 覆盖 WEAK**：如果新定义是 `STB_GLOBAL` 而已有定义是 `STB_WEAK`，则新定义覆盖旧定义。
- **WEAK 不覆盖 GLOBAL**：反之则忽略。
- **两个 WEAK**：保留先定义的。
- **数据覆盖 COMMON/BSS**：如果已有符号在 `SHN_COMMON` 或 BSS 节，而新符号在其他节，则新定义优先。
- **两个 GLOBAL**：报错 "defined twice"。

### 6.3.3 符号类型（Type）

| 类型 | 值 | 说明 |
|------|-----|------|
| `STT_NOTYPE` | 0 | 未指定类型 |
| `STT_OBJECT` | 1 | 数据对象（变量） |
| `STT_FUNC` | 2 | 函数 |
| `STT_SECTION` | 3 | 与节关联的符号 |
| `STT_FILE` | 4 | 源文件名 |
| `STT_COMMON` | 5 | COMMON 数据 |
| `STT_TLS` | 6 | 线程局部存储 |

### 6.3.4 特殊节索引

| 索引 | 值 | 说明 |
|------|-----|------|
| `SHN_UNDEF` | 0 | 未定义（外部引用） |
| `SHN_ABS` | 0xfff1 | 绝对值，不参与重定位 |
| `SHN_COMMON` | 0xfff2 | COMMON 符号 |

### 6.3.5 符号可见性（Visibility）

`st_other` 的低 2 位编码可见性：

```c
#define STV_DEFAULT   0  /* 默认规则 */
#define STV_INTERNAL  1  /* 处理器特定的隐藏类 */
#define STV_HIDDEN    2  /* 其他模块不可见 */
#define STV_PROTECTED 3  /* 不可抢占，但导出 */
```

可见性的传播规则在 `set_elf_sym()` 中实现：取两者中更严格的可见性。

### 6.3.6 符号表操作

**`put_elf_sym()`**——插入新符号到符号表：

```c
ST_FUNC int put_elf_sym(Section *s, addr_t value, unsigned long size,
    int info, int other, int shndx, const char *name)
```

它将符号追加到符号表节的数据中，并更新关联的哈希表。哈希表在负载因子超过 2 时自动重建（`rebuild_hash()`）。

**`find_elf_sym()`**——按名称查找符号：

```c
ST_FUNC int find_elf_sym(Section *s, const char *name)
```

通过 ELF 哈希函数定位桶，然后遍历链表查找。哈希函数定义在 `tccelf.c` 第 389 行：

```c
static ElfW(Word) elf_hash(const unsigned char *name)
{
    ElfW(Word) h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
```

**`set_elf_sym()`**——添加或更新符号（处理冲突）：

```c
ST_FUNC int set_elf_sym(Section *s, addr_t value, unsigned long size,
                       int info, int other, int shndx, const char *name)
```

这是最高层的符号操作函数，实现了 6.3.2 节描述的冲突解决规则。

---

## 6.4 重定位

重定位（Relocation）是链接器的核心功能。当编译器生成目标文件时，它无法知道外部符号或跨节引用的最终地址，因此在指令中留下"占位符"，并生成重定位条目告诉链接器如何修补这些地址。

### 6.4.1 重定位条目结构

x86-64 使用带 addend 的 `Elf64_Rela`：

```c
typedef struct {
  Elf64_Addr   r_offset;  /* 需要修补的位置 */
  Elf64_Xword  r_info;    /* 符号索引和重定位类型 */
  Elf64_Sxword r_addend;  /* 加数 */
} Elf64_Rela;
```

`r_info` 的编码：

```c
#define ELF64_R_SYM(i)       ((i) >> 32)          /* 高 32 位：符号索引 */
#define ELF64_R_TYPE(i)      ((i) & 0xffffffff)   /* 低 32 位：重定位类型 */
#define ELF64_R_INFO(sym,type) ((((Elf64_Xword)(sym)) << 32) + (type))
```

TinyCC 通过宏统一处理 32/64 位：

```c
#if PTR_SIZE == 8
# define ElfW_Rel ElfW(Rela)
# define SHT_RELX SHT_RELA
#else
# define ElfW_Rel ElfW(Rel)
# define SHT_RELX SHT_REL
#endif
```

### 6.4.2 x86-64 重定位类型

在 `x86_64-link.c` 中定义了关键的重定位类型常量：

```c
#define R_DATA_32   R_X86_64_32S    /* 32 位有符号数据重定位 */
#define R_DATA_PTR  R_X86_64_64     /* 64 位指针重定位 */
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_GLOB_DAT  R_X86_64_GLOB_DAT
#define R_COPY      R_X86_64_COPY
#define R_RELATIVE  R_X86_64_RELATIVE
```

**常用重定位类型的语义：**

| 类型 | 值 | 计算公式 | 用途 |
|------|-----|---------|------|
| `R_X86_64_64` | 1 | `S + A` | 绝对 64 位地址（数据引用） |
| `R_X86_64_PC32` | 2 | `S + A - P` | PC 相对 32 位（本地调用） |
| `R_X86_64_32` | 10 | `S + A` | 绝对 32 位无符号 |
| `R_X86_64_32S` | 11 | `S + A` | 绝对 32 位有符号 |
| `R_X86_64_GOTPCREL` | 9 | `G + GOT + A - P` | PC 相对 GOT 条目 |
| `R_X86_64_PLT32` | 4 | `L + A - P` | PC 相对 PLT 条目 |
| `R_X86_64_GLOB_DAT` | 6 | `S` | GOT 条目初始化 |
| `R_X86_64_JUMP_SLOT` | 7 | `S` | PLT 条目初始化 |
| `R_X86_64_RELATIVE` | 8 | `B + A` | 基址相对（动态） |
| `R_X86_64_COPY` | 5 | — | 数据复制重定位 |

其中 `S` = 符号值，`A` = addend，`P` = 重定位位置，`G` = GOT 偏移，`L` = PLT 条目地址，`B` = 基地址。

### 6.4.3 `relocate()` 函数

`x86_64-link.c` 第 201 行的 `relocate()` 函数是重定位的核心实现。它根据重定位类型对目标位置进行不同的修补：

```c
ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    switch (type) {
    case R_X86_64_64:
        /* 对于 DLL 输出：生成 R_RELATIVE 或保留动态重定位 */
        if (s1->output_type & TCC_OUTPUT_DYN) { ... }
        add64le(ptr, val);           /* *ptr += val (64位) */
        break;
    case R_X86_64_32:
    case R_X86_64_32S:
        /* 溢出检查：32S 要求 val == (int)val */
        add32le(ptr, val);           /* *ptr += val (32位) */
        break;
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
        diff = (long long)val - addr;
        add32le(ptr, diff);          /* *ptr += (val - addr) */
        break;
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        add32le(ptr, s1->got->sh_addr - addr +
                     get_sym_attr(s1, sym_index, 0)->got_offset - 4);
        break;
    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
        write64le(ptr, val - rel->r_addend);
        break;
    /* ... TLS 重定位省略 ... */
    }
}
```

**关键设计**：
- 对于 `R_X86_64_64` 在 DLL 输出中，TinyCC 不直接写入绝对地址，而是生成 `R_RELATIVE` 动态重定位条目，让动态链接器在加载时修补。
- `R_X86_64_GOTPCREL` 的计算涉及 GOT 表基址和符号的 GOT 偏移，实现对 GOT 的 PC 相对引用。
- TLS 重定位（`R_X86_64_TLSGD`、`R_X86_64_TLSLD`）包含指令序列的模式匹配和替换优化。

### 6.4.4 重定位表的创建

`put_elf_reloca()`（第 794 行）负责向节添加重定位条目：

```c
ST_FUNC void put_elf_reloca(Section *symtab, Section *s, unsigned long offset,
                            int type, int symbol, addr_t addend)
{
    Section *sr = s->reloc;
    if (!sr) {
        char buf[256];
        snprintf(buf, sizeof(buf), REL_SECTION_FMT, s->name);
        sr = new_section(s->s1, buf, SHT_RELX, symtab->sh_flags);
        sr->sh_entsize = sizeof(ElfW_Rel);
        sr->link = symtab;
        sr->sh_info = s->sh_num;
        s->reloc = sr;
    }
    ElfW_Rel *rel = section_ptr_add(sr, sizeof(ElfW_Rel));
    rel->r_offset = offset;
    rel->r_info = ELFW(R_INFO)(symbol, type);
    rel->r_addend = addend;
}
```

注意重定位节的命名约定：对于 64 位是 `.rela.text`（`REL_SECTION_FMT` = `".rela%s"`），对于 32 位是 `.rel.text`。重定位节通过 `sh_info` 字段关联到被重定位的节。

---

## 6.5 目标文件加载

`tcc_load_object_file()`（`tccelf.c` 第 3260 行）负责加载 `.o` 文件并将其内容合并到当前编译状态中。这是链接器处理输入文件的核心函数。

### 6.5.1 加载流程

函数的执行分为五个阶段：

**阶段 1：验证 ELF 头**

```c
lseek(fd, file_offset, SEEK_SET);
if (tcc_object_type(fd, &ehdr) != AFF_BINTYPE_REL)
    goto invalid;
if (ehdr.e_ident[5] != ELFDATA2LSB ||
    ehdr.e_machine != EM_TCC_TARGET) {
    return tcc_error_noabort("invalid object file");
}
```

验证文件是可重定位目标文件（`ET_REL`），字节序为小端，机器类型与目标平台匹配。

**阶段 2：加载节头和符号表**

```c
shdr = load_data(fd, file_offset + ehdr.e_shoff,
                 sizeof(ElfW(Shdr)) * ehdr.e_shnum);
strsec = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);

for(i = 1; i < ehdr.e_shnum; i++) {
    sh = &shdr[i];
    if (sh->sh_type == SHT_SYMTAB) {
        nb_syms = sh->sh_size / sizeof(ElfW(Sym));
        symtab = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);
        sh = &shdr[sh->sh_link];
        strtab = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);
    }
}
```

**阶段 3：合并节**

对输入文件中的每个节，在当前编译状态中查找同名节。如果找到，将数据追加到现有节；如果未找到，创建新节：

```c
for(j = 1; j < s1->nb_sections; j++) {
    s = s1->sections[j];
    if (strcmp(s->name, sh_name)) continue;
    /* linkonce 节不重复添加 */
    if (!strncmp(sh_name, ".gnu.linkonce", 13)) {
        sm_table[i].link_once = 1;
        goto next;
    }
    goto found;
}
s = new_section(s1, sh_name, sh->sh_type, sh->sh_flags & ~SHF_GROUP);
found:
    offset = section_add(s, size, sh->sh_addralign);
    sm_table[i].offset = offset;
    sm_table[i].s = s;
    if (sh->sh_type != SHT_NOBITS && size) {
        lseek(fd, file_offset + sh->sh_offset, SEEK_SET);
        full_read(fd, s->data + offset, size);
    }
```

**阶段 4：重映射符号**

将输入文件的符号索引映射到合并后的符号表：

```c
for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
    name = strtab + sym->st_name;
    if (sym->st_shndx == SHN_UNDEF) {
        sym_index = set_elf_sym(symtab_section, 0, 0,
            sym->st_info, other, SHN_UNDEF, name);
    } else if (sym->st_shndx < SHN_LORESERVE) {
        /* 映射节索引并调整符号值 */
        s = sm_table[sym->st_shndx].s;
        sym_index = set_elf_sym(symtab_section,
            sym->st_value + sm_table[sym->st_shndx].offset,
            sym->st_size, sym->st_info, other, s->sh_num, name);
    }
    old_to_new_syms[i] = sym_index;
}
```

**阶段 5：处理重定位**

更新重定位条目中的符号索引，使其指向合并后的符号表：

```c
for_each_elem(sr, 0, rel, ElfW_Rel) {
    sym_index = ELFW(R_SYM)(rel->r_info);
    type = ELFW(R_TYPE)(rel->r_info);
    sym_index = old_to_new_syms[sym_index];
    rel->r_info = ELFW(R_INFO)(sym_index, type);
    rel->r_offset += sm_table[sh->sh_info].offset;
}
```

注意 `rel->r_offset` 需要加上节合并时的偏移量，因为节数据被追加到了现有节的末尾。

---

## 6.6 静态库 .a 加载

静态库（archive）是多个目标文件的打包格式，以 `.a` 为后缀。

### 6.6.1 ar 格式

ar 格式的结构非常简单：

```
!<arch>\n                    # 魔数（8 字节）
ar_header[1]                 # 第一个成员头
member_data[1]               # 第一个成员数据
ar_header[2]                 # 第二个成员头
member_data[2]               # ...
...
```

每个 ar_header 固定 60 字节：

```
偏移  大小  字段
0     16    ar_name   （空格填充，以 '/' 结尾）
16    12    ar_date
28    6     ar_uid
34    6     ar_gid
40    8     ar_mode
48    10    ar_size
58    2     ar_fmag    "`\n"
```

### 6.6.2 `tcc_load_archive()` 函数

`tccelf.c` 第 3656 行：

```c
ST_FUNC int tcc_load_archive(TCCState *s1, int fd, int alacarte)
{
    ArchiveHeader hdr;
    int size, len;
    unsigned long file_offset;
    ElfW(Ehdr) ehdr;

    file_offset = sizeof ARMAG - 1;  /* 跳过魔数 "!<arch>\n" */

    for(;;) {
        len = read_ar_header(fd, file_offset, &hdr);
        if (len == 0) return 0;
        if (len < 0) return tcc_error_noabort("invalid archive");
        file_offset += len;
        size = strtol(hdr.ar_size, NULL, 0);
        if (alacarte) {
            /* COFF 符号表：选择性加载 */
            if (!strcmp(hdr.ar_name, "/"))
                return tcc_load_alacarte(s1, fd, size, 4);
            if (!strcmp(hdr.ar_name, "/SYM64/"))
                return tcc_load_alacarte(s1, fd, size, 8);
        } else if (tcc_object_type(fd, &ehdr) == AFF_BINTYPE_REL) {
            /* 顺序加载：加载每个 .o 成员 */
            if (tcc_load_object_file(s1, fd, file_offset) < 0)
                return -1;
        }
        file_offset = (file_offset + size + 1) & ~1; /* 对齐到偶数 */
    }
}
```

### 6.6.3 选择性加载

ar 格式有两种加载模式：

1. **全量加载（`alacarte=0`）**：按顺序加载库中的所有 `.o` 文件。这是 TinyCC 的默认行为。
2. **选择性加载（`alacarte=1`）**：利用库中的符号表（`/` 或 `/SYM64/` 成员），只加载包含未定义符号引用的 `.o` 文件。`tcc_load_alacarte()` 函数实现了这个逻辑——它读取符号表，检查哪些符号在当前编译状态中是未定义的，然后只加载包含这些符号定义的成员。

选择性加载能显著减少链接时间，尤其是对于大型库。但 TinyCC 的 `alacarte` 参数默认为 0，这意味着它采用全量加载策略，依赖后续的符号解析阶段来处理冲突。

---

## 6.7 共享库 .so 加载

`tcc_load_dll()`（`tccelf.c` 第 3828 行）负责加载共享库（`.so` 文件），提取其中的动态符号信息。

### 6.7.1 加载流程

```c
ST_FUNC int tcc_load_dll(TCCState *s1, int fd,
                         const char *filename, int level)
{
    /* 1. 读取并验证 ELF 头 */
    full_read(fd, &ehdr, sizeof(ehdr));
    if (ehdr.e_ident[5] != ELFDATA2LSB ||
        ehdr.e_machine != EM_TCC_TARGET)
        return tcc_error_noabort("bad architecture");

    /* 2. 加载节头，提取 SHT_DYNAMIC 和 SHT_DYNSYM */
    shdr = load_data(fd, ehdr.e_shoff, ...);
    for(i = 0, sh = shdr; i < ehdr.e_shnum; i++, sh++) {
        switch(sh->sh_type) {
        case SHT_DYNAMIC:
            dynamic = load_data(fd, sh->sh_offset, sh->sh_size);
            break;
        case SHT_DYNSYM:
            dynsym = load_data(fd, sh->sh_offset, sh->sh_size);
            dynstr = load_data(fd, sh1->sh_offset, sh1->sh_size);
            break;
        case SHT_GNU_verdef:  /* 版本定义 */
        case SHT_GNU_verneed: /* 版本需求 */
        case SHT_GNU_versym:  /* 版本符号 */
            ...
        }
    }

    /* 3. 提取 SONAME */
    soname = tcc_basename(filename);
    for(i = 0, dt = dynamic; i < nb_dts; i++, dt++)
        if (dt->d_tag == DT_SONAME)
            soname = dynstr + dt->d_un.d_val;

    /* 4. 检查是否已加载 */
    if (tcc_add_dllref(s1, soname, level)->found)
        goto ret_success;

    /* 5. 导出动态符号到编译状态 */
    for(i = 1, sym = dynsym + 1; i < nb_syms; i++, sym++) {
        if (ELFW(ST_BIND)(sym->st_info) == STB_LOCAL)
            continue;
        name = dynstr + sym->st_name;
        sym_index = set_elf_sym(s1->dynsymtab_section,
            sym->st_value, sym->st_size,
            sym->st_info, sym->st_other, sym->st_shndx, name);
    }
}
```

### 6.7.2 动态段（Dynamic Section）

动态段由一系列 `Elf64_Dyn` 条目组成：

```c
typedef struct {
  Elf64_Sxword d_tag;   /* 条目类型 */
  union {
      Elf64_Xword d_val;
      Elf64_Addr  d_ptr;
  } d_un;
} Elf64_Dyn;
```

常见标签：

| 标签 | 说明 |
|------|------|
| `DT_NEEDED` | 需要的共享库名称 |
| `DT_SONAME` | 本库的名称 |
| `DT_SYMTAB` | 动态符号表地址 |
| `DT_STRTAB` | 字符串表地址 |
| `DT_STRSZ` | 字符串表大小 |
| `DT_HASH` | 符号哈希表地址 |
| `DT_GNU_HASH` | GNU 哈希表地址 |

### 6.7.3 版本符号信息

TinyCC 支持处理 `.gnu.version`、`.gnu.version_r` 和 `.gnu.version_d` 节。这些节用于支持符号版本化（symbol versioning），允许同一符号在不同版本的库中有不同的定义。

`store_version()` 函数解析版本信息并存储在 `sym_versions` 数组中，后续在生成输出文件时会创建对应的 `.gnu.version` 和 `.gnu.version_r` 节。

### 6.7.4 `level` 参数

`level` 参数控制库的引用层级：
- `level = 0`：用户直接指定的库（如 `-lfoo`），需要在输出文件的 `DT_NEEDED` 中记录。
- `level > 0`：被其他库间接引用的库。

---

## 6.8 符号解析

符号解析是将未定义的符号引用与已定义的符号进行匹配的过程。

### 6.8.1 `relocate_syms()` 函数

`relocate_syms()`（`tccelf.c` 第 1079 行）遍历符号表，解析未定义符号：

```c
ST_FUNC void relocate_syms(TCCState *s1, Section *symtab, int do_resolve)
{
    for_each_elem(symtab, 1, sym, ElfW(Sym)) {
        sh_num = sym->st_shndx;
        if (sh_num == SHN_UNDEF) {
            name = (char *) s1->symtab->link->data + sym->st_name;
            if (do_resolve) {
                /* JIT 模式：使用 dlsym() 解析符号 */
                void *addr = dlsym(RTLD_DEFAULT, name_ud);
                if (addr) {
                    sym->st_value = (addr_t) addr;
                    goto found;
                }
            } else if (s1->dynsym && find_elf_sym(s1->dynsym, name)) {
                goto found; /* 动态符号存在，稍后处理 */
            }
            sym_bind = ELFW(ST_BIND)(sym->st_info);
            if (sym_bind == STB_WEAK)
                sym->st_value = 0;  /* 弱符号允许未定义 */
            else
                tcc_error_noabort("unresolved reference to '%s'", name);
        } else if (sh_num < SHN_LORESERVE) {
            /* 已定义符号：加上节基地址 */
            sym->st_value += s1->sections[sym->st_shndx]->sh_addr;
        }
    }
}
```

**符号解析的 `do_resolve` 参数：**

| 值 | 含义 |
|----|------|
| 0 | 普通链接模式，未定义符号报错或交给动态链接器 |
| 1 | JIT 模式，使用 `dlsym()` 在运行时解析 |
| 2 | 重定位动态符号表（`.dynsym`），跳过未定义符号 |

### 6.8.2 COMMON 符号处理

`resolve_common_syms()`（第 1931 行）将 COMMON 符号分配到 BSS 节：

```c
ST_FUNC void resolve_common_syms(TCCState *s1)
{
    ElfW(Sym) *sym;
    for_each_elem(symtab_section, 1, sym, ElfW(Sym)) {
        if (sym->st_shndx == SHN_COMMON) {
            /* st_value 存储的是对齐要求 */
            sym->st_value = section_add(bss_section,
                                        sym->st_size, sym->st_value);
            sym->st_shndx = bss_section->sh_num;
        }
    }
    tcc_add_linker_symbols(s1);
}
```

COMMON 符号是 C 语言中未初始化的全局变量的特殊表示。在 C 语言的传统模型中，不同编译单元中同名的 COMMON 符号会被合并（Tentative Definition 规则）。TinyCC 通过将所有 COMMON 符号收集到 `.common` 节（`sh_num = SHN_COMMON`），然后在链接时统一分配到 `.bss` 节来实现这一语义。

注意对于 `SHN_COMMON` 符号，`st_value` 字段存储的不是地址而是对齐要求。

### 6.8.3 动态符号绑定

对于可执行文件，`bind_exe_dynsyms()`（第 2030 行）将未定义符号与共享库的导出符号进行匹配：

- 如果匹配到 `STT_FUNC` 类型的符号：创建 PLT 条目
- 如果匹配到 `STT_OBJECT` 类型的符号：在 BSS 中分配空间，创建 `R_COPY` 重定位
- 如果未匹配且非 `STB_WEAK`：报错 "unresolved reference"

---

## 6.9 ELF 文件输出

`elf_output_file()`（`tccelf.c` 第 2978 行）是 TinyCC 生成 ELF 可执行文件或共享库的核心函数。它实现了一个精心设计的多阶段管道。

### 6.9.1 五阶段管道

```
阶段 1: 准备与解析
  ├─ tcc_add_runtime()      — 添加运行时库（libtcc1.a, crt*.o）
  ├─ resolve_common_syms()  — COMMON 符号分配到 BSS
  ├─ 创建 .interp 节        — 指定动态链接器路径
  ├─ 创建 .dynsym/.dynstr   — 动态符号表
  ├─ 创建 .dynamic 节       — 动态链接信息
  └─ build_got()            — 初始化 GOT

阶段 2: 符号绑定
  ├─ bind_exe_dynsyms()     — 匹配未定义符号与共享库导出
  ├─ build_got_entries()    — 为需要的符号创建 GOT/PLT 条目
  ├─ bind_libs_dynsyms()    — 导出可执行文件中库需要的符号
  └─ create_gnu_hash()      — 创建 GNU 哈希表

阶段 3: 布局
  ├─ alloc_sec_names()      — 分配节名字符串
  ├─ sort_sections()        — 确定节排列顺序
  ├─ layout_sections()      — 分配虚拟地址，生成程序头
  └─ set_sec_sizes()        — 设置各节的最终大小

阶段 4: 重定位
  ├─ relocate_plt()         — 修补 PLT 中的地址
  ├─ relocate_syms(dynsym)  — 解析动态符号的最终地址
  ├─ relocate_syms(symtab)  — 解析所有符号的最终地址
  ├─ relocate_sections()    — 对所有节执行重定位
  └─ fill_local_got_entries() — 填充本地 GOT 条目

阶段 5: 输出
  ├─ update_gnu_hash()      — 最终化 GNU 哈希表
  ├─ reorder_sections()     — 按排序结果重排节
  ├─ tcc_eh_frame_hdr()     — 生成异常处理头
  └─ tcc_write_elf_file()   — 写入 ELF 文件
```

### 6.9.2 关键代码片段

```c
static int elf_output_file(TCCState *s1, const char *filename)
{
    /* 阶段 1: 准备 */
    tcc_add_runtime(s1);
    resolve_common_syms(s1);
    /* 创建动态链接所需节 */
    interp = new_section(s1, ".interp", SHT_PROGBITS, SHF_ALLOC);
    s1->dynsym = new_symtab(s1, ".dynsym", SHT_DYNSYM, SHF_ALLOC, ...);
    dynamic = new_section(s1, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
    got_sym = build_got(s1);

    /* 阶段 2: 绑定 */
    bind_exe_dynsyms(s1, file_type & TCC_OUTPUT_DYN);
    build_got_entries(s1, got_sym);
    bind_libs_dynsyms(s1);
    dyninf.gnu_hash = create_gnu_hash(s1);
    version_add(s1);

    /* 阶段 3: 布局 */
    alloc_sec_names(s1, 0);
    sec_order = tcc_malloc(sizeof(int) * 2 * s1->nb_sections);
    layout_sections(s1, sec_order, &dyninf);

    /* 阶段 4: 重定位 */
    write32le(s1->got->data, dynamic->sh_addr);
    relocate_plt(s1);
    relocate_syms(s1, s1->dynsym, 2);
    relocate_syms(s1, s1->symtab, 0);
    relocate_sections(s1);
    fill_local_got_entries(s1);

    /* 阶段 5: 输出 */
    update_gnu_hash(s1, dyninf.gnu_hash);
    reorder_sections(s1, sec_order);
    ret = tcc_write_elf_file(s1, filename, dyninf.phnum, dyninf.phdr);
}
```

### 6.9.3 目标文件输出

对于 `-c` 选项（仅编译不链接），`elf_output_obj()`（第 3156 行）执行一个简化的流程：只分配节名、计算偏移、写入文件，不做符号解析和重定位。

---

## 6.10 Section 排序与布局

### 6.10.1 `sort_sections()` 算法

`sort_sections()`（第 2213 行）决定节在输出文件和内存映像中的排列顺序。排序的核心思想是：将具有相同权限的节放在一起，以最小化程序头数量。

排序使用一个复合键（主键 + 次键），编码为一个整数：

**主键（高字节）——基于权限：**

| 主键值 | 含义 |
|--------|------|
| 0x100 | 只读 + SHF_ALLOC |
| 0x200 | 可写 + SHF_ALLOC |
| 0x400 | TLS + SHF_ALLOC + SHF_WRITE |
| 0x700 | 非 SHF_ALLOC（调试信息等） |
| 0x900 | 无 sh_name，不输出 |

**次键（低字节）——基于节类型：**

| 次键值 | 节类型 |
|--------|--------|
| 0x00 | .interp |
| 0x10 | 符号表 |
| 0x11 | 字符串表 |
| 0x12 | 哈希表 |
| 0x13 | 版本信息 |
| 0x20 | 重定位表 |
| 0x21 | PLT 重定位 |
| 0x30 | 可执行代码 |
| 0x41-0x43 | init/fini 数组 |
| 0x46 | .dynamic |
| 0x47 | .got（RELRO） |
| 0x50 | 数据 |
| 0x60 | .note |
| 0x70 | BSS |

排序完成后，函数计算需要多少个 `PT_LOAD` 段：每当节的权限标志（读/写/执行/TLS）发生变化时，就需要一个新的 `PT_LOAD` 段。

### 6.10.2 `layout_sections()` 地址分配

`layout_sections()`（第 2328 行）根据排序结果为每个节分配虚拟地址：

```c
addr = ELF_START_ADDR;       /* 默认 0x400000 */
if (s1->output_type & TCC_OUTPUT_DYN)
    addr = 0;                /* 共享库从 0 开始 */

for (每个 PT_LOAD 段) {
    /* 对齐到页边界 */
    addr = (addr + s_align - 1) & -s_align;
    ph->p_vaddr = addr;
    ph->p_offset = file_offset;
    for (段中的每个节) {
        addr = (addr + align - 1) & -align;
        s->sh_addr = addr;
        addr += s->sh_size;
    }
}
```

关键点：
- `ELF_START_ADDR` 在 `x86_64-link.c` 中定义为 `0x400000`，这是 x86-64 Linux 的标准加载地址。
- 每个 `PT_LOAD` 段的虚拟地址对齐到 `ELF_PAGE_SIZE`（0x1000）。
- 文件偏移与虚拟地址在页内必须一致（`p_vaddr % page_size == p_offset % page_size`），以满足 `mmap` 的要求。

---

## 6.11 GOT 和 PLT

GOT（Global Offset Table）和 PLT（Procedure Linkage Table）是实现位置无关代码（PIC）和延迟绑定的核心机制。

### 6.11.1 GOT（全局偏移表）

GOT 是一个指针数组，每个条目存储一个全局符号的绝对地址。代码通过 PC 相对寻址访问 GOT，再通过 GOT 中的指针间接访问目标符号。

```
代码中的 GOTPCREL 引用：
    mov  rax, [rip + offset_to_got_entry]  ; 通过 GOT 加载地址
    call rax                                 ; 调用

GOT 内容：
    .got[0]: _DYNAMIC 地址
    .got[1]: 保留（linker）
    .got[2]: 保留（dynamic linker）
    .got[3]: printf 的地址
    .got[4]: global_var 的地址
    ...
```

### 6.11.2 `build_got()` 函数

```c
static int build_got(TCCState *s1)
{
    s1->got = new_section(s1, ".got", SHT_PROGBITS,
                          SHF_ALLOC | SHF_WRITE);
    s1->got->sh_entsize = 4;
    /* 保留 3 个 PTR_SIZE 空间 */
    section_ptr_add(s1->got, 3 * PTR_SIZE);
    return set_elf_sym(symtab_section, 0, 0,
        ELFW(ST_INFO)(STB_GLOBAL, STT_OBJECT),
        0, s1->got->sh_num, "_GLOBAL_OFFSET_TABLE_");
}
```

GOT 的前 3 个条目是保留的：
- `GOT[0]`：`_DYNAMIC` 段的地址
- `GOT[1]`：链接器保留（用于标识）
- `GOT[2]`：动态链接器的解析函数入口（用于延迟绑定）

### 6.11.3 PLT（过程链接表）

PLT 是实现函数调用延迟绑定的关键。每个通过 PLT 调用的函数在 PLT 中有一个 16 字节的条目。

**PLT[0]（公共入口，16 字节）：**

```asm
push  [got + 8]        ; 压入 link_map 标识
jmp   *[got + 16]      ; 跳转到 _dl_runtime_resolve
```

**PLT[n]（每个函数的入口，16 字节）：**

```asm
jmp   *[got + got_offset]  ; 间接跳转（首次调用时指向下面的 push）
push  reloc_index           ; 压入重定位条目索引
jmp   PLT[0]               ; 跳转到公共入口
```

### 6.11.4 `create_plt_entry()` 函数

在 `x86_64-link.c` 第 137 行：

```c
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                  struct sym_attr *attr)
{
    Section *plt = s1->plt;
    uint8_t *p;

    /* 首次调用时创建 PLT[0] */
    if (plt->data_offset == 0) {
        p = section_ptr_add(plt, 16);
        p[0] = 0xff; p[1] = 0x35;   /* push *(got+8) */
        write32le(p + 2, PTR_SIZE);
        p[6] = 0xff; p[7] = 0x25;   /* jmp *(got+16) */
        write32le(p + 8, PTR_SIZE * 2);
    }

    plt_offset = plt->data_offset;
    relofs = s1->plt->reloc ? s1->plt->reloc->data_offset : 0;

    p = section_ptr_add(plt, 16);
    p[0] = 0xff; p[1] = 0x25;       /* jmp *(got + got_offset) */
    write32le(p + 2, got_offset);
    p[6] = 0x68;                      /* push $reloc_index */
    write32le(p + 7, relofs / sizeof(ElfW_Rel) - 1);
    p[11] = 0xe9;                     /* jmp PLT[0] */
    write32le(p + 12, -(plt->data_offset));

    return plt_offset;
}
```

### 6.11.5 `build_got_entries()` 函数

`build_got_entries()`（第 1417 行）是 GOT/PLT 条目创建的主循环。它执行两遍扫描（pass 0 和 pass 1），因为某些 ARM 架构不允许混合 `R_JMP_SLOT` 和 `R_GLOB_DAT` 类型的重定位。

```c
ST_FUNC void build_got_entries(TCCState *s1, int got_sym)
{
    int pass = 0;
redo:
    for(i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (s->sh_type != SHT_RELX) continue;
        if (s->link != symtab_section) continue;
        for_each_elem(s, 0, rel, ElfW_Rel) {
            type = ELFW(R_TYPE)(rel->r_info);
            gotplt_entry = gotplt_entry_type(type);
            /* 根据 gotplt_entry 的值决定是否创建 GOT/PLT 条目 */
            switch (gotplt_entry) {
            case NO_GOTPLT_ENTRY:   break;    /* 不需要 */
            case AUTO_GOTPLT_ENTRY: /* 自动判断 */
                if (sym->st_shndx == SHN_UNDEF) goto jmp_slot;
                break;
            case BUILD_GOT_ONLY:    /* 仅 GOT */
                put_got_entry(s1, R_GLOB_DAT, sym_index);
                break;
            case ALWAYS_GOTPLT_ENTRY: /* 总是创建 */
                if (is_jmp_slot) goto jmp_slot;
                put_got_entry(s1, R_GLOB_DAT, sym_index);
                break;
            jmp_slot:
                put_got_entry(s1, R_JMP_SLOT, sym_index);
                break;
            }
        }
    }
    if (++pass < 2) goto redo;
}
```

### 6.11.6 `put_got_entry()` 函数

`put_got_entry()`（第 1329 行）为单个符号创建 GOT 条目（和可选的 PLT 条目）：

```c
static struct sym_attr *put_got_entry(TCCState *s1, int dyn_reloc_type,
                                      int sym_index)
{
    attr = get_sym_attr(s1, sym_index, 1);
    if (need_plt_entry ? attr->plt_offset : attr->got_offset)
        return attr;  /* 已创建 */

    /* 分配 GOT 条目 */
    got_offset = s1->got->data_offset;
    section_ptr_add(s1->got, PTR_SIZE);

    /* 创建动态重定位 */
    if (s1->dynsym) {
        if (ELFW(ST_BIND)(sym->st_info) == STB_LOCAL) {
            /* 本地符号：标记为 R_RELATIVE，稍后在
               fill_local_got_entries() 中修补 */
            put_elf_reloc(s1->dynsym, s1->got, got_offset,
                          R_RELATIVE, sym_index);
        } else {
            /* 全局符号：创建正式的动态重定位 */
            if (0 == attr->dyn_index)
                attr->dyn_index = set_elf_sym(s1->dynsym, ...);
            put_elf_reloc(s1->dynsym, s_rel, got_offset,
                          dyn_reloc_type, attr->dyn_index);
        }
    }

    if (need_plt_entry) {
        attr->plt_offset = create_plt_entry(s1, got_offset, attr);
        /* 创建 "sym@plt" 符号 */
        put_elf_sym(s1->symtab, attr->plt_offset, 0, ...,
                    s1->plt->sh_num, "sym@plt");
    } else {
        attr->got_offset = got_offset;
    }
}
```

### 6.11.7 `relocate_plt()` 函数

`relocate_plt()`（`x86_64-link.c` 第 178 行）在最终地址确定后修补 PLT 和 GOT 中的偏移：

```c
ST_FUNC void relocate_plt(TCCState *s1)
{
    p = s1->plt->data;
    if (p < p_end) {
        int x = s1->got->sh_addr - s1->plt->sh_addr - 6;
        add32le(p + 2, x);     /* PLT[0] 的 push */
        add32le(p + 8, x - 6); /* PLT[0] 的 jmp */
        p += 16;
        while (p < p_end) {
            add32le(p + 2, x + (s1->plt->data - p)); /* PLT[n] 的 jmp */
            p += 16;
        }
    }
    /* 初始化 GOT 条目：指向 PLT 条目 + 6（即 push 指令） */
    if (s1->plt->reloc) {
        int x = s1->plt->sh_addr + 16 + 6;
        for_each_elem(s1->plt->reloc, 0, rel, ElfW_Rel) {
            write64le(p + rel->r_offset, x);
            x += 16;
        }
    }
}
```

---

## 6.12 动态链接支持

TinyCC 支持生成动态链接的可执行文件和共享库。这涉及多个协作的节。

### 6.12.1 `.dynamic` 节

`.dynamic` 节包含一系列 `Elf64_Dyn` 条目，描述动态链接所需的所有信息。TinyCC 在 `fill_dynamic()` 中填充以下标签：

| 标签 | 说明 |
|------|------|
| `DT_NEEDED` | 需要的共享库（每个加载的 DLL 一个） |
| `DT_SONAME` | 共享库自身的名称 |
| `DT_SYMTAB` | `.dynsym` 地址 |
| `DT_STRTAB` | `.dynstr` 地址 |
| `DT_STRSZ` | 字符串表大小 |
| `DT_HASH` | 传统哈希表地址 |
| `DT_GNU_HASH` | GNU 哈希表地址 |
| `DT_JMPREL` | PLT 重定位表地址 |
| `DT_PLTRELSZ` | PLT 重定位表大小 |
| `DT_PLTGOT` | `.got.plt` 地址 |
| `DT_RELASZ` | 重定位表大小 |
| `DT_RELA` | 重定位表地址 |
| `DT_INIT_ARRAY` | 构造函数数组地址 |
| `DT_FINI_ARRAY` | 析构函数数组地址 |
| `DT_FLAGS` | `DF_BIND_NOW`（立即绑定） |
| `DT_FLAGS_1` | `DF_1_NOW`、`DF_1_PIE` 等 |

### 6.12.2 `.dynsym` 和 `.dynstr` 节

`.dynsym` 是动态符号表，只包含需要动态链接器处理的符号（全局和弱符号）。`.dynstr` 是对应的字符串表。

TinyCC 维护两个符号表：
- `symtab_section`（`.symtab`）：完整的静态符号表
- `s1->dynsym`（`.dynsym`）：仅用于动态链接的符号表

以及一个临时的 `dynsymtab_section`（`.dynsymtab`），用于在加载 `.so` 文件时收集符号。

### 6.12.3 GNU 哈希表

`create_gnu_hash()`（第 921 行）和 `update_gnu_hash()`（第 961 行）实现了 GNU 扩展哈希表格式，比传统 ELF 哈希表更高效。

GNU 哈希表的结构：

```
nbuckets    : 桶数量
symoffset   : 第一个已定义符号的索引
bloom_size  : Bloom 过滤器大小
bloom_shift : Bloom 过滤器移位量
bloom[]     : Bloom 过滤器（用于快速排除不存在的符号）
buckets[]   : 桶数组
chains[]    : 链数组
```

GNU 哈希使用 Bloom 过滤器实现 O(1) 的"符号不存在"检测，显著加速了动态链接过程。哈希函数使用经典的 DJB 哈希：

```c
static Elf32_Word elf_gnu_hash(const unsigned char *name)
{
    Elf32_Word h = 5381;
    unsigned char c;
    while ((c = *name++))
        h = h * 33 + c;
    return h;
}
```

### 6.12.4 符号排序

`update_gnu_hash()` 中包含一个重要的排序逻辑：它将未定义符号放在符号表的前面，已定义符号按哈希桶分组放在后面。这是因为 GNU 哈希表的 `chains` 数组只为已定义符号分配空间，`symoffset` 标记了已定义符号的起始位置。

### 6.12.5 版本信息

TinyCC 通过 `version_add()` 函数（第 605 行）生成 `.gnu.version` 和 `.gnu.version_r` 节。`.gnu.version` 节是一个 `Elf64_Half` 数组，每个动态符号对应一个版本索引。`.gnu.version_r` 节描述了每个版本属于哪个库及其版本字符串。

---

## 6.13 JIT 运行时 tccrun.c

TinyCC 最独特的特性之一是内置的 JIT（Just-In-Time）执行能力。通过 `tcc -run` 选项，可以将 C 程序直接编译并在内存中执行，无需生成磁盘文件。

### 6.13.1 `tcc_run()` 函数

`tcc_run()`（`tccrun.c` 第 218 行）是 JIT 执行的入口：

```c
LIBTCCAPI int tcc_run(TCCState *s1, int argc, char **argv)
{
    int (*prog_main)(int, char **, char **), ret;
    const char *top_sym;
    jmp_buf main_jb;

    /* 注册退出处理 */
    tcc_add_symbol(s1, "__rt_exit", rt_exit);
    s1->run_main = "_runmain", top_sym = "main";
    if (s1->elf_entryname)
        s1->run_main = top_sym = s1->elf_entryname;
    tcc_add_support(s1, "runmain.o");

    /* 核心：重定位代码到可执行内存 */
    if (tcc_relocate(s1) < 0)
        return -1;

    /* 获取入口函数地址 */
    prog_main = (void*)get_sym_addr(s1, s1->run_main, 1, 1);

    /* 执行 */
    fflush(stdout); fflush(stderr);
    ret = tcc_setjmp(s1, main_jb, tcc_get_symbol(s1, top_sym));
    if (0 == ret)
        ret = prog_main(argc, argv, envp);
    return ret;
}
```

### 6.13.2 `tcc_relocate()` 函数

`tcc_relocate()`（第 151 行）是 JIT 的核心——将编译后的节复制到可执行内存并完成重定位：

```c
LIBTCCAPI int tcc_relocate(TCCState *s1)
{
    int size, ret, ptr_diff;

    /* 第一步：计算所需内存大小 */
    size = tcc_relocate_ex(s1, NULL, 0);
    if (size < 0) return -1;

    /* 第二步：分配可执行内存 */
    ptr_diff = rt_mem(s1, size);
    if (ptr_diff < 0) return -1;

    /* 第三步：复制并重定位 */
    ret = tcc_relocate_ex(s1, s1->run_ptr, ptr_diff);
    if (ret == 0)
        st_link(s1);
    return ret;
}
```

### 6.13.3 内存分配策略

`rt_mem()`（第 105 行）根据平台采用不同的内存分配策略：

**Linux SELinux 模式：**

```c
#ifdef CONFIG_SELINUX
    int fd = mkstemp(tmpfname);
    unlink(tmpfname);
    ftruncate(fd, size);
    /* 代码段：RX */
    ptr = mmap(NULL, size * 2, PROT_READ|PROT_EXEC, MAP_SHARED, fd, 0);
    /* 数据段：RW（与代码段固定距离） */
    prw = mmap((char*)ptr + size, size, PROT_READ|PROT_WRITE,
               MAP_SHARED|MAP_FIXED, fd, 0);
    ptr_diff = (char*)prw - (char*)ptr; /* = size */
#endif
```

这种模式将同一文件映射两次，一次可执行（RX），一次可写（RW），通过 `ptr_diff` 记录两者之间的偏移。

**普通 Linux：**

```c
ptr = tcc_malloc(size += PAGESIZE);  /* 额外一页用于对齐 */
```

分配普通堆内存，之后通过 `mprotect()` 设置页面权限。

**Windows：**

```c
ptr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
```

### 6.13.4 `tcc_relocate_ex()` 函数

`tcc_relocate_ex()`（第 333 行）是重定位的核心实现，采用三遍策略：

**第一遍（ptr == NULL）：计算大小和地址**

```c
for (k = 0; k < 3; ++k) { /* 0:rx, 1:ro, 2:rw */
    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (权限匹配) {
            offset += 对齐;
            s->sh_addr = mem ? addr + offset : 0;
            offset += length;
        }
    }
}
return PAGEALIGN(offset);  /* 返回所需总大小 */
```

**第二遍（copy == 1）：复制数据**

```c
for (k = 0; k < 3; ++k) {
    for (i = 1; i < s1->nb_sections; i++) {
        if (s->data && s->sh_type != SHT_NOBITS)
            memcpy((void*)s->sh_addr, s->data, length);
        else
            memset((void*)s->sh_addr, 0, length);
    }
}
```

**第三遍（copy == 2）：设置权限**

```c
protect_pages(addr, n, mode);
/* mode: 0=rx, 1=ro, 2=rw, 3=rwx */
```

在两次复制之间，执行符号解析和重定位：

```c
relocate_syms(s1, s1->symtab, 1);  /* do_resolve=1: 使用 dlsym() */
relocate_plt(s1);
relocate_sections(s1);
```

### 6.13.5 `tcc_get_symbol()` 函数

JIT 执行完成后，可以通过 `tcc_get_symbol()` 获取编译后符号的地址：

```c
LIBTCCAPI void *tcc_get_symbol(TCCState *s, const char *name)
{
    addr_t addr = get_sym_addr(s, name, 0, 1);
    return addr == -1 ? NULL : (void*)(uintptr_t)addr;
}
```

这使得 JIT 模式可以用于实现脚本引擎、插件系统等场景——先将 C 代码编译到内存，然后通过函数指针调用。

### 6.13.6 与普通链接的对比

| 方面 | 普通链接 | JIT 模式 |
|------|---------|---------|
| 符号解析 | 链接器静态解析 | `dlsym()` 运行时解析 |
| 输出 | ELF 文件 | 内存中的可执行代码 |
| 地址分配 | `layout_sections()` | `tcc_relocate_ex()` |
| GOT/PLT | 生成并输出 | 生成但不输出到文件 |
| 权限管理 | 由 OS 加载器处理 | `mprotect()` 手动设置 |

---

## 6.14 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 链接器的完整实现。我们从 ELF 文件格式的基础知识出发，逐步覆盖了以下核心主题：

1. **ELF 格式**：ELF 头、节头、程序头的结构和字节布局，是理解链接器工作的基础。
2. **Section 管理**：`Section` 结构体是 TinyCC 链接器的核心数据结构，通过 `new_section()`、`section_add()`、`section_ptr_add()` 管理所有节数据。
3. **符号系统**：`Elf64_Sym` 结构体编码了符号的绑定（LOCAL/GLOBAL/WEAK）、类型（NOTYPE/OBJECT/FUNC）和可见性。`set_elf_sym()` 实现了符号冲突的解决规则。
4. **重定位**：`Elf64_Rela` 结构体描述了如何修补代码和数据中的地址引用。`relocate()` 函数实现了 x86-64 平台上所有重定位类型的处理。
5. **目标文件加载**：`tcc_load_object_file()` 通过五阶段流程将 `.o` 文件合并到编译状态中。
6. **库加载**：静态库（`.a`）通过 `tcc_load_archive()` 按成员遍历加载；共享库（`.so`）通过 `tcc_load_dll()` 提取动态符号。
7. **GOT/PLT**：实现了位置无关代码和延迟绑定的核心机制。
8. **文件输出**：`elf_output_file()` 的五阶段管道（准备、绑定、布局、重定位、输出）是链接器的主干。
9. **JIT 运行时**：`tcc_relocate_ex()` 通过分配 RWX 内存、复制节数据、应用重定位，实现了 C 代码的即时执行。

### 练习

#### 练习 1：ELF 文件分析

使用 `readelf` 和 `objdump` 工具分析 TinyCC 编译输出的 `.o` 文件，理解各个节的含义和重定位条目的工作方式。详见 `exercises/ex1_elf.md`。

#### 练习 2：重定位追踪

编写一个包含外部函数调用的 C 程序，使用 TinyCC 编译为目标文件，然后手动追踪重定位过程。详见 `exercises/ex2_reloc.md`。

#### 练习 3：JIT 行为观察

使用 `tcc -run` 执行 C 程序，通过 `/proc/[pid]/maps` 观察 JIT 内存布局，理解运行时重定位的行为。详见 `exercises/ex3_jit.md`。

---

## 参考文献

1. Tool Interface Standard (TIS) Executable and Linkable Format (ELF) Specification Version 1.2.
2. System V Application Binary Interface - AMD64 Architecture Processor Supplement.
3. Fabrice Bellard, "TCC: Tiny C Compiler", https://bellard.org/tcc/.
4. Ian Lance Taylor, "Linkers and Loaders", 2000. (系列文章)
5. Michael Matz et al., "System V Application Binary Interface", 2023.


===== FILE: docs/ch07/examples/libtcc_functions.c =====
/*
 * libtcc_functions.c - Call compiled functions from host
 *
 * Demonstrates how to:
 *   1. Compile a C string into memory
 *   2. Extract function pointers via tcc_get_symbol
 *   3. Call compiled functions from the host program
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libtcc.h"

static int num_errors = 0;

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    num_errors++;
}

/* Program with multiple functions to be called from host */
static const char *program =
    "#include <tcclib.h>\n"
    "\n"
    "/* Recursive fibonacci */\n"
    "int fib(int n) {\n"
    "    if (n <= 2) return 1;\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "\n"
    "/* String formatting function */\n"
    "int format_result(char *buf, int bufsz, int n, int result) {\n"
    "    return snprintf(buf, bufsz, \"fib(%d) = %d\", n, result);\n"
    "}\n"
    "\n"
    "/* Array processing function */\n"
    "void bubble_sort(int *arr, int len) {\n"
    "    int i, j, tmp;\n"
    "    for (i = 0; i < len - 1; i++) {\n"
    "        for (j = 0; j < len - 1 - i; j++) {\n"
    "            if (arr[j] > arr[j+1]) {\n"
    "                tmp = arr[j];\n"
    "                arr[j] = arr[j+1];\n"
    "                arr[j+1] = tmp;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n";

int main(void)
{
    TCCState *s;
    int (*fib)(int);
    int (*format_result)(char *, int, int, int);
    void (*bubble_sort)(int *, int);

    /* Create and configure TCC state */
    s = tcc_new();
    tcc_set_error_func(s, stderr, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Compile */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed with %d errors\n", num_errors);
        tcc_delete(s);
        return 1;
    }

    /* Relocate: resolve all symbols and make code executable */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Relocation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Get function pointers */
    fib = tcc_get_symbol(s, "fib");
    format_result = tcc_get_symbol(s, "format_result");
    bubble_sort = tcc_get_symbol(s, "bubble_sort");

    if (!fib || !format_result || !bubble_sort) {
        fprintf(stderr, "Failed to get symbols\n");
        tcc_delete(s);
        return 1;
    }

    /* Use the compiled functions */
    printf("=== Fibonacci ===\n");
    int i;
    for (i = 1; i <= 20; i++) {
        char buf[256];
        int result = fib(i);
        format_result(buf, sizeof(buf), i, result);
        printf("  %s\n", buf);
    }

    printf("\n=== Bubble Sort ===\n");
    int arr[] = {64, 34, 25, 12, 22, 11, 90, 1, 55, 42};
    int len = sizeof(arr) / sizeof(arr[0]);

    printf("Before: ");
    for (i = 0; i < len; i++) printf("%d ", arr[i]);
    printf("\n");

    bubble_sort(arr, len);

    printf("After:  ");
    for (i = 0; i < len; i++) printf("%d ", arr[i]);
    printf("\n");

    tcc_delete(s);
    return 0;
}


===== FILE: docs/ch07/examples/libtcc_hello.c =====
/*
 * libtcc_hello.c - Basic libtcc usage example
 *
 * Demonstrates the minimal workflow for embedding TCC:
 *   tcc_new → configure → compile → run → tcc_delete
 */
#include <stdio.h>
#include <stdlib.h>
#include "libtcc.h"

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "TCC Error: %s\n", msg);
}

int main(int argc, char **argv)
{
    TCCState *s;
    const char *program =
        "#include <tcclib.h>\n"
        "int main(int argc, char **argv) {\n"
        "    int i;\n"
        "    printf(\"Hello from embedded TCC!\\n\");\n"
        "    printf(\"Arguments:\\n\");\n"
        "    for (i = 0; i < argc; i++)\n"
        "        printf(\"  argv[%d] = %s\\n\", i, argv[i]);\n"
        "    return 0;\n"
        "}\n";

    /* Step 1: Create a new compilation context */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Failed to create TCC state\n");
        return 1;
    }

    /* Step 2: Configure error handling */
    tcc_set_error_func(s, stderr, error_handler);

    /* Step 3: Optionally set library/include paths */
    /* tcc_set_lib_path(s, "/usr/local/lib/tcc"); */
    /* tcc_add_include_path(s, "/usr/include"); */

    /* Step 4: Set output type to in-memory execution */
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Step 5: Compile the source code */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Step 6: Run the compiled program */
    printf("--- Running compiled program ---\n");
    int ret = tcc_run(s, argc, argv);
    printf("--- Program returned: %d ---\n", ret);

    /* Step 7: Cleanup */
    tcc_delete(s);
    return ret;
}


===== FILE: docs/ch07/examples/libtcc_host.c =====
/*
 * libtcc_host.c - Register host functions for use by compiled code
 *
 * Demonstrates how to:
 *   1. Register C functions from the host program with tcc_add_symbol
 *   2. Register global data from the host program
 *   3. Call host functions from dynamically compiled code
 *   4. Build a bidirectional bridge between host and compiled code
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libtcc.h"

/* ========== Host functions callable by compiled code ========== */

/* Basic math operations */
static int host_add(int a, int b) { return a + b; }
static int host_mul(int a, int b) { return a * b; }

/* Math library wrappers (simple implementations to avoid libm dependency) */
static double host_sin(double x)
{
    /* Taylor series approximation */
    double sum = x, term = x;
    int i;
    for (i = 1; i < 10; i++) {
        term *= -x * x / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static double host_cos(double x)
{
    double sum = 1, term = 1;
    int i;
    for (i = 1; i < 10; i++) {
        term *= -x * x / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

static double host_sqrt(double x)
{
    /* Newton's method */
    double guess = x / 2.0;
    int i;
    for (i = 0; i < 20; i++)
        guess = (guess + x / guess) / 2.0;
    return guess;
}

/* I/O functions */
static void host_print_int(const char *label, int value)
{
    printf("[HOST] %s = %d\n", label, value);
}

static void host_print_double(const char *label, double value)
{
    printf("[HOST] %s = %f\n", label, value);
}

static void host_print_string(const char *msg)
{
    printf("[HOST] %s\n", msg);
}

/* Memory allocation bridge */
static void *host_malloc(size_t size) { return malloc(size); }
static void host_free(void *ptr) { free(ptr); }

/* Timestamp function */
static double host_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Host data accessible by compiled code */
static const char *host_version = "1.0.0";
static int host_verbose = 1;

/* ========== Test program ========== */

static const char *program =
    "extern int host_add(int, int);\n"
    "extern int host_mul(int, int);\n"
    "extern double host_sin(double);\n"
    "extern double host_cos(double);\n"
    "extern double host_sqrt(double);\n"
    "extern void host_print_int(const char *, int);\n"
    "extern void host_print_double(const char *, double);\n"
    "extern void host_print_string(const char *);\n"
    "extern void *host_malloc(unsigned long);\n"
    "extern void host_free(void *);\n"
    "extern double host_time(void);\n"
    "extern const char *host_version;\n"
    "extern int host_verbose;\n"
    "\n"
    "int demo_math(void) {\n"
    "    int a = 10, b = 20;\n"
    "    host_print_string(\"--- Math Demo ---\");\n"
    "    host_print_int(\"add(10, 20)\", host_add(a, b));\n"
    "    host_print_int(\"mul(10, 20)\", host_mul(a, b));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_trig(void) {\n"
    "    double pi = 3.14159265358979;\n"
    "    host_print_string(\"--- Trigonometry Demo ---\");\n"
    "    host_print_double(\"sin(pi/6)\", host_sin(pi / 6));\n"
    "    host_print_double(\"cos(pi/6)\", host_cos(pi / 6));\n"
    "    host_print_double(\"sqrt(2)\", host_sqrt(2.0));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_timing(void) {\n"
    "    double t0, t1;\n"
    "    volatile int i;\n"
    "    volatile long long sum = 0;\n"
    "    host_print_string(\"--- Timing Demo ---\");\n"
    "    t0 = host_time();\n"
    "    for (i = 0; i < 1000000; i++)\n"
    "        sum += i;\n"
    "    t1 = host_time();\n"
    "    host_print_double(\"Loop time (sec)\", t1 - t0);\n"
    "    host_print_int(\"Sum (low bits)\", (int)(sum & 0xFFFF));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_memory(void) {\n"
    "    int *arr;\n"
    "    int i;\n"
    "    host_print_string(\"--- Memory Demo ---\");\n"
    "    arr = (int *)host_malloc(10 * sizeof(int));\n"
    "    for (i = 0; i < 10; i++)\n"
    "        arr[i] = i * i;\n"
    "    for (i = 0; i < 10; i++)\n"
    "        host_print_int(\"arr[i]\", arr[i]);\n"
    "    host_free(arr);\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_host_data(void) {\n"
    "    host_print_string(\"--- Host Data Demo ---\");\n"
    "    host_print_string(host_version);\n"
    "    host_print_int(\"verbose\", host_verbose);\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int run_all(void) {\n"
    "    demo_math();\n"
    "    demo_trig();\n"
    "    demo_timing();\n"
    "    demo_memory();\n"
    "    demo_host_data();\n"
    "    host_print_string(\"All demos complete.\");\n"
    "    return 0;\n"
    "}\n";

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "TCC: %s\n", msg);
}

int main(void)
{
    TCCState *s;
    int (*run_all)(void);

    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Failed to create TCC state\n");
        return 1;
    }

    tcc_set_error_func(s, stderr, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Register all host functions and data */
    tcc_add_symbol(s, "host_add", host_add);
    tcc_add_symbol(s, "host_mul", host_mul);
    tcc_add_symbol(s, "host_sin", host_sin);
    tcc_add_symbol(s, "host_cos", host_cos);
    tcc_add_symbol(s, "host_sqrt", host_sqrt);
    tcc_add_symbol(s, "host_print_int", host_print_int);
    tcc_add_symbol(s, "host_print_double", host_print_double);
    tcc_add_symbol(s, "host_print_string", host_print_string);
    tcc_add_symbol(s, "host_malloc", host_malloc);
    tcc_add_symbol(s, "host_free", host_free);
    tcc_add_symbol(s, "host_time", host_time);
    tcc_add_symbol(s, "host_version", &host_version);
    tcc_add_symbol(s, "host_verbose", &host_verbose);

    /* Compile the program */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Relocate */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Relocation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Get and call the entry function */
    run_all = tcc_get_symbol(s, "run_all");
    if (!run_all) {
        fprintf(stderr, "Symbol 'run_all' not found\n");
        tcc_delete(s);
        return 1;
    }

    printf("=== Running compiled code with host function bridge ===\n\n");
    int ret = run_all();
    printf("\n=== Returned: %d ===\n", ret);

    tcc_delete(s);
    return 0;
}


===== FILE: docs/ch07/exercises/ex1_libtcc.md =====
# 练习 1: 使用 libtcc 构建交互式计算器

## 背景

libtcc 允许在运行时编译和执行 C 代码。本练习要求你利用这一能力构建一个交互式计算器，用户输入 C 表达式，程序编译、执行并返回结果。

## 要求

1. 从标准输入逐行读取用户输入（每行一个表达式）
2. 将表达式包装为函数：
   ```c
   double calc(void) { return <用户表达式>; }
   ```
3. 使用 libtcc 编译并执行该函数
4. 输出计算结果
5. 循环执行直到用户输入 `quit`

## 提示

- 使用 `TCC_OUTPUT_MEMORY` 模式
- 注册 `sin`、`cos`、`tan`、`sqrt`、`pow`、`log` 等数学函数为宿主符号
- 为错误处理设置回调函数，将编译错误显示给用户而非终止程序
- 使用 `snprintf` 动态构建要编译的代码字符串
- 考虑添加 `#line 1 "calc"` 指令来改善错误消息

## 扩展挑战

1. 支持变量定义和引用（维护一个全局状态字符串，每次编译时在前面加上之前的变量定义）
2. 支持用户自定义函数（如 `def double square(double x) { return x*x; }`）
3. 实现结果缓存：如果表达式之前计算过，直接返回缓存结果

## 验证标准

```bash
$ ./calculator
> 2 + 3 * 4
14.000000
> sqrt(2)
1.414214
> sin(3.14159/4)
0.707107
> invalid syntax here
Error: expected expression before 'invalid'
> quit
$
```


===== FILE: docs/ch07/exercises/ex2_embed.md =====
# 练习 2: 嵌入式脚本引擎

## 背景

许多应用程序（如游戏引擎、文本编辑器、网络服务器）使用嵌入式脚本语言来提供可扩展性。本练习要求你使用 libtcc 构建一个支持 C 语言的嵌入式脚本系统。

## 要求

### 基础功能

1. 定义一个事件系统，支持以下事件：
   - `on_init`：初始化时调用
   - `on_tick(int tick_number)`：每帧调用
   - `on_message(const char *msg)`：消息到达时调用
   - `on_shutdown`：关闭时调用

2. 从脚本文件（`script.c`）加载 C 代码，编译后注册为事件处理器

3. 模拟一个简单的事件循环：
   ```c
   for (int i = 0; i < 100; i++) {
       on_tick(i);
       if (i == 50) on_message("midpoint reached");
   }
   on_shutdown();
   ```

### 高级功能

4. **热重载**：监听脚本文件的变化（使用 `inotify` 或定时检查 `stat`），文件修改后重新编译并替换事件处理器

5. **安全沙箱**：通过 `tcc_add_symbol` 只暴露允许的 API 给脚本，不暴露 `system`、`exec` 等危险函数

6. **错误恢复**：脚本编译错误不应导致主程序崩溃，而是保持旧版本的事件处理器继续运行

## 提示

- 使用 `typedef` 定义事件处理器的函数指针类型
- 在主线程和编译线程之间使用互斥锁保护函数指针
- 使用 `volatile` 关键字确保函数指针的可见性
- 考虑使用 `dlopen`/`dlsym`（如果输出到 DLL）或直接内存输出模式

## 验证标准

1. 基础脚本能正确编译和执行
2. 修改脚本文件后自动重新加载
3. 脚本中的语法错误被优雅处理，不影响主程序
4. 脚本无法调用未注册的宿主函数


===== FILE: docs/ch07/index.md =====
# 第七章 运行时与嵌入式API

TinyCC 不仅仅是一个编译器——它同时提供了一个完整的运行时支持库和一套嵌入式编程 API（libtcc），使得用户可以在自己的 C 程序中嵌入一个编译器实例。本章将深入探讨 TinyCC 的运行时基础设施和 libtcc 嵌入式 API 的方方面面。

---

## 7.1 运行时支持库 libtcc1.a

每一个 C 编译器都需要一个运行时支持库来提供目标硬件无法直接支持的操作。对于 GCC，这个库是 `libgcc`；对于 TinyCC，对应的库是 `libtcc1.a`。该库的源码位于 `lib/libtcc1.c`，在构建过程中被编译为静态库并安装到 tcc 的库搜索路径中。

### 7.1.1 为什么需要运行时库

现代处理器的指令集并不能覆盖 C 语言标准库或 ABI 所要求的全部操作。典型的"缺失指令"包括：

- **64 位整数除法**：在 32 位平台（如 i386）上，`long long` 类型的除法和取模无法用单条指令完成。
- **无符号到浮点的转换**：将一个 `unsigned long long` 转换为 `float` 或 `double` 时，不能简单地使用硬件浮点指令，因为中间结果可能溢出。
- **算术移位**：某些平台上对 64 位值的算术右移需要特殊处理。
- **128 位整数支持**：x86-64 ABI 中 `__int128` 类型的辅助函数。

编译器在代码生成阶段检测到这些操作时，会生成对运行时库函数的调用而非直接发射指令。

### 7.1.2 内存操作函数

`libtcc1.c` 中最基础的部分是内存操作函数的实现。当编译器需要内联展开 `memcpy` 或 `memmove` 但判断不值得内联时（例如长度不确定），就会调用这些运行时版本：

```c
/* lib/libtcc1.c 中不直接提供 memcpy/memmove，
   但 libtcc1.a 包含由汇编或编译器内建提供的版本。
   在链接时，这些符号会被解析到 libc 或 libtcc1 的实现。 */
```

在实际的 libtcc1.a 中，memcpy 等函数通常由编译器内建（`__builtin_memcpy`）展开而来，或者在没有 libc 可用的裸机环境中提供独立实现。

### 7.1.3 64 位整数除法

这是 libtcc1.c 中最核心也最复杂的部分。在 32 位 x86 平台上，TCC 无法用单条指令完成 64 位除法，因此需要软件模拟。相关函数包括：

| 函数 | 签名 | 功能 |
|------|------|------|
| `__divdi3` | `long long __divdi3(long long u, long long v)` | 有符号 64 位除法 |
| `__moddi3` | `long long __moddi3(long long u, long long v)` | 有符号 64 位取模 |
| `__udivdi3` | `unsigned long long __udivdi3(unsigned long long u, unsigned long long v)` | 无符号 64 位除法 |
| `__umoddi3` | `unsigned long long __umoddi3(unsigned long long u, unsigned long long v)` | 无符号 64 位取模 |

这些函数的实现都依赖一个核心函数 `__udivmoddi4`，它同时计算商和余数：

```c
/* lib/libtcc1.c - 核心除法算法（简化展示） */
static UDWtype __udivmoddi4(UDWtype n, UDWtype d, UDWtype *rp)
{
    DWunion ww, nn, dd, rr;
    UWtype d0, d1, n0, n1, n2;
    UWtype q0, q1;

    nn.ll = n;
    dd.ll = d;
    d0 = dd.s.low;
    d1 = dd.s.high;
    n0 = nn.s.low;
    n1 = nn.s.high;

    if (d1 == 0) {
        /* 除数为 32 位——使用一到两次硬件除法 */
        if (d0 > n1) {
            udiv_qrnnd(q0, n0, n1, n0, d0);
            q1 = 0;
        } else {
            udiv_qrnnd(q1, n1, 0, n1, d0);
            udiv_qrnnd(q0, n0, n1, n0, d0);
        }
    } else {
        /* 除数为 64 位——需要归一化和多位试商 */
        count_leading_zeros(bm, d1);
        /* ...归一化并执行试商法... */
    }

    ww.s.low = q0;
    ww.s.high = q1;
    return ww.ll;
}
```

关键的辅助宏 `udiv_qrnnd` 和 `umul_ppmm` 利用 i386 的 `divl` 和 `mull` 指令来完成单精度的除法和乘法操作：

```c
/* i386 平台上的汇编辅助宏 */
#define udiv_qrnnd(q, r, n1, n0, dv) \
    __asm__("divl %4" \
        : "=a"((USItype)(q)), "=d"((USItype)(r)) \
        : "0"((USItype)(n0)), "1"((USItype)(n1)), "rm"((USItype)(dv)))

#define umul_ppmm(w1, w0, u, v) \
    __asm__("mull %3" \
        : "=a"((USItype)(w0)), "=d"((USItype)(w1)) \
        : "%0"((USItype)(u)), "rm"((USItype)(v)))
```

有符号版本 `__divdi3` 在调用 `__udivmoddi4` 之前先处理符号：

```c
long long __divdi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    uu.ll = u;
    vv.ll = v;

    if (uu.s.high < 0) { c = ~c; uu.ll = -uu.ll; }
    if (vv.s.high < 0) { c = ~c; vv.ll = -vv.ll; }

    DWtype w = __udivmoddi4(uu.ll, vv.ll, NULL);
    return c ? -w : w;
}
```

注意：这些 64 位除法函数**仅在 32 位平台（i386）上需要**。在 x86-64 和 ARM64 等 64 位平台上，硬件原生支持 64 位除法（`idiv` 指令或 ARM 的 `sdiv`/`udiv`），因此 libtcc1.c 通过 `#if defined __i386__` 条件编译保护这些函数。

### 7.1.4 64 位移位操作

在 32 位平台上，对 `long long` 类型的移位操作也需要软件实现：

```c
/* lib/libtcc1.c */
long long __ashrdi3(long long a, int b)   /* 算术右移 */
unsigned long long __lshrdi3(unsigned long long a, int b)  /* 逻辑右移 */
long long __ashldi3(long long a, int b)   /* 左移 */
```

这些函数的实现利用 `DWunion` 联合体来分别访问 64 位值的高 32 位和低 32 位，然后手动执行跨字的移位操作。

### 7.1.5 浮点转换函数

当需要将 64 位无符号整数与浮点数之间相互转换时，存在一个特殊问题：标准的浮点转换指令假定输入是有符号的，对于超过 `LLONG_MAX` 的无符号值会产生错误结果。libtcc1.c 提供了专门的转换函数：

```c
/* 无符号 64 位 → 浮点 */
float __floatundisf(unsigned long long a);
double __floatundidf(unsigned long long a);
long double __floatundixf(unsigned long long a);

/* 浮点 → 无符号 64 位 */
unsigned long long __fixunssfdi(float a);
unsigned long long __fixunsdfdi(double a);
unsigned long long __fixunsxfdi(long double a);

/* 浮点 → 有符号 64 位 */
long long __fixsfdi(float a);
long long __fixdfdi(double a);
long long __fixxfdi(long double a);
```

`__floatundidf` 的实现策略是：如果值的最高位为 0（表示为正数），直接用标准转换；否则先转为 `long double`（其精度足够），再加上 `2^64` 的偏移来补偿符号位的影响：

```c
double __floatundidf(unsigned long long a)
{
    DWunion uu;
    XFtype r;
    uu.ll = a;
    if (uu.s.high >= 0) {
        return (double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0; /* 2^64 */
        return (double)r;
    }
}
```

### 7.1.6 libtcc1 的构建与安装

在 TCC 的构建系统中，libtcc1.a 的构建规则定义在 `Makefile` 中。编译时使用 `-nostdlib` 和 `-shared` 等选项，确保生成的库不依赖外部 libc 符号。该库被安装到 `CONFIG_TCCDIR`（默认 `/usr/local/lib/tcc`）下。

---

## 7.2 边界检查 bcheck.c

TCC 提供了一个独特的功能：**内置的内存和边界检查器**（bounds checker）。当使用 `-b` 选项编译时，TCC 会在所有内存访问前插入检查代码，运行时由 `lib/bcheck.c` 提供的检查逻辑来验证每次指针操作是否合法。

### 7.2.1 架构概述

边界检查系统由两部分协作完成：

1. **编译时**（`tccgen.c` 中相关代码）：编译器在每次指针算术和内存引用前插入对运行时检查函数的调用。
2. **运行时**（`lib/bcheck.c`）：维护一个用 Splay 树实现的内存区域注册表，每次检查时在树中查找指针是否落在合法区域内。

### 7.2.2 运行时数据结构

bcheck.c 使用 **Splay 树**来管理所有已分配的内存区域：

```c
/* lib/bcheck.c */
typedef struct tree_node Tree;
struct tree_node {
    Tree *left, *right;
    size_t start;    /* 区域起始地址 */
    size_t size;     /* 区域大小 */
    unsigned char type;        /* 分配类型: MALLOC, CALLOC 等 */
    unsigned char is_invalid;  /* 区域外的指针是否无效 */
};
```

Splay 树的选择并非偶然：它具有**自调整**特性——最近访问的节点会被移动到根部。在典型的程序执行模式中，同一个内存区域会被反复访问（例如遍历数组），Splay 树使得这种模式下的摊还时间复杂度为 O(log n)。

### 7.2.3 指针算术检查

编译器插入的核心检查函数是 `__bound_ptr_add`，用于指针算术：

```c
/* lib/bcheck.c */
void *__bound_ptr_add(void *p, size_t offset)
{
    size_t addr = (size_t)p;

    if (NO_CHECKING_GET())
        return p + offset;

    WAIT_SEM();
    if (tree) {
        /* 在 Splay 树中查找 p 所属的区域 */
        addr -= tree->start;
        if (addr >= tree->size) {
            tree = splay((size_t)p, tree);
            addr = (size_t)p - tree->start;
        }
        if (addr <= tree->size) {
            if (tree->is_invalid || addr + offset > tree->size) {
                POST_SEM();
                bound_warning("outside region");
                if (never_fatal <= 0)
                    return INVALID_POINTER;
                return p + offset;
            }
        }
    }
    POST_SEM();
    return p + offset;
}
```

注意 `__bound_ptr_add` 的语义与 `__bound_ptr_indir*` 不同：前者允许指针到达区域的**末尾之后一个字节**（因为 C 语言标准允许指向数组末尾的指针），而后者要求目标地址**严格在区域内**。

### 7.2.4 内存访问检查

对于实际的内存读写，编译器根据访问宽度插入不同的检查函数：

```c
/* lib/bcheck.c */
void *__bound_ptr_indir1(void *p, size_t offset);  /* 1 字节访问 */
void *__bound_ptr_indir2(void *p, size_t offset);  /* 2 字节访问 */
void *__bound_ptr_indir4(void *p, size_t offset);  /* 4 字节访问 */
void *__bound_ptr_indir8(void *p, size_t offset);  /* 8 字节访问 */
void *__bound_ptr_indir12(void *p, size_t offset); /* 12 字节访问 */
void *__bound_ptr_indir16(void *p, size_t offset); /* 16 字节访问 */
```

这些函数通过宏 `BOUND_PTR_INDIR(dsize)` 生成，核心检查逻辑为 `addr + offset + dsize > tree->size`——即验证从目标地址开始的 `dsize` 字节是否全部落在合法区域内。

### 7.2.5 局部变量追踪

边界检查器还需要追踪栈上分配的局部变量。每当进入一个函数时，`__bound_local_new` 被调用来注册局部变量区域；函数返回时，`__bound_local_delete` 被调用来注销：

```c
/* lib/bcheck.c */
void __bound_local_new(void *p1)
{
    size_t addr, fp, *p = p1;
    if (NO_CHECKING_GET()) return;

    GET_CALLER_FP(fp);
    /* p 指向一个描述局部变量区域的数组:
       [fp, addr1, size1, addr2, size2, ..., 0] */
    while ((addr = p[0])) {
        splay_insert(addr, p[1], tree);
        p += 2;
    }
}
```

### 7.2.6 标准库函数的包装

边界检查器还拦截了常见的内存和字符串操作函数，以确保它们不会越界访问：

```c
/* lib/bcheck.c - 拦截的函数 */
void *__bound_memcpy(void *dst, const void *src, size_t size);
void *__bound_memmove(void *dst, const void *src, size_t size);
void *__bound_memset(void *dst, int c, size_t size);
int   __bound_strlen(const char *s);
char *__bound_strcpy(char *dst, const char *src);
int   __bound_strcmp(const char *s1, const char *s2);
char *__bound_strdup(const char *s);
/* ... 以及更多 ... */
```

这些包装函数在执行实际操作前先验证所有参数的合法性。

### 7.2.7 多线程支持

bcheck.c 通过平台相关的互斥机制保护 Splay 树的并发访问：

```c
/* lib/bcheck.c - 各平台的锁实现 */
#if defined(__APPLE__)
    /* 使用 GCD dispatch_semaphore */
#elif defined(_WIN32)
    /* 使用 CRITICAL_SECTION */
#else
    /* 使用 pthread_spinlock（最快） */
    static pthread_spinlock_t bounds_spin;
    #define WAIT_SEM()  if (use_sem) pthread_spin_lock(&bounds_spin)
    #define POST_SEM()  if (use_sem) pthread_spin_unlock(&bounds_spin)
#endif
```

使用 spinlock 而非 mutex 是经过性能测试的决定——边界检查的临界区非常短（仅在 Splay 树中查找），spinlock 的开销更低。

### 7.2.8 使用方式

在命令行上使用 `-b` 选项启用边界检查：

```bash
tcc -b -o program program.c
```

在 libtcc API 中，通过设置编译状态的标志来启用：

```c
tcc_set_options(s, "-b");
```

运行时还可以通过 `__bounds_checking(int)` 函数动态启用/禁用检查（在信号处理器中很有用），以及通过 `__bound_never_fatal(int)` 控制越界访问是否导致程序终止。

---

## 7.3 原子操作 stdatomic.c/atomic.S

C11 标准引入了 `<stdatomic.h>` 头文件和原子操作支持。TinyCC 通过两个文件实现了完整的原子操作支持：

- `lib/stdatomic.c`：使用编译器内建函数实现的通用原子操作
- `lib/atomic.S`：各平台的底层汇编实现

### 7.3.1 stdatomic.c 的实现策略

由于 TCC 自身不提供 `__atomic_*` 编译器内建函数（不像 GCC/Clang 那样将原子操作内建到代码生成器中），`lib/stdatomic.c` 采用了一种巧妙的方式：使用**比较并交换（CAS）循环**来模拟所有原子操作。

核心宏 `ATOMIC_GEN_OP` 定义了通用的原子操作模板：

```c
/* lib/stdatomic.c */
#define ATOMIC_GEN_OP(TYPE, MODE, NAME, OP, RET) \
    TYPE __atomic_##NAME##_##MODE(volatile void *atom, TYPE value, \
                                  int memorder) \
    { \
        TYPE xchg, cmp; \
        __atomic_load((TYPE *)atom, (TYPE *)&cmp, __ATOMIC_RELAXED); \
        do { \
            xchg = (OP); \
        } while (!__atomic_compare_exchange( \
            (TYPE *)atom, &cmp, &xchg, true, \
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)); \
        return RET; \
    }
```

这个宏接受四个参数：
- `TYPE`：操作的数据类型（如 `uint32_t`）
- `MODE`：大小标识（如 `4` 表示 4 字节）
- `NAME`：操作名称（如 `add_fetch`）
- `OP`：要执行的运算（如 `cmp + value`）
- `RET`：返回值（`xchg` 表示返回新值，`cmp` 表示返回旧值）

通过这个模板，所有 12 种原子操作都得以定义：

```c
/* lib/stdatomic.c */
ATOMIC_GEN(uint8_t, 1)   /* 1 字节原子操作 */
ATOMIC_GEN(uint16_t, 2)  /* 2 字节原子操作 */
ATOMIC_GEN(uint32_t, 4)  /* 4 字节原子操作 */
ATOMIC_GEN(uint64_t, 8)  /* 8 字节原子操作 */
```

其中 `ATOMIC_GEN` 宏进一步展开为 `ATOMIC_EXCHANGE`、`ATOMIC_ADD_FETCH`、`ATOMIC_FETCH_ADD` 等全部变体。

### 7.3.2 atomic.S 的平台实现

`lib/atomic.S` 提供了底层的原子原语实现，覆盖 i386、x86-64、ARM、ARM64 和 RISC-V 五个平台。这些原语是 `stdatomic.c` 中 CAS 循环的基石。

**x86-64 实现**：利用 `lock` 前缀和 `cmpxchg` 指令：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_compare_exchange_8
__atomic_compare_exchange_8:
    mov    (%rsi),%rax          /* 加载当前期望值 */
    lock cmpxchg %rdx,(%rdi)   /* 原子比较并交换 */
    sete   %dl                  /* 设置成功标志 */
    je     .Lsuccess
    mov    %rax,(%rsi)          /* 失败：更新期望值为实际值 */
.Lsuccess:
    mov    %edx,%eax
    ret
```

`lock cmpxchg` 是 x86 架构上最核心的原子指令之一：它原子地比较内存中的值与 `rax` 寄存器的值，如果相等则将新值写入内存，否则将内存中的实际值加载到 `rax`。

**原子存储** 在 x86-64 上使用 `xchg` 指令（隐含 `lock` 前缀）：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_store_8
__atomic_store_8:
    xchg   %rsi,(%rdi)   /* xchg 天然是原子的 */
    ret
```

**原子加载** 在 x86-64 上利用对齐的自然宽度读取天然原子性：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_load_8
__atomic_load_8:
    mov    (%rdi),%rax   /* 对齐的 8 字节读取在 x86-64 上是原子的 */
    ret
```

**线程屏障** 利用 `lock orq` 实现完整的内存屏障：

```asm
/* lib/atomic.S - x86-64 */
.globl atomic_thread_fence
atomic_thread_fence:
    lock orq $0x0,(%rsp)  /* 全屏障 */
    ret

.globl atomic_signal_fence
atomic_signal_fence:
    ret                    /* 信号屏障仅需编译器屏障 */
```

**ARM 平台** 使用 `ldrex`/`strex`（ARMv6+）或 `ldrexd`/`strexd`（64 位原子操作）指令配对来实现 CAS 操作：

```asm
/* lib/atomic.S - ARM */
__atomic_compare_exchange_4:
    ldr    r12, [r0]         /* 加载当前值 */
.Lretry:
    ldrex  r3, [r0]          /* 独占加载 */
    cmp    r3, r12           /* 比较 */
    bne    .Lfail
    strex  r12, r2, [r0]     /* 尝试独占存储 */
    cmp    r12, #0
    bne    .Lretry           /* strex 失败则重试 */
    mov    r0, #1
    bx     lr
.Lfail:
    clrex
    str    r3, [r1]          /* 更新期望值 */
    mov    r0, #0
    bx     lr
```

`ldrex`/`strex` 是 ARM 的 Load-Linked/Store-Conditional 模式：`ldrex` 标记一个独占监视区域，`strex` 仅在该区域未被其他核心修改时才成功写入。如果 `strex` 失败（返回非零），必须从 `ldrex` 重新开始。

### 7.3.3 is_lock_free 查询

`stdatomic.c` 还实现了 `__atomic_is_lock_free` 函数：

```c
/* lib/stdatomic.c */
bool __atomic_is_lock_free(unsigned long size, const volatile void *ptr)
{
    switch (size) {
    case 1: case 2: case 4: return true;
#if defined __x86_64__ || defined __aarch64__ || defined __riscv
    case 8: return true;   /* 64 位平台原生支持 8 字节原子操作 */
#else
    case 8: return false;  /* 32 位平台需要 CAS 循环 */
#endif
    default: return false;
    }
}
```

---

## 7.4 alloca 实现

`alloca` 是一个在栈上动态分配内存的函数，分配的内存在函数返回时自动释放。与 `malloc` 不同，`alloca` 不需要（也不能）手动释放，且速度极快——它本质上只是移动栈指针。

TCC 的 `alloca` 实现在 `lib/alloca.S` 中，每个支持的架构都有独立的汇编实现。

### 7.4.1 x86-64 实现

```asm
/* lib/alloca.S - x86-64 */
.globl alloca
alloca:
    pop    %rdx           /* 保存返回地址 */
#ifdef _WIN32
    mov    %rcx,%rax      /* Windows: 参数在 rcx */
#else
    mov    %rdi,%rax      /* System V: 参数在 rdi */
#endif
    add    $15,%rax       /* 向上对齐到 16 字节 */
    and    $-16,%rax
    jz     .Ldone

#ifdef _WIN32
.Lprobe:
    cmp    $4096,%rax
    jb     .Lalloc
    test   %rax,-4096(%rsp) /* Windows: 触碰页面以触发栈扩展 */
    sub    $4096,%rsp
    sub    $4096,%rax
    jmp    .Lprobe
.Lalloc:
#endif
    sub    %rax,%rsp      /* 移动栈指针 */
    mov    %rsp,%rax      /* 返回分配的地址 */
.Ldone:
    push   %rdx           /* 恢复返回地址（通过 push+ret） */
    ret
```

几个关键点：

1. **对齐**：分配的大小向上取整到 16 字节边界（`add $15; and $-16`），这是 System V ABI 对栈对齐的要求。
2. **Windows 栈探测**：在 Windows 上，栈空间是按需分配的（guard page 机制）。如果一次移动栈指针超过一个页面（4096 字节），中间的 guard page 不会被触发，导致访问违规。因此 Windows 版本需要逐页探测。
3. **返回地址保存**：`alloca` 是一个特殊函数——它修改了栈指针本身。因此需要先 `pop` 保存返回地址，分配完成后再通过 `push` + `ret` 恢复控制流。

### 7.4.2 i386 实现

```asm
/* lib/alloca.S - i386 */
.globl alloca
alloca:
    pop    %edx           /* 保存返回地址 */
    pop    %eax           /* 获取参数 */
    add    $3,%eax
    and    $-4,%eax       /* 对齐到 4 字节 */
    jz     .Ldone
    sub    %eax,%esp
    mov    %esp,%eax
.Ldone:
    push   %edx           /* 恢复返回地址（双 push 确保栈平衡） */
    push   %edx
    ret
```

i386 版本比 x86-64 简单得多，因为 32 位栈对齐要求更宽松（4 字节而非 16 字节），且 Windows 版本也需要栈探测。

### 7.4.3 ARM64 实现

ARM64 版本在 TCC 自举时使用原始机器码（因为 TCC 的 ARM64 汇编器可能还未就绪），在非 TCC 编译器下使用标准汇编助记符：

```asm
/* lib/alloca.S - ARM64（非 TCC 版本） */
.globl alloca
alloca:
    add    x0, x0, #15     /* 向上对齐到 16 字节 */
    and    x0, x0, #-16
#ifdef _WIN32
    cbz    x0, .Ldone      /* 大小为 0 则跳过 */
    mov    x1, #4096
.Lprobe:
    cmp    x0, x1
    b.lo   .Lalloc
    sub    x2, sp, x1
    ldr    xzr, [x2]       /* 触碰 guard page */
    sub    sp, sp, x1
    sub    x0, x0, x1
    b      .Lprobe
.Lalloc:
    cbz    x0, .Ldone
    sub    sp, sp, x0
#else
    sub    sp, sp, x0      /* 直接减小栈指针 */
#endif
.Ldone:
    mov    x0, sp           /* 返回分配的地址 */
    ret
```

### 7.4.4 RISC-V 实现

```asm
/* lib/alloca.S - RISC-V */
.globl alloca
alloca:
    sub    sp, sp, a0       /* 在栈上分配空间 */
    addi   sp, sp, -15
    andi   sp, sp, -16      /* 对齐到 16 字节 */
    add    a0, sp, zero     /* 返回分配的地址 */
    ret
```

RISC-V 版本是最简洁的——RISC-V 架构没有隐式的栈探测需求，对齐操作直接使用 `andi` 指令完成。

---

## 7.5 libtcc API 完整参考

libtcc 是 TinyCC 提供的嵌入式 API，允许在应用程序中嵌入 C 编译器。这使得 TCC 可以作为 JIT（即时编译）后端、脚本引擎的执行后端、或动态代码生成工具使用。

### 7.5.1 tcc_new / tcc_delete 生命周期

```c
/* 创建一个新的 TCC 编译上下文 */
TCCState *tcc_new(void);

/* 释放 TCC 编译上下文 */
void tcc_delete(TCCState *s);
```

`tcc_new()` 分配并初始化一个 `TCCState` 结构体。该结构体包含了编译器的全部状态：符号表、段列表、预处理器状态、错误处理配置等。调用 `tcc_new()` 后的默认配置如下：

```c
/* libtcc.c - tcc_new() 中的默认设置 */
s->gnu_ext = 1;                    /* 启用 GNU 扩展 */
s->tcc_ext = 1;                    /* 启用 TCC 扩展 */
s->nocommon = 1;                   /* 不使用 common 符号 */
s->dollars_in_identifiers = 1;     /* 允许 $ 在标识符中 */
s->cversion = 199901;              /* 默认 C99 */
s->warn_implicit_function_declaration = 1;
s->warn_discarded_qualifiers = 1;
s->ms_extensions = 1;
s->unwind_tables = 1;
s->ppfp = stdout;                  /* 预处理输出到 stdout */
```

`tcc_delete()` 释放所有关联的内存，包括段数据、库路径、包含路径、符号表等。

**重要**：一个 `TCCState` 实例不应被并发使用。如果需要并行编译，应为每个线程创建独立的 `TCCState`。

### 7.5.2 tcc_set_output_type

```c
int tcc_set_output_type(TCCState *s, int output_type);
```

设置输出类型。**必须在任何编译操作之前调用**，因为它会初始化必要的段和路径。可用的输出类型：

| 常量 | 值 | 说明 |
|------|---|------|
| `TCC_OUTPUT_MEMORY` | 1 | 编译结果保留在内存中，通过 `tcc_relocate()` 加载后直接调用 |
| `TCC_OUTPUT_OBJ` | 3 | 输出 ELF/COFF 目标文件（.o） |
| `TCC_OUTPUT_EXE` | 2 | 输出可执行文件 |
| `TCC_OUTPUT_DLL` | 4 | 输出动态链接库（.so/.dll） |
| `TCC_OUTPUT_PREPROCESS` | 5 | 仅执行预处理（类似 `gcc -E`） |

当设置为 `TCC_OUTPUT_MEMORY` 时，TCC 不会搜索 CRT 对象文件和系统库路径，因为不需要链接。当设置为 `TCC_OUTPUT_EXE` 时，如果定义了 `CONFIG_TCC_PIE`，则输出类型会被调整为 `TCC_OUTPUT_EXE | TCC_OUTPUT_DYN`（位置无关可执行文件）。

### 7.5.3 tcc_compile_string / tcc_add_file

```c
/* 编译一个 C 源码字符串 */
int tcc_compile_string(TCCState *s, const char *buf);

/* 添加文件（C 源码、目标文件、库等） */
int tcc_add_file(TCCState *s, const char *filename);
```

`tcc_compile_string()` 将字符串作为 C 源代码编译。内部调用链为：

```
tcc_compile_string()
  → tcc_compile(s, filetype, str, fd=-1)
    → tcc_open_bf(s, "<string>", len)   // 创建虚拟文件
    → preprocess_start()                 // 初始化预处理器
    → tccgen_init()                      // 初始化代码生成器
    → tccgen_compile()                   // 编译
    → tccgen_finish() / preprocess_end()
```

`tcc_add_file()` 可以接受多种文件类型，通过文件扩展名自动识别：

```c
/* libtcc.c - guess_filetype() */
if (!strcmp(ext, "S"))
    filetype = AFF_TYPE_ASMPP;    /* 需要预处理的汇编 */
else if (!strcmp(ext, "s"))
    filetype = AFF_TYPE_ASM;      /* 不需要预处理的汇编 */
else if (!PATHCMP(ext, "c") || !PATHCMP(ext, "h") || !PATHCMP(ext, "i"))
    filetype = AFF_TYPE_C;        /* C 源码 */
else
    filetype |= AFF_TYPE_BIN;     /* 二进制文件(.o, .a, .so) */
```

对于二进制文件，`tcc_add_file()` 会根据 ELF 文件头判断是目标文件、归档还是共享库，并分别调用 `tcc_load_object_file()`、`tcc_load_archive()` 或 `tcc_load_dll()`。

一个实用的调试技巧：可以在字符串前添加 `#line` 指令来改善错误消息：

```c
tcc_compile_string(s,
    "#line 1 \"my_script.c\"\n"
    "int main() { return 42; }\n");
```

### 7.5.4 tcc_relocate / tcc_get_symbol / tcc_run

```c
/* 执行所有重定位（在使用 tcc_get_symbol 之前必须调用） */
int tcc_relocate(TCCState *s1);

/* 获取符号的地址（在 tcc_relocate 之后调用） */
void *tcc_get_symbol(TCCState *s, const char *name);

/* 链接并运行 main() 函数 */
int tcc_run(TCCState *s, int argc, char **argv);
```

`tcc_relocate()` 是内存输出模式的关键步骤。它完成以下工作：

1. 将编译产生的各段（`.text`、`.data`、`.rodata`、`.bss`）拷贝到可执行内存区域
2. 应用所有重定位——修正函数调用地址、全局变量地址等
3. 使代码段可执行（通过 `mprotect` 或 `VirtualProtect`）

`tcc_get_symbol()` 在重定位完成后查找全局符号。它遍历 ELF 符号表找到指定名称的符号，并返回其在已重定位内存中的地址。

`tcc_run()` 是一个便利函数，相当于 `tcc_relocate()` + 查找 `main` 符号 + 调用 `main(argc, argv)` + 清理。它等价于：

```c
/* tcc_run(s, argc, argv) 的等价展开 */
tcc_relocate(s);
int (*main_func)(int, char**) = tcc_get_symbol(s, "main");
int ret = main_func(argc, argv);
return ret;
```

### 7.5.5 tcc_add_symbol

```c
int tcc_add_symbol(TCCState *s, const char *name, const void *val);
```

这个函数允许将宿主程序中的符号注册到编译器上下文中。当编译的代码引用了这些符号时，链接器会将它们解析到宿主程序提供的地址。

典型用法：

```c
/* 在宿主程序中定义的函数 */
int my_add(int a, int b) { return a + b; }
const char greeting[] = "Hello from host!";

/* 注册到编译器上下文 */
tcc_add_symbol(s, "my_add", my_add);
tcc_add_symbol(s, "greeting", greeting);

/* 编译的代码可以使用这些符号 */
tcc_compile_string(s,
    "extern int my_add(int, int);\n"
    "extern const char greeting[];\n"
    "int run() { return my_add(1, 2); }\n");
```

`tcc_add_symbol()` 的实现将符号添加到 ELF 动态符号表中，在重定位时这些符号会被标记为已解析。

### 7.5.6 tcc_set_error_func

```c
typedef void TCCErrorFunc(void *opaque, const char *msg);
void tcc_set_error_func(TCCState *s, void *error_opaque,
                        TCCErrorFunc *error_func);
```

设置自定义的错误/警告回调函数。默认情况下，TCC 将错误消息输出到 `stderr`。通过设置回调，应用程序可以：

- 将错误重定向到日志文件
- 在 GUI 应用中显示错误对话框
- 收集错误信息进行程序化处理

错误消息的格式为：`文件名:行号: error/warning: 消息内容`

```c
/* 错误回调示例 */
void my_error_handler(void *opaque, const char *msg)
{
    /* opaque 可以是任意用户数据 */
    FILE *log = (FILE *)opaque;
    fprintf(log, "[TCC] %s\n", msg);
}

/* 设置回调 */
tcc_set_error_func(s, log_file, my_error_handler);
```

错误回调在编译和链接过程中都会被调用。编译错误会导致 `tcc_compile_string()` 或 `tcc_add_file()` 返回 -1。

---

## 7.6 使用示例

### 7.6.1 示例 1: Hello World

最基本的 libtcc 用法：编译并执行一个简单的 C 程序。

```c
/* examples/libtcc_hello.c */
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "#include <tcclib.h>\n"
        "int main() {\n"
        "    printf(\"Hello from TCC!\\n\");\n"
        "    return 0;\n"
        "}\n");

    tcc_run(s, 0, NULL);
    tcc_delete(s);
    return 0;
}
```

### 7.6.2 示例 2: 调用编译后的函数

```c
/* examples/libtcc_functions.c */
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "int factorial(int n) {\n"
        "    if (n <= 1) return 1;\n"
        "    return n * factorial(n - 1);\n"
        "}\n");

    tcc_relocate(s);

    typedef int (*factorial_fn)(int);
    factorial_fn fact = (factorial_fn)tcc_get_symbol(s, "factorial");

    for (int i = 0; i <= 10; i++)
        printf("factorial(%d) = %d\n", i, fact(i));

    tcc_delete(s);
    return 0;
}
```

### 7.6.3 示例 3: 错误处理

```c
#include "libtcc.h"
#include <stdio.h>

static int error_count = 0;

void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    error_count++;
}

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_error_func(s, NULL, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* 故意包含语法错误 */
    int ret = tcc_compile_string(s,
        "int main() {\n"
        "    int x = ;\n"  /* 语法错误 */
        "    return x;\n"
        "}\n");

    if (ret == -1) {
        printf("Compilation failed with %d error(s)\n", error_count);
    }

    tcc_delete(s);
    return 0;
}
```

### 7.6.4 示例 4: 宿主函数注册

```c
/* examples/libtcc_host.c */
#include "libtcc.h"
#include <stdio.h>
#include <math.h>

/* 宿主程序中定义的函数 */
double host_sin(double x) { return sin(x); }
double host_cos(double x) { return cos(x); }
void host_print(const char *msg) { printf("[Host] %s\n", msg); }

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* 注册宿主函数 */
    tcc_add_symbol(s, "host_sin", host_sin);
    tcc_add_symbol(s, "host_cos", host_cos);
    tcc_add_symbol(s, "host_print", host_print);

    tcc_compile_string(s,
        "extern double host_sin(double);\n"
        "extern double host_cos(double);\n"
        "extern void host_print(const char *);\n"
        "\n"
        "void compute(void) {\n"
        "    double pi = 3.14159265358979;\n"
        "    double s = host_sin(pi / 4);\n"
        "    double c = host_cos(pi / 4);\n"
        "    host_print(\"sin(pi/4) = cos(pi/4)\");\n"
        "}\n");

    tcc_relocate(s);

    void (*compute)(void) = tcc_get_symbol(s, "compute");
    compute();

    tcc_delete(s);
    return 0;
}
```

### 7.6.5 示例 5: 输出到文件

```c
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_EXE);

    tcc_compile_string(s,
        "#include <tcclib.h>\n"
        "int main() {\n"
        "    printf(\"Compiled and linked by TCC\\n\");\n"
        "    return 0;\n"
        "}\n");

    /* 输出可执行文件（不需要 tcc_relocate） */
    tcc_output_file(s, "output_program");

    tcc_delete(s);
    return 0;
}
```

输出目标文件或 DLL 的方式类似：

```c
/* 输出目标文件 */
tcc_set_output_type(s, TCC_OUTPUT_OBJ);
tcc_output_file(s, "output.o");

/* 输出动态库 */
tcc_set_output_type(s, TCC_OUTPUT_DLL);
tcc_output_file(s, "liboutput.so");
```

---

## 7.7 内部实现: TCCState 结构详解

`TCCState` 是 TCC 最核心的数据结构，它包含了编译器的全部状态。以下是其关键字段的分类说明：

### 7.7.1 编译选项

```c
struct TCCState {
    /* 通用选项 */
    unsigned char verbose;        /* 详细输出级别 (0/1/2/3) */
    unsigned char nostdinc;       /* 不添加标准包含路径 */
    unsigned char nostdlib;       /* 不链接标准库 */
    unsigned char nostdlib_paths; /* 不搜索默认库路径 */
    unsigned char nocommon;       /* 不使用 common 符号 */
    unsigned char static_link;    /* 静态链接 */
    unsigned char rdynamic;       /* 导出所有符号 */
    unsigned char filetype;       /* 文件类型: NONE/C/ASM */
    unsigned char optimize;       /* 是否定义 __OPTIMIZE__ */
    unsigned int  cversion;       /* C 标准版本: 199901/201112 */

    /* 语言选项 */
    unsigned char char_is_unsigned;
    unsigned char leading_underscore;
    unsigned char ms_extensions;
    unsigned char ms_bitfields;
    unsigned char gnu89_inline;
    unsigned char unwind_tables;

    /* 调试选项 */
    unsigned char do_debug;       /* 生成调试信息 (-g) */
    unsigned char dwarf;          /* 使用 DWARF 格式（而非 STAB） */
    unsigned char do_backtrace;   /* 启用运行时回溯 (-bt) */
    unsigned char do_bounds_check;/* 启用边界检查 (-b) */
    /* ... */
};
```

### 7.7.2 路径配置

```c
    char *tcc_lib_path;    /* CONFIG_TCCDIR 或 -B 选项值 */
    char *soname;          /* -soname 指定的 SO 名称 */
    char *rpath;           /* -Wl,-rpath= 指定的运行时路径 */
    char *elfint;          /* ELF 解释器路径 */

    /* 包含路径列表 */
    char **include_paths;
    int nb_include_paths;
    char **sysinclude_paths;
    int nb_sysinclude_paths;

    /* 库路径列表 */
    char **library_paths;
    int nb_library_paths;
    char **crt_paths;
    int nb_crt_paths;
```

### 7.7.3 段管理

```c
    /* 预定义段 */
    Section *text_section;     /* 代码段 .text */
    Section *data_section;     /* 已初始化数据段 .data */
    Section *rodata_section;   /* 只读数据段 .rodata */
    Section *bss_section;      /* 未初始化数据段 .bss */
    Section *tdata_section;    /* 线程局部数据段 .tdata */
    Section *tbss_section;     /* 线程局部 BSS 段 .tbss */
    Section *common_section;   /* Common 符号段 */

    Section *cur_text_section; /* 当前正在生成代码的段 */

    /* 符号与动态链接段 */
    Section *symtab_section;   /* 符号表 */
    Section *dynsymtab_section;/* 动态符号表（临时） */
    Section *dynsym;           /* 导出的动态符号表 */
    Section *got;              /* 全局偏移表 */
    Section *plt;              /* 过程链接表 */

    /* 调试段 */
    Section *stab_section;         /* STAB 调试段 */
    Section *dwarf_info_section;   /* DWARF .debug_info */
    Section *dwarf_abbrev_section; /* DWARF .debug_abbrev */
    Section *dwarf_line_section;   /* DWARF .debug_line */
    Section *dwarf_str_section;    /* DWARF .debug_str */

    /* 边界检查段 */
    Section *bounds_section;   /* 全局边界描述 */
    Section *lbounds_section;  /* 局部边界描述 */

    /* 段数组（动态增长） */
    Section **sections;
    int nb_sections;
```

### 7.7.4 预处理器状态

```c
    /* #include 栈 */
    BufferedFile *include_stack[INCLUDE_STACK_SIZE];
    BufferedFile **include_stack_ptr;

    /* #ifdef 栈 */
    int ifdef_stack[IFDEF_STACK_SIZE];
    int *ifdef_stack_ptr;

    /* 已包含文件缓存（加速重复包含检测） */
    int cached_includes_hash[CACHED_INCLUDES_HASH_SIZE];
    CachedInclude **cached_includes;
    int nb_cached_includes;

    /* #pragma pack 栈 */
    int pack_stack[PACK_STACK_SIZE];
    int *pack_stack_ptr;

    /* -D/-U 命令行宏定义 */
    CString cmdline_defs;
    /* -include 命令行包含文件 */
    CString cmdline_incl;
```

### 7.7.5 错误处理与运行时

```c
    /* 错误回调 */
    void *error_opaque;
    void (*error_func)(void *opaque, const char *msg);
    int error_set_jmp_enabled;
    jmp_buf error_jmp_buf;    /* 编译错误时跳转 */
    int nb_errors;            /* 已发生的错误数 */

    /* 运行时（仅 TCC_IS_NATIVE） */
    const char *run_main;     /* tcc_run() 的入口符号 */
    void *run_ptr;            /* 运行时内存分配 */
    unsigned run_size;        /* 运行时内存大小 */
    struct TCCState *next;    /* 运行时状态链表 */
    struct rt_context *rc;    /* 回溯信息块 */
```

---

## 7.8 线程安全与多实例使用

### 7.8.1 并发编译的支持

TCC 通过编译时信号量 `tcc_compile_sem` 支持多线程使用。核心保护机制在 `tcc_enter_state()` 和 `tcc_exit_state()` 中：

```c
/* libtcc.c */
ST_DATA struct TCCState *tcc_state;  /* 全局当前状态 */

PUB_FUNC void tcc_enter_state(TCCState *s1)
{
    if (s1->error_set_jmp_enabled)
        return;
    WAIT_SEM(&tcc_compile_sem);
    tcc_state = s1;
}

PUB_FUNC void tcc_exit_state(TCCState *s1)
{
    if (s1->error_set_jmp_enabled)
        return;
    tcc_state = NULL;
    POST_SEM(&tcc_compile_sem);
}
```

TCC 的解析器和代码生成器大量使用全局变量（在 `tccpp.c` 和 `tccgen.c` 中定义），因此同一时刻只能有一个 `TCCState` 处于编译状态。信号量确保了这一点。

如果定义了 `CONFIG_TCC_SEMLOCK`（默认启用），则 `tcc_compile_sem` 使用 pthread 互斥量（或 Windows 临界区）。如果不希望使用锁（例如确定单线程使用），可以在编译 libtcc 时定义 `CONFIG_TCC_SEMLOCK=0`。

### 7.8.2 多实例模式

典型的多线程使用模式是：每个线程拥有自己的 `TCCState` 实例，线程间不共享编译状态：

```c
/* 每个线程独立编译 */
void *thread_func(void *arg)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_compile_string(s, (const char *)arg);
    tcc_relocate(s);
    /* ... 使用编译后的代码 ... */
    tcc_delete(s);
    return NULL;
}
```

TCC 的测试套件中 `tests/libtcc_test_mt.c` 提供了一个完整的多线程测试示例：它创建 20 个线程，每个线程独立编译并执行一个 Fibonacci 函数。

### 7.8.3 注意事项

1. **不要跨线程共享 TCCState**：虽然有信号量保护，但一个 TCCState 的内部状态（符号表、段数据等）不是为并发访问设计的。
2. **tcc_relocate 后的代码是线程安全的**：一旦代码被重定位并加载到内存，编译后的函数可以被任何线程安全调用（假设函数本身是线程安全的）。
3. **tcc_add_symbol 的时序**：符号注册必须在 `tcc_relocate()` 之前完成。
4. **错误回调可能在信号量内被调用**：如果错误回调中有耗时操作，可能影响其他线程的编译性能。

---

## 7.9 本章小结与练习

### 小结

本章介绍了 TinyCC 运行时生态系统的三大支柱：

1. **libtcc1.a**：为缺乏硬件支持的操作提供软件实现，包括 64 位除法、浮点转换和移位操作。这些函数仅在需要时由编译器自动链接。

2. **边界检查器**（bcheck.c）：通过 Splay 树追踪所有已分配的内存区域，在每次指针操作前验证合法性。它拦截了标准库的内存操作函数以提供完整的覆盖。

3. **libtcc 嵌入式 API**：提供了一套完整的生命周期管理接口（`tcc_new` → 配置 → 编译 → 重定位 → 使用 → `tcc_delete`），支持内存输出、文件输出、宿主符号注册和自定义错误处理。

此外，我们还详细分析了 TCCState 结构体的内部组织和线程安全模型。

### 练习 1：使用 libtcc 构建计算器

使用 libtcc API 构建一个简单的交互式计算器。程序从标准读取 C 表达式（如 `2 + 3 * 4`），用 libtcc 编译为函数，执行后输出结果。

**要求**：
- 使用 `TCC_OUTPUT_MEMORY` 模式
- 实现错误处理回调
- 支持数学函数（通过 `tcc_add_symbol` 注册 `sin`、`cos` 等）

### 练习 2：嵌入式编译器

编写一个嵌入式脚本系统：从配置文件中读取 C 代码片段，用 libtcc 编译并注册为回调函数，然后在主程序的事件循环中调用这些回调。

**要求**：
- 支持热重载（重新读取文件 → 重新编译 → 替换回调指针）
- 使用互斥锁保护回调指针的替换操作
- 测试多个脚本的并发执行


===== FILE: docs/ch08/examples/cross_compile.sh =====
#!/bin/bash
# cross_compile.sh - Cross compilation demo with TCC
#
# This script demonstrates how to build and use TCC cross compilers
# for different target architectures.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TCC_SRC="${SCRIPT_DIR}/../.."
BUILD_DIR="${TCC_SRC}/build-cross"

echo "=== TCC Cross Compilation Demo ==="
echo ""

# ---- Helper function ----
build_cross_compiler() {
    local arch=$1
    local prefix=$2
    local configure_opts=$3

    local build_dir="${BUILD_DIR}/${arch}"
    echo "--- Building ${arch} cross compiler ---"

    mkdir -p "${build_dir}"
    cd "${TCC_SRC}"

    echo "  Configure: ./configure ${configure_opts}"
    # Uncomment the following lines to actually build:
    # ./configure ${configure_opts}
    # make -j$(nproc)
    # cp tcc "${build_dir}/"

    echo "  Build directory: ${build_dir}"
    echo ""
}

# ---- Build cross compilers (commented out for demo) ----
# Uncomment to build:
#
# ARM64 cross compiler
# build_cross_compiler "arm64" "aarch64-linux-gnu-" \
#     "--targetarm64 --enable-cross --cross-prefix=aarch64-linux-gnu-"
#
# RISC-V 64 cross compiler
# build_cross_compiler "riscv64" "riscv64-linux-gnu-" \
#     "--targetriscv64 --enable-cross --cross-prefix=riscv64-linux-gnu-"
#
# i386 (32-bit x86) cross compiler
# build_cross_compiler "i386" "" \
#     "--targeti386 --enable-cross"

# ---- Demo: Simple C program ----
cat > /tmp/tcc_demo.c << 'EOF'
#include <stdio.h>

int factorial(int n) {
    int result = 1;
    int i;
    for (i = 2; i <= n; i++)
        result *= i;
    return result;
}

int main(void) {
    int i;
    printf("Factorial table:\n");
    for (i = 0; i <= 10; i++)
        printf("  %2d! = %d\n", i, factorial(i));
    return 0;
}
EOF

echo "=== Demo source: /tmp/tcc_demo.c ==="
cat /tmp/tcc_demo.c
echo ""

# ---- Cross compile to different targets ----

# Native compilation (x86-64)
echo "=== Native compilation (x86-64) ==="
if command -v tcc &> /dev/null; then
    tcc -o /tmp/tcc_demo_native /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo_native"
    file /tmp/tcc_demo_native 2>/dev/null || true
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# Generate object file
echo "=== Generate ELF object ==="
if command -v tcc &> /dev/null; then
    tcc -c -o /tmp/tcc_demo.o /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo.o"
    if command -v readelf &> /dev/null; then
        echo "  ELF header:"
        readelf -h /tmp/tcc_demo.o 2>/dev/null | grep -E "Class|Machine|Type" || true
    fi
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# ---- Generate assembly for different architectures ----
echo "=== Generate assembly (x86-64) ==="
if command -v tcc &> /dev/null; then
    tcc -S -o /tmp/tcc_demo.s /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo.s"
    echo "  First 20 lines of assembly:"
    head -20 /tmp/tcc_demo.s 2>/dev/null || true
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# ---- Using cross compilers (when available) ----
echo "=== Cross compilation examples ==="
echo ""
echo "  To build and use an ARM64 cross compiler:"
echo "    ./configure --targetarm64 --enable-cross"
echo "    make"
echo "    ./tcc -o demo_arm64 demo.c"
echo "    file demo_arm64    # should show: ELF 64-bit LSB, ARM aarch64"
echo ""
echo "  To build and use a RISC-V cross compiler:"
echo "    ./configure --targetriscv64 --enable-cross"
echo "    make"
echo "    ./tcc -o demo_riscv demo.c"
echo "    file demo_riscv    # should show: ELF 64-bit LSB, RISC-V"
echo ""
echo "  To cross-compile with a sysroot:"
echo "    ./tcc --sysroot=/path/to/sysroot -o demo demo.c"
echo ""

# ---- Cleanup ----
echo "=== Cleanup ==="
rm -f /tmp/tcc_demo.c /tmp/tcc_demo_native /tmp/tcc_demo.o /tmp/tcc_demo.s
echo "Done."


===== FILE: docs/ch08/examples/inline_asm.c =====
/*
 * inline_asm.c - Inline assembly examples for various architectures
 *
 * Demonstrates GCC-style inline assembly as supported by TCC.
 * Each function uses #if to select the appropriate architecture.
 */
#include <stdio.h>
#include <string.h>

/* ========== x86-64 specific examples ========== */
#if defined(__x86_64__) || defined(_M_X64)

/*
 * Example 1: CPUID - Get CPU vendor string
 * Uses the CPUID instruction to query CPU information.
 */
void get_cpu_vendor(char *vendor)
{
    unsigned int eax_val, ebx_val, ecx_val, edx_val;

    asm volatile(
        "cpuid"
        : "=a"(eax_val), "=b"(ebx_val), "=c"(ecx_val), "=d"(edx_val)
        : "a"(0)  /* EAX=0: get vendor string */
    );

    /* Vendor string is in EBX+EDX+ECX (12 bytes) */
    memcpy(vendor, &ebx_val, 4);
    memcpy(vendor + 4, &edx_val, 4);
    memcpy(vendor + 8, &ecx_val, 4);
    vendor[12] = '\0';
}

/*
 * Example 2: Atomic compare-and-swap
 * Implements a spinlock using lock cmpxchg.
 */
typedef struct { volatile int locked; } spinlock_t;

static inline void spin_lock(spinlock_t *lock)
{
    int expected = 0;
    asm volatile(
        "1: lock cmpxchg %1, %0\n\t"
        "jnz 1b"
        : "+m"(lock->locked), "+a"(expected)
        : "r"(1)
        : "memory", "cc"
    );
}

static inline void spin_unlock(spinlock_t *lock)
{
    asm volatile(
        "movl $0, %0"
        : "=m"(lock->locked)
        :
        : "memory"
    );
}

/*
 * Example 3: RDTSC - Read timestamp counter
 */
static inline unsigned long long rdtsc(void)
{
    unsigned int lo, hi;
    asm volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((unsigned long long)hi << 32) | lo;
}

/*
 * Example 4: BSF (Bit Scan Forward) - Find lowest set bit
 */
static inline int find_lowest_bit(unsigned long val)
{
    int result;
    asm volatile(
        "bsf %1, %0"
        : "=r"(result)
        : "r"(val)
        : "cc"
    );
    return result;
}

/*
 * Example 5: Using memory operands
 */
static inline void atomic_add(volatile int *ptr, int val)
{
    asm volatile(
        "lock addl %1, %0"
        : "+m"(*ptr)
        : "r"(val)
        : "memory"
    );
}

#define ARCH_NAME "x86-64"

/* ========== ARM64 (AArch64) specific examples ========== */
#elif defined(__aarch64__) || defined(_M_ARM64)

/*
 * Example 1: Read CPU ID register
 */
unsigned long read_ctr_el0(void)
{
    unsigned long val;
    asm volatile("mrs %0, ctr_el0" : "=r"(val));
    return val;
}

/*
 * Example 2: Atomic compare-and-swap using LDXR/STXR
 */
static inline long atomic_cas(volatile long *ptr, long oldval, long newval)
{
    long result;
    asm volatile(
        "1: ldxr %0, [%1]\n\t"
        "   cmp  %0, %2\n\t"
        "   b.ne 2f\n\t"
        "   stxr %w0, %3, [%1]\n\t"
        "   cbnz %w0, 1b\n\t"
        "   mov  %0, #1\n\t"
        "   b    3f\n\t"
        "2: clrex\n\t"
        "   mov  %0, #0\n\t"
        "3:\n\t"
        : "=&r"(result)
        : "r"(ptr), "r"(oldval), "r"(newval)
        : "memory", "cc"
    );
    return result;
}

/*
 * Example 3: DMB (Data Memory Barrier)
 */
static inline void dmb_sy(void)
{
    asm volatile("dmb sy" ::: "memory");
}

/*
 * Example 4: NOP and yield
 */
static inline void cpu_relax(void)
{
    asm volatile("yield" ::: "memory");
}

/*
 * Example 5: Cache line operations
 */
static inline void dc_cvac(unsigned long addr)
{
    asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
}

#define ARCH_NAME "AArch64"

/* ========== RISC-V 64 specific examples ========== */
#elif defined(__riscv) && __riscv_xlen == 64

/*
 * Example 1: Read cycle counter
 */
static inline unsigned long long rdtime(void)
{
    unsigned long long val;
    asm volatile("rdtime %0" : "=r"(val));
    return val;
}

/*
 * Example 2: Memory fence
 */
static inline void fence_rw_rw(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

/*
 * Example 3: Atomic LR/SC (Load-Reserved/Store-Conditional)
 */
static inline long atomic_cas(volatile long *ptr, long oldval, long newval)
{
    long result;
    int status;
    asm volatile(
        "1: lr.d    %0, %2\n\t"
        "   bne     %0, %3, 2f\n\t"
        "   sc.d    %1, %4, %2\n\t"
        "   bnez    %1, 1b\n\t"
        "   li      %0, 1\n\t"
        "   j       3f\n\t"
        "2: li      %0, 0\n\t"
        "3:\n\t"
        : "=&r"(result), "=&r"(status), "+A"(*ptr)
        : "r"(oldval), "r"(newval)
        : "memory"
    );
    return result;
}

/*
 * Example 4: NOP
 */
static inline void nop(void)
{
    asm volatile("nop");
}

/*
 * Example 5: Read CSR
 */
static inline unsigned long read_mvendorid(void)
{
    unsigned long val;
    asm volatile("csrr %0, mvendorid" : "=r"(val));
    return val;
}

#define ARCH_NAME "RISC-V 64"

/* ========== ARM 32-bit specific examples ========== */
#elif defined(__arm__)

/*
 * Example 1: SWI (Software Interrupt) for system call
 */
static inline int syscall0(int nr)
{
    register int r7 asm("r7") = nr;
    register int r0 asm("r0");
    asm volatile("swi #0" : "=r"(r0) : "r"(r7) : "memory");
    return r0;
}

/*
 * Example 2: MRS - Read CPSR
 */
static inline unsigned int read_cpsr(void)
{
    unsigned int val;
    asm volatile("mrs %0, cpsr" : "=r"(val));
    return val;
}

/*
 * Example 3: Disable/Enable IRQs
 */
static inline unsigned int disable_irq(void)
{
    unsigned int cpsr, new_cpsr;
    asm volatile(
        "mrs %0, cpsr\n\t"
        "orr %1, %0, #0x80\n\t"
        "msr cpsr_c, %1"
        : "=r"(cpsr), "=r"(new_cpsr)
        :
        : "memory"
    );
    return cpsr;
}

static inline void restore_cpsr(unsigned int cpsr)
{
    asm volatile(
        "msr cpsr_c, %0"
        :
        : "r"(cpsr)
        : "memory"
    );
}

/*
 * Example 4: CLZ (Count Leading Zeros)
 */
static inline int clz(unsigned int val)
{
    int result;
    asm volatile("clz %0, %1" : "=r"(result) : "r"(val));
    return result;
}

#define ARCH_NAME "ARM 32-bit"

#else
#error "Unsupported architecture for inline assembly examples"
#endif

/* ========== Common test code ========== */

int main(void)
{
    printf("Inline Assembly Examples (%s)\n", ARCH_NAME);
    printf("=============================\n\n");

#if defined(__x86_64__) || defined(_M_X64)
    /* CPUID */
    char vendor[13];
    get_cpu_vendor(vendor);
    printf("CPU Vendor: %s\n", vendor);

    /* Timestamp counter */
    unsigned long long t1 = rdtsc();
    volatile int i;
    volatile int sum = 0;
    for (i = 0; i < 1000000; i++) sum += i;
    unsigned long long t2 = rdtsc();
    printf("Loop cycles (approx): %llu\n", t2 - t1);
    printf("Sum: %d\n", sum);

    /* Bit scan */
    unsigned long bits = 0b10100000;
    printf("Lowest set bit in 0x%lx: %d\n", bits, find_lowest_bit(bits));

    /* Atomic add */
    volatile int counter = 0;
    atomic_add(&counter, 42);
    printf("After atomic_add(42): %d\n", counter);

    /* Spinlock demo */
    spinlock_t lock = { .locked = 0 };
    spin_lock(&lock);
    printf("Lock acquired\n");
    spin_unlock(&lock);
    printf("Lock released\n");

#elif defined(__aarch64__) || defined(_M_ARM64)
    unsigned long ctr = read_ctr_el0();
    printf("CTR_EL0: 0x%lx\n", ctr);
    dmb_sy();
    printf("Data memory barrier executed\n");
    cpu_relax();
    printf("CPU relax executed\n");

#elif defined(__riscv) && __riscv_xlen == 64
    unsigned long long t = rdtime();
    printf("Timer: %llu\n", t);
    fence_rw_rw();
    printf("Fence executed\n");
    nop();
    printf("NOP executed\n");

#elif defined(__arm__)
    unsigned int cpsr = read_cpsr();
    printf("CPSR: 0x%08x\n", cpsr);
    printf("CLZ(0xFF000000) = %d\n", clz(0xFF000000));
#endif

    printf("\nAll examples completed successfully.\n");
    return 0;
}


===== FILE: docs/ch08/exercises/ex1_cross.md =====
# 练习 1: 交叉编译实践

## 背景

TCC 可以配置为面向不同 CPU 架构和操作系统生成代码的交叉编译器。本练习要求你实际构建并使用一个交叉编译器。

## 任务

### 基础任务

1. **构建 i386 交叉编译器**：在 x86-64 系统上构建一个面向 32 位 x86 的 TCC 交叉编译器
   ```bash
   ./configure --targeti386 --enable-cross
   make
   ```

2. **编译测试程序**：使用交叉编译器编译以下测试程序并验证输出格式
   ```c
   #include <stdio.h>
   int main(void) {
       printf("sizeof(void*) = %zu\n", sizeof(void*));
       return 0;
   }
   ```

3. **检查输出**：使用 `file` 和 `readelf` 命令分析生成的可执行文件：
   - 文件格式（ELF 32-bit 还是 64-bit？）
   - 目标架构（Machine 字段）
   - 入口地址

### 进阶任务

4. **构建 ARM64 交叉编译器**：如果系统有 `aarch64-linux-gnu-gcc` 交叉工具链，构建 TCC 的 ARM64 版本并生成目标文件

5. **对比分析**：分别用 TCC 交叉编译器和 GCC 交叉编译器编译同一程序，用 `objdump -d` 反汇编并比较生成代码的质量

## 提示

- 交叉编译器没有 `-run` 功能（`TCC_IS_NATIVE` 未定义）
- 使用 `--sysroot` 指定目标系统的根文件系统
- 如果遇到链接错误，可能需要安装目标架构的 `crt` 文件

## 验证标准

- 能成功构建至少一个交叉编译器
- 生成的可执行文件或目标文件格式正确
- 理解了交叉编译中 `TCC_IS_NATIVE` 的含义


===== FILE: docs/ch08/exercises/ex2_asm.md =====
# 练习 2: 内联汇编实验

## 背景

TCC 支持 GCC 风格的内联汇编，允许在 C 代码中嵌入平台特定的汇编指令。本练习要求你编写使用内联汇编的程序。

## 任务

### 任务 1: 平台信息查询

编写一个程序，使用内联汇编查询当前 CPU 的信息：
- **x86-64**：使用 `cpuid` 指令获取 CPU 厂商、型号、特性标志
- **ARM64**：使用 `mrs` 指令读取 `MIDR_EL1`、`CTR_EL0` 等系统寄存器
- **RISC-V**：使用 `csrr` 指令读取 `mvendorid`、`marchid` 等 CSR

### 事务 2: 无锁数据结构

使用 `lock cmpxchg`（x86-64）或 `ldxr/stxr`（ARM64）或 `lr.d/sc.d`（RISC-V）实现一个简单的无锁栈：

```c
typedef struct node {
    int data;
    struct node *next;
} node_t;

typedef struct {
    node_t *top;
} lockfree_stack_t;

void push(lockfree_stack_t *stack, node_t *node);
node_t *pop(lockfree_stack_t *stack);
```

### 任务 3: 精确计时

使用平台特定的高精度计时器测量代码执行时间：
- **x86-64**：`rdtsc` 指令
- **ARM64**：`cntvct_el0` 寄存器
- **RISC-V**：`rdtime` 指令

编写一个基准测试，比较 TCC 编译代码与 GCC `-O2` 编译代码的执行速度。

## 提示

- 使用 `#if defined(__x86_64__)` 等宏进行平台选择
- `asm volatile` 防止编译器优化掉汇编代码
- 破坏列表（clobber list）中的 `"memory"` 确保编译器不会跨汇编指令缓存内存值
- 破坏列表中的 `"cc"` 表示条件码寄存器被修改

## 验证标准

- 程序能在目标平台上正确编译和运行
- 内联汇编的约束和破坏列表正确
- 理解了 `volatile`、约束字符串和破坏列表的作用


===== FILE: docs/ch08/index.md =====
# 第八章 跨平台与汇编器

TinyCC 的一大设计特点是其高度模块化的后端架构。通过在编译时选择不同的 `TCC_TARGET_*` 宏，同一套前端代码可以生成针对不同 CPU 架构和操作系统的代码。本章将深入分析 TCC 的后端抽象层、各平台的代码生成器、目标 OS 支持、交叉编译以及内置汇编器。

---

## 8.1 后端接口抽象

TCC 的代码生成器通过一组约定好的函数接口与平台无关的前端（`tccgen.c`）交互。每个目标架构必须实现这些函数，前端在适当的时候调用它们。

### 8.1.1 核心代码生成接口

以下是后端必须实现的主要函数（定义在 `tccgen.c` 中，由各 `<target>-gen.c` 实现）：

**基本数据操作：**

| 函数 | 功能 |
|------|------|
| `load(int r, SValue *sv)` | 将一个 SValue 加载到寄存器 r 中 |
| `store(int r, SValue *sv)` | 将寄存器 r 的值存储到 SValue 指定的位置 |
| `gfunc_start(CType *func_type)` | 开始函数调用参数传递 |
| `gfunc_param(SValue *v)` | 传递一个函数参数 |
| `gfunc_call(int nb_args)` | 发射函数调用指令 |
| `gfunc_prolog(CType *func_type)` | 生成函数序言（保存寄存器、分配栈帧） |
| `gfunc_epilog(void)` | 生成函数尾声（恢复寄存器、释放栈帧） |

**算术与逻辑操作：**

| 函数 | 功能 |
|------|------|
| `gen_opi(int op)` | 对两个整数值执行二元操作 |
| `gen_opf(int op)` | 对两个浮点值执行二元操作 |
| `gen_cvt_itof(int t)` | 整数转浮点 |
| `gen_cvt_ftoi(int t)` | 浮点转整数 |
| `gen_cvt_ftof(int t)` | 浮点格式转换 |

**跳转与分支：**

| 函数 | 功能 |
|------|------|
| `gjmp(int t)` | 生成无条件跳转，返回跳转目标 token |
| `gjmp_true(int t, int c)` | 条件为真时跳转 |
| `gjmp_false(int t, int c)` | 条件为假时跳转 |
| `gtst(int inv, int t)` | 测试条件并生成条件跳转 |

**值操作辅助：**

| 函数 | 功能 |
|------|------|
| `gv(int rc)` | 将栈顶值"具体化"到寄存器中（register class rc） |
| `gv2(int r1, int r2)` | 将栈顶两个值加载到指定寄存器 |
| `vpush(int v)` | 将值压入虚拟栈 |
| `vpop(void)` | 弹出栈顶值 |
| `vrotb(int n)` | 旋转栈顶 n 个元素 |
| `vset(CType *type, int r, int v)` | 设置栈顶值 |
| `save_reg(int r)` | 保存寄存器到栈（如果它被占用） |
| `get_reg(int rc)` | 分配一个空闲寄存器 |

### 8.1.2 TARGET_DEFS_ONLY 模式

每个后端文件（如 `x86_64-gen.c`）都使用 `#ifdef TARGET_DEFS_ONLY` 条件编译。当以定义了 `TARGET_DEFS_ONLY` 的方式包含时，只暴露寄存器定义和常量；当正常编译时，暴露完整的代码生成实现。

```c
/* x86_64-gen.c 的结构 */
#ifdef TARGET_DEFS_ONLY

#define NB_REGS        25
#define RC_INT         0x0001
#define RC_FLOAT       0x0002
#define PTR_SIZE       8
/* ... 寄存器枚举和宏定义 ... */

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"
/* ... 完整的代码生成实现 ... */

#endif
```

这个设计使得 `tcc.h` 可以通过包含所有后端文件（以 `TARGET_DEFS_ONLY` 模式）来获取所有目标架构的公共定义，而实际编译时只链接一个后端的完整实现。

### 8.1.3 链接器接口

每个后端还需要实现链接相关的函数（在 `<target>-link.c` 中）：

| 函数 | 功能 |
|------|------|
| `relocate_section(Section *s)` | 对一个段进行重定位 |
| `relocate_rel(Section *s)` | 处理 REL 类型的重定位表项 |
| `relocate(TCCState *s1)` | 执行全局重定位 |
| `build_got(TCCState *s1)` | 构建 GOT（全局偏移表） |
| `build_got_entries(TCCState *s1)` | 为需要的符号创建 GOT 条目 |
| `put_got_offset(int index, addr_t off)` | 写入 GOT 偏移 |
| `sym_plt_func(TCCState *s1, int flags)` | 处理 PLT 条目 |
| `create_plt_entry(TCCState *s1, unsigned reloc_type, ...)` | 创建 PLT 条目 |

### 8.1.4 汇编器接口

每个支持内联汇编的后端还需要在 `<target>-asm.c` 中实现汇编指令的解析和编码：

| 函数 | 功能 |
|------|------|
| `asm_parse_instr(void)` | 解析一条汇编指令 |
| `asm_compute_constraints(ASMOperand *operands, ...)` | 计算约束分配 |
| `subst_asm_operands(ASMOperand *operands, ...)` | 替换汇编中的操作数引用 |

---

## 8.2 x86_64 平台

x86-64 是 TCC 支持最完善的平台，也是默认的编译目标（在 64 位 Linux 上）。

### 8.2.1 寄存器分配

TCC 的 x86-64 后端使用 25 个寄存器槽位：

```c
/* x86_64-gen.c */
#define NB_REGS     25
#define NB_ASM_REGS 16

enum {
    TREG_RAX = 0,   /* 返回值寄存器 */
    TREG_RCX = 1,   /* 第4个参数（Windows）/ 临时 */
    TREG_RDX = 2,   /* 第3个参数（Windows）/ 临时 */
    TREG_RSP = 4,   /* 栈指针（不分配给值） */
    TREG_RSI = 6,   /* 第2个参数（System V） */
    TREG_RDI = 7,   /* 第1个参数（System V） */

    TREG_R8  = 8,
    TREG_R9  = 9,
    TREG_R10 = 10,
    TREG_R11 = 11,

    TREG_XMM0 = 16, /* 浮点返回值 */
    TREG_XMM1 = 17,
    /* ... XMM2-XMM7 ... */

    TREG_ST0 = 24,  /* x87 栈顶（long double） */

    TREG_MEM = 0x20 /* 内存位置标记 */
};
```

寄存器分类用于约束分配：

```c
#define RC_INT    0x0001    /* 通用整数寄存器 */
#define RC_FLOAT  0x0002    /* 浮点寄存器 */
#define RC_RAX    0x0004    /* 特定 RAX */
#define RC_RDX    0x0008    /* 特定 RDX */
#define RC_RCX    0x0010    /* 特定 RCX */
#define RC_XMM0   0x1000    /* 特定 XMM0 */
#define RC_IRET   RC_RAX    /* 整数返回寄存器 */
#define RC_FRET   RC_XMM0   /* 浮点返回寄存器 */
```

### 8.2.2 System V ABI 调用约定

在 Linux/macOS 上，x86-64 使用 System V AMD64 ABI：

**整数参数传递**：依次使用 `RDI`、`RSI`、`RDX`、`RCX`、`R8`、`R9`（共 6 个寄存器），超出部分通过栈传递。

**浮点参数传递**：依次使用 `XMM0`-`XMM7`（共 8 个寄存器）。

**返回值**：整数在 `RAX`（和 `RDX` 用于 128 位），浮点在 `XMM0`（和 `XMM1`）。

**调用者保存寄存器**：`RAX`、`RCX`、`RDX`、`RSI`、`RDI`、`R8`-`R11`（可被被调函数自由修改）。

**被调者保存寄存器**：`RBX`、`RBP`、`R12`-`R15`、`RSP`（被调函数必须保存和恢复）。

TCC 在 `gfunc_param` 中实现了这些规则：

```c
/* x86_64-gen.c - 参数传递逻辑（简化） */
static void gfunc_param(SValue *v)
{
    /* 整数参数 */
    if (is_integer_type(vtype)) {
        if (nb_reg_args < 6) {
            /* 使用寄存器传递 */
            reg = arg_regs[nb_reg_args++];
            load_reg(v, reg);
        } else {
            /* 通过栈传递 */
            vpush(v);
            gadd_sp(PTR_SIZE); /* 栈上分配空间 */
        }
    }
    /* 浮点参数 */
    else if (is_float_type(vtype)) {
        if (nb_xmm_args < 8) {
            reg = xmm_regs[nb_xmm_args++];
            load_reg(v, reg);
        } else {
            /* 通过栈传递 */
        }
    }
}
```

### 8.2.3 Windows x64 调用约定

Windows 使用不同的调用约定（Microsoft x64）：

**整数参数**：`RCX`、`RDX`、`R8`、`R9`（仅 4 个）。

**浮点参数**：`XMM0`-`XMM3`（仅 4 个）。

**影子空间**：调用者必须在栈上预留 32 字节的"影子空间"（shadow space）供被调者使用。

**栈对齐**：调用前栈必须 16 字节对齐。

TCC 通过预定义宏区分两种约定：

```c
#ifdef _WIN32
    /* Windows x64 ABI */
    static const int arg_regs[] = { TREG_RCX, TREG_RDX, TREG_R8, TREG_R9 };
#else
    /* System V ABI */
    static const int arg_regs[] = { TREG_RDI, TREG_RSI, TREG_RDX,
                                    TREG_RCX, TREG_R8, TREG_R9 };
#endif
```

### 8.2.4 指令编码

x86-64 的指令编码比其他 RISC 架构复杂得多。TCC 通过一组辅助函数来发射编码后的指令：

```c
/* x86_64-gen.c - 指令编码辅助 */
static void o(unsigned int c)
{
    /* 将一个或多个字节写入当前代码段 */
    int ind1 = ind + 4;
    unsigned char *p;
    if (nocode_wanted) return;
    p = section_ptr_add(cur_text_section, 4);
    write32le(p, c);
    ind = ind1;
}

static void gen_modrm(int mod, int reg, int rm, ...)
{
    /* 生成 ModR/M 字节 */
    /* mod (2 bits): 寻址模式
     * reg (3 bits): 寄存器编号
     * rm  (3 bits): 寄存器/内存操作数 */
    o(0xC0 | (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

static void gen_rex(int width, int reg, int index, int base)
{
    /* 生成 REX 前缀 */
    /* REX.W (bit 3): 64 位操作数
     * REX.R (bit 2): 扩展 reg 字段
     * REX.X (bit 1): 扩展 SIB index
     * REX.B (bit 0): 扩展 rm/base */
    o(0x40 | (width << 3) | ((reg >> 1) & 4) |
      ((index >> 2) & 2) | ((base >> 3) & 1));
}
```

一个完整的 x86-64 指令编码示例——生成 `mov %eax, %ecx`：

```
REX 前缀:  不需要（32 位操作，无扩展寄存器）
操作码:    0x89 (MOV r/m32, r32)
ModR/M:    0xC1 (mod=11, reg=ECX(001), rm=EAX(000))
编码结果:  89 C1
```

---

## 8.3 ARM64 平台

ARM64（AArch64）是 TCC 支持的第二个主要 64 位平台。

### 8.3.1 寄存器分配

```c
/* arm64-gen.c */
#define NB_REGS 28  /* x0-x18, x30, v0-v7 */

#define TREG_R(x) (x)      /* 通用寄存器: x=0..18 */
#define TREG_R30  19        /* 链接寄存器 */
#define TREG_F(x) (x + 20) /* 浮点寄存器: v0-v7 */

#define RC_INT   (1 << 0)
#define RC_FLOAT (1 << 1)
#define RC_R(x)  (1 << (2 + (x)))  /* 特定整数寄存器 */
#define RC_F(x)  (1 << (22 + (x))) /* 特定浮点寄存器 */

#define REG_IRET (TREG_R(0))  /* x0: 整数返回 */
#define REG_FRET (TREG_F(0))  /* v0: 浮点返回 */
```

ARM64 的寄存器比 x86-64 更规整：31 个通用寄存器（x0-x30）和 32 个浮点/SIMD 寄存器（v0-v31），加上零寄存器 xzr 和栈指针 sp。

TCC 的寄存器分配使用了 x0-x18（19 个参数/临时寄存器）和 x30（链接寄存器），以及 v0-v7（8 个浮点寄存器），共 28 个可分配槽位。

### 8.3.2 AAPCS64 调用约定

ARM64 使用 AAPCS64（ARM Architecture Procedure Call Standard）：

**整数参数**：`x0`-`x7`（8 个寄存器），超出部分通过栈传递。

**浮点参数**：`v0`-`v7`（8 个寄存器）。

**返回值**：整数在 `x0`（和 `x1` 用于 128 位），浮点在 `v0`。

**帧指针**：`x29`（FP），链接寄存器：`x30`（LR）。

TCC 的函数序言生成：

```c
/* arm64-gen.c - gfunc_prolog 简化展示 */
static void gfunc_prolog(CType *func_type)
{
    /* 保存帧指针和链接寄存器 */
    /* stp x29, x30, [sp, #-framesize]! */
    o(0xa9000000 | ...);

    /* 设置帧指针 */
    /* mov x29, sp */
    o(0x910003fd);

    /* 保存被调者保存寄存器 */
    /* 为局部变量分配栈空间 */
    /* sub sp, sp, #locals_size */
}
```

### 8.3.3 指令编码

ARM64 使用固定 32 位指令编码，这比 x86-64 的变长编码简单得多。TCC 通过辅助函数编码指令：

```c
/* arm64-gen.c - 指令编码示例 */
/* ADD Xd, Xn, #imm12 */
static void emit_add_imm(int d, int n, int imm12)
{
    o(0x91000000 | (imm12 << 10) | (n << 5) | d);
}

/* MOV Xd, Xn (编码为 ORR Xd, XZR, Xn) */
static void emit_mov(int d, int n)
{
    o(0xaa0003e0 | (n << 16) | d);
}
```

由于 TCC 的 ARM64 汇编器在自举时可能还未就绪，`alloca.S` 等关键文件中的 ARM64 代码段使用原始机器码（`.int` 指令）而非汇编助记符。

---

## 8.4 ARM 32 位平台

### 8.4.1 寄存器分配

```c
/* arm-gen.c */
#ifdef TCC_ARM_VFP
#define NB_REGS 13
#else
#define NB_REGS 9
#endif

#define RC_INT   0x0001
#define RC_FLOAT 0x0002
#define RC_R0    0x0004
#define RC_R1    0x0008
#define RC_R2    0x0010
#define RC_R3    0x0020
#define RC_R12   0x0040  /* IP: 临时寄存器 */
#define RC_F0    0x0080
/* ... F1-F7（VFP 模式下）... */
```

ARM 32 位有 16 个通用寄存器（r0-r15），其中：
- `r0`-`r3`：参数传递和返回值
- `r4`-`r11`：被调者保存寄存器
- `r12`（IP）：过程间临时寄存器
- `r13`（SP）：栈指针
- `r14`（LR）：链接寄存器
- `r15`（PC）：程序计数器

### 8.4.2 ARM vs Thumb 模式

ARM 处理器支持两种指令集：
- **ARM 模式**：32 位固定宽度指令，功能完整
- **Thumb 模式**：16 位（Thumb）或 16/32 位混合（Thumb-2）指令，代码密度更高

TCC 默认生成 ARM 指令。Thumb 模式的支持取决于 `CONFIG_TCC_CPUVER` 的设置。

### 8.4.3 EABI（嵌入式 ABI）

ARM EABI 是嵌入式系统中广泛使用的 ABI 标准：

**参数传递**：`r0`-`r3` 传递前 4 个参数，超出部分通过栈传递。

**返回值**：`r0`（和 `r1` 用于 64 位）。

**浮点**：在 VFP 模式下，浮点参数使用 `s0`-`s15`（单精度）或 `d0`-`d7`（双精度）。

TCC 通过 `TCC_ARM_EABI`、`TCC_ARM_VFP`、`TCC_ARM_HARDFLOAT` 等宏来控制 ABI 选择：

```c
#ifdef TCC_ARM_HARDFLOAT
    /* 硬浮点: 浮点参数通过 VFP 寄存器传递 */
    s->float_abi = ARM_HARD_FLOAT;
#else
    /* 软浮点: 浮点参数通过整数寄存器传递 */
#endif
```

ARM 平台还定义了额外的运行时辅助函数（在 libtcc1 中），例如 `__aeabi_memcpy` 系列：

```c
/* lib/bcheck.c - ARM EABI 内存操作包装 */
void *__bound___aeabi_memcpy(void *dst, const void *src, size_t size);
void *__bound___aeabi_memmove(void *dst, const void *src, size_t size);
void *__bound___aeabi_memset(void *dst, int c, size_t size);
```

---

## 8.5 RISC-V 64

RISC-V 是 TCC 最新添加的目标架构之一。

### 8.5.1 寄存器分配

```c
/* riscv64-gen.c */
#define NB_REGS 19  /* a0-a7, fa0-fa7, xxx, ra, sp */

#define TREG_R(x) (x)      /* 整数寄存器: x=0..7 (a0-a7) */
#define TREG_F(x) (x + 8)  /* 浮点寄存器: x=0..7 (fa0-fa7) */

#define TREG_RA 17  /* 返回地址 (x1) */
#define TREG_SP 18  /* 栈指针 (x2) */

#define PTR_SIZE 8
#define CHAR_IS_UNSIGNED
```

RISC-V 使用 LP64D ABI：

**整数参数**：`a0`-`a7`（即 `x10`-`x17`，8 个寄存器）。

**浮点参数**：`fa0`-`fa7`（即 `f10`-`f17`，8 个寄存器）。

**返回值**：整数在 `a0`（和 `a1` 用于 128 位），浮点在 `fa0`。

TCC 在 RISC-V 后端还定义了平台特定的预定义宏：

```c
/* riscv64-gen.c */
ST_DATA const char *const target_machine_defs =
    "__riscv\0"
    "__riscv_xlen 64\0"
    "__riscv_flen 64\0"
    "__riscv_div\0"
    "__riscv_mul\0"
    "__riscv_fdiv\0"
    "__riscv_fsqrt\0"
    "__riscv_float_abi_double\0"
    ;
```

### 8.5.2 指令编码

RISC-V 使用固定的 32 位指令编码（基本指令集），具有清晰的格式分类：

| 格式 | 用途 | 字段 |
|------|------|------|
| R-type | 寄存器-寄存器运算 | funct7, rs2, rs1, funct3, rd, opcode |
| I-type | 立即数运算/加载 | imm[11:0], rs1, funct3, rd, opcode |
| S-type | 存储 | imm[11:5], rs2, rs1, funct3, imm[4:0], opcode |
| B-type | 条件分支 | 类似 S-type，12 位偏移 |
| U-type | 长立即数 | imm[31:12], rd, opcode |
| J-type | 跳转 (JAL) | 20 位偏移, rd, opcode |

TCC 的 RISC-V 后端使用辅助函数来编码各类指令：

```c
/* riscv64-gen.c */
static void emit_R(int opcode, int rd, int funct3, int rs1, int rs2, int funct7)
{
    o(funct7 << 25 | rs2 << 20 | rs1 << 15 | funct3 << 12 | rd << 7 | opcode);
}
```

### 8.5.3 PC 相对寻址

RISC-V 使用 PC 相对寻址来加载全局变量地址，这需要两条指令（`auipc` + `addi`）配合完成。TCC 在 `riscv64-link.c` 中使用 `pcrel_hi_entries` 来跟踪高 20 位的重定位信息：

```c
/* tcc.h - RISC-V 特有的状态字段 */
#ifdef TCC_TARGET_RISCV64
    struct pcrel_hi { addr_t addr, val; } **pcrel_hi_entries;
    int nb_pcrel_hi_entries;
#endif
```

---

## 8.6 目标 OS 支持

TCC 不仅支持多种 CPU 架构，还支持多种操作系统。不同 OS 的主要区别在于可执行文件格式、动态链接机制和系统调用约定。

### 8.6.1 Linux ELF

ELF（Executable and Linkable Format）是 Linux 和大多数 Unix 系统的原生可执行格式。TCC 的 ELF 支持在 `tccelf.c` 和各平台的 `<target>-link.c` 中实现。

关键特性：
- 生成标准 ELF 可执行文件、共享库和目标文件
- 支持 GOT/PLT 用于位置无关代码（PIC）
- 支持 DWARF 和 STAB 调试信息
- 支持 `.eh_frame` 异常处理表
- ELF 解释器路径通过 `CONFIG_TCC_ELFINTERP` 配置：

```c
/* tcc.h */
#if defined(TCC_TARGET_X86_64)
# define CONFIG_TCC_ELFINTERP "/lib64/ld-linux-x86-64.so.2"
#elif defined(TCC_TARGET_ARM64)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux-aarch64.so.1"
#elif defined(TCC_TARGET_RISCV64)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux-riscv64-lp64d.so.1"
#elif defined(TCC_TARGET_ARM)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.3"
#else
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.2"
#endif
```

### 8.6.2 Windows PE

Windows 使用 PE（Portable Executable）格式。TCC 的 PE 支持在 `tccpe.c` 中实现，当定义了 `TCC_TARGET_PE` 时启用。

关键特性：
- 生成 PE 可执行文件和 DLL
- 支持 PE 导入表和导出表
- 支持 Windows SEH（结构化异常处理）
- 支持 PE 特有的链接选项：

```c
/* tcc.h - PE 特有状态 */
#ifdef TCC_TARGET_PE
    int pe_subsystem;               /* PE 子系统类型 */
    unsigned pe_characteristics;    /* PE 文件特征 */
    unsigned pe_dll_characteristics;/* DLL 特征 */
    unsigned pe_file_align;         /* 文件对齐 */
    unsigned pe_stack_size;         /* 栈大小 */
    addr_t pe_imagebase;            /* 映像基地址 */
#endif
```

当运行在 Windows 上时，TCC 使用 Windows 调用约定（参见 8.2.3 节），并且 `tcc_run()` 通过 `VirtualAlloc` 和 `VirtualProtect` 管理运行时内存。

### 8.6.3 macOS Mach-O

macOS 使用 Mach-O 格式。TCC 的 Mach-O 支持在 `tccmacho.c` 中实现（通过 `TCC_TARGET_MACHO` 启用），支持新的 Mach-O 代码（`CONFIG_NEW_MACHO`）。

关键特性：
- 生成 Mach-O 可执行文件和动态库（`.dylib`）
- 支持 TBD 文件（Text-Based Definition，Apple 的符号定义格式）
- 支持 macOS SDK 路径自动发现
- 支持 install_name：

```c
/* tcc.h - Mach-O 特有状态 */
#if defined TCC_TARGET_MACHO
    char *install_name;
    uint32_t compatibility_version;
    uint32_t current_version;
#endif
```

### 8.6.4 BSD 系统

TCC 支持多种 BSD 变体（FreeBSD、OpenBSD、NetBSD、DragonFly BSD）。这些系统都使用 ELF 格式，但有一些差异：

```c
/* tcc.h */
#if defined TARGETOS_OpenBSD || defined TARGETOS_FreeBSD \
    || defined TARGETOS_NetBSD || defined TARGETOS_FreeBSD_kernel
# define TARGETOS_BSD 1
#endif
```

OpenBSD 的特殊处理包括动态库版本选择：

```c
/* libtcc.c - OpenBSD 专用的 so 文件 glob */
#if defined TARGETOS_OpenBSD && !defined _WIN32
static int tcc_glob_so(TCCState *s1, const char *pattern, char *buf, int size)
{
    /* 选择最新版本的 libxxx.so.x.y */
    glob_t g;
    /* ... glob 匹配并选择最大版本号 ... */
}
#endif
```

---

## 8.7 构建交叉编译器

交叉编译器是指在一种平台上编译出另一种平台可执行代码的编译器。TCC 的构建系统使得创建交叉编译器非常简单。

### 8.7.1 基本配置选项

```bash
# 构建面向 ARM64 Linux 的交叉编译器（在 x86-64 主机上）
./configure --targetarm64 --enable-cross \
    --cross-prefix=aarch64-linux-gnu-

# 构建面向 RISC-V 64 的交叉编译器
./configure --targetriscv64 --enable-cross \
    --cross-prefix=riscv64-linux-gnu-

# 构建面向 i386（32 位 x86）的交叉编译器
./configure --targeti386 --enable-cross
```

`--enable-cross` 选项告诉构建系统：
1. 不编译 `tccrun.c` 中的本机运行代码（因为目标架构不是主机架构）
2. 使用 `CONFIG_TCC_CROSSPREFIX` 前缀来区分运行时文件
3. 不设置 `TCC_IS_NATIVE`，禁用 `-run` 功能

### 8.7.2 交叉编译的使用

```bash
# 使用 ARM64 交叉编译器
aarch64-tcc -o program program.c

# 使用交叉编译器生成目标文件
aarch64-tcc -c -o program.o program.c

# 指定目标库路径
aarch64-tcc -B/path/to/arm64/sysroot/usr/lib -o program program.c
```

### 8.7.3 多架构支持

TCC 可以在同一个构建中支持多个目标架构。通过 `TCC_TARGET_I386` 和 `TCC_TARGET_X86_64` 同时定义，可以构建支持 32/64 位切换的编译器（通过 `-m32`/`-m64` 选项）：

```c
/* tcc.c 中的多架构切换逻辑 */
#ifdef TCC_TARGET_I386
# ifdef TCC_TARGET_X86_64
    if (m32_flag)
        s->seg_size = 32;  /* 32 位模式 */
# endif
#endif
```

---

## 8.8 内置汇编器

TCC 内置了一个 GAS（GNU Assembler）兼容的汇编器，实现在 `tccasm.c` 中。它支持独立的 `.s`/`.S` 汇编文件以及 C 代码中的内联汇编。

### 8.8.1 汇编器架构

汇编器的核心流程：

```
源文件（.s 或 .S）
  → 预处理器（.S 文件需要预处理）
  → 词法分析器（tccpp.c）
  → 汇编指令解析器（tccasm.c）
  → 指令编码器（<target>-asm.c）
  → 目标代码生成（段数据）
```

### 8.8.2 支持的伪指令

TCC 的汇编器支持标准 GAS 伪指令：

| 伪指令 | 功能 |
|--------|------|
| `.text` / `.data` / `.bss` / `.rodata` | 段切换 |
| `.section name` | 指定自定义段 |
| `.global symbol` / `.globl symbol` | 声明全局符号 |
| `.type symbol, type` | 设置符号类型（function/object） |
| `.size symbol, expr` | 设置符号大小 |
| `.byte` / `.word` / `.long` / `.quad` | 数据定义 |
| `.string` / `.asciz` / `.ascii` | 字符串定义 |
| `.align expr` | 对齐 |
| `.skip size` | 跳过指定字节 |
| `.ident` | 版本标识（被忽略） |
| `.file` / `.loc` | 调试位置信息 |
| `.pushsection` / `.popsection` | 段栈操作 |
| `.previous` | 切换到上一个段 |
| `.incbin file` | 包含二进制文件 |

### 8.8.3 标签处理

汇编器需要处理三种标签：

1. **全局标签**：由 `.global` 声明，在链接时可见
2. **本地标签**：不带 `.global` 的标签，仅在当前编译单元可见
3. **数字标签**：如 `1:`、`2:`，通过 `1f`（forward）和 `1b`（backward）引用

```c
/* tccasm.c - 标签处理 */
static Sym *asm_new_label(TCCState *s1, int label, int is_local)
{
    Sym *sym;
    /* 对于全局标签，在全局符号表中查找或创建 */
    /* 对于本地标签，使用特殊的 L..N 前缀避免冲突 */
    sym = asm_label_push(label);
    /* ... */
    return sym;
}

ST_FUNC int asm_get_local_label_name(TCCState *s1, unsigned int n)
{
    /* 数字标签转为 L..N 形式 */
    char buf[64];
    snprintf(buf, sizeof(buf), "L..%u", n);
    return tok_alloc_const(buf);
}
```

### 8.8.4 表达式求值

汇编器中的地址表达式（如 `symbol + offset`）由 `asm_expr()` 处理。它支持：

- 算术运算：`+`、`-`、`*`、`/`
- 位运算：`&`、`|`、`^`、`~`
- 特殊符号：`.`（当前位置）
- 外部符号引用
- PC 相对表达式

```c
/* tccasm.c - 表达式值结构 */
typedef struct ExprValue {
    uint64_t v;   /* 常量值 */
    Sym *sym;     /* 关联的符号（如果有的话） */
    int pcrel;    /* 是否 PC 相对 */
} ExprValue;
```

---

## 8.9 内联汇编 asm()

TCC 支持 GCC 风格的内联汇编语法，允许在 C 函数中嵌入汇编代码：

```c
asm("汇编模板" : 输出操作数 : 输入操作数 : 破坏的寄存器);
```

### 8.9.1 语法支持

TCC 的内联汇编支持以下特性：

```c
/* 基本内联汇编 */
asm("nop");

/* 带操作数的内联汇编 */
int a = 10, b;
asm("mov %1, %0" : "=r"(b) : "r"(a));

/* 带约束的内联汇编 */
asm volatile(
    "lock add %1, %0"
    : "+m"(*addr)
    : "r"(value)
    : "memory"
);

/* 扩展 asm goto */
asm goto("test %0, %0\n\t"
         "jz %l[label]"
         : : "r"(val) : : label);
```

### 8.9.2 约束处理

约束字符串告诉编译器如何分配操作数。TCC 支持的约束包括：

| 约束 | 含义 |
|------|------|
| `r` | 通用寄存器 |
| `m` | 内存操作数 |
| `i` | 立即数 |
| `g` | 通用（寄存器/内存/立即数） |
| `a`/`b`/`c`/`d` | 特定寄存器（x86: EAX/EBX/ECX/EDX） |
| `S`/`D` | ESI/EDI（x86） |
| `f` | 浮点寄存器 |
| `0`-`9` | 匹配第 N 个操作数的约束 |
| `=` | 只写（输出） |
| `+` | 读写（输入输出） |
| `&` | 早期破坏（early clobber） |
| `~{memory}` | 破坏内存 |
| `~{cc}` | 破坏条件码 |

约束解析在 `asm_compute_constraints()` 中完成：

```c
/* tccasm.c 或 <target>-asm.c */
static void asm_compute_constraints(ASMOperand *operands,
    int nb_operands, int nb_outputs, int *pout_reg)
{
    /* 1. 分配优先级（输出约束优先于输入约束）
     * 2. 处理匹配约束（如 "0" 表示与第 0 个操作数使用同一寄存器）
     * 3. 为每个操作数分配寄存器或标记为内存操作数
     * 4. 处理早期破坏标记 */
}
```

### 8.9.3 操作数替换

`subst_asm_operands()` 将汇编模板中的 `%0`、`%1` 等引用替换为实际的寄存器名或内存引用：

```c
/* <target>-asm.c */
static void subst_asm_operands(ASMOperand *operands,
    int nb_operands, CString *out_str, CString *in_str)
{
    /* 解析汇编模板字符串 */
    /* 将 %N 替换为对应的操作数字符串 */
    /* %% 替换为 %（转义） */
    /* %cN 替换为常量值（不带 $ 前缀） */
    /* %pN 与 %N 类似但用于特定场景 */
}
```

例如，如果操作数约束是 `=r`(out) 和 `r`(in)，且 out 分配到 `%eax`，in 分配到 `%ecx`，则模板 `mov %1, %0` 会被替换为 `mov %ecx, %eax`。

### 8.9.4 各平台差异

不同架构的内联汇编有一些差异：

**x86/x86-64**：操作数从 0 开始编号，使用 `AT&T` 语法（源在前，目标在后）或 Intel 语法。

**ARM/ARM64**：操作数使用 `%0`、`%1` 引用，支持 ARM 特有的约束如 `l`（低寄存器）。

**RISC-V**：操作数使用 `%0`、`%1` 引用，约束相对简单。

---

## 8.10 本章小结与练习

### 小结

本章深入分析了 TinyCC 的跨平台架构：

1. **后端抽象层**：通过 `TARGET_DEFS_ONLY` 机制和一组约定好的函数接口，实现了前端与后端的干净分离。每个目标架构必须实现代码生成、链接和汇编三套接口。

2. **具体平台实现**：详细分析了 x86-64（System V ABI / Windows x64）、ARM64（AAPCS64）、ARM 32（EABI）和 RISC-V 64（LP64D）的寄存器分配、调用约定和指令编码。

3. **OS 支持**：TCC 支持 Linux（ELF）、Windows（PE）、macOS（Mach-O）和多种 BSD 系统，每种 OS 有自己的可执行格式和动态链接机制。

4. **交叉编译**：通过 `--enable-cross` 配置选项可以轻松构建面向任何支持架构的交叉编译器。

5. **内置汇编器**：TCC 内置了一个 GAS 兼容的汇编器，支持独立的汇编文件和 GCC 风格的内联汇编。

### 练习 1：交叉编译实践

使用 TCC 的交叉编译功能完成以下任务：

1. 在 x86-64 主机上构建一个面向 ARM64 的交叉编译器
2. 用交叉编译器编译一个简单的 C 程序
3. 用 `readelf` 或 `objdump` 检查生成的 ELF 文件，验证架构和指令集
4. 比较 TCC 和 GCC 交叉编译器生成代码的差异

### 练习 2：内联汇编实验

编写一个 C 程序，使用内联汇编实现以下功能：

1. **CPUID 查询**（x86-64）：使用 `cpuid` 指令获取 CPU 厂商字符串
2. **原子操作**：使用 `lock cmpxchg` 实现一个无锁自旋锁
3. **系统寄存器读取**（ARM64）：使用 `mrs` 指令读取 `CTR_EL0` 寄存器
4. **计时器读取**（RISC-V）：使用 `rdtime` 指令读取周期计数器

要求：为每个函数编写 `#if defined(__x86_64__)` / `#elif defined(__aarch64__)` 等条件编译，使其能在多个平台上编译。


===== FILE: docs/ch09/examples/debug_test.c =====
/*
 * debug_test.c - A C file designed for debugging with GDB
 *
 * Compile with:
 *   tcc -g -o debug_test debug_test.c      (STAB format)
 *   tcc -gdwarf -o debug_test debug_test.c  (DWARF format)
 *
 * Debug with:
 *   gdb ./debug_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== Data structures for testing ========== */

typedef struct point {
    double x;
    double y;
} point_t;

typedef struct rectangle {
    point_t origin;
    double width;
    double height;
    char name[32];
} rect_t;

enum color { RED = 0, GREEN = 1, BLUE = 2, WHITE = 7, BLACK = 8 };

typedef struct {
    int id;
    enum color fill;
    enum color stroke;
    rect_t bounds;
} shape_t;

/* ========== Functions for testing step/next ========== */

double point_distance(const point_t *a, const point_t *b)
{
    double dx = b->x - a->x;
    double dy = b->y - a->y;
    return dx * dx + dy * dy;  /* Note: squared distance */
}

double rect_area(const rect_t *r)
{
    return r->width * r->height;
}

double rect_perimeter(const rect_t *r)
{
    return 2.0 * (r->width + r->height);
}

int point_in_rect(const point_t *p, const rect_t *r)
{
    return (p->x >= r->origin.x &&
            p->x <= r->origin.x + r->width &&
            p->y >= r->origin.y &&
            p->y <= r->origin.y + r->height);
}

/* ========== Recursive function for testing backtrace ========== */

int fibonacci(int n)
{
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

/* ========== Loop testing ========== */

int sum_array(const int *arr, int len)
{
    int sum = 0;
    int i;
    for (i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

/* ========== Pointer and array testing ========== */

void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void bubble_sort(int arr[], int n)
{
    int i, j;
    for (i = 0; i < n - 1; i++) {
        for (j = 0; j < n - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                swap(&arr[j], &arr[j + 1]);
            }
        }
    }
}

/* ========== String operations ========== */

char *build_greeting(const char *name, int count)
{
    /* Dynamic allocation - test with 'print' in GDB */
    size_t len = strlen("Hello, ") + strlen(name) + 20;
    char *buf = malloc(len);
    if (!buf) return NULL;
    snprintf(buf, len, "Hello, %s! (visit #%d)", name, count);
    return buf;
}

/* ========== Main: exercise all the above ========== */

int main(void)
{
    /* Local variables of various types for debugging tests */
    int i, n;
    double result;
    char *msg;

    /* Point and rectangle */
    point_t p1 = {1.0, 2.0};
    point_t p2 = {4.0, 6.0};
    rect_t rect;
    shape_t shape;

    /* Array */
    int numbers[] = {64, 34, 25, 12, 22, 11, 90};
    int num_count = sizeof(numbers) / sizeof(numbers[0]);

    /* Set up the rectangle */
    rect.origin.x = 0.0;
    rect.origin.y = 0.0;
    rect.width = 10.0;
    rect.height = 5.0;
    strncpy(rect.name, "TestRect", sizeof(rect.name) - 1);
    rect.name[sizeof(rect.name) - 1] = '\0';

    /* Set up the shape */
    shape.id = 1;
    shape.fill = BLUE;
    shape.stroke = BLACK;
    shape.bounds = rect;

    /* Test 1: Basic arithmetic and locals */
    printf("=== Test 1: Basic Locals ===\n");
    n = 10;
    result = 0.0;
    for (i = 0; i < n; i++) {
        result += i * 0.5;
    }
    printf("Sum of 0..%d * 0.5 = %.2f\n", n, result);
    /* GDB: break here, print n, result, i */

    /* Test 2: Struct and pointer operations */
    printf("\n=== Test 2: Structs and Pointers ===\n");
    double dist = point_distance(&p1, &p2);
    printf("Distance(p1, p2) = %.4f\n", dist);

    double area = rect_area(&rect);
    printf("Rectangle '%s': area=%.2f, perimeter=%.2f\n",
           rect.name, area, rect_perimeter(&rect));

    printf("Shape %d: fill=%d, stroke=%d\n",
           shape.id, shape.fill, shape.stroke);
    /* GDB: print p1, p2, rect, shape */

    /* Test 3: Point-in-rectangle test */
    printf("\n=== Test 3: Point In Rectangle ===\n");
    point_t test_points[] = {{5, 3}, {-1, 0}, {11, 6}, {0, 0}};
    for (i = 0; i < 4; i++) {
        int inside = point_in_rect(&test_points[i], &rect);
        printf("  (%.0f, %.0f) in rect: %s\n",
               test_points[i].x, test_points[i].y,
               inside ? "YES" : "NO");
    }

    /* Test 4: Array and sorting */
    printf("\n=== Test 4: Array Operations ===\n");
    printf("Before sort: ");
    for (i = 0; i < num_count; i++) printf("%d ", numbers[i]);
    printf("\n");

    bubble_sort(numbers, num_count);

    printf("After sort:  ");
    for (i = 0; i < num_count; i++) printf("%d ", numbers[i]);
    printf("\n");

    int total = sum_array(numbers, num_count);
    printf("Sum = %d\n", total);
    /* GDB: break bubble_sort, step through, watch arr[] */

    /* Test 5: Recursion */
    printf("\n=== Test 5: Recursion ===\n");
    for (i = 0; i <= 15; i++)
        printf("fib(%d) = %d\n", i, fibonacci(i));
    /* GDB: break fibonacci, bt to see call stack */

    /* Test 6: Dynamic memory */
    printf("\n=== Test 6: Dynamic Memory ===\n");
    for (i = 1; i <= 3; i++) {
        msg = build_greeting("World", i);
        printf("  %s\n", msg);
        free(msg);
    }
    /* GDB: break build_greeting, print buf content after snprintf */

    /* Test 7: Enum and switch */
    printf("\n=== Test 7: Enum and Switch ===\n");
    enum color colors[] = {RED, GREEN, BLUE, WHITE, BLACK};
    for (i = 0; i < 5; i++) {
        const char *name;
        switch (colors[i]) {
        case RED:   name = "Red";   break;
        case GREEN: name = "Green"; break;
        case BLUE:  name = "Blue";  break;
        case WHITE: name = "White"; break;
        case BLACK: name = "Black"; break;
        default:    name = "Unknown"; break;
        }
        printf("  Color %d: %s\n", colors[i], name);
    }
    /* GDB: print colors, print name */

    printf("\n=== All tests passed ===\n");
    return 0;
}


===== FILE: docs/ch09/exercises/ex1_debug.md =====
# 练习 1: 调试信息格式对比实验

## 背景

TCC 支持两种调试信息格式：STAB 和 DWARF。本练习要求你比较这两种格式的差异。

## 任务

### 任务 1: 生成并检查调试段

1. 使用以下命令分别生成 STAB 和 DWARF 格式的调试信息：
   ```bash
   tcc -g -c -o debug_stab.o debug_test.c
   tcc -gdwarf -c -o debug_dwarf.o debug_test.c
   ```

2. 使用 `readelf -S` 查看两种格式生成的段：
   ```bash
   readelf -S debug_stab.o   # 查看 .stab 和 .stabstr
   readelf -S debug_dwarf.o  # 查看 .debug_* 段
   ```

3. 使用 `size` 命令比较两种格式的大小：
   ```bash
   size debug_stab.o debug_dwarf.o
   ```

### 任务 2: 解析调试信息

1. 使用 `objdump` 查看 STAB 信息：
   ```bash
   objdump --stabs debug_stab.o
   ```

2. 使用 `objdump` 查看 DWARF 信息：
   ```bash
   objdump --dwarf=info debug_dwarf.o
   objdump --dwarf=line debug_dwarf.o
   objdump --dwarf=abbrev debug_dwarf.o
   ```

3. 分析：
   - STAB 中的类型编码字符串（如 `int:t1=r1;-2147483648;2147483647;`）
   - DWARF 中的 abbreviation 表和 DIE 树
   - 行号信息的编码方式

### 任务 3: GDB 调试对比

1. 分别用两种格式编译并用 GDB 调试
2. 测试以下操作在两种格式下的表现：
   - `list` 命令（源码显示）
   - `print variable`（变量打印）
   - `print struct_var`（结构体打印）
   - `info locals`（局部变量）
   - `backtrace`（调用栈）

## 验证标准

- 能正确识别两种格式对应的段名
- 能解释 STAB 类型编码字符串的含义
- 能解释 DWARF abbreviation 和 DIE 的结构
- 理解两种格式在空间效率和调试器兼容性方面的权衡


===== FILE: docs/ch09/exercises/ex2_test.md =====
# 练习 2: 编写和运行 TCC 测试

## 背景

TCC 的测试套件是保证编译器质量的关键基础设施。本练习要求你编写新测试并理解测试流程。

## 任务

### 任务 1: 为 tests2/ 添加新测试

在 `tests/tests2/` 目录下添加一个新的测试文件，覆盖以下特性之一（选择一个）：

**选项 A: 复合字面量 (Compound Literals)**
```c
#include <stdio.h>

int main(void) {
    int *p = (int[]){1, 2, 3, 4, 5};
    int i;
    for (i = 0; i < 5; i++)
        printf("%d ", p[i]);
    printf("\n");

    struct { int x; int y; } *ps = &(struct { int x; int y; }){10, 20};
    printf("%d %d\n", ps->x, ps->y);

    return 0;
}
```

**选项 B: 指定初始化器 (Designated Initializers)**
```c
#include <stdio.h>

int main(void) {
    int arr[10] = {[0] = 1, [5] = 6, [9] = 10};
    int i;
    for (i = 0; i < 10; i++)
        printf("%d ", arr[i]);
    printf("\n");

    struct { int a; int b; int c; } s = {.c = 30, .a = 10};
    printf("%d %d %d\n", s.a, s.b, s.c);

    return 0;
}
```

**选项 C: _Static_assert**
```c
#include <stdio.h>

_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
_Static_assert(sizeof(void*) >= sizeof(int), "pointer must be >= int");

struct header {
    int magic;
    int version;
    int length;
};
_Static_assert(sizeof(struct header) == 12, "header must be 12 bytes");

int main(void) {
    printf("All static assertions passed\n");
    printf("sizeof(int) = %zu\n", sizeof(int));
    printf("sizeof(void*) = %zu\n", sizeof(void*));
    printf("sizeof(struct header) = %zu\n", sizeof(struct header));
    return 0;
}
```

### 步骤

1. 创建测试文件（选择下一个可用编号）
2. 使用参考编译器生成 `.expect` 文件：
   ```bash
   gcc -o test XX_name.c && ./test > XX_name.expect
   ```
3. 使用 TCC 编译并验证输出匹配：
   ```bash
   tcc -o test XX_name.c && ./test | diff - XX_name.expect
   ```
4. 如果有差异，分析是 TCC 的 bug 还是可接受的行为差异

### 任务 2: 运行完整测试套件

1. 构建 TCC（如果还没有构建）
2. 运行完整测试套件：
   ```bash
   cd tests && make test
   ```
3. 记录测试结果：通过了多少，失败了多少
4. 对于失败的测试，分析失败原因

### 任务 3: 边界条件测试

编写一个测试，专门测试整数溢出和边界条件：

```c
#include <stdio.h>
#include <limits.h>

int main(void) {
    /* 整数溢出 */
    int max = INT_MAX;
    printf("INT_MAX = %d\n", max);
    printf("INT_MAX + 1 = %d\n", max + 1);
    printf("INT_MIN = %d\n", INT_MIN);
    printf("INT_MIN - 1 = %d\n", INT_MIN - 1);

    /* 无符号溢出 */
    unsigned umax = UINT_MAX;
    printf("UINT_MAX = %u\n", umax);
    printf("UINT_MAX + 1 = %u\n", umax + 1);

    /* 除法边界 */
    printf("-1 / 2 = %d\n", -1 / 2);
    printf("-1 %% 2 = %d\n", -1 % 2);
    printf("1 / -2 = %d\n", 1 / -2);

    return 0;
}
```

## 验证标准

- 添加的测试文件格式正确（有 `.c` 和 `.expect` 文件）
- TCC 输出与预期输出完全一致
- 能解释失败测试的原因
- 理解了 TCC 测试套件的组织结构和运行方式


===== FILE: docs/ch09/index.md =====
# 第九章 调试与测试

一个编译器的正确性至关重要——一个错误的代码生成可能会导致程序在运行时产生难以追踪的 bug。TinyCC 通过两套机制来保证质量：调试信息生成（使得用户可以用 GDB 等调试器调试 TCC 编译的程序）和全面的测试套件（覆盖语言特性的方方面面）。本章将深入分析 TCC 的调试信息生成机制和测试基础设施。

---

## 9.1 STAB 调试格式

STAB（Symbol Table Debugger）是一种较老的调试信息格式，最初由 BSD 系统引入。TCC 历史上首先支持的就是 STAB 格式，这也是默认的调试格式（除非使用 `-gdwarf` 选项）。

### 9.1.1 STAB 基础

STAB 信息存储在 ELF 文件的 `.stab` 和 `.stabstr` 段中。每条 STAB 记录（`Stab_Sym` 结构）包含：

```c
/* stab.h */
typedef struct {
    unsigned long n_strx;    /* 字符串表偏移 */
    unsigned char n_type;    /* 符号类型 */
    unsigned char n_other;   /* 杂项信息 */
    unsigned short n_desc;   /* 描述字段 */
    unsigned long n_value;   /* 值（地址或常量） */
} Stab_Sym;
```

主要的 STAB 类型：

| 类型 | 含义 |
|------|------|
| `N_SLINE` (0x44) | 源代码行号映射 |
| `N_SO` (0x64) | 源文件名 |
| `N_FUN` (0x24) | 函数定义 |
| `N_LSYM` (0x80) | 局部类型定义 |
| `N_GSYM` (0x20) | 全局符号 |
| `N_PSYM` (0xa0) | 函数参数 |
| `N_LBRAC` (0xc0) | 左花括号（块开始） |
| `N_RBRAC` (0xe0) | 右花括号（块结束） |

### 9.1.2 类型编码

STAB 使用字符串来编码类型信息，遵循 GCC 的 stabstring 格式。TCC 在 `tccdbg.c` 中通过 `default_debug` 数组预定义了基本类型：

```c
/* tccdbg.c - default_debug 数组 */
static const struct {
    int type;
    int size;
    int encoding;
    const char *name;
} default_debug[] = {
    { VT_INT,  4, DW_ATE_signed,
      "int:t1=r1;-2147483648;2147483647;" },
    { VT_BYTE, 1, DW_ATE_signed_char,
      "char:t2=r2;0;127;" },
    { VT_LLONG | VT_LONG, 8, DW_ATE_signed,
      "long int:t3=r3;-9223372036854775808;9223372036854775807;" },
    { VT_INT | VT_UNSIGNED, 4, DW_ATE_unsigned,
      "unsigned int:t4=r4;0;037777777777;" },
    /* ... 更多类型 ... */
    { VT_FLOAT, 4, DW_ATE_float,
      "float:t14=r1;4;0;" },
    { VT_DOUBLE, 8, DW_ATE_float,
      "double:t15=r1;8;0;" },
    { VT_LDOUBLE, 16, DW_ATE_float,
      "long double:t16=r1;16;0;" },
    { VT_BOOL, 1, DW_ATE_boolean,
      "bool:t26=r26;0;255;" },
    { VT_VOID, 1, DW_ATE_unsigned_char,
      "void:t27=27" },
};
```

STAB 类型编码语法：
- `name:tN`：定义类型编号 N，名称为 name
- `rN;low;high;`：范围类型（整数），类型 N，范围 [low, high]
- `*T`：指向类型 T 的指针
- `arT;low;high;element_type`：数组类型
- `sN`：结构体，大小 N 字节
- `uN`：联合体，大小 N 字节

### 9.1.3 STAB 的局限

STAB 格式有几个显著局限：
- 不支持类型间的引用关系（如递归结构体）
- 字符串编码空间效率低
- 不支持复杂的类型操作（如模板、命名空间）
- 大多数现代调试器更偏好 DWARF

因此，STAB 主要用于兼容性场景和简单的调试需求。

---

## 9.2 DWARF 调试格式

DWARF 是现代 Unix/Linux 系统的标准调试信息格式。TCC 从较新版本开始支持 DWARF（通过 `-gdwarf` 选项或在某些平台上默认启用）。

### 9.2.1 DWARF 段结构

TCC 生成以下 DWARF 段：

| 段名 | 内容 |
|------|------|
| `.debug_info` | 类型信息、变量、函数 |
| `.debug_abbrev` | abbreviation 表（DIE 格式定义） |
| `.debug_line` | 行号信息（源码到机器码映射） |
| `.debug_str` | 字符串表 |
| `.debug_line_str` | 行号段专用字符串表 |
| `.debug_aranges` | 地址范围表（加速查找） |

### 9.2.2 Abbreviation 表

DWARF 的 abbreviation 表定义了调试信息条目（DIE，Debug Information Entry）的格式。TCC 在 `tccdbg.c` 中静态定义了所有需要的 abbreviation：

```c
/* tccdbg.c - DWARF abbreviation 定义 */
#define DWARF_ABBREV_COMPILE_UNIT       1
#define DWARF_ABBREV_BASE_TYPE          2
#define DWARF_ABBREV_VARIABLE_EXTERNAL  3
#define DWARF_ABBREV_VARIABLE_STATIC    4
#define DWARF_ABBREV_VARIABLE_LOCAL     5
#define DWARF_ABBREV_FORMAL_PARAMETER   6
#define DWARF_ABBREV_POINTER            7
#define DWARF_ABBREV_ARRAY_TYPE         8
#define DWARF_ABBREV_SUBRANGE_TYPE      9
#define DWARF_ABBREV_TYPEDEF           10
#define DWARF_ABBREV_ENUMERATOR_SIGNED  11
#define DWARF_ABBREV_ENUMERATION_TYPE   13
#define DWARF_ABBREV_MEMBER            14
#define DWARF_ABBREV_MEMBER_BF         15
#define DWARF_ABBREV_STRUCTURE_TYPE     16
#define DWARF_ABBREV_UNION_TYPE        18
#define DWARF_ABBREV_SUBPROGRAM_EXTERNAL 20
#define DWARF_ABBREV_SUBPROGRAM_STATIC  21
#define DWARF_ABBREV_LEXICAL_BLOCK      22
#define DWARF_ABBREV_SUBROUTINE_TYPE    24
/* ... */
```

每个 abbreviation 条目定义了：
1. **标签**（如 `DW_TAG_compile_unit`）
2. **是否有子节点**
3. **属性列表**（`DW_AT_*` 类型和 `DW_FORM_*` 格式）

例如，编译单元的 abbreviation：

```c
/* tccdbg.c */
DWARF_ABBREV_COMPILE_UNIT, DW_TAG_compile_unit, 1,  /* 有子节点 */
    DW_AT_producer,  DW_FORM_strp,      /* 编译器名称 */
    DW_AT_language,  DW_FORM_data1,      /* 语言: C */
    DW_AT_name,      DW_FORM_line_strp,  /* 源文件名 */
    DW_AT_comp_dir,  DW_FORM_line_strp,  /* 编译目录 */
    DW_AT_low_pc,    DW_FORM_addr,       /* 代码起始地址 */
    DW_AT_high_pc,   DW_FORM_data8,      /* 代码大小 */
    DW_AT_stmt_list, DW_FORM_sec_offset, /* 行号段偏移 */
    0, 0,
```

### 9.2.3 行号状态机

DWARF 使用一个**行号状态机**来紧凑地编码源代码行号与机器码地址之间的映射。状态机的状态包括：

```c
/* DWARF 行号状态机状态 */
typedef struct {
    unsigned long address;   /* 当前指令地址 */
    unsigned int file;       /* 源文件编号 */
    unsigned int line;       /* 当前行号 */
    unsigned int column;     /* 当前列号 */
    unsigned int is_stmt;    /* 是否为语句开始 */
    unsigned int end_sequence; /* 序列结束标志 */
} dwarf_line_state;
```

TCC 在 `tccdbg.c` 中定义了行号状态机的参数：

```c
/* tccdbg.c */
#define DWARF_LINE_BASE    -5    /* line_increment 的最小值 */
#define DWARF_LINE_RANGE   14    /* line_increment 的范围 */
#define DWARF_OPCODE_BASE  13    /* 第一个特殊操作码 */

#if defined TCC_TARGET_ARM64
#define DWARF_MIN_INSTR_LEN  4   /* ARM64: 4 字节指令 */
#elif defined TCC_TARGET_ARM
#define DWARF_MIN_INSTR_LEN  2   /* ARM: 2 字节指令 */
#else
#define DWARF_MIN_INSTR_LEN  1   /* x86: 1 字节指令 */
#endif
```

行号状态机的操作码分为三类：

1. **标准操作码**（1-12）：
   - `DW_LNS_copy` (1)：发出当前行号记录
   - `DW_LNS_advance_pc` (2)：推进地址
   - `DW_LNS_advance_line` (3)：推进行号
   - `DW_LNS_set_file` (4)：设置文件编号
   - `DW_LNS_set_column` (5)：设置列号
   - `DW_LNS_negate_stmt` (10)：翻转 is_stmt
   - `DW_LNS_set_basic_block` (11)：标记基本块开始

2. **特殊操作码**（13-255）：
   - 编码为 `opcode = opcode_base + (line_increment - line_base) + line_range * address_increment`
   - 一条特殊操作码同时推进地址和行号

3. **扩展操作码**（opcode=0）：
   - `DW_LNE_end_sequence`：标记序列结束
   - `DW_LNE_set_address`：设置绝对地址
   - `DW_LNE_define_file`：定义文件

### 9.2.4 类型 DIE

DWARF 使用 DIE（Debug Information Entry）树来描述类型信息。TCC 为每种 C 类型生成对应的 DIE：

**基本类型**（`DW_TAG_base_type`）：

```c
/* DWARF_ABBREV_BASE_TYPE */
/* 属性: byte_size, encoding, name */
```

编码值（`DW_ATE_*`）：

| 编码 | 含义 |
|------|------|
| `DW_ATE_signed` (5) | 有符号整数 |
| `DW_ATE_unsigned` (7) | 无符号整数 |
| `DW_ATE_signed_char` (6) | 有符号字符 |
| `DW_ATE_unsigned_char` (8) | 无符号字符 |
| `DW_ATE_float` (4) | 浮点数 |
| `DW_ATE_boolean` (2) | 布尔值 |

**结构体类型**（`DW_TAG_structure_type`）：

```c
/* DWARF_ABBREV_STRUCTURE_TYPE */
/* 有子节点 */
/* 属性: name, byte_size, decl_file, decl_line, sibling */
/* 子节点: member (DW_TAG_member) */
/*   属性: name, decl_file, decl_line, type, data_member_location */
```

**指针类型**（`DW_TAG_pointer_type`）：

```c
/* DWARF_ABBREV_POINTER */
/* 属性: byte_size, type */
```

**函数类型**（`DW_TAG_subroutine_type`）：

```c
/* DWARF_ABBREV_SUBROUTINE_TYPE */
/* 有子节点 */
/* 属性: type (返回类型), sibling */
/* 子节点: formal_parameter (DW_TAG_formal_parameter) */
```

---

## 9.3 调试信息生成时机

调试信息的生成贯穿 TCC 编译的各个阶段。`tccdbg.c` 提供了一组函数来协调这一过程：

### 9.3.1 tcc_debug_start

在编译一个文件开始时调用，初始化调试段：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_start(TCCState *s1)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 初始化 DWARF 段 */
            s1->dwarf_info_section = find_section(s1, ".debug_info");
            s1->dwarf_abbrev_section = find_section(s1, ".debug_abbrev");
            s1->dwarf_line_section = find_section(s1, ".debug_line");
            s1->dwarf_str_section = find_section(s1, ".debug_str");
            /* 写入 abbreviation 表 */
            put_abbrevs(s1);
            /* 初始化行号状态机 */
            init_line_state(s1);
        } else {
            /* 初始化 STAB 段 */
            s1->stab_section = find_section(s1, ".stab");
            /* 写入默认类型信息 */
            put_stabs(s1, default_debug);
        }
    }
}
```

### 9.3.2 tcc_debug_line

每当编译器处理到一个新的源代码行时调用，更新行号映射：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_line(TCCState *s1)
{
    if (s1->do_debug && !nocode_wanted) {
        /* 获取当前源文件和行号 */
        int file = get_debug_file_index(file->filename);
        int line = file->line_num;

        if (s1->dwarf) {
            /* 使用行号状态机编码 */
            emit_dwarf_line(s1, file, line, ind);
        } else {
            /* STAB: 直接写入 N_SLINE 记录 */
            put_stabn(s1, N_SLINE, line, ind - cur_text_section->sh_addr);
        }
    }
}
```

### 9.3.3 tcc_debug_funcstart / tcc_debug_funcend

在函数编译开始和结束时调用，生成函数范围和局部变量信息：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_funcstart(TCCState *s1, Sym *sym)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 写入 DW_TAG_subprogram DIE */
            begin_dwarf_func(s1, sym);
        } else {
            /* STAB: 写入 N_FUN 记录 */
            put_stabs_r(s1, sym->v, N_FUN, 0, 0, ind, sym->type.t);
        }
    }
}

ST_FUNC void tcc_debug_funcend(TCCState *s1, int size)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 结束 DW_TAG_subprogram DIE */
            end_dwarf_func(s1, size);
        } else {
            /* STAB: 写入 N_FUN 结束记录 */
            put_stabn(s1, N_FUN, 0, size, "");
        }
    }
}
```

### 9.3.4 块作用域

进入和离开代码块（`{`...`}`）时，调试器需要知道变量的作用域范围：

```c
/* STAB 模式 */
/* N_LBRAC: 块开始 */
/* N_RBRAC: 块结束 */

/* DWARF 模式 */
/* DW_TAG_lexical_block DIE */
/* 包含 low_pc 和 high_pc 属性 */
```

---

## 9.4 tccdbg.c 内部结构

### 9.4.1 _tccdbg 状态结构

TCC 的调试信息生成器维护一个独立的状态结构 `_tccdbg`，通过 `TCCState->dState` 指针访问：

```c
/* tccdbg.c */
struct _tccdbg {
    /* DWARF 状态 */
    struct {
        int *hash;        /* 调试信息哈希表（用于类型去重） */
        int nb_hash;
        /* 字符串池 */
        CString debug_str;
        CString debug_line_str;
        /* 当前 DIE 的子节点计数 */
        int dwarf_info_child_count;
        /* 行号状态机当前状态 */
        unsigned int dwarf_line_state[/*...*/];
    } dw;

    /* STAB 状态 */
    struct {
        /* 类型编号映射 */
        int *type_offsets;
        int nb_types;
    } stab;

    /* 通用 */
    int last_line_num;     /* 上一次发出的行号 */
    int last_file_num;     /* 上一次发出的文件编号 */
};
```

### 9.4.2 调试哈希表

DWARF 要求相同的类型只出现一次。TCC 使用哈希表来检测重复类型：

```c
/* tccdbg.c */
static int debug_type_hash(Sym *s)
{
    /* 基于类型的 hash 值 */
    int h = s->type.t;
    if (s->type.ref)
        h += (uintptr_t)s->type.ref;
    return h & (s1->dState->dw.nb_hash - 1);
}

static int debug_find_type(Sym *s)
{
    int h = debug_type_hash(s);
    int *ph = &s1->dState->dw.hash[h];
    /* 在哈希链中查找匹配的类型 */
    /* ... */
}
```

### 9.4.3 DWARF 字符串池

DWARF 使用两种字符串表：
- `.debug_str`：通过 `DW_FORM_strp` 引用，可以被多个段共享
- `.debug_line_str`：通过 `DW_FORM_line_strp` 引用，行号段专用

TCC 在 `_tccdbg` 结构中维护两个 `CString` 来累积这些字符串。

### 9.4.4 STAB 的 N_DEFAULT_DEBUG

STAB 模式下，`default_debug` 数组中的所有基本类型在文件编译开始时就被写入 `.stab` 段。这确保了类型编号 1-29 始终可用：

```c
/* tccdbg.c */
#define N_DEFAULT_DEBUG (sizeof(default_debug) / sizeof(default_debug[0]))
/* N_DEFAULT_DEBUG ≈ 29 个基本类型 */
```

复杂类型（结构体、指针、数组等）在遇到时按需生成，并分配更高的类型编号。

---

## 9.5 使用 GDB 调试 TCC 编译的程序

### 9.5.1 基本流程

```bash
# 步骤 1: 使用 -g 选项编译
tcc -g -o program program.c

# 步骤 2: 使用 GDB 调试
gdb ./program
```

在 GDB 中可以使用标准的调试命令：

```
(gdb) break main          # 设置断点
(gdb) run                 # 运行程序
(gdb) list                # 查看源代码
(gdb) print variable      # 打印变量值
(gdb) step                # 单步执行（进入函数）
(gdb) next                # 单步执行（不进入函数）
(gdb) backtrace           # 查看调用栈
(gdb) info locals         # 查看局部变量
```

### 9.5.2 DWARF 模式调试

使用 DWARF 格式可以获得更好的调试体验：

```bash
tcc -gdwarf -o program program.c
gdb ./program
```

DWARF 提供了更精确的类型信息和更高效的行号查找。

### 9.5.3 调试 libtcc 编译的代码

当使用 libtcc API 时，内存中的代码不会有文件系统路径。GDB 可以通过以下方式调试：

```c
/* 方法 1: 使用 tcc_set_options 启用调试 */
tcc_set_options(s, "-g");
tcc_compile_string(s, source_code);
tcc_relocate(s);

/* 方法 2: 使用 #line 指令指定虚拟文件名 */
tcc_compile_string(s,
    "#line 1 \"script.c\"\n"
    "int main() { return 42; }\n");
```

对于 JIT 编译的代码，GDB 的 JIT 接口（`__jit_debug_register_code`）可以用来注册代码映射。TCC 的 `-run` 模式自动处理了运行时调试信息的注册。

### 9.5.4 运行时回溯

TCC 支持运行时栈回溯（通过 `-bt` 选项），即使没有 GDB 也能获得基本的错误定位：

```bash
tcc -bt -o program program.c
./program
# 如果程序崩溃，会显示类似:
# program.c:10: at main() Division by zero
```

回溯功能的实现定义在 `tccrun.c` 中，使用 `rt_context` 结构来跟踪调试信息：

```c
/* tccrun.c */
typedef struct rt_context {
    /* STAB 信息 */
    Stab_Sym *stab_sym, *stab_sym_end;
    char *stab_str;
    /* 或 DWARF 信息 */
    unsigned char *dwarf_line, *dwarf_line_end;
    unsigned char *dwarf_line_str;
    /* ELF 符号表 */
    ElfW(Sym) *esym_start, *esym_end;
    char *elf_str;
    /* 运行时状态 */
    addr_t prog_base;
    void *bounds_start;
    void *top_func;
    int num_callers;
    int dwarf;
} rt_context;
```

回溯函数通过检查栈帧链（frame pointer chain）和程序计数器（PC）值，利用 STAB 或 DWARF 信息将地址翻译为源文件名和行号。

---

## 9.6 TCC 测试套件

TCC 有全面的测试套件来验证编译器的正确性。测试文件位于 `tests/` 目录下。

### 9.6.1 测试结构概览

```
tests/
├── tcctest.c          # 主要的 C 语言特性测试（4500+ 行）
├── tcctest.h          # 测试辅助宏
├── boundtest.c        # 边界检查测试
├── libtcc_test.c      # libtcc API 测试
├── libtcc_test_mt.c   # libtcc 多线程测试
├── abitest.c          # ABI/调用约定测试
├── vla_test.c         # 变长数组测试
├── testfp.c           # 浮点测试
├── Makefile           # 测试构建脚本
├── pp/                # 预处理器测试
│   ├── 01_hash.c
│   ├── 02_hashif.c
│   └── ...
└── tests2/            # 扩展测试集
    ├── 00_assignment.c / .expect
    ├── 01_comment.c / .expect
    ├── 02_printf.c / .expect
    └── ...（约 130 个测试用例）
```

### 9.6.2 tcctest.c

`tcctest.c` 是 TCC 最核心的测试文件，涵盖了几乎所有 C 语言特性。它的测试策略是：编译时同时用参考编译器（如 GCC）和 TCC 编译，然后比较两个程序的输出。

测试覆盖的特性包括：

```c
/* tcctest.c 中测试的特性（摘选） */
void integer_ops(void)       /* 整数运算 */
void float_ops(void)         /* 浮点运算 */
void pointer_ops(void)       /* 指针操作 */
void struct_ops(void)        /* 结构体/联合体 */
void enum_ops(void)          /* 枚举 */
void array_ops(void)         /* 数组 */
void string_ops(void)        /* 字符串操作 */
void cast_ops(void)          /* 类型转换 */
void control_flow(void)      /* 控制流 */
void loop_ops(void)          /* 循环 */
void function_ops(void)      /* 函数调用 */
void varargs_ops(void)       /* 可变参数 */
void preprocessor_ops(void)  /* 预处理器 */
void bitfield_ops(void)      /* 位域 */
void special_ops(void)       /* 特殊操作（sizeof, typeof 等） */
```

### 9.6.3 tests2/ 扩展测试集

`tests2/` 目录包含约 130 个独立的测试文件，每个文件测试一个特定的 C 语言特性。每个测试文件都有一个对应的 `.expect` 文件，包含预期的输出。

命名约定：
```
XX_name.c       # 测试文件
XX_name.expect  # 预期输出
```

例如：
- `00_assignment.c`：赋值操作
- `01_comment.c`：注释处理
- `02_printf.c`：printf 格式化
- `03_struct.c`：结构体
- `124_atomic_counter.c`：原子操作
- `127_asm_goto.c`：asm goto
- `132_bound_test.c`：边界检查

### 9.6.4 预处理器测试

`tests/pp/` 目录专门测试预处理器的正确性：

```
tests/pp/
├── 01_hash.c          # 宏定义
├── 02_hashif.c        # #if 条件编译
├── 03_hashelif.c      # #elif
├── 04_*.c             # 更多预处理器特性
└── ...
```

### 9.6.5 测试执行

测试通过 `tests/Makefile` 执行：

```bash
cd tests
make test          # 运行所有测试
make test-tcc      # 仅运行 tcctest
make test2         # 运行 tests2 扩展测试
make test-pp       # 运行预处理器测试
make test-bound    # 运行边界检查测试
make test-asm      # 运行汇编器测试
```

典型的测试流程：

```bash
# 1. 用 TCC 编译并运行测试
tcc -o tcctest_tcc tcctest.c && ./tcctest_tcc > output_tcc

# 2. 用参考编译器编译并运行
gcc -o tcctest_gcc tcctest.c && ./tcctest_gcc > output_gcc

# 3. 比较输出
diff output_tcc output_gcc
```

### 9.6.6 交叉测试

TCC 还支持交叉测试——用一个平台的 TCC 编译面向另一个平台的测试程序：

```bash
# tests/Makefile 中的交叉测试目标
make test-arm      # ARM 交叉测试
make test-arm64    # ARM64 交叉测试
make test-riscv64  # RISC-V 交叉测试
```

---

## 9.7 添加新测试

### 9.7.1 为 tests2/ 添加测试

添加一个新的测试用例到 `tests2/` 非常简单：

**步骤 1**：创建测试文件，选择下一个可用的编号

```c
/* tests/tests2/135_my_feature.c */
#include <stdio.h>

int main(void)
{
    int x = 42;
    int *p = &x;
    
    /* 测试你的特性 */
    printf("x = %d\n", x);
    printf("*p = %d\n", *p);
    printf("&x = %p\n", (void*)&x);
    
    return 0;
}
```

**步骤 2**：生成预期输出

```bash
# 使用参考编译器生成预期输出
gcc -o test 135_my_feature.c && ./test > 135_my_feature.expect
```

**步骤 3**：验证 TCC 的输出

```bash
tcc -o test 135_my_feature.c && ./test | diff - 135_my_feature.expect
```

### 9.7.2 测试编写最佳实践

1. **可移植性**：避免依赖特定平台的输出格式
2. **确定性**：不依赖未初始化的值、随机数或时间
3. **完整性**：覆盖正常路径和边界条件
4. **简洁性**：每个测试专注于一个特性
5. **可比较的输出**：使用 `printf` 输出关键值，便于 diff 比较

```c
/* 好的测试模式 */
#include <stdio.h>

int main(void)
{
    /* 测试赋值 */
    int a = 10;
    printf("a = %d\n", a);

    /* 测试指针 */
    int *p = &a;
    *p = 20;
    printf("a = %d\n", a);

    /* 测试数组 */
    int arr[3] = {1, 2, 3};
    printf("arr = %d %d %d\n", arr[0], arr[1], arr[2]);

    return 0;
}
```

### 9.7.3 边界条件测试

```c
/* 测试整数边界 */
#include <stdio.h>
#include <limits.h>

int main(void)
{
    printf("INT_MAX = %d\n", INT_MAX);
    printf("INT_MIN = %d\n", INT_MIN);
    printf("UINT_MAX = %u\n", UINT_MAX);
    
    /* 溢出行为 */
    int x = INT_MAX;
    x = x + 1;
    printf("INT_MAX + 1 = %d\n", x);
    
    /* 除法边界 */
    int a = -7, b = 2;
    printf("-7 / 2 = %d\n", a / b);
    printf("-7 %% 2 = %d\n", a % b);
    
    return 0;
}
```

---

## 9.8 本章小结与练习

### 小结

本章介绍了 TinyCC 的调试和测试体系：

1. **STAB 调试格式**：较老但兼容性好的格式，通过 `.stab` 和 `.stabstr` 段存储调试信息。基本类型在 `default_debug` 数组中预定义，复杂类型按需生成。

2. **DWARF 调试格式**：现代标准格式，使用 abbreviation 表定义 DIE 格式，行号状态机紧凑地编码源码到机器码的映射。支持更精确的类型信息和更好的调试器兼容性。

3. **调试信息生成时机**：贯穿编译的各个阶段——`tcc_debug_start` 初始化、`tcc_debug_line` 行号映射、`tcc_debug_funcstart`/`funcend` 函数范围、块作用域追踪。

4. **tccdbg.c 内部结构**：使用 `_tccdbg` 状态结构管理 DWARF 字符串池、调试哈希表和行号状态机。

5. **GDB 调试**：使用 `-g`（STAB）或 `-gdwarf`（DWARF）选项编译，然后用 GDB 标准流程调试。

6. **测试套件**：`tcctest.c`（4500+ 行）覆盖核心 C 特性，`tests2/`（130+ 个文件）覆盖具体特性，`pp/` 专门测试预处理器。所有测试通过输出比较来验证正确性。

### 练习 1: 调试信息实验

1. 分别使用 `-g`（STAB）和 `-gdwarf`（DWARF）编译同一个程序
2. 使用 `readelf -S` 查看两种模式下生成的段
3. 使用 `objdump --stabs` 和 `objdump --dwarf=info` 查看调试信息内容
4. 用 GDB 在两种模式下分别调试，比较体验差异

### 练习 2: 编写和运行测试

1. 为 `tests2/` 添加一个新测试，覆盖以下特性之一：
   - `_Generic` 选择表达式（C11）
   - `_Static_assert` 静态断言（C11）
   - 指定初始化器（designated initializers）
   - 复合字面量（compound literals）

2. 运行完整测试套件并报告结果：
   ```bash
   cd tests && make test
   ```

3. 尝试在不同平台上运行测试，记录差异


===== FILE: docs/ch10/examples/add_warning.patch =====
From: TinyCC Developer <developer@example.com>
Subject: [PATCH] Add -Wshadow warning for variable shadowing

This patch adds a new -Wshadow warning option that reports when
a local variable declaration shadows a variable in an outer scope.

Example:
    int x = 10;
    void foo(void) {
        int x = 20;  // warning: declaration of 'x' shadows previous
    }

The implementation adds:
1. A 'warn_shadow' field to TCCState
2. Command-line parsing for -Wshadow / -Wno-shadow
3. A check in the variable declaration path that searches for
   same-named symbols in outer scopes

Signed-off-by: TinyCC Developer <developer@example.com>
---
 tcc.h      |  1 +
 libtcc.c   |  4 ++++
 tccgen.c   | 28 ++++++++++++++++++++++++++++
 3 files changed, 33 insertions(+)

diff --git a/tcc.h b/tcc.h
--- a/tcc.h
+++ b/tcc.h
@@ -XXX,6 +XXX,7 @@
     unsigned char warn_discarded_qualifiers;
+    unsigned char warn_shadow;  /* -Wshadow: warn about variable shadowing */
     #define WARN_ON  1
     unsigned char warn_num;

diff --git a/libtcc.c b/libtcc.c
--- a/libtcc.c
+++ b/libtcc.c
@@ -XXX,6 +XXX,10 @@
     } else if (strstart("-Wno-discarded-qualifiers", &p)) {
         s->warn_discarded_qualifiers = 0;
+    } else if (strstart("-Wshadow", &p)) {
+        s->warn_shadow = 1;
+    } else if (strstart("-Wno-shadow", &p)) {
+        s->warn_shadow = 0;
     } else if (strstart("-W", &p)) {

diff --git a/tccgen.c b/tccgen.c
--- a/tccgen.c
+++ b/tccgen.c
@@ -XXX,6 +XXX,34 @@
+/*
+ * Check if a new local variable declaration shadows a symbol
+ * from an outer scope. Called during local variable declaration.
+ */
+static void check_shadow_declaration(int v)
+{
+    TCCState *s1 = tcc_state;
+    Sym *sym;
+
+    if (!s1->warn_shadow)
+        return;
+
+    /* Search for the same name in outer scopes */
+    sym = sym_find(v);
+    while (sym) {
+        /* Skip if same scope level */
+        if (sym->sym_scope >= local_scope) {
+            sym = sym->prev_tok;
+            continue;
+        }
+        /* Skip typedefs and enum constants - they live in different spaces */
+        if ((sym->type.t & VT_BTYPE) == VT_ENUM_VAL ||
+            (sym->type.t & VT_TYPEDEF)) {
+            sym = sym->prev_tok;
+            continue;
+        }
+        /* Skip asm symbols */
+        if (IS_ASM_SYM(sym)) {
+            sym = sym->prev_tok;
+            continue;
+        }
+        /* Found a shadowed declaration */
+        tcc_warning("declaration of '%s' shadows a previous declaration",
+                    get_tok_str(v, NULL));
+        break;
+    }
+}
+
 /* ... existing declaration handling code ... */
 /* At the point where a new local variable is pushed: */
+    check_shadow_declaration(v);

---
2.XX.X

Notes for applying this patch:

1. The line numbers (@@ -XXX) are placeholders - adjust based on
   your version of TCC source code.

2. The exact location where check_shadow_declaration() should be
   called depends on the structure of decl() in your version.
   Look for where push_local_sym() or similar is called for
   new variable declarations.

3. Test with:
   int x;
   void f(void) { int x; }    // should warn
   void g(void) { typedef int x; }  // should NOT warn
   enum { y };
   void h(void) { int y; }    // should warn


===== FILE: docs/ch10/examples/script_engine.c =====
/*
 * script_engine.c - A mini script engine using libtcc
 *
 * Demonstrates a complete embedded scripting system:
 *   - Loading and compiling C scripts
 *   - Registering host functions for the script to call
 *   - Hot-reload support (re-compile when file changes)
 *   - Error recovery (keep old script running on compile failure)
 *
 * Usage:
 *   ./script_engine script.c
 *
 * The script must define:
 *   void on_init(void);
 *   void on_event(const char *event, const char *data);
 *   void on_shutdown(void);
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libtcc.h"

/* ========== Host API available to scripts ========== */

static void host_log(const char *level, const char *msg)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
    printf("[%s %s] %s\n", timebuf, level, msg);
}

static int host_random_int(int min, int max)
{
    return min + rand() % (max - min + 1);
}

static double host_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void host_print_int(const char *label, int val)
{
    printf("  %s = %d\n", label, val);
}

static void host_print_str(const char *label, const char *val)
{
    printf("  %s = \"%s\"\n", label, val);
}

/* ========== Script engine state ========== */

typedef void (*script_fn_init)(void);
typedef void (*script_fn_event)(const char *, const char *);
typedef void (*script_fn_shutdown)(void);

typedef struct {
    TCCState *tcc;
    script_fn_init on_init;
    script_fn_event on_event;
    script_fn_shutdown on_shutdown;
    char *source_path;
    time_t last_modified;
    int loaded;
    int reload_count;
} ScriptEngine;

/* ========== Error handler ========== */

static int compile_errors = 0;

static void script_error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "  [compile error] %s\n", msg);
    compile_errors++;
}

/* ========== Script loading ========== */

static void register_host_functions(TCCState *s)
{
    tcc_add_symbol(s, "host_log",          host_log);
    tcc_add_symbol(s, "host_random_int",   host_random_int);
    tcc_add_symbol(s, "host_time_sec",     host_time_sec);
    tcc_add_symbol(s, "host_print_int",    host_print_int);
    tcc_add_symbol(s, "host_print_str",    host_print_str);
}

static int script_load(ScriptEngine *eng, const char *path)
{
    TCCState *s;
    struct stat st;

    if (stat(path, &st) < 0) {
        fprintf(stderr, "Error: cannot stat '%s'\n", path);
        return -1;
    }

    compile_errors = 0;

    /* Create new TCC state */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Error: tcc_new() failed\n");
        return -1;
    }

    tcc_set_error_func(s, NULL, script_error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Register host API */
    register_host_functions(s);

    /* Compile script */
    if (tcc_add_file(s, path) == -1) {
        fprintf(stderr, "Script compilation failed (%d error(s))\n",
                compile_errors);
        tcc_delete(s);
        return -1;
    }

    /* Relocate */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Script relocation failed\n");
        tcc_delete(s);
        return -1;
    }

    /* Get entry points */
    script_fn_init new_init = tcc_get_symbol(s, "on_init");
    script_fn_event new_event = tcc_get_symbol(s, "on_event");
    script_fn_shutdown new_shutdown = tcc_get_symbol(s, "on_shutdown");

    if (!new_event) {
        fprintf(stderr, "Warning: script does not define on_event()\n");
    }

    /* Swap in new state (under lock if multi-threaded) */
    if (eng->tcc) {
        /* Call old shutdown before replacing */
        if (eng->on_shutdown)
            eng->on_shutdown();
        tcc_delete(eng->tcc);
    }

    eng->tcc = s;
    eng->on_init = new_init;
    eng->on_event = new_event;
    eng->on_shutdown = new_shutdown;
    eng->last_modified = st.st_mtime;
    eng->loaded = 1;
    eng->reload_count++;

    host_log("ENGINE", "Script loaded successfully");

    /* Call init */
    if (eng->on_init)
        eng->on_init();

    return 0;
}

static void script_unload(ScriptEngine *eng)
{
    if (eng->loaded) {
        if (eng->on_shutdown)
            eng->on_shutdown();
        tcc_delete(eng->tcc);
        eng->tcc = NULL;
        eng->loaded = 0;
    }
    free(eng->source_path);
}

/* ========== Hot reload check ========== */

static int script_check_reload(ScriptEngine *eng)
{
    struct stat st;
    if (!eng->source_path || !eng->loaded)
        return 0;
    if (stat(eng->source_path, &st) < 0)
        return 0;
    if (st.st_mtime <= eng->last_modified)
        return 0;

    host_log("ENGINE", "Script file changed, reloading...");
    return script_load(eng, eng->source_path);
}

/* ========== Example default script ========== */

static const char *default_script =
    "#include <tcclib.h>\n"
    "\n"
    "extern void host_log(const char *, const char *);\n"
    "extern int host_random_int(int, int);\n"
    "extern double host_time_sec(void);\n"
    "extern void host_print_int(const char *, int);\n"
    "\n"
    "void on_init(void) {\n"
    "    host_log(\"SCRIPT\", \"Hello from the script engine!\");\n"
    "}\n"
    "\n"
    "void on_event(const char *event, const char *data) {\n"
    "    host_log(\"SCRIPT\", event);\n"
    "    if (!strcmp(event, \"random\")) {\n"
    "        int n = host_random_int(1, 100);\n"
    "        host_print_int(\"random number\", n);\n"
    "    } else if (!strcmp(event, \"time\")) {\n"
    "        /* no-op: time is printed by host */\n"
    "    }\n"
    "}\n"
    "\n"
    "void on_shutdown(void) {\n"
    "    host_log(\"SCRIPT\", \"Goodbye from the script engine!\");\n"
    "}\n";

/* ========== Main ========== */

int main(int argc, char **argv)
{
    ScriptEngine eng = {0};
    char input[256];
    double t0, t1;

    printf("=== Mini Script Engine (libtcc) ===\n\n");

    if (argc > 1) {
        /* Load script from file */
        eng.source_path = strdup(argv[1]);
        printf("Loading script: %s\n", argv[1]);
        if (script_load(&eng, argv[1]) < 0) {
            fprintf(stderr, "Failed to load script.\n");
            /* Fall through to interactive mode with no script */
        }
    } else {
        /* No file provided: write default script to temp file */
        const char *tmp = "/tmp/tcc_script_engine_demo.c";
        FILE *f = fopen(tmp, "w");
        if (f) {
            fputs(default_script, f);
            fclose(f);
            eng.source_path = strdup(tmp);
            printf("Using default script: %s\n", tmp);
            script_load(&eng, tmp);
        }
    }

    /* Event loop */
    printf("\nType events (or 'quit' to exit):\n");
    printf("  random  - generate a random number\n");
    printf("  time    - show current time\n");
    printf("  reload  - force script reload\n");
    printf("  <other> - pass as event to script\n\n");

    while (1) {
        /* Check for hot-reload before each command */
        script_check_reload(&eng);

        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        /* Trim newline */
        input[strcspn(input, "\n")] = '\0';

        if (!strcmp(input, "quit") || !strcmp(input, "exit"))
            break;

        if (!strcmp(input, "reload")) {
            if (eng.source_path)
                script_load(&eng, eng.source_path);
            continue;
        }

        if (!strcmp(input, "time")) {
            t0 = host_time_sec();
            if (eng.on_event)
                eng.on_event("time", "");
            t1 = host_time_sec();
            printf("  Event dispatch time: %.6f sec\n", t1 - t0);
            continue;
        }

        /* Forward to script */
        if (eng.on_event) {
            t0 = host_time_sec();
            eng.on_event(input, "");
            t1 = host_time_sec();
            printf("  [%.6f sec]\n", t1 - t0);
        } else {
            printf("  No script loaded.\n");
        }
    }

    /* Cleanup */
    printf("\nShutting down...\n");
    script_unload(&eng);
    printf("Done. (%d reload(s))\n", eng.reload_count);
    return 0;
}


===== FILE: docs/ch10/exercises/ex1_project.md =====
# 练习 1: 扩展脚本引擎

## 背景

本练习基于 `examples/script_engine.c` 中的脚本引擎，要求你扩展其功能。

## 任务

### 基础扩展

1. **添加更多宿主 API**：在 `register_host_functions()` 中注册以下函数：
   - `host_read_file(const char *path)` - 读取文件内容返回字符串
   - `host_write_file(const char *path, const char *content)` - 写入文件
   - `host_getenv(const char *name)` - 获取环境变量
   - `host_sleep_ms(int ms)` - 休眠指定毫秒数

2. **添加 REPL 模式**：当脚本中定义了 `on_command(const char *line)` 函数时，用户输入的每一行都作为命令传递给脚本处理，实现交互式脚本。

3. **添加错误恢复**：当脚本编译失败时，保持旧版本脚本继续运行。修改 `script_load()` 使其在失败时不改变 `eng` 的状态。

### 高级扩展

4. **多脚本支持**：修改引擎支持同时加载多个脚本文件，每个脚本有自己的命名空间：
   ```
   load module_a.c
   load module_b.c
   call module_a.init()
   ```

5. **事件优先级**：修改事件分发机制，支持事件优先级和取消传播：
   ```c
   /* 脚本中返回 0 表示继续传播，非 0 表示取消 */
   int on_event(const char *event, const char *data) {
       if (!strcmp(event, "shutdown"))
           return 1;  /* 阻止关闭 */
       return 0;
   }
   ```

6. **性能监控**：添加事件处理时间统计，记录每个脚本每个事件的平均处理时间。

## 验证标准

- 基础扩展的所有新 API 能被脚本正确调用
- 编译失败时旧脚本继续运行
- REPL 模式正确工作
- 能用 GDB 调试引擎和脚本


===== FILE: docs/ch10/exercises/ex2_contribute.md =====
# 练习 2: 参与 TCC 开源社区

## 背景

参与开源项目是提升编程能力和理解真实软件工程的最佳途径。本练习引导你完成参与 TCC 社区的全过程。

## 任务

### 任务 1: 阅读和理解

1. 克隆 TCC 仓库：
   ```bash
   git clone https://repo.or.cz/tinycc.git
   cd tinycc
   ```

2. 阅读最近 50 个提交的 log：
   ```bash
   git log --oneline -50
   ```

3. 找到最近修复的一个 bug，详细阅读：
   - bug 的描述
   - 修复的代码变更
   - 附带的测试

4. 写一份分析报告，说明：
   - bug 是如何引入的
   - 修复方案是否是唯一的？有无替代方案？
   - 修复是否可能引入新的 bug？

### 任务 2: 找到并报告一个 bug

1. 阅读 TCC 的测试套件（`tests/`），理解已测试的特性
2. 编写一个测试用例，尝试触发 TCC 的 bug：
   - 测试边界条件（极大的数组、深层嵌套、超长标识符）
   - 测试 C11/C17 特性（`_Generic`、`_Static_assert`、`_Alignas`）
   - 测试预处理器的角落情况
3. 如果找到 bug，按照以下模板报告：

```
Subject: [BUG] 简短描述

## 环境
- TCC version: [tcc -v 输出]
- OS: [uname -a 输出]
- Architecture: [x86_64/arm64/etc]

## 重现代码
[最小化的 C 代码]

## 预期行为
[参考编译器 GCC/Clang 的输出]

## 实际行为
[TCC 的输出或错误]

## 分析
[如果有的话，对原因的猜测]
```

### 任务 3: 提交一个改进

从以下方向选择一个进行改进：

**方向 A: 添加缺失的警告**
- 参考 `examples/add_warning.patch` 的示例
- 选择一个 GCC 有但 TCC 没有的警告

**方向 B: 改进错误消息**
- 找到一个 TCC 的错误消息不够清晰的情况
- 修改为更友好的消息

**方向 C: 添加测试**
- 为 `tests2/` 添加覆盖某个 C 特性的测试

4. 使用 `git format-patch` 生成补丁
5. 在邮件列表上提交补丁（或在本地记录提交过程）

## 验证标准

- 能成功克隆和构建 TCC
- 理解了 TCC 的提交历史和开发模式
- 完成了 bug 分析报告
- 至少尝试了一项改进（添加警告/改进消息/添加测试）
- 理解了补丁提交的完整流程


===== FILE: docs/ch10/index.md =====
# 第十章 综合实践

前九章系统地介绍了 TinyCC 的内部架构——从词法分析到代码生成，从运行时库到调试信息。本章将这些知识综合运用，通过四个完整的实践项目来加深理解，同时提供阅读 TCC 源码的实用技巧和参与社区的指南。

---

## 10.1 项目 1: 用 libtcc 构建脚本引擎

本项目使用 libtcc API 构建一个支持 C 语言语法的嵌入式脚本引擎。这个引擎可以加载 C 源码文件、编译为可执行代码、注册宿主函数、并提供运行时 API。

### 10.1.1 设计目标

- 支持从文件或字符串加载脚本
- 脚本可以调用宿主程序提供的函数（事件系统、日志、I/O）
- 宿主程序可以调用脚本中定义的函数（回调）
- 支持错误报告和恢复
- 支持脚本热重载

### 10.1.2 架构设计

```
┌─────────────────────────────────────────────┐
│                宿主程序                       │
│  ┌─────────┐  ┌──────────┐  ┌─────────────┐│
│  │事件循环  │  │日志系统   │  │脚本管理器    ││
│  └─────────┘  └──────────┘  └──────┬──────┘│
│                                      │       │
│  ┌───────────────────────────────────▼─────┐│
│  │          libtcc 编译引擎                 ││
│  │  tcc_new → 编译 → 重定位 → 获取符号     ││
│  └─────────────────────────────────────────┘│
│                                      │       │
│  ┌───────────────────────────────────▼─────┐│
│  │          脚本代码 (script.c)             ││
│  │  on_init() / on_event() / on_shutdown() ││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘
```

### 10.1.3 实现细节

脚本管理器维护一个 `ScriptEngine` 结构：

```c
typedef void (*script_init_fn)(void);
typedef void (*script_event_fn)(const char *event, const char *data);
typedef void (*script_shutdown_fn)(void);

typedef struct {
    TCCState *tcc_state;
    script_init_fn on_init;
    script_event_fn on_event;
    script_shutdown_fn on_shutdown;
    char *source_path;
    time_t last_modified;
    int loaded;
    pthread_mutex_t lock;
} ScriptEngine;
```

加载脚本的流程：

```c
int script_load(ScriptEngine *engine, const char *path)
{
    TCCState *s = tcc_new();
    if (!s) return -1;

    /* 设置错误回调 */
    tcc_set_error_func(s, engine, script_error_handler);

    /* 设置输出模式 */
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* 注册宿主 API */
    register_host_api(s);

    /* 编译脚本 */
    if (tcc_add_file(s, path) == -1) {
        tcc_delete(s);
        return -1;
    }

    /* 重定位 */
    if (tcc_relocate(s) < 0) {
        tcc_delete(s);
        return -1;
    }

    /* 获取脚本入口点 */
    pthread_mutex_lock(&engine->lock);

    /* 释放旧状态 */
    if (engine->tcc_state)
        tcc_delete(engine->tcc_state);

    engine->tcc_state = s;
    engine->on_init = tcc_get_symbol(s, "on_init");
    engine->on_event = tcc_get_symbol(s, "on_event");
    engine->on_shutdown = tcc_get_symbol(s, "on_shutdown");
    engine->loaded = 1;

    pthread_mutex_unlock(&engine->lock);
    return 0;
}
```

宿主 API 注册：

```c
/* 宿主提供的函数 */
static void host_log(const char *msg) { printf("[LOG] %s\n", msg); }
static int host_random(void) { return rand(); }
static double host_time(void) { /* ... */ }

static void register_host_api(TCCState *s)
{
    tcc_add_symbol(s, "host_log", host_log);
    tcc_add_symbol(s, "host_random", host_random);
    tcc_add_symbol(s, "host_time", host_time);
}
```

脚本文件示例：

```c
/* script.c - 脚本代码 */
extern void host_log(const char *msg);
extern int host_random(void);

void on_init(void) {
    host_log("Script initialized!");
}

void on_event(const char *event, const char *data) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Event: %s (%s)", event, data);
    host_log(buf);
}

void on_shutdown(void) {
    host_log("Script shutting down");
}
```

### 10.1.4 热重载

热重载通过定期检查文件修改时间实现：

```c
int script_check_reload(ScriptEngine *engine)
{
    struct stat st;
    if (stat(engine->source_path, &st) < 0)
        return 0;

    if (st.st_mtime > engine->last_modified) {
        host_log("Script modified, reloading...");
        script_shutdown(engine);
        int ret = script_load(engine, engine->source_path);
        engine->last_modified = st.st_mtime;
        return ret;
    }
    return 0;
}
```

### 10.1.5 完整示例代码

完整的脚本引擎实现见 `examples/script_engine.c`。

---

## 10.2 项目 2: 为 TCC 添加一个新警告

本项目通过一个完整的 walkthrough 来演示如何为 TCC 添加一个新的编译器警告。

### 10.2.1 目标

添加一个 `-Wshadow` 警告：当局部变量名遮蔽（shadow）了外层作用域的同名变量时发出警告。例如：

```c
int x = 10;
void foo(void) {
    int x = 20;  /* 警告: variable 'x' shadows outer declaration */
}
```

### 10.2.2 修改步骤

**步骤 1: 添加警告选项标志**

在 `tcc.h` 的 `TCCState` 结构体中添加新字段：

```c
/* tcc.h */
struct TCCState {
    /* ... */
    unsigned char warn_shadow;    /* 新增: -Wshadow */
    /* ... */
};
```

**步骤 2: 解析命令行选项**

在 `tcc.c` 或 `libtcc.c` 的选项解析代码中添加：

```c
/* libtcc.c - 在 tcc_set_options 或命令行解析中 */
} else if (strstart("-Wshadow", &p)) {
    s->warn_shadow = 1;
} else if (strstart("-Wno-shadow", &p)) {
    s->warn_shadow = 0;
```

**步骤 3: 实现检测逻辑**

在 `tccgen.c` 的符号表管理代码中，当推送新的局部变量时检查是否遮蔽了外层变量：

```c
/* tccgen.c - 在 push_local_sym 或相关函数中添加 */
static void check_shadow(TCCState *s1, int v, Sym *local_stack)
{
    if (!s1->warn_shadow)
        return;

    /* 在外层作用域中查找同名符号 */
    Sym *outer = sym_find(v);
    while (outer) {
        if (outer->sym_scope < local_scope &&
            !(outer->type.t & VT_TYPEDEF) &&
            !IS_ASM_SYM(outer)) {
            tcc_warning("declaration of '%s' shadows "
                       "a previous declaration",
                       get_tok_str(v, NULL));
            break;
        }
        outer = outer->prev_tok;
    }
}
```

**步骤 4: 在正确的位置调用检查**

在局部变量声明时调用 `check_shadow`：

```c
/* tccgen.c - 在 decl() 函数中处理局部变量声明时 */
if (local_scope > 0) {
    check_shadow(tcc_state, v, local_stack);
}
```

**步骤 5: 更新帮助信息**

在帮助文本中添加新选项的描述。

### 10.2.3 测试新警告

```c
/* test_shadow.c */
#include <stdio.h>

int global_var = 10;

void test_shadow(void) {
    int local_var = 20;
    {
        int local_var = 30;  /* 应该警告: shadows outer 'local_var' */
        printf("%d\n", local_var);
    }
    {
        int global_var = 40;  /* 应该警告: shadows 'global_var' */
        printf("%d\n", global_var);
    }
}

int main(void) {
    int i;
    for (i = 0; i < 5; i++) {
        int i = 100;  /* 应该警告: shadows 'i' */
        printf("%d\n", i);
    }
    test_shadow();
    return 0;
}
```

```bash
tcc -Wshadow -c test_shadow.c
# 预期输出:
# test_shadow.c:8: warning: declaration of 'local_var' shadows a previous declaration
# test_shadow.c:12: warning: declaration of 'global_var' shadows a previous declaration
# test_shadow.c:18: warning: declaration of 'i' shadows a previous declaration
```

### 10.2.4 补丁提交

完整的修改应该作为一个补丁提交。参见 `examples/add_warning.patch` 中的示例格式。

---

## 10.3 项目 3: 分析 TCC 编译性能

TCC 以其编译速度著称。本项目通过基准测试和性能分析来量化和理解 TCC 的性能特征。

### 10.3.1 基准测试设计

**测试 1: 编译速度**

```bash
#!/bin/bash
# benchmark_compile.sh

# 生成不同大小的测试文件
generate_test_file() {
    local lines=$1
    local file=$2
    echo "/* Generated test file: $lines lines */" > "$file"
    echo "#include <stdio.h>" >> "$file"
    echo "int main(void) {" >> "$file"
    echo '    int i;' >> "$file"
    echo '    volatile int sum = 0;' >> "$file"
    for ((i=0; i<lines; i++)); do
        echo "    sum += $i;" >> "$file"
    done
    echo '    printf("sum = %d\n", sum);' >> "$file"
    echo '    return 0;' >> "$file"
    echo '}' >> "$file"
}

for size in 100 1000 10000 50000 100000; do
    generate_test_file $size "/tmp/test_${size}.c"
    echo "=== $size lines ==="
    echo -n "  TCC: "
    time tcc -c -o /dev/null "/tmp/test_${size}.c" 2>&1 | grep real
    echo -n "  GCC -O0: "
    time gcc -O0 -c -o /dev/null "/tmp/test_${size}.c" 2>&1 | grep real
    echo -n "  GCC -O2: "
    time gcc -O2 -c -o /dev/null "/tmp/test_${size}.c" 2>&1 | grep real
done
```

**测试 2: 运行时性能**

比较 TCC 编译的代码与 GCC 不同优化级别的运行速度：

```c
/* benchmark_runtime.c */
#include <stdio.h>
#include <time.h>

/* 计算密集型函数 */
double compute_pi(int iterations)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < iterations; i++) {
        double term = 1.0 / (2.0 * i + 1.0);
        if (i % 2 == 0)
            sum += term;
        else
            sum -= term;
    }
    return 4.0 * sum;
}

/* 递归函数 */
int ackermann(int m, int n)
{
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

/* 数组操作 */
void matrix_multiply(double *C, const double *A, const double *B, int n)
{
    int i, j, k;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            double sum = 0.0;
            for (k = 0; k < n; k++)
                sum += A[i*n+k] * B[k*n+j];
            C[i*n+j] = sum;
        }
}

int main(void)
{
    clock_t start, end;
    double cpu_time;

    /* 测试 1: 计算 Pi */
    start = clock();
    volatile double pi = compute_pi(10000000);
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Pi computation: %.4f sec, pi = %.10f\n", cpu_time, pi);

    /* 测试 2: Ackermann 函数 */
    start = clock();
    volatile int ack = ackermann(3, 10);
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Ackermann(3,10): %.4f sec, result = %d\n", cpu_time, ack);

    /* 测试 3: 矩阵乘法 */
    #define N 200
    static double A[N*N], B[N*N], C[N*N];
    int i;
    for (i = 0; i < N*N; i++) { A[i] = 1.0; B[i] = 1.0; }
    start = clock();
    matrix_multiply(C, A, B, N);
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Matrix %dx%d multiply: %.4f sec, C[0][0] = %.0f\n",
           N, N, cpu_time, C[0]);

    return 0;
}
```

### 10.3.2 使用 gprof 分析

```bash
# 使用 gprof 分析 TCC 自身
./configure --extra-cflags="-pg"
make
./tcc -c -o /dev/null large_file.c
gprof tcc gmon.out > analysis.txt
```

### 10.3.3 使用 perf 分析

```bash
# 使用 perf 分析编译性能
perf record -g ./tcc -c -o /dev/null large_file.c
perf report
perf annotate
```

### 10.3.4 预期结果

TCC 的编译速度通常比 GCC 快 5-10 倍（无优化模式），原因是：

1. **单遍编译**：TCC 不做复杂的中间优化
2. **直接代码生成**：不构建 SSA 或中间表示
3. **简单链接**：ELF 重定位直接应用
4. **小代码库**：整个编译器可以快速加载

运行时性能方面，TCC 生成的代码通常比 GCC `-O0` 略快（因为 TCC 的寄存器分配器更激进），但比 GCC `-O2` 慢 2-5 倍。

---

## 10.4 项目 4: 构建最小的 C 解释器

本项目使用 TCC 作为 JIT 后端，构建一个最小的 C 语言"解释器"——它逐行读取 C 代码，将其包装为函数，用 TCC 编译并立即执行。

### 10.4.1 设计

```
用户输入: "int x = 42;"
  → 包装为: "int __repl_0001(void) { int x = 42; printf("%d\n", x); return 0; }"
  → tcc_compile_string()
  → tcc_relocate()
  → tcc_get_symbol("__repl_0001")
  → 调用函数

用户输入: "printf(\"hello\\n\");"
  → 包装为: "int __repl_0002(void) { printf(\"hello\\n\"); return 0; }"
  → ...
```

### 10.4.2 状态管理

挑战在于维护变量状态——每次输入都是独立编译的，变量不会自动保持。解决方案：维护一个全局变量字符串，在每次编译时附加到代码前面：

```c
typedef struct {
    TCCState *tcc;
    char *globals;       /* 累积的全局变量声明 */
    int repl_count;      /* REPL 表达式计数 */
} ReplState;
```

### 10.4.3 完整实现

完整的 C 解释器实现见 `examples/script_engine.c`，它结合了 REPL 和脚本文件加载功能。

### 10.4.4 高级特性

1. **类型推断**：通过检查编译器的类型信息来推断表达式类型
2. **错误恢复**：编译错误后保持之前的状态不变
3. **Tab 补全**：利用 `tcc_list_symbols` 枚举可用符号
4. **历史记录**：记录用户输入以便重用

---

## 10.5 阅读 TCC 源码的技巧

### 10.5.1 推荐的阅读顺序

1. **从 libtcc.h 开始**：这是公共 API，了解用户视角
2. **libtcc.c**：API 的实现，包括选项解析和编译流程
3. **tcc.h**：核心数据结构定义（TCCState、Sym、SValue 等）
4. **tccpp.c**：预处理器和词法分析器
5. **tccgen.c**：语义分析和代码生成的前端
6. **x86_64-gen.c**（或其他目标后端）：具体的代码生成
7. **tccelf.c**：ELF 文件处理
8. **tccdbg.c**：调试信息
9. **tccrun.c**：运行时执行（`-run` 模式）

### 10.5.2 使用 GDB 调试 TCC 自身

```bash
# 构建带调试信息的 TCC
./configure --extra-cflags="-g -O0"
make

# 使用 GDB 调试 TCC 编译一个文件
gdb --args ./tcc -c test.c

# 常用断点
(gdb) break tcc_compile       # 进入编译入口
(gdb) break tcc_compile_string # 字符串编译
(gdb) break tccgen_compile     # 前端编译入口
(gdb) break expr_eq            # 表达式解析
(gdb) break gen_opi             # 整数运算代码生成
(gdb) break gfunc_call          # 函数调用代码生成
(gdb) break tcc_error           # 错误发生点

# 条件断点（例如在特定行号停）
(gdb) break tccgen.c:1234
```

### 10.5.3 使用 grep 搜索的技巧

```bash
# 查找函数定义
grep -n "^ST_FUNC\|^LIBTCCAPI\|^PUB_FUNC" tcc*.c | grep "function_name"

# 查找符号引用
grep -rn "VT_CONST\|VT_LOCAL\|VT_JMP" tccgen.c | head -20

# 查找特定操作码的处理
grep -n "case TOK_EQ:\|case TOK_NE:\|case TOK_LT:" tccgen.c

# 查找目标后端的接口函数实现
grep -n "^ST_FUNC\|^static.*void gfunc\|^static.*void gen_" x86_64-gen.c

# 查找错误消息
grep -rn "tcc_error\|tcc_warning" tccgen.c | grep -i "shadow\|undefined\|type"
```

### 10.5.4 理解编译流程

TCC 的编译流程可以概括为：

```
源代码
  ↓
词法分析 (tccpp.c: next(), macro expansion)
  ↓
语法分析 + 语义分析 + 代码生成 (tccgen.c)
  ├── 声明处理: decl(), type_decl()
  ├── 表达式处理: expr(), expr_eq(), unary()
  ├── 语句处理: block()
  └── 代码发射: gv(), gfunc_call(), gen_opi()
  ↓
目标代码 (段数据)
  ↓
链接 (tccelf.c)
  ├── 重定位处理
  ├── 符号解析
  └── 输出生成
```

关键全局变量：

```c
/* tccgen.c */
int ind;          /* 当前代码偏移 */
SValue *vtop;     /* 虚拟栈顶 */
int rsym;         /* 返回跳转目标 */
int anon_sym;     /* 匿名符号计数器 */
int loc;          /* 局部变量偏移 */
```

### 10.5.5 ONE_SOURCE 模式

TCC 默认使用 `ONE_SOURCE=1` 模式，将所有 `.c` 文件通过 `#include` 编译为一个编译单元。这简化了调试（所有函数在一个地址空间），也使得 TCC 可以自举编译：

```c
/* libtcc.c */
#if ONE_SOURCE
#include "tccpp.c"
#include "tccgen.c"
#include "tccdbg.c"
#include "tccasm.c"
#include "tccelf.c"
#include "tccrun.c"
#ifdef TCC_TARGET_X86_64
#include "x86_64-gen.c"
#include "x86_64-link.c"
#include "i386-asm.c"
#endif
#endif
```

---

## 10.6 参与 TCC 社区

### 10.6.1 邮件列表

TCC 的主要开发讨论在 `tinycc-devel` 邮件列表上进行：

- 地址：`tinycc-devel@nongnu.org`
- 档案：https://lists.nongnu.org/mailman/listinfo/tinycc-devel

邮件列表是提交补丁、报告 bug 和讨论设计决策的主要渠道。

### 10.6.2 代码仓库

TCC 的官方 Git 仓库：

```bash
# 克隆仓库
git clone https://repo.or.cz/tinycc.git

# 查看最近的提交
git log --oneline -20

# 查看某个文件的修改历史
git log --oneline tccgen.c
```

镜像仓库也可能存在于 GitHub 等平台上。

### 10.6.3 补丁提交流程

1. **从最新代码开始**：
   ```bash
   git pull origin master
   ```

2. **创建特性分支**：
   ```bash
   git checkout -b my-feature
   ```

3. **实现修改**：确保代码风格与现有代码一致

4. **测试**：
   ```bash
   make test
   ```

5. **生成补丁**：
   ```bash
   git format-patch master
   ```

6. **提交到邮件列表**：
   - 将补丁作为附件发送到 `tinycc-devel@nongnu.org`
   - 在邮件中解释修改的目的和实现方式
   - 包含测试结果

### 10.6.4 代码风格

TCC 有自己独特的代码风格：

```c
/* TCC 代码风格要点 */
/* 1. 使用 Tab 缩进（宽度通常为 4） */
/* 2. 花括号在行尾 */
if (condition) {
    do_something();
} else {
    do_other();
}

/* 3. 函数返回类型在单独行或同一行 */
ST_FUNC void my_function(int arg)
{
    /* ... */
}

/* 4. 注释使用 C 风格 */
/* 这是一个注释 */

/* 5. 宏名称使用大写 */
#define MY_MACRO(x) ((x) + 1)

/* 6. 全局变量使用 ST_DATA */
ST_DATA int my_global;

/* 7. 内部函数使用 ST_FUNC */
ST_FUNC void internal_func(void);
```

### 10.6.5 报告 Bug

报告 bug 时应包含：

1. **TCC 版本**：`tcc -v` 的输出
2. **操作系统和架构**：`uname -a` 的输出
3. **最小重现代码**：尽可能小的 C 代码片段
4. **预期行为**：你期望的结果
5. **实际行为**：实际观察到的结果
6. **参考编译器的行为**：GCC/Clang 对同一代码的处理结果

---

## 10.7 扩展阅读

### 10.7.1 编译器理论

- **Compilers: Principles, Techniques, and Tools** (Aho, Lam, Sethi, Ullman)：经典的"龙书"，涵盖编译器设计的方方面面
- **Engineering a Compiler** (Cooper & Torczon)：更现代的编译器工程教材
- **Advanced Compiler Design and Implementation** (Muchnick)：深入的优化技术

### 10.7.2 C 语言标准

- **ISO/IEC 9899:2018**（C17 标准）：C 语言的权威规范
- **C11 标准草案 N1570**：免费可得的 C11 标准草案
- **The New C Standard: An Economic and Cultural Commentary** (Derek M. Jones)：对 C 标准的详细注释

### 10.7.3 ELF 和目标文件格式

- **System V Application Binary Interface**：ELF 格式的权威规范
- **ELF Specification** (Oracle/SVR4)：ELF 文件格式的详细描述
- **Linkers and Loaders** (John R. Levine)：链接器和加载器的经典教材

### 10.7.4 x86/x86-64 架构

- **Intel 64 and IA-32 Architectures Software Developer's Manual**：Intel 官方手册
- **AMD64 Architecture Programmer's Manual**：AMD 的 x86-64 手册
- **System V AMD64 ABI**：x86-64 调用约定规范

### 10.7.5 ARM 架构

- **ARM Architecture Reference Manual (ARM ARM)**：ARM 架构规范
- **ARM A64 Instruction Set Architecture**：ARM64 指令集
- **AAPCS64**：ARM64 调用约定规范

### 10.7.6 RISC-V 架构

- **RISC-V Instruction Set Manual (Volume 1: User-Level ISA)**：RISC-V 用户级 ISA
- **RISC-V Calling Convention Specification**：RISC-V 调用约定

### 10.7.7 在线资源

- TCC 官方网站：https://bellard.org/tcc/
- TCC 邮件列表档案：https://lists.nongnu.org/mailman/listinfo/tinycc-devel
- TCC 源码浏览器：https://repo.or.cz/tinycc.git
- DWARF 调试标准：https://dwarfstd.org/
- Godbolt Compiler Explorer（在线查看编译器输出）：https://godbolt.org/

### 10.7.8 相关项目

- **QBE**：一个小型的编译器后端，与 TCC 有相似的设计哲学
- **cproc**：另一个小型 C 编译器
- **chibicc**：一个教学用的小型 C 编译器，有详细的注释和逐步实现的教程
- **8cc**/**9cc**：同样是教学用途的小型 C 编译器

---

## 10.8 本章小结

本章通过四个实践项目将前九章的理论知识转化为动手能力：

1. **脚本引擎**项目展示了 libtcc API 的完整应用，包括宿主函数注册、错误处理、符号查找和热重载。

2. **添加新警告**项目演示了修改 TCC 编译器本身的完整流程——从添加选项标志到实现检测逻辑再到测试。

3. **编译性能分析**项目提供了量化 TCC 性能的方法，帮助理解 TCC 的速度优势和代码质量权衡。

4. **C 解释器**项目展示了如何使用 TCC 作为 JIT 后端构建交互式工具。

此外，我们还提供了：
- 阅读 TCC 源码的系统化方法（推荐的阅读顺序、GDB 调试技巧、grep 模式）
- 参与 TCC 社区的指南（邮件列表、补丁提交流程、代码风格）
- 扩展阅读的资源列表（书籍、标准、在线资源）

通过本章的学习，读者应该具备了：
- 独立使用 libtcc API 构建应用的能力
- 修改和扩展 TCC 编译器的能力
- 分析和优化编译器性能的能力
- 参与 TCC 开源社区的能力

这是本书的最后一章。希望通过对 TinyCC 源码的深入分析和实践，读者不仅理解了这个编译器的工作原理，更获得了理解任何编译器系统的通用能力。编译器不是魔法——它是精密的工程，而 TinyCC 是学习这门工程的绝佳起点。


===== FILE: docs/index.md =====
# 深入理解 TinyCC

## 从源码到实践的编译器教科书

> 基于 TinyCC 0.9.28 源码的编译器原理与实现教科书

---

本书以 [TinyCC](https://repo.or.cz/tinycc.git) (tcc) 编译器的完整源码为基础，系统性地讲解现代 C 编译器的设计与实现。不同于传统编译器教材仅停留在理论层面，本书的每一个概念都直接对应 tcc 源码中的具体实现，读者可以边读理论、边看源码、边做实验。

### 本书特色

- **理论与实践并重** — 每个概念既有严格的理论阐述，又有 tcc 源码中的具体实现
- **可运行的示例** — 每章配有可直接编译运行的示例代码
- **循序渐进** — 从词法分析到代码生成，从 ELF 链接到 JIT 运行时
- **面向不同层次读者** — 新手可从附录开始，专家可直接深入特定章节

### 适用读者

- 想理解编译器工作原理的计算机专业学生
- 想深入学习 C 语言实现细节的 C 程序员
- 想将 tcc 嵌入自己项目的开发者
- 想参与编译器开发的开源贡献者

### 快速开始

```bash
# 获取 tcc 源码
git clone https://repo.or.cz/tinycc.git
cd tinycc

# 构建
./configure && make && make test

# 编译并运行一个 C 程序
./tcc -run hello.c
```

---

## 目录

### 第一部分：基础篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 1 章](ch01/index.md) | 编译器导论 | 编译器基本概念、tcc 项目概览、单遍编译架构 |
| [第 2 章](ch02/index.md) | 词法分析 | Token 类型系统、BufferedFile、标识符哈希 |
| [第 3 章](ch03/index.md) | 预处理器 | 宏定义与展开三阶段、条件编译、#include |

### 第二部分：核心篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 4 章](ch04/index.md) | 语法分析与类型系统 | CType/Sym 结构、符号表、表达式解析 |
| [第 5 章](ch05/index.md) | 代码生成 | 虚拟栈、后端接口、寄存器分配 |
| [第 6 章](ch06/index.md) | 链接器与 ELF | ELF 格式、重定位、GOT/PLT、JIT |

### 第三部分：应用篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 7 章](ch07/index.md) | 运行时与嵌入式 API | libtcc API、运行时库 |
| [第 8 章](ch08/index.md) | 跨平台与汇编器 | 多架构后端、交叉编译、内联汇编 |
| [第 9 章](ch09/index.md) | 调试与测试 | STAB/DWARF、测试套件 |
| [第 10 章](ch10/index.md) | 综合实践 | 脚本引擎、修改 tcc、社区参与 |

### 附录

| 附录 | 主题 |
|------|------|
| [新手入门](appendix/prerequisites.md) | C 语言基础、编译器概念、学习路径 |

---

## 编译流水线速览

```
源码 → tccpp.c (词法+预处理) → tccgen.c (解析+代码生成) → tccelf.c (链接) → 输出
```

## 关键源码文件

| 文件 | 行数 | 职责 |
|------|------|------|
| `tcc.c` | ~430 | 主程序入口 |
| `tcc.h` | ~2000 | 核心数据结构 |
| `tccpp.c` | ~4000 | 词法分析器 + 预处理器 |
| `tccgen.c` | ~9000 | 语法分析器 + 代码生成器 |
| `tccelf.c` | ~3500 | ELF 链接器 |
| `libtcc.c` | ~2200 | libtcc API 实现 |

## 许可证

TinyCC 使用 LGPL v2.1 许可证。


===== FILE: README.md =====
# Inside TinyCC: A Compiler Textbook from Source to Practice

> A comprehensive compiler textbook based on the TinyCC 0.9.28 source code

[中文版](README_zh.md) | English

---

## About This Book

This book uses the complete source code of [TinyCC](https://repo.or.cz/tinycc.git) (tcc) as a foundation to systematically explain the design and implementation of modern C compilers. Unlike traditional compiler textbooks that stay at the theoretical level, every concept in this book directly corresponds to a specific implementation in the tcc source code — readers can read the theory, study the source, and run experiments simultaneously.

### Highlights

- **Theory meets practice** — Every concept has rigorous theoretical exposition alongside concrete tcc source code
- **Runnable examples** — Each chapter includes example code that can be compiled and run directly
- **Progressive learning** — From lexical analysis to code generation, from ELF linking to JIT runtime
- **For all levels** — Beginners can start from the appendix; experts can dive directly into specific chapters

### Target Audience

- Computer science students who want to understand compiler internals
- C programmers who want to deepen their understanding of the language
- Developers who want to embed tcc into their own projects
- Open-source contributors who want to participate in compiler development

### Prerequisites

- C language basics (pointers, structs, preprocessor)
- Basic computer architecture knowledge (CPU, memory, assembly)
- Linux command line basics

If any of these are lacking, please start with [Appendix A: Prerequisites for Beginners](docs/appendix/prerequisites.md).

---

## Table of Contents

### Part I: Foundations

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 1](docs/ch01/index.md) | Compiler Introduction | Compiler concepts, tcc overview, single-pass architecture |
| [Ch 2](docs/ch02/index.md) | Lexical Analysis | Token system, BufferedFile, identifier hashing |
| [Ch 3](docs/ch03/index.md) | Preprocessor | Macro expansion (3 stages), conditional compilation, #include |

### Part II: Core

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 4](docs/ch04/index.md) | Syntax Analysis & Type System | CType/Sym structures, symbol table, expression parsing |
| [Ch 5](docs/ch05/index.md) | Code Generation | Virtual stack, backend interface, register allocation |
| [Ch 6](docs/ch06/index.md) | Linker & ELF | ELF format, relocation, GOT/PLT, JIT runtime |

### Part III: Applications

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 7](docs/ch07/index.md) | Runtime & Embedded API | libtcc API, runtime library |
| [Ch 8](docs/ch08/platforms.md) | Cross-Platform & Assembler | Multi-arch backends, cross-compilation, inline assembly |
| [Ch 9](docs/ch09/index.md) | Debugging & Testing | STAB/DWARF, test suite |
| [Ch 10](docs/ch10/index.md) | Hands-on Projects | Script engine, modifying tcc, community participation |

### Appendix

| Appendix | Topic |
|----------|-------|
| [Prerequisites](docs/appendix/prerequisites.md) | C language basics, compiler concepts, learning path |

---

## Quick Start

```bash
# Clone tcc source
git clone https://repo.or.cz/tinycc.git
cd tinycc

# Build
./configure && make && make test

# Compile and run a C program
./tcc -run hello.c
```

## Build the Website

```bash
cd book

# Build static site
./build.sh

# Local preview at http://127.0.0.1:8080
./build.sh serve

# Clean build artifacts
./build.sh clean
```

## Compilation Pipeline

```
Source → tccpp.c (Lexer+Preprocessor) → tccgen.c (Parser+CodeGen) → tccelf.c (Linker) → Output
```

## Key Source Files

| File | Lines | Responsibility |
|------|-------|----------------|
| `tcc.c` | ~430 | Main entry point |
| `tcc.h` | ~2000 | Core data structures |
| `tccpp.c` | ~4000 | Lexer + preprocessor |
| `tccgen.c` | ~9000 | Parser + code generator |
| `tccelf.c` | ~3500 | ELF linker |
| `libtcc.c` | ~2200 | libtcc API implementation |

## Disclaimer

> **⚠️ The majority of this book's content was generated by AI (Qwen Code).** While efforts have been made to ensure accuracy, the explanations, code examples, and analyses may contain errors or inaccuracies. Readers are strongly encouraged to verify the content against the actual TinyCC source code and consult authoritative references. Contributions and corrections are welcome via pull requests.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

TinyCC itself is licensed under LGPL v2.1.


===== FILE: README_zh.md =====
# 深入理解 TinyCC：从源码到实践的编译器教科书

> 基于 TinyCC 0.9.28 源码的编译器原理与实现教科书

[English](README.md) | 中文版

---

## 本书简介

本书以 [TinyCC](https://repo.or.cz/tinycc.git) (tcc) 编译器的完整源码为基础，系统性地讲解现代 C 编译器的设计与实现。不同于传统编译器教材仅停留在理论层面，本书的每一个概念都直接对应 tcc 源码中的具体实现，读者可以边读理论、边看源码、边做实验。

### 本书特色

- **理论与实践并重** — 每个概念既有严格的理论阐述，又有 tcc 源码中的具体实现
- **可运行的示例** — 每章配有可直接编译运行的示例代码（全部经过测试）
- **循序渐进** — 从词法分析到代码生成，从 ELF 链接到 JIT 运行时
- **面向不同层次读者** — 新手可从附录开始，专家可直接深入特定章节

### 适用读者

- 想理解编译器工作原理的计算机专业学生
- 想深入学习 C 语言实现细节的 C 程序员
- 想将 tcc 嵌入自己项目的开发者
- 想参与编译器开发的开源贡献者

### 前置知识

- C 语言基础（指针、结构体、预处理器）
- 基本的计算机组成原理知识（CPU、内存、汇编）
- Linux 命令行基本操作

如果以上知识有欠缺，请先阅读[附录 A：新手入门预备知识](docs/appendix/prerequisites.md)。

---

## 目录

### 第一部分：基础篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 1 章](docs/ch01/index.md) | 编译器导论 | 编译器基本概念、tcc 项目概览、单遍编译架构 |
| [第 2 章](docs/ch02/index.md) | 词法分析 | Token 类型系统、BufferedFile、标识符哈希 |
| [第 3 章](docs/ch03/index.md) | 预处理器 | 宏定义与展开三阶段、条件编译、#include |

### 第二部分：核心篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 4 章](docs/ch04/index.md) | 语法分析与类型系统 | CType/Sym 结构、符号表、表达式解析 |
| [第 5 章](docs/ch05/index.md) | 代码生成 | 虚拟栈、后端接口、寄存器分配 |
| [第 6 章](docs/ch06/index.md) | 链接器与 ELF | ELF 格式、重定位、GOT/PLT、JIT |

### 第三部分：应用篇

| 章节 | 主题 | 核心内容 |
|------|------|----------|
| [第 7 章](docs/ch07/index.md) | 运行时与嵌入式 API | libtcc API、运行时库 |
| [第 8 章](docs/ch08/index.md) | 跨平台与汇编器 | 多架构后端、交叉编译、内联汇编 |
| [第 9 章](docs/ch09/index.md) | 调试与测试 | STAB/DWARF、测试套件 |
| [第 10 章](docs/ch10/index.md) | 综合实践 | 脚本引擎、修改 tcc、社区参与 |

### 附录

| 附录 | 主题 |
|------|------|
| [新手入门](docs/appendix/prerequisites.md) | C 语言基础、编译器概念、学习路径 |

---

## 快速开始

```bash
# 获取 tcc 源码
git clone https://repo.or.cz/tinycc.git
cd tinycc

# 构建
./configure && make && make test

# 编译并运行一个 C 程序
./tcc -run hello.c
```

## 构建静态网站

```bash
cd book

# 构建网站
./build.sh

# 本地预览 http://127.0.0.1:8080
./build.sh serve

# 清理构建产物
./build.sh clean
```

## 编译流水线

```
源码 → tccpp.c (词法+预处理) → tccgen.c (解析+代码生成) → tccelf.c (链接) → 输出
```

## 关键源码文件

| 文件 | 行数 | 职责 |
|------|------|------|
| `tcc.c` | ~430 | 主程序入口 |
| `tcc.h` | ~2000 | 核心数据结构 |
| `tccpp.c` | ~4000 | 词法分析器 + 预处理器 |
| `tccgen.c` | ~9000 | 语法分析器 + 代码生成器 |
| `tccelf.c` | ~3500 | ELF 链接器 |
| `libtcc.c` | ~2200 | libtcc API 实现 |

## 免责声明

> **⚠️ 本书内容主要由 AI（Qwen Code）生成。** 虽然已尽力确保准确性，但文中的解释、示例代码和源码分析可能存在错误或不准确之处。强烈建议读者对照 TinyCC 实际源码进行验证，并参考权威资料。欢迎通过 Pull Request 提交修正和改进。

## 许可证

本项目使用 [GNU 通用公共许可证 v3.0](LICENSE)。

TinyCC 本身使用 LGPL v2.1 许可证。


