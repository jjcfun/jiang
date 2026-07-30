# Jiang语言指南

> **过时文档，不再维护。**
>
> 这个单文件指南保留为历史参考和官网内容迁移来源。新的用户文档入口是
> [jiang-lang.org](https://jiang-lang.org/)。仓库内仍维护 `grammar.md`、
> `language-design.md`、`std.md` 和 `compiler/` 下的开发文档。

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

`Self` 是类型位置的特殊名字。`self` 是类型内部实例函数、`init` 和 `deinit` 的显式参数名，
表示当前 receiver 或初始化目标。没有 `self` 参数的类型内部函数是类型函数。

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

普通存储是否可重新赋值由绑定名后的 `!` 表示。类型级 `!` 当前只用于 `T&!` 与 `T*!`：

```c
Bool flag! = true;
flag = false;

Int[3] mutable_values! = [1, 2, 3];
mutable_values = [4, 5, 6];

Int[3] mutable_items! = [1, 2, 3];
mutable_items[0] = 10;
```

类型推导默认得到不可重赋值 binding；需要可写 binding 时写 `_ value!`。绑定可变性不进入
`TypeId` 或函数签名。`T&!` 是唯一可变引用，`T*!` 是可写 raw pointer；其他类型不能带 `!`。
普通变量定义不接受左侧 `ref`。引用值由 RHS 显式创建：

```c
Int& borrowed = value$.ref();
Int&! unique = mutable_value$.mut_ref();
```

`ref` / `ref!` 保留给 pattern binding；例如 `(ref borrowed) = value;` 和
`(ref! unique) = mutable_value;`。类型位可以省略，分别等价于 `ref _ borrowed`
和 `ref! _ unique`。绑定名后的 `!` 仍只表示新 binding 可重新赋值。
语言没有 `unique` 参数关键字；`unique` 可以作为普通标识符。

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

// 绑定名后紧跟'!'号，表示该存储位置可重新赋值
Bool foo! = true;
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
- 字符串字面量按 UTF-8 字节序列处理，默认类型为 `UInt8[:0]&`；backing storage 自动追加末尾 `0`，但 sentinel 不计入 length。当前可用于 `UInt8[_]` / `UInt8[]&` / `UInt8[:0]&`，也可按 expected type 转成 `UInt8*` 或 `UInt8[N:0]`
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

Optional 只使用 `T?` 表示；compiler-owned constructor 名称不对外开放。

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

if a1 is .some(Int x!) {
  // 这里 x binding 可重新赋值
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
defer {
    unsafe {
        handle$.dealloc();
    }
}

defer {
    log("closing");
    unsafe {
        handle$.dealloc();
    }
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
- `a$.mut_ref()`：从可写 place 创建唯一可变引用 `T&!`
- `a$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `T*`，需要 `unsafe`
- `a$.mut_ptr()`：从可写 place 创建可写 raw pointer `T*!`，需要 `unsafe`
- `a$.get()`：显式解引用 `T^` / `T&`，返回指向的值
- `a$.move()`：显式转交当前变量的值，源变量随后失效且不再析构
- `a$.addr()`：获取值 `a` 的裸指针，需要 `unsafe`
- `a$.dealloc()`：对 `a` 做释放操作，需要 `unsafe`
- `Int$.size()`：获取类型 `Int` 的大小
- `Int$.align()`：获取类型 `Int` 的 ABI 对齐
- `Int$.max_align()`：获取当前内建分配器保证支持的最大 Jiang 类型对齐
- `Int$.alloc()`：分配一个未初始化的 `Int*!`，元素数为 `1`
- `Int$.alloc(10)`：分配一个包含 `10` 个未初始化元素的 `Int*!`

在当前设计中，许多原本会被写成内建函数的操作，都会逐步迁移到隐式操作层。例如，类型大小不再写作 `size_of(T)`，而统一写作 `T$.size()`。

隐式操作层中的一部分 primitive 能力由编译器 builtin 或标准库提供，例如：

- `T$.size()`
- `T$.align()`
- `T$.max_align()`
- `T$.alloc()`
- `T$.alloc(...)`
- `value$.ref()`
- `value$.mut_ref()`
- `value$.ptr()`
- `value$.mut_ptr()`
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
当前裸指针获取、裸指针转换和显式释放等低层操作已经接入 `unsafe` effect。调用这些操作时，
需要放在 `unsafe { ... }` 中，或由外层 unsafe 函数承接。

```c
Int addr = 0x12345678;
Int* ptr = unsafe {
    addr$.as(Int*)
};
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

#### 数组可写性

数组 binding 可写时，可以修改元素或整体重新赋值：

```c
Int[_] b! = [1, 2, 3] // b: [1, 2, 3]

b[1] = 4 // b: [1, 4, 3]

Int[_] c! = [1, 2, 3]
c = [4, 5, 6]

// 数组在堆上创建
Int[_]^ e! = new [1, 2, 3]
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

Jiang 区分共享引用与唯一可变引用，并对二者执行静态 borrow check。
指针语义约定为：

- `T^`：自动解引用的 owning pointer；它不是 C 风格 raw pointer
- `T&`：shared non-owning reference，不承担释放职责，也不授予写能力
- `T&!`：唯一可变 non-owning reference，存活期间排斥重叠的共享/可变借用
- `T*`：裸指针，主要用于 FFI / ABI / 低层 capability 场景；可在 `unsafe` 中按下标读取，
  `T*!` 还可写入
- `T[]&`：borrowed slice view，语义上类似 `{ T*, length }&` 的连续内存引用视图，
  不表达所有权；裸 `T[]` 是 unsized array type，不能作为普通 value
- `T[:0]&`：sentinel borrowed slice view，layout 与 `T[]&` 一样是 `{ data, length }`，并额外
  保证 `data[length] == 0`；裸 `T[:0]` 是带 sentinel 的 unsized array type，不能作为普通 value

只有 `T^` 表达语言级所有权。`T&`、`T&!`、`T[]&`、`T*` 和 `T*!` 都不拥有目标对象。
类型后缀 `!` 当前只允许写在 reference 和 raw pointer 外层，即 `T&!` 与 `T*!`，并进入签名。
`T name!` 中绑定名后的 `!` 只允许重新赋值，不进入签名。

#### Pointer / 引用类型

Pointer / reference 类型也遵循 **从左往右，从里到外** 的原则。`^` 表示 owning pointer 外层；
`&` 表示 reference 外层。只有 reference 和 raw pointer 外层可以继续写 `!`。

```c
// 在栈中开辟内存空间
Int a = 123;

// new关键字可以创建拥有所有权的对象，并返回一个 owning pointer
Int^ b = new Int(123);

// 创建数组，并返回一个 owning pointer
Int[3]^ c = new [1, 2, 3];

// 临时引用
Int& d = a$.ref();

Int?& maybe_ref;    // reference to optional Int
Int^ owner_slot!;   // owning pointer 绑定本身可重新赋值
Int&! mut_ref;      // 唯一可变引用
Int*! mut_ptr;      // 可写 raw pointer

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

使用 `T^` 时默认自动解引用，除非 expected type 本身与 owning pointer 类型一致。`T&`、`T&!`、
`T*` 和 `T*!` 不参与默认自动解引用。Jiang 没有前缀手动解引用语法，表达式位置的 `*ptr`
这种写法不成立；
需要显式解引用或写入单对象指针/引用时使用 `ptr$.get()` / `ptr$.set(value)`。其中 `ptr$.set(value)`
要求 `T&!` 或 `T*!` 写能力；`T&` 和 `T*` 只能读取。

类型推导会保留 pointer/reference 层，并默认得到不可变绑定；写出 expected type 只会让 `T^`
触发自动解引用，`T&` / `T*` 仍需要显式 `$.get()`：

```c
Int&! ref = value$.mut_ref();
Int copied_value = ref$.get();
Int explicit_value = ref$.get(); // 显式解引用
```

`$` 操作符会阻止自动解引用，并进入隐式操作层。通过类似 `ptr$.dealloc()`、`ptr$.ref()`、`ptr$.ptr()`、
`ptr$.get()`、`ptr$.set(value)` 的语法，可以调用 pointer/reference 自身的一些低层操作。

```c
Int a = 100;

Int^ b = new Int(200);

// owning pointer 默认自动解引用，b 直接表示其元素的值
Int c = a + b;

print("c = %d", c); // 输出： c = 300

// '$' 符号阻止自动解引用，并进入 b 的隐式操作层
unsafe {
    b$.dealloc();
}
```

`$.ref()` 和 `$.ptr()` 的返回类型固定为 `T&` 和 `T*`：

```c
Int^ p = new Int(41);

_ ref = p$.ref(); // ref: Int&
unsafe {
    _ raw = p$.ptr(); // raw: Int*
}

// 普通值上下文会自动解引用
Int x = p + 1;

// 进入隐式层后返回的引用不会再次自动解引用
Int bad = p$.ref() + 1; // 编译错误

// 显式解引用
Int explicit = p$.get();

unsafe {
    Int*! raw_ptr = p$.mut_ptr();
    Int raw_value = raw_ptr$.get();
    raw_ptr$.set(42);
}

Int[1] items = [41];
Int* raw = unsafe {
    items$.ptr()
};

Int y = unsafe {
    raw[0]
};
```

`T*` raw pointer 只能通过 `$.get()` 显式读取单个目标对象；只有 raw pointer 外层带 `!` 的
`T*!` 才允许通过 `$.set(value)` 写入。raw pointer 也可以在 `unsafe` 中通过下标表达式取元素：

```c
Int[2] mutable_items! = [41, 0];

unsafe {
    Int* ptr = mutable_items$.ptr();
    Int*! writable = mutable_items$.mut_ptr();
    _ raw = ptr;                   // raw: Int*
    Int value = ptr[0];
    writable[1] = 42;

    _ item_ref = ptr[1]$.ref();    // item_ref: Int&
    _ item_ptr = ptr[1]$.ptr();    // item_ptr: Int*
}
```

`T*` 支持 unsafe 下标读，`T*!` 支持 unsafe 下标读写，但当前不提供 `offset()` 这类额外指针算术语法。
`Void*` / `Void*!` 只能传递、比较和转换，不能 `$.get()`、`$.set()` 或下标访问；访问前必须先
转换为具有具体元素类型的 raw pointer。
旧的 `T[*]` / `T[*:S]` many pointer 类型已经移除：低层地址统一使用 `T*` / `T*!`，需要
length 或 sentinel 保证时使用 `T[]&` / `T[:S]&`。

除数组、slice、raw pointer 外，显式实现 `SubscriptGet` trait 的用户类型也支持 `value[index]`
语法；如果该类型还显式实现 `SubscriptSet`，则支持 `value[index] = new_value`。

`^` owning pointer 可通过 `ptr$.dealloc()` 主动释放默认堆分配器上的对象；`&` 引用只是非 owning 引用，不参与释放。
`ptr$.dealloc()` 是低层释放操作，不作为普通析构入口使用。目标语言的自动析构由作用域退出、`T^` 字段析构和类型的 `deinit()` 规则处理。

```c
// 定义一个 owning pointer，指向堆内存
Int^ a = new Int(100);

// 可以主动释放 owning pointer 管理的内存空间
unsafe {
    a$.dealloc();
}
```

### 所有权、implicit copy 和析构

Jiang 的 borrow checker 同时检查所有权/lifetime/drop safety 与 `T&!` 的唯一可变借用。
共享引用可以共存；活跃的 `T&!` 会排斥重叠的共享/可变借用，并阻止直接访问来源 place。
引用最后一次使用后，来源 place 可以恢复访问。raw pointer 不参与这项别名证明。

函数省略 `@life` 且返回 Shape 非空时，readonly `self` / `Self&! self` reference receiver
优先成为完整默认来源。没有该特例时，只有恰好一个用户可见参数 root 的 Shape 非空且与返回
Shape 兼容，才默认使用该完整 root；一个多-region 参数仍算一个 root。`Self self` 按值
receiver 只参与普通唯一 root 规则。零个非空 root 时必须用 `@life()` 确认空契约；两个或更多
非空 root，或唯一 root Shape 不兼容时必须显式标注。默认规则只读取公开签名，不读取函数体；
任意显式 `@life(...)` 都完全替换默认返回契约。`@life(return: input)` 只传播 `input`
值已经携带的 borrow，不能延长按值参数局部槽的生命周期；不含 borrow 的参数约束为空。

struct / union 使用 `@region` 显式声明公开 lifetime shape。单一 lifetime slot 字段写作
`@life(a)`；字段类型具有多个公开 region 时可以按位置绑定，也可以使用具名映射：

```jiang
@region(a, b)
struct Pair {
    @life(a)
    Int& first;

    @life(b)
    Int& second;
}

@region(a, b: a)
struct Wrapper {
    @life(a, b)
    Pair direct;

    @life(a: b, b: a)
    Pair reversed;
}
```

`b: a` 在声明 `b` 的同时表示 `a` outlives `b`。每个 target 在 `@region` 中只出现一次；
source 也必须由同一 annotation 声明，但可以写在 target 之前或之后。coverage 允许成环，
`@region(a: b, b: a)` 表示两个 region 互相覆盖。每个 region 都必须由字段或 union payload
的实际 slot 直接使用。字段 binding 的 target 必须唯一且完整；named 模式不能与位置模式
混用，也不能使用 `self` source。

高阶函数可以用 `Fn` / `RawFn` 的契约名描述 callback 的返回来源：

```jiang
@life(callback.result: callback.value, return: value)
Int& apply(Fn<Int& result, Int& value> callback, Int& value);
```

callable contract 只能引用 result/参数的声明名；需要参与 contract 的位置必须命名。
不支持 `callback[0]` 一类位置路径。ABI 隐藏的 closure environment、receiver adapter
和 continuation 不能出现在公开 contract 中。

契约名本身不参与类型身份；解析后的来源关系参与 callable 的语义兼容性。因此，返回其他参数
借用的函数或 lambda 不能传给上述 `callback`。

聚合返回值必须整体映射，例如 `@life(return: (left, right))`；不能拆成
`return.left`、`return.right` 多条 target。聚合参数的单个 region 使用公开名称选择，
例如 `value.second`。tuple/Fn 不支持 `[0]` 一类位置式 lifetime path。

`Fn<R, Args...>` 和 `RawFn<R, Args...>` 默认使用空 lifetime 契约：`R` 不能借用 callback
参数。允许 callback 返回参数 borrow 时，必须像上例一样显式声明
`callback.result: callback.value`。这条规则不同于普通函数的 signature elision。

- `T^` 是 owning pointer，拥有堆上对象，并参与自动析构。
- `T&` 是 non-owning reference，不拥有资源，不参与自动析构。
- `T*` / `T*!` 是低层指针；`T[]&` 是 slice reference。它们不表达语言级所有权。裸 `T[]` 是 unsized array type，不是可独立存放的 reference value。
- runtime drop 由 ownership、字段和自定义 `deinit` 决定；`!Movable` 值仍会在原 place 正常析构。
- `T&`、`T&!`、`T*`、`T*!`、`T[]&` 字段不会被编译器自动释放。
- `T[]&` 本身不拥有整段 buffer；drop slice reference 时不 drop 全部元素。但 `slice[i]` 是已初始化元素 place，覆盖时按元素类型的 drop 规则处理旧值。
- 经过 `T*!` 得到的 place 是裸指针派生 place，写入时是 raw write，不隐式 drop 旧值。
- 如果 nominal 有自定义 `deinit`，先执行自定义 `deinit`，再执行编译器生成的递归字段析构。
- `Movable` 是默认 auto trait；nominal 可用 `!Movable` 保持地址固定，包含它的聚合也不可移动。
- `Copyable` 继承 `Movable`。基本标量和 enum 默认 Copyable；struct/union 必须显式实现 Copyable，
  且所有字段或 payload 也必须 Copyable。带自定义 `deinit` 的类型不能 Copyable。
- 非 Copyable、但 Movable 的值在普通按值位置默认 move；Copyable 值默认 copy。
- `$.move()` 可以显式转移非 Copyable 值，也可以强制 move Copyable 值；源 place 随后失效。
- 泛型代码只有在 `T: Copyable` 约束下才能依赖隐式复制。

显式转移所有权使用 `move()`：

```c
Buffer^ a = new Buffer();
Buffer^ b = a$.move();
// move 后 a 失效，离开作用域时不会再析构 a
```

### 切片（Slice）

`T[]` 是长度在运行时确定的 unsized array type。它描述一段连续 `T` 元素序列，但裸 `T[]`
不能作为普通 value 单独存放或传递。`T[]&` 是借用的 slice view，运行时 layout 类似
`{ data: T*, length }`，不拥有元素和 buffer，并要求被引用存储的 lifetime 覆盖 view 的使用范围。
`T[]^` 是 owned unsized array，拥有已初始化的 buffer，drop 时会按元素类型逐个析构并释放底层
allocation。`T[:S]` 同样是 unsized array type；`T[:S]&` 是带 sentinel 保证的 borrowed view。
标准库 `Vector<T>.slice()` 返回借用 `T[]&` 视图；`Vector<T>.into_slice()` 会消耗 `Vector`，
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

// by-value 解构必须保留类型位置；借用 binding 可省略类型位
(ref first, Int second) = pair;

// type pattern 支持局部推导
(Int[_] left, _[3] right) = arrays;
```

#### 一元组

只有一个元素的元组被称之为一元组，如：`(Int)`。一元组有个特性，即这个元组与它的元素是等价的，占用的内存空间也一致。

从数学上不难看出：`(1 + 1) = (2) = 2`，这里的 `(2)` 和 `2`相等。将`(2)`看成一元组, 自然推断出这个结论。

```c
// 以下两个函数签名等价
(Int) add(Int a, Int b);
Int add(Int a, Int b);

// 以下两种语法也等价
(Int x!) = add(1, 2);		// 解构出可重赋值 binding
Int x! = add(1, 2); 		// 定义可重赋值 binding
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
UInt8* c_str = "hello";
```

字符串字面量的底层存储会自动追加末尾 `0`，但 `length` 不包含这个 sentinel。例如 `"hello"` 作为 `UInt8[:0]&` 时，`length == 5`。`UInt8[5:0]` 的逻辑长度是 5，但实际 storage 是 6 个 `UInt8`，因此 `UInt8[5:0]$.size() == 6`。需要 C 风格 NUL 结尾指针时使用 `UInt8*`；raw pointer 类型本身不记录 sentinel。

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

Jiang 支持位置参数、命名参数和默认参数：

```c
Int add(Int base = 1, Int extra) {
    return base + extra;
}
```

规则如下：

- 位置参数按定义顺序匹配
- 默认参数可以出现在参数列表任意位置
- 位置参数不会按类型跳过默认参数，而是绑定最早尚未绑定的参数
- 当前默认值只支持 literal，并按参数 expected type 检查
- 命名参数使用 `name: value`，可以重排或跳过带默认值的参数
- 命名参数出现后，后续普通参数也必须使用命名形式
- overload 决议必须能按参数数量和参数类型区分候选，否则诊断为歧义

```c
add(10, 20);
add(extra: 20);
```

#### 函数调用

```c
Int[_] list = [5, 3, 4, 1, 2];

sort(list) { left, right => left < right };
```

#### 函数签名

```c
// 排序
Int[]& sort(Int[]& list, RawFn<Bool, Int, Int> compare)

// 支持泛型的排序，其中 T 需要实现 Numeric
@where(T: Numeric)
T[]& sort<T>(T[]& list, RawFn<Bool, T, T> compare)

// 支持泛型的可错排序，其中 E 可以为任意错误类型
@where(T: Numeric)
T[]&@E sort<T, E>(T[]& list, RawFn<Bool@E, T, T> compare)
```

#### 函数指针

`RawFn<R, A, B, ...>` 表示函数指针类型：

- 第一个类型参数是返回类型
- 后续类型参数按顺序表示参数类型

例如：

```c
RawFn<Bool, Int, Int> compare;
```

表示：

- 返回 `Bool`
- 接收两个 `Int` 参数

callable 类型可以为 result 和全部参数提供契约名：

```jiang
Fn<Int result, Int value> transform;
RawFn<Bool result, Int left, Int right> compare;
```

契约名按需提供，但已经提供的名称不能重复。它们只用于 `@life` 等 callable 契约、文档和诊断，不是调用参数标签，
也不进入函数类型身份或 ABI。因此仅契约名不同的两个 `Fn` / `RawFn` 类型仍是同一函数类型。

当前支持：

- 普通顶层函数衰减为 `RawFn<...>`
- 类型函数衰减为 `RawFn<...>`
- 实例方法通过 `Type.method` 衰减为 `RawFn<Ret, Receiver&, Args...>`
- `RawFn<...>` 的返回类型可以写成 `T@E`
- 通过 `RawFn<...>` 变量进行调用
- 非捕获 lambda 表达式赋值给 `RawFn<...>`
- 若同名函数/方法存在多个重载：
  - 调用时按参数个数和参数类型**精确匹配**
  - 返回类型不参与重载决议
  - 将函数值赋给 `RawFn<...>` 时，目标 `RawFn<...>` 类型会参与消歧
  - 若没有目标类型上下文（例如 `_ f = foo;`）且存在多个重载，则编译报歧义

示例 1：顶层函数

```c
Bool less(Int left, Int right) {
    return left < right;
}

RawFn<Bool, Int, Int> compare = less;
Bool ok = compare(1, 2);
```

示例 2：类型函数

```c
struct Math {
    Bool less(Int left, Int right) {
        return left < right;
    }
}

RawFn<Bool, Int, Int> compare = Math.less;
Bool ok = compare(1, 2);
```

示例 3：实例方法（未绑定方法值）

```c
struct User {
    Int id;

    Int add(self, Int extra) {
        return self.id + extra;
    }
}

RawFn<Int, User&, Int> add = User.add;

User user = User(id: 40);
Int value = add(user$.ref(), 2);
```

这里 `User.add` 是未绑定实例方法：

- 第一个参数是接收者引用 `User&`
- 后续参数与方法声明中的普通参数保持一致
- 当前需要显式传入 `user$.ref()`

示例 4：返回 errorable result 的函数指针

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

RawFn<Bool@Err, Int, Int> compare = less;
```

示例 5：重载函数值按 `RawFn<...>` 目标类型消歧

```c
Int add(Int value) {
    return value + 1;
}

Int add(Int left, Int right) {
    return left + right;
}

RawFn<Int, Int> inc = add;
RawFn<Int, Int, Int> sum = add;
```

示例 6：lambda 表达式

```c
RawFn<Int, Int> inc = { value => value + 1 };
RawFn<Int, Int, Int> add = { left, right => left + right };
RawFn<Int> answer = { => 42 };

Int base = 10;
Fn<Int, Int> add_base = { [_ captured = base] value => value + captured };
Fn<Int> borrowed = { [ref value = base] => value$.get() };
Fn<Int>^ owned = new { [_ captured = base] => captured };
```

lambda 规则：

- lambda 使用 `{ [captures] args => body }`
- 参数只写 binding name；完整 expected `RawFn<...>` / `Fn<...>` 类型提供参数和 result 类型
- 单参数写作 `{ x => x }`
- 无参数写作 `{ => expr }`
- `=>` 后可以写表达式或多条 block statement
- `RawFn` 不携带 environment，因此只接受非捕获 lambda
- `Fn` 是 callable view，可以捕获外层 local；`Fn^` 是可移动的 owned heap closure
- 可选 capture list 写在参数之前，值 capture 遵守 copy/move，`ref` / `ref!` capture 遵守 borrow/lifetime
- 未列入 capture list 的外层 local 仍按默认捕获规则处理

当前不支持：

- 通过实例值获取绑定方法函数值（例如 `value.method`）
- `init` 转函数指针

#### 异步函数（Async）

```c
@where(T: Numeric, E2: CompareError)
async T[]&@E1 sort<T, E1, E2>(
    T[]& list,
    RawFn<async RawFn<async Bool@E2, T, T>@E1, T[]&> compare
)

@where(T: Numeric, E2: CompareError)
@alias(Cmp = RawFn<async Bool@E2, T, T>)
async T[]&@E1 sort<T, E1, E2>(
    T[]& list,
    RawFn<async Cmp@E1, T[]&> compare
)
```

`unsafe` 和 `async` 可以写在 `RawFn<...>` 的返回类型前，表示这个函数类型带有对应调用效果：

```c
RawFn<async Bool>[]& callback_list;
RawFn<unsafe Int, Int>[]& unsafe_callbacks;
```

`RawFn<async Bool>[]&` 表示“元素为异步函数指针的切片引用”。`unsafe` 和 `async` 不修饰
`Bool` 这个返回值类型，而是修饰外层 `RawFn<...>` 函数类型。如果函数本身异步并返回切片引用，应写成：

```c
RawFn<async Bool[]&> load_callbacks;
```

闭包值使用 `Fn<...>`。lambda 必须有完整的 expected callable type；异步、unsafe 和 Domain
effect 均由该类型决定，lambda 表达式本身不重复书写：

```jiang
Fn<async [global_domain] Int, Int> load = { id => fetch(id) };
```

async `Fn` / `RawFn` 动态调用复用普通 async 函数的 Domain 切换。跨 Domain 的参数、result 和 capture
必须满足 Sendable，普通 borrow 不能跨不兼容 Domain 逃逸。

`Sendable` 与 move/copy 是三件独立的事：

- 直接移动一个值要求它同时满足 `Movable` 和 `Sendable`；复制则要求 `Copyable` 和
  `Sendable`。
- `T: Sendable + !Movable` 不能直接按值转移，但可以放在 heap 上，通过移动 `T^` owner
  handle 跨 Domain；pointee 的地址不会变化。
- `T&` 和 `T&!` 仍是受 lifetime 限制的普通 borrow，不会因为 `T: Sendable` 就能跨 Domain。

tuple、定长 array、optional、errorable result、Task 和用户声明的 aggregate 都会递归检查
payload。raw pointer 不自动满足 `Sendable`，只能留在显式 `unsafe` 边界中。共享可变状态应使用
`Atomic<T>`、`Mutex<T>^` 等具有明确同步契约的 handle，而不是让普通 borrow 逃逸：

```jiang
Mutex<Int>^ counter = new Mutex<Int>(0);
Task(domain: global_domain) {
    counter.with_lock { value =>
        value$.set(value$.get() + 1);
    };
};
```

domain-bound owned closure 也遵守同一规则。创建 `Fn<async [domain] (...)>^` 时，它的每个
capture 都必须能以对应的 move/copy 方式安全进入目标 Domain；borrow capture 不能借 closure
owner 延长 lifetime。

调用带 `unsafe` effect 的函数需要进入显式 effect context：

```c
Int value = unsafe {
    unsafe_callback(1)
};
```

异步函数调用默认是隐式挂起点，调用结果仍是函数声明的返回类型：

```c
async Int load_page();

async Int render() {
    Int page = load_page();
    page + 1
}
```

最外层从普通函数进入异步运行时，使用 `sync [main_domain]`。`main_domain` 绑定程序启动线程，
`global_domain` 使用进程共享的并发执行器：

```jiang
async [global_domain] Int load_page(Int id) {
    id
}

Int main() {
    sync [main_domain] {
        Task<Int> page = Task(domain: global_domain) {
            load_page(1)
        };
        page.await()
    }
}
```

需要并发启动调用时，使用 Task initializer 得到地址固定的 `Task<T>`。Task 是 eager 的，创建后
立即开始执行；依次调用 `await()` 不会把启动过程串行化：

```c
async Int load_both() {
    Task<Int> left = Task { load_left() };
    Task<Int> right = Task { load_right() };
    left.await() + right.await()
}
```

`Task { ... }` 继承 current Domain；`Task(domain: global_domain) { ... }` 显式指定
execution domain。Task closure 和 sync block 使用最后一个表达式作为结果，不支持显式
`return`；`return` 只用于普通或 async 函数体。
直接 `Task<T>` 是 `!Movable` 原地值，可以直接作为 struct、tuple 或固定数组中的静态字段；包含它的
聚合值同样不可移动、按值传参、返回或捕获。`new Task { ... }` 创建可移动、非 Copyable 的
`Task<T>^` owner。owner 可以按值传参、返回、存入字段、容器和泛型实例；Task 的公开类型不包含 execution
domain。`task.await()` 消费一次 result，重复消费会被诊断。`task.cancel()` 只同步、幂等地发布请求，
不等待也不消费 result；`task.cancel_and_await()` 发布请求并等待退出。若 Task 正在等待显式 child
Task，取消会传播到 child，并在 child 进入终态后继续清理 parent；直接调用的 async child 会继承
同一取消上下文，在恢复边界先 unwind，再恢复 parent。CPU 密集型 async 代码可调用
`coroutine.check_cancelled()` 显式观察请求；`await()` 遇到已取消 child 时会取消当前 parent，并由
结构化 cleanup 取消其余 sibling。`cancel_and_await()` 只取消目标 Task，不取消 caller。

直接 Task 是结构化子任务。未消费的直接 Task 离开 scope、执行 `return`/`throw` 或随 parent 取消时，编译器
先向该退出路径上的全部活跃 Task 请求取消，再逐个等待其进入终态；所有 child 完成后才销毁其余
局部值并释放 parent frame。直接 `Task<T>` 的地址固定，不支持 `move()`、`forget()` 或重新赋值。
`Task<T>^` owner 析构不阻塞、也不隐式取消；owner 与 coroutine 通过固定的双方原子交接决定最后回收者。

Domain 是静态执行身份，Executor 是它在运行时采用的调度策略。
需要接入自有事件循环时，实现 `Executor`，再由 Domain 的 `make_executor` 创建它：

```jiang
struct InlineExecutor: Executor {
    () enqueue(Self& self, ExecutorJob job) {
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

每个命名 `const` Domain binding 都有独立身份，并且只懒创建一个 Executor 实例。
`.serial` 保证同一 Domain 的 Job 不重叠；`.concurrent` 允许并行执行。`ExecutorJob` 是一次性值，
`enqueue` 接收后必须最终调用一次 `run()`，不能复制、保存借用或静默丢弃。

跨线程共享简单标量状态时使用 `Atomic<T>`。默认的 `get()`、`set()`、`get_and_set()` 和
`compare_exchange()` 使用 sequential order；需要更弱顺序时使用对应的 `*_with_order` 方法：

```jiang
Atomic<Int> state = Atomic<Int>(0);
state.set_with_order(1, .release);
Int observed = state.get_with_order(.acquire);
Int previous = state.get_and_set_with_order(2, .acquire_release);
Bool changed = state.compare_exchange_with_order(
    2, 3, MemoryOrder.acquire_release, MemoryOrder.acquire
);
```

`MemoryOrder` 统一提供 `relaxed`、`acquire`、`release`、`acquire_release` 和 `sequential`。load 只接受
`relaxed`、`acquire`、`sequential`；store 只接受 `relaxed`、`release`、`sequential`；exchange 接受
全部顺序；compare-exchange 的 failure order 只接受 `relaxed`、`acquire`、`sequential`，且不能强于
success order。Atomic 是显式的内部可变性入口，调用写操作不要求外部 binding 带 `!`。当前 `T`
仅支持后端保证 lock-free 的整数、Bool 和裸指针标量。

需要保护共享状态时使用 `Mutex<T>`。Mutex 将锁与值绑定，`with_lock<R>()` 只在同步 callback
执行期间提供值的唯一可变引用：

```jiang
struct State: Movable {
    Int count;
}

Mutex<State> state = Mutex<State>(State(count: 0));
Int count = state.with_lock { value =>
    value.count = value.count + 1;
    value.count
};
```

callback 是普通同步 `Fn`，不能在持锁期间 `await`；它的返回值也不能携带从 `value` 派生的引用。
`Mutex<T>` 是 `!Movable`，正常 `return`、`throw` 和 cleanup 都会通过内部 RAII lock 自动解锁。
当前 Mutex 不提供 poison 状态；不可恢复的进程终止没有可供后续持锁者观察的恢复阶段。

需要 detached 执行时，直接把 Task initializer 作为语句启动，不形成 Task handle：

```c
Task(domain: global_domain) {
    refresh_cache()
};
```

在 async 函数中只需要结构化切换 execution domain 时，继续使用 `sync [Domain]`，不必创建临时
Task：

```c
async Response load() {
    sync [global_domain] {
        request.send()
    }
}
```

这里 `sync` 挂起当前 coroutine，在 `global_domain` 执行 block，完成后回到进入前的 Domain；它不会
阻塞 worker thread。普通同步函数中的最外层 `sync [Domain]` 仍会阻塞调用线程等待结果。

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
- 分支根 pattern 支持 variant / optional / tuple / literal
- binding/wildcard 只作为子 pattern 使用，不能单独作为分支根
- Tuple payload 在 variant/optional pattern 中展开一层，例如 `.pair(left, right)`；
  嵌套 Tuple 继续保留括号，例如 `.nested((left, right), tail)`
- 当前不支持对 `T@E` 结果直接使用 `switch` 表达式

#### 异常

Jiang 的异常不是 runtime exception，也不做栈展开。它只是返回值编码，并且只写作 `T@E`：

```c
Int@Err parse(UInt8[]& text)
  
()@Err flush()
  
RawFn<Bool@Err, Int, Int> compare
```

其中：

- `T` 是成功值类型
- `E` 是错误值类型
- `T@E` 只允许作为函数返回类型，或出现在 callable 的返回位
- 底层布局复用通用 result/union 模型，不单独引入 runtime exception 机制
- `T@E` 必须连续书写，`T @E` 和 `T@ E` 都不合法

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

- 只允许出现在返回 errorable type 的函数里
- `throw` 的值必须与当前函数的错误类型 `E` 一致
- `throw` 只是语句，不是表达式

在 `T@E` 函数里调用另一个 `U@E` 函数时，调用表达式直接表现为 `U`；失败时错误自动传播：

```c
Int@Err ok() {
    return 1;
}

Int@Err outer(Bool fail) {
    return parse(fail) + ok() + 40;
}
```

这里：

- `parse(fail)` 和 `ok()` 的成功值可以直接参与表达式
- 失败时自动从 `outer` 返回同一个错误
- 只支持**相同错误类型 `E`** 的隐式传播
- 在非 errorable 函数里，不能把 errorable 调用结果当普通值直接使用

异常的使用方式是：

- 在 `T@E` 函数里依靠普通调用做同 `E` 的隐式传播
- 用 `try expr catch { ... }` 或 `try expr catch error { ... }` 处理单个失败结果

异常结果不通过 `switch` 匹配。

单个可错表达式可以用 `try catch` 处理。先看不需要错误值的形式：

```jiang
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
    Int value = try parse(true) catch {
        0
    };
    return value;
}
```

需要检查错误值时，直接在 `catch` 后声明绑定。绑定类型可以推导，也可以显式写出：

```jiang
Int main() {
    Int a = try parse(true) catch error {
        if (error == Err.bad) {
            42
        } else {
            0
        }
    };
    Int b = try parse(true) catch Err error {
        if (error == Err.bad) {
            42;
        } else {
            0;
        };
    };
    return a + b;
}
```

`catch` 规则：

- 只支持前置 `try catch` 表达式形式：`try expr catch binding? { ... }`
- `try` 只包住 `catch` 前面的单个表达式
- `expr` 必须是 `T@E`
- 不需要错误值时直接写 `catch { ... }`
- `catch error { ... }` 从错误类型 `E` 推导绑定类型；也可写 `catch E error { ... }`
- 为兼容需要分组的写法，绑定可写成 `catch (error) { ... }` 或 `catch (E error) { ... }`
- `catch` 后必须是 block，block 尾表达式是失败分支的结果
- 成功结果类型为 `T`，失败分支的结果类型必须能与 `T` 统一
- `catch` 不做 runtime unwind，仍然只是结果值分支

不支持：

- 后缀 `expr catch`
- 多条 `catch`
- `catch (...) => ...` 旧写法
- 不带 block 的失败分支
- `finally`
- 不同错误类型自动组合

#### Defer

`defer` 会在当前块退出时按 LIFO 顺序执行。

```c
defer {
    unsafe {
        handle$.dealloc();
    }
}

defer {
    log("closing");
    unsafe {
        handle$.dealloc();
    }
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
Int i! = 0;
while (i < 10) {
    print("i = %d", i);
    i += 1;
}
```

##### For 循环

Jiang 语言支持 `for-in` 语法，用于遍历区间、数组或任何可迭代对象。
裸 `T*` / `T*!` raw pointer 不作为 iterable；需要遍历指针区间时，使用 range 产生 index，
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
Point point1 = Point(x: 0, y: 0)
// 与以下两种方式等价
_ point1 = Point(x: 0, y: 0)
Point point1 = Point(x: 0, y: 0)

Point move_point(Point point, Offset offset) {
  // 返回一个新的point
  return Point(x: point.x + offset.x, y: point.y + offset.y)
  // 与以下方式等价
  // return Point(x: point.x + offset.x, y: point.y + offset.y)
}
```

#### init函数

struct 可以自定义 `init` 函数。

`init` 具有以下语义：

- `init` 是结构体内的特殊构造器入口
- `init` 允许可见性修饰，例如 `public init(...)`
- `init` 必须显式声明 `self` 参数
- `init` 不声明返回类型，语义等价于 `()`
- `init` 只允许 `return;` / `return ();`
- `Point(...)` / `new Point(...)` 是结构体构造语法
- 如果类型定义了一个或多个 `init`，那么 `Point(...)` 会在这些 `init` 中按参数个数和参数类型做重载决议
- `init` 支持普通位置参数、命名参数和默认参数，规则与普通函数一致
- 如果类型没有定义 `init`，默认构造器使用 `Point(field: value)`
- 只要类型定义了 `init`，`Point(...)` 就只参与显式 `init` 的重载决议
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

  public init(self, Int x, Int y) {
    self.x = x;
    self.y = y;
  }

  public init(self, Int value) {
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
- `deinit` 必须显式声明 `self` 参数
- `deinit` 不声明返回类型，语义等价于 `()`
- `deinit` 只允许 `return;` / `return ();`
- `deinit` 不允许 `public` 等可见性修饰
- `deinit` 由该 nominal 的 drop 触发，不作为普通方法暴露
- `ptr$.dealloc()` 是低层释放操作，不作为普通析构入口使用
- 定义了 `deinit` 的 nominal type 不能实现 `Copyable`
- 如果 struct 有自定义 `deinit`，先执行自定义 `deinit`，再执行编译器生成的递归字段析构

```c
struct Buffer {
  UInt8* data;

  deinit() {
    unsafe {
      self.data$.dealloc();
    }
    return;
  }
}
```

#### 名义类型内部函数

除 `init` / `deinit` 外，名义类型当前都可以定义普通内部函数。
函数是否是实例函数由参数列表决定：

- `Ret foo(...)`：类型函数，只允许 `Type.foo(...)`
- `Ret foo(self, ...)`：实例函数，`self` 的类型为 `Self&`，支持 `value.foo(...)`
- `Ret foo(Self self, ...)`：move receiver 实例函数，调用会消耗 receiver

需要让方法消耗 receiver 时，使用 `Self self` 作为第一个参数：

```jiang
struct Box: Movable {
    Int value;

    Int consume(Self self) {
        self.value
    }
}

Int use() {
    Box box = Box(value: 1);
    box.consume()
}
```

调用 `box.consume()` 后，`box` 已经被 move，后续不能再使用。旧 receiver attribute
当前不再作为合法实例方法语法使用。

当前适用范围：

- `struct`：支持 `init`、`deinit`、类型函数、实例函数
- `union`：支持类型函数、实例函数
- `enum`：支持类型函数、实例函数

`init` / `deinit` 仍然是 `struct` 的特殊生命周期入口。
`union` / `enum` 不承诺自定义生命周期入口。
union variant 和普通类型函数/实例函数共用 `Type.member` 访问面，不能同名。

```c
struct User {
  Int id;

  init(self, Int id) {
    self.id = id;
    return;
  }

  Int zero() {
    return 0;
  }

  Int value(self) {
    return self.id;
  }
}

Int a = User.zero();
User user = User(id: 42);
Int b = user.value();
```

```c
enum Mode {
  read,
  write,

  Int answer() {
    return 42;
  }

  Int value(self) {
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

  Int answer() {
    return 42;
  }

  Int value(self) {
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
Point p1 = Point(x: 0, y: 0)
// p1赋值给p2是值拷贝
Point p2 = p1

// p3为 owning pointer，此时为引用类型
Point^ p3 = new Point(x: 100, y: 200)

// 由于Jiang语言的 owning pointer 默认自动解引用，此时的p3被当成值
print("p3.x = %d, p3.y = %d", p3.x, p3.y) // 输出：p3.x = 100, p3.y = 200
```

#### 结构体的可变属性

```c
struct User {
  // id为不可变属性
  Int id;
  // age为可变属性
  Int age!;
  // nick_name为可空的可变属性
  UInt8[]& nick_name!;
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
enum [UInt16] HttpStatus {
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
enum [UInt8] Kind {
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

union [Kind] ExplicitResult {
  Int a;
  Int b;
}

union ImplicitResult {
  Int a;
  Int b;
}
```

规则：

- `union [TagEnum]` 的 variant 名必须能对应到 `TagEnum` 的成员。
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
- `RawFn<...>`
- `T^`
- `T&`
- `T*` / `T*!`
- `T*`
- optional
- 其他 `union` / `enum`

`union` 也支持泛型：

```c
union Outcome<T, E> {
  T value;
  E error;
}
```



### 泛型（Generic）

Jiang 语言通常以 `<T>` 形式声明泛型参数。

`@where(...)` 是一种编译期约束注解，用于约束其后一个泛型声明中的类型参数。  
当前 `@where(...)` 支持以下约束项：

- `Name: const Type`
- `Name: Trait`
- `Name: !Trait`
- `Name: Trait<Assoc = Type>`
- `Name: TraitA & TraitB & TraitC`
- `Name == Type`
- `Name != Type`
- `Name.[Trait].Assoc == Type`

`Name: const Type` 是 const generic constraint 的 canonical 形式，并把对应泛型参数绑定到
value namespace。例如 `@where(N: const Int) struct Fixed<T, N>`。泛型列表中的
`N: const Int` 是等价简写，编译器会 lower 到同一条 Semantic Model predicate。

在泛型声明上，`@where(...)` 中引用的名字必须出现在后续声明的 `<...>` 泛型参数列表中。  
在 trait 内部，`@where(...)` 也可以引用当前 trait 可见的关联类型名。
关联类型绑定优先写在 trait bound 内部，例如 `@where(T: Sequence<Element = Int>)`；需要单独写 projection equality 时，使用显式 trait 投影，例如 `@where(T.[Sequence].Element == Int)`。

同一个声明前的 attribute 按源码顺序应用，并且都作用在当前声明自己的 namespace 上。
当前声明的泛型参数会先进入这个 namespace；后面的 attribute 可以引用前面 attribute
引入的名字，前面的 attribute 不能引用后面的名字。attribute 引入的名字只在当前声明的
签名、约束、成员和函数体中可见，不泄漏到外层模块。

`@alias(Name = Type)` 可以定义声明局部类型别名。一个 `@alias(...)` 可以包含多个逗号分隔的
绑定；这些绑定等价于按顺序拆成多个 `@alias`，因此后面的绑定可以引用前面引入的名字：

```c
@alias(Items = T[]&, Cmp = RawFn<Bool, Items, Items>)
@where(T: Hashable)
Cmp compare<T>(Items left, Items right)
```
`Name == Type` / `Name != Type` 也可用于类型形状匹配，pattern 中的 `_` 表示单个 type argument
wildcard，例如 `@where(T == _^)`、`@where(T != _?)`。后缀语法在这里直接按 canonical type 匹配。
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
@region(a)
struct Foo<T> {
  @life(a)
  T value;

  T bar(self) {
    return self.value * 2;
  }
}

// 显式给出实例类型，并使用 expected type 构造简写
Foo<Int> x = .(value: 123);
// 此时 T 明确为 Float
Foo<Float> y = Foo<Float>(value: 3.14);
// 也可以写成
_ z = Foo<Float>(value: 3.14);
```

泛型参数的顶层 `!` 能力约束当前只有显式 `Mutable` 模式：

- `@where(T: Mutable)` 表示该泛型参数必须是 `T&!` 或 `T*!` 这类带顶层可写能力的类型

```c
@where(T: Mutable)
@region(a)
struct MutableBox<T> {
  @life(a)
  T value;
}

MutableBox<Int*!> a = MutableBox<Int*!>(value: null);
```

其中：

- `Mutable` 是语言内建约束名
- 它由编译器识别，不是普通用户可实现的 trait
- 但表面语法仍然通过 `@where(T: ...)` 使用

当前规则只看**顶层** `!`：

- `Int`、`Int&` 和 `Int*` 不满足 `Mutable`
- `Int&!` 和 `Int*!` 满足 `Mutable`

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
  Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
  () hash<H: Hasher>(self, H&! hasher);
}
```

trait 也可以继承一个或多个父 trait：

```c
trait HashEq: Hashable {
  Bool equal(Self& lhs, Self& rhs);
}
```

当多个 trait 含有同名 requirement 时：

```c
trait AddInt {
  Int apply(self, Int delta);
}

trait FlagValue {
  Int apply(self, Bool flag);
}

struct Counter: AddInt, FlagValue {
  Int base;

  Int apply(self, Int delta) {
    return self.base + delta;
  }

  Int apply(self, Bool flag) {
    if (flag) {
      return self.base + 10;
    }
    return self.base;
  }
}

Counter counter = Counter(base: 30);
Int a = counter.apply(2);
Int b = counter.apply(true);
```

这里：

- 同名不同签名的方法按普通重载规则区分
- 同名同签名的多个 trait requirement 可以共用同一份实现

每个 trait 都有保留的 companion type 名：

- `Trait.Any`：dynamic trait view，保存 erased receiver 和 compiler-private vtable。
- `Trait.Receiver`：vtable slot 使用的 erased receiver。
- `Trait.VTable`：compiler-private 方法表类型，用户源码不能直接命名或传参。

对应的 intrinsic type operation 为：

```c
Value.Any& any = Value$.ref(box);
Int a = any.value();

Value.Any^ owned = Value$.new(Box(data: 1));
Value.Any& owned_ref = owned$.ref();
Int b = owned_ref.value();

Value.Receiver receiver = any.receiver;
RawFn<Int, Value.Receiver> value_fn = any.value;
Int c = value_fn(receiver);
```

当前实现支持 borrowed trait object `Trait$.ref(value)` 和 owning trait object `Trait$.new(value)`。
`Trait$.new(value)` 会为 receiver 创建 owning storage，并在 `Trait.Any^` drop 时通过
receiver type info 触发 receiver drop。move receiver trait object dispatch 暂不支持；如果 trait
中存在 move receiver requirement，构造 `Trait.Any` / `Trait.Receiver` 会编译失败。

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
  Int value(self);
}

trait HasDouble: HasValue {
  Int double_value(self);
}

struct Box: HasValue {
  Int inner;

  Int value(self) {
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
  Int value(self);
}

struct User {
  Int id;
}

extend User {
  Int id_value(self) {
    return self.id;
  }
}

extend User: HasValue {
  Int value(self) {
    return self.id;
  }
}
```

当前 `extend` 的限制：

- method 不进入模块顶层命名空间
- `self` 参数表示 receiver `Self&`；`Self self` 表示拥有值 receiver
- 字段声明用 `Type name!` 表达该字段 storage 可写；实际赋值还要求访问链上的外层 place
  可写，
  共享引用不能把字段升级成可写 place
- 默认 `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`；`Self self` 方法会消耗 `value`
- receiver 已经是 pointer 时，`ptr.method(args...)` 与 `value.method(args...)` 使用同一套 lookup
- `Type.method(receiver, args...)` 是显式方法调用形式
- generic receiver extension 必须显式声明模式参数，例如 `extend <T> Holder<T>`、
  `extend <T> T?` 和 `extend <T, E> T@E`。推荐在 `extend` 与参数列表之间保留空格；空格不是
  语法要求，并为未来的 `extend [options] <T>` 形式保留清晰结构。目标中的未声明名称按普通类型名解析，
  `extend Holder<T>` 不会隐式声明 `T`；`_` 只匹配一个 type argument 且不绑定名称。
- binder 与 owner generic parameter 相互独立，不要求数量相同。`extend <T> T[]^` 会从
  `Int[]^` 捕获 `T = Int`；`@where(S == T[]) extend <T, S> S^` 表达同一绑定链。
  无法从 target/equality pattern 推导的 binder 会在声明处报错。
- concrete specialized target 可以直接写，例如 `extend Holder<Int>`、`extend Int?`；需要同时保留
  pattern 参数并添加附加条件时，使用 `@where(T == Int) extend <T> Holder<T> { ... }`。
- receiver 形状约束写在 `@where` 中，例如 `@where(T == _^) extend <T> Holder<T>` 或
  `@where(T != _?) extend <T> Holder<T>`。
- `T^` / `T&` / `T*` / `T*!`、`T[]` / `T[:S]`、`T[N]` / `T[N:S]` 等语法糖作为类型
  receiver 或 where pattern 时，会按对应内部 canonical owner 查找和匹配 extension。
- `public extend` 可以跨模块传播；普通 `extend` 只在声明模块可见。`public import` 会继续 re-export
  imported module 的 public extensions。
- 用户模块可以扩展 builtin 或其他模块公开的类型，不使用全局 orphan 禁令；extension 只参与当前模块
  可见集合中的 lookup。相同调用同时匹配多个同 specificity extension 时报告 ambiguity，concrete pattern
  优先于 generic pattern。
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

编译器默认把可复用的接口和构建产物保存在当前目录的 `build/cache`。
可以为一次编译选择其他位置：

```bash
jiangc --artifact-cache-dir path/to/cache path/to/pkg -o pkg
```

debug 构建会按源码复用未变化的编译产物；release 构建仍对整个 package 做统一优化。
两种模式在输入和输出均未变化时都会直接复用上次成功结果。缓存缺失、损坏或不匹配只会
回退到正常构建，不改变程序语义。需要完全重新构建时，
可以显式清理所选择的缓存目录；下次编译会自动重建：

```bash
jiangc --artifact-cache-dir path/to/cache --clean-artifact-cache
```

不指定 `--artifact-cache-dir` 时清理默认的 `build/cache`。编译器会拒绝空路径、
文件系统根路径以及含 `.` 或 `..` 路径段的清理请求。排查构建复用情况时可加
`--artifact-stats`，编译器会在标准错误输出接口与对象的命中、缺失、失效、生成、复用、
最终链接和无修改快速命中数量。

当输入路径是目录时，编译器会读取该目录下固定文件名的 `package.ini`。当前识别：

- `[package].name`
- `[package].root`
- `[package].type`
- `[package].version`
- `[dependencies]`

这些 package 字段都可选：

- `name` 未写时，默认取当前目录名
- `root` 未写时，默认取 `<name>.jiang`
- `type` 未写时，默认是普通 package；`type = lang` 表示该 package 提供自定义语法 provider
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
version = 1.0.0
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

#### Lang Package / 自定义语法

`type = lang` package 可以提供 syntax-stage provider。使用方仍通过普通 `[dependencies]`
声明依赖：

```ini
[dependencies]
sql = ../sql-lang
```

源码中用 dependency alias 调用：

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

provider package manifest：

```ini
[package]
name = sql-lang
root = lang.jiang
type = lang
```

provider root 必须 public 导出 `Lang`，并实现 `std.jiang.syntax.Provider`。编译器在 host 上
把 lang package 编译成 dynamic library；lexer 调用 `scan` 决定 block 边界，parser 调用 `parse`
取得 `std.jiang.syntax.Tree`，再转换成普通 Jiang AST。DSL 返回的节点继续走普通 resolve、
type check、JIL 和 backend。

当前限制：

- 只支持 block invocation：`#alias { ... }`
- 不支持 `#alias(...)`
- 一个 lang package 只提供一个默认 provider
- provider 不能直接生成 Semantic Model/JIL/backend IR
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

传给 C 风格 API 的字符串使用 `UInt8*` 表示。字符串字面量在 `UInt8*` 上下文中会生成以 `\0` 结尾的只读全局数据，pointer 类型本身不记录 sentinel。

如果需要借用 NUL 结尾且保留 length 的只读字节序列，使用 `UInt8[:0]&`；如果需要传给 C ABI，使用 `UInt8*`。

```c
extern {
  public Int open(UInt8* path, Int options);
  public Int write(Int fd, UInt8[]& buf, Int count);
  public Int errno;
}

```

也支持单条声明：

```c
extern public Int puts(UInt8* text);
public extern Int errno;
```
