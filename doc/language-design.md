# Jiang 语言设计草案

本文档记录 Jiang 语言本身的设计，不记录编译器源码目录结构和实现细节。编译器工程约定见
`doc/architecture.md`。

当前 `release/0.4.4` 分支继承 0.4.3 已实现的自举编译器、core 源码化入口、
标准库孵化入口、泛型/trait 基础、Lang Package 自定义语法、MIR/backend、inline asm、
WASI 输出和源码级语言测试。
本文档描述当前分支希望稳定下来的语言规则；
未定设计必须显式标注，避免 parser、resolve、sema 在隐含假设上继续扩展。

## 状态标记

本文档只记录目标设计和当前实现差异。
为避免混淆，后续章节使用这些状态：

- **目标规则**：希望长期保留的语言规则。
- **当前已定义**：parser、HIR/type check/MIR/backend 中已经接入并有测试覆盖的规则。
- **当前缺口**：目标设计需要，但当前实现尚未完整接入。
- **未定**：设计尚未冻结，不能作为 resolve/sema 的硬前提。

## 设计目标

Jiang 是面向系统编程的语言，目标是在低层控制能力、工程可维护性和高层抽象之间取得平衡。

核心方向：

- 明确的值语义、指针语义和可变性语义。
- 可读的泛型和 trait 约束。
- AST 保留源码结构，语义信息进入 resolve/sema/HIR。
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
- 关键字集合包括 `new`、`import`、`public`、`alias`、`extern`、`return`、`if`、
  `else`、`guard`、`while`、`for`、`in`、`is`、`enum`、`union`、`struct`、`trait`、
  `extend`、`associated`、`switch`、`try`、`catch`、`break`、`continue`、
  `defer`、`throw`、`true`、`false`、`null`、`self`、`Self`。
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
- provider 不能直接生成 HIR、MIR、后端 IR，也不能绕过普通 resolve/type check。
- DSL 生成的节点和普通 Jiang 源码节点进入同一套 resolve/sema/MIR/backend。

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

字符串字面量是 UTF-8 字节序列。字符串字面量的默认类型为 `UInt8[:0]&`；backing storage 会自动追加末尾 `0`，但该 sentinel 不计入 length。字符串字面量可用于 `UInt8[_]` / `UInt8[]&` / `UInt8[:0]&`，也可在 expected type 下转换为 `UInt8[*:0]` 或 `UInt8[N:0]`。

`UInt8[*]` 是普通裸 many pointer，不直接接收字符串字面量；如果要表达 C 风格 NUL 结尾字符串，使用 `UInt8[*:0]`。新代码使用 `UInt8[:0]&` 表达只读 sentinel slice，使用 `UInt8[*:0]` 表达 C ABI 的 NUL 结尾 many pointer。

## 类型系统

Jiang 类型语法遵循从左往右、从里到外的原则。类型后缀越靠右，包裹范围越大。

基础类型名例如 `Int`、`UInt8`、`Bool`、`Float`、`Double`、`Char` 都是普通名字，由 resolver 解析为内建类型。

目标规则：除 `Tuple` 和 `Fn` 暂时排除外，所有类型都拥有自己的 namespace。这里的 namespace
表示该类型可承载静态函数、实例方法、constructor、associated type、trait implementation
和 extension 成员；它不要求该类型一定是 `struct` 内存模型。`Int`、`Bool`、array、slice、
sentinel slice、pointer、reference、box、optional、result、user struct、enum、union、trait
self type 等都应统一作为 type namespace provider 参与 `Type.member` 和 `value.method`
lookup。

内建类型和语法糖类型也遵循同一条规则：`UInt8[]&` 的成员 lookup 会落到
`Reference<Slice<UInt8>>` 的 namespace，`Int[4]` 会落到 array 类型的 namespace，
`Int` 会落到 builtin integer type 的 namespace。backend 或 MIR 可以继续把 builtin 类型
lowering 成高效 ABI 表示，但 resolve/sema 层不应该因为类型是不是 nominal struct 而拆出
不同的成员查找路径。

已确定类型语法：

- `_`：推断类型。
- `()`：Unit 类型。
- `T` / `foo.Bar`：命名类型。
- `T<A, B>`：泛型类型参数。
- `(A, B)`：tuple type。
- `T!`：当前类型层级的可变 flag。
- `T?`：`Option<T>` 的类型语法糖。
- `T?!`：optional 类型层可变的规范写法。
- `T^`：`Box<T>` 的类型语法糖，表示 owning pointer；它不是 C 风格 raw pointer。
- `T&`：`Reference<T>` 的类型语法糖，表示非 owning 引用，不表达释放职责。
- `T[]&`：`Reference<Slice<T>>` 的语法糖，borrowed slice view，layout 是 `{ data, length }`，不表达所有权。裸 `T[]` / `Slice<T>` 是 unsized array type，不能作为普通 value。
- `T[:S]&`：`Reference<SentinelSlice<T, S>>` 的语法糖，borrowed sentinel slice view，
  layout 与 `T[]&` 一样是 `{ data, length }`，并额外保证 `data[length] == S`。
  裸 `T[:S]` / `SentinelSlice<T, S>` 是带 sentinel 的 unsized array type，不能作为普通 value。
- `T[*]`：`ManyPointer<T>` 的类型语法糖，不默认自动解引用，只能通过下标访问元素。
- `T[*:0]`：sentinel many pointer，不带 length，适合 C string ABI。
- `T*`：`RawPointer<T>` 的类型语法糖，供 FFI / ABI / 低层能力使用，不使用语言级 owning pointer 语法表示。
- `T[N]`：定长数组。
- `T[N:0]`：sentinel 定长数组。逻辑长度为 `N`，实际 storage 为 `N + 1` 个元素，末尾元素保存 sentinel；`T[N:0]$.size()` 包含 sentinel storage。
- `T[_]`：数组长度由初始化器推断。
- `T@E`：`Result<T, E>` 的返回类型语法糖。
- `@E` 后的错误类型顶层不能带 `?` 或 `!`；errorable 类型层本身也不能再追加 `?` 或 `!`。如果值类型需要 optional/mutable，必须写在 `@E` 前，例如 `T?!@E`。
- 由于 `(T)` 与 `T` 等价，`T@(E?)` 与 `T@E?` 等价；非法原因仍然是 `@` 后错误类型顶层不能带 `?`。

内建后缀类型语法不经过普通名字解析。即使用户定义了同名 `Option<T>`、`Array<T>`、
`Slice<T>`、`SentinelSlice<T, S>`、`Box<T>`、`Reference<T>`、`RawPointer<T>`、
`ManyPointer<T>` 或 `Result<T, E>`，也不会影响 `T?`、`T[N]`、`T[]&`、`T[:0]&`、
`T^`、`T&`、`T*`、`T[*]`、`T@E` 等表面语法。语法糖形成的类型仍会参与对应
builtin owner 的 extension/member lookup，例如 `UInt8[]^` 可查找 `Box<UInt8[]>` 上的类型函数。

sentinel value 使用 `const T S` 语义，`S` 的类型来自元素类型 `T`。整数 literal 会根据
元素类型转换；非整数 constable 类型也可以作为 sentinel，只要元素类型不是 move-only。
例如 `UInt8[5:0]`、`Bool[1:true]`、`Char[3:'\0']` 和 enum/struct const sentinel 都是同一套规则。

示例：

```jiang
Int[2][3] matrix;
Int?[] values;
UInt8[*] raw;
Result@Error result;
Result?!@Error maybe_result;
```

## 可变性

可变性是类型系统的一部分，并且是分层的。`!` 表示当前类型层级可变；它不表示 optional，也不表示空值。也就是说，`Int` 与 `Int!` 是不同的 Jiang 类型形态，虽然类型检查可以在受控位置做兼容判断。

`!` 作用在当前类型层级，表示这一层可变；同一类型层级中最多出现一次。`?` 是 `Option<T>` 的语法糖，可以重复出现，`T??` 表示 `Option<Option<T>>`。

`^` / `&` 会基于左侧已经形成的类型创建新的 language handle 外层。创建出的外层也可以
继续带 `?` / `!`，但一个完整源码类型中最多只能出现一个 `^` 或 `&` 外层；因此不支持
`T^^`、`T&&`、`T^&` 或 `T&^` 这类 language handle 的 handle。raw pointer、many pointer
和 slice 是 ABI/低层指针形态，允许按 C ABI 需要叠加，例如 `UInt8[*][*]`、`LLVMType*[*]`。
类型归一化阶段如果得到重复 `^` / `&` 层，按当前同类 handle 合并；如果同时出现 `^`
和 `&`，必须保留错误状态并报告 diagnostic，不能静默选择一种。

为了避免用户在不同排列之间做选择，同一层级的 type suffix 顺序固定：

- 普通层写成 `T?!`
- owning pointer 外层写成 `T^?!`
- reference 外层写成 `T&?!`

其中 `?` 和 `!` 可以省略，但剩余 suffix 的相对顺序不能变化：

```jiang
Int?! value;       // optional + mutable
Int!^?! owner;     // optional mutable owning pointer to mutable Int
Int?& ref;         // reference to optional Int
Int&! ref_slot;    // mutable slot containing non-owning reference to Int
```

`T!&` 不作为目标语言类型存在。Jiang 的引用类型不表达“可变借用”或独占访问；
`T&!` 中的 `!` 属于 reference 外层，只表示引用 slot 本身可重绑定。

`Int!?`、`Int^!?`、`Int^^`、`Int&&`、`Int^&` 都是语法错误。编译器可以按规范顺序恢复后继续解析，但必须报告 diagnostic。

如果同一层级同时出现 `?` 和 `!`，源码只能写成 `T?!`：

```jiang
Int?! value; // optional 层可变
```

`T!?` 是语法错误，编译器可以恢复为 `T?!` 继续解析，但必须报告 diagnostic。

重复可变标记是语法错误。编译器恢复时可以把 `T!!` 当作 `T!` 继续解析，但必须报告 diagnostic。只要重复发生在同一类型层级内，即使中间夹着 `?` 也要报错，例如 `T!?!`。

language handle 外层只能是 `^` 或 `&` 之一，不能写成 `T^&` 或 `T&^`。raw pointer、
many pointer 和 slice 可以继续叠加，用于描述 FFI 中的 pointer-to-pointer、pointer array
或 C 字符串数组。

源码报错规则和类型归一化规则要分开：源码中直接写出的重复 suffix 必须报错；归一化阶段因为泛型替换得到重复 flag 时，重复的 `?` / `!` / 同类 handle 可以合并。

```jiang
// 假设 T 实例化为 Int!
T! value; // 归一化为 Int!
```

示例：

```jiang
Bool! flag = true;
flag = false;

Int[3] values = [1, 2, 3];
Int[3]! mutable_values = values; // 外层数组值可重新赋值
mutable_values = [4, 5, 6];

Int![3]! mutable_items = [1, 2, 3]; // 显式声明：元素层和外层数组值都可变
mutable_items[0] = 10;
mutable_items = [4, 5, 6];
```

变量、参数、全局变量和字段声明如果写出了完整左侧类型，则以左侧声明类型为准。左侧类型可以表达每个类型层级的可变性，例如 `Int![3]!` 表示元素层是 `Int!`，外层数组值也是可变层。初始化器只负责提供值，不能反过来改变左侧声明出来的分层可变性。

赋值、初始化、传参和 `return` 都是“把右值写入一个目标位置”的场景。这里不使用普通类型兼容性去判断可变性，而是以目标位置的类型为准：比较目标类型和值类型时，在两侧相同结构位置忽略 mutable flag，但保留 optional flag、pointer kind、数组形状、字段、payload 等结构差异。

```jiang
Int! a = 3;
Int b = a;   // 允许：把 mutable 右值写入不可变目标，目标类型是 Int
Int! c = b;  // 允许：把不可变右值写入 mutable 目标，目标类型是 Int!

Int?! optional_mut = 1;
Int? optional_value = optional_mut; // 允许：optional 层相同，只忽略同层 mutable
Int plain = optional_mut;           // 编译错误：不能忽略 optional

Int! value = 3;
Int& ref = value$.ref(); // 允许：reference 结构相同，pointee 的 mutable 以左侧 Int& 为准
```

这条规则只属于写入目标的场景，不能把 `Int` 与 `Int!` 视为全局等价类型。二元运算、分支合并、重载判定中不应该因为某一侧带 `!` 就把两种类型普遍合并；这些上下文需要先有明确的 expected type 或目标位置，再按上面的写入规则检查。

类型推导场景不同：如果左侧没有写出完整类型结构，默认推导为不可变绑定；只有 `_ value!` 或等价的可变解构绑定能对推导结果的最外层追加 `!`。推导表达式保留自然类型，不自动解引用 `T^` / `T&`；只有显式 expected type、运算符、函数参数等上下文需要值类型时才触发自动解引用。也就是说，推导可以得到“这个绑定本身可变”，但不能凭空把推导类型内部的数组元素、tuple 元素、union payload 或 struct 字段改成可变。内部层级需要可变时，必须显式写出左侧类型。

解构语法可以为每个解构出来的绑定重新指定可变性，因为解构本质上是在声明多个局部绑定。但这种可变性也只作用于对应元素类型的最外层，不能深入修改该元素类型的内部层级：

```jiang
(Int, User) pair = (1, user);
(_ count!, _ current_user!) = pair; // count 和 current_user 两个绑定本身可变

(Int[3]) arrays = ([1, 2, 3]);
(_ inferred_array!) = arrays; // 只能让 inferred_array 这个外层绑定可变
```

内部成员的可变性来自类型定义本身。对于 `struct` 字段、tuple 元素、union payload、数组元素，只要成员类型在其定义处是可变的，该成员就可以通过 owner 或 `T&` 引用写入；这不受外层变量本身是否带 `!` 影响，也不由引用类型提供额外的只读/独占权限。

```jiang
struct User {
    Int id;
    Int! age;
}

User user = User(id: 1, age: 18);
user.age = 19; // 允许：age 字段自身是可变字段
user.id = 2;   // 编译错误：id 字段自身不可变

User& ref = user$.ref();
ref.age = 20; // 允许：T& 不拥有 User，但可以写入 User 内部声明为 ! 的字段
```

数组、tuple、union 和 struct 也遵循同一条分层规则：外层变量或引用 slot 的可变性只控制该 slot 是否可整体重赋值；成员或元素能否被修改，由成员或元素类型自己的可变性决定。`T&` 不提供数据竞争保护，也不阻止写入内部 `!` 成员。

## 指针、引用、数组和 Slice

Jiang 不引入 shared/mutable alias borrow checker，但会检查所有权、lifetime 和 drop safety。
这里先固定 pointer/reference 的目标语义：

- `T^`：`Box<T>` 的语法糖，表示 owning pointer；它不是 C 风格 raw pointer。
- `T&`：非 owning 引用，不表达释放职责。通过 `T&` 可以读写目标内部声明为 `!` 的成员。
- `T&!`：可重绑定的引用 slot，slot 中保存的是 `T&`。它允许引用变量/字段改指向，但不改变目标对象的所有权。
- `T*`：裸指针，只用于 FFI / ABI / 低层 capability 场景。
- `T[*]`：many pointer，可下标访问，不表达单对象 ownership。
- `T[]`：`Slice<T>` 的语法糖，表示 unsized array type，必须通过 `T[]&` 形成 borrowed slice view，或通过 `T[]^` 形成 owning handle。
- `T[*:0]`：sentinel many pointer，不带 length，但类型语义保证能扫描到 sentinel。
- `T[:0]`：带 sentinel 的 unsized array type，必须通过 `T[:0]&` 形成 sentinel slice view，或通过 `T[:0]^` 形成 owning handle；sentinel view 保证 `data[length] == 0`。

标准库 `Vector<T>.slice()` 返回借用 view；`Vector<T>.into_slice(Self self)` 消耗 receiver，
并把 initialized 区间转移为 owning `T[]^`。

`T&`、`T&!` 和 `T[]&` 可以作为字段；它们不拥有目标对象，字段析构时不会释放目标对象。存储 `T&` / `T&!` / `T[]&` 字段时，目标对象的生命周期必须覆盖包含该字段的值。裸 `T[]` 是 unsized array type，不能作为普通字段类型。

Jiang 不通过引用类型系统保证 data-race freedom。多个线程或多个引用同时访问同一对象并写入
`!` 成员时，语言类型系统不做排他性证明；并发安全必须通过标准库的 mutex、rwlock、atomic、
channel 或用户协议保证。

`^` 和 `&` 会创建新的 language handle 外层。一个完整源码类型中最多只能出现一个 `^`
或 `&` 外层；源码中不允许写出 `^^`、`&&`、`^&` 或 `&^`。`T*`、`T[*]` 和 `T[]&`
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

Int&! ref = value$.ref();
_ copied = ref;      // 推导上下文保留 reference，copied: Int&
_ mutable! = ref;    // mutable: Int&!
Int copied_value = ref$.get();
Int& kept = ref;     // expected type 是 Int&，保留 reference
_ raw_ref = ref$.ref(); // '$' 阻止自动解引用，raw_ref: Int&
```

Jiang 没有前缀手动解引用语法，`*foo()` 这类写法不成立。需要显式取出 `T^` / `T&` / `T*`
指向的值时，使用隐式操作层的 `value$.get()`；需要通过 pointer/reference 写入目标对象时使用
`value$.set(new_value)`，并且 pointee 类型必须带顶层 `!`。

```jiang
Int& ref = value$.ref();
Int copied = ref$.get();
Int!* ptr = value$.ptr();
ptr$.set(42);
```

`$` 会阻止自动解引用，并进入隐式操作层。`$.ref()` 和 `$.ptr()` 分别投影到语言引用和裸指针：

```jiang
Int^ value = new Int(42);

_ ref = value$.ref(); // ref: Int&
_ ptr = value$.ptr(); // ptr: Int*
value$.dealloc();

Int sum = value + 100;        // 允许：value 自动解引用为 Int
Int bad = value$.ref() + 100; // 错误：Int& 不会在结果位置继续自动解引用
```

`T[*]` many pointer 不参与默认自动解引用。它表示一段可按元素索引的连续地址，必须通过下标表达式取元素：

```jiang
Int[*] ptr;

_ raw = ptr;    // raw: Int[*]
Int value = ptr[0];
ptr[1] = 42;

_ item_ref = ptr[1]$.ref(); // item_ref: Int&
_ item_ptr = ptr[1]$.ptr(); // item_ptr: Int*
```

数组长度是类型的一部分；slice 长度是运行时值。

## 所有权、implicit copy 和析构

目标规则：Jiang 不引入完整 alias borrow checker，但必须把资源释放、自动析构、隐式复制和
显式 move 的边界固定下来。当前 borrow check 已经作为 MIR 后的必经阶段，用于检查
move/use-after-move、引用逃逸和 drop safety；它不检查 shared/mutable aliasing，
也不负责 data-race freedom。

所有权类型：

- `T^` 是 owning pointer。它拥有指向的堆对象，并参与自动析构。
- `T&` 是非 owning 引用。它不拥有资源，不参与自动析构。
- `T[*]`、`T*` 是低层指针；`T[]&` 是 slice reference。它们不表达所有权，不参与自动析构。裸 `T[]` 是 unsized array type，不是可独立存放的 reference value。

自动析构规则：

- 只有 `Movable` 类型会自动 drop。
- `T^` 是内建 `Movable`，drop 时先 drop pointee，再释放其堆存储。
- nominal、tuple、array、optional、errorable 作为值拥有自己的字段、元素或 payload；
  如果内部类型需要 drop，外层按结构递归 drop。
- `T&`、`T*`、`T[*]` 本身不拥有目标对象，不会因为 element type 是 `Movable`
  就自动 drop。
- `T[]&` 本身不拥有整段 buffer，drop slice reference 时不 drop 全部元素；但 `slice[i]`
  是一个已初始化 `T` place，覆盖该元素时按 `T` 的 drop 规则处理旧值。
- 经过 `T*` / `T[*]` 得到的 place 是低层裸指针派生 place，写入时是 raw write，
  不隐式 drop 旧值。
- 如果 nominal 有自定义 `deinit`，它必须声明 `Movable`；drop 该 nominal 时先执行
  自定义 `deinit`，再递归 drop 字段。

示例：

```jiang
struct Node: Movable {
    Node^ next;      // 自动析构
    UInt8[*] bytes;  // 不自动析构
    Int length;

    deinit() {
        bytes$.dealloc(); // 只有自定义 deinit 会释放 many pointer
    }
}
```

析构顺序：

- 同一个 nominal 内，自动析构的字段按字段声明逆序执行。
- 自定义 `deinit` 发生在自动递归字段析构之前。
- 已经被显式 move 的局部变量不再参与析构。

implicit copy / Movable 规则：

- 普通 `struct`、`union` 默认可以隐式 copy。
- `T^` 是内建 Movable，不能隐式 copy，转移所有权必须写 `$.move()`。
- 显式声明 `Movable` 的 nominal type 永远不能隐式 copy，转移所有权必须写 `$.move()`。
- `T&`、`T&!`、`T[*]`、`T*`、`T[]&` 是 non-owning view，字段中包含这些类型不影响 implicit copy。
- nominal type 直接或间接包含 `Movable` 字段时，必须显式声明 `Movable`。
- 定义了自定义 `deinit` 的 nominal type 必须显式声明 `Movable`。
- 泛型参数只有声明 `T: !Movable` bound 时，才能在泛型代码里按 implicit copy 使用。
- 禁止隐式 copy 的类型如果确实需要复制，必须由类型作者手动实现 copy/clone 语义。实现可以选择深拷贝、共享引用计数或直接禁止复制。
- 存在自定义 copy/clone 不会恢复隐式 copy；调用方必须显式调用该方法。

```jiang
struct Point {
    Int x;
    Int y;
}

Point p2 = p1; // 允许：普通值类型

struct Buffer: Movable {
    UInt8^ data;
    Int length;
}

Buffer b2 = b1;        // 编译错误：包含 owning pointer 字段，禁止隐式 copy
Buffer b3 = b1$.move(); // 允许：显式转移所有权
```

显式 move：

- `value$.move()` 会把变量的位级内容转交给新的目标位置。
- move 后，源变量进入失效状态，后续不能读取、写入、调用方法或再次 move。
- move 后的源变量离开作用域时不会调用 `deinit`。
- move 的目标变量成为新的有效值，后续按普通局部变量规则参与析构。

```jiang
Buffer a = Buffer();
Buffer b = a$.move();

a.length; // 编译错误：a 已经 move
// 作用域结束时只析构 b，不析构 a
```

这套规则只解决所有权转移、析构和悬垂引用安全，不等同于完整 alias borrow checker。
`T&` 不表达只读或独占访问，也不用于静态防止数据竞争。

生命周期约束使用 `@life(...)` leading annotation 表达。`@life(a > b)` 表示 `a` 的目标 lifetime 必须 outlive `b`；`>` 只表示 outlives，不表示值比较或依赖方向。`@life` 与 `@where` 分离：`@where` 只描述类型、trait 和 associated type 约束，`@life` 只描述引用 lifetime 约束。

常用 lifetime 名：

- `self`：当前 `struct` / `union` 实例 lifetime。
- `return`：函数返回值 lifetime。
- 参数名：参数或参数引用目标 lifetime。
- 字段名：该字段引用目标 lifetime。

带 `T&` / `T&!` / `T[]&` 字段的类型需要表达字段目标必须覆盖包含者：

```jiang
@life(data > self)
struct Slice {
    UInt8[]& data;
    Int len;
}
```

返回引用只来自同一种输入来源时，该来源 lifetime 默认覆盖当前函数返回值，不需要额外写
`@life(source > return)`。例如只返回 `self` 内部字段，或只返回同一个参数引用，都可以使用默认规则。
如果返回引用可能来自多种输入来源，必须显式写出所有允许来源。函数显式写了 return lifetime
约束后，borrow check 只允许标注中的来源。

跨函数调用、返回含引用字段的值、或把来源关系写入 public API 时，仍建议显式表达返回值不超过来源：

```jiang
@life(input > return)
UInt8& first(UInt8& input);

@life(buffer > return)
Slice make_slice(Buffer& buffer);
```

第一版 lifetime 检查目标是防悬垂：局部引用不能逃出其来源 owner 的有效范围；
owner 被 move/drop/free 后，依赖它的引用不能继续使用；跨函数和存储到类型字段的关系通过
`@life` 检查。Jiang 不做 shared/mutable alias borrow checking。

## 隐式操作层

`$` 用于进入值或类型的隐式操作层。

已确定操作：

- `value$.as(Type)`：强制类型转换，不保证类型安全。
- `value$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`。
- `value$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `T*`。
- `value$.get()`：显式解引用 `T^` / `T&` / `T*`，返回指向的值；`T[*]` many pointer 必须使用下标访问。
- `value$.set(new_value)`：显式写入 `T!*` 指向的单个目标对象；`T*` 不允许写入。
- `value$.move()`：显式转交当前变量的值，源变量随后失效且不再析构。
- `value$.addr()`：获取地址值。
- `value$.dealloc()`：释放默认堆分配器上的对象。
- `optional$.some()`：强制解包 optional。
- `Type$.size()`：类型大小。
- `Type$.align()`：ABI 对齐。
- `Type$.max_align()`：默认分配器保证支持的最大对齐。
- `Type$.alloc()`：分配一个未初始化元素，返回 `Type![*]`。
- `Type$.alloc(n)` / `Type$.alloc_many(n)`：分配 `n` 个元素。

安全类型转换优先用类型初始化形式，例如 `Int(value)`；`$.as()` 保留为底层强制转换。

当前 package 默认处在全局 unsafe 模式。隐式操作层的低层操作，尤其是裸指针转换、裸指针取值、
地址获取和显式释放，不因为缺少 unsafe/capability gate 而报错；borrow check 仍然只检查所有权、
lifetime 和 drop safety。

未来如果引入 capability 系统，`$` 会成为受编译期 capability 约束的低层操作层。每个 `$` 操作都需要
对应能力；缺少能力时编译失败。

初步分类：

- 总是安全或低风险的编译期查询：`Type$.size()`、`Type$.align()`、`Type$.max_align()`。
- 类型系统强制操作：`optional$.some()`，后续需要定义失败时的诊断、trap 或静态证明规则。
- 需要低层内存能力：`value$.ptr()`、`value$.addr()`、`value$.dealloc()`、`Type$.alloc()`、`Type$.alloc(n)`、`Type$.alloc_many(n)`。
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
- `union`
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

`ComptimeValue` 只存在于 sema、interface loading/building 和 HIR->MIR lowering 之前。标量 const
在 MIR 中降成 `MirConst`，枚举 case 降成整数 tag const；复合 const 整体作为运行时值使用时按需
materialize 成 readonly `MirGlobal`，initializer 用 `MirStaticValue` 表达。backend 只消费 MIR
事实，不读取 `ComptimeValue`。

const initializer 不能依赖运行时值，也不能执行 IO 或其他运行时副作用。递归 initializer 诊断为
`recursive_const_initializer`；comptime 函数调用受递归深度和 branch quota 限制，避免编译期执行失控。
const generic 参数使用 `const Type Name` 写在泛型参数列表中，例如 `struct Array<T, const Int N>` 或 `Int size<const Int N>() { N }`。const generic 名字是值层参数，可在表达式中使用；不能作为类型名使用。

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
- root file 的 `public alias` 可以重新导出一个具体 public symbol。
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

如果右侧解析为已有 namespace/type/value/member symbol，alias 会绑定到同一个 name domain，
并在 HIR 中记录目标 `DefId`。如果右侧不能解析为已有 symbol，则按 type alias 处理，右侧必须
是类型语法。

`public alias` 是 package public surface 的显式 re-export 机制。package 对外只暴露 root file
的 public namespace；root file 可以通过 `public import` 重新导出模块命名空间，也可以通过
`public alias` 重新导出某个具体符号。非 root module 的 public 声明不会自动成为 package API。

未定事项：

- ambiguous re-export 的诊断和恢复策略。
- 版本求解、lockfile 和 registry 规则。

## 函数和方法

函数一定有返回类型。无返回值使用 Unit：

```jiang
() hello() {
    return ();
}
```

函数声明示例：

```jiang
Int add(Int left, Int right) {
    return left + right;
}
```

函数参数支持默认值。带默认值的参数必须位于参数列表尾部；当前默认值只支持 literal，
并按参数的 expected type 检查：

```jiang
Int add(Int left, Int right = 1) {
    return left + right;
}
```

调用支持 C# 风格命名参数。位置参数必须出现在命名参数之前；命名参数可以重排，
也可以跳过带默认值的参数：

```jiang
add(10);
add(10, right: 20);
draw(x: 1, y: 2);
```

type check 会把 call args 重排成函数签名顺序，并把缺失参数替换成默认值。这个结果写入
`TypeCheckStore.call_args`，MIR lowering 只消费重排后的参数列表，不重新做 overload
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

## Struct、Enum、Union

`struct` 用于普通名义类型，支持类型函数、实例函数、`init` 和 `deinit`。

`enum` 表示有限命名成员集合。

`union` 是 tagged union，也属于 sum type。Jiang 的 `union` 可以复用 enum-like tag，但语义上是带 tag 的 union。

union variant 和普通类型函数/实例函数共用 `Type.member` 访问面，不能同名。

union variant 的外部可见性由外层类型是否 public 控制。

当前命名空间规则：

- module/package/import alias 使用 namespace namespace。
- 顶层类型、trait 和 associated type 使用 type namespace。
- 函数、全局变量、builtin value 和普通方法使用 value namespace。
- 字段、enum case 和 union variant 使用 member namespace。
- 每个 type namespace provider 拥有自己的 member/type/value 子 namespace，供 `Type.member`
  路径继续解析；`struct`、`enum`、`union`、builtin type 和大部分语法糖类型都属于
  type namespace provider。
- union variant 虽然底层在 member namespace，仍会和 method 的 value namespace 做额外同名冲突检查。
- `Tuple` 和 `Fn` 暂时不作为可扩展 namespace provider；后续如果需要 tuple method 或函数类型
  method，再单独冻结 lookup 和 ABI 规则。

示例：

```jiang
union Maybe<T> {
    T some;
    () none;
}
```

union variant 声明按 grammar 使用字段式写法，所有 variant 必须写出 payload 类型。
没有 payload 的 tag 使用 unit 类型：`union Maybe<T> { T some; () none; }`。

## Trait 和 Extend

`trait` 描述行为约束。

`extend` 给已有类型增加实现或方法。

示例：

```jiang
trait Equatable {
    Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
    UInt64 hash(self);
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
- 字段能否被赋值由字段类型本身决定：字段类型必须带 `!`。instance method 的 `self` 可以写入 `Self` 内部声明为 `!` 的字段。
- 第一版不需要 `mutating` 或等价标记；修改 `!` 字段是普通 instance method 能力。方法是否会修改状态属于后续 effect proposal，不进入第一版类型规则。
- 默认 `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`；`Self self`
  方法等价于传入 `value$.move()`，调用后原 receiver 失效。
- 如果 receiver 已经是 pointer/reference，`ref.method(args...)` 也等价于 `Type.method(ref, args...)`。
- `Type.method(receiver, args...)` 是显式方法调用形式；第一个实参必须匹配 receiver 类型。
- instance method 作为函数值时，显式 receiver 保留为第一个参数。例如 `Int get(self)`
  的函数值类型是 `Fn<Int, Self&>`；`Int take(Self self)` 的函数值类型是 `Fn<Int, Self>`。
  类型函数没有 receiver 参数。
- trait 可以声明没有 receiver 参数的类型函数 requirement，通过 `Type.method(args...)`
  调用，也可以在泛型约束中通过 `T.method(args...)` 调用。
  带 `self` 参数的 trait function requirement 是实例函数 requirement。
- trait 本身不是普通值类型；动态 trait view 通过 compiler-provided companion type
  表达：`Trait.Any` 和 `Trait.Receiver`。`Trait$.ref(value)` 生成 borrowed dynamic view，
  不移动原值；`Trait$.new(value)` 生成 owning dynamic view，返回 `Trait.Any^`。
  `Trait.VTable` 是 compiler-private 方法表类型，用户源码不能直接命名或传参。当前实现支持
  ref receiver method 的动态分派和 owning trait object drop，暂不支持 move receiver
  trait object dispatch。
- 泛型 receiver 的实例方法签名必须用实际 receiver type args 实例化后再检查。例如 `Box<T>.get() -> T` 在 `Box<Int!>` 上调用时，等价于 `Box.get(box&) -> Int!`；如果这个结果写入 `Int` 目标，再按上面的写入目标规则忽略顶层 mutable。
- union variant name 和同一 union 的类型函数/显式 method name 共享类型成员命名空间，
  不能重名，避免 `Union.member(...)` 歧义。
- 同名函数和同名方法允许 overload；参数数量、参数类型或默认参数可接受范围必须
  能区分调用。
- `extend Type: Trait { ... }` 当前做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现。
- `Hashable` 继承 `Equatable`；可作为 hash key 的类型必须同时定义 hash 和相等比较。
- `Movable` / `Hashable` / `Equatable` 属于 compiler core trait。std prelude 只导出同一个
  DefId；即使后续启用 no-std，它们仍然是语言核心约束。

完整 trait solving 仍需继续补齐；trait method lookup 和显式 associated type projection 已进入当前类型检查路径。

未定事项：

- trait parent 的解析和循环检查。
- associated type 的 where constraint。
- trait method lookup。
- trait conformance solving。

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
@where(T: Sequence, T.[Sequence].Element == Int, T: !Mutable, T != Box<_>)
```

类型相等/不等约束中的右侧类型可以作为形状 pattern 使用，`_` 匹配单个 type argument。
例如 `@where(T == Box<_>)` 匹配任意 boxed value，`@where(T != Option<_>)` 排除 optional。
内建后缀类型在 pattern 中按 canonical builtin type 处理：`T[]` 等价于 `Slice<T>`，`T[N]`
等价于 `Array<T, N>`。

当前 AST 使用：

- `WhereConstraint`：一条泛型约束。
- `TypeBound`：约束右侧的 bound 表达式。
- `TypeBoundIntersection`：`A & B`。

后置 `T id<T> @where(...)` 不支持。

## Optional 和 Errorable

Optional 使用 `T?` 表示，也可以显式写作 `Option<T>`。

`?` 是 `Option<T>` 的语法糖。`Int?` 表示 `Int` 值可能为空；`Int?!` 表示 optional 这一层本身可变。

Optional 不再幂等：`T??` 表示 `Option<Option<T>>`。`!` 是当前绑定或类型层的可变标记，
例如 `Int?!` 表示 optional 这一层本身可变。

已确定表达式能力：

- optional chaining: `value?.field`
- coalesce: `value ?? fallback`
- guard: `guard value is .some(payload) else { return; }`
- 强制解包: `value$.some()`
- 条件解包 pattern: `value is .some(payload)`
- 可变条件解包 pattern: `value is .some(Int! payload)`
- 借用解包 pattern: `value is .some(ref Int payload)`
- 可变借用解包 pattern: `value is .some(ref Int! payload)`

`.some(...)` / `.none` 是 `Option<T>` 的 pattern 写法。`some` 是普通标识符，
不再作为 optional pattern 关键字。`ref` 是绑定模式，不是类型名；
`ref Int! payload` 等价于 `(ref Int)! payload`，生成可变的引用绑定。

示例：

```jiang
if value is .some(payload) {
    // payload: T
}

if value is .some(Int! payload) {
    // payload: T!
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

Errorable 使用 `T@E` 表示。`@E` 是 errorable 边界，`?` 和 `!` 只能作用在 `@` 左侧的值类型层，不能作用在 errorable 类型层或错误类型顶层。

合法：

```jiang
Result@Error value;
Result?!@Error maybe_value;
Result@(T1?, T2!) tuple_error;
```

非法：

```jiang
Result@Error?;
Result@Error!;
Result@Error?!;
Result@(Error?);
Result@(Error!);
```

当前 errorable value 不做隐式传播。`T@E` 只能作为完整 errorable value 保存或返回；
如果上下文需要 `T`，必须用 `try expr catch (...) => ...` 显式处理 error 分支。
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
`tail_expr` 的 `block` 值为 `Unit`。Jiang 没有通用表达式语句，只有
`call_stmt`、赋值、控制语句和声明等明确 statement 形态可以在语句位置出现。
因此：

```jiang
Int x = {
    foo();
    1
};
```

上面的 `block` 类型为 `Int`。如果没有最后的 `tail_expr`，或者最后一个
源码元素是赋值、局部变量声明、`defer` 等语句，则 `block` 类型为 `Unit`。

语句 result type 规则：

| 语句 | result type |
| --- | --- |
| `call_stmt` | `Unit` |
| `block` | block 的 tail expr result type；无 tail expr 时为 `Unit` |
| `return expr?;` | `Never` |
| `throw expr;` | `Never` |
| `break;` | `Never` |
| `continue;` | `Never` |
| `var_decl_stmt` | `Unit` |
| `destructure_stmt` | `Unit` |
| `assign_stmt` | `Unit` |
| `defer_stmt` | `Unit` |
| `guard_stmt` | `Unit` |
| `while_stmt` | `Unit` |
| `for_stmt` | `Unit` |

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
- `T[*]`：many pointer 不是 iterable；需要遍历时使用 range 产生 index，再用 `p[i]` 访问。

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
负责选择具体 iteration plan，并把 `pattern` 的 expected type 设为 `Element`。MIR
lowering 只消费这个 plan，不在 MIR 阶段重新做 trait lookup。

## Pattern Matching

pattern 目前包括：

- literal
- variant
- optional

binding/wildcard 只作为 optional 或 variant payload 的子 pattern 使用，不能作为
`is` 或 `switch` 分支根。当前不支持 tuple pattern；tuple 解构应使用独立
destructure 语法。

`is` 用于 pattern matching，不再使用 `==` 表达 pattern 解构。

示例方向：

```jiang
if value is .some(payload) {
}

if block is .some(Int! dead) {
}
```

普通 union variant 和 optional 都使用 dot case pattern；optional 不再支持旧 `some payload` pattern。

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
- `public alias name = target;` 将一个具体 symbol 重新导出到当前模块 public namespace。
- `public` 标记声明对外可见。
- 基本类型不是关键字，由名字解析绑定到内建声明。

ambiguous re-export 仍需后续完善；package dependency 第一版只支持本地源码路径。

名称解析需要单独定稿：

- type namespace、value namespace、field namespace、variant namespace、trait/associated type namespace 是否分离。
- `foo.Bar` 在不同上下文中如何解析为 module path、type member、variant 或 field。
- import alias 和 package path 的解析顺序：`import dep;` 优先查当前 package dependency alias。
- 重复声明、shadowing 和 visibility 规则。

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
- 常规 const initializer 由 type check 后的 HIR comptime interpreter 执行。它支持 const 引用、
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
- HIR-level、MIR-level 或 backend-level plugin 不属于当前 Lang Package 设计。

## 后续提案

### API Effect

第一版不实现 API effect system。当前语言规则中，函数或方法能否写入字段只由字段类型是否带 `!` 决定；方法不需要 `mutating` 标记。

后续可以引入 `@effect(...)` 作为 API 行为契约，而不是借用类型系统的一部分。例如：

```jiang
@effect(read)
Int get() {
    return self.value;
}

@effect(write(self))
Void set(Int value) {
    self.value = value;
}

@effect(write(self), io, alloc)
Void save(File& file) {
}
```

候选 effect 包括：

- `read`：不修改 receiver、参数或 global 可见状态，不调用 unknown/write/io 函数。
- `write(self)` / `write(arg)` / `write(global)`：可能修改对应对象或状态。
- `io`：执行输入输出。
- `alloc`：分配内存。
- `unsafe`：未来 capability 系统中可用于标记低层操作；当前暂不启用检查。

未标注函数在该提案中默认为 `unknown` / impure，不强制第一版代码全量标注。若未来启用检查，`@effect(read)` 函数中写入 `self` 的 `!` 字段应编译失败；trait requirement 也可以携带 effect，要求实现不比 requirement 更“脏”。
