# Capability 设计草案

本文记录 Jiang capability 体系的当前设计草案，并讨论它如何支撑 effect、协程、异步和
数据竞争防护。这里描述的是方向，不是已实现规范。
设计目标是：尽量把数据竞争检查压到编译期完成，不为普通可变访问插入锁、atomic
或动态 borrow flag。

## 目标

- `async` 类似 Kotlin `suspend`：调用 async 函数表示可能挂起，不要求显式 `await`。
- 普通函数默认运行在调用者当前 execution domain 中。
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
- **current domain**：当前函数体运行所在的 domain。
- **domain-polymorphic function**：没有显式 domain 的函数。它继承调用者的 current domain。
- **domain switch / hop**：从一个 domain 进入另一个 domain 的执行上下文。

普通 executor 或线程池不一定是 serial domain。只有能保证同一 domain 上代码串行执行的 domain，
才能用于放宽 `T!&` alias 和跨挂起点规则。

## Domain 能力

语言核心只认识抽象 capability，不内建 `ui`、`work` 这类具体 domain：

```jiang
trait Domain {
}

trait SerialDomain: Domain {
}
```

具体 domain 由 runtime、框架或第三方库提供：

```jiang
public struct UiDomain: SerialDomain {
}

public const ui = UiDomain();
```

因此：

- `current` 是语言内建特殊值，表示继承调用点当前 domain。
- `ui`、`work`、`io` 等不是语言魔法；它们来自 import 后可见的 const value。
- `async [domain_expr]` / `sync [domain_expr]` 中的 `domain_expr` 必须是编译期 const value，且实现
  `SerialDomain`。

`ui`、`work`、`io` 的调度策略不属于 `std` 的固定职责。`std` 最多提供 `Domain`、`SerialDomain`、
`Send`、`Sync` 或 executor 基础接口；具体事件循环、线程池、UI 主线程由库维护。

## 语法草案

`async` / `sync` 沿用 Jiang keyword options 语法，可以带 domain：

```jiang
async [current] Int load_data();

async [ui] () render(Model!& model) {
    model.loading = true;
    load_data();
    model.loading = false;
}
```

不写 domain 时，默认继承 current domain：

```jiang
async Int load_data(); // 等价于 async [current]
() inc(Int!& value);   // 等价于 sync [current]
```

也可以使用第三方库提供的 const domain value：

```jiang
import app_runtime;

async [app_runtime.ui] () render(Model!& model) {
}
```

如果一个 keyword 后续还有多个 option，可以用带 key 的形式：

```jiang
async [domain: app_runtime.ui] () render(Model!& model) {
}
```

`async` block 还可以带运行时 context：

```jiang
async [page, page_ctx] {
    load(model$.ref());
}

async [domain: page, context: page_ctx] {
    load(model$.ref());
}
```

这里 `page` 是编译期可见的静态 domain tag，用于 data race 检查；`page_ctx` 是运行时
async context，用于调度、取消、join、页面生命周期或 tracing 等 runtime 语义。`context`
不参与 domain 静态相等判断，也不能替代 domain。

函数类型沿用第一个类型参数作为 callable signature head：

```jiang
Fn<async Int>
Fn<async [ui] (), Model!&>
RawFn<unsafe async [ui] (), UInt8*>
```

`async [ui]` 是 `async [domain: ui]` 的短写。`async [ui, ctx]` 是
`async [domain: ui, context: ctx]` 的短写。`ui` 本质上仍是 const domain value，不是语言魔法。

## 默认 Domain

普通函数默认是 `sync [current]`：

```jiang
() inc(Int!& value) {
    value = value + 1;
}
```

语义上可以理解为：

```text
for serial domain D:
sync [D] () inc(Int!& @D value)
```

但用户不需要写 `D`。因此同一个函数可以在不同 domain 下调用：

```jiang
async [ui] () update_ui(Int!& value) {
    inc(value); // inc 在 ui domain 下运行
}

async [worker] () update_worker(Int!& value) {
    inc(value); // inc 在 worker domain 下运行
}
```

`async` 默认是 `async [current]`。它表示 coroutine frame 继承调用点 domain；如果 frame 中有
`T!& @D` 跨挂起点存在，那么每次 resume 都必须回到 `D`。

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

async [ui] () refresh(Model!& model) {
    model.loading = true;
    Int value = fetch(); // 可能挂起，默认保持 ui domain
    model.value = value;
}
```

如果 callee 没有显式 domain，它继承 caller 的 current domain。显式 domain 的 callee 在自己的
domain 下运行，但调用返回后 caller continuation 仍回到 caller domain：

```jiang
async [worker] Int compute();

async [ui] () refresh(Model!& model) {
    Int value = compute(); // compute 在 worker domain 下运行
    model.value = value;   // 返回后仍在 ui domain 下
}
```

但是不能把 `ui` domain 下的普通 mutable ref 传给 `worker` domain 函数：

```jiang
async [worker] () compute_in_place(Model!& model);

async [ui] () refresh(Model!& model) {
    compute_in_place(model); // error: Model!& 属于 ui domain，不能传给 worker domain
}
```

## Resume 与运行时开销

`async [D]` 的 domain 约束作用于整个 coroutine frame，不只作用于第一个挂起点之前：

```jiang
async [current] () foo(T!& x) {
    x = ...
    bar(); // 默认 await
    x = ... // 这里仍必须在同一个 current domain 恢复
}
```

实现上：

- 同 domain await：不需要 domain hop，只是普通 coroutine resume。
- 跨 domain 完成后恢复：需要 post/enqueue 回原 domain。
- 如果 async frame 没有持有 domain-bound `T!&` 跨 await，后续可以允许更自由的 resume / Send future。

因此 domain 机制不是必然比 Rust 更重。只有在 Jiang 为了保持 domain 约束而实际跨
domain 恢复时，
才比 Rust 多一次 hop/post。静态 capability 检查本身应当是零运行时开销。

## Domain 切换规则

数据竞争检查主要发生在 domain 切换点。初步规则：

- 普通函数调用不切 domain，callee 继承 caller 的 current domain。
- 无显式 domain 的 async 调用不切 domain，callee 继承 caller 的 current domain。
- `async [D]` callee 在 `D` 下运行，参数必须能安全进入 `D`。
- `async [D] {}` 创建 future，是严格 domain 切换边界。
- `async [D] {}` 是显式进入 `D` 的 block；进入 block 时不能携带原 domain 的普通 `T!&`。

跨 domain 时的能力检查：

- `T!&`：只能留在同一个 serial domain 内，不能传给或捕获到其他 domain。
- `T&`：跨 domain 需要 `T` 是可共享的不可变/同步安全类型。
- `T^`：可以 move 到另一个 domain，前提是 `T` 可安全跨 domain 移动。
- `Fn` / `Fn^`：根据参数类型和捕获 environment 判断是否能跨 domain。
- `T*` / `T[*]`：跨 domain 需要 `unsafe` 边界。
- `Atomic<T>` / `Mutex<T>` / `Channel<T>`：由标准库或 runtime 声明为同步安全入口。

## Future 与结构化并发

普通 async 调用表示 suspend call，不创建 future，也不返回 future。调用点挂起当前 coroutine，
callee 完成后继续执行：

```jiang
async [page] Int load_page();

async [page] Int render() {
    Int value = load_page(); // 隐式 await，返回 Int
    value + 1
}
```

需要并发启动而不等待结果时，使用 `async [D] {}`。它创建一个 future：

```jiang
let future = async [page] {
    load_page()
}
```

Jiang 的 future 语义是 eager、single-completion、cached result：

- 创建 future 后立即入队或开始执行，不等到 `await` / `join` 时才启动。
- future body 只执行一次。
- 完成值缓存一次；`await` / `join` 读取缓存结果，不重复执行 body。
- coroutine 是无栈协程；future 保存的是编译器生成的 coroutine frame，不保存独立调用栈。
- 如果 body 返回 `Result<T, E>`，future 缓存的就是这个 `Result<T, E>` 值；Jiang 不引入隐藏
  exception channel。
- future result type 不能是 future；`async [D] { ... }` 不允许产生 nested future。实现上可以用
  `@where` 风格的内部 type predicate 禁止 `Future<Future<T>>`。

因此这里是合法的：

```jiang
let future = async [page] {
    load_page() // body type: Int
}
```

但这里应诊断：

```jiang
let nested = async [page] {
    async [page] {
        load_page()
    }
}
```

future result 的消费走 `$` Intrinsic Operation：

```jiang
Int value = future$.join();  // sync context 中阻塞等待
Int next = future$.await();  // async context 中挂起等待
```

Future 只能在 body 内作为局部不透明值使用。用户不能主动写 `Future` 类型，也不能把 future
作为函数返回值、参数类型、字段类型或 public ABI 暴露：

```jiang
Future<Int, page> make_future(); // error: Future 不能出现在签名中
```

编译器内部可以用 future kind 保存 result type 和 domain；如果写 `async { ... }`，domain
默认是调用点 `current`。这个 kind 只服务 type check、borrow check、lowering 和 runtime ABI，
不进入用户可命名类型空间。

普通 `T!&` 不能被捕获到不同 domain。`async [D] {}` 是严格 domain 切换边界：

```jiang
async [ui] () render(Model!& model) {
    async [worker] {
        update_worker(model) // error: model 属于 ui domain，不能进入 worker future
    }
}
```

同 domain 的 future creation 仍需要生命周期约束。如果 future 可能在当前函数返回后运行，
不能捕获栈上 borrow。
结构化并发可以允许有限的同 domain capture，但必须证明所有 future 在 scope 结束前完成。

函数先不支持 future creation Intrinsic Operation。也就是说不提供 `foo$().task()`、
`foo$().future()` 或类似形式；0.4.6 先以 block-form `async [D] {}` 为并发启动入口。

## Actor 与 Isolate

`actor` 不替代 `domain`。它可以作为 domain 之上的对象模型：

- actor 拥有自己的状态。
- 外部不能直接拿到 actor state 的 `T!&`。
- 外部只能发消息或调用 async 方法。
- actor 内部方法在该 actor 的 serial domain 上串行执行。

初版不建议把 actor 纳入核心 domain 语法，因为每个 actor instance 的 domain 往往是运行时值，
而当前 `async [D]` 草案要求 `D` 是编译期 const value。可以先用库层 `Actor<T>` 封装状态；
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
- `sync`：调用在某个 domain 同步执行。
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

- `async [D]` 的 parser 表达和错误信息如何设计。
- `D` 是否必须是 const value，还是未来允许 dependent / instance domain。
- `T&` 跨 domain 的 shareable 规则如何表达，是否需要公开 `Send` / `Sync` 等 trait 名称。
- `Fn<async [D] ...>` 的 parser 表达和错误信息如何设计。
- `T!&` 返回值的生命周期和 domain 如何在 HIR / type check 中表示。
- 标准库和第三方 runtime 如何声明跨 domain 能力。
- `unique` 和 optimizer noalias 的精确关系。
- `async [D] {}`、`go async [D] {}`、结构化并发 scope 的最终语法。
