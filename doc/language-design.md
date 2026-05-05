# Jiang 语言设计草案

本文档记录 Jiang 语言本身的设计，不记录 stage1 compiler 的目录结构和实现细节。编译器工程约定见 `doc/develop.md`。

当前目标是先固定 stage1 前端需要依赖的语言边界：词法、语法、类型、声明、泛型和错误处理。未定设计必须显式标注，避免 parser、resolver、type checker 在隐含假设上继续扩展。

## 状态标记

本文档同时记录目标设计和当前实现差异。为避免混淆，后续章节使用这些状态：

- **目标规则**：希望长期保留的语言规则。
- **stage0 已支持**：bootstrap 编译器已有实现或测试覆盖。
- **stage1 已解析**：stage1 lexer/parser/AST 已能表达，但还没有完整语义检查。
- **stage1 缺口**：stage0 已有或目标设计需要，但 stage1 尚未实现。
- **未定**：设计尚未冻结，不能作为 resolver/type checker 的硬前提。

## 设计目标

Jiang 是面向系统编程的语言，目标是在低层控制能力、工程可维护性和高层抽象之间取得平衡。

核心方向：

- 明确的值语义、指针语义和可变性语义。
- 可读的泛型和 trait 约束。
- AST 保留源码结构，语义信息进入 resolver/type checker/HIR。
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

- identifier 和基本类型名统一为 `ident`。
- 关键字包括 `import`、`public`、`alias`、`extern`、`return`、`if`、`else`、`while`、`for`、`in`、`is`、`enum`、`union`、`struct`、`record`、`trait`、`extend`、`associated`、`static`、`switch`、`try`、`catch`、`break`、`continue`、`defer`、`throw`、`true`、`false`、`null`、`Self`。
- 字符字面量使用单引号，例如 `'a'`。
- 字符串字面量使用双引号，文本按 UTF-8 字节序列处理。
- `Span` 使用字节偏移和字节长度；line/column 在诊断阶段计算。

`Self` 是类型位置的特殊名字。`self` 作为方法体中的接收者值使用，当前不按关键字处理。

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
- `T&`：自动解引用的临时引用，不表达释放职责。
- `T[]`：slice。
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
```

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

内部成员的可变性来自类型定义本身。对于 `struct` / `record` 字段、tuple 元素、union payload、数组元素，只要成员类型在其定义处是可变的，该成员就总是可变；这不受外层变量本身是否带 `!` 影响：

```jiang
struct User {
    Int id;
    Int! age;
}

User user = User(id: 1, age: 18);
user.age = 19; // 允许：age 字段自身是可变字段
user.id = 2;   // 编译错误：id 字段自身不可变
```

数组、tuple、union、record 也遵循同一条分层规则：外层变量的可变性只控制外层绑定；成员或元素能否被修改，由成员或元素类型自己的可变性决定。

## 指针、引用、数组和 Slice

现阶段先不引入完整 borrow checker，但先固定 pointer/reference 的目标语义：

- `T^`：owning heap pointer，通常来自 `new T(...)`，拥有堆上对象；它不是 C 风格 raw pointer。
- `T&`：临时引用，不表达释放职责。
- `RawPointer<T>`：裸指针，只用于 FFI / ABI / 低层 capability 场景。
- `T[*]`：many pointer，可下标访问，不表达单对象 ownership。
- `T[]`：slice，是 `{ ptr, len }` 形态的视图值，不表达所有权。

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

## 隐式操作层

`$` 用于进入值或类型的隐式操作层。

已确定操作：

- `value$.as(Type)`：强制类型转换，不保证类型安全。
- `value$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`。
- `value$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `RawPointer<T>`。
- `value$.get()`：显式解引用 `T^` / `T&`，返回指向的值；`T[*]` many pointer 必须使用下标访问。
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

`compiler/` 目录内的自举编译器源码默认拥有全部 capability，这是编译器实现的特殊配置，不代表普通 Jiang 包默认拥有这些能力。普通包默认应采用最小能力集合，并通过显式配置或受控上下文获得额外能力。

## 声明

顶层声明包括：

- `import "path";`
- `public import "path";`
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

兼容性状态：

- stage0 已支持 `public import`，但历史实现存在问题。
- stage1 已恢复 `public import` 语法，并在 module graph / resolver 中建立模块名导入语义：普通 import 会绑定一个模块名，默认模块名来自文件名；访问被导入模块的 public API 需要写 `module.Name`。
- stage1 已支持最小 import alias lookup：`import math = "math"; math.Number` 会在被导入模块的 public API 中查找 `Number`。
- stage1 已支持模块名 re-export：`public import "leaf";` 会导出 `leaf` 模块命名空间，外部通过 `current.leaf.Name` 访问，不会摊平为 `current.Name`。
- ambiguous re-export、跨 package import 仍需后续细化。

## 函数和方法

函数一定有返回类型。无返回值使用 Unit：

```jiang
() hello() {
    return ();
}
```

Jiang 目标语言不支持函数参数标签和默认参数。函数参数按定义顺序进行位置匹配，调用参数也必须按位置提供。

兼容性状态：

- stage0 曾支持参数标签和默认参数，这是历史实现，不进入 stage1 目标语言。
- stage1 AST 当前仍有 `Param.label` 和 `Param.default_value` 预留，parser 当前也可能解析调用侧 label 和默认值表达式；这些应作为待清理的 stage1 缺口处理。
- 后续 resolver/type checker 不应基于参数标签或默认参数设计调用匹配规则。

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

`init` / `deinit` 状态：

- stage0 已支持结构体初始化函数、构造 sugar 和 `deinit`。
- stage1 AST 有 `InitDecl` 和 `deinit_body` 预留。
- stage1 parser 当前尚未实现 init/deinit 解析；遇到 struct init 会报 “not implemented yet”。

## Struct、Record、Enum、Union

`struct` 用于普通名义类型。

`record` 是更偏数据记录的结构体形式。stage0 已支持 record 字段字面量、默认字段，并禁止普通 call syntax；stage1 parser 当前把 `record` 解析为 `StructDecl(record_flag: true)`。record 与 struct 的完整语义差异仍需定稿。

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

兼容性状态：

- stage0 当前主要语法是 `union(Tag) Name { Type field; }` 风格。
- stage1 目标语法是 enum-like variant 形式：`union Maybe<T> { some(T), none }`。
- 这两套语法需要在迁移阶段明确兼容策略。

## Trait 和 Extend

`trait` 描述行为约束。

`extend` 给已有类型增加实现或方法。

示例：

```jiang
trait Hashable {
    UInt64 hash();
}

extend Int: Hashable {
    UInt64 hash() {
        return UInt64(self);
    }
}
```

当前规则：

- method 不进入模块顶层命名空间；它们记录在 extend/method side table 中。
- method body 中的 `self` 是 receiver 的不可变引用。
- 字段能否被赋值只由字段类型本身是否 mutable 决定，不由 receiver 绑定是否 mutable 决定。
- `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`。
- 如果 receiver 已经是 pointer，`ptr.method(args...)` 也等价于 `Type.method(ptr, args...)`。
- `Type.method(receiver, args...)` 是显式方法调用形式；第一个实参必须匹配 receiver pointer。
- 泛型 receiver 的实例方法签名必须用实际 receiver type args 实例化后再检查。例如 `Box<T>.get(self: Box<T>&) -> T` 在 `Box<Int!>` 上调用时，等价于 `Box.get(box&) -> Int!`；如果这个结果写入 `Int` 目标，再按上面的写入目标规则忽略顶层 mutable。
- union variant name 和同一 union 的 static/显式 method name 共享类型成员命名空间，不能重名，避免 `Union.member(...)` 歧义。
- 同名函数和同名方法允许 overload；参数数量或参数类型必须不同。
- `extend Type: Trait { ... }` 当前做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现。

stage0 已支持 trait 继承、associated type 和相关负例检查。stage1 目前已支持 method side table、method overload、显式/隐式 method call 和基础 trait conformance；完整 trait solving、trait method lookup、associated type projection 仍不在本阶段范围内。

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

支持 trait bound 内的关联类型绑定：

```jiang
@where(T: Iterator<Item = Int>)
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
- early-exit coalesce: `value ?? return`
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

stage0 已支持 `x == null` / `x != null` 分支窄化。目标设计仍偏向显式 optional handling；后续需要决定 null-check narrowing 是长期保留，还是只作为旧代码兼容能力。

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

stage0 已支持 `throw`、错误传播、typed catch、fallback 等行为。stage1 AST/parser 已预留 `try` / `catch` / `throw` 形态，但完整类型检查和传播规则仍需在 type checker 阶段定义。

未定事项：

- `throw expr` 的类型。
- `try expr catch ...` 和 `try { ... } catch ...` 的统一模型。
- catch binding 的作用域和类型。
- 未捕获错误如何向外传播。

## 控制流

语句：

- `return`
- `throw`
- `break`
- `continue`
- `defer`
- `if`
- `switch`
- `try`
- `while`
- `for`
- block
- assignment
- expression statement
- local variable declaration

表达式：

- block expr
- if expr
- switch expr
- try/catch expr
- binary/unary expr
- call/field/index/slice/postfix expr

`defer` 在当前块退出时按 LIFO 顺序执行。`defer` 内不支持 `return`、`break`、`continue`。

## Pattern Matching

pattern 目前包括：

- wildcard
- binding
- literal
- tuple
- variant
- optional

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
- 普通 `import "path";` 会绑定一个模块名，默认模块名来自 import path 的文件名部分，例如 `import "utils/math.jiang";` 绑定模块名 `math`。
- `import alias = "path";` 使用显式 alias 作为模块名。
- 被导入模块的 public API 通过 `module.Name` 访问，不默认平铺到当前模块。
- `public import` 导入当前模块使用，并将被导入模块作为当前模块 public API 中的一个模块命名空间重新导出。它不摊平被导入模块的声明；例如 `middle` 中 `public import "leaf";` 后，外部通过 `middle.leaf.Name` 访问，而不是 `middle.Name`。
- `public` 标记声明对外可见。
- 基本类型不是关键字，由名字解析绑定到内建声明。

兼容性状态：stage0 已支持默认模块名 import 和 `public import` re-export，但历史实现有问题；stage1 parser/module graph/resolver 已支持默认模块名、显式 import alias 和模块名 re-export。ambiguous re-export 和 package path 解析仍需后续完善。

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

该部分暂不定稿，等 resolver/type checker 基础稳定后再设计。

## Stage1 实现状态

### 前端 Feature Matrix

| Feature | stage0 | stage1 lexer/parser/AST | 语义状态 |
| --- | --- | --- | --- |
| 基本类型名作为 ident | 已支持 | 已支持 | resolver 绑定内建类型 |
| char/string literal | 已支持 | 已支持 | expected type 规则待 type checker 固定 |
| 默认模块名 import | 已支持 | 已解析 | resolver 支持 `module.Name` |
| `public import` | 已支持但实现有问题 | 已解析 | module graph/resolver 已支持模块名 re-export |
| alias/global/function | 已支持 | 已解析 | resolver/type checker 未完成 |
| 参数 label/default | 历史支持 | 部分解析 | 目标语言不支持，stage1 待清理 |
| struct | 已支持 | 已解析 | init/deinit 缺口 |
| record | 已支持 | 解析为 struct flag | 语义差异待定稿 |
| enum | 已支持 | 已解析 | resolver/type checker 未完成 |
| union | stage0 旧语法 | stage1 目标语法 | 兼容策略未定 |
| trait/extend | 已支持部分语义 | 已解析骨架 | associated type/method lookup 待实现 |
| `@where` leading annotation | 部分支持旧语义 | 已解析 | trait constraint checking 待实现 |
| `T: A & B` TypeBound | 未完整确认 | 已解析 | constraint solver 待实现 |
| optional null narrowing | 已支持 | AST 可表达相关条件 | 正式规则待定 |
| errorable/try/catch | 已支持较多行为 | AST/parser 预留 | type checker 待实现 |
| pattern matching / `is` | 部分支持 | AST/parser 初版 | binding/exhaustiveness 待定 |
| init/deinit | 已支持 | AST 预留，parser 缺口 | stage1 待实现 |

已完成或正在实现：

- `source.jiang` 最小源文件模型。
- `diagnostic.jiang` 最小 diagnostic bag。
- `token.jiang` / `lexer.jiang` / `TokenBuffer`。
- AST 基础节点。
- parser minimal 到完整 AST parser 的第一版骨架。
- leading `@where(...)` 和 `TypeBound`。

尚未完成：

- 完整 source manager 和 line/column diagnostic。
- resolver/scope。
- type model。
- type checker。
- HIR typed representation。
- JIR lowering。
- LLVM backend 接入 stage1。
- module graph/package manifest。
- LSP 支持。

### 语义模型缺口

resolver/type checker 开始前，需要先明确这些模型：

- `DefId`、`ScopeId`、`BindingId` 和 namespace 拆分。
- AST type 到 semantic `TypeId` 的 lowering 规则。
- builtin type 的注册和查找方式。
- alias 展开、nominal type identity、array length const eval。
- pointer/ref/auto-deref 的 lvalue/rvalue 规则。
- literal expected type 和 coercion 规则。
- optional/errorable 的控制流和传播规则。
- HIR 保留哪些源码结构，JIR 降低哪些语法糖。

## 下一阶段任务计划

### P0：语言设计收敛

- 校对本文档与 `doc/jiang.md`，删除互相冲突的旧描述。
- 维护 Feature Matrix，标注 stage0 已支持、stage1 已解析、stage1 缺口和未定设计。
- 清理 stage1 中参数 label/default 的 AST/parser 预留或标记为无效语法。
- 决定 `record`、init/deinit 的正式语法和 stage1 对齐策略。
- 完善 `public import` 的 ambiguous re-export 和跨 package visibility 规则。
- 决定 null-check narrowing、errorable `T@E` 与 `try/catch` 的精确规则。
- 实现 `T^` / `T&` 的单 handle 层限制、expected-type auto-deref 和 `$` 隐式层规则。

### P1：Source / Diagnostic

- 将 `Span` 从 `token.jiang` 迁移或桥接到 `source.jiang`。
- 增加 source file table。
- 增加 diagnostic label/note/suggestion。
- 实现 line/column 查询。

### P2：Resolver / Scope

- 定义 `DefId`、`ScopeId`、`BindingId`。
- 定义 namespace 拆分和 path resolution 规则。
- 实现顶层声明收集。
- 实现 block/local scope。
- 解析 path/name 到 declaration。
- 处理 import/module 可见性。
- 暂缓 overload、trait method lookup 和泛型实例化。

### P3：Type Model / Type Check

- 新建或完善 `type.jiang`。
- 将 AST type lowering 到 semantic type。
- 实现 builtin type、nominal type、alias、tuple/unit、array/slice/pointer/ref/optional/errorable/function type。
- 处理 literal expected type。
- 检查 optional/errorable、pointer/slice/array。
- 先实现 locals/globals、assignment/lvalue、call、field/index/slice、if/block/return。

### P4：Generics / Trait Constraints

- 实现 `GenericParams` 和 `WhereConstraint` 的语义检查。
- 实现 `TypeBound` 和 intersection bound。
- 实现 trait declaration、extend declaration 和基本 trait conformance。
- 初期只做声明与基本校验，method lookup 和 associated type projection 后续扩展。

### P5：HIR / JIR

- 定义 typed HIR。
- 从 AST + resolved data + type info 生成 HIR。
- 将 HIR lower 到 JIR。
- 明确控制流、temporary、storage operation。
- 降低 defer、short-circuit、coalesce early-exit、try/catch、switch/pattern、auto-deref。

### P6：Driver / Tests

- 整理 compiler test harness。
- 支持单文件和 package 编译入口。
- 为 parser/resolver/type checker 建立分层测试。
- 减少 stage0 测试重复编译成本。
