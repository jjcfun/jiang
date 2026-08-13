# 闭包设计与实现状态

本文描述 Jiang 闭包和函数指针的设计方向。核心区分是：

- `RawFn<Ret, Args...>` 表示裸函数指针，不带 environment，可与 C 函数指针互通。
- `Fn<Ret, Args...>` 表示可重复调用的栈上 Jiang 闭包值，可带 environment，movable。
- `FnOnce<Ret, Args...>` 表示只能消费调用一次的栈上 Jiang 闭包值。
- `Fn<Ret, Args...>^` 表示堆上的 owned closure，environment 生命周期跟随 owner。

闭包表达式必须出现在有明确 expected callable type 的位置，不能像普通局部变量一样从闭包
表达式本身推导出公开匿名类型。这个规则接近 Swift 的 closure 使用方式：调用者先给出
函数类型，闭包体再按这个 expected type 检查。

## 已实现状态

基础闭包闭环已经落地：`RawFn` / `Fn` 分离、lambda expected type 下推、捕获分析、
`Fn` / `Fn^` lowering、heap env、`Fn(raw)` 显式包装、`Fn^$.ref()` callable borrow view，
以及对应 borrow / drop 检查。0.4.7-2 在此基础上让可变 capture 使用 `T&!`，并参与唯一
借用、最后一次使用和 async/domain 边界检查。

0.5.1 的 JIL 在 borrow check 前保留 typed closure environment、capture operands 和栈环境
place。capture 携带的 loans 绑定到 `Fn` / `Fn^` 根并随 move、参数和聚合传播；只有 LLVM
lowering 才把环境擦成 `{ receiver, vtable }` ABI。栈环境使用普通 JIL local，因此 async
函数中的闭包环境可以复用 coroutine frame rewrite，不依赖 backend block 内临时 `alloca`。

已验证的行为见本文末尾测试清单。测试清单中的 `[x]` 表示当前已有语言测试或 smoke
覆盖；未在清单中标成 `[x]` 的完整 `Send` / `Sync` 等能力仍属于后续设计。

## 目标

- 支持读取外层局部变量的闭包。
- 支持把闭包赋给显式 `Fn<...>` 类型的局部变量或参数。
- 支持用 `new { ... }` 创建 `Fn<...>^` 堆闭包。
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

`RawFn<Ret, Args...>`、`Fn<Ret, Args...>` 和 `FnOnce<Ret, Args...>` 是编译器内建类型族。
`Args...` 是内建的 type parameter pack，不要求先开放普通用户泛型参数包。

两者默认使用空 `LifetimeContract`：返回类型 `Ret` 不能携带参数 borrow。允许 callable
返回某个参数的 borrow 时，由接收它的函数通过命名 result/参数和 `@life` 显式声明来源；
普通函数自身则使用 signature elision：reference receiver 优先；否则只有唯一非空参数 root
才能成为默认返回来源。零个或多个非空 root 时必须显式写 `@life`，其中 `@life()` 确认空契约。

```jiang
Fn<Bool, Int, Int>       // 栈闭包值，可能带 environment
Fn<Bool, Int, Int>^      // owned closure handle，environment 在 heap 上
FnOnce<Bool, Int, Int>   // 消费调用一次的栈闭包值
FnOnce<Bool, Int, Int>^  // 消费调用一次的 owned heap closure
RawFn<Bool, Int, Int>    // 裸函数指针，不带 environment
```

`Fn` 与 `FnOnce` 使用相同的 closure ABI；调用所有权是独立的类型语义，不产生另一套表示。
`FnOnce` 调用会消费 callable place，因此不能通过 `FnOnce&` 调用，也不能再次调用原值。
其闭包体可以 move 按值捕获；普通同步 `Fn` 的闭包体不能 move 按值捕获。当前 async callable 继续使用
Task/coroutine 的独立 ownership protocol，因此不允许声明 async `FnOnce`。

`RawFn<...>` 的运行时值是函数入口。它不保存捕获环境，适合 top-level function、type/static
function、未绑定实例方法和非捕获 lambda。

`Fn<...>` 的源码语义是 movable stack closure object。每个 closure expression 有一个编译器内部
匿名 closure object 类型，字段就是捕获 environment。公开的 `Fn<...>` 是 erased callable view，
运行时抽象为 `{receiver, vtable}`：`receiver` 指向匿名 closure object，`vtable` 指向只读
callable vtable。

callable vtable 至少包含：

- `call: RawFn<Ret, Void*, Args...>`，使用 erased receiver 调用 closure body。
- `drop: RawFn<Unit, Void*>`，销毁 receiver 指向的匿名 closure object。

普通 `Fn` 的 receiver 指向当前栈帧或当前 aggregate 内的匿名 closure object，因此 `Fn<...>` 不能
返回、保存到 heap/global，也不能写入可能比当前函数更久的外部位置。

`Fn<...>^` 是 owned closure handle。`new { [captures] args => body }` 直接构造 heap closure object；
它不是把一个普通 stack `Fn` 再装进 `Box`。`Fn^` move 时只移动 owner handle；drop 时通过
vtable 中的 `drop(receiver)` drop heap closure object，并释放其 storage。

调用时抽象成 `vtable.call(receiver, args...)`：call 槽的底层调用约定为
`RawFn<Ret, Void*, Args...>`，调用时先传 `receiver`，再传源码参数。

`Fn<...>` 是统一 erased callable 类型，不把每个 closure 的匿名具体类型暴露给用户。每个
closure expression 仍有内部 env layout；捕获字段按顺序紧密放入连续 env memory。非捕获 lambda
的 receiver 可以为空；`Fn(raw)` 的 receiver 保存 raw 函数入口，call 槽指向编译器生成的 trampoline。

源语言不暴露闭包表达式自己的匿名类型。每个捕获闭包表达式对应独立 environment layout。
即使两个闭包形状相同，也不要求共享内部表示。

`RawFn<...>` 是 copy；`Fn<...>` 和 `Fn<...>^` 都是 movable。`Fn<...>` 不应因为隐藏的 env layout
在同一个公开类型下有时 copy、有时 non-copy；基础规则把 `Fn` 当作非 Copyable。后续如果需要
可复制 closure，可以增加显式能力类型或约束，而不是让 `Fn<...>` 的 copy 能力依赖调用点。

`Fn<...>` 和 `Fn<...>^` 不暴露 `$.ptr()`。闭包值不是 C 函数指针，`ptr` 也不应泄漏 receiver、
type info 或 heap closure object 的内部布局。需要 C ABI 函数指针时使用 `RawFn<...>`。

## Closure ABI

`RawFn<Ret, Args...>` 的 ABI 就是函数入口地址。它不携带 receiver，不需要 drop，也不代表
Jiang closure object。C ABI 函数指针只应该映射到 `RawFn<...>`。

`Fn<Ret, Args...>` 的运行时值固定为两个字段：

```text
{
  receiver: Void*,
  vtable:   { call: RawFn<Ret, Void*, Args...>, drop: RawFn<Unit, Void*> }*
}
```

`receiver` 指向编译器合成的 closure object。对普通 stack `Fn`，closure object 位于当前栈帧
或当前 aggregate 内；对 `Fn^`，closure object 位于 heap。非捕获 lambda 可以使用空 receiver。

`call` 槽负责把 erased receiver cast 回真实 closure object，再执行 lambda body。`drop` 槽负责
销毁 receiver 指向的 closure object；对于 heap `Fn^`，drop shim 还会释放 heap storage。空 env
或不需要 runtime drop 的 env 可以使用 noop drop shim。

`Fn^` 是 owner handle。`Fn^` 本身可以直接调用；`Fn^$.ref()` 返回 `Fn&`，用于从 owner handle
临时借出 callable view。`Fn^&` 表示 owner handle slot 的引用，不是 callable view；需要先通过
`$.ref()` 借成 `Fn&` 后再调用。

`Fn(raw)` 生成的 `Fn` 使用 raw 函数入口作为 receiver，vtable 的 `call` 槽是编译器生成的
trampoline：先取出 receiver 中保存的 raw function，再以源码参数调用它。这个方向不影响 C ABI；
反向 `Fn -> RawFn` 不支持。

## Lambda 语法

canonical lambda 使用 brace closure 语法：

```jiang
Fn<Int, Int> inc = { value => value + 1 };

Int base = 10;
Fn<Int, Int> add_base = { value => value + base };

RawFn<Int, Int, Int> sum = { left, right => left + right };
```

lambda 必须有 expected callable type。参数类型可以从 expected callable type 推导：

```jiang
Fn<Bool, Int, Int> less = { left, right => left < right };
RawFn<Bool, Int, Int> raw_less = { left, right => left < right };
```

没有 expected type 时，lambda 表达式不能作为局部变量 initializer：

```jiang
let bad = { left, right => left + right }; // fail
```

当 expected type 是 `Fn<...>` 时，lambda 可以捕获环境：

```jiang
Int base = 1;
Fn<Int, Int> add_base = { value => value + base };
```

当 expected type 是 `RawFn<...>` 时，lambda 必须无捕获：

```jiang
Int base = 1;
RawFn<Int, Int> add_one = { value => value + 1 };    // ok
RawFn<Int, Int> bad_add = { value => value + base }; // fail: captures environment
```

显式 capture list 写在参数之前，只选择已有 local 及其捕获方式：

```jiang
Config config = load_config();
Fn<Int, Int> f = {
    [old_value, ref config]
    arg => arg + old_value + config.offset
};
```

capture list 不是局部变量声明或 initializer 语法，只保留三种形式：

```jiang
[value]       // 按值：Copyable copy，其他 Movable move
[ref value]   // 共享借用
[ref! value]  // 独占借用
```

`[ref value]` 与 `_ value = value$.ref()` 使用同一语义，`[ref! value]` 与
`_ value = value$.mut_ref()` 使用同一语义。已有 reference handle 的 reborrow 是幂等的，
因此 `T&!` 可以通过 `[ref! value]` 捕获为 `T&!`，不会形成嵌套的 `T&!&!`。

capture 名字在闭包内保持不变。需要捕获表达式结果、快照或不同名字时，先使用普通 local：

```jiang
Int snapshot = counter;
Config config = load_config();
Fn<Int, Int> f = { [snapshot, ref config] arg => arg + snapshot + config.offset };
```

未列入列表的外层 local 默认按共享引用/view 捕获，raw pointer 是按值例外：

- 普通 value 和 owner handle 借用其 binding storage，不发生隐式 copy/move。
- raw pointer 按值复制；`T*!` 保留写入 pointee 的能力，但不能修改外层 pointer binding。
- 已有 `T&` / slice 保持共享 view；`T&!` 隐式 reborrow 为 `T&`，不转移唯一 capability。
- 隐式 capture 不能写入外层 storage；需要写入时显式写 `[ref! value]`。
- 需要把值或 owner 移入 environment 时显式写 `[value]`。

闭包体里对隐式捕获名字的自动解引用不是按 env field 类型临时猜测，而是 capture metadata 的
一部分。普通 value 的 environment 字段保存 shared reference，body 使用原名时自动访问原 place；
已有 shared borrow 或 raw pointer 则直接保存 handle value。显式 capture 与参数使用同一局部 binding
namespace，不能重名。

## RawFn 和 Fn 转换

`RawFn<...>` 可以通过 `Fn(raw)` 显式转换成同签名的 `Fn<...>`。这个转换只包装函数入口，
不绑定参数，不捕获源码变量。
运行时用一个按签名缓存的 trampoline 从 `receiver` 取回 raw 函数再调用：

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
Fn<Int, Int> f = { value => value + 1 };
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
sort { left, right => self.compare(left, right) };
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

默认捕获必须能从闭包 body 和 expected type 推导出安全 environment：

- 普通 value 和 owner local 的隐式捕获是共享引用，闭包值不能活过被捕获 storage 的生命周期；
  raw pointer 是不携带语言级 lifetime 的按值例外。
- 写入外层可写 storage 必须显式 `[ref! value]`，并满足唯一借用约束。
- owner move 只能通过显式 `[value]` 进入 environment，不能由默认捕获隐式发生。

显式 `[value]` 可以让闭包拥有值副本或被移动的值；隐式 capture 只借用外层 storage。

后续能力可以再加入：

- 精确字段捕获：只捕获被使用的字段，而不是整个 local。

owner capture 牵涉 consuming call、drop 和闭包重复调用规则，应该和 `FnOnce` 或等价能力
一起设计。

## 调用能力

长期可扩展消费 environment 的调用能力：

- `Fn`：可重复调用；environment 字段是否可变由字段类型决定。
- `FnOnce`：会消费 environment，只能调用一次。

基础闭环保守处理：

- `RawFn<...>`：不带 environment，可重复调用。
- `Fn<...>`：movable stack closure object，可重复调用；允许共享引用捕获和可变 env 字段。
- `Fn<...>^`：movable owned closure handle；调用时可临时借成 `Fn<...>&`。
- 修改 capture 字段不引入 `FnMut`；后续由 write / async / send-like effect 机制约束数据竞争。
- owner capture 可以进入 `Fn^` 的 heap environment；消费 environment 的调用能力等 `FnOnce` 设计完成。

## Lifetime 和存储

`Fn<...>` 的 env memory 在栈上。无论是否捕获，裸 `Fn` 都不能作为返回值、不能写入 heap/global，
也不能保存到可能比当前函数更久的 aggregate 中：

```jiang
Fn<Int> make_bad() {
    { => 42 } // fail: stack Fn outlives its env storage
}
```

栈内调用允许引用捕获：

```jiang
Int local = 1;
Fn<Int> f = { => local + 1 };
Int value = f();
```

`Fn<...>^` 的 env memory 在 heap 上。`new { ... }` 会直接构造 heap closure object；隐式捕获仍可
使用，但默认是引用捕获，因此是否能流出当前作用域交给 lifetime / borrow check 判断：

- `return new { => 1 }` 没有外部 borrow，可以通过。
- `return new { => local }` 隐式捕获 `local&`，如果 `local` 是当前栈局部，则生命周期检查失败。
- `return new { [value] => value }` 把 owner 移入 heap env，env 随 `Fn^` owner 存活。

```jiang
Fn<Int>^ make_bad() {
    Int local = 1;
    new { => local } // fail: env 中的 local& 逃逸
}

Fn<Int>^ make_answer() {
    new { => 42 } // ok: heap env 为空
}
```

`Fn^$.ref()` 返回 `Fn&`，表示把 heap closure 临时借成栈内 callable view。`Fn^&` 如果以后需要，
应表示 owner handle slot 的引用，不作为普通调用所需的借用形式。

struct / union 的 public region、tuple 元素和 callable result/参数可提供契约名。callable
契约只能使用这些声明名，例如 `@life(f.result: f.input)`；不支持 `f[0]`、`f[1]`
一类位置路径。closure environment 是 ABI 隐藏参数，不能出现在公开 contract 中。

## Borrow 和可变捕获

可变捕获应借用被捕获 storage，并和 `T&!` 参数规则一致：

```jiang
Int total! = 0;
Fn<(), Int> add = { value =>
    total = total + value;
};
```

在 `add` 存活期间，外层不能再创建冲突的可变借用或写入 `total`，除非 borrow check 能证明
闭包不再使用。这个规则复用“最后一次引用使用后允许创建唯一可变借用”的能力。

## Lowering

Parser 只记录 closure expr 的语法事实。capture 分析应在 resolve/type-check 后进行，
因为需要知道 identifier 是否解析到外层 local、字段、global 或函数。

建议阶段：

1. AST：新增 `lambda_expr`，包含 params、body、是否 block body。
2. Resolve：lambda body 进入子 scope；外层 local 引用标记为 capture candidate。
3. Semantic Model：为 lambda 分配内部 function def，在 `sem.Lambda` 上记录 params、captures 和 call body。
4. Type check：从 expected type 检查参数和返回，并推导 capture kind、call ability。
5. Borrow check：检查 capture lifetime、mutable-borrow conflict、move 后使用。
6. JIL：`Fn` 构造 stack anonymous closure object，并生成 callable vtable。
7. JIL：`Fn^` 构造 heap anonymous closure object，并复用 callable vtable call/drop 路径。
8. Drop elaborate：通过 `vtable.drop(receiver)` 销毁 closure object。

当 expected type 是 `RawFn<...>` 时，不构造 environment；如果 capture analysis 发现任何捕获，
直接报错。

Semantic Model 不把 closure env 伪装成源码 struct，也不新增 AST 节点。`sem.Lambda` 平铺保存显式
callable-local binding metadata；params 和显式 captures 属于同一个局部 binding namespace：

```text
sem.Lambda {
  params:   [x, y]
  captures: [base, name]
}
```

params 和显式 captures 不能同名。resolve 结束后，
全局/成员 namespace 仍保存在 `NamespaceStore`，但 lambda 内的参数、显式 capture local 和普通
local 都已经落成具体 `DefId` / `sem.LocalId`，后续阶段不再通过字符串名字查找它们。

`sem.LambdaParam` 和 `sem.LambdaCapture` 本身不直接保存 type。type check 从 expected callable type
绑定参数类型，并从显式 capture 的同名 source local 绑定 capture 类型，统一写入对应
`sem.Local.def_id` 的 `def_type`：

```text
lambda.params[i].local_id   -> sem.Local.def_id -> TypeCheckStore.def_type
lambda.captures[i].local_id -> sem.Local.def_id -> TypeCheckStore.def_type
```

type check 还会把显式 capture 和隐式 capture 合并记录到 `TypeCheckStore.lambda_captures`。
这张 side table 是统一 capture list，按后续 env field 顺序保存 capture kind、source def、
local def 和 capture type：

```text
lambda.function_def -> [
  { kind: explicit, source_def: base, local_def: base, type: Int, field: 0, deref_on_use: false },
  { kind: implicit, source_def: name, local_def: name, type: String&, field: 1, deref_on_use: true },
]
```

隐式 capture 由 lambda body 中指向外层 local/parameter 的 `def_ref` 推导，按首次出现顺序去重。
显式 capture 的 `source_def` 和 `local_def` 分别表示同名外层 source 与闭包内 binding；只有隐式
capture 用 `source_def` 匹配闭包体中的外层 `def_ref`。
当前 Semantic Model 不把这些 `def_ref` 重写成新的 capture local；type check、JIL lowering 和 borrow check
通过 `lambda_captures` side table 把它们映射到 env field。

## Environment 表示评估

当前实现把捕获 env 降低为编译器合成的私有匿名 struct，而不是 tuple。env 的语义信息
仍保存在 `LambdaCaptureInfo` side table 中：

- `kind` 区分显式 capture 和隐式 capture。
- `source_def` / `local_def` 描述外层来源和闭包体中的绑定。
- `type_id` 是 capture 字段类型。
- `field_index` 是后端 env layout 的稳定字段序。
- `deref_on_use` 描述隐式引用捕获在 body 中是否自动解引用。
- capture 的 `T&!` 字段类型记录显式可变借用需求。

JIL lowering 从 `LambdaCaptureInfo` 生成 sourceless 的私有 struct def 和 field def。stack `Fn`
创建临时 env struct，heap `Fn^` 分配同形状 env struct，并把指针擦成 `Void*` 存入 closure
object。field 名字尽量沿用 capture 名字，没有稳定源码名字时使用 `_0`、`_1` 这类匿名字段名。

这个 synthetic env struct 只服务 layout、projection、drop 和诊断展示，不进入 resolver namespace，
也不是用户可写或可引用的 Semantic Model struct。borrow check、drop elaborate 和 backend 不从 struct 名字
反推 capture 语义，只消费 `LambdaCaptureInfo`、field index 和普通类型/drop 规则。后续 debug info、
artifact 或 async frame 如果需要更强的 env 身份，应继续从现有 `LambdaCaptureInfo` 生成，而不是把
源码 Semantic Model 改写成用户可见 struct。

## 与 async / 数据竞争的关系

async closure 已建立在普通 closure 之上。源码仍使用普通
`{ [captures] params => body }`，async effect 由 expected `Fn<async ...>` 类型决定；
vtable code slot 采用统一的
`(env, args..., continuation*) -> ()` 启动 ABI，启动 shim 分配具体 coroutine frame，并把 closure
environment 与参数写入 frame。完成 shim 释放 frame 后恢复调用方 continuation。

当前第一阶段只开放同 Domain 动态调用。跨 Domain 调度、取消传播以及 async `RawFn`
的完整运行时适配仍需继续完成。数据竞争机制还需要约束：

- 哪些 capture 可以跨 await 存活。
- 哪些 closure 可以跨 task/thread move。
- 可变引用 capture 是否允许进入 async task。
- raw pointer capture 是否需要 unsafe 或 Send-like 约束。

因此闭包设计应避免把“捕获即可跨线程/跨 task”作为默认承诺。

## 测试清单

下面清单同步到当前实现状态，包括 Fn/RawFn 混用诊断和可变 capture 借用检查。

- [x] 非捕获 lambda 可赋给显式 `RawFn<...>`。
- [x] 非捕获 lambda 可赋给显式 `Fn<...>`。
- [x] 捕获 lambda 可在 `Fn<...>` expected type 下通过 type check。
- [x] 捕获 lambda 赋给 `RawFn<...>` 报错。
- [x] `RawFn<...>` 可通过 `Fn(raw)` 包装成同签名 `Fn<...>`。
- [x] `Fn<...>` 运行时值使用 `{receiver, vtable}` 两字段表示。
- [x] `Fn<...>` call slot 使用 receiver-first 调用约定。
- [x] `Fn(raw)` 通过 trampoline 适配 receiver-first 调用约定。
- [x] `Fn<...>` 不能转换成 `RawFn<...>`。
- [x] Semantic Model 使用 `sem.Lambda` 平铺记录 params 和显式 captures。
- [x] lambda params 和显式 capture 同名时报错。
- [x] 显式 capture 只接受已有 local 的同名 value/ref/ref! 模式。
- [x] type check side table 记录显式和隐式 capture metadata。
- [x] lambda body 按 expected `RawFn` / `Fn` 的 unsafe、async effects 检查。
- [x] async lambda 根据 expected `Fn<async ...>` 类型确定 effect，不增加重复的表达式前缀。
- [x] 同 Domain async `Fn` 通过 continuation ABI 启动和恢复。
- [x] async `Fn` 跨 Domain 动态调用。
- [x] async `RawFn` 同 Domain、跨 Domain 和 extern 启动 ABI。
- [x] `RawFn<Result<Ret, Err>, ...>` 和 `Fn<Result<Ret, Err>, ...>` 返回位解析。
- [x] `self.method` 产生带显式 receiver 参数的 `RawFn<...>`。
- [x] 需要绑定 receiver 时必须写显式 lambda。
- [x] 没有 expected type 的 lambda initializer 报错。
- [x] 普通 value 与 owner 隐式按共享引用捕获，raw pointer 按值捕获。
- [x] 隐式捕获只提供共享读取，写入必须显式使用 `ref!`。
- [x] 共享引用捕获外层非平凡 local 并立即调用。
- [x] 显式可变捕获修改外层 `!` storage。
- [x] 裸 `Fn` 返回导致 stack env 流出当前函数时报错。
- [x] 裸 `Fn` 保存到返回 aggregate 导致 stack env 流出当前函数时报错。
- [x] 捕获闭包传给只调用不保存的参数可以通过。
- [x] 非捕获裸 `Fn` 返回也会报错。
- [x] `Fn` 是 movable/non-copy closure value；赋值默认 move，也可以用 `$.move()` 强制显式转移。
- [x] `Fn<...>` 和 `Fn<...>^` 不暴露 `$.ptr()`。
- [x] `new { ... }` 构造 `Fn<...>^` 的非捕获闭包，并支持直接调用。
- [x] `new { ... }` 捕获字段的 heap env 后端 lowering 完整验证。
- [x] `Fn^$.ref()` 返回 `Fn&`，用于把 heap closure 临时借成 callable view。
- [x] 裸 `Fn` 显式 owner capture 随 stack closure object 离开 scope 自动 drop。
- [x] `Fn^` 可放入 struct、optional、tuple、array 后返回，并借成 `Fn&` 调用。
- [x] 对外层 local 执行非法 move 的闭包报错。
- [x] 修改 capture 字段不额外要求独占 closure value，后续交给 effect / 数据竞争机制约束。
