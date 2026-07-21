# Coroutine 极致优化设计

本文固定 Jiang coroutine/Task 的最终优化方向。目标不是减少某一个 benchmark 中的 malloc，
而是在不牺牲生命周期正确性、取消语义和跨 Domain 并发安全的前提下，
让每一种调用形态只承担
它实际需要的成本。

## 当前实现的问题

当前实现已经完成第一轮架构收敛：普通 async 直接调用复用父 continuation，调度所有权位于
`TaskState` 的 Job header，每个 coroutine frame 不再携带独立 resume token。剩余结构性成本是：

- 普通显式 async 调用把子 frame 放在父 frame，却单独 heap 分配 TaskState。
- observed async block heap 分配 `[TaskState, frame]`，与普通 async 调用采用不同的存储路径。
- TaskState 对所有调用统一保留 waiter、取消、external continuation、result drop、allocation pointer
  和 ownership 字段。即使某条路径不需要这些能力，也要支付空间和初始化成本。
- `local_serial` 与 concurrent Task 暂时共用原子 Job 状态机，尚未生成纯 load/store 快路径。
- Task 被消费和 coroutine 完成通过 ownership CAS 竞争最终释放者；同 serial executor 和严格结构化
  Task 也支付同样的原子成本。
- 静态可追踪的 immutable async lambda 已去虚化并把 child frame 嵌入 parent frame；真正动态的
  async Fn/RawFn context 使用专用 coroutine frame ABI。同一 serial executor 的 size-class frame
  在 executor-local freelist 中复用，跨 executor、concurrent 和超大 frame 仍需完善共享 fallback。

最后一点说明：只恢复旧 inline Task 不足以解决问题。
存储、结构化生命周期、完成协议、Job 调度
和最终回收必须作为一个整体重构。

## 语义基础：Task 是结构化子任务

`Task<T>` 已经只能作为 body-local 类型，不能出现在参数、返回值、字段或 global 中，也不能被
lambda/effect block 捕获。最终实现继续利用这个限制，固定以下语义：

- 显式 Task 必须在父 coroutine 完成前完成。
- `await()` 正常消费 Task；`cancel()` 请求取消并等待 Task 完成。
- 活跃 Task 到达 scope exit、return、throw 或父任务取消路径时，
  编译器先请求取消，再等待完成。
- 同一退出路径有多个活跃 Task 时，先向全部 Task 发出取消，再逐个 join，不能串行执行
  `cancel + join`。
- standalone `async { ... }` 是显式 detached 入口。丢弃 Task handle 不再隐式表达 detach。
- 禁止对 Task 使用 `forget`，否则结构化生命周期无法成立。

因此父 frame 是所有 scoped Task storage 的合法 owner。父 cleanup 只有在所有子任务 join 后才能
继续销毁局部值和释放父 frame。

## 外部实现对照

### Swift

Jiang 的 body-local `Task<T>` 最接近 Swift `async let`：
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

frame pool、task-local arena 等 allocator 技术只处理无法静态嵌入的 fallback frame，不能进入语言
语义，也不能代替逃逸、递归 layout 和跨线程生命周期证明。

当前动态 callable allocator 把 64 到 8192 字节分成 8 个二次幂 size class。local serial 路径只做
普通 freelist pop/push，不执行锁或原子操作；frame header 保存 class，completion 在 handoff 前归还。
同 executor serial 热路径优先使用 local pool；跨 executor 或 concurrent 路径使用 ABA-safe shared
pool。runtime 只依赖 `system.thread.AtomicStackArray` 的 opaque handle，不依赖平台队列头布局或符号。
macOS provider 暂由 `OSAtomicEnqueue/Dequeue` 保证 ABA safety；最终应由 compiler atomic intrinsic
提供目标相关的 lock-free tagged CAS，不能退化成未经证明的单指针 Treiber stack。

公开的 `Atomic<T>` 属于 std；load/store/exchange/CAS 必须由 compiler intrinsic 直接生成 LLVM
atomic IR。system 只承载目标能力和 runtime 私有 provider，不能让每次用户原子操作经过 OS 函数。

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

源码形成 body-local Task 时：

- Task control state 默认位于父 frame；同步 root 可以位于调用者栈。
- 静态已知且不形成递归 layout 的子 frame，可以与 control state 一起位于父 frame。
- 递归/mutual-recursive async 调用、动态 async callable 或 layout 不可静态嵌入时，只单独分配
  child frame；control state 仍由父 frame 持有。
- control state 和 child frame 的 storage slot 都参与 CFG lifetime slot reuse。
- parent 是唯一回收者，child completion 不释放 scoped storage，不需要 ownership CAS。

不能简单规定所有 child frame 永远 inline。
递归 frame 会形成无限 layout；大型冷分支 frame 也可能
因为扩大父 allocation 和 cache footprint 而比单独分配更慢。最终由 storage planner 根据 layout、
递归 SCC、CFG 热度和 profile 信息选择 `parent_frame` 或 `heap_frame`，
而不是把经验阈值写死在 ABI 中。

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

同 serial executor 的 Task 最终使用静态 `local_serial` 调度模式：schedule word、waiter 和取消状态都
使用普通 load/store；只有跨 executor、concurrent Domain 或外部线程 callback 使用 atomic 模式。

### ScopedTaskState

scoped Task 只保存 join/cancel 所需状态：

```text
ScopedTaskState<T>
  completion_word
  cancel_target_word
  child_frame
  result
```

`completion_word` 是 tagged word：running sentinel、等待它的 parent frame pointer，或最终完成
状态。parent 在写入等待关系时用 CAS 把 running 替换为自身 frame pointer；child completion 用
一次 exchange 发布完成，并从旧值直接取得 waiter。exchange 是 TaskState 的最后一次访问，之后
只能使用已经复制到局部值的 waiter 信息。

当前独立的 `state`、`waiter_registered` 和 `result_initialized` 因而合并进 completion word；
完成状态同时编码 success/cancelled/error，观察到完成就意味着 result 状态已经最终确定。

- `ownership_state`：结构化 parent 是唯一回收者，删除。
- `allocation_pointer`：storage plan 是 MIR/生成函数的静态事实，删除。
- `result_drop`：drop shim 由 concrete Task result type 静态选择，不存函数指针。
- waiter context/resume/executor：parent frame pointer 直接编码在 completion word 的旧值中。
- external continuation：只存在于 external-async adapter layout，不进入普通 ScopedTaskState。

`cancel_target_word` 编码 cancel-requested bit 以及 inactive、handler、claimed、passive suspend
和 child TaskState pointer。它与 completion 的并发转换不同，强行合并会增加 CAS 循环和错误
共享。只有计数证明合并后更少原子操作时才继续压缩。

同步 root 的 blocking join 不能直接复用 async waiter 协议：系统 wake 仍可能在 parent 观察状态后
使用 word 地址。它采用 `running -> publishing -> completed`，先进入 publishing 并 wake，再以最后
一次 release store 写 completed；waiter 被唤醒后必须等到 completed 才能回收栈上 storage。这个
额外阶段只存在于 blocking join，不污染 coroutine-to-coroutine fast path。

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

父子在同一个 serial executor 上执行时不存在并发访问：

- completion、waiter registration、cancel request 使用普通 load/store。
- completion 发现 parent 正在等待时直接进入 scheduler 的 inline/tail-resume 路径。
- scoped reclaim 不执行 CAS。
- 已同步完成的 Task，`await()` 只检查 lifecycle word 并 move result。

“同为 serial Domain”不够；必须证明 executor binding 相同。不同 serial executor、concurrent
Domain 和可能从任意线程完成的 external async 都使用 concurrent 模式。

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

1. Task handle 在函数入口初始化为 null，创建成功后再发布真实 state pointer。
2. MIR lowering 根据离开的词法 local range 找出 Task；已消费或未初始化 Task 的 handle 为 null。
3. 第一阶段向全部仍有 handle 的 Task 传播取消。
4. 第二阶段为每个 Task 生成 join suspend point，并消费 handle。
5. join 完成后 drop 未消费 result、销毁 child frame，并清除 Task owner。
6. 所有 Task 完成后才继续 `StorageDead`、普通 local drop 和 parent completion。

Task cleanup CFG 必须先于 coroutine suspend-point collection 和 frame liveness，否则新增 join 状态
不会进入 state dispatch，parent frame 生命周期也无法被正确证明。

## 实现顺序

1. 已完成：固定结构化 Task 语义，禁止 forget，并为 scope/return/throw/cancel 建立
   cancel-all-then-join CFG。
2. 先把 scoped reclaim 改为 parent-only：completion 不再释放 TaskState，join 后由 parent 回收。
   同时建立“最终完成发布后零访问”协议；
   这一步即使暂时保留 heap allocation 也必须成立。
3. 增加 TaskRegion analysis 和 storage plan，再让 scoped Task control state 进入父 frame/栈。
4. 进行中：普通 async、async block、async Fn/RawFn 统一消费 storage plan；非泛型递归回边已经使用
   heap frame + Job handoff，静态 immutable async lambda 已去虚化，真正动态 frame 仍需接入统一
   placement/allocator。
5. 删除 scoped ownership CAS、allocation pointer 和 result-drop 函数指针，合并 completion word。
6. 实现 `local_serial` 与 `concurrent` 两套静态 lowering。
7. 已完成：删除 resume token；用 Task JobHeader 统一并发调度，AsyncContext 只保存函数状态，
   completion callback 通过 handoff 对称转移。
8. 已完成：dynamic async callable、非泛型 recursive backedge 和 detached frame 接入 coroutine
   size-class allocator；serial local pool 为零原子热路径，concurrent/cross-executor 使用 ABA-safe
   shared pool。后续用 compiler wide-atomic intrinsic 替换 macOS provider 的过渡 OSAtomic 实现。
9. 最后基于 profile 调整 parent-frame/heap-frame placement，不用固定大小阈值代替证据。

每一步都必须保持同一套 Task lifecycle 语义；
storage、sync 和 reclaim policy 可以静态特化，但不能
重新产生两套取消或完成协议。

## 验证门槛

正确性测试至少覆盖：

- Task 正常 await、显式 cancel、scope exit、early return、throw 和父取消。
- 已消费 Task 不得重复取消；所有退出路径上的未消费 Task 必须先传播取消。
- 多个 live child 先全部 cancel 再 join。
- child completion 与 waiter registration、cancel request、parent cleanup 的全部竞态。
- 同/不同 serial executor、concurrent Domain 和外部线程 callback。
- recursive async、dynamic async Fn/RawFn、detached async 和 sync root。
- result/capture drop 恰好一次，并在 sanitizer 下无 UAF、leak 或 data race。

性能验证不能只测总耗时，还要有结构断言：

- direct async call：0 TaskState allocation，0 ownership CAS；
  acyclic known callee 为 0 frame allocation，recursive/dynamic callee 最多 1 次。
- scoped Task：0 control-state allocation，0 ownership CAS。
- 同 serial executor scoped Task：join/completion 路径 0 atomic operation。
- concurrent scheduling：0 resume-token allocation。
- known acyclic child frame：允许 0 child-frame allocation，并验证 slot reuse。
- known immutable async lambda：调用热路径 0 child-frame allocation、0 indirect start call，并验证
  capture env 与所有显式参数按源 MIR `.param` local 顺序写入 frame。
- recursive/dynamic child：最多 1 次 frame allocation，不再额外分配 TaskState/token。
- detached coroutine：最多 1 次 frame allocation。

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
