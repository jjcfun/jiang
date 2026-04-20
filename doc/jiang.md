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

Jiang 语言支持显式的类型转换，采用 `a$.cast(Type)` 的语法。

这里的 `$` 符号表示**进入隐式操作层**。可以把它理解成：对一个值或一个类型，切换到它的“隐式层 / 元层”再进行操作。

- 对值使用：`a$.xxx()`
- 对类型使用：`Type$.xxx()`
- 对复合表达式使用：`(expr)$.xxx()`

后缀 `$` 绑定在它左侧的完整对象上。因此：

- `a$.b` 等价于 `(a$).b`
- `a.b$` 等价于 `(a.b)$`
- 如果要对整个 `(a + b)` 进入隐式操作层，必须写成 `(a + b)$.xxx()`

例如：

- `a$.cast(Int)`：对值 `a` 做类型转换
- `a$.ref()`：从值 `a` 获取一个临时指针
- `a$.addr()`：获取值 `a` 的地址值
- `self.data$.free()`：对 `self.data` 这个完整表达式做隐式释放操作
- `Int$.size()`：获取类型 `Int` 的大小

在当前设计中，许多原本会被写成内建函数的操作，都会逐步迁移到隐式操作层。例如，类型大小不再写作 `size_of(T)`，而统一写作 `T$.size()`。

`cast` 是一个特殊的隐式层方法，它接收一个类型表达式作为参数。

```c
Float f = 10.5;

// 将 Float 转换为 Int
Int i = f$.cast(Int);

print("i = %d", i); // 输出：i = 10

// 将 Int 转换为 UInt8
Int val = 255;
UInt8 small_val = val$.cast(UInt8);

// 注意：某些危险的转换（如指针强转）可能需要包裹在 sudo 块中
Int addr = 0x12345678;
sudo {
    Int* ptr = addr$.cast(Int*);
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

现阶段，Jiang 语言先不引入所有权和借用系统，也不区分多种 allocator。运行时只有默认堆分配器。
在此基础上，指针语义先约定为：

- `T*`：指向默认堆分配器分配出的 `T`
- `T&`：临时指针，通常用于引用已有值，不承担释放职责

后续如果语言正式引入所有权、借用或多 allocator，这里的规则再进一步细化。

#### 指针类型

指针类型也遵循 **从左往右，从里到外** 的原则

```c
// 在栈中开辟内存空间
Int a = 123;

// new关键字可以在堆中开辟内存空间，并返回一个指针
Int* b = new 123;

// 在堆中创建数组，并返回一个数组指针
Int[3]* c = new {1, 2, 3};

// 临时指针
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

Int* b = new 200;

// 指针默认自动解引用，b直接表示了其元素的值
Int c = a + b;

print("c = %d", c); // 输出： c = 300

// '$'符号用于进入b的隐式操作层，此时可以调用指针本身的一些方法
b$.free();
```

当前版本里，Jiang 只约定 `*` 指针可通过 `ptr$.free()` 主动释放默认堆分配器上的对象；`&` 指针只是临时指针，不参与释放。

```c
// 定义一个指针a，指向堆内存
Int* a = new 100;

// 可以主动释放指针的内存空间
a$.free();
```

### 切片（Slice）

slice 是一个带有 `length` 属性的胖指针。它与数组的区别在于：数组类型的长度是在编译器确定的，而 slice 的长度在运行时确定。
现阶段可以把 `T[]` 看作一个轻量的 `{ ptr, len }` 视图值；它本身不表达所有权语义。

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
@where(T: Numbric)
T[] sort<T>(T[] list, Fn<Bool, T, T> compare)

// 支持范型的排序，会抛出异常，其中E可以为任意类型
@where(T: Numbric)  
T[]@E sort<T, E>(T[] list, Fn<Bool@E, T, T> compare)

@where(T: Numbric, E2: CompareError)  
T[]@E1 sort<T, E1, E2>(T[] list, Fn<Fn<Bool@E2, T, T>@E1, T[]> compare)
```

#### Async

```c
@where(T: Numbric, E2: CompareError)  
async T[]@E1 sort<T, E1, E2>(T[] list,Fn<async Fn<async Bool@E2, T, T>@E1, T[]> compare)

@where(T: Numbric, E2: CompareError)  
@alias(Cmp = Fn<async Bool@E2, T, T>)
async T[]@E1 sort<T, E1, E2>(T[] list, Fn<async Cmp@E1, T[]> compare)
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

struct 可以自定义 `init` 函数。

`init` 具有以下语义：

- `init` 是结构体内唯一的特殊构造器入口
- `init` 允许可见性修饰，例如 `public init(...)`
- `init` 隐式拥有 `self`
- `init` 不声明返回类型，语义等价于 `()`
- `init` 只允许 `return;` / `return ();`
- `init` 不能写成 `static init`
- `Point(...)` / `Point.init(...)` / `new Point(...)` 不再作为构造语法存在
- 构造统一使用 `Point { ... }`
- 如果类型定义了 `init`，那么 `Point { ... }` 会按 `init` 参数名构造
- 如果类型没有定义 `init`，那么 `Point { ... }` 才表示默认字段初始化
- 一旦类型定义了 `init`，默认字段初始化语法失效
- `new Point { ... }` 会先按上面的规则构造出 `Point` 值，再把这个值放到堆上

```c
struct Point {
  Int x;
  Int y;

  public init(Int x, Int y) {
    self.x = x;
    self.y = y;
    return;
  }
}
```

```c
Point p1 = Point { x: 1, y: 2 };
Point* p2 = new Point { x: 1, y: 2 };
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

### 泛型（Generic）

Jiang 语言通常以 `<T>` 形式声明泛型参数。

`@where(...)` 是一种编译期约束注解，用于约束其后一个泛型声明中的类型参数。  
`@where(...)` 中引用的参数名，必须出现在后续声明的 `<...>` 泛型参数列表中。

#### Concept

`concept` 用于定义一种**仅存在于编译期**的约束类型。  
它类似泛型系统里的 trait，但不作为运行时类型使用，也不能直接作为普通变量、字段、参数或返回值类型。

例如，`Numbric` 可以被定义为一个 concept，表示“所有数值类型”：

```c
concept Numbric;
```

此时：

- `Int`、`Float`、`Double` 等数值类型可以被视为满足 `Numbric`
- `Numbric` 本身不能直接写成 `Numbric x;`
- `Numbric` 主要用于 `@where(...)` 这类泛型约束位置

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
