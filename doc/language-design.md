# Jiang 语言设计草案

本文档记录 Jiang 语言本身的设计，不记录编译器源码目录结构和实现细节。编译器工程约定见
`doc/architecture.md`。

当前 release/0.5.3 在既有泛型/trait、JIL/backend、所有权、Task 与 Domain/Executor
基础上，用 payload enum 统一表示普通代数数据类型，并保持既有 layout、lifetime、
pattern 和 drop 规则。compiler service 保留为后续工具链的基础，LSP 与 Linux no-libc
仍属于后续版本。
本文档描述当前分支希望稳定下来的语言规则；
未定设计必须显式标注，避免 parser、resolve、sema 在隐含假设上继续扩展。

## 状态标记

本文档只记录目标设计和当前实现差异。
为避免混淆，后续章节使用这些状态：

- **目标规则**：希望长期保留的语言规则。
- **当前已定义**：parser、Semantic Model/type check/JIL/backend 中已经接入并有测试覆盖的规则。
- **当前缺口**：目标设计需要，但当前实现尚未完整接入。
- **未定**：设计尚未冻结，不能作为 resolve/sema 的硬前提。

## 设计目标

Jiang 是面向系统编程的语言，目标是在低层控制能力、工程可维护性和高层抽象之间取得平衡。

核心方向：

- 明确的值语义、指针语义和可变性语义。
- 可读的泛型和 trait 约束。
- AST 保留源码结构，语义信息进入 resolve/sema/Semantic Model。
- 字符串、数组、slice、指针等系统级类型有直接语法支持。
- 通过 lang package 支持场景化语法扩展；扩展必须回到 Jiang syntax tree，再进入普通语义检查。

## 命名规范

- 类型名使用 `PascalCase`。
- 函数名、变量名、字段名、枚举成员、模块别名使用 `snake_case`。
- 基本类型名不是关键字，词法阶段按 `ident` 处理，后续 resolver 解释为内建类型。

示例：

```jiang
struct SourceFile {
    UInt8[]& file_path;
    Int start_offset;
}

UInt8[]& read_source(UInt8[]& file_path) {
    return file_path;
}

import store = "token_store.jiang";
```

## 词法

Token 只表示词法事实，不承载语义类型。

已确定：

- identifier、关键字和基本类型名在 token 层统一为 `ident`；后续由 `SymbolStore`
  和 resolve/sema 解释。
- identifier 的 ASCII fast path 允许 ASCII 字母或 `_` 作为首字符，ASCII 字母、数字或 `_`
  作为后续字符。非 ASCII UTF-8 字符按 Unicode `XID_Start` / `XID_Continue` 判定；
  数字、组合标记等只能在 `XID_Continue` 允许的位置出现。Unicode punctuation 不属于
  identifier，lexer 应产生 `unicode_punctuation` 诊断。
- 保留关键字包括 `new`、`where`、`life`、`import`、`public`、`const`、`alias`、
  `extern`、`return`、`if`、`else`、`guard`、`while`、`for`、`in`、`is`、`enum`、
  `struct`、`trait`、`extend`、`associated`、`init`、`deinit`、`comptime`、
  `switch`、`try`、`catch`、`await`、`break`、`continue`、`defer`、`do`、`throw`、
  `true`、`false`、`null`。`ref`、`unsafe`、`async`、`sync` 是上下文关键字；
  `self`、`Self` 是特殊名字。
- 字符字面量使用单引号，例如 `'a'`。
- 字符串字面量使用双引号，文本按 UTF-8 字节序列处理。
- `Span` 使用字节偏移和字节长度；line/column 在诊断阶段计算。

`Self` 是类型位置的特殊名字。`self` 是类型内部实例函数、`init` 和 `deinit` 的显式参数名，
表示当前 receiver 或初始化目标。没有 `self` 参数的类型内部函数是类型函数。

## Lang Package / 自定义语法

当前已定义：Jiang 支持 block 形式的 lang invocation：

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

`#sql` 中的 `sql` 不是普通名字，也不通过 `import` / resolve 查找。它只来自当前 package
manifest 的 dependency alias：

```ini
[dependencies]
sql = ../sql-lang
```

被调用 dependency 必须声明为 lang package：

```ini
[package]
name = sql-lang
root = lang.jiang
type = lang
```

lang package root 必须 public 导出固定入口 `Lang`，并满足 `std.jiang.syntax.Provider`。
编译器在 host 上把该 package 编译成 dynamic library，lexer/parser 在 syntax 阶段调用 provider。

语言层规则：

- 只支持 block invocation：`#alias { ... }`。
- 当前不支持 `#alias(...)`。
- 当前不支持源码内声明多个 parser 入口。
- 一个 lang package 只提供一个默认 provider。
- provider 输出必须是 `std.jiang.syntax.Tree`。当前支持 expression 和 statement 位置，因此
  root kind 必须匹配 `Input.entry_kind`；其他 root kind 是 public syntax tree 为后续扩展保留的结构。
- provider 不能直接生成 Semantic Model、JIL、后端 IR，也不能绕过普通 resolve/type check。
- DSL 生成的节点和普通 Jiang 源码节点进入同一套 resolve/sema/JIL/backend。

provider 有两个阶段：

```text
scan(input, builder) -> ScanResult
parse(input, builder) -> NodeId
```

lexer 看到 `#alias {` 后创建 per-block provider 实例并调用 `scan`。`scan` 负责判断 DSL block
边界，并可把私有 token/cache 保存在 provider 实例字段中。parser 后续读到 `raw_block` token 时
调用同一实例的 `parse`，得到 public syntax tree，再由 compiler 转换成内部 AST。编译器内建
provider 当前包括 inline asm：`#asm { ... }` 是短名，`#jiang.asm { ... }` 是完整内建路径。

这种机制的目标不是把 Jiang 变成文本宏语言，而是让不同领域可以使用更适合的表面语法，例如
SQL、shader 或 UI DSL，同时保持后续类型检查、借用检查、单态化和 backend 仍由 Jiang 编译器统一处理。

## 字面量

已确定字面量：

- integer literal
- float literal
- char literal
- string literal
- bool literal: `true` / `false`
- null literal: `null`

字符字面量用于表示单个字符。`UInt8 byte = 'a';` 这类初始化由 expected type 约束；非 ASCII 字符初始化 `UInt8` 应编译失败。

字符串字面量是 UTF-8 字节序列。字符串字面量的默认类型为 `UInt8[:0]&`；backing storage 会自动追加末尾 `0`，但该 sentinel 不计入 length。字符串字面量可用于 `UInt8[_]` / `UInt8[]&` / `UInt8[:0]&`，也可在 expected type 下转换为 `UInt8*` 或 `UInt8[N:0]`。

`UInt8*` 可以接收字符串字面量，用于 C ABI 的 NUL 结尾地址；类型本身不记录 sentinel。需要保留 length 和 sentinel 语义时使用 `UInt8[:0]&`。

## 类型系统

Jiang 类型语法遵循从左往右、从里到外的原则。类型后缀越靠右，包裹范围越大。

基础类型名例如 `Int`、`UInt8`、`Bool`、`Float`、`Double`、`Char` 都是普通名字，由 resolver 解析为内建类型。

目标规则：除 `Tuple` 和 `Fn` 暂时排除外，所有类型都拥有自己的 namespace。这里的 namespace
表示该类型可承载静态函数、实例方法、constructor、associated type、trait implementation
和 extension 成员；它不要求该类型一定是 `struct` 内存模型。`Int`、`Bool`、array、slice、
sentinel slice、pointer、reference、box、optional、result、user struct、enum、trait
self type 等都应统一作为 type namespace provider 参与 `Type.member` 和 `value.method`
lookup。

内建后缀类型也遵循同一条规则：`UInt8[]&` 的成员 lookup 会落到 borrowed slice
类型的 namespace，`Int[4]` 会落到 array 类型的 namespace，
`Int` 会落到 builtin integer type 的 namespace。backend 或 JIL 可以继续把 builtin 类型
lowering 成高效 ABI 表示，但 resolve/sema 层不应该因为类型是不是 nominal struct 而拆出
不同的成员查找路径。

已确定类型语法：

- `_`：推断类型。
- `Void`：Void type；它唯一的值写作 `()`。
- `T` / `foo.Bar`：命名类型。
- `T<A, B>`：泛型类型参数。
- `(A, B)`：tuple type。
- `T&!`：唯一可变引用。
- `T?`：optional 类型。
- `T^`：owning pointer；它不是 C 风格 raw pointer。
- `T&`：非 owning 引用，不表达释放职责。
- `T[]&`：borrowed slice view，layout 是 `{ data, length }`，不表达所有权。裸 `T[]` 是
  unsized array type，不能作为普通 value。
- `T[:S]&`：borrowed sentinel slice view，
  layout 与 `T[]&` 一样是 `{ data, length }`，并额外保证 `data[length] == S`。
  裸 `T[:S]` 是带 sentinel 的 unsized array type，不能作为普通 value。
- `T*`：raw pointer，供 FFI / ABI / 低层能力使用；可在 `unsafe` 中按下标读取，`T*!`
  还可在 `unsafe` 中按下标写入。
- `T[N]`：定长数组。
- `T[N:0]`：sentinel 定长数组。逻辑长度为 `N`，实际 storage 为 `N + 1` 个元素，末尾元素保存 sentinel；`T[N:0]$.size()` 包含 sentinel storage。
- `T[_]`：数组长度由初始化器推断。
- `T@E`：errorable result，只能出现在函数、方法和 callable 类型的返回位。
- 错误类型 `E` 顶层不能带 `?` 或 `!`。
- `T@E` 两侧不允许空白，避免与前缀 annotation 和普通表达式混淆。

内建后缀类型语法不经过普通名字解析，compiler-owned constructor 名称也不进入用户可见
namespace。用户仍可声明同名 nominal type，但不会影响 `T?`、`T[N]`、`T[]&`、`T[:0]&`、
`T^`、`T&`、`T*` 等表面语法。后缀类型仍会参与对应内部 canonical owner 的
extension/member lookup。

sentinel value 使用 `S: const T` 语义，`S` 的类型来自元素类型 `T`。整数 literal 会根据
元素类型转换；非整数 constable 类型也可以作为 sentinel，只要元素类型是 Copyable。
例如 `UInt8[5:0]`、`Bool[1:true]`、`Char[3:'\0']` 和 enum/struct const sentinel 都是同一套规则。

示例：

```jiang
Int[2][3] matrix;
Int?[] values;
UInt8* raw;
Int@Error result;
Int?@Error maybe_result;
```

## 可变性

当前版本只支持两种类型级可变能力：

- `T&!`：唯一可变引用；在其存活期间，借用的 place 不能再被共享借用、可变借用或直接访问。
- `T*!`：可写 raw pointer；在 `unsafe` 中允许下标写入。

其他类型不支持 `!` 后缀。`Int!`、`T^!`、`T[]!`、`T?!` 和 `T!&` 都必须报错。
如果普通变量、参数、全局或字段需要可写，应把 `!` 放在绑定名上：

```jiang
Int value! = 1;
value = 2;

Int& shared = value$.ref();
Int&! borrowed = value$.mut_ref();
Int*! pointer = unsafe { value$.mut_ptr() };
```

绑定名后的 `!` 是 place metadata，不进入 `TypeId` 或函数签名。因此 `foo(Int value!)`
和 `foo(Int value)` 的签名相同；前者只允许函数体内重新赋值参数槽。相反，
`foo(Int&! value)` 的唯一可变引用能力属于参数类型，必须进入签名。
语言不提供额外的 `unique` 参数修饰符；`unique` 是普通标识符。

字段同样使用绑定名表达存储可写性：

```jiang
struct User {
    Int id;
    Int age!;
}
```

共享引用 `T&` 不授予写能力；通过共享引用也不能把字段或元素升级成 `T&!`。
需要修改借用目标时，必须从可写 place 创建 `ref!` / `$.mut_ref()`，并由 borrow checker
保证该可变引用唯一。raw pointer 不参与引用别名证明，写入仍受 `unsafe` 约束。

## 指针、引用、数组和 Slice

Jiang 对共享引用和唯一可变引用执行静态 borrow check，并同时检查 ownership、lifetime 和 drop safety。
这里先固定 pointer/reference 的目标语义：

- `T^`：owning pointer；它不是 C 风格 raw pointer。
- `T&`：shared non-owning reference，不表达释放职责，也不提供可变能力。
- `T&!`：唯一可变的 non-owning reference；存活期间排斥指向同一 place 的其他共享或可变引用。
- `T*`：裸指针，只用于 FFI / ABI / 低层 capability 场景；可在 `unsafe` 中下标读取，
  `T*!` 还可下标写入。
- `T[]`：unsized array type，必须通过 `T[]&` 形成 borrowed slice view，或通过 `T[]^`
  形成 owning handle。
- `T[:0]`：带 sentinel 的 unsized array type，必须通过 `T[:0]&` 形成 sentinel slice view，或通过 `T[:0]^` 形成 owning handle；sentinel view 保证 `data[length] == 0`。

旧的 `T[*]` / `T[*:S]` many pointer 类型已经移除。低层连续内存地址统一使用 `T*` / `T*!`；
需要 length 或 sentinel 保证时使用 slice reference。raw pointer 类型本身不携带这些元数据。

`Void*` / `Void*!` 只用于擦除类型后的传递、比较和转换，不能 `$.get()`、`$.set()` 或下标访问；
访问前必须先转换成具有具体元素类型的 raw pointer。

数组字面量的元素是普通 expression，并由 expected type 约束元素类型与长度。`new [expr, ...]`
可以直接按 expected `T[N]^`、`T[]^` 或 `T[:S]^` 在最终 owning storage 中初始化。借用 expected type
也会参与字面量类型检查，例如 `T[]& value = [...]` 会先完成元素检查，但随后因为引用了
临时 storage 而被 ownership/lifetime 检查拒绝。

标准库 `Vector<T>.slice()` 返回借用 view；`Vector<T>.into_slice(Self self)` 消耗 receiver，
并把 initialized 区间转移为 owning `T[]^`。

`T&`、`T&!` 和 `T[]&` 可以作为字段；它们不拥有目标对象，字段析构时不会释放目标对象。
存储引用字段时，目标对象的生命周期必须覆盖包含该字段的值；`T&!` 字段还会持续持有
其来源 place 的唯一借用。裸 `T[]` 是 unsized array type，
不能作为普通字段类型。

函数签名直接用 `T&!` 声明调用点必须提供唯一可变访问能力：

```jiang
Void swap(Int&! left, Int&! right) {
}
```

该能力属于类型并进入函数签名；`swap(Int&! left, Int&! right)` 不能把同一个 place 同时传给
两个参数。唯一可变能力完全由 `T&!` 类型表达，不存在额外的参数修饰关键字。

唯一可变引用会在单个 borrow-check 域内排斥重叠别名，但这不等于自动证明任意并发程序
没有 data race。跨 domain 传递受 domain borrow 规则约束；跨线程共享可变状态仍必须通过
标准库的 mutex、atomic 或其他显式同步协议表达。Channel 与 rwlock 尚未进入 0.4.8 公共 API。

`^` 和 `&` 会创建新的 language handle 外层。一个完整源码类型中最多只能出现一个 `^`
或 `&` 外层；源码中不允许写出 `^^`、`&&`、`^&` 或 `&^`。`T*` 和 `T[]&`
是 ABI/低层指针视图，可以按 C ABI 需要叠加。归一化阶段如果因为泛型替换得到重复同类
language handle，可以合并；如果得到 `^` 与 `&` 混合的 handle 层，必须保留错误状态并报告
diagnostic。

`T^` 在普通值上下文中默认自动解引用。`T&` 和 `T*` 不默认解引用，必须通过 `$.get()` 显式读取：

```jiang
Int^ foo();

Int^ a = foo();      // expected type 是 Int^，保留 owning pointer
_ b = foo();         // 推导上下文保留自然类型，b: Int^
Int c = foo();       // expected type 是 Int，自动解引用
_ d = foo() + 123;   // 算术上下文，自动解引用为 Int

Int&! ref = value$.mut_ref();
Int copied_value = ref$.get();
```

Jiang 没有前缀手动解引用语法，`*foo()` 这类写法不成立。需要显式取出 `T^` / `T&` / `T*`
指向的值时，使用隐式操作层的 `value$.get()`；需要通过 pointer/reference 写入目标对象时使用
`value$.set(new_value)`。写入 reference 需要 `T&!`，写入 raw pointer 需要 `T*!`。

```jiang
Int& ref = value$.ref();
Int copied = ref$.get();
unsafe {
    Int*! ptr = value$.mut_ptr();
    ptr$.set(42);
}
```

`$` 会阻止自动解引用，并进入隐式操作层。`$.ref()` 和 `$.ptr()` 分别投影到语言引用和裸指针：

```jiang
Int^ value = new Int(42);

_ ref = value$.ref(); // ref: Int&
unsafe {
    _ ptr = value$.ptr(); // ptr: Int*
    value$.dealloc();
}

Int sum = value + 100;        // 允许：value 自动解引用为 Int
Int bad = value$.ref() + 100; // 错误：Int& 不会在结果位置继续自动解引用
```

`T*` raw pointer 不参与默认自动解引用。它表示可在 `unsafe` 中按元素索引的地址：

```jiang
Int* ptr;
Int*! writable;

unsafe {
    _ raw = ptr;                  // raw: Int*
    Int value = ptr[0];
    writable[1] = 42;

    _ item_ref = ptr[1]$.ref();   // item_ref: Int&
    _ item_ptr = ptr[1]$.ptr();   // item_ptr: Int*
}
```

数组长度是类型的一部分；slice 长度是运行时值。

## 所有权、implicit copy 和析构

当前 borrow check 是 JIL 后的必经阶段，用于检查 move/use-after-move、引用逃逸、drop safety
以及 `T&!` 的唯一可变借用。它按重叠 place 和引用的后续使用检查 alias 冲突；raw pointer
不参与这项别名证明，其访问由 `unsafe` 边界约束。

所有权类型：

- `T^` 是 owning pointer。它拥有指向的堆对象，并参与自动析构。
- `T&` 是非 owning 引用。它不拥有资源，不参与自动析构。
- `T*` / `T*!` 是低层指针；`T[]&` 是 slice reference。它们不表达所有权，不参与自动析构。裸 `T[]` 是 unsized array type，不是可独立存放的 reference value。

自动析构规则：

- 是否需要 runtime drop 由类型的 ownership、字段和自定义 `deinit` 决定，不再由 `Movable`
  标记代替。`!Movable` 值仍会在原 place 的生命周期结束时正常析构。
- `T^` 是 owning pointer，drop 时先 drop pointee，再释放其堆存储。
- nominal、tuple、array、optional、errorable 作为值拥有自己的字段、元素或 payload；
  如果内部类型需要 drop，外层按结构递归 drop。
- `T&`、`T*`、`T*!` 本身不拥有目标对象，不会因为 element type 是 `Movable`
  就自动 drop。
- `T[]&` 本身不拥有整段 buffer，drop slice reference 时不 drop 全部元素；但 `slice[i]`
  是一个已初始化 `T` place，覆盖该元素时按 `T` 的 drop 规则处理旧值。
- 经过 `T*!` 得到的 place 是低层裸指针派生 place，写入时是 raw write，
  不隐式 drop 旧值。
- 如果 nominal 有自定义 `deinit`，drop 该 nominal 时先执行自定义 `deinit`，再递归 drop 字段；
  该类型不能实现 `Copyable`。

示例：

```jiang
struct Node {
    Node^ next;      // 自动析构
    UInt8* bytes;  // 不自动析构
    Int length;

    deinit() {
        unsafe {
            bytes$.dealloc(); // raw pointer 不会自动释放，必须显式管理
        }
    }
}
```

析构顺序：

- 同一个 nominal 内，自动析构的字段按字段声明逆序执行。
- 自定义 `deinit` 发生在自动递归字段析构之前。
- 已经被显式 move 的局部变量不再参与析构。

implicit copy / Movable 规则：

- `Movable` 是默认 auto trait，表示初始化完成后值可以改变 storage address。nominal 可以用
  `!Movable` 显式退出；包含 `!Movable` 字段或 payload 的聚合也不可移动。
- `!Movable` 值不能按值传参、返回、赋给新 place、捕获、`$.move()` 或 `$.forget()`；它必须
  直接初始化到最终 place，并一直保留到该 place 析构。直接 `Task<T>` 和 `Mutex<T>` 使用这一规则
  保持地址稳定。
- `Copyable` 继承 `Movable`，决定普通值使用是复制还是移动。整数、浮点、Bool、Char、enum、
  shared reference、raw pointer 和 RawFn 默认 Copyable。
- `T&!` 是唯一 capability，不能自由复制成两个可同时使用的引用；按值传播必须转移它，
  或建立受 lifetime 约束的 reborrow，并在派生借用存活期间冻结原引用。
- tuple、array、optional 和 errorable 只有在所有组成类型都 Copyable 时才 Copyable。
- 用户定义的 struct 和 payload enum 不默认 Copyable；必须显式实现 `Copyable`，且所有字段或
  payload 都必须 Copyable。带自定义 `deinit` 的 nominal 不能 Copyable。
- `T^`、捕获环境的 `Fn` / `Fn^`、直接 `Task<T>` 和 `Task<T>^` 都不是 Copyable。
- 非 Copyable、但 Movable 的值在普通按值位置默认 move；不需要写 `$.move()`。Copyable 值
  默认 copy，也可以用 `$.move()` 强制转移并让源 place 失效。
- 泛型代码只有在 `T: Copyable` 约束下才能依赖隐式复制；无该约束的按值使用按 move 处理。
- 自定义 `copy()` / `clone()` 只是普通 API，不会让类型获得隐式 Copyable 语义。
- owned nominal 的 stored field 可以独立 move，嵌套字段沿同一 place path 处理。move 后该字段
  及其祖先 aggregate 不能作为完整值读取，但不重叠的兄弟字段仍可使用；重新初始化缺失字段后，
  祖先 aggregate 恢复完整。
- 部分移动沿 CFG 做保守合流：任一可达前驱移动过字段，合流后的父值都视为可能不完整。drop
  只析构该路径上仍然初始化的字段；分支内是否发生 move 由对应字段的运行时 drop state 保留。
- 普通 `T&`、`T&!`、raw pointer/index 派生 place 不能被部分移动。任一路径上的父 nominal
  自身声明自定义 `deinit` 时也禁止部分移动，保证 `deinit` 始终观察完整 `self`。

`T&!` 和 `T[]&!` 不属于 Copyable。binding、字段、返回值等普通按值传播会 move capability；
将已有可变引用传给 `T&!` 或 `T&` 参数时建立只持续到调用点的 reborrow，因此调用返回后
原引用仍可继续使用。
reference 的 ABI 表示即使只是一个地址，也不能据此授予第二份可变能力。

```jiang
struct Point: Copyable {
    Int x;
    Int y;
}

Point p2 = p1; // 允许：普通值类型

struct Buffer {
    UInt8^ data;
    Int length;
}

Buffer b2 = b1;         // 默认 move，b1 随后失效
Buffer b3 = b2$.move(); // 也允许显式 move，b2 随后失效
```

move：

- 非 Copyable 值在赋值、按值传参、返回和 capture 时默认 move；`value$.move()` 是保留的
  显式形式。
- 对 Copyable 值使用 `$.move()` 会强制 move，而不是 copy。
- move 后，源变量进入失效状态，后续不能读取、写入、调用方法或再次 move。
- move 后的源变量离开作用域时不会调用 `deinit`。
- move 的目标变量成为新的有效值，后续按普通局部变量规则参与析构。

```jiang
Buffer a = Buffer();
Buffer b = a;

a.length; // 编译错误：a 已经 move
// 作用域结束时只析构 b，不析构 a
```

`T&` 表达共享只读访问，`T&!` 表达唯一可变访问。borrow checker 会阻止仍活跃的 `T&!`
与重叠共享/可变引用并存，也会阻止通过来源 place 绕过该借用直接访问。引用最后一次
使用后，来源 place 可以恢复访问。

生命周期来源约束使用 `@life(...)` leading annotation 表达，并统一写成
`target: source`。例如 `@life(return: input)` 表示返回值的 lifetime shape 由 `input`
覆盖。每个 target 在一条 annotation 中必须唯一；同 Shape 的多个候选来源使用
`left & right` 逐位取得共同最短 region，多-slot source 使用 `(left, right)` 构造
product shape。
`@life` 与 `@where` 分离：`@where` 只描述类型、trait 和 associated type 约束，
`@life` 只描述 lifetime 来源覆盖。

常用 lifetime 名：

- `self`：方法或 trait 方法的 receiver lifetime。
- `return`：函数返回值 lifetime。
- 参数名：参数值的完整 lifetime shape。
- callable 的 result/参数契约名：对应 callable 位置的 lifetime shape。
- public region 名：nominal 类型公开 shape 中的具名位置。

struct / payload enum 只有显式 `@region` 才公开 lifetime shape。裸名称按源码顺序声明 public
region，`target: source` 在声明 target 的同时表示 source 覆盖 target。每个 target 在
annotation 中只出现一次，source 必须由同一 annotation 的其他 item 声明，但可以位于 target
之前或之后。coverage 可以成环，例如 `a: b, b: a` 表示两个 region 互相覆盖。每个 public
region 必须由字段或 enum payload 的实际 lifetime slot 直接使用，不支持 phantom region：

```jiang
@region(a, b: a)
struct Pair {
    @life(a)
    Int& first;

    @life(b)
    Int& second;
}
```

字段类型只有一个 lifetime slot 时使用 `@life(a)`。多-slot 字段可以按公开 Shape 顺序写
`@life(a, b)`，也可以按 type occurrence 提供的名称写 `@life(left: a, right: b)`。
named 模式的 target 必须唯一且完整，不能与位置模式混用。字段 binding 不支持 `self` source，
也不改变字段 `TypeId`、layout 或 ABI。

`@region` 的普通参数是固定单 slot；`value: T` 显式声明
`shape(value) = shape(T)`，其中 `T` 必须是同一 nominal 的类型泛型参数。region schema
不再从字段布局反向推导。`@region(r, value: T = r)` 还声明字段省略 `value` binding 时，
用 `r` 填充 `shape(T)` 的全部 slots；没有 `= r` 时必须显式提供完整 binding，实际 shape
为空时除外。shape-valued region 必须位于固定 region 之后。

region slot 不得与同一 nominal 的字段、泛型参数或其他 region slot 重名；函数、内部类型和
extension 成员不参与该冲突检查。

reference 使用专用 `reference(value)` Shape，不退化成普通 product，其 schema 在语义上等价于
`@region(r, value: T = r)`。外层 borrow 是第一个
逻辑位置，第二个逻辑位置保持完整 pointee Shape。reference 字段必须绑定外层 borrow；例如
`Pair` 的 Shape 为 `(a, b)` 时，可以只写 `@life(r)`，由 `r` 填充 pointee，也可以完整写作
`@life(r, (a, b))`，但不能扁平化或部分绑定。函数 contract 中，reference 参数根名表示外层
borrow，`input.a` 表示 pointee 的公开 region；`(T t)& input` 中的 `input.t` 表示完整
`shape(T)`。

函数没有显式 `@life` 且返回 Shape 非空时，readonly `self` / `Self&! self` reference receiver
的 Shape 非空则优先使用 receiver，等价于 `@life(return: self)`；即使还有其他非空参数 root，
也不产生歧义。没有该特例时，只有恰好一个用户可见参数 root 的 Shape 非空且与返回 Shape
兼容，才默认使用该完整 root。一个 product Shape 仍只算一个 root。`Self self` 按值 receiver
不享受优先级，只作为普通参数参与唯一 root 计数。

`Task<T>` 的公开 Shape 由一个 capture slot 和 `T` 的 result Shape 组成；`Task<T>^` 继承同一
Shape，owner handle 不增加新的 slot。capture slot 约束 Task closure 中的借用，result Shape
约束 `await()` 取出的值。Task 是直接值还是 owner、frame 位于栈还是 heap，都不改变这份契约。
`await()`、`cancel_and_await()` 或直接 Task 的结构化 join 结束执行后，capture loans 随 Task 根一起
结束。`Task<T>^` owner 析构不等待 coroutine，因此带 capture loans 的 owner 必须先消费，不能靠
离开作用域静默结束借用。

返回 Shape 非空但零个非空输入 root 时，必须显式写 `@life()` 确认返回值不携带参数 borrow；
存在两个或更多非空 root，或唯一 root Shape 不兼容时，也必须显式写出契约。默认契约只由公开
签名决定，不读取函数体。任意显式 `@life(...)` 都完全替换 implicit return contract；
只声明 `callback.result: callback.input` 之类的 callable 子契约不会继续补充外层默认来源。

`@life(return: input)` 约束的是 `input` 值携带进来的 loans，不是按值参数 binding 自身的栈槽。
因此包含引用字段的值可以传播已有 borrow，但不能对按值 `T` / `T^` 参数的字段临时取引用后返回。
不含 borrow 的参数对应空 loan 集合，约束自然成立。raw pointer 不携带语言级 lifetime。
带 `@region` 的 nominal 参数使用公开 region 名选择单个位置，例如 `input.left`。该名称解析为
类型声明的字段 binding，不是对同名字段的访问；private 字段名不能出现在公开 callable contract 中。

`Fn` / `RawFn` 的 result 和参数可以提供按需契约名，供外层函数约束 callback：

```jiang
@life(callback.result: callback.fallback, return: fallback)
Int& apply(
    Fn<Int& result, Int& value, Int& fallback> callback,
    Int& value,
    Int& fallback
);

@life(return.result: return.value)
Fn<T result, T value>^ make_identity<T>();
```

Fn 作为函数参数或返回值时使用同一套命名位置规则；根分别是参数名和 `return`。callable contract
只能引用 result/参数的声明名；需要参与 contract 的位置必须命名。
不支持 `callback[0]` 之类的位置路径。closure environment、receiver adapter 和 continuation
等 ABI 隐藏参数不能出现在公开 contract 中。

名称只在语法、接口和诊断中保留；声明检查会把它们一次性解析成参数索引。解析后的
`LifetimeContract` 属于 callable 的语义签名，调用传播、函数值兼容性和 lambda expected type
共用该 contract；
closure environment 等 ABI 隐藏参数不进入公开索引。trait object 动态派发和 RawFn/Fn adapter
同样必须把 contract 映射到 JIL 实参数；borrow checker 不再用“callee 加全部实参”猜测间接调用来源。

裸 `Fn<R, Args...>` / `RawFn<R, Args...>` 的默认返回契约为空。它等价于把 `R` 固定在参数
lifetime 之外，因此 callback 不能把参数 borrow 作为 `R` 返回；这与普通函数的 signature
elision 不同。需要返回参数 borrow 的高阶接口必须用 callable 契约名显式声明来源。

跨函数调用、返回含引用字段的值、或把来源关系写入 public API 时，仍建议显式表达返回值不超过来源：

```jiang
@life(return: input)
UInt8& first(UInt8& input);

@life(return: buffer)
Slice make_slice(Buffer& buffer);
```

当前 lifetime 检查会阻止局部引用逃出来源 owner 的有效范围，并阻止 owner 在活跃借用期间被
move/drop/free。跨函数和存储到类型字段的来源关系通过 `@life` 检查；shared/mutable alias
冲突则由 loan 的种类、重叠 place 和最后一次使用共同判断。

返回聚合值时，`return` 必须作为完整 Shape 一次映射。source 可以由参数的具名 region
投影或 product expression 构造：

```jiang
@life(return: (left, right))
PairRef make_pair(Int& left, Int& right);

@life(return: value.second)
Int& take_second((Int& first, Int& second) value);
```

不允许把完整 target 拆成 `return.a`、`return.b` 多条映射，也不支持 tuple/Fn 的 `[0]`
位置式 lifetime path。源码中的具名位置会解析为稳定内部投影；array 的运行时下标不能建立
彼此独立的 lifetime 身份，必须保守地与同一 array 的其他元素别名。

`panic(message)` 是进程级不可恢复错误入口，而不是可捕获的 control flow。它先向标准错误输出
消息与换行，再立即 abort；不执行 unwind，也不保证运行局部析构。可恢复失败继续使用 `T@E`，
Task cancellation 不复用 panic。

## 隐式操作层

`$` 用于进入值或类型的隐式操作层。

已确定操作：

- `value$.as(Type)`：强制类型转换，不保证类型安全。
- `value$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`。
- `value$.mut_ref()`：从可写 place 创建唯一可变引用 `T&!`。
- `value$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `T*`，需要 `unsafe`。
- `value$.mut_ptr()`：从可写 place 创建可写裸指针 `T*!`，需要 `unsafe`。
- `value$.get()`：显式解引用 `T^` / `T&` / `T*`，返回指向的值；raw pointer 也可在 `unsafe` 中使用下标访问。
- `value$.set(new_value)`：显式写入 `T*!` 指向的单个目标对象；`T*` 不允许写入。
- `value$.move()`：显式转交当前变量的值；Copyable receiver 也会被强制 move，源 place 随后失效。
- `value$.drop()`：立即结束当前值的生命周期并执行正常析构，需要 `unsafe`。
- `value$.forget()`：让 Movable 值失效但跳过析构，需要 `unsafe`；`!Movable` receiver 必须拒绝。
- `value$.addr()`：获取裸指针，需要 `unsafe`。
- `value$.dealloc()`：释放默认堆分配器上的对象，需要 `unsafe`。
- `optional$.some()`：强制解包 optional。
- `Type$.size()`：类型大小。
- `Type$.align()`：ABI 对齐。
- `Type$.max_align()`：默认分配器保证支持的最大对齐。
- `Type$.alloc()`：分配一个未初始化元素，返回 `Type*!`。
- `Type$.alloc(n)`：分配 `n` 个未初始化元素，返回 `Type*!`。

安全类型转换优先用类型初始化形式，例如 `Int(value)`；`$.as()` 保留为底层强制转换。

隐式操作层的低层操作会逐步接入 effect 检查。当前裸指针获取和显式释放需要放在 `unsafe`
中；语言引用仍由 borrow checker 检查 ownership、lifetime、drop safety 和唯一可变借用。

如果后续引入更细的 capability 系统，`$` 会成为受编译期 capability 约束的低层操作层。每个 `$`
操作都需要对应能力；缺少能力时编译失败。

初步分类：

- 总是安全或低风险的编译期查询：`Type$.size()`、`Type$.align()`、`Type$.max_align()`。
- 类型系统强制操作：`optional$.some()`，后续需要定义失败时的诊断、trap 或静态证明规则。
- 需要低层内存能力：`value$.ptr()`、`value$.mut_ptr()`、`value$.dealloc()`、`Type$.alloc()`、`Type$.alloc(n)`。
- 需要 unsafe/cast 能力：`value$.as(Type)`。

当前阶段不区分编译器源码包和普通 Jiang 包，普通 package 也默认拥有这些低层能力。最小能力集合和
显式授权规则推迟到 capability 系统设计时再固定。

## 声明

顶层声明包括：

- `import name;`
- `import alias = "path.jiang";`
- `public import name;`
- `public import alias = "path.jiang";`
- `alias Name = Type;`
- `alias Name;`
- `public alias exported = module.symbol;`
- `public alias exported;`
- const global declaration: `const Type name = expr;`
- public const global declaration: `public const Type name = expr;`
- global declaration: `Type name = expr;`
- function declaration / definition
- `struct`
- `enum`
- `trait`
- `extend`

目标语言支持 `public import`，用于 re-export 被导入模块的 public API。

顶层 `const` 是编译期常量声明。initializer 在 type check 后通过 sema 级 comptime evaluator
求值，并记录到 `ComptimeStore`。当前支持 literal、`const` 引用、枚举 case、tuple/array/struct
字面量、默认 struct constructor、字段访问、一元/二元表达式、`if`、block 尾表达式、普通 Jiang
函数调用、自定义 `init`、泛型 struct constructor 和泛型函数调用。

`public const` 是模块公开接口的一部分。编译器在 interface artifact 中保存最终实例化后的
declaration type 和 const payload；跨模块使用时由 importer 还原成 `ComptimeValue`，不重新执行
定义模块的 initializer。value path 会先解析出真实 value root，再由 type check 验证后续 member
chain，因此 `build.target.link_libc` 这类 public aggregate const 字段读取按普通字段访问处理。

`ComptimeValue` 只存在于 sema、interface loading/building 和 Semantic Model->JIL lowering 之前。标量 const
在 JIL 中降成 `jil.Const`，枚举 case 降成整数 tag const；复合 const 整体作为运行时值使用时按需
materialize 成 readonly `jil.Global`，initializer 用 `jil.StaticValue` 表达。backend 只消费 JIL
事实，不读取 `ComptimeValue`。

const initializer 不能依赖运行时值，也不能执行 IO 或其他运行时副作用。递归 initializer 诊断为
`recursive_const_initializer`；comptime 函数调用受递归深度和 branch quota 限制，避免编译期执行失控。
const generic 参数的 canonical 约束语法是 `@where(K: const Type)`，例如
`@where(N: const Int) struct Fixed<T, N>`。声明列表中的 `N: const Int` 是等价简写，lower
到同一条 Semantic Model predicate。这里 `const Type` 是一种约束 kind，不是 trait；const generic 名字
绑定在 value namespace，可在表达式中使用，不能作为类型名使用。重复的同类型约束会去重，
类型不一致时报 `conflicting_const_constraint`。
trait associated item 也可以使用同一形式表达编译期值约束，例如
`associated kind: const DomainKind`。

### Import

当前固定两种 import path：

```jiang
import dep;
import dep = "foo/bar.jiang";
```

`import dep;` 中的 `dep` 是 module/package 名称，不是文件路径。它会优先按当前 package
manifest 的 `[dependencies]` alias 解析到依赖 package root；未命中 dependency 时，再按当前
编译上下文中已登记的 module/package 名称解析。

`import dep = "foo/bar.jiang";` 中的字符串是显式文件路径。路径按 Zig 风格解析：相对路径以
当前 import 所在源文件的目录为基准，绝对路径按原路径规范化。编译器只加载字面路径
本身，不隐式补 `.jiang`，也不尝试目录入口 `mod.jiang`。

如果当前 source 是 virtual/buffer，没有真实文件路径，相对 file import 暂按字符串本身规范化；
后续如果需要 IDE buffer 的相对文件 import，需要给 virtual source 增加 base directory。

import 只引入一个模块命名空间 alias，不把目标模块的声明平铺到当前 namespace。被导入模块
的 public API 通过 `dep.Name` 访问。`public import` re-export 的也是这个模块命名空间 alias。

file import 只允许引用当前 package 内的 source file。跨 package 源码依赖必须通过
`[dependencies]` 和 `import dep;` 进入；直接用字符串路径导入另一个 package 的 source 会报错。

### Package

目标语言支持把目录作为 package 入口。目录入口使用固定文件名 `package.ini` 描述 package：

```ini
[package]
name = frontend
root = src/main.jiang

[dependencies]
util = ../util_pkg
```

当前 manifest 只固定这些字段：

- `[package].name`：package 名称。未写时默认取 package 目录名。
- `[package].root`：package 入口源文件。未写时默认取 `<name>.jiang`。
- `[dependencies]`：本地依赖表，key 是依赖 package alias，value 是依赖 package 路径。

manifest 中的 `name` 和 dependency key 使用 Jiang lexer 的 identifier 规则，而不是 ASCII-only
正则：ASCII 字母或 `_` 可作为首字符，ASCII 数字可作为后续字符，UTF-8 标识符字符也可作为
首字符和后续字符；具体 UTF-8 判定复用 lexer 的 Unicode `XID_Start` / `XID_Continue` 规则。
manifest 必须复用 lexer 语义，不能另起一套名字规则。

当前已经把 manifest root 接入 compile path：目录输入会读取 `package.ini`，再编译
manifest 指定的 root source。`ModuleResolver` 会按 source 所在目录向上查找 `package.ini`，
创建或复用对应 package，并登记 `[dependencies]`。依赖 package 内部继续按自己的 manifest
解析相对 dependency path，因此 `app -> util -> base` 这类递归源码依赖会进入同一编译 closure。

package dependency cycle 不允许；module import cycle 允许。也就是说，同一 package 内的
source file 可以形成 import cycle，resolve 会用 visited set 截断递归；不同 package 之间通过
manifest dependency 形成闭环时必须诊断。

package 对外导出面固定为 root file 的 public namespace：

- root file 的 public declaration 是 package API。
- root file 的 `public import` 可以重新导出一个 module namespace，但不 flatten 目标 module
  的 declarations。
- root file 的 `public alias` 可以重新导出 public symbol；函数 alias 保留目标 overload family。
- 非 root module 的 public declaration 不会自动暴露为 package API。
- dependency package 中的 `main` 不参与当前 package 的 hosted entry wrapper 选择。

### Alias

alias 有两种目标：

```jiang
alias Name = Type;
alias name = module.symbol;
```

如果省略右侧目标，`alias Name;` 等价于 `alias Name = Name;`，通常用于同名 re-export：

```jiang
public alias Bool;
```

如果右侧解析为已有 namespace/type/value/member symbol，alias 会绑定到同一个 name domain。
非函数 alias 在 Semantic Model 中记录单一目标 `DefId`；函数 alias 记录一个可见 public overload anchor，
调用时在目标原始 namespace 和 name 下枚举 public overload set，再执行普通重载决议。目标模块
的 private 同名函数不会通过 alias 暴露。如果右侧不能解析为已有 symbol，则按 type alias
处理，右侧必须是类型语法。

`public alias` 是 package public surface 的显式 re-export 机制。package 对外只暴露 root file
的 public namespace；root file 可以通过 `public import` 重新导出模块命名空间，也可以通过
`public alias` 重新导出符号或函数 overload family。非 root module 的 public 声明不会自动成为
package API。

未定事项：

- ambiguous re-export 的诊断和恢复策略。
- 版本求解、lockfile 和 registry 规则。

## 函数和方法

函数一定有返回类型。无返回值使用 `Void`，对应的值写作 `()`。`return;` 是 `return ();` 的简写，
函数也可以在末尾隐式返回 `()`：

```jiang
Void hello() {
    return;
}
```

函数声明示例：

```jiang
Int add(Int left, Int right) {
    return left + right;
}
```

函数参数支持默认值。默认参数可以出现在任意位置；当前默认值只支持 literal，并按参数的
expected type 检查：

```jiang
Int add(Int left = 1, Int right) {
    return left + right;
}
```

位置实参总是绑定最早尚未绑定的参数，不会按类型跳过默认参数。命名参数可以重排，也可以跳过
带默认值的参数；第一个命名参数出现后，后续普通参数都必须使用命名形式：

```jiang
add(10, 20);
add(right: 20);
add(left: 10, right: 20);
draw(x: 1, y: 2);
```

type check 会把 call args 重排成函数签名顺序，并把缺失参数替换成默认值。这个结果写入
`TypeCheckStore.call_args`，JIL lowering 只消费重排后的参数列表，不重新做 overload
或默认参数匹配。

同名函数和同名方法允许 overload。默认参数参与 overload 检查：如果两个 overload
在同一调用点可能同时满足参数数量和参数类型，必须诊断为歧义，而不是依赖声明顺序选择其中一个。

泛型函数：

```jiang
@where(T: Numeric)
T add<T>(T left, T right);
```

`init` / `deinit` 是目标语言的一部分。`init(self, ...)` 定义构造函数，
`deinit(self)` 定义析构逻辑；构造 sugar 使用 `Type(...)`，堆分配构造使用 `new Type(...)`。

## 函数指针和闭包

当前已定义：Jiang 区分裸函数指针和闭包值。

- `RawFn<Ret, Args...>` 是裸函数指针。它只保存函数入口，不携带捕获环境，不需要 drop，
  可用于 C ABI 函数指针边界。
- `Fn<Ret, Args...>` 是 erased callable view。它可以表示捕获 lambda，运行时模型是
  `{ receiver, vtable }`；`receiver` 指向编译器合成的 closure object，`vtable` 提供
  call/drop 槽。
- `Fn<Ret, Args...>^` 是 owned heap closure。`new { [captures] args => body }` 会直接构造
  heap closure object；移动 `Fn^` 只移动 owner handle，drop 时通过 closure vtable
  销毁 environment。
- owned closure 只拥有 closure object 和 by-value capture；borrow capture 仍然受来源 lifetime
  约束。所有 capture loans 合并到一个 callable environment lifetime slot，因此多个 borrow
  capture 使 `Fn^` 受其中最短来源限制。
- callable 类型可写成 `Fn<R result, A value, B fallback>` 或对应的 `RawFn` 形式。result 和参数
  可以按需命名，已提供的名称必须唯一；这些名称只为 lifetime contract、文档和诊断提供稳定引用，不参与 TypeId、
  ABI、重载或调用参数匹配。

`RawFn` 适合顶层函数、类型函数、未绑定实例方法和非捕获 lambda：

```jiang
Int inc(Int value) {
    value + 1
}

RawFn<Int, Int> raw = inc;
RawFn<Int, Int> also_raw = { value => value + 1 };
```

`Fn` 可以捕获外层 local。lambda 必须出现在有 expected callable type 的位置，参数类型由
expected type 下推：

```jiang
Int base = 10;
Fn<Int, Int> add_base = { value => value + base };
```

lambda 可以用 `[...]` 显式选择已有 local 的捕获方式；未列出的普通 value、owner 和 reference local
按共享引用/view 捕获，raw pointer 按值捕获：

```jiang
Fn<Int, Int> add_snapshot = { [base] value => value + base };
Fn<Int> read = { [ref base] => base$.get() };
```

`[name]` 按值捕获，遵守 Copyable/move 规则；`[ref name]` / `[ref! name]` 遵守普通 borrow 和
lifetime 规则。capture list 不声明变量、类型或 initializer，也不接受表达式；需要快照或表达式
结果时先声明普通 local。隐式捕获不能写入外层 storage；`T*!` 仍可在 `unsafe` 中写入 pointee。
逃逸的 owned `Fn^` 不能保存指向已结束栈帧的 borrow。

`[ref name]` 和 `[ref! name]` 分别复用 `name$.ref()` 与 `name$.mut_ref()` 的类型和 lowering 语义。
对已有 reference handle 的 reborrow 是幂等操作；例如 `T&!` 经 `[ref! name]` 后仍为 `T&!`，
不会产生嵌套 reference。

`RawFn` 不允许捕获。`RawFn` 可以通过 `Fn(raw)` 显式包装成同签名 `Fn`，但 `Fn` 不会隐式或
显式退回 `RawFn`：

```jiang
Fn<Int, Int> callable = Fn(raw);
RawFn<Int, Int> bad = callable; // fail
```

方法值不会自动绑定 receiver。`self.method` 或 `Type.method` 作为值时得到的是带显式
receiver 参数的 `RawFn`；如果调用点需要不带 receiver 的 `Fn`，必须写 lambda 显式捕获并调用：

```jiang
struct Meter {
    Int value;

    Int add(self, Int extra) {
        self.value + extra
    }

    Int call(self, Fn<Int, Int> callback) {
        callback(1)
    }

    Int ok(self) {
        self.call { extra => self.add(extra) }
    }
}
```

`Fn` 和 trait object 都是 erased value：调用者不直接知道具体实现类型，而是通过运行时表间接
调用。区别是：trait object 的 vtable 来自 trait requirement，receiver 指向满足 trait 的具体
值；closure object 的 vtable 来自某个 lambda/callable 签名，receiver 指向该闭包的 environment。
trait object 表达“某个类型实现了某个 trait”，closure object 表达“某段代码加上它捕获的环境”。

## 异步、Task 和并发同步

函数声明可以带 `unsafe`、`async` 和静态 Domain effect。effect 也进入 `RawFn` / `Fn` 的函数类型：

```jiang
unsafe Int read_raw(Int* pointer);
async [global_domain] Int load(Int id);

RawFn<unsafe Int, Int*> reader = read_raw;
Fn<async [global_domain] Int, Int> loader = { id => load(id) };
```

lambda 自身不增加 effect 前缀；async/unsafe/domain effect 必须由完整 expected callable type
下推。async `Fn` 与 async `RawFn` 使用普通 async 函数相同的 start/completion ABI，只额外携带 closure
environment。动态调用可以在相同或不同 Domain 间切换；跨 Domain 的参数、result 和 capture
按值 transfer 必须满足 Sendable；共享 borrow `T&`（含 borrowed slice）在 `T: Sendable` 时可以
跨 Domain，并继续由同一套 lifetime/borrow check 约束。

`Sendable` 只描述“值能否安全进入另一个 Domain”，不改变值原有的 ownership、地址稳定性或
lifetime：

- 直接转移 `T` 仍要求 `T` 可移动，复制 `T` 仍要求 `T` 可复制；两种操作还必须满足
  `T: Sendable`。`Sendable` 不隐含 `Movable` 或 `Copyable`，反向也不成立。
- 转移 `T^` 只移动 owner handle，不移动 heap pointee。因此地址固定的
  `T: Sendable + !Movable` 可以通过 `T^` 进入另一个 Domain，不能直接按值转移。
- `T&` 可以在 `T: Sendable` 时作为参数、result 或 capture 跨 Domain。引用可以保存或返回，但其
  使用期限不能超过来源 owner；跨 Domain 不会放宽 owner move、drop 或 reborrow 约束。
- `T&!` 不直接跨 Domain。调用需要 `T&` 时可以从 `T&!` 自动建立 shared reborrow；新借用存活期间
  原 `T&!` 保持冻结。
- tuple、定长 array、optional、errorable result、Task 和用户声明的 aggregate 会逐层检查其
  payload。任一组成部分不满足 `Sendable`，外层也不满足。
- raw pointer 不携带 ownership 或 lifetime 证明，不会自动满足 `Sendable`。低层共享必须留在
  显式 `unsafe` 边界内，或封装进具有明确同步契约的类型。

`unsafe extend T: Sendable;` 表示实现者显式承担 `T` 的跨 Domain 安全责任。该形式只允许用于
`Sendable`，并跳过 aggregate 字段的递归 Sendable 验证；普通 conformance 的结构验证保持不变。
它适用于内部使用 raw pointer、但已自行保证同步、ownership、地址稳定性和释放顺序的 handle。
该声明不会改变字段类型本身的 conformance，也不会改变普通 borrow 的 lifetime。

`Atomic<T>` 在 `T` 是受支持的原子值时提供同步边界。`Mutex<T>` 在 `T: Sendable` 时可以作为
跨 Domain 的同步对象，但 `Mutex<T>` 本体地址固定，应通过 `Mutex<T>^` 转移 owner handle：

```jiang
Mutex<Int>^ counter = new Mutex<Int>(0);
Task(domain: global_domain) {
    counter.with_lock { value =>
        value$.set(value$.get() + 1);
    };
};
```

domain-bound owned closure `Fn<async [domain] (...)>^` 在构造时检查全部 capture。值 capture
遵守相同的 move/copy 与 `Sendable` 规则；`T&` capture 的有效期由 closure environment 的 lifetime
shape 传播，不能活过来源 owner。

普通 async 调用是隐式挂起点，表达式类型仍是函数声明的返回类型。要提前启动并获得
handle，使用 Task initializer：

```jiang
async [main_domain] Int render() {
    Task<Int> first = Task(domain: global_domain) { load(1) };
    Task<Int> second = Task(domain: global_domain) { load(2) };
    first.await() + second.await()
}
```

Task creation 是 eager 的。`Task { ... }` 创建地址固定的直接 `Task<T>`；`new Task { ... }`
在 heap 上原地初始化同一 Task 布局并返回 `Task<T>^` owner：

- 直接 `Task<T>` 是 `!Movable`、非 Copyable 的结构化子任务，可放入 struct、tuple 或固定数组的
  静态 place；包含它的聚合也不可移动、按值传参、返回或捕获。
  optional/errorable/payload enum 等动态变体暂不承载直接 Task。
- `Task<T>^` 是 Movable、非 Copyable 的一等 owner，可以按值传参、返回、存入字段、容器和
  泛型实例。
  move 只转移 owner pointer，不移动 heap 上的 Task/TaskState。
- Task 的公开类型只包含 result type，不包含 Domain 类型参数；Domain 是创建点和 runtime
  元数据。
- `await()` 消费一次 result；第二个可能消费同一 result 的源码位置会被诊断。
- `cancel()` 同步、幂等地发布取消请求，不等待、不消费 result；取消后仍可 `await()`。
- `cancel_and_await()` 发布取消请求并异步等待目标退出，消费 result ownership，但不取消 caller。
- 直接 Task 离开作用域前若仍活跃，compiler 先向同一路径的全部 child 发布取消，再逐个等待
  结束。`Task<T>^` owner 析构不阻塞、也不隐式取消，由 owner/coroutine 双方交接完成最终回收。

取消是协作式的：resume/suspend boundary 会观察请求，长时间不挂起的 async 代码可调用
`coroutine.check_cancelled()` 建立显式检查点。普通 `await()` 发现 child 已取消且没有 result
时，当前 parent 进入 cancellation cleanup，并取消、等待其余 sibling。

`coroutine.sync(Domain) { ... }` 接受必填 Domain 目标和普通尾随 closure。目标可以是命名
`const` Domain，也可以是普通 Domain value 的共享引用。在 async context 中，它挂起
当前 coroutine，结构化切换到目标 Domain，完成后回到原 Domain；它不创建用户可见 Task。普通同步函数
用最外层 `coroutine.sync(Domain)` 进入 runtime 时，会阻塞当前线程等待 closure 完成。
`Task { ... }` 可以继承已有 current Domain；`Task(domain: D) { ... }` 显式选择 execution Domain。

`main_domain` 是绑定进程启动线程的标准串行 Domain，`global_domain`
是进程共享的标准并发 Domain。
Domain 是 execution identity；Executor 是该身份采用的排队策略。每个 canonical `const`
Domain binding 恰好懒创建一个程序级共享 Executor：

```jiang
struct InlineExecutor: Executor {
    Void enqueue(Self& self, ExecutorJob job) {
        job.run();
    }
}

struct InlineDomain: Domain<kind = .serial> {
    associated ExecutorType = InlineExecutor;

    InlineExecutor make_executor(Self& self) {
        InlineExecutor()
    }
}

const InlineDomain inline_domain = InlineDomain();
```

普通 Domain value 则是拥有者管理的运行时身份。它与 `const` Domain 使用同一个 `Domain`
trait、`Executor` contract、serial gate 和 Task ABI，不引入第二套协程模型。它支持 move、
参数、返回值、字段和 generic 流转；Task 和 `coroutine.sync` 通过共享引用选择它：

```jiang
SceneDomain domain = SceneDomain(config: config);
Task<Int> task = Task(domain: domain$.ref()) { load_scene() };
Int value = coroutine.sync(domain$.ref()) { update_scene() };
```

`async [D]` 和 domain-bound callable type 的 `D` 是静态 effect identity，仍只接受 canonical
`const` Domain binding。普通 Domain value 不进入函数类型，因此不需要 dependent effect 或把
Domain 类型参数加入 `Task<T>`。

普通 Domain owner drop 是 non-blocking 的，不广播取消已启动的 Task。每个已接受的 Task
持有 execution lease：即使 Task 还没开始执行，Domain owner 也可以先离开作用域；Executor
只在 owner 和最后一个 lease 都释放后销毁。Task 不因此携带 Domain value 的 borrow lifetime。
`coroutine.sync(domain$.ref())` 则保持普通共享借用直到 closure 及其结构化子协程全部完成；
调用返回后借用结束，结果不携带 Domain lifetime，也不需要为 Executor 建立 execution lease。
`make_executor` 的结果必须自包含，不能保存对 Domain receiver 或其配置字段的引用。

普通 Domain 当前可以放在 local、参数、返回值、aggregate 字段和 generic value 中，
不能直接放入 global storage。长期全局身份应使用 canonical `const` Domain binding。
命名 `const` Domain 具有稳定的程序级 identity，调度开销低于普通 runtime Domain；长期共享身份
应优先使用它。普通 Domain value 为独立 identity 和确定资源生命周期支付少量动态开销，适合页面、
场景或会话等有限生存期资源。选择应以所有权语义为主，而不是把需要及时释放的 Executor 改成常驻值。

`Executor.enqueue` 是同步方法，但可以把 move-only `ExecutorJob` 放入自己的队列后再运行。
它可能从多个线程并发调用，因此 Executor 的可变状态必须使用显式同步。
Job 被接收后必须最终恰好运行一次；未运行、重复运行或在仍有 pending Job 时销毁均违反
contract。Domain 的 `.serial`
保证由 runtime 的 per-domain gate 维护，即使多个串行 Domain 复用同一种并发 Executor，
它们也拥有各自独立的执行身份和串行序列。

跨线程共享简单标量状态使用 `Atomic<T>`。`get()`、`set()`、`get_and_set()` 和
`compare_and_set()` 默认使用 sequential order；同名重载接受 `MemoryOrder.relaxed`、`acquire`、
`release`、`acquire_release` 或 `sequential` 中对该操作合法的顺序。Atomic 只支持后端保证 lock-free
的整数、Bool 和 raw pointer 标量；它是显式内部可变性入口，写操作不要求外部 binding 带 `!`。

同步临界区使用 `Mutex<T>.with_lock<R>(Fn<R, T&!>)`。Mutex 将 lock 与受保护值绑定，只在同步
callback 期间提供 `T&!`，callback 返回后自动解锁。公开 API 不提供 guard，锁的作用域只能由
`with_lock` callback 表达；callback 返回值受生命周期约束，不能让受保护值的引用活过锁。
`Mutex<T>` 是 `!Movable`，当前不提供 poison 状态或公共 Channel/RwLock API。

## Struct 与 Enum

`struct` 用于普通名义类型，支持类型函数、实例函数、`init` 和 `deinit`。

`enum` 是统一的 nominal sum type。无 payload enum 表示有限命名整数集合；variant 可以写成
`case(T)` 或 `case(T name, U other)` 并携带 payload。

payload enum 使用统一的 tagged-sum layout、move、borrow、drop、pattern 和 JIL 语义。
enum variant 和普通类型函数/实例函数共用 `Type.member` 访问面，不能同名。
enum variant 的外部可见性由外层类型是否 public 控制。

当前命名空间规则：

- module/package/import alias 使用 namespace domain。
- 顶层类型、trait 和 associated type 使用 type namespace。
- 函数、全局变量、builtin value 和普通方法使用 value namespace。
- 字段和 enum case 使用 member namespace。
- 每个 type namespace provider 拥有自己的 member/type/value 子 namespace，供 `Type.member`
  路径继续解析；`struct`、`enum`、builtin type 和大部分语法糖类型都属于
  type namespace provider。
- enum variant 虽然底层在 member namespace，仍会和 method 的 value namespace 做额外同名冲突检查。
- `Tuple` 和 `Fn` 暂时不作为可扩展 namespace provider；后续如果需要 tuple method 或函数类型
  method，再单独冻结 lookup 和 ABI 规则。

示例：

```jiang
enum Value<T> {
    none,
    some(T) = 2,
    pair(T value, Bool enabled),
}
```

enum 使用 variant-first 语法，无 payload variant 不需要写 `Void`。case 必须位于成员之前；
存在 method 或嵌套 nominal 成员时，用 `;` 分隔。
payload variant 保留整数 enum 的 underlying type、隐式递增值和显式 discriminant；variant 上的
`@life` 等语义注解使用统一的 lowering 和检查管线。
值到整数的转换使用目标整数类型构造表达式；无 payload 整数 enum 保留
`Type.init?(integer)` 查找已声明 case 的能力。

## Trait 和 Extend

`trait` 描述行为约束。

`extend` 给已有类型增加实现或方法。

泛型 extension 使用独立且显式的模式参数列表：`extend <T> Foo<T> {}`。推荐在 `extend` 和 `<T>`
之间保留空格，为未来的 `extend [options] <T>` 形式保留清晰结构，但该空格不是语法强制要求。
目标类型中的名称不会隐式成为模式参数；未在 `extend <...>` 中声明的名称按普通类型名解析。
`extend Foo<T> {}` 只有在
作用域中确实存在类型 `T` 时才合法，否则报告 `unresolved_type`。`_` 是匿名类型占位符，不创建绑定。

具体 extension target 会在 Semantic Model lowering 时归一化为 canonical owner pattern 和相等约束。例如
`extend Int?` 等价于 `@where(T == Int) extend <T> T?`，`extend Int[4]` 等价于对 array element 和
count 分别添加 `T == Int`、`N == 4`。member lookup 只执行统一的 extension where predicate 匹配。

extension binder 是独立的语义参数 owner，不按位置复用 target owner 的 generic DefId，也不要求两边
参数数量相同。target/equality pattern 在 lookup 时递归生成 `GenericBindings`，并将绑定统一应用到
where predicates、成员参数、返回类型和函数值。`extend <T> T[]^` 可直接捕获嵌套的 `T`；
无法从 target/equality pattern 推导的 binder 报 `unbound_extension_parameter`。

extension 不使用全局 orphan 禁令，用户模块可以扩展 builtin 或其他模块公开的类型。普通 extension 只在
声明模块可见；`public extend` 通过 import graph 传播，`public import` 可以继续 re-export。member lookup
只考虑使用点可见且 pattern/where predicates 满足的 extension；concrete pattern 优先于 generic pattern，
同 specificity 的多个可调用候选报告 ambiguity，不按声明顺序静默选择。

示例：

```jiang
trait Equatable {
    Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
    Void hash<H: Hasher>(self, H&! hasher);
}

trait Indexable {
    Int to_index(self);
    Self from_index(Int index);
}
```

当前规则：

- method 不进入模块顶层命名空间；它们挂到对应 type namespace provider 的成员集合中。
- `extend` 的目标只要求是可扩展 type namespace provider，不要求目标是源码中的 nominal
  `struct`。因此 builtin type、array、slice、sentinel slice、pointer/reference/box 等类型
  都可以通过 core 或 std 源码挂载方法和 trait implementation；`Tuple` 和 `Fn` 暂不支持。
- 第一个参数是 `self` 的类型内部函数是 instance method，`self` 的类型为 `Self&`。
- 第一个参数是 `Self self` 的类型内部函数是 move receiver method，调用会消耗 receiver。
- 没有 receiver 参数的类型内部函数是类型函数，函数体中不能使用 `self`。
- `init(self, ...)` 是 constructor，拥有初始化中的 `self` 目标；`self` 在 `init` body
  中表示正在初始化的 `Self` storage。`init` 只能通过 `Type(...)` / `new Type(...)`
  调用，不作为普通函数值暴露。
- 字段能否被赋值由字段名后的 `!` 和访问路径的可写能力共同决定。修改 receiver 需要 `Self&! self`。
- 默认 `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`；`Self self`
  方法等价于传入 `value$.move()`，调用后原 receiver 失效。
- 如果 receiver 已经是 pointer/reference，`ref.method(args...)` 也等价于 `Type.method(ref, args...)`。
- `Type.method(receiver, args...)` 是显式方法调用形式；第一个实参必须匹配 receiver 类型。
- instance method 作为函数值时，显式 receiver 保留为第一个参数。例如 `Int get(self)`
  的函数值类型是 `RawFn<Int, Self&>`；`Int take(Self self)` 的函数值类型是 `RawFn<Int, Self>`。
  类型函数没有 receiver 参数。
- trait 可以声明没有 receiver 参数的类型函数 requirement，通过 `Type.method(args...)`
  调用，也可以在泛型约束中通过 `T.method(args...)` 调用。
  带 `self` 参数的 trait function requirement 是实例函数 requirement。
- trait 本身不是普通值类型；动态 trait view 通过 compiler-provided companion type
  表达：`Trait.Any` 和 `Trait.Receiver`。`Trait$.ref(value)` 生成 borrowed dynamic view，
  不移动原值；`Trait$.new(value)` 生成 owning dynamic view，返回 `Trait.Any^`。
  `Trait.VTable` 是 compiler-private 方法表类型，用户源码不能直接命名或传参。当前实现支持
  shared/mutable ref receiver method 的动态分派和 owning trait object drop，暂不支持 move receiver
  trait object dispatch。
- 泛型 receiver 的实例方法签名必须用实际 receiver type args 实例化后再检查。例如
  `Holder<T>.get() -> T` 在 `Holder<Int*!>` 上调用时，结果类型为 `Int*!`。
- enum variant name 和同一 enum 的类型函数/显式 method name 共享类型成员命名空间，
  不能重名，避免 `Enum.member(...)` 歧义。
- 同名函数和同名方法允许 overload；参数数量、参数类型或默认参数可接受范围必须
  能区分调用。
- `extend Type: Trait { ... }` 当前做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现。
- `Hashable` 继承 `Equatable`；可作为 hash key 的类型必须同时定义 hash 和相等比较。
- `Movable`、`Copyable`、`Mutable`、`Sendable`、`Domain`、`Contiguous`、`Hashable`、
  `Equatable` 等属于 compiler core trait。std prelude 只导出同一个 DefId；即使后续启用 no-std，
  它们仍然是语言核心约束。

当前已经支持 trait parent 继承和循环诊断、required method 签名检查、associated type/const 实现、
associated type bound、显式 projection、trait-list associated binding 和 where-bound member lookup。
这仍不是通用逻辑程序式 trait solver；候选歧义、递归约束和更复杂 higher-ranked 规则需要保持
显式限制。

未定事项：

- generic associated type 与 higher-ranked bound。
- move receiver trait object dispatch。
- 更复杂递归约束的终止与歧义规则。

## 泛型和 Type Bound

泛型参数写在声明名之后：

```jiang
T id<T>(T value);
```

约束使用 leading annotation：

```jiang
@where(T: Hashable)
T id<T>(T value);
```

支持多个约束：

```jiang
@where(T: Hashable, U: Equatable)
```

支持 intersection bound：

```jiang
@where(T: Hashable & Equatable)
```

支持类型相等/不等约束、negative trait bound，以及 associated type equality constraint；相等关系使用 `==`，不使用赋值语义的 `=`。associated type projection 需要显式写出 trait，以避免短投影歧义：

```jiang
@where(T: Sequence, T.[Sequence].Element == Int, T: !Mutable, T != _^)
```

类型相等/不等约束中的右侧类型可以作为形状 pattern 使用，`_` 匹配单个 type argument。
例如 `@where(T == _^)` 匹配任意 owning pointer，`@where(T != _?)` 排除 optional。
内建后缀类型在 pattern 中直接按 canonical type 处理。

`@where(T: Copyable)` 表示泛型 body 可以隐式复制 `T`；只有 `T: Movable` 时，按值使用仍是 move。
`@where(T: !Movable)` 是真正的 negative trait bound，表示 `T` 地址固定，不能把它当作旧版的
“允许复制”约束。negative bound 对其他 trait 也统一表示“不实现该 trait”。

当前 AST 使用：

- `WhereConstraint`：一条泛型约束。
- `TypeBound`：约束右侧的 bound 表达式。
- `TypeBoundIntersection`：`A & B`。

后置 `T id<T> @where(...)` 不支持。

## Optional 和 Errorable

Optional 只使用 `T?` 表示。`Int?` 表示 `Int` 值可能为空。Optional 不再幂等：
`T??` 表示两层 optional；optional 类型层不支持 `!`。

已确定表达式能力：

- optional chaining: `value?.field`
- coalesce: `value ?? fallback`
- guard: `guard value is .some(payload) else { return; }`
- 强制解包: `value$.some()`
- 条件解包 pattern: `value is .some(payload)`
- 可重赋值条件解包 binding: `value is .some(Int payload!)`
- 借用解包 pattern: `value is .some(ref Int payload)`
- 唯一可变借用解包 pattern: `value is .some(ref! Int payload)`

`.some(...)` / `.none` 是 optional 的 pattern 写法。`some` 是普通标识符，
不再作为 optional pattern 关键字。`ref` 是绑定模式，不是类型名；
`ref T payload` 创建共享借用；`ref! T payload` 创建唯一可变借用并得到 `T&!`。
如果只需要让新绑定可重新赋值，写 `ref T payload!`；绑定名上的 `!` 不改变借用能力。

示例：

```jiang
if value is .some(payload) {
    // payload: T
}

if value is .some(Int payload!) {
    // payload: T；binding 可重新赋值
}

if value is .some(ref Int payload) {
    // payload: T&
}

switch value {
    .some(payload) => ...
    .some(ref Int payload) => ...
    .none => ...
}
```

同一个 optional match/switch 层级中，不同 `.some(...)` 分支匹配范围相同，只是绑定形式不同，因此只能出现一个 `.some(...)` 分支，同时出现是编译错误。

目标设计偏向显式 optional handling。是否支持 `x == null` / `x != null`
分支窄化仍未定；在定稿前，sema 不应依赖该能力。

Errorable 只使用 `T@E` 表示。`T` 是成功值类型，`E` 是错误类型。错误类型顶层不能带
`?` 或 `!`。

合法：

```jiang
Int@Error parse();
Int?@Error parse_optional();
RawFn<Int@(T1?, T2)> parse_tuple_error;
```

非法：

```jiang
Int@Error? parse_optional_error();
Int@Error! parse_mutable_error();
```

errorable 函数调用在同错误类型的函数中透明投影为成功类型 `T`，失败时自动向上一层传播；成功值可
直接参与表达式。`try call() catch error { ... }` 对单个调用点阻止自动传播并处理 error 分支；
不需要错误值时写作 `catch { ... }`。
`throw expr` 只能出现在返回 errorable type 的函数中，`expr` 必须可赋给该函数的 error type。
catch binding 只在 catch body 内可见，类型来自被处理 errorable value 的 error type。

## 控制流

语句：

- `return`
- `throw`
- `break`
- `continue`
- `defer`
- `guard`
- `if`
- `switch`
- `while`
- `for`
- block
- assignment
- expression statement
- local variable declaration

表达式：

- if expr
- switch expr
- try catch expr
- binary/unary expr
- call/field/index/slice/postfix expr

`stmt` 和 `expr` 在语法上保持分离。`block` 的语法以 `doc/grammar.md` 为准：
`block <- "{" stmt* tail_expr? "}"`。`stmt` 不贡献 `block` 的值；`block`
作为表达式使用时，其值只来自最后一个不带分号的 `tail_expr`。没有
`tail_expr` 的 `block` 值为 `Void`。Jiang 没有通用表达式语句，只有
`call_stmt`、赋值、控制语句和声明等明确 statement 形态可以在语句位置出现。
因此：

```jiang
Int x = {
    foo();
    1
};
```

上面的 `block` 类型为 `Int`。如果没有最后的 `tail_expr`，或者最后一个
源码元素是赋值、局部变量声明、`defer` 等语句，则 `block` 类型为 `Void`。

语句 result type 规则：

| 语句 | result type |
| --- | --- |
| `call_stmt` | `Void` |
| `block` | block 的 tail expr result type；无 tail expr 时为 `Void` |
| `return expr?;` | `Never` |
| `throw expr;` | `Never` |
| `break;` | `Never` |
| `continue;` | `Never` |
| `var_decl_stmt` | `Void` |
| `destructure_stmt` | `Void` |
| `assign_stmt` | `Void` |
| `defer_stmt` | `Void` |
| `guard_stmt` | `Void` |
| `while_stmt` | `Void` |
| `for_stmt` | `Void` |

`Never` 表示该语句不会正常继续执行，可以在分支类型统一时转换为任意目标类型。
`return`、`throw`、`break`、`continue` 仍然是语句，不属于普通表达式语法。

`guard expr else { ... }` 用于提前退出并把 pattern 绑定带到后续作用域。
`else` block 必须非空，且最后一条语句必须是 `return`、`break`、`continue`
或 `throw`。更复杂的“所有分支都退出”由后续控制流分析处理。

`defer` 在当前块退出时按 LIFO 顺序执行。`defer` 内不支持 `return`、`break`、`continue`。

### For-in、Sequence 和 Iterator

`for pattern in expr` 优先使用 `Sequence`。`Sequence` 描述“某个值可以产生遍历器”，不会和真正保存遍历状态的 `Iterator` 混在一起。

当前内建 iterable：

- `start..end`：range，左闭右开，item type 是 `Int`。
- `T[]&`：slice view，item type 是 `T`。
- `T[N]`：定长数组，item type 是 `T`。
- `T*` / `T*!`：raw pointer 不是 iterable；需要遍历时使用 range 产生 index，再在 `unsafe` 中用 `p[i]` 访问。

自定义遍历协议分为两层：

```jiang
trait Iterator {
    associated Element;
    Element? next();
}

trait Sequence {
    associated Element;
    associated Iter: Iterator<Element = Element>;
    Iter make_iterator();
}
```

`Iterator` 是有状态游标，`next()` 每次返回下一个元素，`none` 表示结束。
`Sequence` 是容器或视图，`make_iterator()` 产生游标。`for pattern in expr` 的 type check
负责选择具体 iteration plan，并把 `pattern` 的 expected type 设为 `Element`。JIL
lowering 只消费这个 plan，不在 JIL 阶段重新做 trait lookup。

## Pattern Matching

pattern 目前包括：

- literal
- variant
- optional
- tuple

binding/wildcard 只作为子 pattern 使用，不能作为 `is` 或 `switch` 分支根。
tuple pattern 可以作为分支根，并递归保留括号层级。
payload enum/optional 的 Tuple payload 直接展开一层，因此 `(Int, Int)` payload 使用
`.case(left, right)`，不是 `.case((left, right))`。

binding 的统一语义形态是 `binding_mode? type_pattern name binding_mutability?`。
match payload 中可以省略 type pattern；所有 `ref` / `ref!` binding 也可以省略，
例如 `ref item` 等价于 `ref _ item`。独立 by-value 解构必须保留类型位置，并且
整个解构必须写在括号内。type pattern 支持 `_` 以及 `Int[_]`、`_[3]` 这类局部推导。

普通变量定义不接受左侧 `ref`；引用变量通过 `value$.ref()` / `value$.mut_ref()`
作为 RHS 初始化。

`is` 用于 pattern matching，不再使用 `==` 表达 pattern 解构。

示例方向：

```jiang
if value is .some(payload) {
}

if block is .some(Int dead!) {
}
```

普通 enum variant 和 optional 都使用 dot case pattern；optional 不再支持旧 `some payload` pattern。

## Module 和 Visibility

目标规则：

- `import` 只导入当前模块使用，不做 re-export。
- `import dep;` 按 module/package 名称解析，并绑定模块命名空间 `dep`。
- `import alias = "path.jiang";` 按当前文件目录相对路径解析，并绑定模块命名空间 `alias`。
- file import 必须显式写出目标文件路径，不隐式补扩展名或目录入口。
- 被导入模块的 public API 通过 `module.Name` 访问，不默认平铺到当前模块。
- `public import` 导入当前模块使用，并将被导入模块作为当前模块 public API 中的一个模块
  命名空间重新导出。它不摊平被导入模块的声明；例如 `middle` 中 `public import leaf;` 后，
  外部通过 `middle.leaf.Name` 访问，而不是 `middle.Name`。
- `public alias name = target;` 将 symbol 重新导出到当前模块 public namespace；函数目标保留
  目标 namespace/name 下的 public overload family。
- `public` 标记声明对外可见。
- 基本类型不是关键字，由名字解析绑定到内建声明。

ambiguous re-export 仍需后续完善；package dependency 第一版只支持本地源码路径。

当前 resolver 已区分 module、type、value 和 member domain；字段和 enum case 使用 member
domain，associated type 使用 type domain。`foo.Bar` 根据左侧已解析的 module/type/value root 继续查找，
不会仅凭文本在 JIL 或 backend 重判。`import dep;` 优先查当前 package dependency alias。

仍需继续收口的是跨多个 public re-export 路径的 ambiguity 诊断，以及更细的 shadowing policy；
这些规则
不能改变已经建立的 namespace domain 和 package visibility 边界。

## 编译期执行

目标规则：

- `comptime { ... }` 是语言内建编译期 block，表示 block 内 Jiang 代码在编译期执行。
- `comptime` 使用普通关键字入口，不占用后续 `#sql { ... }`、`#asm { ... }` 这类 custom syntax
  namespace；`@` 保留给 attribute / annotation。
- 当前 `comptime` 只支持 module-level，用于 target-specific import / declaration 选择。
- `comptime` block 不生成 runtime code。
- `comptime` block 内使用普通 Jiang 语法。`if`、布尔表达式、字段访问、枚举比较等都复用普通
  parser、resolve、type check 和 const eval，不引入 `#if` 小语言，也不维护第二套 compile-only
  AST/type system。
- `comptime` block 内未执行的分支不参与 import graph、name resolve、type check 或 codegen。
- 第一版仍然先完整 parse `comptime` block，所以未执行分支里的语法错误仍然诊断；只有 parse
  之后的语义阶段会跳过未执行分支。
- `comptime if` 的 condition 是普通表达式，但类型必须能在编译期求值为 `Bool`。
  conditional import 需要在 module graph 阶段就决定依赖边，因此 source selection 使用一个窄的
  AST-level 前置 evaluator；它只负责选源文件里的顶层 item，并产出同一套 `ComptimeValue` 模型。
- 常规 const initializer 由 type check 后的 Semantic Model comptime interpreter 执行。它支持 const 引用、
  aggregate literal、字段访问、控制流、block 尾表达式、普通函数调用和自定义 `init`，但不执行
  IO，不访问运行时变量。

示例：

```jiang
import build;

comptime {
    if (build.target.os == .macos) {
        import provider = "os/macos.jiang";
    } else if (build.target.os == .linux) {
        import provider = "os/linux.jiang";
    } else {
        import provider = "os/unsupported.jiang";
    }
}
```

这里 `if` 仍然是普通 Jiang `if`，区别只是它处在 `comptime` block 内，因此 condition
必须能 const eval 为 `Bool`。

编译器提供 `build` virtual package 承载本次构建的编译期信息。`build` 下直接平铺常用 facts，
不引入 `BuildInfo` 总结构。目标形态包括 `build.target`、后续的 `build.mode`、
`build.compiler`、`build.features` 等。当前 `build.target` 以
`public const TargetInfo target` 的形式暴露，包含 `os`、`arch`、`abi` 和 `link_libc`。
普通源码、`comptime` 条件和 public const aggregate 字段读取都通过同一套 value path / const value
机制访问这些 facts。

## 自定义语法补充规则

当前自定义语法固定为 syntax-stage lang package 机制，入口见本文前面的
“Lang Package / 自定义语法”章节。

补充原则：

- `#` 保留给 custom syntax / lang invocation；`comptime {}` 是核心语言语法，`@` 用于 attribute。
- host lexer 只识别 provider path 和 raw block envelope；DSL body 的内部 token/cache 由 provider 自己维护。
- 自定义语法必须返回 Jiang syntax tree，不能通过字符串拼接回灌 host parser 来隐藏错误位置。
- 自定义语法不能绕过基础语言的错误恢复、诊断 span 和 IDE/LSP 能力。
- Semantic Model-level、JIL-level 或 backend-level plugin 不属于当前 Lang Package 设计。

## 后续提案

### API Effect

第一版不实现通用 API effect system。当前函数是否能写入某个 place，由 binding 名后的 `!`、
receiver 类型和 `T&!` / `T*!` capability 决定；方法不需要额外 `mutating` 标记。现有
`unsafe`、`async` 和
Domain effect 是调用上下文与 ABI 的语言规则，不属于下面设想的行为摘要。

后续可以引入 `@effect(...)` 作为 API 行为契约，而不是借用类型系统的一部分。例如：

```jiang
@effect(read)
Int get(self) {
    return self.value;
}

@effect(write(self))
Void set(Self&! self, Int value) {
    self.value = value;
}

@effect(write(self), io, alloc)
Void save(self, File& file) {
}
```

候选 effect 包括：

- `read`：不修改 receiver、参数或 global 可见状态，不调用 unknown/write/io 函数。
- `write(self)` / `write(arg)` / `write(global)`：可能修改对应对象或状态。
- `io`：执行输入输出。
- `alloc`：分配内存。

未标注函数在该提案中默认为 `unknown` / impure，不强制第一版代码全量标注。若未来启用检查，`@effect(read)` 函数中写入 `self` 的 `!` 字段应编译失败；trait requirement 也可以携带 effect，要求实现不比 requirement 更“脏”。
