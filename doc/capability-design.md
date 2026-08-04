# Capability 设计草案

本文记录 Jiang capability 体系的当前设计草案，并讨论它如何支撑 effect、协程、异步和
数据竞争防护。`T&!` 唯一借用、基础 domain 检查和 Task 生命周期规则已有实现；更完整的
跨 domain shareability、同步类型和结构化并发仍是设计方向，不应把整篇都视为已实现规范。
设计目标是：尽量把数据竞争检查压到编译期完成，不为普通可变访问插入锁、atomic
或动态 borrow flag。

## 目标

- `async` 类似 Kotlin `suspend`：调用 async 函数表示可能挂起，不要求显式 `await`。
- 普通函数不绑定 execution domain；它只在调用者当前同步控制流中直接执行。
- `T&!` 表示唯一可变引用；其存活期间不能存在指向重叠 place 的其他共享或可变引用。
- domain 切换检查叠加在唯一借用之上：普通 `T&!` 不能跨 serial domain 逃逸。
- `Atomic`、`Mutex` 等同步类型是跨 domain 共享可变状态的显式入口；Channel、Actor 留给后续版本。
- 唯一性属于 `T&!` 类型并进入函数签名；参数位置不使用额外的能力修饰关键字。

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
才能用于放宽 `T&!` alias 和跨挂起点规则。

## Domain 能力

语言核心只认识抽象 capability，并提供 `main_domain`、`global_domain` 两个标准 binding；
UI event loop、专用线程池等具体 Domain 仍由库和应用定义。0.5.0 的 `Domain` trait 同时声明
语言级执行语义和运行时 Executor 类型：

```jiang
public enum DomainKind {
    serial,
    concurrent,
}

public trait Domain {
    associated kind: const DomainKind;
    associated ExecutorType: Executor;

    ExecutorType make_executor(Self& self);
}

```

Domain identity 由命名 `const` binding 表示；同一类型的两个 binding 仍是不同 Domain：

```jiang
public struct UiDomain: Domain<kind = .serial> {
    associated ExecutorType = SerialExecutor;

    SerialExecutor make_executor(Self& self) {
        SerialExecutor()
    }
}

public struct WorkerPoolDomain: Domain<kind = .concurrent> {
    associated ExecutorType = ConcurrentExecutor;

    ConcurrentExecutor make_executor(Self& self) {
        ConcurrentExecutor()
    }
}

public const UiDomain ui_domain = UiDomain();
public const WorkerPoolDomain worker_pool_domain = WorkerPoolDomain();
```

因此：

- `current` 是语言内建特殊值，表示继承当前 async/sync domain context。
- `ui_domain`、`worker_pool_domain` 等不是语言魔法；它们是 import 后可见的命名 `const`。
- `async [domain]` / `sync [domain]` 中的 `domain` 必须是实现 `Domain` 的 canonical const
  binding；编译器通过 `Domain.kind` 读取 serial/concurrent 语义。

`Domain.kind` 描述语言级串行性，`ExecutorType` 和 `make_executor` 选择排队策略。用户 Executor
只处理一次性的 `ExecutorJob`，不接触 coroutine frame、TaskState 或 continuation ABI。
runtime 即使面对自定义 Executor 也继续维护 Domain identity、serial gate 和 Task 生命周期。

`Domain.kind == .serial` 保证同一 domain 上的 continuation 不会并发执行；`Domain.kind ==
.concurrent` 允许同一 domain 内多个任务并发执行，因此普通 `T&!` 不能依赖它保证安全。在
concurrent domain 中共享可变状态必须使用 `Mutex`、`Atomic` 或等价同步
capability。

## 语法草案

函数声明只使用 `async` 表示 suspend function。函数前不再使用 `sync` 关键字；普通函数就是
同步函数。`async` 函数可以不带 domain，也可以在定义时指定 domain：

```jiang
async Int load_data();

async [ui_domain] () render(Model&! model) {
    model.loading = true;
    load_data();
    model.loading = false;
}

() inc(Int&! value) {
    value = value + 1;
}
```

不带 domain 的 `async` 函数是 domain-neutral suspend function。它本身不选择 executor，只能
在已有 current domain 的上下文中调用：

```jiang
sync [ui_domain] {
    Int value = load_data(); // 隐式 await，在 ui_domain current domain 上运行
}
```

带 domain 的 `async [D]` 函数是 domain-bound suspend function。调用它表示进入 `D` 执行
callee body，参数按跨入 `D` 的规则检查；返回后 caller continuation 回到原 current domain。
因此这类函数可以避免整个 body 再包一层 `sync [D] {}`。

`Task` initializer 与 `sync` block 可以带 domain。最外层进入异步运行时必须显式写 domain：

```jiang
import app_runtime;

sync [app_runtime.ui_domain] {
    load_data();
}
```

Task initializer 使用命名参数选择 execution domain：

```jiang
Task(domain: app_runtime.ui_domain) {
    load_data();
};
```

只有外层已有 current domain 时，内层 `Task { ... }` / `sync { ... }` 才能省略 domain，
并继承 current：

```jiang
sync [ui_domain] {
    Task<Int> a = Task { load_data() }; // 继承 ui_domain
    Task<Int> b = Task { load_data() }; // 继承 ui_domain
    a.await() + b.await()
}
```

普通函数中直接写无 domain 的 `sync {}` 或 `Task { ... }` 应诊断，因为普通函数没有 current domain。
Task initializer 当前只公开 `domain` 命名参数；runtime context 不是用户语法。

函数类型沿用第一个类型参数作为 callable signature head：

```jiang
Fn<async Int>
Fn<async [ui_domain] Int, Model&!>
RawFn<unsafe async [ui_domain] Result<Int, Error>, Void*>
```

在 callable type 中，`async [D]` 修饰 callable signature，不修饰返回值类型本身，也不表示
异步地产生一个 `Fn` 值。`Fn<async [D] R, Args...>` 表示调用该 callable 时进入 `D` 并最终
得到 `R`。

lambda 的 effect 完全由 expected callable type 决定，表达式本身不重复书写 `async`：

```jiang
Fn<async [ui_domain] Int, Int> load = { id => fetch(id) };
```

async `Fn` / `RawFn` 动态调用遵守与直接 async 调用相同的 Domain 切换和 Sendable 检查。

`async [ui_domain]` / `sync [ui_domain]` 是 effect keyword option 中
`async [domain: ui_domain]` / `sync [domain: ui_domain]` 的短写。
函数声明和 callable type 只使用 canonical const Domain binding；带 `context` 的形式只用于 block。

## 默认 Domain

普通函数没有默认 domain，也不是隐式 `sync [current]`：

```jiang
() inc(Int&! value) {
    value = value + 1;
}
```

它可以被任意调用者同步调用，执行在调用者当前控制流中。普通函数不能直接建立或等待
Task。如果普通函数要进入异步运行时，必须显式写外层 domain：

```jiang
Int main() {
    sync [ui_domain] {
        load_data()
    }
}
```

在已有 current domain 的 Task/sync block 或 async 调用链中，普通函数调用不切 domain，
只是当前 coroutine 内的同步片段。普通函数没有 hidden domain/context 参数。

domain-neutral `async` 函数不绑定 domain；调用点必须已经有 current domain，coroutine frame
也在该 current domain 下执行和恢复。实现上 async frame 可以携带隐藏的 coroutine
context/domain handle，但这个上下文不进入普通参数列表。

domain-bound `async [D]` 函数在签名 metadata 中保存 Domain binding，调用点按进入 `D` 的规则
检查参数和返回 continuation。它不是普通 `async` 函数的默认形态，而是给“整个函数必须在
某个 domain 执行”的 API 使用。

## `T&!` 的唯一借用与 Domain 语义

`T&!` 是 Rust 式唯一可变引用。它可以被顺序 reborrow，但不能与指向同一 place 的共享引用
或另一个可变引用同时存活：

```jiang
() bump(Int&! value) {
    value = value + 1;
}
```

函数参数直接用 `T&!` 表达这一能力：

```jiang
() swap(Int&! left, Int&! right) {
}
```

编译器在创建借用、调用和 reborrow 时检查冲突，并沿封装边界要求调用者同样提供 `T&!`。
是否向优化器发出 noalias 仍需服从完整 lifetime 与 ABI 规则。

## Async 调用与隐式 Await

Jiang 不需要显式 `await`。调用 async 函数就是挂起点：

```jiang
async Int fetch();

async () refresh(Model&! model) {
    model.loading = true;
    Int value = fetch(); // 可能挂起，默认保持 ui_domain
    model.value = value;
}
```

`refresh` 本身不选择 domain。调用者需要在某个 current domain 中运行它：

```jiang
sync [ui_domain] {
    refresh(model$.mut_ref())
}
```

如果需要在其他 domain 启动工作，应使用显式 domain block。普通 mutable ref 不能跨到其他
domain：

```jiang
sync [ui_domain] {
    Task(domain: worker_pool_domain) {
        update_worker(model) // error: model 属于 ui_domain，不能进入 worker_pool_domain
    };
}
```

## Resume 与运行时开销

async coroutine frame 在创建它的 current domain 上执行和恢复：

```jiang
async () foo(T&! x) {
    x = ...
    bar(); // 默认 await
    x = ... // 仍在同一个 current domain 恢复
}
```

实现上：

- 同 domain await：不需要 domain hop，只是普通 coroutine resume。
- `task.await()` 在 waiter 的 current domain 中挂起；Task 可以在其他 domain 执行。
- 跨 domain completion 将 waiter continuation enqueue 回等待方 domain，再从原 current domain 恢复。

静态 capability 检查本身应当是零运行时开销。运行时开销来自实际的 enqueue、suspend/resume
和 Task 等待。

## Domain 切换规则

数据竞争检查主要发生在 domain 切换点。初步规则：

- 普通函数调用不切 domain，callee 只是 caller 当前控制流里的同步片段。
- domain-neutral async 函数调用不切 domain；callee 在 caller 的 current domain 中挂起/恢复。
- domain-bound `async [D]` 函数调用进入 `D`；参数必须能安全进入 `D`，返回后 caller 回到原
  current domain。
- `Task { ... }` / `sync { ... }` 只有外层已有 current domain 时可省略 domain，并继承 current。
- `Task(domain: D) { ... }` 创建 task，是显式 domain 入口；如果 Domain binding `D` 不等于 current，
  则是 domain 切换边界。
- 普通同步函数中的最外层 `sync [D] {}` 阻塞调用线程，进入 runtime 并等待 block 完成。
- async context 中的 `sync [D] {}` 是结构化 domain switch：挂起当前 coroutine，在 `D` 执行
  block，完成后回到进入前的 domain；它不创建用户可见 Task，也不阻塞 worker thread。
- `task.await()` 不把 caller 留在 Task 的 execution domain；完成后 caller 回到等待方 current domain。

async context 中若能静态证明 `D` 与 current domain 相同，`sync [D]` 直接执行 block；只有跨 Domain
时才 enqueue 和挂起。domain-neutral async 函数也允许使用静态 `sync [D]`；当前可以保守 enqueue，
后续再根据 frame 中的 current domain 增加运行时同 Domain fast path。

跨 domain 时的能力检查：

- `T&!`：只能留在同一个 serial domain 内，不能传给或捕获到其他 domain。
- `T&`：跨 domain 需要 `T` 是可共享的不可变/同步安全类型。
- `T^`：可以 move 到另一个 domain，前提是 `T` 可安全跨 domain 移动。
- `Fn` / `Fn^`：根据参数类型和捕获 environment 判断是否能跨 domain。
- `T*` / `T*!`：跨 domain 需要 `unsafe` 边界。
- `Atomic<T>` / `Mutex<T>`：由标准库或 runtime 声明为同步安全入口。

`sync [D] {}` 即使结构化等待 block 完成，也不能把外层其他 domain 的普通 `T&!` 带入 `D`。
等待不为普通引用增加跨 domain 同步语义。

## Task 与结构化并发

普通 async 调用表示 suspend call，不创建 task，也不返回 task。调用点挂起当前 coroutine，
callee 完成后继续执行：

```jiang
async Int load_page();

async Int render() {
    Int value = load_page(); // 隐式 await，返回 Int
    value + 1
}
```

需要并发启动而不等待结果时，使用 Task initializer：

```jiang
Task<Int> a = Task { load_a() };
Task<Int> b = Task { load_b() };

a.await() + b.await()
```

Task closure 内的 `load_a()` 是普通隐式 await；initializer 立即启动该 closure，并返回地址稳定的
`Task<Int>`。
`Task.await()` 消费一个 Task；多个 Task 提前启动后，即使按顺序调用 `await()`，仍保持并发执行：

```jiang
Task<Int> a = Task { load_a() };
Task<Int> b = Task { load_b() };

a.await() + b.await()
```

Jiang 不提供 `await expr` 或隐式多 Task barrier。需要等待多个 Task 时，分别调用 `await()`；已完成
Task 走 ready fast path，未完成 Task 才挂起当前 coroutine。

`Task { ... }` / `Task(domain: D) { ... }` 创建显式 task，用于自定义 task body 或显式
domain 边界：

```jiang
Task<Int> task = Task {
    load_page()
};
```

Jiang 的 task 语义是 eager、single-completion、cached result、single-consumer：

- 创建 task 后立即入队或开始执行，不等到 `await()` 时才启动。
- task body 只执行一次。
- 完成值缓存一次；`await()` 读取缓存结果，不重复执行 body。
- coroutine 是无栈协程；task 保存的是编译器生成的 coroutine frame，不保存独立调用栈。
- 如果 body 返回 `Result<T, E>`，task 缓存的就是这个 `Result<T, E>` 值；Jiang 不引入隐藏
  exception channel。
- task result type 不能是 task；`Task { ... }` / `Task(domain: D) { ... }` 不允许产生
  nested task。实现上可以用 `@where` 风格的内部 type predicate 禁止 `Task<Task<T>>`。
- `await()` 和 `cancel_and_await()` 消费一次 result；第二个消费位置会被诊断。
- `cancel()` 只同步、幂等地发布请求，不消费 result，也不等待 Task 退出；取消后仍可 `await()`。

因此这里是合法的：

```jiang
Task<Int> task = Task(domain: PageDomain) {
    load_page() // body type: Int
};
```

但这里应诊断：

```jiang
Task<Task<Int>> nested = Task(domain: PageDomain) {
    Task(domain: PageDomain) {
        load_page()
    }
};
```

用户可以在局部变量或静态可寻址的聚合字段中写直接 `Task<T>`，也可以用 `new Task` 创建
`Task<T>^` owner。Task 的
公开类型不包含 domain 参数；domain 只保存在创建点和 runtime state 中：

```jiang
Task<Int> task = Task(domain: PageDomain) { load_page() };
Task<Int>^ owned = new Task(domain: PageDomain) { load_page() };
```

直接 `Task<T>` 是 `!Movable`，包含它的聚合值也不能移动、按值传参或返回。直接 Task 字段目前只支持
struct、tuple 和固定数组中的静态位置，不支持 optional/error union/union 等动态变体。
`Task<T>^` 是 Movable、非 Copyable 的一等
owner，可以出现在参数、返回值、字段、容器、泛型实例和 public ABI：

```jiang
Task<Int>^ make_task() {
    new Task(domain: PageDomain) { load_page() }
}
```

编译器内部 task kind 只保存 result type；domain 不进入公开类型身份。TaskState 直接内联为 Task 的
唯一字段，`Task<T>^` 只是 heap Task 的 owner pointer，不指向第二个 control block。

直接 Task 的 observer ownership 固定在创建它的 async/sync effect body；直接 binding 或包含它的聚合值
不能被嵌套的 async/sync
block 或 lambda 捕获。`Task<T>^` owner 可以 move 进普通 callable 或其他存储，并遵守普通 lifetime
与跨 Domain Sendable 规则。Task 可以运行于不同 domain，完成后 caller 仍回到等待方 current
domain。跨 domain 返回的 result 必须满足对应的 Sendable 约束。

```jiang
sync [ui_domain] {
    Task<Image> image = Task(domain: worker_pool_domain) { load_image() };
    Image value = image.await(); // allowed: Task 没有被嵌套 effect block 捕获
}
```

```jiang
Task<Image> image = Task(domain: worker_pool_domain) { load_image() };
Task(domain: ui_domain) {
    image.await() // error: task_capture_not_allowed
};
```

普通 `T&!` 不能被捕获到不同 domain。`Task(domain: D) { ... }` 是严格 domain 切换边界：

```jiang
sync [ui_domain] {
    Task(domain: worker_pool_domain) {
        update_worker(model) // error: model 属于 ui_domain，不能进入 worker_pool_domain task
    };
}
```

同 domain 的 task creation 仍需要生命周期约束。如果 task 可能在当前函数返回后运行，
不能捕获栈上 borrow。
结构化并发可以允许有限的同 domain capture，但必须证明所有 task 在 scope 结束前完成。

当前采用显式、协作式 cancellation：`task.cancel()` 向 task 的 execution Domain 原子发布请求后
立即返回；`task.cancel_and_await()` 才等待 task 正常完成或完成 cancellation unwind。取消请求线程
不能直接析构 frame；resume 入口和 suspend boundary 检查请求，frame、capture 和跨 suspend local
沿正常 drop state 析构。长时间不挂起的计算可以调用 `coroutine.check_cancelled()` 建立显式检查点；
没有请求时它立即返回，有请求时直接进入当前 coroutine cleanup。cancellation 不进入 async 函数的
普通返回类型。

`await()` 遇到因取消结束、没有 result 的 child 时，当前 parent 也进入 cancellation cleanup；其余未完成
sibling 会先全部收到取消请求，再被逐个等待。`cancel_and_await()` 则只请求并消费指定 Task，不会取消
caller，因此适合“停止这一项工作后继续执行”的路径。

直接 Task 是结构化子任务：未 `await()` 或 `cancel_and_await()` 的 Task 离开作用域时，
编译器先向同一退出路径上的全部活跃 Task 请求取消，再逐个等待终态。需要 detached 执行时使用
不形成 Task handle 的 standalone Task initializer；直接 Task 固定在创建它的 place 上，不支持
`move()`、`forget()` 或重新赋值。

当前实现在 coroutine entry 和自然 resume boundary 检查取消。挂起时不会从请求线程抢占
frame。
parent 挂起于显式 `child.await()` 时会把取消单向传播给 child，并等待 child terminal acknowledgement
后再 unwind。隐式 async call 的 child 继承根 Task cancellation context，在自然恢复边界先观察请求并
unwind，再恢复 parent；根 Task 负责唯一的 request claim。

底层 async API 使用 `core/coroutine` 的主动 suspend 原语桥接 callback：

```jiang
async Response send(Request& request) {
    coroutine.suspend { continuation =>
        Operation operation = request.start { response =>
            continuation.resume(response);
        };
        continuation.on_cancel {
            [operation] =>
            operation.cancel();
        };
    }
}
```

`suspend` 的 registration 同步执行；若 Task 已取消，则不启动 registration 中的外部操作。registration
期间发生取消时，`on_cancel` 必须立即或在所属 Domain 上执行 handler；completion 与 cancel 竞争只能有
一方取得 continuation 的 terminal resume 权。取消 handler 用于尽快中断网络请求等外部操作，原
completion/cancel acknowledgement 仍负责确认外部系统不再持有 continuation，之后 coroutine frame
才能进入 unwind。当前不提供类型级 `Cancellable` trait；每个 suspend operation 独立注册 handler，
避免强迫一个类型的多个 async 方法共享一个 cancel 方法。

跨 Domain 共享可变状态不使用普通 `T&!` 表达。需要共享时使用 `Mutex<T>`、`Atomic<T>` 或
move/copy 结果；Channel 和 actor 消息留给后续版本，需要底层逃逸时显式进入 `unsafe`。

## Actor 与 Isolate

`actor` 不替代 `domain`。它可以作为 domain 之上的对象模型：

- actor 拥有自己的状态。
- 外部不能直接拿到 actor state 的 `T&!`。
- 外部只能发消息或调用 async 方法。
- actor 内部方法在该 actor 的 serial domain 上串行执行。

初版不建议把 actor 纳入核心 domain 语法，因为每个 actor instance 的 domain 往往是运行时值，
而当前 `async [D]` / `sync [D]` block 要求 `D` 是 canonical const Domain binding。可以先用库层
`Actor<T>` 封装状态；
未来如果需要语言级 actor，再单独设计 `self domain` 或 instance domain。

`isolate` 更适合表示隔离状态、独立 heap 或 actor-like container，不适合替代 `domain`。
本文中的主概念是 execution domain。

## `T&!` 作为字段

`T&!` 可以作为字段或 aggregate payload，但 aggregate 的生命周期不能超过借用来源，且该字段
存活期间持续占用来源 place 的唯一可变借用：

```jiang
struct Holder {
    Int&! value;
}
```

tuple、enum payload 和 generic container 遵循同一条 lifetime/loan 规则。不能把局部来源的
`T&!` 逃逸到更长寿命的 heap/global storage：

```jiang
(Int&!, Int)
Option<Int&!>
Vector<Int&!>
```

闭包 env 同样受这些规则约束：

- 非逃逸 `Fn` 可以捕获 `T&!`，因为 env 是编译器管理的 borrow frame。
- 逃逸 `Fn^` 不能捕获普通 `T&!`，除非后续引入明确的 domain-bound owner 机制。

## 与 Effect 的关系

数据竞争不通过 `@effect(write)` / `@effect(set)` 表达。`effect` 更适合表达调用上下文：

- `unsafe`：调用需要 unsafe context。
- `async`：调用可能挂起。
- `sync`：block 在某个 domain 同步执行；函数前不保留 `sync` 修饰符。
- `io`、`atomic`、`alloc` 等未来可作为独立 effect 讨论。

普通 mutable access 由 `T&!` 唯一借用、serial domain 和 domain 切换检查共同保证。这样 `Fn`、
capture list 和函数参数不需要额外写 `@effect(write)`。

## 与 Rust 的差异

Rust 主要靠 `&mut T` 独占、`Send` / `Sync` trait、executor API 约束来防数据竞争。Rust 没有显式
`async [domain]`；task 被哪个 executor poll，就在哪执行。

Jiang 的 `T&!` 与 Rust `&mut T` 一样表达唯一可变借用；Jiang 还叠加显式 domain 规则，
禁止普通 `T&!` 跨不兼容 domain 使用。

这使 Jiang 更接近 domain/dispatcher/actor 风格的并发模型，但仍希望保持静态检查和
零额外运行时 borrow
开销。

## 未决问题

- `D` 当前必须是 canonical const Domain binding；未来是否允许 dependent / instance Domain。
- `T&` 跨 domain 的 shareable 规则如何表达，是否需要公开 `Send` / `Sync` 等 trait 名称。
- 复杂循环和跨 suspend reborrow 的诊断如何进一步收紧。
- 标准库和第三方 runtime 如何声明跨 domain 能力。
- `T&!` 与 optimizer noalias 的精确关系。
- 动态 task group、失败聚合和显式 detached handle 是否需要独立类型。
