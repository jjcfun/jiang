# 闭包设计草案

本文描述 Jiang 闭包和函数指针的设计方向。核心区分是：

- `RawFn<Ret, Args...>` 表示裸函数指针，不带 environment，可与 C 函数指针互通。
- `Fn<Ret, Args...>` 表示 Jiang 闭包值，可带 environment，用于日常高阶函数。

闭包表达式必须出现在有明确 expected callable type 的位置，不能像普通局部变量一样从闭包
表达式本身推导出公开匿名类型。这个规则接近 Swift 的 closure 使用方式：调用者先给出
函数类型，闭包体再按这个 expected type 检查。

## 目标

- 支持读取外层局部变量的闭包。
- 支持把闭包赋给显式 `Fn<...>` 类型的局部变量、字段或参数。
- 支持非捕获 lambda 初始化 `RawFn<...>`。
- 支持 `RawFn<...>` 与 C 函数指针互通。
- 基础闭环支持默认捕获推导，并由 lifetime / borrow check 阻止借用逃逸。
- 为 async closure、iterator adapter、task spawn 和数据竞争检查预留语义空间。

## 设计边界

- async closure 建立在普通捕获闭包之上，不作为基础闭包语义的一部分。
- 闭包是否能直接装箱成 trait object 需要等 callable trait 形状稳定后再决定。
- 闭包表达式不能脱离 expected type 单独推导类型。
- `Fn<...>` 不能转换成 `RawFn<...>`；一旦擦成闭包值，就不再依赖隐藏的“是否捕获”事实。
- 完整 `Send` / `Sync` 或等价并发安全检查不放进基础闭包语义。

## 类型模型

`Fn<Ret, Args...>` 和 `RawFn<Ret, Args...>` 是编译器内建类型族。`Args...` 是内建的
type parameter pack，不要求先开放普通用户泛型参数包。

```jiang
Fn<Bool, Int, Int>       // 闭包值，可能带 environment
RawFn<Bool, Int, Int>    // 裸函数指针，不带 environment
```

`RawFn<...>` 的运行时值是函数入口。它不保存捕获环境，适合 top-level function、type/static
function、未绑定实例方法和非捕获 lambda。

`Fn<...>` 的运行时值是 Jiang callable。它可以表示 `{env, code}`，也可以由 `RawFn<...>`
包装得到空 environment 的闭包值。

源语言不暴露闭包表达式自己的匿名类型。每个捕获闭包表达式对应独立 environment layout。
即使两个闭包形状相同，也不要求共享内部表示。

## Lambda 语法

沿用现有 lambda 语法：

```jiang
Fn<Int, Int> inc = (value) => value + 1;

Int base = 10;
Fn<Int, Int> add_base = (value) => value + base;

RawFn<Int, Int, Int> sum = (left, right) => { left + right };
```

lambda 必须有 expected callable type。参数类型可以从 expected callable type 推导：

```jiang
Fn<Bool, Int, Int> less = (left, right) => left < right;
RawFn<Bool, Int, Int> raw_less = (left, right) => left < right;
```

没有 expected type 时，lambda 表达式不能作为局部变量 initializer：

```jiang
let bad = (left, right) => left + right; // fail
```

当 expected type 是 `Fn<...>` 时，lambda 可以捕获环境：

```jiang
Int base = 1;
Fn<Int, Int> add_base = (value) => value + base;
```

当 expected type 是 `RawFn<...>` 时，lambda 必须无捕获：

```jiang
Int base = 1;
RawFn<Int, Int> add_one = (value) => value + 1;    // ok
RawFn<Int, Int> bad_add = (value) => value + base; // fail: captures environment
```

后续可以引入显式 environment 字段初始化列表。它写在参数列表之后、`=>` 之前：

```jiang
Fn<Int, Int> f = (arg) [value = old_value$.move(), Config& config = config$.ref()] => {
    arg + value + config.offset
};
```

捕获列表不是一套独立的 `move` / `ref` 小语言，而是 closure environment 字段初始化列表。
每个 item 的形式接近局部变量初始化：

```jiang
alias = expr
Type alias = expr
```

`expr` 在闭包创建时求值，结果保存为 environment 字段 `alias`；body 中的 `alias` 解析到
这个字段。移动、借用和复制都由普通表达式语义表达：

```jiang
(arg) [
    owned = old_owned$.move(),
    borrowed = old_value$.ref(),
    Int snapshot = counter
] => {
    arg + snapshot
}
```

未列入列表的外层 local 仍按默认规则自动捕获。默认规则不是“全部引用”：

- 标量和小的 `Copy` 值默认按值捕获，environment 保存创建闭包时的值。
- 只读取的非平凡 local 默认按共享引用捕获，environment 保存 `T&`。
- 写入外层 `!` storage 时默认按可变引用捕获，environment 保存 `T!&`，并参与 unique borrow 检查。
- owner move 不做默认推导；需要写成 `field = value$.move()` 这类显式 environment 字段初始化。

基础闭环可以先实现默认捕获，再逐步补齐显式字段初始化列表的 owner capture 和 drop 语义。

## RawFn 和 Fn 转换

`RawFn<...>` 可以通过 `Fn(raw)` 显式转换成同签名的 `Fn<...>`。这个转换只包装函数入口，
不绑定参数，不捕获新环境：

```jiang
RawFn<Bool, Foo&, Int, Int> raw = Foo.compare;
Fn<Bool, Foo&, Int, Int> f = Fn(raw);

f(self, left, right);
```

`Fn(raw)` 是编译器内建转换表达式，不是普通泛型 `init`。没有 expected type 时，可以从
`raw` 推导出完整 `Fn<...>` 类型：

```jiang
let f = Fn(raw); // Fn<Bool, Foo&, Int, Int>
```

是否允许 `RawFn<...> -> Fn<...>` 隐式转换可以独立决定；显式 `Fn(raw)` 必须支持。反向
转换不允许：

```jiang
Fn<Int, Int> f = (value) => value + 1;
RawFn<Int, Int> raw = f; // fail
```

即使 `f` 的 initializer 是非捕获 lambda，`f` 也已经是普通 `Fn<...>` 值，不能再静态还原为
`RawFn<...>`。

## 方法值

方法调用和方法值取值要区分。普通方法调用：

```jiang
self.compare(left, right)
```

等价于：

```jiang
Foo.compare(self, left, right)
```

但 `self.compare` 作为表达式不绑定 `self`，而是得到未绑定方法的函数指针：

```jiang
RawFn<Bool, Foo&, Int, Int> cmp = self.compare;
RawFn<Bool, Foo&, Int, Int> cmp2 = Foo.compare;

cmp(self, left, right);
```

因此它不能直接传给不带 receiver 的闭包参数：

```jiang
fn sort(Fn<Bool, Int, Int> compare);

sort(self.compare); // fail: RawFn<Bool, Foo&, Int, Int> 不是 Fn<Bool, Int, Int>
```

绑定 receiver 必须显式写 lambda：

```jiang
sort((left, right) => self.compare(left, right));
```

这个 lambda 捕获 `self`，目标类型是 `Fn<Bool, Int, Int>`。

## FFI

C 函数指针使用 `RawFn<...>` 表示：

```jiang
extern fn qsort(
    base: Void*,
    count: UInt,
    size: UInt,
    compare: RawFn<Int, Void*, Void*>
);
```

`RawFn<Ret, Args...>` 与 C 函数指针互通的前提是 ABI、参数类型和返回类型都可被 C 表示。
普通 `Fn<...>` 可能带 environment，不能直接作为 C 函数指针传递。

## Capture 分类

默认捕获必须能从闭包 body 和 expected type 推导出最小安全 environment：

- 标量和小的 `Copy` 值可以按值保存，不借用外层 storage。
- 只读取的非平凡 local 保存共享引用，闭包值不能逃逸超过被捕获 storage 的生命周期。
- 写入外层 `!` storage 保存可变引用，闭包创建和调用都要满足 unique borrow 约束。
- owner move 只能通过显式字段初始化进入 environment，不能由默认捕获隐式发生。

因此基础闭环里，非 owner capture 的闭包不能拥有外层对象；它只能保存值副本或借用外层
storage。

后续能力可以再加入：

- environment 字段初始化列表：`[field = expr, Type field = expr]`。
- owner capture：通过 `field = value$.move()` 移动 owner 进 environment，并在闭包销毁时 drop。
- 精确字段捕获：只捕获被使用的字段，而不是整个 local。

owner capture 牵涉 consuming call、drop 和闭包重复调用规则，应该和 `FnOnce` 或等价能力
一起设计。

## 调用能力

长期应区分三种调用能力：

- `Fn`：不修改 environment，可重复调用。
- `FnMut`：可能修改 environment，需要 unique receiver，可重复调用。
- `FnOnce`：会消费 environment，只能调用一次。

基础闭环保守处理：

- `RawFn<...>`：不带 environment，可重复调用。
- `Fn<...>`：可重复调用；允许共享引用捕获。
- 修改 capture 的闭包：调用需要 unique closure value。
- owner capture / consuming closure：先拒绝，等 `FnOnce` 设计完成。

## Lifetime 和逃逸

按引用捕获的闭包本质上把外层 local 的 borrow 存进 environment。规则应和“引用存入字段”
保持一致：

```jiang
Fn<Fn<Int>> make_bad = () => {
    Int local = 1;
    return () => local; // fail: local borrow escapes
};
```

非逃逸调用允许引用捕获：

```jiang
Int local = 1;
Fn<Int> f = () => local + 1;
Int value = f();
```

如果闭包作为参数传入函数，默认按可能逃逸处理，除非参数类型或调用约定能表达
non-escaping。后续可以加入 `@noescape` 或等价约束，用于高阶函数内联调用。

## Borrow 和可变捕获

可变捕获应借用被捕获 storage，并和 `@unique` 规则一致：

```jiang
Int! total = 0;
Fn<(), Int> add = (value) => {
    total = total + value;
};
```

在 `add` 存活期间，外层不能再创建冲突的可变借用或写入 `total`，除非 borrow check 能证明
闭包不再使用。这个规则要复用现有“最后一次引用使用后允许 unique 操作”的能力。

## Lowering

Parser 只记录 closure expr 的语法事实。capture 分析应在 resolve/type-check 后进行，
因为需要知道 identifier 是否解析到外层 local、字段、global 或函数。

建议阶段：

1. AST：新增 `lambda_expr`，包含 params、body、是否 block body。
2. Resolve：lambda body 进入子 scope；外层 local 引用标记为 capture candidate。
3. HIR：为捕获 lambda 分配内部 closure def，记录显式字段初始化列表和 call body。
4. Type check：从 expected type 检查参数和返回，并推导 capture kind、call ability。
5. Borrow check：检查 capture lifetime、unique conflict、move 后使用。
6. MIR：构造 environment aggregate；闭包调用 lowering 成 `closure.call(env, args...)`。
7. Drop elaborate：基础闭包 environment 不拥有 capture，后续 owner capture 再接入 drop。

当 expected type 是 `RawFn<...>` 时，不构造 environment；如果 capture analysis 发现任何捕获，
直接报错。

## 与 async / 数据竞争的关系

async closure 应建立在普通 closure 之上：async state machine 保存的是 closure environment
和跨 await 存活 local 的组合。数据竞争机制需要约束：

- 哪些 capture 可以跨 await 存活。
- 哪些 closure 可以跨 task/thread move。
- 可变引用 capture 是否允许进入 async task。
- raw pointer capture 是否需要 unsafe 或 Send-like 约束。

因此闭包设计应避免把“捕获即可跨线程/跨 task”作为默认承诺。

## 测试清单

- [x] 非捕获 lambda 可赋给显式 `RawFn<...>`。
- [x] 非捕获 lambda 可赋给显式 `Fn<...>`。
- [x] 捕获 lambda 可在 `Fn<...>` expected type 下通过 type check。
- [x] 捕获 lambda 赋给 `RawFn<...>` 报错。
- [x] `RawFn<...>` 可通过 `Fn(raw)` 包装成同签名 `Fn<...>`。
- [x] `Fn<...>` 不能转换成 `RawFn<...>`。
- [ ] `self.method` 产生带显式 receiver 参数的 `RawFn<...>`。
- [ ] 需要绑定 receiver 时必须写显式 lambda。
- [x] 没有 expected type 的 lambda initializer 报错。
- [ ] 小的 `Copy` 值默认按值捕获。
- [ ] 共享引用捕获外层非平凡 local 并立即调用。
- [ ] 可变捕获修改外层 `!` storage。
- [ ] 捕获闭包返回导致 local borrow escape 报错。
- [ ] 对外层 local 执行 move 的闭包报错。
- [ ] 修改 capture 的闭包需要 unique closure value。
