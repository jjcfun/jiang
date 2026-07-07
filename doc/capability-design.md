# Capability 设计草案

本文记录 Jiang capability 体系的当前设计草案，并讨论它如何支撑 effect、协程、异步和
数据竞争防护。这里描述的是方向，不是已实现规范。设计目标是：尽量把数据竞争检查
压到编译期完成，不为普通可变访问插入锁、atomic 或动态 borrow flag。

## 目标

- `async` 类似 Kotlin `suspend`：调用 async 函数表示可能挂起，不要求显式 `await`。
- 所有普通函数默认运行在一个隐式 serial token 下，token 从调用上下文推导并传递。
- `T!&` 表示当前 serial token 下的可变引用；同一 serial token 内允许多个 `T!&` alias。
- 数据竞争检查转换为 token 切换检查：普通 `T!&` 不能跨 serial token 逃逸。
- `Atomic`、`Mutex`、`Channel` 等同步类型是跨 token 共享可变状态的显式入口。
- `unique` 不作为普通 `T!&` 的默认语义；它是参数位置的额外能力，用于 noalias、move/drop
  协议或特殊 API 约束。

本文的主体是 capability。`effect` 用来描述调用上下文，例如 `unsafe`、`async`、`io`；
data race 本身不靠 `write` / `set` effect，而靠 serial token 与 capability 检查。

## 术语

- **serial token**：一个串行 isolation domain。同一个 serial token 上的 continuation 不会并发执行。
- **current token**：当前函数体运行所在的 serial token。
- **token-polymorphic function**：没有显式 token 的普通函数。它继承调用者的 current token。
- **token switch**：从一个 serial token 进入另一个 serial token 的执行上下文。

普通 executor 或线程池不一定是 serial token。只有能保证同一 token 上代码串行执行的 token，
才能用于放宽 `T!&` alias 和跨挂起点规则。

## 语法草案

`async` 函数可以带 token：

```jiang
async [ui] () render(Model!& model) {
    model.loading = true;
    load_data();
    model.loading = false;
}
```

不带 token 的 `async` 默认继承 current token：

```jiang
async Int load_data();
```

函数类型沿用第一个类型参数作为 callable signature head：

```jiang
Fn<async Int>
Fn<async [ui] (), Model!&>
RawFn<unsafe async [ui] (), UInt8*>
```

为了避免和运行时参数混淆，token 语法使用 `async [token]`，不使用 `async(token)`。
按照 Jiang keyword options 的代码风格，关键字和 options 之间建议保留空格；parser 后续可
决定是否兼容 `async[token]` 这种紧凑写法。

## 普通函数的隐式 token

所有普通函数默认带一个从调用上下文推导的 serial token：

```jiang
() inc(Int!& value) {
    value = value + 1;
}
```

语义上可以理解为：

```text
for serial token K:
sync[K] () inc(Int![K]& value)
```

但用户不需要写 `K`。因此同一个同步函数可以在不同 token 下调用：

```jiang
async [ui] () update_ui(Int!& value) {
    inc(value); // inc 在 ui token 下运行
}

async [worker] () update_worker(Int!& value) {
    inc(value); // inc 在 worker token 下运行
}
```

## `T!&` 的 token 语义

`T!&` 是 current serial token 下的可变引用。同一 serial token 内允许多个 `T!&` alias：

```jiang
() bump_twice(Int!& value) {
    Int!& again = value$.ref();
    value = value + 1;
    again = again + 1;
}
```

这不是 Rust `&mut` 的唯一引用模型。安全性来自 serial token 的串行执行，而不是来自
全局唯一。

因此后端和优化不能默认把 `T!&` 当作 noalias。需要 noalias 或严格独占时，在参数位置写
`unique`：

```jiang
() swap(unique Int!& left, unique Int!& right) {
}
```

`unique` 是调用点能力要求，不是字段或局部变量的常驻类型属性。

## Async 调用与隐式 await

Jiang 不需要显式 `await`。调用 async 函数就是挂起点：

```jiang
async Int fetch();

async [ui] () refresh(Model!& model) {
    model.loading = true;
    Int value = fetch(); // 可能挂起，默认保持 ui token
    model.value = value;
}
```

如果 callee 没有显式 token，它继承 caller 的 current token。显式 token 的 callee 在自己的
token 下运行，但调用返回后 caller continuation 仍回到 caller token。也就是说：

```jiang
async [worker] Int compute();

async [ui] () refresh(Model!& model) {
    Int value = compute(); // compute 在 worker token 下运行
    model.value = value;   // 返回后仍在 ui token 下
}
```

但是不能把 `ui` token 下的普通 mutable ref 传给 `worker` token 函数：

```jiang
async [worker] () compute_in_place(Model!& model);

async [ui] () refresh(Model!& model) {
    compute_in_place(model); // error: Model!& 属于 ui token，不能传给 worker token
}
```

## Token 切换规则

数据竞争检查主要发生在 token 切换点。初步规则：

- 普通函数调用不切 token，callee 继承 caller 的 current token。
- 无显式 token 的 async 调用不切 token，callee 继承 caller 的 current token。
- `async [token]` callee 在 `token` 下运行，参数必须能安全进入该 token。
- `spawn[token]` / `launch[token]` 创建并发任务，是严格 token 切换边界。
- 未来如果引入 `switch[token]` / `run[token]` block，进入 block 时不能携带原 token 的普通
  `T!&`。

跨 token 时的能力检查：

- `T!&`：只能留在同一个 serial token 内，不能传给或捕获到其他 token。
- `T&`：跨 token 需要 `T` 是可共享的不可变/同步安全类型。
- `T^`：可以 move 到另一个 token，前提是 `T` 可安全跨 token 移动。
- `Fn` / `Fn^`：根据参数类型和捕获 environment 判断是否能跨 token。
- `T*` / `T[*]`：跨 token 需要 `unsafe` 边界。
- `Atomic<T>` / `Mutex<T>` / `Channel<T>`：由标准库声明为同步安全入口。

## Spawn 与结构化并发

`spawn[token]` 或类似 API 会创建可能并发执行的任务。普通 `T!&` 不能被捕获到不同 token：

```jiang
async [ui] () render(Model!& model) {
    spawn[worker] {
        model.value = 1; // error: model 属于 ui token
    }
}
```

同 token 的 spawn 仍需要生命周期约束。如果任务可能在当前函数返回后运行，不能捕获栈上
borrow。结构化并发可以允许有限的同 token capture，但必须证明所有任务在 scope 结束前完成。

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
这样可以避免普通 aggregate 自身也必须携带 token/lifetime/capability 的复杂度。

闭包 env 是例外但必须受控：

- 非逃逸 `Fn` 可以捕获 `T!&`，因为 env 是编译器管理的 borrow frame。
- 逃逸 `Fn^` 不能捕获普通 `T!&`，除非后续引入明确的 token-bound owner 机制。

## SerialToken 能力

只有 serial token 能证明同 token 内无并发。可以用内建或标准库 trait 表示：

```jiang
trait ExecutorToken;
trait SerialToken: ExecutorToken;
```

`SerialToken` 的实现是调度语义承诺。用户自定义 executor 如果要实现 `SerialToken`，应视为
`unsafe` promise，因为编译器无法自动证明其调度器真的串行。

## 与 effect 的关系

数据竞争不通过 `@effect(write)` / `@effect(set)` 表达。`effect` 更适合表达调用上下文：

- `unsafe`：调用需要 unsafe context。
- `async`：调用可能挂起。
- `io`、`atomic`、`alloc` 等未来可作为独立 effect 讨论。

普通 mutable access 由 serial token 和 token 切换检查保证。这样 `Fn`、capture list 和函数参数
不需要额外写 `@effect(write)`。需要 noalias 时在参数位置写 `unique`。

## 未决问题

- 是否需要显式 `switch[token]` / `run[token]` 语法，以及它们是否保证返回 caller token。
- `async [token]` 的 token 是否允许普通值参数，还是初版只允许内建 token / actor self。
- `T&` 跨 token 的 shareable 规则如何表达，是否需要公开 `Send` / `Sync` 等 trait 名称。
- `Fn<async [token] ...>` 的 parser 表达和错误信息如何设计。
- `T!&` 返回值的生命周期和 token 如何在 HIR / type check 中表示。
- 标准库同步类型如何声明跨 token 能力。
- `unique` 和 optimizer noalias 的精确关系。
