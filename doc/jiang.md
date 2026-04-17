# Jiang语言指南

> Jiang语言的目标是成为编程语言的“银弹”。`All in one`是Jiang语言的核心思想。

# 语法

### 命名规范

当前仓库建议采用下面这套命名风格：

- 类型名使用 `PascalCase`
- 函数名使用 `snake_case`
- 变量名使用 `snake_case`
- 结构体字段名使用 `snake_case`
- 枚举成员使用 `snake_case`
- 模块别名优先使用 `PascalCase`

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

import Store = "token_store.jiang";
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
Int[2][3] a = {{1, 2}, {3, 4}, {5, 6}}

// 最里层的数组元素为可空的Int类型
Int?[2][3] b = {{1, null}, {3, 4}, {5, 6}}

Int?[2]?[3] c = {{1, null}, null, {5, 6}}

// 指针同理
Int[3]* b = new {1, 2, 3}

Int?[3]* c = new {1, null, 3}
```

### 基本类型

```c
Int a1 = -123;
UInt8 a2 = 23;
UInt8[3] a3 = "abc";
Float num3 = 12.3;
Double num4 = 132.54;

// 类型后紧跟'!'号，表示可变
Bool! foo = true;
foo = false;

// 类型推断
_ x = 100; // 推断为 Int

_ y = 3.14; // 推断为 Double

_ name = "Jiang"; // 推断为UInt8[5]
```



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



### 类型转换 (Type Casting)

Jiang 语言支持显式的类型转换，采用 `$a.cast(Type)` 的语法。其中 `$` 符号表示对对象本身进行“元操作”，`cast` 是一个特殊的元方法，它接收一个类型表达式作为参数。

```c
Float f = 10.5;

// 将 Float 转换为 Int
Int i = $f.cast(Int);

print("i = %d", i); // 输出：i = 10

// 将 Int 转换为 UInt8
Int val = 255;
UInt8 small_val = $val.cast(UInt8);

// 注意：某些危险的转换（如指针强转）可能需要包裹在 sudo 块中
Int addr = 0x12345678;
sudo {
    Int* ptr = $addr.cast(Int*);
}
```

### 数组（Array）

数组的长度是类型的一部分，必须在编译期就确定，所以数组类型不支运行时改变长度，这与C、Rust、Zig类似。

#### 不可变数组

```c
// 定义不可变数组
Int[3] a = Int[3] {1, 2, 3} // a: {1, 2, 3}


// 初始化数组时，如果元素个数与数组长度不想等，将会报编译错误
Int[5] a = {1, 2, 3}

// 在堆上创建数组，并返回数组指针
Int[3]* x = new {1, 2, 3}

// 通过length属性，可以获取到数组的长度（注意，Jiang语言中，指针是自动解引用的）
print("array %d", x.length)
```

#### 数组类型推断

```c
// 定义数组时，可以使用类型推断
// 以下3种定义等价
Int[_] a = Int[_] {1, 2, 3}
_ a = Int[_] {1, 2, 3}
Int[_] a = {1, 2, 3}
```

- 定义并初始化数组时，至少保证等号的一边拥有明确的数组类型申明，否则将编译失败

```c
// 这是错误示例，将不会通过编译
_ x = {1, 2, 3}

// 这是正确示例
Int[_] y = {1, 2, 3}

// 这也是正确示例
_ z = Int[_] {1, 2, 3}
```

#### 数组可变性

虽然数组的长度固定，但支持数组内的元素可变：

```c
// 数组内的元素可变
Int![_] b = {1, 2, 3} // b: {1, 2, 3}

b[1] = 4 // b: {1, 4, 3}

// 数组不可变，但是变量可以重新赋值
Int[_]! c = {1, 2, 3}
c = {4, 5, 6}

// 数组可变，变量也可以重新赋值
Int![_]! d = {1, 2, 3} // d: {1, 2, 3}
d[1] = 4 // d: {1, 4, 3}
d = {4, 5, 6} // d: {4, 5, 6}

// 数组在堆上创建
Int[_]*! e = new {1, 2, 3}
// 这里，变量赋值的时候会在堆上分配内存
e = new {4, 5, 6}
```

#### 多维数组

Jiang语言其实没有多维数组的概念，多维数组在这里只是嵌套的数组。以数组类型`Int[2][3]`为例。
从左往右看`Int[2][3]`类型，可以看作数组从里到外依次嵌套：最外层的数组有3个元素，每个元素都是Int[2]类型的数组。

```sc
Int[2][3] foo = {{1, 2}, {3, 4}, {5, 6}}

foo[0]  // {1, 2}

foo[0][1] // 2

```

### 指针（Pointer）

#### 指针类型

指针类型也遵循 **从左往右，从里到外** 的原则

```c
// 在栈中开辟内存空间
Int a = 123;

// new关键字可以在堆中开辟内存空间，并返回一个指针
Int* b = new 123;

// 在堆中创建数组，并返回一个数组指针
Int[3]* c = new {1, 2, 3};

```

指针类型的\*号与元素类型间不能存在空格

```c
// Bad
Int * a;

// Bad
Int *a;

// Good
Int* a;
```

#### 自动解引用

使用指针时，除非明确使用`$`操作符表示指针本身，否则将自动解引用，使用指针与使用其元素无异。
特别说明一下，Jiang语言并不追求一切皆显式，并提供了一种类似代理（Proxy）的特性，可以将一些实现细节放在代理内部，并对使用者透明。就比如指针类型，就是一种拥有代理特性的类型。使用指针时，与直接使用指针的元素无异。此时，指针这个概念对使用者来说是透明的。而`$`操作符则是关心这个代理本身，通过类似`$ptr.move() `的语法，就可以调用指针本身的一些方法。
所以，Jiang语言的理念就是：对于用户，不需要关心冰箱的制冷原理；对于维修者，又提供了螺丝刀，允许拆开冰箱看看内部结构

```c
Int a = 100;

Int* b = new 200;

// 指针默认自动解引用，b直接表示了其元素的值
Int c = a + b;

print("c = %d", c); // 输出： c = 300

// '$'符号用于取指针本身，此时可以调用指针本身的一些方法
// 此时b指针将所有权转移到d指针
Int* d = $b.move();
```

jiang语言的指针带有所有权，当指针赋值给其他变量的时候，所有权将发生转移

```c
// 定义一个指针a，指向堆内存，此时a拥有这块内存的有所有权
Int* a = new 100;

// 当a指针赋值给b时，意味这块内存空间的所有权从a转移到了b
Int* b = $a.move();

// 编译通过，此时 c = 300
Int c = b + 200;

// 继续使用a，将导致编译失败，此时a已经失去了所有权
Int d = a + 200; // 编译错误

// 在sudo模式下，可以主动释放指针的内存空间，sudo类似Rust语言的unsafe关键字
sudo $b.free();

// 等价于
sudo {
  $b.free();
}

// 此时继续使用b，将会报编译错误
print("b = %d", b); // 编译错误

```

### 切片（Slice）

slice是一个带有length属性的胖指针。它与数组的区别在于：数组类型的长度是在编译器确定的，而Slice的长度在运行时确定

```c
// x为一个数组
Int[_] x = {1, 2, 3};

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

#### 空元组

没有任何元素的元组被称之为空元组，用 `()` 表示。空元组不会占用内存空间

```c
() hello() {
	print("Hello World!");
  // return语句必须有返回值，即使返回值是个空元组
  return ();
}
```

### 字符串常量

```c
UInt8[_] str1 = "hello";
```

### 函数

#### 返回值

jiang语言的函数一定有返回值，即使是个空值。 空值用空元组 `()` 表示，在运行时不会占用内存空间

```c
() hello() {
  print("Hello World!");

  // 下方的return语句可以省略
  return ();
}
```

#### 函数签名

```c
// 排序
Int[] sort(Int[] list, Fn<Bool, Int, Int> compare)

// 支持范型的排序，其中T需要实现Numbric相关特性
@<T: Numbric>
T[] sort(T[] list, Fn<Bool, T, T> compare)

// 支持范型的排序，会抛出异常，其中E可以为任意类型
@<T: Numbric, E>
T[]@E sort(T[] list, Fn<Bool@E, T, T> compare)

@<T: Numbric, E1, E2: CompareError>
T[]@E1 sort(T[] list, Fn<Fn<Bool@E2, T, T>@E1, T[]> compare)

@<T: Numbric, E1, E2: CompareError>
async T[]@E1 sort(T[] list,Fn<async Fn<async Bool@E2, T, T>@E1, T[]> compare)

@<T: Numbric, E1, E2: CompareError>
@alias(Cmp = Fn<async Bool@E2, T, T>)
async T[]@E1 sort(T[] list, Fn<async Cmp@E1, T[]> compare)
```

#### 函数调用

```c
Int[_] list = {5, 3, 4, 1, 2};

sort(list, { $0 < $1 });

sort(list, { (a, b) -> a < b });

sort(list, { (_ a, _ b) -> a < b });
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
// 遍历 0 到 9
for i in 0..10 {
    print("%d", i);
}
```

**2. 集合遍历**
直接遍历容器中的元素。

```c
Int[_] list = {10, 20, 30};
for item in list {
    print("item: %d", item);
}
```

**3. 带索引的遍历 (Explicit Indexing)**
Jiang 不支持隐式的索引迭代。如果需要索引，必须调用 `list.indexed()` 方法，该方法会返回一个包含 `(Int, Element)` 元组的序列。

```c
Int[_] list = {10, 20};

// 通过显式解构获取索引和元素
for (i, item) in list.indexed() {
    print("index: %d, value: %d", i, item);
}
```

**4. 解构规则 (Destructuring Rules)**
为了保持语法的一致性与严谨性，Jiang 规定：

- 如果 `in` 前面的模式（Pattern）包含超过 1 个元素，**必须** 使用括号 `()` 包裹。
- 单个元素的迭代可以不用括号。

```c
// 遍历元组列表 (Int, Int)[_]
(Int, Int)[_] pairs = {(1, 2), (3, 4)};

// 正确：明确的元组解构
for (a, b) in pairs {
    print("a=%d, b=%d", a, b);
}

// 错误：解构超过 1 个元素必须加括号，以下代码编译失败
for a, b in pairs { ... }

// 正确：带索引且解构元组元素
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
Point point1 = { x: 0, Y: 0 }

Point point move_point(Point point, Offset offset) {
  // 返回一个新的point
  return { x: point.x + offset.x, y: point.y + offset.y }
  // 与以下方式等价
  // return Point { x: point.x + offset.x, y: point.y + offset.y }
}
```

#### init函数

struct除了默认的构造语法，还可以自定义 `init` 函数。

`init` 具有以下语义：

- `init` 是结构体内唯一的特殊构造器入口
- `init` 隐式拥有 `self`
- `init` 不声明返回类型，语义等价于 `()`
- `init` 只允许 `return;` / `return ();`
- `Point(...)` 是 `Point.init(...)` 的语法糖
- `new Point(...)` 会先分配内存，再调用 `Point.init(...)`

默认构造字面量与 `init` 并存，`Point { ... }` / `new Point { ... }` 仍然可用

```c
struct Point {
  Int x;
  Int y;

  init(Int x, Int y) {
    self.x = x;
    self.y = y;
    return;
  }
}
```

```c
Point p1 = Point.init(1, 2);
Point p2 = Point(1, 2);      // 等价于 Point.init(1, 2)
Point* p3 = new Point(1, 2); // 默认走 new + malloc + init
Point p4 = Point { x: 1, y: 2 };
```

#### 结构体内部函数

除 `init` 外，struct 还可以定义普通内部函数。第一版使用 `static` 区分类型函数与实例函数：

- `static Ret foo(...)`：类型函数，只允许 `Type.foo(...)`
- `Ret foo(...)`：实例函数，函数体内有隐式 `self`，只允许 `value.foo(...)`

`init` 仍然是唯一特殊构造器入口，不能写成 `static init`。

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
User user = User(42);
Int b = user.value();
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

// 这里'$'操作符作用是阻止解引用，此时$p指代指针本身
// 赋值完后，p3的所有权转移到了p4上，继续使用p3将导致编译错误
Point* p4 = $p3.move()
print("p3.x = %d, p3.y = %d", p3.x, p3.y) // 这里将编译失败，因为p3已经失去所有权
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
User user1 = {
  id: 123,
  age: 18
}
// 与以下定义等价：
// User user1 = User {
//   id: 123,
//   age: 18,
//	 nick_name: null
// }

print("user age = %d", user1.age); // 输出：user age = 18

user1.age += 1;
print("user age = %d", user1.age); // 输出：user age = 19

user1.id = 200; // 编译错误，不可变属性无法修改

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
	.low: print("priority value: %d", priority.value)
  .medium: print("priority value: %d", priority.value)
  .high: print("priority value: %d", priority.value)
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

union(Kind) MyUnion {
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
  .a(_ value): print("value = %d", value);

  // 多个语句必须用 {}
  .b(_! value): {
    value += 0.1;
    print("value = %f", value);
  }

  .c(_ v1, _ b2): print("value = (%d, %d)", v1, v2);

  .d(_ v): print("value = Foo {x: %d, y: %d}", v.x, v.y);

	else: break;
}

// 使用 if 判断
if (x == MyUnion.a(_ value)) {
  print("value = %d", value)
}
```

### 范型（Generic）

jiang语言通常以`@<T>`形式定义范型

```c
/// 定义范型函数，其中T可以为Int、Float等数值类型
@<T: Numbric>
T add(T a, T b) {
  return a + b;
}

// 此时T为Int类型
add(100, 200)
// 此时T为Double类型
add(3.14, 9.8)
// 强制指定T为Float类型
add<Float>(3.14, 9.8)

/// 定义范型结构体
@<T: Numbric>
struct Foo {
  T value;

  T bar() {
		retrun self.value * 2;
  }
}

// 此时T为Int类型
Foo x = {value: 123}
// 此时T为Float类型
Foo<Float> y = {value: 3.14}
// 也可以写成
_ y = Foo<Float> {value: 3.14}
```

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

// 3.导入模块，并使用Math作为模块名
public import Math = "utils/math.jiang";
Math.max(100, 200);

// 4.导入模块后，可以通过alias为模块中的公开符号创建本地别名
import Math = "utils/math.jiang";
alias maximum = Math.max;
maximum(100, 200);

// 5.public alias会在当前模块中重新导出该符号
import Math = "utils/math.jiang";
public alias max = Math.max;
public alias min = Math.min;

```

其中：

- `import "utils/math.jiang";` 会导入整个模块，并默认使用文件名 `math` 作为模块名
- `public import "utils/math.jiang";` 会在导入模块的同时，将模块名 `math` 对外导出
- `public import Math = "utils/math.jiang";` 会导入模块并使用 `Math` 作为公开模块名
- `alias maximum = Math.max;` 会为符号创建一个当前模块内可见的别名
- `public alias max = Math.max;` 会为符号创建一个公开别名，使其他模块可以通过当前模块访问该符号

`alias` 是纯符号别名，而不是新的变量绑定。它用于给已经存在的符号路径起一个新的名字。

```c
import Math = "utils/math.jiang";

alias maximum = Math.max;
public alias minimum = Math.min;
```

上面的 `maximum` 和 `minimum` 都直接指向原始符号，不会创建新的函数、副本或存储空间。

当前建议 `alias` 的右侧只能是可命名的符号路径，例如：

```c
alias Foo = A.B;
alias max = Math.max;
public alias read = IO.read;
```

而不应该是任意表达式：

```c
// bad
alias x = a + b;
```

使用 `public alias` 重新导出符号时，目标符号本身必须是源模块中的公开符号。如果当前模块已经存在同名定义，则应当报错，除非显式更换别名。

### FFI

```c
extern {
  public Int open(CString path, Int options)
  public Int write(Int fd, UInt8[] buf, Int count)
}

```
