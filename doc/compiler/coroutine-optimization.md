# Coroutine 极致优化设计

本文固定 Jiang coroutine/Task 的最终优化方向。目标不是减少某一个 benchmark 中的 malloc，
而是在不牺牲生命周期正确性、取消语义和跨 Domain 并发安全的前提下，
让每一种调用形态只承担
它实际需要的成本。

## 当前实现的问题

当前实现已经完成第一轮架构收敛：普通 async 直接调用复用父 continuation，
scoped Task 的 control state 与静态已知 child frame 由父 frame 持有，
调度所有权位于 `TaskState` 的 Job header，每个 coroutine frame 不再携带独立 resume token。
剩余结构性成本是：

- TaskState 对所有 scoped 调用仍统一保留并发 Job header；取消状态已经压缩为 request word 与
  tagged target word。external continuation 和 suspend-handler payload 都已移出通用 TaskState，
  只在实际需要时进入父 frame/当前 suspend record。
- completion word 已合并 readiness、结果状态和唯一 waiter；剩余空间优化重点是按静态 Task 形态
  裁剪取消字段，而不是继续压缩完成协议。
- `local_serial` 已为 completion word、waiter registration 和结构化 Task 启动生成普通 load/store
  专用路径。MIR 记录每个 suspend 的 direct child、Task child、跨 executor、external、dynamic 来源，
  并通过递归 SCC 固定点计算传递执行封闭摘要。匹配调用点使用独立 confined
  resume 变体：
  Job schedule、request/handoff、suspend/complete 全部使用无原子协议，标准并发 ABI 保持不变。
- 静态可追踪的 immutable async lambda 已去虚化并把 child frame 嵌入 parent frame；真正动态的
  async Fn/RawFn context 使用专用 coroutine frame ABI。同一 serial executor 的 size-class frame
  在 executor-local freelist 中复用，跨 executor、concurrent 和超大 frame 仍需完善共享 fallback。

最后一点说明：只恢复旧 inline Task 不足以解决问题。
存储、结构化生命周期、完成协议、Job 调度
和最终回收必须作为一个整体重构。

## 语义基础：Task 是结构化子任务

直接 `Task<T>` 是地址稳定的 `!Movable` 结构化子任务；`new async` 在 heap 上原地初始化相同布局，
并返回可移动、非 Copyable 的 `Task<T>^` owner。TaskState 是 Task 的唯一内联字段，不存在独立
control block。两种形态共享完成、等待与取消状态机：

- 显式 Task 必须在父 coroutine 完成前完成。
- `await()` 正常消费 result；`cancel()` 只同步发布幂等取消请求，`cancel_and_await()` 请求取消并
  等待 Task 完成。result 只能被 `await()` 或 `cancel_and_await()` 消费一次。
- 活跃 Task 到达 scope exit、return、throw 或父任务取消路径时，
  编译器先请求取消，再等待完成。
- 同一退出路径有多个活跃 Task 时，先向全部 Task 发出取消，再逐个 join，不能串行执行
  `cancel + join`。
- standalone `async { ... }` 是显式 detached 入口。丢弃直接 Task 不表达 detach；丢弃 `Task<T>^`
  owner 不阻塞、也不隐式取消，由 owner/coroutine 双方交接回收 heap Task。
- 禁止对 Task 使用 `forget`，否则结构化生命周期无法成立。

因此父 frame 是所有 scoped Task storage 的合法 owner。父 cleanup 只有在所有子任务 join 后才能
继续销毁局部值和释放父 frame。

## 外部实现对照

### Swift

Jiang 的直接 `Task<T>` 最接近 Swift `async let`：
子任务不能越过词法作用域；
未显式等待的子任务在任何退出路径上都先隐式取消，再等待完成。
它不等价于 Swift 的非结构化 `Task`；后者即使 handle 被丢弃也会继续运行。
因此 Jiang 不应把 scoped Task 和 detached coroutine 放进同一个通用 runtime object。

Swift runtime 对结构化子任务允许编译器提供预分配空间。空间足够时，task header、future result
和 initial async context 可以共用这块 storage；不足时再从 parent task allocator 取得空间。这个
设计证明“父级预留、必要时 fallback”可行，但 Jiang 不直接复制 task-local LIFO allocator：Jiang
先由 MIR 的 `TaskRegion` 和 CFG lifetime 证明 slot 生命周期及复用顺序，避免把正确性建立在动态
deallocation 顺序上。

Swift future 把完成状态和 waiter 链表头编码在同一个 atomic word 中。Jiang 的 scoped Task 是
线性 owner，最多只有 parent 一个 waiter，因此可以使用更小的 `lifecycle_word`，没有必要复制
Swift 为 priority、task-local storage、多 waiter 和非结构化 handle 付出的完整 `AsyncTask` 布局。

Swift 的另一个关键约束比布局本身更重要：`completeFuture` 用一次 atomic exchange 发布完成，
exchange 返回的旧值已经携带 waiter；从这一刻开始，async-let storage 可能已被 parent 销毁，
completion 路径不得再读取 task object。Jiang 必须采用同样的 lifetime boundary，不能先写
`completed`，再回头读取 waiter、取消状态或 ownership 字段。

Swift runtime 还保留了独立的 inline-task completion 入口。
这说明 storage/reclaim fast path 值得静态特化，但 Jiang 只保留一套可验证的生命周期状态机：
`storage`、`sync` 和 `reclaim` 是编译期 policy，取消、完成和 join 语义不能分叉。

### Rust

Rust 把 `async fn` 降为一个返回匿名 `Future` 值的普通函数。Future 在被 poll 前不会执行；局部
pinning 本身不要求 heap allocation，跨 `await` 的局部值直接占用 Future 自身 storage。只有
executor ownership、动态大小或显式 boxing 等需求才引入间接存储。

这对应 Jiang 的 direct async call：它应当只是可内嵌的 coroutine state machine，不创建
TaskState。Rust 的 Future 默认也不代表已启动的并发子任务，所以 Jiang 不能直接照搬 lazy poll
语义；Jiang 的 scoped Task 仍由 scheduler 主动执行，并额外承担结构化 join 和取消传播。

### .NET

.NET `ValueTask` 可以直接携带结果，或包装 `Task`/`IValueTaskSource` 来避免部分分配；可复用 source
和 async method builder pooling 能继续降低 heap allocation，但同时引入单次 await、对象归还时机、
更大 value copy 等约束。它适合作为后续 allocator 优化，不应成为 Jiang 的第一层架构。

Jiang 先删除不必要的 control block、resume token 和 ownership CAS，再考虑 frame size-class pool。
否则 pooling 只会隐藏多余对象，无法消除多余初始化、原子状态转换和 cache footprint。

### C++ sender/receiver

C++ execution 的 `connect` 产生不可移动的 operation state，调用者负责让它活到异步 operation
完成；标准还明确允许 operation state 的生命周期在 completion operation 执行期间结束。
这与 Jiang 固定 binding 的 scoped Task 很接近，也再次要求 completion 把“可能销毁自己”当作
最后一步，不能在发出完成信号后访问 operation state。

C++ 模型本身不决定 operation state 必须位于 heap：具体组合器可以把它作为上层 operation state
的子对象。Jiang 的 TaskRegion/storage plan 采用这一点，但由编译器 CFG 和 coroutine frame
liveness 自动完成组合，不把 sender/receiver 模板层暴露给语言使用者。

### Jiang 的取舍

综合以上实现，Jiang 固定为一套语义状态机和三类静态形态：

- direct async：借鉴 Rust 的 value state machine，0 Task control state。
- scoped Task：借鉴 Swift `async let` 的结构化生命周期和预分配 storage，由 parent 唯一回收。
- detached coroutine：独立 heap ownership，语义上对应 Swift 非结构化 Task，而不是 scoped Task。

这里的“一套”以 source resume 和 continuation ABI 为边界：direct、scoped/heap Task、跨 Domain
Task 和 detached 都复用同一 frame state dispatch、suspend 与取消入口。它们只替换 continuation
终点；跨 Domain 只替换 enqueue policy。`extern async` 没有 Jiang resume body，但其 adapter 发布
同一 TaskState completion 协议。direct/detached 不形成可观察 Task，因此不为布局统一而补造
TaskState。

frame pool、task-local arena 等 allocator 技术只处理无法静态嵌入的 fallback frame，不能进入语言
语义，也不能代替逃逸、递归 layout 和跨线程生命周期证明。

当前动态 callable allocator 把 64 到 8192 字节分成 8 个二次幂 size class。local serial 路径只做
普通 freelist pop/push，不执行锁或原子操作；frame header 保存 class，completion 在 handoff 前归还。
同 executor serial 热路径优先使用 local pool；跨 executor 或 concurrent 路径使用 ABA-safe shared
pool。runtime 只依赖 `system.thread.AtomicStackArray` 的 opaque handle，不依赖平台队列头布局或符号。
macOS provider 暂由 `OSAtomicEnqueue/Dequeue` 保证 ABA safety；最终应由 compiler atomic intrinsic
提供目标相关的 lock-free tagged CAS，不能退化成未经证明的单指针 Treiber stack。

公开的 `Atomic<T>` 由 builtin 声明并通过 core 导出；load/store/exchange/CAS 由 compiler intrinsic
直接生成 LLVM atomic IR。协程 runtime 的标量状态字也通过同一 `Atomic<Int>` 路径访问，不再经过
system 或 OS atomic 函数。system 只保留等待/唤醒、队列和 ABA-safe shared frame stack 等目标能力。

## 三种执行形态

### Direct async call

源码没有形成 Task handle 时：

- 不创建 TaskState。
- 静态已知且不形成递归 layout 的子 frame 使用父 frame 中的 storage。
- immutable local 直接绑定的 async lambda 在调用点去虚化；capture env 与显式参数直接写入嵌入式
  child frame，不经过 vtable start shim，也不发生运行时 frame allocation。
- 递归、动态 callee 或 layout 不可嵌入时，只为 child frame 使用一次 fallback allocation。
- 子 coroutine 直接持有父 continuation。
- 同 serial executor 完成时允许直接 tail-resume；跨 executor 才进入 scheduler。
- 子 coroutine 继承当前 Task 的 Job pointer，但不创建新的 Task control state。

这是普通 async 调用的最低成本路径，不能为了统一 Task 实现而退化。
fallback 也不能创建 Task control state 或独立 resume token。

当前 lowering 已识别非泛型 self-recursive 和 mutual-recursive 回边：无环边仍嵌入 child frame，
回边通过 heap async-context start shim，并在同一 Task Job 上 handoff。20,000 层非尾递归测试用于
证明 native stack 不随 coroutine 深度增长。泛型递归还依赖 monomorph instance discovery 的递归
去重；在该前置问题修复前，不能声称 generic recursive async 已完成。

### Scoped Task

源码形成直接 Task 时：

- Task control state 默认位于父 frame；同步 root 可以位于调用者栈。
- 静态已知且不形成递归 layout 的子 frame，可以与 control state 一起位于父 frame。
- 递归/mutual-recursive async 调用、动态 async callable 或 layout 不可静态嵌入时，只单独分配
  child frame；control state 仍由父 frame 持有。
- control state 和 child frame 的 storage slot 都参与 CFG lifetime slot reuse。
- parent 是唯一回收者，child completion 不释放 scoped storage，不需要 ownership CAS。
- `local_serial` 且传递 `may_defer=false` 的 Task root 使用 run-to-completion 变体：创建表达式返回前
  child 必然完成，因此不建立 schedule target，不注册 waiter，也不调用通用 completion shim；结果和
  completion word 由 child 直接发布。该路径仍保持 eager Task 语义，不把 Task 改成 lazy Future。

不能简单规定所有 child frame 永远 inline。
递归 frame 会形成无限 layout；大型冷分支 frame 也可能
因为扩大父 allocation 和 cache footprint 而比单独分配更慢。最终由 storage planner 根据 layout、
递归 SCC、CFG 热度和 profile 信息选择 `parent_frame` 或 `heap_frame`，
而不是把经验阈值写死在 ABI 中。

### Heap Task owner

`new async { ... }` 直接在 heap allocation 中初始化 `Task<T>`，并返回 `Task<T>^`。owner pointer
可以按值传参、返回、存入字段、数组和泛型实例，而 TaskState 与 coroutine frame 的地址保持稳定。
heap Task 不使用通用引用计数：owner 和 coroutine 各持有固定的一方 lifetime 状态，最后离开的一方
回收 result、frame 和 Task allocation。owner 析构不能在 serial Domain 上阻塞等待，也不能隐式取消。
当前 `new async` 始终采用这个 heap baseline。直接 `Task<T>` 已由类型规则证明不逃逸并使用 parent-local
storage，不需要再做事后 escape analysis；只有分配 benchmark 证明有必要时，才考虑对未逃逸
`Task<T>^` 做不改变地址、生命周期和 `new` 可观察语义的 as-if stack promotion。

### Detached coroutine

standalone async 没有结构化 parent owner：

- frame 使用 self-owned heap storage。
- 不创建带 result/waiter 的完整 TaskState。
- completion 关闭 schedule word 后丢弃结果，并以不再访问 frame 的 tail-return 路径回收 frame。
- detached 与 scoped Task 使用同一 continuation ABI，但只有 scoped Task 拥有 Job header；
  没有 Task 的 detached continuation 直接调用或直接入队。

## 统一而可裁剪的布局

### AsyncContext 与 JobHeader

函数状态与调度状态必须分离。每个 coroutine frame 是 Swift 式 `AsyncContext`：

```text
AsyncContext
  state_pc
  result / live locals
  continuation_context
  continuation_resume
  continuation_executor
  cancellation_link
```

只有显式 Task 拥有固定的 Job 前缀：

```text
TaskJobHeader
  schedule_state
  scheduled_executor
  scheduled_context
  scheduled_resume
  active_executor
```

`schedule_state` 是 tagged generation word：低 3 位区分 `idle`、`queued`、`running`、`pending`、
`completed` 和三个短暂 publishing 状态，高位在每次发布新 target 时递增。generation 使
`queued -> running -> ... -> queued` 不再形成 ABA；CAS 失败只表示状态竞争，后续判断必须基于
一次 acquire snapshot，不能把多次独立探测拼成同一个状态。

queue callback 在 `queued` generation 仍不可变时先把 executor/context/resume 捕获到局部值，再用
精确的 generation CAS 取得 `running` lease；CAS 失败就丢弃捕获值。取得 lease 后 publisher 可以
写入下一代 pending target，但不能再改变已经捕获的当前调用目标。callback 在调用 target 后不得
访问 TaskState。内部 async completion 不是普通 wake，而是 `handoff`：从当前 continuation 对称
转移到下一个 continuation；外部 I/O、取消和首次启动才使用 `request`。

同一 Job 同时只允许一个逻辑 wake。`pending`/`queued` 收到重复 request 时，先读取 target，再确认
generation 未变化；只有稳定的同一代才验证 executor、context 和 resume 完全相同。若 generation
已变化则重新进入状态循环，而不是拿旧 snapshot 检查下一代 target。稳定代上的不同目标不能互相
覆盖，也不能静默丢弃，而应暴露为 lowering 协议错误。

完成路径先把 Job state 变为 completed，阻止新的 request，再解除 cancellation target，最后发布
Task completion。最终发布之后，child、completion shim 和 queue callback 都不得再访问 TaskState
或 child frame。async callable completion shim 必须直接 handoff 到 external continuation 的真实 target，
不能调用一个再发普通 request 的 wrapper，否则会留下无人接续的 `running + pending` Job。

同 serial executor 完成的 Task 使用静态 `local_serial` completion 模式：completion word 与 waiter
registration 使用普通 load/store。Job schedule word 仍可能被外部 callback 跨线程访问；取消链也可能
沿 Task 内部的跨 Domain 调用传播，因此只有额外证明整个执行子图 fully-confined 后才能去原子化，
不能从“parent 与 Task completion 同 executor”直接推出。

### ScopedTaskState

scoped Task 只保存 join/cancel 所需状态：

```text
ScopedTaskState<T>
  completion_word
  cancel_target_word
  child_frame
  result
```

`completion_word` 是 tagged word：0 表示 running，1/3 分别表示有结果/无结果完成；其余值为
waiter record 指针。低位 0 是父 coroutine frame 内的 async waiter record，低位 1 是同步调用栈上的
blocking acknowledgement word。parent 初始化 record 后，用一次 CAS 将其发布；child completion
用一次 acquire-release exchange 发布终态，并从旧值直接取得唯一 waiter。

async completion 在 exchange 后只读取 parent-owned waiter record，并以一次 runtime request 恢复
parent，不再读取 TaskState。waiter record 参与 `TaskRegion` frame slot reuse，生命周期不重叠的
scoped Task 不重复扩大父 frame。普通 async waiter 的完成热路径没有 waiter claim/arm CAS，也没有
第二次原子完成写。

- `ownership_state`：结构化 parent 是唯一回收者，删除。
- `allocation_pointer`：storage plan 是 MIR/生成函数的静态事实，删除。
- `result_drop`：drop shim 由 concrete Task result type 静态选择，不存函数指针。
- waiter context/resume/executor/task：从 TaskState 删除，改存 parent frame 的可复用 waiter record；
  record 指针直接编码在 completion word 中。
- external continuation：只存在于 external-async adapter capture，不进入普通 ScopedTaskState；
  其 7 个指针随所属 TaskRegion 存活，互不重叠的 external Task 复用同一个 parent-frame slot。
  以 64 位目标为例，普通 TaskState 因此固定减少 56 字节。
- cancel handler context/function/frame/resume/executor：不进入 TaskState。`cancel_target_word` 的低位 0
  表示 child TaskState，低位 1 表示当前 suspend record；claim 仍只做原有的一次 CAS。
  handler record 自带 context/function，owner resume 所需信息复用 record 原有字段，
  不增加第二份 payload。以 64 位目标为例，TaskState 再固定减少 40 字节；
  suspend record 同时从 16 个机器字缩到 13 个。

`cancel_target_word` 编码 cancel-requested bit 以及 inactive、handler、claimed、passive suspend
和 child TaskState pointer。它与 completion 的并发转换不同，强行合并会增加 CAS 循环和错误
共享。只有计数证明合并后更少原子操作时才继续压缩。

同步 root 的 blocking join 不能在看到 completed 后立即回收 storage，因为系统允许虚假唤醒，
completion 仍可能尚未执行使用 completion-word 地址的 wake。blocking parent 因此发布低位打 tag 的
栈上 ack 指针；completion exchange 终态后先 wake，再 release-store ack。parent 从系统 wait 返回后
必须 acquire-load ack，只有看到 1 才能继续回收。这样既消除了地址复用 ABA，
也让 async 热路径仍然只有一次 exchange；blocking 专用握手不会增加 TaskState 字段。

## 完成发布协议

scoped concurrent Task 的完成顺序固定为：

1. 写入 result 或 cancelled/error 状态所需的数据。
2. 将 child Task 的 Job schedule word 变为 completed，拒绝后续 resume request。
3. claim/解除 cancel target，保证并发取消方不再持有 child frame 的调度权。
4. 复制完成后仍需使用的 executor 等信息到局部值。
5. atomic exchange completion word；旧值给出是否存在 parent waiter。
6. exchange 后只允许使用旧值和局部副本请求 parent resume；不得访问 TaskState 或 child frame。

parent 只有 acquire 观察到最终完成后才能 move/drop result 和复用 storage slot。若 parent waiter
可能在同 executor 上立即执行，步骤 6 也必须安全：child completion 必须 tail-return，queue
callback 不能在 resume 返回后释放内嵌 token。这个协议是父帧内联 storage 的前置条件，
不允许用 ownership CAS 或延迟 free 绕过。

## Storage planner

MIR 在 coroutine frame layout 之前运行 Task ownership/storage analysis。每个 Task origin 形成
`TaskRegion`：

```text
TaskRegion
  origin
  binding
  consume site
  live exits
  suspend points before consume
  child frame layout
  execution relation
  storage plan
```

分析必须覆盖普通 CFG 边和 cancellation edge。它证明：

- handle 没有 escape、capture 或 forget；
- binding 只初始化一次，不允许 move、forget 或重新赋值；
- 每条退出路径都进入结构化 cancel/join cleanup；
- parent frame 销毁严格发生在 join 之后；
- storage slot 在 child completion 前不会复用。

storage plan 是正交维度，不再用一个 `inline_task` Bool 混合语义：

```text
control: parent_frame | caller_stack | heap
frame: parent_frame | caller_stack | heap | external
sync: local_serial | concurrent
reclaim: parent | self
```

合法组合由执行形态决定。backend 只消费已经证明的 plan，不自行猜测逃逸或 Domain 关系。

## 状态机与 fast path

### local_serial

parent 与 Task completion 在同一个 serial executor 上执行时，completion word 不存在并发访问：

- completion publish、ready check 和 waiter registration 使用普通 load/store。
- completion 发现 parent 正在等待时使用专用 handoff；idle parent 以一次 generation CAS 直接转为
  running，直接 tail-resume 传入的 continuation，不发布 target、不入队、不支付第二次 CAS。
- scoped reclaim 不执行 CAS。
- 已同步完成的 Task，`await()` 只检查 lifecycle word 并 move result。

“同为 serial Domain”不够；必须证明 executor binding 相同。不同 serial executor、concurrent
Domain 和可能从任意线程完成的 external async 都使用 concurrent 模式。
继承当前 executor 的 async wrapper 也可以使用 local completion；即使 wrapper 内部调用另一个
Domain，跨 Domain continuation 必须先回到 wrapper executor，才允许发布 wrapper 的 completion word。
这不等价于取消链 fully-confined。

执行封闭摘要不是简单 Bool。继承 executor 的 wrapper 没有声明 Domain，但内部可能只调用某个
静态 binding；因此固定点使用三态格：

```text
unconstrained               // 任意串行入口都保持独占
requires_binding(binding)   // 入口为该 binding 时保持独占
escaping                    // external/dynamic/冲突 binding，不能去原子化
```

递归 SCC 从 `unconstrained` 开始，遇到跨 executor、external、dynamic 或不一致的 binding 约束后
单调下降。显式 Domain 函数会用自己的 binding 消去相同约束；不同约束直接变为 `escaping`。
这使 `async same_domain_child()` wrapper 可在同 binding 调用点专门化，同时不会把跨 binding wrapper
误判为本地执行。

同一固定点还计算独立的传递 `may_defer` 位。它回答的不是源码中是否出现 `async`，
而是 confined 变体从当前入口开始是否可能等待未来事件：普通同步调用不产生等待；direct child 和
`local_serial` Task child 传播 child 的结果；external/dynamic/arbitrary wake 必然产生等待。
改写前标记为 `cross_executor` 的边，如果其 binding 正是 confined 变体的入口约束，local variant
会把 enqueue 改成直接 resume，因此只传播 child 的真实等待能力；声明 Domain 与所需 binding
不一致时仍严格标记为可延迟。该证明用于生成独立的 run-to-completion 路径，
不能直接放宽标准 resume ABI 或取消协议。

无等待的非递归 direct-async DAG 使用按需生成的 `__rtc_resume_`：callee 仍接收现有 frame pointer，
但省略 state dispatch、取消 claim、executor enter/leave 和 suspend skeleton，完成后直接返回 caller。
递归 SCC 不生成该变体，继续使用 `musttail` handoff trampoline，保证原生栈深度为常量。标准 scheduled
root 已经进入 executor 后，同 binding 的静态 direct-child 边也可以调用 RTC 变体；root 自身仍保留并发
completion 和入口 executor lease，不把跨线程协议错误地降成 confined 协议。无显式 Domain 的 wrapper
若带有逻辑上 cross-executor、但由 confinement binding 证明实际同 executor 的边，必须先按该 binding
生成完整 confined 可达闭包，再允许外层 RTC；只生成外层变体会把 scheduler handoff 留在嵌套栈调用中，
破坏 Job lease。纯 direct-child DAG 不额外生成 confined 副本，避免无用代码尺寸和间接分支回归。

显式 `local_serial` scoped Task 统一使用 confined resume 和 serial completion。它保留 Task 的发布、等待与
capture-env 析构协议，不再为“可能同步完成”复制一套完整协程体。只有语法上的 direct async 调用在静态
证明整个调用 DAG 不会等待时使用 RTC resume；Task handle 一旦存在，就继续采用标准 Task ABI。这样优化
边界由调用形态决定，不需要在 frame layout 阶段反向删除 Task 字段和伪 suspend，也避免协程体随入口协议
成倍复制。direct-child RTC 改写仍发生在 frame liveness 与 layout 之前，因此不可达的 direct child frame
不会被固定进 parent frame。

fully-confined 变体采用 borrowed executor-context ABI。只有标准 resume、queue callback 等真实调度
边界执行一次 `executor_enter/leave`；confined 父子、递归和 completion 链借用该 context，
不在每层重复切换 active context。变体只能从证明为 `local_serial` 且 child resume 静态可知的
Task root 进入。
函数引用、Task frame 中的 resume initializer 和递归边全部重写到独立 synthetic DefId，
不会改变标准 `_Resume/_Complete` 符号的并发语义。变体从 `local_serial` Task root 开始按可达闭包
惰性生成；未被 local root 使用的 preserving coroutine 不增加 MIR/LLVM 函数。Task completion 已有
独立 serial shim，不再额外复制一份无人引用的 confined completion。

confined resume 与四个 local Task protocol helper 使用 LLVM internal linkage。release O2 会把 helper
和可内联的 child resume 融入调用者，再删除独立函数副本；它们不会作为无用 runtime ABI 导出。
confined handoff 的 target 已经处于当前 executor 的 `running` 或 `pending` lease，发布信息也已经存在，
因此 backend 直接生成与 resume ABI 同签名的 `musttail` continuation 调用；不重复写
executor/context/resume，不递增 generation，也不进入 runtime 调度器。`musttail` 是递归 SCC
保持常量原生栈的硬约束，不能降级成依赖优化器猜测的普通 tail hint。
目标 continuation 必须在返回前消费该 lease，完成或对称移交给下一层。

外部取消只原子写共享 root mailbox，并请求 root resume；它不再从调用线程遍历 child TaskState。
root 在所属 executor 内向 child 传播取消，因此 confined 子树的 cancel target 和 Job schedule
可以使用普通 load/store。confined MIR 仍显式标记用户 `Atomic<T>` 为 `user_atomic`，只把编译器生成的
coroutine protocol 操作降为 local，不能借执行封闭证明削弱用户原子的内存语义。

### concurrent

跨线程路径使用 acquire/release 状态机：

- child 写 result、关闭调度和取消入口后，用 release exchange 最终发布 completed。
- parent 用 acquire load 观察 completed。
- waiter 注册与 completion 竞争只修改一个 completion word；旧值携带唯一 parent waiter。
- cancel target 继续使用独立 tagged atomic word。
- queue callback 在 tail-call resume 后不再访问 frame，最终发布后可以立即回收 scoped storage。

禁止为了源码统一让 local_serial 继续走 atomic helper；
同步模式必须在 MIR 中静态可见并生成不同
lowering。

### Suspend registration handshake

`coroutine.suspend` 的 continuation registration 使用四阶段状态，不能用单个 `resumed` Bool：

```text
registering -> parked -> resuming -> resumed
```

- handler 在 `registering` 阶段完成时，发布 result 后只把状态置为 `resumed`，不 enqueue；registration
  返回后直接沿当前调用栈继续。
- handler 在 `parked` 阶段完成时，发布 result 后 enqueue parent frame。
- registration 只有在 `registering -> parked` 成功后才真正挂起；若观察到 `resuming`，只等待短暂的
  result publication，不进入 scheduler。
- phase 的最终 release store 发布 result；重复 resume 不能再次写 result 或 enqueue。
- registration-time cancellation 走同一 fast path，但在继续用户代码前重新检查 cancellation request；
  已经 parked 的路径由正常 resume entry 检查取消，不重复支付原子读。

这个握手同时是正确性协议和性能协议：同步完成是 0 enqueue，异步完成恰好 1 enqueue。

### Scheduler request coalescing

Task Job 的调度状态必须至少区分 `idle`、`queued`、`running`、`pending` 和 `completed`。
`queued` 与 `running` 不能合并：前者已经存在尚未执行的 callback，若提前再次运行并释放 frame，
旧 callback 会变成 UAF；后者在相同 serial executor 上可以安全地直接重入或 symmetric transfer。

取消、I/O completion 和普通 resume 对同一 Job 的同一逻辑 wake 先在 schedule word 合并。queue
callback 先捕获稳定 `queued` generation 的 target，再用精确 CAS 取得 `running`；若 generation
已变化则 callback 退出。普通 suspend 把同代 `running -> idle`，或消费 `pending` 并调度已发布
target；内部 completion 使用 handoff 直接消费 running lease。frame 中不允许重新引入独立 resume
token 或调度引用计数。

## Cleanup lowering

结构化 Task cleanup 是可挂起控制流，不能继续作为 `StorageDead` 前的一条 release statement：

1. 每个直接 Task place 在最终 destination 中原地初始化，编译器记录初始化和 result 消费事实。
2. MIR lowering 根据离开的词法 place range 找出 Task；未初始化或已经完成消费的 place 不进入 cleanup。
3. 第一阶段向全部仍然活跃的 Task 传播取消。
4. 第二阶段为每个 Task 生成 join suspend point，并消费其 result ownership。
5. join 完成后 drop 未消费 result、销毁 child frame，并按 storage plan 释放 Task storage。
6. 所有 Task 完成后才继续 `StorageDead`、普通 local drop 和 parent completion。

Task cleanup CFG 必须先于 coroutine suspend-point collection 和 frame liveness，否则新增 join 状态
不会进入 state dispatch，parent frame 生命周期也无法被正确证明。

## 实现顺序

1. 已完成：固定结构化 Task 语义，禁止 forget，并为 scope/return/throw/cancel 建立
   cancel-all-then-join CFG。
2. 已完成：scoped reclaim 改为 parent-only，completion 不再释放 TaskState，join 后由 parent 回收。
   同时建立“最终完成发布后零访问”协议；
   这一步即使暂时保留 heap allocation 也必须成立。
3. 已完成：增加 TaskRegion analysis 和 storage plan，让 scoped Task control state 与静态 child frame
   进入父 frame。
4. 已完成：普通 async、async block、async Fn/RawFn 统一消费 storage plan；递归回边使用 heap frame +
   Job handoff，静态 immutable async lambda 已去虚化，动态 frame 接入 coroutine allocator。
5. 已完成：删除 scoped ownership CAS、allocation pointer、result-drop 函数指针、
   result-initialized 原子字、通用 external continuation 和固定 cancel-handler payload；waiter 协议已合并
   进 completion word，waiter/adapter record 都由 parent frame 按 TaskRegion 复用，cancel handler 直接
   由 tagged target word 指向 suspend record。
6. 已完成：`local_serial` 与 `concurrent` 生成不同 completion/join lowering；同 binding serial
   completion 使用普通 load/store，显式跨 binding、concurrent Domain、external async 保留
   acquire/release 协议。传递执行封闭分析、递归 SCC/binding 固定点和独立 confined
   resume 变体已经接通并按 local-root 可达闭包惰性生成；confined Task 的 start、request、handoff、
   suspend、complete 与 cancel propagation 均使用无原子协议，并消除了父子 resume 间重复的
   executor enter/leave。传递 `may_defer` 固定点、非递归 direct-async DAG 的最小 RTC resume，以及
   direct-child fusion 已经接通；standard scheduled root 持有 executor 后可以直调 RTC child，并在 layout
   前删除不再跨 suspend 的 direct-child frame slot。显式 scoped Task 不生成专用 RTC 协程体，统一使用
   confined resume 与 serial completion。递归 SCC 保留 `musttail` trampoline，不能退回原生递归栈。
7. 已完成：删除 resume token；用 Task JobHeader 统一并发调度，AsyncContext 只保存函数状态，
   completion callback 通过 handoff 对称转移。
8. 已完成：dynamic async callable、非泛型 recursive backedge 和 detached frame 接入 coroutine
   size-class allocator；serial local pool 为零原子热路径，concurrent/cross-executor 使用 ABA-safe
   shared pool。调度器统一通过语言层 atomic intrinsic 实现，不依赖特定系统的队列原语。
9. 后续仅在 benchmark 证明必要时调整 parent-frame/heap-frame placement，不用固定大小阈值代替证据。

每一步都必须保持同一套 Task lifecycle 语义；
storage、sync 和 reclaim policy 可以静态特化，但不能
重新产生两套取消或完成协议。

## 验证门槛

正确性测试至少覆盖：

- Task 正常 await、显式 cancel、scope exit、early return、throw 和父取消。
- Task result 不得在不同源码位置重复消费；`cancel()` 可重复调用，并且取消后仍可 await。
- 多个 live child 先全部 cancel 再 join。
- child completion 与 waiter registration、cancel request、parent cleanup 的全部竞态。
- 同/不同 serial executor、concurrent Domain 和外部线程 callback。
- recursive async、dynamic async Fn/RawFn、detached async 和 sync root。
- result/capture drop 恰好一次，并在 sanitizer 下无 UAF、leak 或 data race。

`script/lang_check.sh` 可用 `LANG_CHECK_SANITIZER=address|thread` 对 run 用例启用 ASan/TSan，
并用 `LANG_CHECK_RUN_FILTER` 选择竞态用例。macOS 默认使用系统 clang 的 compiler-rt；其他平台可用
`LANG_CHECK_SANITIZER_CLANG` 指定带 sanitizer runtime 的 clang。compile-only check/fail/emit 在该模式下
跳过，避免把 sanitizer gate 和普通诊断测试混在一起。

性能验证不能只测总耗时，还要有结构断言：

- direct async call：0 TaskState allocation，0 ownership CAS；
  acyclic known callee 为 0 frame allocation，recursive/dynamic callee 最多 1 次。
- scoped Task：0 control-state allocation，0 ownership CAS。
- heap Task owner：当前 baseline 恰好 2 次 box allocation，分别保存完整 `Task<T>` 和 child frame；
  TaskState 不形成第三个 allocation。
- 同 serial executor scoped Task：completion word/join registration 0 atomic operation；传递证明
  fully-confined 后，整个 join/completion 路径 0 atomic operation。
- `may_defer=false` scoped Task：沿 confined resume 与 serial completion 执行，不复制专用协程体；若 profile
  证明 Task ABI 本身是瓶颈，再设计调用 RTC core 的薄适配层，不能复制 body 或在 layout 后删除协议字段。
- `may_defer=false` direct child：标准 scheduled root 内 0 child handoff、0 child state dispatch，child frame
  不进入 parent frame layout；scheduled root 自身仍保留一次 executor enter/leave 和 concurrent completion。
- concurrent scheduling：0 resume-token allocation。
- known acyclic child frame：允许 0 child-frame allocation，并验证 slot reuse。
- known immutable async lambda：调用热路径 0 child-frame allocation、0 indirect start call，并验证
  capture env 与所有显式参数按源 MIR `.param` local 顺序写入 frame。
- recursive/dynamic child：最多 1 次 frame allocation，不再额外分配 TaskState/token。
- detached coroutine：最多 1 次 frame allocation。

`frame_layout_slot_mapping.jiang` 直接统计 lowering 后的 MIR allocation 节点，固定 direct/scoped 为 0、
heap owner 为 2、detached 为 1。该结构计数与 allocator pool/cache 是否命中无关；运行时 benchmark
只能用于判断这些 baseline allocation 是否值得优化，不能替代结构断言。

同时记录 frame size、heap bytes、原子 RMW 次数、enqueue 次数、direct resume 次数和 allocator
remote-free 次数。任何“优化”如果只减少 malloc，
却增加热路径原子操作、frame cache footprint 或
跨线程回收，都不能仅凭单项指标合入。

## 参考实现

- [Swift SE-0317: async let bindings][swift-async-let]
- [Swift runtime Task ABI][swift-task-abi]
- [Swift runtime Task implementation][swift-task-runtime]
- [Rust Future](https://doc.rust-lang.org/std/future/trait.Future.html)
- [Rust async functions](https://doc.rust-lang.org/reference/items/functions.html#async-functions)
- [.NET ValueTask](https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.valuetask)
- [C++ execution operation state](https://eel.is/c++draft/exec.opstate)

[swift-async-let]: https://github.com/swiftlang/swift-evolution/blob/main/proposals/0317-async-let.md
[swift-task-abi]: https://github.com/swiftlang/swift/blob/main/include/swift/ABI/Task.h
[swift-task-runtime]: https://github.com/swiftlang/swift/blob/main/stdlib/public/Concurrency/Task.cpp
