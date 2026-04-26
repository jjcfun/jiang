# Jiang语言指南

> Jiang语言的目标是成为编程语言的“银弹”。`All in one`是Jiang语言的核心思想。


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
    UInt8[] file_path;
    Int start_offset;
}

UInt8[] read_source(UInt8[] file_path) {
    return file_path;
}

import store = "token_store.jiang";
```

这样可以稳定区分类型和值：

- `TokenKind`、`SourceFile` 看起来就是类型
- `kw`、`string_lit` 看起来就是枚举值
- `read_source`、`start_offset` 看起来就是函数和字段

当前不建议把枚举成员写成 `SomeField` 这种 `PascalCase` 形式，因为它会和类型名混淆。

### 类型概要

关于Jiang语言的类型，遵循 **从左往右，从里到外** 的原则。比如`Int[2][3]`，表示一个数组，这个数组的元素有3个，每个元素都是`Int[2]`类型。从左往右看，`Int -> Int[2] -> Int[2][3]`，类型的范围是从里到外逐渐扩大的。简单来说：**越是右边，范围越大**

```c
// 嵌套的数组
Int[2][3] a = [[1, 2], [3, 4], [5, 6]]

// 最里层的数组元素为可空的Int类型
Int?[2][3] b = [[1, null], [3, 4], [5, 6]]

Int?[2]?[3] c = [[1, null], null, [5, 6]]

// 指针同理
Int[3]* b = new [1, 2, 3]

Int?[3]* c = new [1, null, 3]
```

### 基本类型

```c
Int a1 = -123;
UInt8 a2 = 23;
Char ch = "a";
UInt8[3] a3 = "abc";
Int16 a4 = -45;
UInt16 a5 = 512;
Float num3 = 12.3;
Double num4 = 132.54;
Float16 num5 = 1.5;
Float32 num6 = 2.5;
Float64 num7 = 3.5;

// 类型后紧跟'!'号，表示可变
Bool! foo = true;
foo = false;

// 类型推断
_ x = 100; // 推断为 Int

_ y = 3.14; // 推断为 Double

_ name = "Jiang"; // 推断为 UInt8[_]
```

其中：

- `Char` 表示单个 Unicode 标量，字面量语法与字符串一致，例如 `"a"`、`"中"`
- 字符串字面量按 UTF-8 字节序列处理，当前仍使用 `UInt8[_]` / `UInt8[]`
- `()` 表示 `Unit` 类型；它是一个零大小值，同时承担无返回值语义

当前数值自动提升仅覆盖 `Int / Float / Double`：

```c
Float f = 0.5$.as(Float);
Float a = 2 + f;   // Int + Float -> Float
Double b = 2 + 0.5; // Int + Double -> Double
```

`%` 仍然只允许整数参与；`Double -> Float`、`Float/Double -> Int` 仍需要显式 `as`。



### 可选类型 (Option)

```c
Int? a1 = 123;
// a2为Int?类型
_ a2 = a1;

Bar? b1 = {x: 1, y: 2};
// b2为Int?类型
_ b2 = b1?.x;

// 可选类型还支持链式调用
_ foo = x?.y?.z

// 判空解包
if a1 == Option.some(_ x) {
  // 这里x不为null，类型为Int
} else {
	// 这里x为null
}
```

#### Optional Coalesce

`??` 用于 optional 取值失败时提供默认值：

```c
Int value = maybe ?? 42;
Int other = maybe ?? fallback();
```

`??` 也支持在局部变量初始化位置提前退出：

```c
Int value = maybe ?? return;
Int value = maybe ?? return 42;
Int value = maybe ?? break;
Int value = maybe ?? continue;
```

其中：

- 左侧必须是 optional
- `return` / `break` / `continue` 只支持出现在局部变量初始化右侧
- `return expr` 会按当前函数返回类型检查

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
- `defer` 体内也不支持 `?? return`、`?? break`、`?? continue`



### 类型转换 (Type Casting)

Jiang 语言支持显式的类型转换，采用 `a$.as(Type)` 的语法。

这里的 `$` 符号表示**进入隐式操作层**。可以把它理解成：对一个值或一个类型，切换到它的“隐式层 / 元层”再进行操作。

- 对值使用：`a$.xxx()`
- 对类型使用：`Type$.xxx()`
- 对复合表达式使用：`(expr)$.xxx()`

后缀 `$` 绑定在它左侧的完整对象上。因此：

- `a$.b` 等价于 `(a$).b`
- `a.b$` 等价于 `(a.b)$`
- 如果要对整个 `(a + b)` 进入隐式操作层，必须写成 `(a + b)$.xxx()`

例如：

- `a$.as(Int)`：对值 `a` 做类型转换
- `a$.ref()`：从值 `a` 获取一个临时引用
- `a$.ptr()`：从值 `a` 获取一个裸指针
- `a$.addr()`：获取值 `a` 的地址值
- `a$.free()`：对 `a` 做释放操作
- `Int$.size()`：获取类型 `Int` 的大小
- `Int$.align()`：获取类型 `Int` 的 ABI 对齐
- `Int$.max_align()`：获取当前内建分配器保证支持的最大 Jiang 类型对齐
- `Int$.alloc()`：分配一个未初始化的 `Int*`
- `Int$.alloc_array(10)`：分配一个长度为 `10` 的 `Int[*]`

在当前设计中，许多原本会被写成内建函数的操作，都会逐步迁移到隐式操作层。例如，类型大小不再写作 `size_of(T)`，而统一写作 `T$.size()`。

当前 stage0 里，与隐式操作层和运行时相关的边界可以简单理解为：

- `std/prelude.jiang` 提供默认预导入的 trait 和基础类型声明，例如 `Range`
- 一部分 primitive 能力由编译器 builtin 直接支持，例如：
  - `Char.equal(...)`
  - `Char.hash()`
  - `assert(...)`
  - `print(...)`
  - `panic()`
  - `T$.alloc()`
  - `T$.alloc_array(...)`
  - `T$.align()`
  - `T$.max_align()`
- 最小运行时仍依赖宿主 C 运行时，当前主要使用：
  - `malloc`
  - `free`
  - `printf`
  - `abort`

`as` 是一个特殊的隐式层方法，它接收一个类型表达式作为参数。

```c
Float f = 10.5;

// 将 Float 转换为 Int
Int i = f$.as(Int);

print("i = %d", i); // 输出：i = 10

// 将 Int 转换为 UInt8
Int val = 255;
UInt8 small_val = val$.as(UInt8);

// 注意：某些危险的转换（如指针强转）可能需要包裹在 sudo 块中
Int addr = 0x12345678;
sudo {
    Int* ptr = addr$.as(Int*);
}
```

### 数组（Array）

数组的长度是类型的一部分，必须在编译期就确定，所以数组类型不支运行时改变长度，这与C、Rust、Zig类似。

#### 不可变数组

```c
// 定义不可变数组
Int[3] a = [1, 2, 3] // a: [1, 2, 3]


// 初始化数组时，如果元素个数与数组长度不想等，将会报编译错误
Int[5] a = [1, 2, 3]

// 在堆上创建数组，并返回数组指针
Int[3]* x = new [1, 2, 3]

// 通过length属性，可以获取到数组的长度（注意，Jiang语言中，指针是自动解引用的）
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
Int[_]*! e = new [1, 2, 3]
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

### 指针（Pointer）

现阶段，Jiang 语言先不引入所有权和借用系统，也不区分多种 allocator。运行时只有默认堆分配器。
在此基础上，指针语义先约定为：

- `T*`：自动解引用的单对象指针，适合 `new T` 这类场景
- `T[*]`：可按下标访问和按元素偏移的 many-pointer
- `T&`：临时指针，通常用于引用已有值，不承担释放职责

后续如果语言正式引入所有权、借用或多 allocator，这里的规则再进一步细化。

#### 指针类型

指针类型也遵循 **从左往右，从里到外** 的原则

```c
// 在栈中开辟内存空间
Int a = 123;

// new关键字可以在堆中开辟内存空间，并返回一个指针
Int* b = new Int(123);

// 在堆中创建数组，并返回一个数组指针
Int[3]* c = new [1, 2, 3];

// 临时引用
Int& d = a$.ref();

```

指针类型的 `*` 与 `&` 都紧跟在元素类型后面，不能存在空格

```c
// Bad
Int * a;

// Bad
Int *a;

// Good
Int* a;

// Bad
Int & b;

// Good
Int& b;
```

#### 自动解引用

使用指针时，除非明确使用`$`操作符表示指针本身，否则将自动解引用，使用指针与使用其元素无异。
特别说明一下，Jiang语言并不追求一切皆显式，并提供了一种类似代理（Proxy）的特性，可以将一些实现细节放在代理内部，并对使用者透明。就比如指针类型，就是一种拥有代理特性的类型。使用指针时，与直接使用指针的元素无异。此时，指针这个概念对使用者来说是透明的。而 `$` 操作符则是进入隐式操作层，通过类似 `ptr$.free()` 的语法，就可以调用指针本身的一些方法。
所以，Jiang语言的理念就是：对于用户，不需要关心冰箱的制冷原理；对于维修者，又提供了螺丝刀，允许拆开冰箱看看内部结构

```c
Int a = 100;

Int* b = new Int(200);

// 指针默认自动解引用，b直接表示了其元素的值
Int c = a + b;

print("c = %d", c); // 输出： c = 300

// '$'符号用于进入b的隐式操作层，此时可以调用指针本身的一些方法
b$.free();
```

这里没有单独的显式解引用语法，`*ptr` 这种写法不成立。`T*` 表示自动解引用的单对象指针：要使用指针元素，直接写 `ptr` 即可；要操作指针本身，则使用隐式层语法，例如 `ptr$.free()`。如果需要按下标访问，则使用 `T[*]` many-pointer。

```c
Int! value = 41;
Int!* p = value$.ptr();

// 读取指针元素
Int x = p;

// 修改指针元素
p = p + 1;

Int[1] items = [41];
Int[*] raw = items[0]$.as(Int[*]);

// many-pointer 通过下标访问
Int y = raw[0];
```

`T[*]` many-pointer 支持下标读写，但当前不提供 `offset()` 这类额外指针算术语法。

除数组、slice、many-pointer 外，显式实现 `SubscriptGet` trait 的用户类型也支持 `value[index]` 语法；如果该类型还显式实现 `SubscriptSet`，则支持 `value[index] = new_value`。

当前版本里，Jiang 只约定 `*` 指针可通过 `ptr$.free()` 主动释放默认堆分配器上的对象；`&` 指针只是临时指针，不参与释放。

```c
// 定义一个指针a，指向堆内存
Int* a = new Int(100);

// 可以主动释放指针的内存空间
a$.free();
```

### 切片（Slice）

slice 是一个带有 `length` 属性的胖指针。它与数组的区别在于：数组类型的长度是在编译器确定的，而 slice 的长度在运行时确定。
现阶段可以把 `T[]` 看作一个轻量的 `{ ptr, len }` 视图值；它本身不表达所有权语义。

```c
// x为一个数组
Int[_] x = [1, 2, 3];

// 数组自动转换为切片
Int[] y = x[..];

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
(_ x, _! y) = foo(10, 200);
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
UInt8[] str2 = "hello";
```

### Range

Jiang 支持用 `start .. end` 语法直接创建 `Range` 值：

```c
Range r = 0..10;
```

它等价于：

```c
Range r = Range(start: 0, end: 10);
```

其中 `end` 为开区间端点。

`Range` 来自隐式预导入的 `std/prelude.jiang`，当前定义为：

```c
struct Range {
  Int start;
  Int end;
}
```

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

Jiang 目前只支持普通位置参数：

```c
Int add(Int base, Int extra) {
    return base + extra;
}
```

规则如下：

- 参数按定义顺序匹配
- 当前不支持标签参数
- 当前不支持默认参数

#### 函数调用

```c
Int[_] list = [5, 3, 4, 1, 2];

sort(list, { $0 < $1 });
sort(list, { (a, b) -> a < b });
sort(list, { (_ a, _ b) -> a < b });
```

#### 函数签名

```c
// 排序
Int[] sort(Int[] list, Fn<Bool, Int, Int> compare)

// 支持泛型的排序，其中 T 需要实现 Numbric
@where(T: Numbric)
T[] sort<T>(T[] list, Fn<Bool, T, T> compare)

// 支持泛型的排序，会抛出异常，其中 E 可以为任意类型
@where(T: Numbric)
T[]@E sort<T, E>(T[] list, Fn<Bool@E, T, T> compare)
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

当前不支持：

- 通过实例值获取绑定方法函数值（例如 `value.method`）
- `init` 转函数指针
- 闭包

#### 异步函数（Async）

```c
@where(T: Numbric, E2: CompareError)
async T[]@E1 sort<T, E1, E2>(T[] list,Fn<async Fn<async Bool@E2, T, T>@E1, T[]> compare)

@where(T: Numbric, E2: CompareError)
@alias(Cmp = Fn<async Bool@E2, T, T>)
async T[]@E1 sort<T, E1, E2>(T[] list, Fn<async Cmp@E1, T[]> compare)
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

`if` 也可以作为表达式使用：

```c
Int x = if (flag) { 1 } else { 2 };
Int y = if (flag) 1 else 2;
Int z = if (flag) {
    Int base = 40;
    base + 2
} else {
    0
};
```

当前规则：

- `else` 分支必填
- 两个分支的结果类型必须一致
- 分支可以写成单个裸表达式，或写成 `{ ... }`
- 裸表达式分支不需要 `;`
- `{ ... }` 内可以写多条语句，最后一个**不带 `;`** 的表达式作为分支结果
- 前置语句当前主要支持局部变量声明、赋值和普通表达式语句

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
        base + 1
    }
    else => 0;
};
```

当前规则：

- 分支使用 `=>`
- 分支右侧可以写成单个裸表达式，或 `{ ... }`
- 裸表达式分支必须以 `;` 结束
- `{ ... }` 分支后面不需要再写 `;`
- `{ ... }` 内可以写多条语句，最后一个**不带 `;`** 的表达式作为分支结果
- 所有分支结果类型必须一致
- `enum` / `union` / `optional` 仍然做穷尽性检查
- 当前不支持模式绑定形式的 `switch` 表达式
- 当前不支持对 `T@E` 结果直接使用 `switch` 表达式

#### 异常

Jiang 的异常不是 runtime exception，也不做栈展开。它只是返回值编码，语法写作 `T@E`：

```c
Int@Err parse(UInt8[] text)
  
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
- 用 `try expr catch fallback` 处理单个失败结果
- 用 `try expr catch (e) fallback` 或 `try expr catch (e) { expr }` 处理单个失败结果并读取错误值
- 在需要拦截错误时使用语句级 `try-catch`

异常结果不通过 `switch` 匹配。

单个可错表达式可以用前置 `try` 加 `catch` 处理：

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
    Int a = try parse(false) catch 0;
    Int b = try parse(true) catch (e) if (e == Err.bad) 42 else 0;
    Int! handled = 0;
    try parse(true) catch (e) {
        if (e == Err.bad) {
            handled = handled + 1;
        }
    };
    Int c = try parse(true) catch (e) {
        Int base = 0;
        if (e == Err.bad) base + 7 else base
    };
    return a + b + handled + c;
}
```

单条 `catch` 规则：

- `try expr catch fallback`
  - `expr` 必须是 `T@E`
  - 结果类型是 `T`
  - `fallback` 的类型必须能赋给 `T`
- `try expr catch (name) fallback`
  - `expr` 必须是 `T@E`
  - `name` 的类型自动推断为错误类型 `E`
  - 结果类型是 `T`
  - `fallback` 的类型必须能赋给 `T`
- `try expr catch (name) { expr }`
  - 只在表达式位置可用
  - `expr` 必须是 `T@E`
  - `name` 的类型自动推断为错误类型 `E`
  - 花括号内可以写多条语句，最后一个不带 `;` 的表达式作为结果
- `try expr catch (name) { ... };`
  - 这是语句级形式
  - 成功时继续执行，错误时进入块
- `catch` 不会做 runtime unwind，仍然只是结果值分支

示例：

```c
enum ErrA {
    a,
}

enum ErrB {
    b,
}

Int@ErrA fail_a() {
    throw ErrA.a;
}

Int@ErrB fail_b() {
    throw ErrB.b;
}

Int main() {
    Int! result = 0;
    try {
        fail_a();
        fail_b();
    } catch (ErrA e) {
        result = 1;
    } catch (ErrB e) {
        result = 2;
    }
    return result;
}
```

`try-catch` 规则：

- 只支持语句级：
  - `try { ... } catch (Type name) { ... }`
- `catch` 至少一个，可多个
- `catch` 类型必须显式写出，且必须带绑定名
- `try` 块中所有可能出现的错误类型都必须被 `catch` 完整覆盖，否则编译错误
- 匹配按**精确错误类型**进行
- 在 `try` 中，原本会隐式传播到当前函数外部的错误，优先跳转到匹配的 `catch`
- 语句级 `try` 不产值
- 表达式级 `try` 只支持：
  - `try` body 的最后一个不带 `;` 的表达式必须是可错表达式
  - `catch` 分支可以是单个表达式，或多语句 `{ ... }`
  - body 只能产生一种错误类型；`catch` 类型必须与它精确一致

不支持：

- `try expr`
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
- `defer` 体内也不支持 `?? return`、`?? break`、`?? continue`

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
Jiang 不支持隐式的索引迭代。如果需要索引，必须调用 `list.indexed()` 方法，该方法会返回一个包含 `(Int, Element)` 元组的序列。

```c
Int[_] list = [10, 20];

for (i, item) in list.indexed() {
    print("index: %d, value: %d", i, item);
}
```

**4. 解构规则 (Destructuring Rules)**
为了保持语法的一致性与严谨性，Jiang 规定：

- 如果 `in` 前面的模式（Pattern）包含超过 1 个元素，**必须** 使用括号 `()` 包裹。
- 单个元素的迭代可以不用括号。

```c
(Int, Int)[_] pairs = [(1, 2), (3, 4)];

for (a, b) in pairs {
    print("a=%d, b=%d", a, b);
}

for (i, (a, b)) in pairs.indexed() {
    print("index=%d, a=%d, b=%d", i, a, b);
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
- `Point(...)` / `Point.init(...)` / `new Point(...)` 是结构体构造语法
- 如果类型定义了一个或多个 `init`，那么 `Point(...)` / `Point.init(...)` 会在这些 `init` 中按参数个数和参数类型做重载决议
- `init` 只支持普通位置参数，不支持标签参数
- 如果类型没有定义 `init`，那么默认字段初始化使用 `Point { field: value }`
- 只要类型定义了 `init`，就不允许再用 `Point { ... }`
- `new` 只接受构造形式，不支持任意表达式
- 例如：
  - `new Int`
  - `new Int(123)`
  - `new Point(...)`
  - `new Point { ... }`
  - `new [1, 2, 3]`
- `new Point(...)` 会先按上面的规则构造出 `Point` 值，再把这个值放到堆上
- `struct` / `record` 字段声明支持同类型多名字写法，例如 `Int x, y, z;`

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
Point p1 = Point(1, 2);
Point p2 = Point(3);
Point* p3 = new Point(4, 5);
```

#### deinit函数

struct 还可以定义 `deinit` 函数。

`deinit` 具有以下语义：

- `deinit` 是结构体内唯一的特殊析构器入口
- `deinit` 隐式拥有 `self`
- `deinit` 不声明返回类型，语义等价于 `()`
- `deinit` 只允许 `return;` / `return ();`
- `deinit` 不允许 `public` / `static` 等可见性或静态修饰
- `deinit` 不作为普通方法暴露给外部调用
- 对结构体指针执行 `ptr$.free()` 时，如果该结构体定义了 `deinit`，则会先触发 `deinit`，再释放对象自身内存

```c
struct List {
  Int[]! data;

  deinit() {
    if self.data != null {
      self.data$.free();
      self.data = null;
    }
    return;
  }
}
```

#### 名义类型内部函数

除 `init` 外，名义类型当前都可以定义普通内部函数。第一版使用 `static` 区分类型函数与实例函数：

- `static Ret foo(...)`：类型函数，只允许 `Type.foo(...)`
- `Ret foo(...)`：实例函数，函数体内有隐式 `self`，只允许 `value.foo(...)`

当前适用范围：

- `struct`：支持 `init`、static 方法、实例方法
- `union`：支持 static 方法、实例方法
- `enum`：支持 static 方法、实例方法

`init` / `deinit` 仍然是 `struct` 独有的特殊生命周期入口，不能定义在 `union` / `enum` 中。

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

// p3为指针，此时为引用类型
Point* p3 = new Point { x: 100, y: 200 }

// 由于Jiang语言的指针自动解引用，此时的p3被当成值
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
  UInt8[]?! nick_name;
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

### record

`record` 是轻量数据类型，使用字段字面量初始化：

```c
record Point {
  Int x;
  Int y = 2;
}

Point p1 = Point { x: 40 };
Point p2 = { x: 1, y: 2 };
```

规则：

- `record` 支持 `Type { field: value }`
- 当 expected type 已知且为 `record` 时，允许直接写 `{ field: value }`
- `record` 字段支持默认值
- `record` 字段声明支持同类型多名字写法，例如 `Int x, y, z;`
- `struct` 只有在没有定义 `init` 时才支持 `Type { ... }`

局部变量声明也支持同类型多名字写法，但它只是语法糖，每个变量仍然必须显式初始化：

```c
Int a = 1, b = 2, c = 3;
```

### 枚举类型（Enum）

```c
// 定义枚举类型，枚举值默认从0开始，类型为Int
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
print("enum value: %d", PetKind.dog.value)

// 初始化
Priority priority = Priority.medium

// 通过类型推导，可以省略枚举名
HttpStatus status = .ok

switch (priority) {
	.low => print("priority value: %d", priority.value)
  .medium => print("priority value: %d", priority.value)
  .high => print("priority value: %d", priority.value)
}
```

### 联合类型（Union）

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
  .a(_ value) => print("value = %d", value);

  // 多个语句必须用 {}
  .b(_! value) => {
    value += 0.1;
    print("value = %f", value);
  }

  .c(_ v1, _ b2) => print("value = (%d, %d)", v1, v2);

  .d(_ v) => print("value = Foo {x: %d, y: %d}", v.x, v.y);

	else => break;
}

// 使用 if 判断
if (x == MyUnion.a(_ value)) {
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

同类型的多个 variant 也可以合并声明：

```c
union MyUnion {
  Int a, b, c;
  Float r;
}
```

`union` 的 payload 当前支持任意普通类型，包括：

- `struct`
- `record`
- tuple
- array
- slice
- `Fn<...>`
- `T&`
- `T*`
- `T[*]`
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
当前 `@where(...)` 支持两类约束项：

- `Name: Trait`
- `Name: TraitA & TraitB & TraitC`
- `Name = Type`

在泛型声明上，`@where(...)` 中引用的名字必须出现在后续声明的 `<...>` 泛型参数列表中。  
在 trait 内部，`@where(...)` 也可以引用当前 trait 可见的关联类型名。

例如：

```c
@where(T: Numbric)
T add<T>(T a, T b) {
  return a + b;
}
```

```c
/// 定义泛型函数，其中 T 可以为 Int、Float 等数值类型
@where(T: Numbric)
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
@where(T: Numbric)
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

泛型参数的顶层 `!` 可变性约束有三种模式：

- 默认不写约束时，泛型参数只接受**不带**顶层 `!` 的实参
- `@where(T: Mutable)` 表示该泛型参数**必须**带顶层 `!`
- `@where(T: MaybeMutable)` 表示该泛型参数可以带或不带顶层 `!`，并保留实例化后的实际可变性

```c
@where(T: Mutable)
struct MutableBox<T> {
  T value;
}

@where(T: MaybeMutable)
struct MaybeMutableBox<T> {
  T value;
}

MutableBox<Int!> a = MutableBox<Int!> { value: 1 };
MaybeMutableBox<Int> b = MaybeMutableBox<Int> { value: 2 };
MaybeMutableBox<Int!> c = MaybeMutableBox<Int!> { value: 3 };
```

其中：

- `Mutable` 和 `MaybeMutable` 是语言内建约束名
- 它们由编译器识别，不是普通用户可实现的 trait
- 但表面语法仍然通过 `@where(T: ...)` 使用

当前规则只看**顶层** `!`：

- `Int` 不满足 `Mutable`
- `Int!` 满足 `Mutable`
- `MaybeMutable` 同时接受 `Int` 与 `Int!`

### Trait

`trait` 用于定义一种**仅存在于编译期**的约束类型。  
它不作为运行时类型使用，也不能直接作为普通变量、字段、参数或返回值类型。

例如，`Numbric` 可以被定义为一个 trait，表示“所有数值类型”：

```c
trait Numbric;
```

`std/prelude.jiang` 中默认提供了一些常用 trait，例如：

- `Numbric`
- `FromStringLiteral`
- `Hashable`
- `Equatable`

其中 `Numbric`、`FromStringLiteral`、`Hashable` 和 `Equatable` 因为来自隐式预导入的 `std/prelude.jiang`，所以可以直接用于 `@where(...)`。

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
trait Hashable {
  Int hash();
}
```

trait 也可以继承一个或多个父 trait：

```c
trait HashEq: Hashable {
  Bool equal(Self other);
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

trait 还可以在 trait 体内部使用 `type` 声明关联类型：

```c
trait Iterator {
  type Item;
  Item next();
}
```

关联类型可以直接写 bound：

```c
trait HasItem {
  type Item: Hashable & Equatable;
  Item item();
}
```

子 trait 会自动继承父 trait 的关联类型，并可以继续对它加约束：

```c
trait Iterator {
  type Item;
  Item next();
}

@where(Item = UInt8)
trait ByteIterator: Iterator {
  Int next_int();
}
```

内建的 `SubscriptGet` / `SubscriptSet` trait 可用于让用户类型支持 `value[index]` 与可选的 `value[index] = new_value`：

```c
public trait SubscriptGet {
  type Index: Equatable;
  type Value;

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

- `Int`、`Float`、`Double` 等数值类型可以被视为满足 `Numbric`
- `Numbric` 本身不能直接写成 `Numbric x;`
- `Numbric` 主要用于 `@where(...)` 这类泛型约束位置
- trait 内部函数当前用于声明**约束签名**，不是运行时成员，也不是默认实现
- 用户自定义类型若要满足某个 trait，当前需要在类型定义处显式声明
- 仅仅“方法签名刚好匹配”并不会自动满足 trait
- 实现子 trait 的类型，会自动被视为也实现其父 trait
- trait 的关联类型使用 `type Name;` 声明
- 关联类型 bound 可写作 `type Item: Hashable;`
- 多个关联类型 bound 可写作 `type Item: Hashable & Equatable;`
- 子 trait 会继承父 trait 的关联类型，且当前不允许重新声明父 trait 的同名关联类型
- 子 trait 可以通过 `@where(Item: Hashable)` 或 `@where(Item = UInt8)` 继续约束继承来的关联类型
- `@where(...)` 中多个 trait 约束也支持 `&`，例如 `@where(T: Hashable & Equatable)`
- `FromStringLiteral` 是 builtin trait。显式声明该 trait，且类型提供 `init(UInt8[] bytes)` 后，可在有目标类型的上下文里直接写 `T x = "hello";`
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
  type Item;
  Item next();
}

struct Counter: Iterator {
  type Item = UInt8;

  UInt8 next() {
    return 42;
  }
}
```

如果一个实现块同时涉及多个带同名关联类型的 trait，可以用限定形式消歧：

```c
trait Left {
  type Item;
  Item left();
}

trait Right {
  type Item;
  Item right();
}

struct Pair: Left, Right {
  type Left.Item = UInt8;
  type Right.Item = UInt8;

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
  type Item;
  Item next();
}

struct Counter {}

extend Counter: Iterator {
  type Item = UInt8;

  UInt8 next() {
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

- 只支持扩展当前文件里**已经声明过**的本地 `struct` / `enum` / `union`
- `extend` 块里只允许普通方法
- 不支持 `init`
- 不支持 `deinit`
- `extend Type: Trait1, Trait2` 可以同时涉及多个带同名 requirement 的 trait

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
	return a > b ? a : b;
}

public Int min(Int a, Int b) {
	return a < b ? a : b;
}

// foo只能在文件内部使用
() foo() {
	Int a = 1
  Int b = 2
  Int c = 3

  // 输出：the maximum value of 1 and 2 is 2
  print("the maximum value of 1 and 2 is %d", max(a, b))

  // 输出：the minimum value of 2 and 3 is 2
  print("the minimum value of 2 and 3 is %d" min(b, c))
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
- 编译时会隐式预导入 `std/prelude.jiang`，该文件中的 `public` 定义无需显式 `import`
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

当输入路径是目录时，编译器会读取该目录下固定文件名的 `package.ini`。当前只识别 `[package]` 段里的：

- `name`
- `root`

这两个字段都可选：

- `name` 未写时，默认取当前目录名
- `root` 未写时，默认取 `<name>.jiang`
- 若显式写了，则覆盖默认值
- `name` 无论显式还是默认值，都必须满足 Jiang 标识符规则：`[A-Za-z_][A-Za-z0-9_]*`

例如：

```text
lexer/
  package.ini
  lexer.jiang
```

最小 `package.ini`：

```ini
[package]
version = 0.1.0
type = lib
```

此时默认：

- `name = lexer`
- `root = lexer.jiang`

也可以显式覆盖：

```ini
[package]
name = frontend
root = src/main.jiang
version = 0.1.0
type = lib
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
- 带引号的 `import "..."` 继续保留给文件路径导入
- 标准库 package `std` 由编译器内置提供，可直接写 `import std;`
- `std` 的 package 入口统一是 `std/std.jiang`

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

### FFI

传给 C 风格 API 的字符串指针当前使用 `UInt8*` 表示。字符串字面量在 `UInt8*` 上下文中会自动生成以 `\0` 结尾的只读全局数据。若需要 owning 包装类型，可通过 `import std;` 使用 `std.ffi.CString`。标准库 package 的统一入口是 `std/std.jiang`。

`ffi.CString` 显式声明了 `FromStringLiteral`，因此可以直接写：

```c
import std;

std.ffi.CString text = "hello";
puts(text.bytes[0]$.ptr());
```

```c
extern {
  public Int open(UInt8* path, Int options);
  public Int write(Int fd, UInt8[] buf, Int count);
  public Int errno;
}

```

也支持单条声明：

```c
extern public Int puts(UInt8* text);
public extern Int errno;
```
