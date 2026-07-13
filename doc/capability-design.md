# Capability 设计草案

本文记录 Jiang capability 体系的当前设计草案，并讨论它如何支撑 effect、协程、异步和
数据竞争防护。这里描述的是方向，不是已实现规范。
设计目标是：尽量把数据竞争检查压到编译期完成，不为普通可变访问插入锁、atomic
或动态 borrow flag。

## 目标

- `async` 类似 Kotlin `suspend`：调用 async 函数表示可能挂起，不要求显式 `await`。
- 普通函数不绑定 execution domain；它只在调用者当前同步控制流中直接执行。
- `T!&` 表示某个 serial domain 下的可变引用；同一 serial domain 内允许多个 `T!&` alias。
- 数据竞争检查转换为 domain 切换检查：普通 `T!&` 不能跨 serial domain 逃逸。
- `Atomic`、`Mutex`、`Channel`、`Actor` 等同步/并发类型是跨 domain 共享可变状态的显式入口。
- `unique` 不作为普通 `T!&` 的默认语义；它是参数位置的额外能力，用于 noalias、move/drop
  协议或特殊 API 约束。

本文的主体是 capability。`effect` 用来描述调用上下文，例如 `unsafe`、`async`、`sync`；
data race 本身不靠 `write` / `set` effect，而靠 serial domain 与 capability 检查。

## 术语

- **domain**：执行域。它描述一段代码在哪里运行，以及可变引用属于哪个执行上下文。
- **serial domain**：串行执行域。同一个 serial domain 上的 continuation 不会并发执行。
- **current domain**：当前 async/sync domain block 或 coroutine 执行所在的 domain。
- **domain-neutral async function**：没有显式 domain 的 async 函数。它本身不选择 executor，
  只能在已有 current domain 的上下文中运行。
- **domain switch / hop**：从一个 domain 进入另一个 domain 的执行上下文。

普通 executor 或线程池不一定是 serial domain。只有能保证同一 domain 上代码串行执行的 domain，
才能用于放宽 `T!&` alias 和跨挂起点规则。

## Domain 能力

语言核心只认识抽象 capability，不内建 `UiDomain`、`WorkerDomain` 这类具体 domain。
0.4.6 使用单一 `Domain` trait，并用 `const` 约束形式的 associated item 表示 domain
执行语义，避免 `Domain<.serial>` / `Domain<.concurrent>` 这种 generic 形式带来的多重实现问题：

```jiang
enum DomainKind {
    serial,
    concurrent,
}

trait Domain {
    associated kind: const DomainKind;
}

```

具体 domain 由 runtime、框架或第三方库提供，domain identity 由类型本身表示：

```jiang
public struct UiDomain: Domain {
    associated kind = .serial;
}

public struct WorkerPoolDomain: Domain {
    associated kind = .concurrent;
}
```

因此：

- `current` 是语言内建特殊值，表示继承当前 async/sync domain context。
- `UiDomain`、`WorkerPoolDomain` 等不是语言魔法；它们来自 import 后可见的类型。
- `async [domain_type]` / `sync [domain_type]` 中的 `domain_type` 必须是实现
  `Domain` 的类型；编译器通过 `Domain.kind` 读取 serial/concurrent 语义。

`Domain` 只描述 identity 和串行性。用户实现 `Domain` 时不需要接触 executor、coroutine frame
或 continuation ABI。编译器根据 `Domain.kind` 选择内部 runtime 调度入口；具体事件循环、线程池
和 UI 主线程适配由 runtime 层提供，不作为 `Domain` 的公开成员。

`Domain.kind == .serial` 保证同一 domain 上的 continuation 不会并发执行；`Domain.kind ==
.concurrent` 允许同一 domain 内多个任务并发执行，因此普通 `T!&` 不能依赖它保证安全。在
concurrent domain 中共享可变状态必须使用 `Mutex`、`Atomic`、`Channel` 或等价同步
capability。

## 语法草案

函数声明只使用 `async` 表示 suspend function。函数前不再使用 `sync` 关键字；普通函数就是
同步函数。`async` 函数可以不带 domain，也可以在定义时指定 domain：

```jiang
async Int load_data();

async [UiDomain] () render(Model!& model) {
    model.loading = true;
    load_data();
    model.loading = false;
}

() inc(Int!& value) {
    value = value + 1;
}
```

不带 domain 的 `async` 函数是 domain-neutral suspend function。它本身不选择 executor，只能
在已有 current domain 的上下文中调用：

```jiang
sync [UiDomain] {
    Int value = load_data(); // 隐式 await，在 UiDomain current domain 上运行
}
```

带 domain 的 `async [D]` 函数是 domain-bound suspend function。调用它表示进入 `D` 执行
callee body，参数按跨入 `D` 的规则检查；返回后 caller continuation 回到原 current domain。
因此这类函数可以避免整个 body 再包一层 `sync [D] {}`。

`async` / `sync` block 可以带 domain。最外层进入异步运行时必须显式写 domain：

```jiang
import app_runtime;

sync [app_runtime.UiDomain] {
    load_data();
}
```

如果一个 keyword 后续还有多个 option，可以用带 key 的形式：

```jiang
async [domain: app_runtime.UiDomain] {
    load_data();
}
```

只有外层已有 current domain 时，内层 `async {}` / `sync {}` 才能省略 domain，并继承 current：

```jiang
sync [UiDomain] {
    Future<Int> a = async { load_data() }; // 继承 UiDomain
    Future<Int> b = async { load_data() }; // 继承 UiDomain
    a.await() + b.await()
}
```

普通函数中直接写无 domain 的 `sync {}` 或 `async {}` 应诊断，因为普通函数没有 current domain。

`async` / `sync` block 后续可以带运行时 context：

```jiang
async [PageDomain, page_ctx] {
    load(model$.ref());
}

async [domain: PageDomain, context: page_ctx] {
    load(model$.ref());
}
```

这里 `PageDomain` 是编译期可见的静态 domain type，用于 data race 检查；`page_ctx` 是后续版本
预留的运行时 async context，可用于调度、取消、页面生命周期或 tracing。0.4.6 尚不开放
`context` option；它不参与 domain 静态相等判断，也不能替代 domain。

函数类型沿用第一个类型参数作为 callable signature head：

```jiang
Fn<async Int>
Fn<async [UiDomain] Int, Model!&>
RawFn<unsafe async [UiDomain] Result<Int, Error>, UInt8*>
```

在 callable type 中，`async [D]` 修饰 callable signature，不修饰返回值类型本身，也不表示
异步地产生一个 `Fn` 值。`Fn<async [D] R, Args...>` 表示调用该 callable 时进入 `D` 并最终
得到 `R`。

`async [UiDomain]` / `sync [UiDomain]` 是 effect keyword option 中
`async [domain: UiDomain]` / `sync [domain: UiDomain]` 的短写。`async [UiDomain, ctx]`
是 `async [domain: UiDomain, context: ctx]` 的短写。函数声明和 callable type 只使用静态
domain type；带 `context` 的形式只用于 block。`UiDomain` 本质上仍是 domain type，不是语言魔法。

## 默认 Domain

普通函数没有默认 domain，也不是隐式 `sync [current]`：

```jiang
() inc(Int!& value) {
    value = value + 1;
}
```

它可以被任意调用者同步调用，执行在调用者当前控制流中。普通函数不能直接建立或等待
Future。如果普通函数要进入异步运行时，必须显式写外层 domain：

```jiang
Int main() {
    sync [UiDomain] {
        load_data()
    }
}
```

在已有 current domain 的 async/sync block 或 async 调用链中，普通函数调用不切 domain，
只是当前 coroutine 内的同步片段。普通函数没有 hidden domain/context 参数。

domain-neutral `async` 函数不绑定 domain；调用点必须已经有 current domain，coroutine frame
也在该 current domain 下执行和恢复。实现上 async frame 可以携带隐藏的 coroutine
context/domain handle，但这个上下文不进入普通参数列表。

domain-bound `async [D]` 函数在签名 metadata 中保存 domain type，调用点按进入 `D` 的规则
检查参数和返回 continuation。它不是普通 `async` 函数的默认形态，而是给“整个函数必须在
某个 domain 执行”的 API 使用。

## `T!&` 的 Domain 语义

`T!&` 是某个 serial domain 下的可变引用。同一 serial domain 内允许多个 `T!&` alias：

```jiang
() bump_twice(Int!& value) {
    Int!& again = value$.ref();
    value = value + 1;
    again = again + 1;
}
```

这不是 Rust `&mut` 的唯一引用模型。安全性来自 serial domain 的串行执行，而不是来自
全局唯一。

因此后端和优化不能默认把 `T!&` 当作 noalias。需要 noalias、重分配、释放、move-out
或严格独占时，在参数位置写 `unique`：

```jiang
() swap(unique Int!& left, unique Int!& right) {
}
```

`unique` 是用户显式声明的调用点能力要求，不是字段或局部变量的常驻类型属性。
编译器负责检查 callee 要求 `unique` 时 caller 必须满足并沿封装边界透传；
编译器不尝试从所有 `unsafe` 函数体中自动推断 `unique`。

## Async 调用与隐式 Await

Jiang 不需要显式 `await`。调用 async 函数就是挂起点：

```jiang
async Int fetch();

async () refresh(Model!& model) {
    model.loading = true;
    Int value = fetch(); // 可能挂起，默认保持 UiDomain
    model.value = value;
}
```

`refresh` 本身不选择 domain。调用者需要在某个 current domain 中运行它：

```jiang
sync [UiDomain] {
    refresh(model$.ref())
}
```

如果需要在其他 domain 启动工作，应使用显式 domain block。普通 mutable ref 不能跨到其他
domain：

```jiang
sync [UiDomain] {
    async [WorkerPoolDomain] {
        update_worker(model) // error: model 属于 UiDomain，不能进入 WorkerPoolDomain
    }
}
```

## Resume 与运行时开销

async coroutine frame 在创建它的 current domain 上执行和恢复：

```jiang
async () foo(T!& x) {
    x = ...
    bar(); // 默认 await
    x = ... // 仍在同一个 current domain 恢复
}
```

实现上：

- 同 domain await：不需要 domain hop，只是普通 coroutine resume。
- `future.await()` 在 waiter 的 current domain 中挂起；Future 可以在其他 domain 执行。
- 跨 domain completion 将 waiter continuation enqueue 回等待方 domain，再从原 current domain 恢复。

静态 capability 检查本身应当是零运行时开销。运行时开销来自实际的 enqueue、suspend/resume
和 Future 等待。

## Domain 切换规则

数据竞争检查主要发生在 domain 切换点。初步规则：

- 普通函数调用不切 domain，callee 只是 caller 当前控制流里的同步片段。
- domain-neutral async 函数调用不切 domain；callee 在 caller 的 current domain 中挂起/恢复。
- domain-bound `async [D]` 函数调用进入 `D`；参数必须能安全进入 `D`，返回后 caller 回到原
  current domain。
- `async {}` / `sync {}` 只有外层已有 current domain 时可省略 domain，并继承 current。
- `async [D] {}` 创建 future，是显式 domain 入口；如果 domain type `D` 不等于 current，
  则是 domain 切换边界。
- `sync [D] {}` 从普通同步世界阻塞进入 domain type `D`，也可以在已有 current domain 中
  显式切到 `D`。
- `future.await()` 不把 caller 留在 Future 的 execution domain；完成后 caller 回到等待方 current domain。

async 函数或 async block 内的 `sync [D] {}` 必须能静态证明 `D` 与 current domain 不同。
domain-neutral async 函数的 current domain 运行时才确定，因此也不能在其中使用静态 `sync [D]`。
同一 serial domain 的 sync wait 会占住该 domain 的执行权，使等待中的 continuation 无法恢复；在
concurrent domain 中也会无谓阻塞 worker。需要继续异步执行时应直接调用 async 函数或使用 async block。
能静态证明跨 domain 的 `sync [OtherDomain] {}` 暂不由这条规则禁止。

跨 domain 时的能力检查：

- `T!&`：只能留在同一个 serial domain 内，不能传给或捕获到其他 domain。
- `T&`：跨 domain 需要 `T` 是可共享的不可变/同步安全类型。
- `T^`：可以 move 到另一个 domain，前提是 `T` 可安全跨 domain 移动。
- `Fn` / `Fn^`：根据参数类型和捕获 environment 判断是否能跨 domain。
- `T*` / `T[*]`：跨 domain 需要 `unsafe` 边界。
- `Atomic<T>` / `Mutex<T>` / `Channel<T>`：由标准库或 runtime 声明为同步安全入口。

`sync [D] {}` 即使会阻塞等待，也不能把外层其他 domain 的普通 `T!&` 带入 `D`。阻塞只保证
caller 等待 block 结束，不给普通引用增加跨 domain 同步语义。

## Future 与结构化并发

普通 async 调用表示 suspend call，不创建 future，也不返回 future。调用点挂起当前 coroutine，
callee 完成后继续执行：

```jiang
async Int load_page();

async Int render() {
    Int value = load_page(); // 隐式 await，返回 Int
    value + 1
}
```

需要并发启动而不等待结果时，使用 `async call` 或 async block：

```jiang
Future<Int> a = async load_a();
Future<Int> b = async load_b();

a.await() + b.await()
```

`async load_a()` 阻止普通 `load_a()` 调用的隐式 await，并返回 body-local `Future<Int>`。
`Future.await()` 消费一个 Future；多个 Future 提前启动后，即使按顺序调用 `await()`，仍保持并发执行：

```jiang
Future<Int> a = async load_a();
Future<Int> b = async load_b();

a.await() + b.await()
```

Jiang 不提供 `await expr` 或隐式多 Future barrier。需要等待多个 Future 时，分别调用 `await()`；已完成
Future 走 ready fast path，未完成 Future 才挂起当前 coroutine。

`async {}` / `async [D] {}` 可创建一个显式 block future，用于需要自定义 future body 或显式
domain 边界的场景：

```jiang
let future = async {
    load_page()
}
```

Jiang 的 future 语义是 eager、single-completion、cached result、single-consumer：

- 创建 future 后立即入队或开始执行，不等到 `await()` 时才启动。
- future body 只执行一次。
- 完成值缓存一次；`await()` 读取缓存结果，不重复执行 body。
- coroutine 是无栈协程；future 保存的是编译器生成的 coroutine frame，不保存独立调用栈。
- 如果 body 返回 `Result<T, E>`，future 缓存的就是这个 `Result<T, E>` 值；Jiang 不引入隐藏
  exception channel。
- future result type 不能是 future；`async { ... }` / `async [D] { ... }` 不允许产生
  nested future。实现上可以用 `@where` 风格的内部 type predicate 禁止 `Future<Future<T>>`。
- `await()` 和 `cancel()` 都消费 Future，只能选择其中一个；Future 被消费后不能再次 await、cancel
  或移动。

因此这里是合法的：

```jiang
let future = async [PageDomain] {
    load_page() // body type: Int
}
```

但这里应诊断：

```jiang
let nested = async [PageDomain] {
    async [PageDomain] {
        load_page()
    }
}
```

Future 只能在 async/sync body 内作为局部不透明值使用。用户可以在局部变量上写
`Future<T>`，但不能显式写 domain 参数。编译器会根据 initializer 把它补全成内部
`Future<T, domain D>`：

```jiang
Future<Int> future = async [PageDomain] { load_page() }; // internal domain: PageDomain
```

如果局部变量没有 initializer，`Future<T>` 的 domain 默认是调用点 `current`。
`Future<T, PageDomain>` 这类显式 domain 参数暂不开放。

Future 不能出现在函数返回值、参数类型、字段类型或 public ABI：

```jiang
Future<Int> make_future(); // error: Future 不能出现在签名中
```

编译器内部用 future kind 保存 result type 和 domain。这个 kind 服务 type check、borrow check、
lowering 和 runtime ABI；用户可见的 `Future<T>` 只是 body-local 标注表面。

Future 的 observer ownership 固定在创建它的 async/sync effect body。0.4.7 禁止 Future 被任何
嵌套的 async/sync block 或 lambda 捕获，不区分是否 move、是否同 domain；普通词法 block 不形成
capture 边界。Future 可以在创建它的 effect body 中等待一个运行于其他 domain 的 task，完成后
caller 仍回到等待方 current domain。跨 domain 返回的 result 必须满足对应的 Sendable 约束。

```jiang
sync [UiDomain] {
    Future<Image> image = async [WorkerPoolDomain] { load_image() };
    Image value = image.await(); // allowed: Future 没有被嵌套 effect block 捕获
}
```

```jiang
Future<Image> image = async [WorkerPoolDomain] { load_image() };
async [UiDomain] {
    image.await() // error: future_capture_not_allowed
}
```

普通 `T!&` 不能被捕获到不同 domain。`async [D] {}` 是严格 domain 切换边界：

```jiang
sync [UiDomain] {
    async [WorkerPoolDomain] {
        update_worker(model) // error: model 属于 UiDomain，不能进入 WorkerPoolDomain future
    }
}
```

同 domain 的 future creation 仍需要生命周期约束。如果 future 可能在当前函数返回后运行，
不能捕获栈上 borrow。
结构化并发可以允许有限的同 domain capture，但必须证明所有 future 在 scope 结束前完成。

0.4.6 不提供 cancellation。0.4.7 采用显式、协作式 cancellation：`future.cancel()` 消费 Future，
向 task 的 execution domain 请求取消，并挂起 caller，直到 task 已正常完成或完成 cancellation
unwind。取消请求线程不能直接析构 frame；resume 入口和 suspend boundary 检查请求，frame、capture
和跨 suspend local 沿正常 drop state 析构。若 completion 先发生，`cancel()` 丢弃已完成 result
后返回；cancellation 不进入 async 函数的普通返回类型。

`cancel()` 返回时保证 task 不会再执行用户代码，相关 frame、capture 和未消费 result 已完成
清理。Future 在未 `await()` 或 `cancel()` 时离开词法作用域仍表示 detach，不请求取消，task 继续
执行；task 完成后由 task/observer ownership 协议完成最终释放。若以后需要只发请求而不等待，
应使用不同的显式操作，不能削弱 `cancel()` 的终态确认语义。

跨 domain 共享可变状态不使用普通 `T!&` 表达。需要共享时使用 `Mutex<T>`、`Atomic<T>`、
`Channel<T>`、actor 消息或 move/copy 结果；需要底层逃逸时显式进入 `unsafe`。

## Actor 与 Isolate

`actor` 不替代 `domain`。它可以作为 domain 之上的对象模型：

- actor 拥有自己的状态。
- 外部不能直接拿到 actor state 的 `T!&`。
- 外部只能发消息或调用 async 方法。
- actor 内部方法在该 actor 的 serial domain 上串行执行。

初版不建议把 actor 纳入核心 domain 语法，因为每个 actor instance 的 domain 往往是运行时值，
而当前 `async [D]` / `sync [D]` block 草案要求 `D` 是编译期 domain type。可以先用库层
`Actor<T>` 封装状态；
未来如果需要语言级 actor，再单独设计 `self domain` 或 instance domain。

`isolate` 更适合表示隔离状态、独立 heap 或 actor-like container，不适合替代 `domain`。
本文中的主概念是 execution domain。

## `T!&` 作为字段

初版禁止 `T!&` 作为普通持久字段：

```jiang
struct Holder {
    Int!& value; // error
}
```

同时禁止把 `T!&` 存入 tuple、enum payload、generic container、heap/global storage 等持久位置：

```jiang
(Int!&, Int)  // error
Option<Int!&> // error
Vector<Int!&> // error
```

`T!&` 只允许作为参数、局部绑定、返回的短生命周期 borrow projection 和临时表达式。
这样可以避免普通 aggregate 自身也必须携带 domain/lifetime/capability 的复杂度。

闭包 env 是例外但必须受控：

- 非逃逸 `Fn` 可以捕获 `T!&`，因为 env 是编译器管理的 borrow frame。
- 逃逸 `Fn^` 不能捕获普通 `T!&`，除非后续引入明确的 domain-bound owner 机制。

## 与 Effect 的关系

数据竞争不通过 `@effect(write)` / `@effect(set)` 表达。`effect` 更适合表达调用上下文：

- `unsafe`：调用需要 unsafe context。
- `async`：调用可能挂起。
- `sync`：block 在某个 domain 同步执行；0.4.6 不保留函数前 `sync` 修饰符。
- `io`、`atomic`、`alloc` 等未来可作为独立 effect 讨论。

普通 mutable access 由 serial domain 和 domain 切换检查保证。这样 `Fn`、capture list 和函数参数
不需要额外写 `@effect(write)`。需要 noalias 时在参数位置写 `unique`。

## 与 Rust 的差异

Rust 主要靠 `&mut T` 独占、`Send` / `Sync` trait、executor API 约束来防数据竞争。Rust 没有显式
`async [domain]`；future 被哪个 executor poll，就在哪执行。

Jiang 的草案不同：

- `T!&` 不是全局唯一可变引用。
- 同一 serial domain 内允许多个 `T!&` alias。
- 跨 domain 使用 `T!&` 被禁止。
- `unique` 是用户显式声明的独占能力，用于结构性失效、noalias 或特殊 API 约束。

这使 Jiang 更接近 domain/dispatcher/actor 风格的并发模型，但仍希望保持静态检查和
零额外运行时 borrow
开销。

## 未决问题

- `async [D]` / `sync [D]` block 的 parser 表达和错误信息如何设计。
- `D` 0.4.6 必须是 domain type；未来是否允许 dependent / instance domain。
- `T&` 跨 domain 的 shareable 规则如何表达，是否需要公开 `Send` / `Sync` 等 trait 名称。
- `Fn<async [D] ...>` 的 coroutine context ABI、闭包捕获和错误信息如何设计。
- `T!&` 返回值的生命周期和 domain 如何在 HIR / type check 中表示。
- 标准库和第三方 runtime 如何声明跨 domain 能力。
- `unique` 和 optimizer noalias 的精确关系。
- `async [D] {}`、结构化并发 scope、detached future 的最终语法。
