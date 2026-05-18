# Jiang 语言设计草案

本文档记录 Jiang 语言本身的设计，不记录编译器源码目录结构和实现细节。编译器工程约定见
`doc/architecture.md`。

当前目标是固定 stage2 / 1.0 需要依赖的语言边界：词法、语法、类型、声明、泛型和错误处理。
未定设计必须显式标注，避免 parser、resolve、sema 在隐含假设上继续扩展。

## 状态标记

本文档只记录目标设计和 stage2 当前设计差异。
为避免混淆，后续章节使用这些状态：

- **目标规则**：希望长期保留的语言规则。
- **stage2 已定义**：stage2 PEG/AST/骨架已能表达，但还没有完整语义检查。
- **stage2 缺口**：目标设计需要，但 stage2 尚未实现。
- **未定**：设计尚未冻结，不能作为 resolve/sema 的硬前提。

## 设计目标

Jiang 是面向系统编程的语言，目标是在低层控制能力、工程可维护性和高层抽象之间取得平衡。

核心方向：

- 明确的值语义、指针语义和可变性语义。
- 可读的泛型和 trait 约束。
- AST 保留源码结构，语义信息进入 resolve/sema/HIR。
- 字符串、数组、slice、指针等系统级类型有直接语法支持。
- 后续支持自定义语法，但基础语言语法必须先稳定。

## 命名规范

- 类型名使用 `PascalCase`。
- 函数名、变量名、字段名、枚举成员、模块别名使用 `snake_case`。
- 基本类型名不是关键字，词法阶段按 `ident` 处理，后续 resolver 解释为内建类型。

示例：

```jiang
struct SourceFile {
    UInt8[] file_path;
    Int start_offset;
}

UInt8[] read_source(UInt8[] file_path) {
    return file_path;
}

import store = "token_store.jiang";
```

## 词法

Token 只表示词法事实，不承载语义类型。

已确定：

- identifier、关键字和基本类型名在 token 层统一为 `ident`；后续由 `KeywordTable`
  和 resolve/sema 解释。
- 关键字集合包括 `new`、`import`、`public`、`alias`、`extern`、`return`、`if`、
  `else`、`guard`、`while`、`for`、`in`、`is`、`enum`、`union`、`struct`、`record`、`trait`、
  `extend`、`associated`、`static`、`switch`、`try`、`catch`、`break`、`continue`、
  `defer`、`throw`、`true`、`false`、`null`、`self`、`some`、`Self`。
- 字符字面量使用单引号，例如 `'a'`。
- 字符串字面量使用双引号，文本按 UTF-8 字节序列处理。
- `Span` 使用字节偏移和字节长度；line/column 在诊断阶段计算。

`Self` 是类型位置的特殊名字。`self` 是类型内部 instance method 和 constructor body 中的 contextual keyword，表示当前 receiver 或初始化目标；`static` 类型函数中没有 `self`。

## 字面量

已确定字面量：

- integer literal
- float literal
- char literal
- string literal
- bool literal: `true` / `false`
- null literal: `null`

字符字面量用于表示单个字符。`UInt8 byte = 'a';` 这类初始化由 expected type 约束；非 ASCII 字符初始化 `UInt8` 应编译失败。

字符串字面量默认是 UTF-8 字节序列，当前可用于 `UInt8[_]` / `UInt8[]`。

## 类型系统

Jiang 类型语法遵循从左往右、从里到外的原则。类型后缀越靠右，包裹范围越大。

基础类型名例如 `Int`、`UInt8`、`Bool`、`Float`、`Double`、`Char` 都是普通名字，由 resolver 解析为内建类型。

已确定类型语法：

- `_`：推断类型。
- `()`：Unit 类型。
- `T` / `foo.Bar`：命名类型。
- `T<A, B>`：泛型类型参数。
- `(A, B)`：tuple type。
- `T!`：当前类型层级的可变 flag。
- `T?`：内建 optional 类型层；optional 不是可直接命名的 `Option<T>` 普通泛型类型。
- `T?!`：optional 类型层可变的规范写法。
- `T^`：自动解引用的 owning heap pointer，通常来自 `new T(...)`；它不是 C 风格 raw pointer。
- `T&`：自动解引用的非 owning 引用，不表达释放职责。
- `T[]`：slice reference。
- `T[*]`：many pointer，不默认自动解引用，只能通过下标访问元素。
- `RawPointer<T>`：裸指针，供 FFI / ABI / 低层能力使用，不使用语言级 owning pointer 语法表示。
- `T[N]`：定长数组。
- `T[_]`：数组长度由初始化器推断。
- `T@E`：errorable。
- `@E` 后的错误类型顶层不能带 `?` 或 `!`；errorable 类型层本身也不能再追加 `?` 或 `!`。如果值类型需要 optional/mutable，必须写在 `@E` 前，例如 `T?!@E`。
- 由于 `(T)` 与 `T` 等价，`T@(E?)` 与 `T@E?` 等价；非法原因仍然是 `@` 后错误类型顶层不能带 `?`。

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

`?` / `!` 作用在当前类型层级，并且都是 type flag。同一类型层级中，每种 flag 最多出现一次；源码里重复写出时必须报 diagnostic。类型归一化阶段如果因为泛型替换或别名展开得到重复 flag，按幂等规则合并。

`^` / `&` 会基于左侧已经形成的类型创建新的 pointer/reference 外层。创建出的外层也可以继续带 `?` / `!`，但一个完整源码类型中最多只能出现一个 pointer/reference 层；因此不支持 `T^^`、`T&&`、`T^&` 或 `T&^` 这类 handle 的 handle。类型归一化阶段如果得到重复 handle 层，按当前同类 handle 合并；如果同时出现 `^` 和 `&`，必须保留错误状态并报告 diagnostic，不能静默选择一种。

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

`T!&` 不作为目标语言类型存在。Jiang 的引用类型不表达 Rust 式“可变借用”或独占访问；`T&!` 中的 `!` 属于 reference 外层，只表示引用 slot 本身可重绑定。

`Int!?`、`Int^!?`、`Int^^`、`Int&&`、`Int^&` 都是语法错误。编译器可以按规范顺序恢复后继续解析，但必须报告 diagnostic。

如果同一层级同时出现 `?` 和 `!`，源码只能写成 `T?!`：

```jiang
Int?! value; // optional 层可变
```

`T!?` 是语法错误，编译器可以恢复为 `T?!` 继续解析，但必须报告 diagnostic。

重复可变标记是语法错误。编译器恢复时可以把 `T!!` 当作 `T!` 继续解析，但必须报告 diagnostic。只要重复发生在同一类型层级内，即使中间夹着 `?` 也要报错，例如 `T!?!`。

pointer/reference 外层只能是 `^` 或 `&` 之一，不能写成 `T^&` 或 `T&^`。

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

类型推导场景不同：如果左侧没有写出完整类型结构，默认推导为不可变绑定；只有 `_!` 或等价的可变解构绑定能对推导结果的最外层追加 `!`。推导表达式保留自然类型，不自动解引用 `T^` / `T&`；只有显式 expected type、运算符、函数参数等上下文需要值类型时才触发自动解引用。也就是说，推导可以得到“这个绑定本身可变”，但不能凭空把推导类型内部的数组元素、tuple 元素、union payload 或 record 字段改成可变。内部层级需要可变时，必须显式写出左侧类型。

解构语法可以为每个解构出来的绑定重新指定可变性，因为解构本质上是在声明多个局部绑定。但这种可变性也只作用于对应元素类型的最外层，不能深入修改该元素类型的内部层级：

```jiang
(Int, User) pair = (1, user);
(_! count, _! current_user) = pair; // count 和 current_user 两个绑定本身可变

(Int[3]) arrays = ([1, 2, 3]);
(_! inferred_array) = arrays; // 只能让 inferred_array 这个外层绑定可变
```

内部成员的可变性来自类型定义本身。对于 `struct` / `record` 字段、tuple 元素、union payload、数组元素，只要成员类型在其定义处是可变的，该成员就可以通过 owner 或 `T&` 引用写入；这不受外层变量本身是否带 `!` 影响，也不由引用类型提供额外的只读/独占权限。

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

数组、tuple、union、record 也遵循同一条分层规则：外层变量或引用 slot 的可变性只控制该 slot 是否可整体重赋值；成员或元素能否被修改，由成员或元素类型自己的可变性决定。`T&` 不提供数据竞争保护，也不阻止写入内部 `!` 成员。

## 指针、引用、数组和 Slice

现阶段不引入 Rust 式 borrow checker，但先固定 pointer/reference 的目标语义：

- `T^`：owning heap pointer，通常来自 `new T(...)`，拥有堆上对象；它不是 C 风格 raw pointer。
- `T&`：非 owning 引用，不表达释放职责。通过 `T&` 可以读写目标内部声明为 `!` 的成员。
- `T&!`：可重绑定的引用 slot，slot 中保存的是 `T&`。它允许引用变量/字段改指向，但不改变目标对象的所有权。
- `RawPointer<T>`：裸指针，只用于 FFI / ABI / 低层 capability 场景。
- `T[*]`：many pointer，可下标访问，不表达单对象 ownership。
- `T[]`：slice reference，语义上类似 `{ T[*], length }&` 的连续内存引用视图；它不表达所有权。

`T&`、`T&!` 和 `T[]` 可以作为字段；它们不拥有目标对象，字段析构时不会释放目标对象。存储 `T&` / `T&!` / `T[]` 字段时，目标对象的生命周期必须覆盖包含该字段的值。

Jiang 不通过引用类型系统保证 data-race freedom。多个线程或多个引用同时访问同一对象并写入 `!` 成员时，语言类型系统不做 Rust 式排他性证明；并发安全必须通过标准库的 mutex、rwlock、atomic、channel 或用户协议保证。

`^` 和 `&` 会创建新的 pointer/reference 外层。一个完整源码类型中最多只能出现一个 pointer/reference 外层；源码中不允许写出 `^^`、`&&`、`^&` 或 `&^`。归一化阶段如果因为泛型替换得到重复同类 handle，可以合并；如果得到 `^` 与 `&` 混合的 handle 层，必须保留错误状态并报告 diagnostic。

`T^` 和 `T&` 在普通值上下文中默认自动解引用。只有 expected type 本身就是同一个 pointer/reference 类型时，表达式才保留 pointer/reference 层：

```jiang
Int^ foo();

Int^ a = foo();      // expected type 是 Int^，保留 owning pointer
_ b = foo();         // 推导上下文保留自然类型，b: Int^
Int c = foo();       // expected type 是 Int，自动解引用
_ d = foo() + 123;   // 算术上下文，自动解引用为 Int

Int&! ref = value$.ref();
_ copied = ref;      // 推导上下文保留 reference，copied: Int&
_! mutable = ref;    // mutable: Int&!
Int copied_value = ref; // expected type 是 Int，自动解引用
Int& kept = ref;     // expected type 是 Int&，保留 reference
_ raw_ref = ref$.ref(); // '$' 阻止自动解引用，raw_ref: Int&
```

Jiang 没有前缀手动解引用语法，`*foo()` 这类写法不成立。需要显式取出 `T^` / `T&` 指向的值时，使用隐式操作层的 `value$.get()`：

```jiang
Int& ref = value$.ref();
Int copied = ref$.get();
```

`$` 会阻止自动解引用，并进入隐式操作层。`$.ref()` 和 `$.ptr()` 分别投影到语言引用和裸指针：

```jiang
Int^ value = new Int(42);

_ ref = value$.ref(); // ref: Int&
_ ptr = value$.ptr(); // ptr: RawPointer<Int>
value$.free();

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
_ item_ptr = ptr[1]$.ptr(); // item_ptr: RawPointer<Int>
```

数组长度是类型的一部分；slice 长度是运行时值。

## 所有权、Copy 和析构

目标规则：Jiang 不引入完整 Rust 式 borrow checker，但必须把资源释放、自动析构、隐式复制和显式 move 的边界固定下来。

所有权类型：

- `T^` 是 owning pointer。它拥有指向的堆对象，并参与自动析构。
- `T&` 是非 owning 引用。它不拥有资源，不参与自动析构。
- `T[*]`、`RawPointer<T>` 是低层指针；`T[]` 是 slice reference。它们不表达所有权，不参与自动析构。

自动析构规则：

- 局部变量离开作用域时，如果变量类型有 `deinit`，编译器自动调用该 `deinit`。
- `T^` 本身是内建资源类型；离开作用域时自动析构指向的 `T`，并释放其堆存储。
- struct 的 `T^` 字段会自动析构，无论该 struct 是否实现了自定义 `deinit`。
- `T&`、`T&!`、`T[*]`、`RawPointer<T>`、`T[]` 字段不会被编译器自动释放。需要释放这些资源时，必须由类型作者在自定义 `deinit` 中显式处理。
- 如果 struct 有自定义 `deinit`，先执行自定义 `deinit`，再执行编译器生成的 `T^` 字段析构。这样自定义 `deinit` 仍然可以读取 owning 字段。
- 自动字段析构只认 `T^`。是否级联调用非 `T^` 字段类型自己的 `deinit`，不作为默认规则；外层类型需要释放这类字段时，应在自定义 `deinit` 中显式调用。

示例：

```jiang
struct Node {
    Node^ next;      // 自动析构
    UInt8[*] bytes;  // 不自动析构
    Int length;

    deinit() {
        bytes$.free(); // 只有自定义 deinit 会释放 many pointer
    }
}
```

析构顺序：

- 同一个 struct 内，自动析构的 `T^` 字段按字段声明逆序执行。
- 自定义 `deinit` 发生在自动 `T^` 字段析构之前。
- 已经被显式 move 的局部变量不再参与析构。

Copy 规则：

- 没有指针字段、没有资源语义的普通值类型可以隐式 copy。
- 指针和引用视图类型以及直接或间接包含这类字段的 struct 默认禁止隐式 copy。这里包括 `T^`、`T&`、`T&!`、`T[*]`、`RawPointer<T>` 和 `T[]`。
- 禁止隐式 copy 的类型如果确实需要复制，必须由类型作者手动实现 copy/clone 语义。实现可以选择深拷贝、共享引用计数或直接禁止复制。
- 存在自定义 copy/clone 不会恢复隐式 copy；调用方必须显式调用该方法。

```jiang
struct Point {
    Int x;
    Int y;
}

Point p2 = p1; // 允许：普通值类型

struct Buffer {
    UInt8[*] data;
    Int length;
}

Buffer b2 = b1;        // 编译错误：包含指针字段，禁止隐式 copy
Buffer b3 = b1.copy(); // 允许：如果 Buffer 显式实现 copy
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

这套规则只解决所有权转移、析构和悬垂引用安全，不等同于 Rust 式 borrow checker。`T&` 不表达只读或独占访问，也不用于静态防止数据竞争。

生命周期约束使用 `@life(...)` leading annotation 表达。`@life(a > b)` 表示 `a` 的目标 lifetime 必须 outlive `b`；`>` 只表示 outlives，不表示值比较或依赖方向。`@life` 与 `@where` 分离：`@where` 只描述类型、trait 和 associated type 约束，`@life` 只描述引用 lifetime 约束。

常用 lifetime 名：

- `self`：当前 `struct` / `record` / `union` 实例 lifetime。
- `return`：函数返回值 lifetime。
- 参数名：参数或参数引用目标 lifetime。
- 字段名：该字段引用目标 lifetime。

带 `T&` / `T&!` / `T[]` 字段的类型需要表达字段目标必须覆盖包含者：

```jiang
@life(data > self)
struct Slice {
    UInt8[] data;
    Int len;
}
```

返回引用或返回含引用字段的值时，需要表达返回值不超过来源：

```jiang
@life(input > return)
UInt8& first(UInt8& input);

@life(buffer > return)
Slice make_slice(Buffer& buffer);
```

第一版 lifetime 检查目标是防悬垂：局部引用不能逃出其来源 owner 的有效范围；owner 被 move/drop/free 后，依赖它的引用不能继续使用；跨函数和存储到类型字段的关系通过 `@life` 检查。Jiang 不做 Rust 式 shared/mutable alias borrow checking。

## 隐式操作层

`$` 用于进入值或类型的隐式操作层。

已确定操作：

- `value$.as(Type)`：强制类型转换，不保证类型安全。
- `value$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`。
- `value$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `RawPointer<T>`。
- `value$.get()`：显式解引用 `T^` / `T&`，返回指向的值；`T[*]` many pointer 必须使用下标访问。
- `value$.move()`：显式转交当前变量的值，源变量随后失效且不再析构。
- `value$.addr()`：获取地址值。
- `value$.free()`：释放默认堆分配器上的对象。
- `optional$.some()`：强制解包 optional。
- `Type$.size()`：类型大小。
- `Type$.align()`：ABI 对齐。
- `Type$.max_align()`：默认分配器保证支持的最大对齐。
- `Type$.alloc()`：分配一个未初始化对象。
- `Type$.alloc_array(n)`：分配数组。

安全类型转换优先用类型初始化形式，例如 `Int(value)`；`$.as()` 保留为底层强制转换。

未来规划：隐式操作层会纳入 capability 系统。`$` 不是绕过语言规则的普通后门，而是进入受编译期 capability 约束的低层操作层。每个 `$` 操作都需要对应能力；缺少能力时编译失败。

初步分类：

- 总是安全或低风险的编译期查询：`Type$.size()`、`Type$.align()`、`Type$.max_align()`。
- 类型系统强制操作：`optional$.some()`，后续需要定义失败时的诊断、trap 或静态证明规则。
- 需要低层内存能力：`value$.ptr()`、`value$.addr()`、`value$.free()`、`Type$.alloc()`、`Type$.alloc_array(n)`。
- 需要 unsafe/cast 能力：`value$.as(Type)`。

编译器自身源码包默认拥有全部 capability，这是编译器实现的特殊配置，不代表普通 Jiang 包默认拥有这些能力。普通包默认应采用最小能力集合，并通过显式配置或受控上下文获得额外能力。

## 声明

顶层声明包括：

- `import name;`
- `import alias = "path.jiang";`
- `public import name;`
- `public import alias = "path.jiang";`
- `alias Name = Type;`
- global declaration: `Type name = expr;`
- function declaration / definition
- `struct`
- `record`
- `enum`
- `union`
- `trait`
- `extend`

目标语言支持 `public import`，用于 re-export 被导入模块的 public API。

### Import

stage2 当前固定两种 import path：

```jiang
import dep;
import dep = "foo/bar.jiang";
```

`import dep;` 中的 `dep` 是 module/package 名称，不是文件路径。它通过当前编译上下文中已登记
的 module/package 名称解析；后续接入 package manifest 后，跨 package dependency 也走这条规则。

`import dep = "foo/bar.jiang";` 中的字符串是显式文件路径。路径按 Zig 风格解析：相对路径以
当前 import 所在源文件的目录为基准，绝对路径按原路径规范化。编译器只加载字面路径
本身，不隐式补 `.jiang`，也不尝试目录入口 `mod.jiang`。

如果当前 source 是 virtual/buffer，没有真实文件路径，相对 file import 暂按字符串本身规范化；
后续如果需要 IDE buffer 的相对文件 import，需要给 virtual source 增加 base directory。

import 只引入一个模块命名空间 alias，不把目标模块的声明平铺到当前 namespace。被导入模块
的 public API 通过 `dep.Name` 访问。`public import` re-export 的也是这个模块命名空间 alias。

### Alias

stage2 当前只定义 type alias：

```jiang
alias Name = Type;
```

type alias 在 resolve 中绑定到 type namespace。右侧必须是类型语法，不能是普通 value
表达式或 module namespace。

通用 alias 暂不冻结。后续如果需要支持 value/module/member alias，仍使用
`alias name = target;` 形式，再明确 target 推断、声明收集和循环依赖规则。

在通用 alias 语义冻结前，`alias Name = Type;` 一律按 type alias 理解。

未定事项：

- ambiguous re-export 的诊断和恢复策略。
- 跨 package import 的路径解析和可见性边界。

## 函数和方法

函数一定有返回类型。无返回值使用 Unit：

```jiang
() hello() {
    return ();
}
```

Jiang 目标语言不支持函数参数标签和默认参数。函数参数按定义顺序进行位置匹配，调用参数也必须按位置提供。

目标规则：resolve/sema 不基于参数标签或默认参数设计调用匹配规则。

函数声明示例：

```jiang
Int add(Int left, Int right) {
    return left + right;
}
```

泛型函数：

```jiang
@where(T: Numeric)
T add<T>(T left, T right);
```

`init` / `deinit` 是目标语言的一部分。`init(...)` 定义构造函数，
`deinit()` 定义析构逻辑；构造 sugar 使用 `Type(...)`，堆分配构造使用 `new Type(...)`。

## Struct、Record、Enum、Union

`struct` 用于普通名义类型。

`record` 是更偏数据记录的结构体形式。record 与 struct 的完整语义差异仍需定稿。

`enum` 表示有限命名成员集合。

`union` 是 tagged union，也属于 sum type。Jiang 的 `union` 可以复用 enum-like tag，但语义上是带 tag 的 union。

union 的 public 规则类似 record：字段和 variant 的外部可见性由外层类型是否 public 控制。

示例：

```jiang
union Maybe<T> {
    some(T),
    none
}
```

目标语法采用 enum-like variant 形式：`union Maybe<T> { some(T), none }`。

## Trait 和 Extend

`trait` 描述行为约束。

`extend` 给已有类型增加实现或方法。

示例：

```jiang
trait Equatable {
    static Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
    UInt64 hash();
}

trait Indexable {
    Int to_index();
    static Self from_index(Int index);
}
```

当前规则：

- method 不进入模块顶层命名空间；它们记录在 extend/method side table 中。
- 类型内部非 `static` 函数是 instance method，拥有隐式 receiver `self: Self&`。`self` 是 method body 中的 contextual keyword。
- `static` 类型函数没有 receiver，函数体中不能使用 `self`。
- `init(...)` 是 constructor，拥有初始化中的 `self` 目标；`self` 在 `init` body 中表示正在初始化的 `Self` storage。`init` 只能通过 `Type(...)` / `new Type(...)` 调用，不作为普通函数值暴露。
- 字段能否被赋值由字段类型本身决定：字段类型必须带 `!`。instance method 的 `self` 可以写入 `Self` 内部声明为 `!` 的字段。
- 第一版不需要 `mutating` 或等价标记；修改 `!` 字段是普通 instance method 能力。方法是否会修改状态属于后续 effect proposal，不进入第一版类型规则。
- `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`。
- 如果 receiver 已经是 pointer/reference，`ref.method(args...)` 也等价于 `Type.method(ref, args...)`。
- `Type.method(receiver, args...)` 是显式方法调用形式；第一个实参必须匹配 receiver reference。
- instance method 作为函数值时，隐式 receiver 展开为第一个参数。例如 `Int get()` 的函数值类型是 `Fn<Int, Self&>`，`Void set(Int value)` 的函数值类型是 `Fn<Void, Self&, Int>`。`static` 函数值没有 receiver 参数。
- trait 可以声明 static function requirement；static requirement 没有 `self`，通过
  `Type.method(args...)` 调用，也可以在泛型约束中通过 `T.method(args...)` 调用。非 `static` trait function requirement 隐含 `Self&` receiver。
- 泛型 receiver 的实例方法签名必须用实际 receiver type args 实例化后再检查。例如 `Box<T>.get() -> T` 在 `Box<Int!>` 上调用时，等价于 `Box.get(box&) -> Int!`；如果这个结果写入 `Int` 目标，再按上面的写入目标规则忽略顶层 mutable。
- union variant name 和同一 union 的 static/显式 method name 共享类型成员命名空间，不能重名，避免 `Union.member(...)` 歧义。
- 同名函数和同名方法允许 overload；参数数量或参数类型必须不同。
- `extend Type: Trait { ... }` 当前做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现。
- `Hashable` 继承 `Equatable`；可作为 hash key 的类型必须同时定义 hash 和相等比较。

完整 trait solving、trait method lookup、associated type projection 仍需单独定稿。

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

支持 associated type equality constraint；相等关系使用 `==`，不使用赋值语义的 `=`：

```jiang
@where(T: Iterator, T.Item == Int)
```

当前 AST 使用：

- `WhereConstraint`：一条泛型约束。
- `TypeBound`：约束右侧的 bound 表达式。
- `TypeBoundIntersection`：`A & B`。

后置 `T id<T> @where(...)` 不支持。

## Optional 和 Errorable

Optional 使用 `T?` 表示。

`?` 是语言内建 optional 类型层，不暴露为可直接命名的 `Option<T>` 普通泛型类型。`Int?` 表示 `Int` 值可能为空；`Int?!` 表示 optional 这一层本身可变。

重复 optional 标记是语法错误。编译器恢复时可以把 `T??` 当作 `T?` 继续解析，但必须报告 diagnostic。只要重复发生在同一类型层级内，即使中间夹着 `!` 也要报错，例如 `T?!?`。`!` 与 `?` 一样属于当前类型层级的 type flag，`Int?!` 表示 optional 层本身可变。

这个规则只针对源码中直接写出的 type flag。类型归一化阶段如果因为泛型替换得到重复 optional，`?` 按幂等规则合并：

```jiang
// 假设 T 实例化为 Int?
T? value; // 归一化为 Int?
```

已确定表达式能力：

- optional chaining: `value?.field`
- coalesce: `value ?? fallback`
- guard: `guard value is some payload else { return; }`
- 强制解包: `value$.some()`
- 条件解包 pattern: `value is some payload` 或 `value is some _ payload`
- 可变条件解包 pattern: `value is some _! payload`

`some` 是 optional pattern 位置的 contextual keyword，类似 `init` 在初始化声明中的特殊角色；它不是普通类型名，也不是 `Option.some` 这种公开 union variant。`some` 后面接普通 binding pattern：`some payload` 是 `some _ payload` 的简写，`some _! payload` 表示可变绑定。`!` 仍然属于 binding pattern 的类型部分，不挂在 `some` 关键字上。

示例：

```jiang
if value is some payload {
    // payload: T
}

if value is some _ payload {
    // payload: T
}

if value is some _! payload {
    // payload: T!
}

switch value {
    some payload => ...
    some _ payload => ...
    some _! payload => ...
    null => ...
}
```

同一个 optional match/switch 层级中，不同 `some` 分支匹配范围相同，只是绑定形式不同，因此只能出现一个 `some` 分支，同时出现是编译错误。

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

完整 errorable 类型检查和传播规则仍需在 sema 阶段定义。

未定事项：

- `throw expr` 的类型。
- catch binding 的作用域和类型。
- 未捕获错误如何向外传播。

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

`stmt` 和 `expr` 在语法上保持分离，但每条 `stmt` 在类型系统中都有
result type。`block` 内只允许语句，不存在独立的 tail expression。表达式语句
必须写 `;`。`block` 作为表达式使用时，其值等于最后一条语句的值；空
`block` 的值为 `Unit`。因此：

```jiang
Int x = {
    foo();
    1;
};
```

上面的 `block` 类型为 `Int`。如果最后一条语句是赋值、局部变量声明、
`defer` 等无值语句，则 `block` 类型为 `Unit`。

语句 result type 规则：

| 语句 | result type |
| --- | --- |
| `expr;` | `expr` 的类型 |
| `block` | block 最后一条语句的 result type；空 block 为 `Unit` |
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

非最后一条语句的 result value 会被隐式丢弃。`Never` 表示该语句不会正常
继续执行，可以在分支类型统一时转换为任意目标类型。`return`、`throw`、
`break`、`continue` 仍然是语句，不属于普通表达式语法。

`guard expr else { ... }` 用于提前退出并把 pattern 绑定带到后续作用域。
`else` block 必须非空，且最后一条语句必须是 `return`、`break`、`continue`
或 `throw`。更复杂的“所有分支都退出”由后续控制流分析处理。

`defer` 在当前块退出时按 LIFO 顺序执行。`defer` 内不支持 `return`、`break`、`continue`。

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
if value is some payload {
}

if block is some _! dead {
}
```

`some` 只用于 optional pattern。普通 union variant 仍然使用 variant pattern，不复用 optional 的 `some` 语法。

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
- `public` 标记声明对外可见。
- 基本类型不是关键字，由名字解析绑定到内建声明。

ambiguous re-export 和 package path 解析仍需后续完善。

名称解析需要单独定稿：

- type namespace、value namespace、field namespace、variant namespace、trait/associated type namespace 是否分离。
- `foo.Bar` 在不同上下文中如何解析为 module path、type member、variant 或 field。
- import alias 和 package path 的解析顺序。
- 重复声明、shadowing 和 visibility 规则。

## 自定义语法

Jiang 后续要支持自定义语法。当前原则：

- lexer 保持通用，不把所有未来语法过早硬编码。
- AST/parser 先稳定核心语言。
- 自定义语法必须明确进入哪个阶段展开：token-level、AST-level、HIR-level。
- 自定义语法不能破坏基础语言的错误恢复和 IDE/LSP 能力。

该部分暂不定稿，等 resolve/sema 基础稳定后再设计。

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
- `unsafe`：使用需要 unsafe/capability 的低层操作。

未标注函数在该提案中默认为 `unknown` / impure，不强制第一版代码全量标注。若未来启用检查，`@effect(read)` 函数中写入 `self` 的 `!` 字段应编译失败；trait requirement 也可以携带 effect，要求实现不比 requirement 更“脏”。
