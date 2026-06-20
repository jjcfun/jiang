# Jiang语言指南

> Jiang语言的目标是成为系统编程语言的“银弹”。`All in one`是Jiang语言的核心思想。


### 命名规范

当前仓库建议采用下面这套命名风格：

- 类型名使用 `PascalCase`
- 函数名使用 `snake_case`
- 变量名使用 `snake_case`
- 结构体字段名使用 `snake_case`
- 枚举成员使用 `snake_case`
- 模块别名优先使用 `snake_case`

示例：

```c
enum TokenKind {
    kw,
    string_lit,
    left_paren,
}

struct SourceFile {
    UInt8[]& file_path;
    Int start_offset;
}

UInt8[]& read_source(UInt8[]& file_path) {
    return file_path;
}

import store = "token_store.jiang";
```

这样可以稳定区分类型和值：

- `TokenKind`、`SourceFile` 看起来就是类型
- `kw`、`string_lit` 看起来就是枚举值
- `read_source`、`start_offset` 看起来就是函数和字段

当前不建议把枚举成员写成 `SomeField` 这种 `PascalCase` 形式，因为它会和类型名混淆。

### 词法和关键字

Token 只记录词法事实，不承载语义类型。identifier、关键字和基本类型名在 token 层统一为 `ident`，后续由名字解析、关键字表和语义检查解释。

基础类型名例如 `Int`、`UInt8`、`Bool`、`Float`、`Double`、`Char` 都不是词法关键字，而是由 resolver 解析到内建声明的普通名字。

`Self` 是类型位置的特殊名字。`self` 是类型内部 instance method 和 constructor body 中的 contextual keyword，表示当前 receiver 或初始化目标；`static` 类型函数中没有 `self`。

### 类型概要

关于Jiang语言的类型，遵循 **从左往右，从里到外** 的原则。比如`Int[2][3]`，表示一个数组，这个数组的元素有3个，每个元素都是`Int[2]`类型。从左往右看，`Int -> Int[2] -> Int[2][3]`，类型的范围是从里到外逐渐扩大的。简单来说：**越是右边，范围越大**

```c
// 嵌套的数组
Int[2][3] a = [[1, 2], [3, 4], [5, 6]]

// 最里层的数组元素为可空的Int类型
Int?[2][3] b = [[1, null], [3, 4], [5, 6]]

Int?[2]?[3] c = [[1, null], null, [5, 6]]

// owning pointer 同理
Int[3]^ b = new [1, 2, 3]

Int?[3]^ c = new [1, null, 3]
```

### 可变性

可变性是类型系统的一部分，并且是分层的。`!` 表示当前类型层级可变：

```c
Bool! flag = true;
flag = false;

Int[3] values = [1, 2, 3];
Int[3]! mutable_values = values; // 外层数组值可重新赋值
mutable_values = [4, 5, 6];

Int![3]! mutable_items = [1, 2, 3]; // 元素层和外层数组值都可变
mutable_items[0] = 10;
mutable_items = [4, 5, 6];
```

如果变量声明写出了完整左侧类型，则以左侧声明类型为准。类型推导默认得到不可变绑定；需要可变绑定时写 `_ value!` 或写出带 `!` 的左侧类型。类型推导会保留表达式的自然类型，不自动解引用 `T^` / `T&`；需要值类型时写出 expected type。类型推导只能影响推导结果的最外层可变性，不能凭空改变数组元素、tuple 元素、union payload 或 struct 字段等内部层级的可变性。

结构体、tuple、union 和数组的内部成员是否可修改，由成员类型自己的可变性决定，不由外层变量是否带 `!` 决定。

### 基本类型

```c
Int a1 = -123;
UInt u0 = 123;
UInt8 a2 = 23;
Char ch = 'a';
UInt8[3] a3 = "abc";
Int16 a4 = -45;
UInt16 a5 = 512;
Float num3 = 12.3;
Double num4 = 132.54;
Float16 num5 = 1.5;
Float32 num6 = 2.5;
Float64 num7 = 3.5;

// 类型后紧跟'!'号，表示当前类型层级可变
Bool! foo = true;
foo = false;

// 类型推断
_ x = 100; // 推断为 Int

_ y = 3.14; // 推断为 Double

_ name = "Jiang"; // 推断为 UInt8[_]
```

其中：

- `Int` / `UInt` 是 pointer-sized 整数，语义上分别对应 `isize` / `usize`；在 64-bit target 上通常是 64 位，在 32-bit target 上通常是 32 位。
- 需要固定 ABI 宽度、文件格式或网络协议布局时，使用 `Int8/Int16/Int32/Int64` 与 `UInt8/UInt16/UInt32/UInt64`。
- `Char` 表示单个 Unicode 标量，例如 `'a'`、`'中'`
- 字符串字面量按 UTF-8 字节序列处理，默认类型为 `UInt8[:0]&`；backing storage 自动追加末尾 `0`，但 sentinel 不计入 length。当前可用于 `UInt8[_]` / `UInt8[]&` / `UInt8[:0]&`，也可按 expected type 转成 `UInt8[*:0]` 或 `UInt8[N:0]`
- `()` 表示 `Unit` 类型；它是一个零大小值，同时承担无返回值语义

数值字面量在类型检查前保持未定型，优先由上下文决定最终类型。普通已经定型的数值之间不做隐式提升：

```c
Float f = 0.5;          // 0.5 按期望类型定为 Float
Float a = 2 + f;        // 2 是字面量，可按 Float 参与表达式
Double b = 2 + 0.5;     // 2 和 0.5 按 Double 参与表达式

Int two = 2;
Float c = two + f;      // 错误：Int 变量不会隐式提升为 Float
```

安全的数值转换使用类型初始化形式，例如 `Float(two)`、`Double(f)`、`Int(f)`。`$.as(Type)` 是低层强制转换，不保证类型安全，不作为普通数值转换的推荐写法。

`%` 仍然只允许整数参与；`Double -> Float`、`Float/Double -> Int` 等窄化转换需要显式写出目标类型。



### 可选类型 (Optional)

Optional 使用 `T?` 表示，也可以显式写作 `Option<T>`。`T?` 是 `Option<T>` 的类型语法糖。

```c
Int? a1 = 123;
// a2为Int?类型
_ a2 = a1;

Bar? b1 = {x: 1, y: 2};
// b2为Int?类型
_ b2 = b1?.x;

// 可选类型还支持链式调用
_ foo = x?.y?.z

// 条件解包
if a1 is .some(x) {
  // 这里x不为null，类型为Int
} else {
	// 这里x为null
}

if a1 is .some(Int! x) {
  // 这里x为可变绑定
}

if a1 is .some(ref Int x) {
  // 这里x为借用绑定，不会移动payload
}
```

#### Optional Coalesce

`??` 用于 optional 取值失败时提供默认值：

```c
Int value = maybe ?? 42;
Int other = maybe ?? fallback();
```

提前退出使用 `guard`：

```c
guard maybe is .some(value) else {
    return;
}
```

其中：

- 左侧必须是 optional
- `??` 右侧必须是 fallback 值，不支持 `return` / `break` / `continue`
- `guard` 的 `else` block 必须非空，且最后一条语句必须是
  `return`、`break`、`continue` 或 `throw`

### Defer

`defer` 会在当前块退出时按 LIFO 顺序执行。

```c
defer handle$.free();

defer {
    log("closing");
    handle$.free();
}
```

支持两种形式：

- `defer expr;`
- `defer { ... }`

当前限制：

- `defer` 体内不支持 `return`、`break`、`continue`



### 类型转换与隐式操作层

Jiang 语言把安全转换和低层强制转换分开：

- `Type(value)`：安全转换或初始化，由目标类型的 `init` 规则决定。
- `value$.as(Type)`：强制转换，不保证类型安全，主要用于底层实现、裸指针转换或能力受控的场景。

这里的 `$` 符号表示**进入隐式操作层**。可以把它理解成：对一个值或一个类型，切换到它的“隐式层 / 元层”再进行操作。

- 对值使用：`a$.xxx()`
- 对类型使用：`Type$.xxx()`
- 对复合表达式使用：`(expr)$.xxx()`

后缀 `$` 绑定在它左侧的完整对象上。因此：

- `a$.b` 等价于 `(a$).b`
- `a.b$` 等价于 `(a.b)$`
- 如果要对整个 `(a + b)` 进入隐式操作层，必须写成 `(a + b)$.xxx()`

例如：

- `a$.as(Int)`：对值 `a` 做低层强制转换
- `a$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`
- `a$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `T*`
- `a$.get()`：显式解引用 `T^` / `T&`，返回指向的值
- `a$.move()`：显式转交当前变量的值，源变量随后失效且不再析构
- `a$.addr()`：获取值 `a` 的地址值
- `a$.free()`：对 `a` 做释放操作
- `Int$.size()`：获取类型 `Int` 的大小
- `Int$.align()`：获取类型 `Int` 的 ABI 对齐
- `Int$.max_align()`：获取当前内建分配器保证支持的最大 Jiang 类型对齐
- `Int$.alloc()`：分配一个未初始化的 `Int![*]`，长度为 `1`
- `Int$.alloc(10)` / `Int$.alloc_many(10)`：分配一个长度为 `10` 的 `Int![*]`

在当前设计中，许多原本会被写成内建函数的操作，都会逐步迁移到隐式操作层。例如，类型大小不再写作 `size_of(T)`，而统一写作 `T$.size()`。

隐式操作层中的一部分 primitive 能力由编译器 builtin 或标准库提供，例如：

- `T$.size()`
- `T$.align()`
- `T$.max_align()`
- `T$.alloc()`
- `T$.alloc(...)`
- `T$.alloc_many(...)`
- `value$.ref()`
- `value$.ptr()`
- `value$.get()`
- `value$.as(Type)`

普通数值转换优先使用目标类型初始化：

```c
Float f = 10.5;

// 将 Float 转换为 Int
Int i = Int(f);

print("i = %d", i); // 输出：i = 10

// 将 Int 转换为 UInt8
Int val = 255;
UInt8 small_val = UInt8(val);
```

`as` 是一个特殊的隐式层方法，它接收一个类型表达式作为参数，用于不保证类型安全的强制转换。
当前 package 默认处在全局 unsafe 模式，编译器不会因为裸指针转换或裸指针访问本身报错；后续如果
引入 capability/unsafe gate，再把这些低层操作纳入显式能力检查。

```c
Int addr = 0x12345678;
Int* ptr = addr$.as(Int*);
```

### 数组（Array）

数组的长度是类型的一部分，必须在编译期就确定，所以数组类型不支持运行时改变长度。

#### 不可变数组

```c
// 定义不可变数组
Int[3] a = [1, 2, 3] // a: [1, 2, 3]


// 初始化数组时，如果元素个数与数组长度不想等，将会报编译错误
Int[5] a = [1, 2, 3]

// 在堆上创建数组，并返回 owning pointer
Int[3]^ x = new [1, 2, 3]

// 通过length属性，可以获取到数组的长度（注意，Jiang语言中，owning pointer 默认自动解引用）
print("array %d", x.length)
```

#### 数组类型推断

```c
// 定义数组时，可以使用类型推断
// 以下2种定义等价
Int[_] a = [1, 2, 3]
_ a = [1, 2, 3]
```

- 只要提供了数组字面量初始化器，编译器就可以从元素推断数组类型与长度

```c
// 这是正确示例
_ x = [1, 2, 3]

// 这是正确示例
Int[_] y = [1, 2, 3]

// 这也是正确示例
Int[3] z = [1, 2, 3]
```

#### 数组可变性

虽然数组的长度固定，但支持数组内的元素可变：

```c
// 数组内的元素可变
Int![_] b = [1, 2, 3] // b: [1, 2, 3]

b[1] = 4 // b: [1, 4, 3]

// 数组不可变，但是变量可以重新赋值
Int[_]! c = [1, 2, 3]
c = [4, 5, 6]

// 数组可变，变量也可以重新赋值
Int![_]! d = [1, 2, 3] // d: [1, 2, 3]
d[1] = 4 // d: [1, 4, 3]
d = [4, 5, 6] // d: [4, 5, 6]

// 数组在堆上创建
Int[_]^! e = new [1, 2, 3]
// 这里，变量赋值的时候会在堆上分配内存
e = new [4, 5, 6]
```

#### 多维数组

Jiang语言其实没有多维数组的概念，多维数组在这里只是嵌套的数组。以数组类型`Int[2][3]`为例。
从左往右看`Int[2][3]`类型，可以看作数组从里到外依次嵌套：最外层的数组有3个元素，每个元素都是Int[2]类型的数组。

```sc
Int[2][3] foo = [[1, 2], [3, 4], [5, 6]]

foo[0]  // [1, 2]

foo[0][1] // 2

```

### 指针与引用

Jiang 不引入完整 alias borrow checker，但会固定所有权、引用、析构和显式 move 的边界。
指针语义约定为：

- `T^`：`Box<T>` 的语法糖，表示自动解引用的 owning pointer；它不是 C 风格 raw pointer
- `T&`：自动解引用的非 owning 引用，通常用于引用已有值，不承担释放职责
- `T&!`：可重绑定的 reference slot，不改变目标对象所有权
- `T*`：裸指针，主要用于 FFI / ABI / 低层 capability 场景
- `T[*]`：可按下标访问和按元素偏移的 many-pointer；它不默认自动解引用
- `T[]&`：`Reference<Slice<T>>` 的语法糖，是 borrowed slice view，语义上类似 `{ T[*], length }&` 的连续内存引用视图，不表达所有权；裸 `T[]` / `Slice<T>` 是 unsized array type，不能作为普通 value
- `T[*:0]`：sentinel many-pointer，不带 length，适合 C string ABI
- `T[:0]&`：`Reference<SentinelSlice<T, 0>>` 的语法糖，是 sentinel borrowed slice view，layout 与 `T[]&` 一样是 `{ data, length }`，并额外保证 `data[length] == 0`；裸 `T[:0]` 是带 sentinel 的 unsized array type，不能作为普通 value

只有 `T^` 表达语言级所有权。`T&`、`T[]&`、`T[*]` 和 `T*` 都不拥有目标对象。

#### Pointer / Reference 类型

Pointer / reference 类型也遵循 **从左往右，从里到外** 的原则。`^` 是 `Box<T>` 的语法糖，表示 owning pointer 外层；`&` 是 `Reference<T>` 的语法糖，表示 reference 外层；二者都可以和 optional / mutable 标记组合使用。

```c
// 在栈中开辟内存空间
Int a = 123;

// new关键字可以创建拥有所有权的对象，并返回一个 owning pointer
Int^ b = new Int(123);

// 创建数组，并返回一个 owning pointer
Int[3]^ c = new [1, 2, 3];

// 临时引用
Int& d = a$.ref();

Int!^? maybe_owner; // optional owning pointer to mutable Int
Int?& maybe_ref;    // reference to optional Int
Int^! owner_slot;   // owning pointer 绑定本身可变

```

`^` 与 `&` 都紧跟在元素类型后面，不能存在空格。

```c
// Bad
Int ^ a;

// Bad
Int ^a;

// Good
Int^ a;

// Bad
Int & b;

// Good
Int& b;
```

#### 自动解引用

使用 `T^` 时默认自动解引用，除非 expected type 本身与 owning pointer 类型一致。`T&`、`T*` 和
`T[*]` 不参与默认自动解引用。Jiang 没有前缀手动解引用语法，表达式位置的 `*ptr` 这种写法不成立；
需要显式解引用或写入单对象指针/引用时使用 `ptr$.get()` / `ptr$.set(value)`。其中 `ptr$.set(value)`
只有在 pointee 类型带顶层 `!` 时才合法，例如 `Int!*` 可以写入，`Int*` 只能读取。

类型推导会保留 pointer/reference 层，并默认得到不可变绑定；写出 expected type 只会让 `T^`
触发自动解引用，`T&` / `T*` 仍需要显式 `$.get()`：

```c
Int&! ref = value$.ref();

_ copied = ref;      // copied: Int&
_ mutable! = ref;    // mutable: Int&!
Int copied_value = ref$.get();
Int& kept = ref;     // expected type 是 Int&，保留 reference
_ raw_ref = ref$.ref(); // raw_ref: Int&
Int explicit_value = ref$.get(); // 显式解引用
```

`$` 操作符会阻止自动解引用，并进入隐式操作层。通过类似 `ptr$.free()`、`ptr$.ref()`、`ptr$.ptr()`、
`ptr$.get()`、`ptr$.set(value)` 的语法，可以调用 pointer/reference 自身的一些低层操作。

```c
Int a = 100;

Int^ b = new Int(200);

// owning pointer 默认自动解引用，b 直接表示其元素的值
Int c = a + b;

print("c = %d", c); // 输出： c = 300

// '$' 符号阻止自动解引用，并进入 b 的隐式操作层
b$.free();
```

`$.ref()` 和 `$.ptr()` 的返回类型固定为 `T&` 和 `T*`：

```c
Int^ p = new Int(41);

_ ref = p$.ref(); // ref: Int&
_ raw = p$.ptr(); // raw: Int*

// 普通值上下文会自动解引用
Int x = p + 1;

// 进入隐式层后返回的引用不会再次自动解引用
Int bad = p$.ref() + 1; // 编译错误

// 显式解引用
Int explicit = p$.get();

Int!* raw_ptr = p$.ptr();
Int raw_value = raw_ptr$.get();
raw_ptr$.set(42);

Int[1] items = [41];
Int[*] raw = items[0]$.as(Int[*]);

// many-pointer 通过下标访问
Int y = raw[0];
```

`T*` raw pointer 不参与默认自动解引用，只能通过 `$.get()` 显式读取单个目标对象；只有 pointee
类型带顶层 `!` 的 raw pointer，例如 `T!*`，才允许通过 `$.set(value)` 显式写入。
`T[*]` many-pointer 也不参与默认自动解引用。它表示一段可按元素索引的连续地址，必须通过下标表达式取元素：

```c
Int[*] ptr = items[0]$.as(Int[*]);

_ raw = ptr;    // raw: Int[*]
Int value = ptr[0];
ptr[1] = 42;

_ item_ref = ptr[1]$.ref(); // item_ref: Int&
_ item_ptr = ptr[1]$.ptr(); // item_ptr: Int*
```

`T[*]` many-pointer 支持下标读写，但当前不提供 `offset()` 这类额外指针算术语法。

除数组、slice、many-pointer 外，显式实现 `SubscriptGet` trait 的用户类型也支持 `value[index]` 语法；如果该类型还显式实现 `SubscriptSet`，则支持 `value[index] = new_value`。

`^` owning pointer 可通过 `ptr$.free()` 主动释放默认堆分配器上的对象；`&` 引用只是非 owning 引用，不参与释放。
`ptr$.free()` 是低层释放操作，不作为普通析构入口使用。目标语言的自动析构由作用域退出、`T^` 字段析构和类型的 `deinit()` 规则处理。

```c
// 定义一个 owning pointer，指向堆内存
Int^ a = new Int(100);

// 可以主动释放 owning pointer 管理的内存空间
a$.free();
```

### 所有权、implicit copy 和析构

Jiang 的目标规则是不引入完整 alias borrow checker，但明确资源释放、自动析构、隐式 copy 和
显式 move 的边界。

- `T^` 是 owning pointer，拥有堆上对象，并参与自动析构。
- `T&` 是 non-owning reference，不拥有资源，不参与自动析构。
- `T[*]`、`T*` 是低层指针；`T[]&` 是 slice reference。它们不表达语言级所有权。裸 `T[]` 是 unsized array type，不是可独立存放的 reference value。
- 只有 `Movable` 类型会自动 drop；`T^` 是内建 `Movable`。
- `T&`、`T&!`、`T[*]`、`T*`、`T[]&` 字段不会被编译器自动释放。
- `T[]&` 本身不拥有整段 buffer；drop slice reference 时不 drop 全部元素。但 `slice[i]` 是已初始化元素 place，覆盖时按元素类型的 drop 规则处理旧值。
- 经过 `T*` / `T[*]` 得到的 place 是裸指针派生 place，写入时是 raw write，不隐式 drop 旧值。
- 如果 nominal 有自定义 `deinit`，先执行自定义 `deinit`，再执行编译器生成的递归字段析构。
- 普通 `struct`、`union` 默认可以隐式 copy。
- `T^` 是内建 Movable；显式声明 `Movable` 的 nominal type 永远不能隐式 copy。
- 直接或间接包含 Movable 字段，或定义了自定义 `deinit` 的 nominal type，必须显式声明 `Movable`。
- `T&`、`T&!`、`T[*]`、`T*`、`T[]&` 是 non-owning view，字段中包含这些类型不影响 implicit copy。
- 泛型参数只有声明 `T: !Movable` bound 时，才能在泛型代码里按 implicit copy 使用。

显式转移所有权使用 `move()`：

```c
Buffer^ a = new Buffer();
Buffer^ b = a$.move();
// move 后 a 失效，离开作用域时不会再析构 a
```

### 切片（Slice）

`Slice<T>`（后缀写法 `T[]`）是长度在运行时确定的 unsized array type。它描述一段连续 `T` 元素序列，但裸 `T[]` 不能作为普通 value 单独存放或传递。
`T[]&` / `Reference<Slice<T>>` 才是借用的 slice view，运行时 layout 类似 `{ data: T[*], length }`，不拥有元素和 buffer，并要求被引用存储的 lifetime 覆盖 slice view 的使用范围。
`T[]^` / `Box<Slice<T>>` 是 owned unsized array，拥有已初始化的 buffer，drop 时会按元素类型逐个析构并释放底层 allocation。
`SentinelSlice<T, S>`（后缀写法 `T[:S]`）同样是 unsized array type；`T[:S]&` 是带 sentinel 保证的 borrowed view。
标准库 `Vector<T>.slice()` 返回借用 `T[]&` 视图；`Vector<T>.into_array()` 会消耗 `Vector`，
把已初始化区间交给返回的 `T[]^` 拥有，调用后原 `Vector` 失效。

```c
// x为一个数组
Int[_] x = [1, 2, 3];

// 数组自动转换为 borrowed slice view
Int[]& y = x[..];

print("y.length = %d", y.length); // 输出：y.length = 3
```

### 元组（Tuple）

#### 多元组

```c
// 函数可以通过元组来返回多个值
(Int, Int) foo(Int a, Int b) {
  return (a * a, b * 2);
}

// 接收元组返回值
_ result = foo(10, 200);
// 与以下方式等价：
// (Int, Int) result = foo(10, 200);

print("result0 = %d", result[0]); // 输出：result0 = 10;
print("result1 = %d", result[1]); // 输出：result1 = 200;

// 解构元组，用于接收多个返回值
(_ a, _ b) = foo(10, 200);
// 与以下方式等价：
// (Int a, Int b) = foo(10, 200);
print("x = %d, y = %d", x, y); // 输出：a = 100, b = 200

/// 解构元组的时候，可直接定义变量
(_ x, _ y!) = foo(10, 200);
y += 100;
print("x = %d, y = %d", x, y); // 输出：x = 100, y = 300
```

#### 一元组

只有一个元素的元组被称之为一元组，如：`(Int)`。一元组有个特性，即这个元组与它的元素是等价的，占用的内存空间也一致。

从数学上不难看出：`(1 + 1) = (2) = 2`，这里的 `(2)` 和 `2`相等。将`(2)`看成一元组, 自然推断出这个结论。

```c
// 以下两个函数签名等价
(Int) add(Int a, Int b);
Int add(Int a, Int b);

// 以下两种语法也等价
(Int! x) = add(1, 2);		// 解构变量
Int! x = add(1, 2); 		// 定义变量
```

#### Unit

`()` 表示 `Unit` 类型。`Unit` 不占用内存空间，可以像普通类型一样作为值、参数、局部变量和字段使用。

```c
() hello() {
	print("Hello World!");
  // return语句必须有返回值，即使返回值是 Unit
  return ();
}
```

### 字符串常量

```c
UInt8[_] str1 = "hello";
UInt8[]& str2 = "hello";
UInt8[5:0] str3 = "hello";
UInt8[:0]& str4 = "hello";
UInt8[*:0] c_str = "hello";
```

字符串字面量的底层存储会自动追加末尾 `0`，但 `length` 不包含这个 sentinel。例如 `"hello"` 作为 `UInt8[:0]&` 时，`length == 5`。`UInt8[5:0]` 的逻辑长度是 5，但实际 storage 是 6 个 `UInt8`，因此 `UInt8[5:0]$.size() == 6`。普通 `UInt8[*]` 不直接接收字符串字面量；需要 C 风格 NUL 结尾指针时使用 `UInt8[*:0]`。

### Range

Jiang 支持 `start .. end` 形式的 range expression：

```c
_ r = 0..10;
```

语法上 range expression 只表示 `logic_or_expr ".." logic_or_expr`。`Range` 是否作为内建类型、标准库类型或编译期特殊结构暴露，后续再定稿。
目前约定 `end` 为开区间端点。

### 函数

#### 返回值

Jiang 的函数一定有返回值，即使是 `Unit` 值。`Unit` 用 `()` 表示，在运行时不会占用内存空间。

```c
() hello() {
  print("Hello World!");
  return ();
}
```

#### 函数参数

Jiang 支持位置参数、命名参数和尾部默认参数：

```c
Int add(Int base, Int extra = 1) {
    return base + extra;
}
```

规则如下：

- 位置参数按定义顺序匹配
- 带默认值的参数必须位于参数列表尾部
- 当前默认值只支持 literal，并按参数 expected type 检查
- 命名参数使用 `name: value`，可以重排或跳过带默认值的参数
- 命名参数出现后，后续不能再出现位置参数
- overload 决议必须能按参数数量和参数类型区分候选，否则诊断为歧义

```c
add(10);
add(10, extra: 20);
```

#### 函数调用

```c
Int[_] list = [5, 3, 4, 1, 2];

sort(list, (left, right) => left < right);
sort(list, (Int left, Int right) => left < right);
```

#### 函数签名

```c
// 排序
Int[]& sort(Int[]& list, Fn<Bool, Int, Int> compare)

// 支持泛型的排序，其中 T 需要实现 Numeric
@where(T: Numeric)
T[]& sort<T>(T[]& list, Fn<Bool, T, T> compare)

// 支持泛型的排序，会抛出异常，其中 E 可以为任意类型
@where(T: Numeric)
T[]&@E sort<T, E>(T[]& list, Fn<Bool@E, T, T> compare)
```

#### 函数指针

`Fn<R, A, B, ...>` 表示函数指针类型：

- 第一个类型参数是返回类型
- 后续类型参数按顺序表示参数类型

例如：

```c
Fn<Bool, Int, Int> compare;
```

表示：

- 返回 `Bool`
- 接收两个 `Int` 参数

当前支持：

- 普通顶层函数衰减为 `Fn<...>`
- `static` 方法衰减为 `Fn<...>`
- 实例方法通过 `Type.method` 衰减为 `Fn<Ret, Receiver&, Args...>`
- `Fn<...>` 的返回类型可以写成 `T@E`
- 通过 `Fn<...>` 变量进行调用
- 非捕获 lambda 表达式赋值给 `Fn<...>`
- 若同名函数/方法存在多个重载：
  - 调用时按参数个数和参数类型**精确匹配**
  - 返回类型不参与重载决议
  - 将函数值赋给 `Fn<...>` 时，目标 `Fn<...>` 类型会参与消歧
  - 若没有目标类型上下文（例如 `_ f = foo;`）且存在多个重载，则编译报歧义

示例 1：顶层函数

```c
Bool less(Int left, Int right) {
    return left < right;
}

Fn<Bool, Int, Int> compare = less;
Bool ok = compare(1, 2);
```

示例 2：`static` 方法

```c
struct Math {
    static Bool less(Int left, Int right) {
        return left < right;
    }
}

Fn<Bool, Int, Int> compare = Math.less;
Bool ok = compare(1, 2);
```

示例 3：实例方法（未绑定方法值）

```c
struct User {
    Int id;

    Int add(Int extra) {
        return self.id + extra;
    }
}

Fn<Int, User&, Int> add = User.add;

User user = User { id: 40 };
Int value = add(user$.ref(), 2);
```

这里 `User.add` 是未绑定实例方法：

- 第一个参数是接收者引用 `User&`
- 后续参数与方法声明中的普通参数保持一致
- 当前需要显式传入 `user$.ref()`

示例 4：可错返回的函数指针

```c
enum Err {
    bad = 1,
}

Bool@Err less(Int left, Int right) {
    if (left < 0) {
        throw Err.bad;
    }
    return left < right;
}

Fn<Bool@Err, Int, Int> compare = less;
```

示例 5：重载函数值按 `Fn<...>` 目标类型消歧

```c
Int add(Int value) {
    return value + 1;
}

Int add(Int left, Int right) {
    return left + right;
}

Fn<Int, Int> inc = add;
Fn<Int, Int, Int> sum = add;
```

示例 6：lambda 表达式

```c
Fn<Int, Int> inc = (Int value) => value + 1;
Fn<Int, Int, Int> add = (Int left, Int right) => left + right;
Fn<Int> answer = () => 42;
```

lambda 规则：

- 参数列表必须写 `(...)`
- 参数可以省略类型；目标 `Fn<...>` 类型会参与参数类型推断
- 单参数也必须写括号，例如 `(Int x) => x`
- 无参数写 `() => expr`
- body 可以是表达式或 block
- 当前只支持非捕获闭包；不能读取或写入外层局部变量
- lambda 可以赋值给 `Fn<...>` 或传给需要 `Fn<...>` 的参数

当前不支持：

- 通过实例值获取绑定方法函数值（例如 `value.method`）
- `init` 转函数指针
- 捕获外部变量的闭包

#### 异步函数（Async）

```c
@where(T: Numeric, E2: CompareError)
async T[]&@E1 sort<T, E1, E2>(T[]& list,Fn<async Fn<async Bool@E2, T, T>@E1, T[]&> compare)

@where(T: Numeric, E2: CompareError)
@alias(Cmp = Fn<async Bool@E2, T, T>)
async T[]&@E1 sort<T, E1, E2>(T[]& list, Fn<async Cmp@E1, T[]&> compare)
```

### 控制流（Control Flow）

#### 块语句（Block）

使用 `{}` 可以将多条语句组合成一个代码块。代码块通常用于函数体、`if` 分支或作为独立的作用域。

```c
{
    Int a = 1;
    Int b = 2;
    print("sum = %d", a + b);
}
```

#### 条件分支（If/Else）

Jiang 语言支持标准的 `if` 条件分支。条件表达式必须放在括号 `()` 内。

```c
Int a = 10;

if (a == 10) {
    print("a is ten");
} else if (a > 10) {
    print("a is greater than ten");
} else {
    print("a is less than ten");
}
```

#### if表达式

`if` 只有表达式形式；作为语句使用时写成 `if ...;`。

```c
Int x = if (flag) { 1; } else { 2; };
Int z = if (flag) {
    Int base = 40;
    base + 2;
} else {
    0;
};
```

当前规则：

- `else` 分支必填
- 两个分支的结果类型必须一致
- 分支必须写成 `{ ... }`
- `{ ... }` 内只允许语句，不允许独立的 tail expression
- 表达式语句必须以 `;` 结束
- 分支结果是 block 中最后一条语句的值；空 block 的结果是 `Unit`

#### switch表达式

`switch` 也可以作为表达式使用：

```c
Int x = switch (value) {
    1 => 40;
    2 => 42;
    else => 0;
};

Int y = switch (value) {
    1 => {
        Int base = 40;
        base + 1;
    }
    else => 0;
};
```

当前规则：

- 分支使用 `=>`
- `=>` 左侧可以用 `,` 匹配多个 pattern，例如 `.a, .b => ...`
- `switch` 只有表达式形式；作为语句使用时写成 `switch ...;`
- 分支右侧可以写成单条语句，或 `{ ... }`
- `{ ... }` 内只允许语句，不允许独立的 tail expression
- 分支结果是右侧语句或 block 最后一条语句的值
- 所有分支结果类型必须一致
- `enum` / `union` / `optional` 仍然做穷尽性检查
- 分支根 pattern 只支持 variant / optional / literal
- binding/wildcard 只作为 variant 或 optional payload 的子 pattern 使用
- 当前不支持 tuple pattern；tuple 解构应使用独立 destructure 语法
- 当前不支持对 `T@E` 结果直接使用 `switch` 表达式

#### 异常

Jiang 的异常不是 runtime exception，也不做栈展开。它只是返回值编码，语法写作 `T@E`：

```c
Int@Err parse(UInt8[]& text)
  
()@Err flush()
  
Fn<Bool@Err, Int, Int> compare
```

其中：

- `T` 是成功值类型
- `E` 是错误值类型
- `@E` 只允许出现在函数返回类型和 `Fn<...>` 的返回位
- 底层布局复用通用 result/union 模型，不单独引入 runtime exception 机制

抛出错误使用 `throw expr;`：

```c
enum Err {
    bad = 7,
}

Int@Err parse(Bool fail) {
    if (fail) {
        throw Err.bad;
    }
    return 42;
}
```

`throw` 规则：

- 只允许出现在返回类型为 `@E` 的函数里
- `throw` 的值必须与当前函数的错误类型 `E` 一致
- `throw` 只是语句，不是表达式

在 `T@E` 函数里调用另一个 `U@E` 函数时，错误会自动传播：

```c
Int@Err ok() {
    return 1;
}

Int@Err outer(Bool fail) {
    parse(fail);
    Int x = ok();
    return x + 41;
}
```

这里：

- `parse(fail)` 成功时继续执行
- 失败时自动从 `outer` 返回同一个错误
- 只支持**相同错误类型 `E`** 的隐式传播
- 在非 `@E` 函数里，不能把 `@E` 调用结果当普通值直接使用

异常的使用方式是：

- 在 `@E` 函数里依靠普通调用做同 `E` 的隐式传播
- 用 `try expr catch (...) => fallback` 处理单个失败结果

异常结果不通过 `switch` 匹配。

单个可错表达式可以用 `try catch` 处理：

```c
enum Err {
    bad = 7,
}

Int@Err parse(Bool fail) {
    if (fail) {
        throw Err.bad;
    }
    return 41;
}

Int main() {
    Int a = try parse(false) catch () => 0;
    Int b = try parse(true) catch (e) => {
        if (e == Err.bad) {
            42;
        } else {
            0;
        };
    };
    Int c = try parse(true) catch (e) => {
        Int base = 0;
        if (e == Err.bad) {
            base + 7;
        } else {
            base;
        };
    };
    return a + b + c;
}
```

`catch` 规则：

- 只支持前置 `try catch` 表达式形式：`try expr catch (...) => fallback`
- `try` 只包住 `catch` 前面的单个表达式
- `expr` 必须是 `T@E`
- `catch` 参数列表必须写 `(...)`；不需要错误值时写 `()`
- `catch` 绑定可省略类型；如果写绑定，类型自动推断为错误类型 `E`
- fallback 可以是表达式或 block
- 成功结果类型为 `T`，fallback 的结果类型必须能与 `T` 统一
- `catch` 不做 runtime unwind，仍然只是结果值分支

不支持：

- 后缀 `expr catch`
- 多条 `catch`
- 无 `=>` 的 `catch` fallback
- `finally`
- 不同错误类型自动组合

#### Defer

`defer` 会在当前块退出时按 LIFO 顺序执行。

```c
defer handle$.free();

defer {
    log("closing");
    handle$.free();
}
```

支持两种形式：

- `defer expr;`
- `defer { ... }`

当前限制：

- `defer` 体内不支持 `return`、`break`、`continue`

#### 算术运算符

Jiang 语言支持以下算术运算符：

| 运算符 | 说明 |
| :----- | :--- |
| `+`    | 加法 |
| `-`    | 减法 |
| `*`    | 乘法 |
| `/`    | 除法 |
| `%`    | 取模 |

```c
Int a = 17 % 5;
print("a = %d", a); // 输出：a = 2
```

#### 位运算符

Jiang 语言支持以下整数位运算符：

| 运算符 | 说明 |
| :----- | :--- |
| `~`    | 按位取反 |
| `&`    | 按位与 |
| `|`    | 按位或 |
| `^`    | 按位异或 |
| `<<`   | 左移 |
| `>>`   | 右移 |

- 位运算只支持整数类型。
- `&` / `|` / `^` 要求两侧操作数类型一致。
- `<<` / `>>` 要求左右两侧都是整数类型，结果类型与左侧操作数一致。
- 位运算优先级低于算术运算，高于比较运算。

```c
Int mask = (1 << 5) | (1 << 3) | (1 << 1);
UInt8 bits = ~0;
```

#### 比较运算符

Jiang 语言支持以下比较运算符，其优先级低于算术运算符：

| 运算符 | 说明     |
| :----- | :------- |
| `==`   | 等于     |
| `!=`   | 不等于   |
| `<`    | 小于     |
| `>`    | 大于     |
| `<=`   | 小于等于 |
| `>=`   | 大于等于 |

```c
if (a != 0) {
    print("a is not zero");
}
```

#### 循环（Loops）

##### While 循环

`while` 循环在给定的条件表达式为真时持续执行其代码块。

```c
Int! i = 0;
while (i < 10) {
    print("i = %d", i);
    i += 1;
}
```

##### For 循环

Jiang 语言支持 `for-in` 语法，用于遍历区间、数组或任何可迭代对象。
裸 `T[*]` many pointer 不作为 iterable；需要遍历指针区间时，使用 range 产生 index，
再在循环体内写 `p[i]`。

**1. 区间遍历**
目前仅支持左闭右开区间 `start..end`。

```c
for i in 0..10 {
    print("%d", i);
}
```

**2. 集合遍历**
直接遍历容器中的元素。

```c
Int[_] list = [10, 20, 30];
for item in list {
    print("item: %d", item);
}
```

**3. 带索引的遍历 (Explicit Indexing)**
Jiang 不支持隐式的索引迭代。如果需要索引，必须调用 `list.indexed()` 方法，
该方法会返回一个包含 `(Int, Item)` 元组的序列。

```c
Int[_] list = [10, 20];

for pair in list.indexed() {
    (i, item) = pair;
    print("index: %d, value: %d", i, item);
}
```

**4. 绑定规则**
`for` 的 `in` 前面只接受不可失败的 binding pattern，不接受 optional、variant、literal
这类可失败 match pattern。需要解构时，在循环体内使用独立 destructure 语句。

```c
(Int, Int)[_] pairs = [(1, 2), (3, 4)];

for pair in pairs {
    (a, b) = pair;
    print("a=%d, b=%d", a, b);
}
```

### 结构体（Struct）

#### 定义结构体

```c
// 定义一个结构体类型Point
struct Point {
  Int x; // 这是一个属性
  Int y; // 这也是一个属性
}

// 定义一个结构体类型Offset
// 注意，Point类型和Offset类型不等价
struct Offset {
  // 定义相同类型的多个属性
  Int x, y;
  // 与以下方式等价：
  // Int x;
  // Int y;
}

// 定义一个结构体常量
Point point1 = Point { x: 0, y: 0 }
// 与以下两种方式等价
_ point1 = Point { x: 0, y: 0 }
Point point1 = Point { x: 0, y: 0 }

Point point move_point(Point point, Offset offset) {
  // 返回一个新的point
  return Point { x: point.x + offset.x, y: point.y + offset.y }
  // 与以下方式等价
  // return Point { x: point.x + offset.x, y: point.y + offset.y }
}
```

#### init函数

struct 可以自定义 `init` 函数。

`init` 具有以下语义：

- `init` 是结构体内的特殊构造器入口
- `init` 允许可见性修饰，例如 `public init(...)`
- `init` 隐式拥有 `self`
- `init` 不声明返回类型，语义等价于 `()`
- `init` 只允许 `return;` / `return ();`
- `init` 不能写成 `static init`
- `Point(...)` / `new Point(...)` 是结构体构造语法
- 如果类型定义了一个或多个 `init`，那么 `Point(...)` 会在这些 `init` 中按参数个数和参数类型做重载决议
- `init` 支持普通位置参数、命名参数和尾部默认参数，规则与普通函数一致
- 如果类型没有定义 `init`，那么默认字段初始化使用 `Point { field: value }`
- 只要类型定义了 `init`，就不允许再用 `Point { ... }`
- `new` 只接受构造形式，不支持任意表达式
- 例如：
  - `new Int`
  - `new Int(123)`
  - `new Point(...)`
  - `new .(...)`
  - `new [1, 2, 3]`
- `new Point(...)` 会先按上面的规则构造出 `Point` 值，再把这个值放到堆上
- `struct` 字段声明支持同类型多名字写法，例如 `Int x, y, z;`

```c
struct Point {
  Int x;
  Int y;

  public init(Int x, Int y) {
    self.x = x;
    self.y = y;
  }

  public init(Int value) {
    self.x = value;
    self.y = value;
  }
}
```

```c
Point p1 = Point(x: 1, y: 2);
Point p2 = Point(3);
Point^ p3 = new .(x: 4, y: 5);
```

#### deinit函数

struct 还可以定义 `deinit` 函数。

`deinit` 具有以下语义：

- `deinit` 是结构体内唯一的特殊析构器入口
- `deinit` 隐式拥有 `self`
- `deinit` 不声明返回类型，语义等价于 `()`
- `deinit` 只允许 `return;` / `return ();`
- `deinit` 不允许 `public` / `static` 等可见性或静态修饰
- `deinit` 由该 nominal 的 drop 触发，不作为普通方法暴露
- `ptr$.free()` 是低层释放操作，不作为普通析构入口使用
- 定义了 `deinit` 的 nominal type 必须声明 `Movable`
- 如果 struct 有自定义 `deinit`，先执行自定义 `deinit`，再执行编译器生成的递归字段析构

```c
struct Buffer {
  UInt8[*] data;

  deinit() {
    self.data$.free();
    return;
  }
}
```

#### 名义类型内部函数

除 `init` 外，名义类型当前都可以定义普通内部函数。第一版使用 `static` 区分类型函数与实例函数：

- `static Ret foo(...)`：类型函数，只允许 `Type.foo(...)`
- `Ret foo(...)`：实例函数，函数体内有隐式 `self`，只允许 `value.foo(...)`

实例函数默认使用 `@self(ref)` receiver，也就是 `self` 的类型为 `Self&`。
需要让方法消耗 receiver 时，可以写 `@self(move)`：

```jiang
struct Box: Movable {
    Int value;

    @self(move)
    Int consume() {
        self.value
    }
}

Int use() {
    Box box = Box(value: 1);
    box.consume()
}
```

调用 `box.consume()` 后，`box` 已经被 move，后续不能再使用。`@self(...)` 只支持
`ref` 和 `move`，并且只能写在实例方法上；`static` 方法、`init` 和 `deinit` 不支持。

当前适用范围：

- `struct`：支持 `init`、static 方法、实例方法
- `union`：支持 static 方法、实例方法
- `enum`：支持 static 方法、实例方法

`init` / `deinit` 仍然是 `struct` 的特殊生命周期入口。`union` / `enum` 不承诺自定义生命周期入口。
union variant 和普通/static method 共用 `Type.member` 访问面，不能同名。

```c
struct User {
  Int id;

  init(Int id) {
    self.id = id;
    return;
  }

  static Int zero() {
    return 0;
  }

  Int value() {
    return self.id;
  }
}

Int a = User.zero();
User user = User { id: 42 };
Int b = user.value();
```

```c
enum Mode {
  read,
  write,

  static Int answer() {
    return 42;
  }

  Int value() {
    return self.value;
  }
}

Int a = Mode.answer();
Mode mode = Mode.write;
Int b = mode.value();
```

```c
union Result {
  Int a;
  Int b;

  static Int answer() {
    return 42;
  }

  Int value() {
    return self.a;
  }
}

Int a = Result.answer();
Result result = .a(1);
Int b = result.value();
```

字段初始化规则：

- 非 optional 且无默认值字段，必须在所有返回路径上完成初始化
- optional 字段默认初始化为 `null`
- 带默认值字段进入 `init` 时视为已初始化
- 不可变字段最多初始化一次；带默认值的不可变字段不能在 `init` 中再次赋值

Jiang语言中，结构体即可以是值类型，也可以是引用类型。这取决于是初始化时是否带有`new`关键字。以上方的Point结构体为例：

```c
// p1为值
Point p1 = Point { x: 0, y: 0 }
// p1赋值给p2是值拷贝
Point p2 = p1

// p3为 owning pointer，此时为引用类型
Point^ p3 = new Point { x: 100, y: 200 }

// 由于Jiang语言的 owning pointer 默认自动解引用，此时的p3被当成值
print("p3.x = %d, p3.y = %d", p3.x, p3.y) // 输出：p3.x = 100, p3.y = 200
```

#### 结构体的可变属性

```c
struct User {
  // id为不可变属性
  Int id;
  // age为可变属性
  Int! age;
  // nick_name为可空的可变属性
  UInt8[]&?! nick_name;
}

// 定义一个结构体常量并初始化
// 注意：可空属性可以不传，此时该属性初始化为null
User user1 = User(id: 123, age: 18)
// 与以下定义等价：
// User user1 = User(id: 123, age: 18, nick_name: null)

print("user age = %d", user1.age); // 输出：user age = 18

user1.age += 1;
print("user age = %d", user1.age); // 输出：user age = 19

user1.id = 200; // 编译错误，不可变属性无法修改

```

局部变量声明也支持同类型多名字写法，但它只是语法糖，每个变量仍然必须显式初始化：

```c
Int a = 1, b = 2, c = 3;
```

### 枚举类型（Enum）

```c
// 定义枚举类型，枚举值默认从0开始，底层类型为Int32
enum PetKind {
	dog,
  cat,
}

// 也可以指定部分值，其他的会自动递增
enum Priority {
    low = 1,
    medium,   // 自动变成 2
    high,     // 自动变成 3
}

// 显式指定值类型为UInt16
enum(UInt16) HttpStatus {
    ok = 200,
    created = 201,
    bad_request = 400,
    not_found = 404,
    internal_error = 500,
}

// 获取枚举值
print("enum value: %d", Int(PetKind.dog))

// 初始化
Priority priority = Priority.medium

// 从底层整数值尝试恢复 enum，失败时返回 null
Priority? parsed = Priority.init?(2)

// 通过类型推导，可以省略枚举名
HttpStatus status = .ok

switch (priority) {
	.low => print("priority value: %d", Int(priority))
  .medium => print("priority value: %d", Int(priority))
  .high => print("priority value: %d", Int(priority))
}
```

### 联合类型（Union）

Jiang 的 `union` 是安全的 tagged union：每个值都会携带当前 variant 的 tag，并且每个 variant 可以有自己的 payload。它不是 C 风格的 untagged/raw union。

`union` 可以复用已有 `enum` 作为 tag，也可以省略 tag enum，由编译器根据 variant 名自动生成隐式 tag。`enum` 只表示 tag/value set；`union` 表示 tag 加 payload 的 sum type。

```c
enum(UInt8) Kind {
  a = 1,
  b,
  c,
  d,
  e
}

union MyUnion {
	Int a;
  Double b;
  (Int, Int) c;
	Foo d;
	() e;

  struct Foo {
    Int x;
    Int y;
  }
}

// 使用
MyUnion x = MyUnion.a(123);

// 类型推导
MyUnion y = .b(3.15);

// 使用 switch 处理所有情况（编译器确保完整性）
switch (x) {
	// 单个语句可以不用 {}
  .a(value) => print("value = %d", value);

  // 多个语句必须用 {}
  .b(value!) => {
    value += 0.1;
    print("value = %f", value);
  }

  .c(v1, b2) => print("value = (%d, %d)", v1, v2);

  .d(v) => print("value = Foo {x: %d, y: %d}", v.x, v.y);

	else => break;
}

// 使用 if 判断
if (x is .a(value)) {
  print("value = %d", value)
}
```

`union` 可以显式绑定 tag enum，也可以省略并让编译器按成员名自动生成隐式 tag：

```c
enum Kind {
  a,
  b,
}

union(Kind) ExplicitResult {
  Int a;
  Int b;
}

union ImplicitResult {
  Int a;
  Int b;
}
```

规则：

- `union(TagEnum)` 的 variant 名必须能对应到 `TagEnum` 的成员。
- 省略 tag enum 时，编译器按 variant 声明生成隐式 tag。
- `union` variant 本身不单独声明 `public` / `private`，只由外层 `union` 是否公开决定。
- 如果 `union` 是 `public`，它的 variant 属于公开类型表面；如果 `union` 不公开，variant 也只在模块内可见。
- 如果需要隐藏 union 的部分实现细节，优先用 public `struct` 包装 private union/data。

同类型的多个 variant 也可以合并声明：

```c
union MyUnion {
  Int a, b, c;
  Float r;
}
```

`union` 的 payload 当前支持任意普通类型，包括：

- `struct`
- tuple
- array
- slice
- `Fn<...>`
- `T^`
- `T&`
- `T[*]`
- `T*`
- optional
- 其他 `union` / `enum`

`union` 也支持泛型：

```c
union Result<T, E> {
  T value;
  E error;
}
```



### 泛型（Generic）

Jiang 语言通常以 `<T>` 形式声明泛型参数。

`@where(...)` 是一种编译期约束注解，用于约束其后一个泛型声明中的类型参数。  
当前 `@where(...)` 支持以下约束项：

- `Name: Trait`
- `Name: !Trait`
- `Name: Trait<Assoc = Type>`
- `Name: TraitA & TraitB & TraitC`
- `Name == Type`
- `Name != Type`
- `Name.[Trait].Assoc == Type`

在泛型声明上，`@where(...)` 中引用的名字必须出现在后续声明的 `<...>` 泛型参数列表中。  
在 trait 内部，`@where(...)` 也可以引用当前 trait 可见的关联类型名。
关联类型绑定优先写在 trait bound 内部，例如 `@where(T: Sequence<Element = Int>)`；需要单独写 projection equality 时，使用显式 trait 投影，例如 `@where(T.[Sequence].Element == Int)`。
`Name == Type` / `Name != Type` 也可用于类型形状匹配，pattern 中的 `_` 表示单个 type argument
wildcard，例如 `@where(T == Box<_>)`、`@where(T != Option<_>)`。后缀语法糖在这里按 canonical
builtin type 匹配：`T[]` 匹配 `Slice<_>`，`T[N]` 匹配 `Array<_, _>`。
例如：

```c
@where(T: Numeric)
T add<T>(T a, T b) {
  return a + b;
}
```

```c
/// 定义泛型函数，其中 T 可以为 Int、Float 等数值类型
@where(T: Numeric)
T add<T>(T a, T b) {
  return a + b;
}

// 此时 T 推断为 Int
add(100, 200);
// 此时 T 推断为 Double
add(3.14, 9.8);
// 强制指定 T 为 Float
add<Float>(3.14, 9.8);

/// 定义泛型结构体
@where(T: Numeric)
struct Foo<T> {
  T value;

  T bar() {
    return self.value * 2;
  }
}

// 此时 T 推断为 Int
Foo x = { value: 123 };
// 此时 T 明确为 Float
Foo<Float> y = Foo<Float> { value: 3.14 };
// 也可以写成
_ z = Foo<Float> { value: 3.14 };
```

泛型参数的顶层 `!` 可变性约束当前只有显式 `Mutable` 模式：

- 默认不写约束时，泛型参数只接受**不带**顶层 `!` 的实参
- `@where(T: Mutable)` 表示该泛型参数**必须**带顶层 `!`

```c
@where(T: Mutable)
struct MutableBox<T> {
  T value;
}

MutableBox<Int!> a = MutableBox<Int!> { value: 1 };
```

其中：

- `Mutable` 是语言内建约束名
- 它由编译器识别，不是普通用户可实现的 trait
- 但表面语法仍然通过 `@where(T: ...)` 使用

当前规则只看**顶层** `!`：

- `Int` 不满足 `Mutable`
- `Int!` 满足 `Mutable`

### Trait

`trait` 用于定义一种**仅存在于编译期**的约束类型。  
它不作为运行时类型使用，也不能直接作为普通变量、字段、参数或返回值类型。
需要动态 trait view 时，使用编译器提供的 companion type：`Trait.Any`、
`Trait.VTable` 和 `Trait.Receiver`。

例如，`Numeric` 可以被定义为一个 trait，表示“所有数值类型”：

```c
trait Numeric;
```

编译器 core 会内建一组最小 trait，并把它们导出到默认 prelude。`std/prelude.jiang`
会继续导出标准库层的便利 API，但 `Hashable`、`Equatable` 这类 core trait
不依赖 std package 本身；后续 no-std 模式关闭 std prelude 时，这些 core trait
仍然有效。

默认 prelude 中常用 trait 例如：

- `Numeric`
- `FromStringLiteral`
- `Hashable`
- `Equatable`

其中 `Hashable` 和 `Equatable` 来自 compiler core，并通过 prelude 以普通 trait
DefId 暴露，所以可以直接用于 `@where(...)`。标准库 trait 也可以由
`std/prelude.jiang` 继续导出。

若一个 `public trait` 被 `public` 类型显式实现，那么模块外可以通过该 trait requirement 调用对应方法。  
若 trait 本身不是 `public`，则类型本身仍然可以对外可见，但外部不能通过该 private trait requirement 调用这些方法。

当前还要求：

- 只要 trait 本身是 `public`
- 那么用于实现该 trait requirement 的方法就必须显式写 `public`
- 这条规则同时适用于类型定义体内实现，以及 `extend Type: Trait { ... }` 中的实现
- 若一个类型同时声明多个带同名 requirement 的 trait：
  - 同名且签名完全一致：允许共存
  - 同名但签名不同：允许共存
  - 未限定调用时，按普通重载规则解析

用户也可以自定义 trait，并声明内部函数签名，用于约束满足该 trait 的类型必须提供对应实例方法：

```c
trait Equatable {
  static Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
  UInt64 hash();
}
```

trait 也可以继承一个或多个父 trait：

```c
trait HashEq: Hashable {
  static Bool equal(Self& lhs, Self& rhs);
}
```

当多个 trait 含有同名 requirement 时：

```c
trait AddInt {
  Int apply(Int delta);
}

trait FlagValue {
  Int apply(Bool flag);
}

struct Counter: AddInt, FlagValue {
  Int base;

  Int apply(Int delta) {
    return self.base + delta;
  }

  Int apply(Bool flag) {
    if (flag) {
      return self.base + 10;
    }
    return self.base;
  }
}

Counter counter = Counter { base: 30 };
Int a = counter.apply(2);
Int b = counter.apply(true);
```

这里：

- 同名不同签名的方法按普通重载规则区分
- 同名同签名的多个 trait requirement 可以共用同一份实现

每个 trait 都有三个保留的 companion type 名：

- `Trait.Any`：borrowed dynamic trait view，保存 erased receiver 和 vtable，不移动原值。
- `Trait.VTable`：某个 concrete type 对该 trait 的方法表。
- `Trait.Receiver`：vtable slot 使用的 erased receiver。

对应的 intrinsic type operation 为：

```c
Value.Any any = Value$.any(box);
Value.VTable vtable = Value$.vtable(Box);
Value.Receiver receiver = Value$.receiver(box);
Int a = any.value();
Int b = vtable.value(receiver);
```

0.4.1 支持 ref receiver trait instance method 的动态分派和 vtable slot 读取。
`@self(move)` / owned receiver trait object 暂不支持；如果 trait 中存在 move receiver
requirement，构造 `Trait.Any` / `Trait.VTable` / `Trait.Receiver` 会编译失败。

trait 还可以在 trait 体内部使用 `associated` 声明关联类型：

```c
trait Iterator {
  associated Element;
  Element? next();
}
```

关联类型可以直接写 bound：

```c
trait HasItem {
  associated Item: Hashable & Equatable;
  Item item();
}
```

子 trait 会自动继承父 trait 的关联类型，并可以继续对它加约束：

```c
trait Sequence {
  associated Element;
  associated Iter: Iterator<Element = Element>;
  Iter make_iterator();
}

@where(Iter.[Iterator].Element == Element)
trait ByteSequence: Sequence {
  Int next_int();
}
```

内建的 `SubscriptGet` / `SubscriptSet` trait 可用于让用户类型支持 `value[index]` 与可选的 `value[index] = new_value`：

```c
public trait SubscriptGet {
  associated Index: Equatable;
  associated Value;

  Value subscript_get(Index index);
}

public trait SubscriptSet: SubscriptGet {
  () subscript_set(Index index, Value value);
}
```

其中：

- 读取 `value[index]` 会映射到 `subscript_get(...)`
- 写入 `value[index] = new_value` 只有在类型显式实现 `SubscriptSet` 且实际提供 `subscript_set(...)` 时才成立
- `Value` 是否带 `!` 不再自动推出“可写下标”；读写由 trait 显式区分

- `Int`、`Float`、`Double` 等数值类型可以被视为满足 `Numeric`
- `Numeric` 本身不能直接写成 `Numeric x;`
- `Numeric` 主要用于 `@where(...)` 这类泛型约束位置
- trait 内部函数当前用于声明**约束签名**，不是运行时成员，也不是默认实现
- 用户自定义类型若要满足某个 trait，当前需要在类型定义处显式声明
- 仅仅“方法签名刚好匹配”并不会自动满足 trait
- 实现子 trait 的类型，会自动被视为也实现其父 trait
- trait 的关联类型使用 `associated Name;` 声明
- 关联类型 bound 可写作 `associated Item: Hashable;`
- 多个关联类型 bound 可写作 `associated Item: Hashable & Equatable;`
- 子 trait 会继承父 trait 的关联类型，且当前不允许重新声明父 trait 的同名关联类型
- 子 trait 可以通过 `@where(Item: Hashable)` 或 `@where(Item == UInt8)` 继续约束继承来的关联类型
- `@where(...)` 中多个 trait 约束也支持 `&`，例如 `@where(T: Hashable & Equatable)`
- `FromStringLiteral` 是 builtin trait。显式声明该 trait，且类型提供 `init(UInt8[]& bytes)` 后，可在有目标类型的上下文里直接写 `T x = "hello";`
- 若继承链中出现同名 requirement：
  - 同名且签名完全一致：允许合并
  - 同名但签名不同：编译报错
- 若 trait 继承未知父 trait，或出现继承环，编译报错
- 若继承链中出现同名关联类型：
  - 名字相同且约束兼容：视为同一个关联类型
  - 名字相同但约束冲突：编译报错
- 当前不支持关联类型默认类型

```c
trait HasValue {
  Int value();
}

trait HasDouble: HasValue {
  Int double_value();
}

struct Box: HasValue {
  Int inner;

  Int value() {
    return self.inner;
  }
}
```

类型实现带关联类型的 trait 时，需要在实现体中显式绑定关联类型：

```c
trait Iterator {
  associated Element;
  Element? next();
}

struct Counter: Iterator {
  associated Element = UInt8;

  UInt8? next() {
    return 42;
  }
}
```

如果一个实现块同时涉及多个带同名关联类型的 trait，可以用限定形式消歧：

```c
trait Left {
  associated Item;
  Item left();
}

trait Right {
  associated Item;
  Item right();
}

struct Pair: Left, Right {
  associated Left.Item = UInt8;
  associated Right.Item = UInt8;

  UInt8 left() {
    return 20;
  }

  UInt8 right() {
    return 22;
  }
}
```

如果不限定 trait 名，而同一个实现块里多个 trait 都带来同名关联类型，则会按歧义处理并报错。

同样地，`extend` 中也可以绑定关联类型：

```c
trait Iterator {
  associated Element;
  Element? next();
}

struct Counter {}

extend Counter: Iterator {
  associated Element = UInt8;

  UInt8? next() {
    return 42;
  }
}
```

### Extend

可以在类型定义之后使用 `extend` 补普通方法，或补显式 trait 实现：

```c
trait HasValue {
  Int value();
}

struct User {
  Int id;
}

extend User {
  Int id_value() {
    return self.id;
  }
}

extend User: HasValue {
  Int value() {
    return self.id;
  }
}
```

当前 `extend` 的限制：

- method 不进入模块顶层命名空间
- method body 中的 `self` 默认是 receiver `Self&`；`@self(move)` 方法中 `self` 是拥有值 `Self`
- 字段能否赋值只由字段类型本身是否 mutable 决定
- 默认 `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`；`@self(move)` 方法会消耗 `value`
- receiver 已经是 pointer 时，`ptr.method(args...)` 与 `value.method(args...)` 使用同一套 lookup
- `Type.method(receiver, args...)` 是显式方法调用形式
- `extend Holder<T>` / `extend Box<T>` 可以声明 generic receiver extension；`extend Holder<Int>` 这类 specialized target 暂不支持，使用 `@where(T == Int) extend Holder<T> { ... }`
- receiver 形状约束写在 `@where` 中，例如 `@where(T == Box<_>) extend Holder<T>` 或
  `@where(T != Option<_>) extend Holder<T>`；`_` 只匹配一个 type argument。
- `T^` / `T&` / `T*` / `T[*]`、`T[]` / `T[:S]`、`T[N]` / `T[N:S]` 等语法糖作为类型
  receiver 或 where pattern 时，会按对应 canonical builtin owner 查找和匹配 extension，例如
  `UInt8[]^` 可查找 `Box<UInt8[]>` 上的方法，`UInt8[3]` 可匹配 `Array<UInt8, _>`。
- `public extend` 可以跨模块传播；普通 `extend` 只在声明模块和直接导入者可见。`public import` 会继续 re-export imported module 的 public extensions
- union variant name 和同一 union 的 method name 不能重名，避免 `Union.member(...)` 歧义
- 同名 method 可以 overload，但参数数量或参数类型必须不同
- `extend Type: Trait { ... }` 会做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现
- 不支持 `init`
- 不支持 `deinit`
- 完整 trait solving 仍需继续补齐

### 模块（Module）

Jiang语言的模块以文件为单位，文件内的所有定义都属于该模块

#### 模块导入

下面将基于以下目录结构展开讲解：

```c
|-- src
		|-- main.jiang
	  |-- utils
				|-- math.jiang
```

- math.jiang文件

```c
// src/utils/math.jiang

// public关键字表示这是个公开方法，可以在模块外调用，否则只能在文件内使用
public Int max(Int a, Int b) {
    if a > b {
        a
    } else {
        b
    }
}

public Int min(Int a, Int b) {
    if a < b {
        a
    } else {
        b
    }
}

// foo只能在文件内部使用
() foo() {
    Int a = 1;
    Int b = 2;
    Int c = 3;
    _ maximum = max(a, b);
    _ minimum = min(b, c);
}
```

- main.jiang文件

```c
// src/main.jiang

/// 以下几种方式分别展示了import和alias的用法

// 1.导入模块，默认将使用文件名'math'作为模块名
import "utils/math.jiang";
math.max(100, 200);

// 2.导入的同时并对外导出math模块
public import "utils/math.jiang";
math.max(100, 200);

// 3.导入模块，并使用snake_case别名作为模块名
public import math_utils = "utils/math.jiang";
math_utils.max(100, 200);

// 4.导入模块后，可以通过alias为模块中的公开符号创建本地别名
import math_utils = "utils/math.jiang";
alias maximum = math_utils.max;
maximum(100, 200);

// 5.public alias会在当前模块中重新导出该符号
import math_utils = "utils/math.jiang";
public alias max = math_utils.max;
public alias min = math_utils.min;

```

其中：

- `import "utils/math.jiang";` 会导入整个模块，并默认使用文件名 `math` 作为模块名
- `public import "utils/math.jiang";` 会在导入模块的同时，将模块名 `math` 对外导出
- 显式 `import alias = "..."` 中的 `alias` 约定使用 snake_case
- `import math_utils = "utils/math.jiang";` 会导入模块并使用 snake_case 别名 `math_utils` 作为模块名
- `public import math_utils = "utils/math.jiang";` 会导入模块并使用 `math_utils` 作为公开模块名
- `alias maximum = math_utils.max;` 会为符号创建一个当前模块内可见的别名
- `public alias max = math_utils.max;` 会为符号创建一个公开别名，使其他模块可以通过当前模块访问该符号

#### Package

除了直接编译单个 `.jiang` 文件，编译器也支持把一个目录当作 package 入口：

```bash
jiangc --emit-llvm path/to/pkg
jiangc --emit-obj path/to/pkg -o pkg.o
jiangc path/to/pkg -o pkg
```

当输入路径是目录时，编译器会读取该目录下固定文件名的 `package.ini`。当前识别：

- `[package].name`
- `[package].root`
- `[package].version`
- `[dependencies]`

这些 package 字段都可选：

- `name` 未写时，默认取当前目录名
- `root` 未写时，默认取 `<name>.jiang`
- `version` 未写时，manifest 内保留为空；编译器自身 release 构建从根 `package.ini` 读取版本
- 若显式写了，则覆盖默认值
- `name` 无论显式还是默认值，都必须满足 Jiang lexer 的标识符规则：ASCII 字母或 `_`
  可作为首字符，ASCII 数字可作为后续字符，UTF-8 标识符字符也可作为首字符和后续字符。
- `version` 目前允许 ASCII 字母、数字、`.`、`_`、`+`、`-`。

例如：

```text
lexer/
  package.ini
  lexer.jiang
```

最小 `package.ini`：

```ini
[package]
```

此时默认：

- `name = lexer`
- `root = lexer.jiang`

也可以显式覆盖：

```ini
[package]
name = frontend
root = src/main.jiang
version = 0.4.1
```

当前第一版 package 机制还支持本地依赖：

```ini
[dependencies]
util = ../util_pkg
```

在 package 内部，可以直接：

```c
import util;
```

这会导入依赖 package 的入口模块。当前本地依赖只支持这种“按依赖名导入 package root”的形式；还不支持 `dep/submodule` 这类更细路径。

注意：

- package import 不能加引号，必须写成 `import util;`
- 带引号的 `import "..."` 只用于当前 package 内的文件路径导入
- 跨 package 不能用字符串路径直接导入 source，必须通过 `[dependencies]`
- package dependency cycle 不允许；同一 package 内的 module import cycle 允许
- package 对外只暴露 root file 的 public namespace
- root file 可以通过 `public import` 重新导出模块 namespace，也可以通过 `public alias`
  重新导出具体 public symbol
- 非 root module 的 public 声明不会自动成为 package API
- dependency package 中的 `main` 不会成为当前 package 的 hosted entry wrapper

`alias` 是纯符号别名，而不是新的变量绑定。它用于给已经存在的符号路径起一个新的名字。

```c
import math_utils = "utils/math.jiang";

alias maximum = math_utils.max;
public alias minimum = math_utils.min;
```

上面的 `maximum` 和 `minimum` 都直接指向原始符号，不会创建新的函数、副本或存储空间。

当前建议 `alias` 的右侧只能是可命名的符号路径，例如：

```c
alias Foo = A.B;
alias max = math_utils.max;
public alias read = io.read;
```

而不应该是任意表达式：

```c
// bad
alias x = a + b;
```

使用 `public alias` 重新导出符号时，目标符号本身必须是源模块中的公开符号。如果当前模块已经存在同名定义，则应当报错，除非显式更换别名。

同名 alias 可以省略右侧目标：

```c
public alias Bool;
```

它等价于：

```c
public alias Bool = Bool;
```

### FFI

传给 C 风格 API 的字符串优先使用 `UInt8[*:0]` 表示。字符串字面量在 `UInt8[*:0]` 上下文中会生成以 `\0` 结尾的只读全局数据。`UInt8[*]` 是普通裸 many pointer，不直接接收字符串字面量。

如果需要借用 NUL 结尾的只读字节序列，使用 `UInt8[:0]&`；如果需要传给 C ABI，使用 `UInt8[*:0]`。

```c
extern {
  public Int open(UInt8[*:0] path, Int options);
  public Int write(Int fd, UInt8[]& buf, Int count);
  public Int errno;
}

```

也支持单条声明：

```c
extern public Int puts(UInt8[*:0] text);
public extern Int errno;
```
