# JIL 设计

JIL 是 type check 之后的可执行语义 IR，用来承接后续 borrow check、drop 插入、优化和 backend。
JIL 的输入是 Semantic Model、`TypeCheckStore` 和 `ModuleGraph`；它不回读 AST，不重新 resolve，
也不重新 type check。Layout 只在实例化、drop、ABI 或 backend 的真实使用点按需查询。

## 边界

- JIL 以 function 为主要 lowering/codegen 单位。
- JIL body 由 locals、basic blocks、statements 和 terminators 组成。
- Semantic Model block 是表达式/语句容器；JIL basic block 是 CFG 节点。
- JIL place 表达语义位置，例如 local、field、index、deref。
- JIL nominal field projection 只保存 field `DefId` 和 field `TypeId`。tuple、closure environment 和
  coroutine frame 等编译器生成结构保存逻辑 index；LLVM element index 不进入 JIL。
- 每个源码函数先生成一份 template JIL body。template 可以包含泛型 `TypeId`、const parameter
  和 `DefId + GenericArgs` 形式的函数引用。
- concrete JIL 由 `instance_jil(InstanceKey)` 按需从 template 实例化；template 与 concrete 使用
  同一套 JIL 数据结构，不增加另一层 IR。
- JIL lowering 不为 nominal field 或 union payload 固化 layout index。drop、ABI 和 backend 在
  concrete owner type 的真实使用点按需查询 `LayoutStore`；layout 不需要在 JIL lowering 前批量完成。
- Semantic Model `for in` 在 JIL 中统一降成 index-loop CFG；range、array、slice 只影响 index 来源。

JIL 生成完成后，borrow check、drop elaboration 和 backend 会继续把 JIL 与 layout 查询结果
组合使用。

优化 pass、测量方法和 tail/musttail 证据见
[JIL 优化与基线](jil-optimization.md)。

`$` Intrinsic Operation 不在 JIL lowering 中重新按文本猜测。type check 先根据
`@intrinsic(value, Pattern)` / `@intrinsic(type, Pattern)` 的 receiver pattern 和 where 约束
选出 builtin lowering kind；JIL lowering 只消费这个 side table。带所有权副作用的
operation 需要显式表达：

- `move`：生成所有权转移，源 place 失效。
- `forget`：源 place 失效，不生成释放。
- `free`：生成释放，并使 receiver place 失效。

## Drop Elaboration

JIL lowering 初始产物只表达源码中已经显式形成的控制流和当前阶段能确定的 drop terminator。
隐式析构路径不在 Semantic Model lowering 或 type check 中展开，而是在 borrow check 验证后由 drop
elaboration 改写 JIL。

固定顺序如下：

```text
Semantic Model/type facts -> initial JIL
  -> concrete instance JIL
  -> optional structural verifier (--verify-jil)
  -> concrete borrow check
  -> drop elaboration
  -> optional drop-output verifier (--verify-jil)
  -> direct self-call candidate selection
  -> candidate-local provenance/escape analysis
  -> safe tail-recursion transform
  -> optional backend-input verifier (--verify-jil)
  -> parameter attribute proof
  -> backend
```

drop elaboration 的职责：

- 根据 locals 的 live range 和 CFG exit 插入隐式 drop。
- 只对 sema drop query 判定为需要 runtime drop 的值生成 drop；`!Movable` 值仍在固定 place
  的生命周期末尾析构。
- 对 `custom_drop` nominal type 先生成 `deinit` call，再生成自动递归字段 drop。
- 对 `recursive_drop` 类型递归展开字段/owner pointer drop。
- 保持所有插入的控制流仍然是普通 JIL basic block / terminator，不引入 backend-only 节点。

borrow check 负责在 drop elaboration 前证明已有 move/drop/use 不变量。drop elaboration 先按
sema drop query 判断是否需要 runtime drop，再按 layout drop category 改写 CFG；backend 只消费
elaborated JIL，不再自行推导析构顺序。

## Verifier

`jil/verifier.jiang` 是所有 JIL pass 共用的结构 gate。当前固定检查：

- function、body、local、block 与 global 的索引身份一致，entry、return local 和所有 CFG successor
  都在边界内。
- statement、terminator、operand、rvalue 和 place 中引用的 local、index local、projection、
  call destination 与 cleanup edge 都有效。
- drop 输出以及 backend 输入中的可发出 function 已完成 drop elaboration。
- coroutine resume/completion 在 backend 前不能保留 `CancelTerminator`；不直接发出的原始 async
  body 可以保留它，供 coroutine 分析与诊断使用。

开发校验通过 `--verify-jil` 在 lowering、drop elaboration 输出和 backend 入口运行 verifier。
验证失败会终止当前编译，backend 不负责修补损坏的 JIL。普通用户构建不默认遍历完整 JIL；
相关 compiler test 和开发构建负责覆盖 gate。后续新增 CFG pass 时，必须声明它保留或使哪些
analysis 失效，并在 pass 后复用同一个 verifier。

## 结构

阶段入口按职责组织：

```text
src/jil.jiang             JIL 阶段稳定入口
src/jil/model.jiang       数据模型入口
src/jil/model/            ID、Place、Value、CFG、Program、Store
src/jil/lower.jiang       Semantic Model -> JIL lowering 入口
src/jil/lower/            package 入口、body lowering、阶段内模型与共享 support
src/jil/instantiate.jiang template JIL -> concrete instance JIL
src/jil/references.jiang  concrete body 的直接函数引用收集
src/jil/analysis.jiang    dataflow analysis 入口
src/jil/analysis/         provenance 与参数属性证明
src/jil/optimize.jiang    优化编排入口
src/jil/optimize/         安全尾递归等具体 transform
```

`jil.jiang` 和各同名入口文件只提供稳定模块边界；模型、lowering、analysis 和 transform
不再堆在单个入口文件中。

`lower/package.jiang` 只保留 package 到 `jil.Store` 的调度入口。函数体 lowering
不放进入口文件；lowering 期间使用、但不会进入稳定 JIL 的缓存键和临时结果放在
`lower/model.jiang`。package 与函数体 lowering 共用的阶段状态放在
`lower/context.Context`；单个函数体使用 `lower/body.BodyLowerer`。函数、入口、local、
coroutine、expression、control-flow、pattern、call、place、constant、closure、ownership、
trait object、aggregate、task、suspend 和 intrinsic 按职责拆分。

这些文件可以在 `jil/lower` 内通过 `public extend BodyLowerer` 或
`public extend lower_context.Context` 组织同一阶段状态，但阶段外只导入
`jil/lower.jiang`。`lower/package.jiang` 只重导出 package-level 实现模块，
`ownership.jiang` 和 `task.jiang` 只重导出各自的内部职责模块；这种重导出是阶段内
可见性边界，不构成稳定 API，也不允许各文件复制一份独立 lowering 状态。

第一版 JIL 使用非 SSA 的 local + assignment 形式：

```text
jil.Store
  templates: DefId -> jil.Function
  functions: concrete jil.Function table

jil.Body
  locals: ArrayList<jil.LocalDecl>
  blocks: ArrayList<jil.Block>

jil.Block
  statements: ArrayList<jil.Statement>
  terminator: jil.Terminator
```

`jil.LocalDecl` 包含 local kind、`TypeId` 和来源信息。function body 至少包含 return local、
params 和 user locals。temporary local 由 lowering 在需要 materialize complex operand 时创建。

## Operand、Rvalue、Place

- `Operand` 表达可以直接使用的值，例如 constant、copy place、move place。
- `Rvalue` 表达一次 assignment 右值，例如 use operand、binary op、aggregate、ref。
- `Place` 表达可读写位置，例如 local、field projection、index projection、deref projection。

complex operand 必须先 lower 到 temporary local，不能在 operand 中嵌套表达式树。

## Global Place

JIL place 支持 local base 和 global base。非 `const` 全局变量是 addressable storage，可以作为
place 参与 read/write、borrow、field projection、index projection 和 deref projection。

`const` 仍保持值语义，不承诺拥有唯一 storage，也不因为跨 package 使用就自动 materialize 成
只读 global。因此动态下标访问、取地址或需要稳定 storage 的表数据应声明为非 `const` global，
例如 `UInt8![N] TABLE = [...]`。编译器可以把只读使用的 global lowering 成只读 backend global，
但这属于实现优化，不改变 source 层 `const` 和 global variable 的语义边界。

标准库中生成的 Unicode XID 表使用 global array，以便 `std.jiang` tokenizer 能通过动态下标
查询压缩 bitmap。

backend 符号名不使用源码 global 名称。普通 Jiang global 使用 package/module/DefId 派生名：

```text
_Jp{package_index}_m{module_index}_g{def_id}
```

这样不同文件或不同 package 中同名 global 不会在 LLVM module 的全局符号表里冲突。`public`
只影响 Jiang package API，不表示 backend 必须导出源码同名符号。`extern` global 保留源码名，
用于和外部 ABI 对接；未来如果支持显式 link name/export attribute，也应在这里接入。

## Terminator

control flow 由 terminator 表达：

- `return`
- `goto`
- `branch`
- `call`
- `switch`
- `unreachable`

`call` 使用 terminator，返回值写入 destination，再跳 continuation。这样 borrow check 和后续
异常/cleanup 路径都能在 CFG 上表达。

## Lowering 规则

- Semantic Model block lowering 按顺序把 statements 写入当前 JIL block。
- block tail expression 写入 destination，或在 function body 中写入 return local。
- `if` 使用 branch / then / else / join blocks。
- `loop` 和 `while` 使用 header / body / exit blocks，并维护 loop target stack。
- `return expr` 先把 expr lower 到 return local，再生成 return terminator。
- `switch` 使用 discriminant/tag branch blocks；enum/union variant pattern 的具体选择来自
  `TypeCheckStore`。
- `for in` 对 range 使用 `[start, end)` index loop；对 array/slice 使用 `len` 和 indexed place。
- field/member access lowering 生成 concrete `jil.Place` projection。
- hosted entry lowering 只从 `ModuleGraph.root_module` 查找 language `main`，dependency
  package 的 `main` 不会生成 C ABI `main` wrapper。

## 泛型实例

`template_jil(DefId)` 对每个源码函数只 lower 一次。`FunctionRef` 只保存一份权威泛型身份：
`DefId + GenericArgs`；类型参数和 const 参数不再拆成两套平行数组。

`instance_jil(InstanceKey)` 只替换当前函数体中的 type/const parameter，并解析 type check 已选择的
静态 trait 分派。它不会在查询内部递归实例化 callee；concrete body 中收集到的
函数引用由 emission
worklist 继续请求目标实例。borrow check 和 drop elaboration 只消费 concrete instance。

## Async continuation ABI

async source function 的普通 JIL body 只作为 coroutine pass 的输入，不直接生成源码形状的
backend function。backend 生成 `_Resume(frame)` 和 `_Complete(context)`；普通隐式 await 直接把
child frame 连接到 caller continuation。observed `Task { ... }` 显式 materialize `Task<T>`；
直接作为语句使用的 Task initializer 生成 detached start。

所有非 external 的 async source 都进入同一套 resume lowering：standard `_Resume(frame)` 共享 frame
state dispatch、suspend 边界和取消检查。confined 变体只改写已证明 executor-local 的协议操作，RTC
变体只裁剪已证明不会 suspend 的 direct DAG。入口本身只选择 frame continuation 和调度策略：

| 入口 | control state | completion | schedule |
| --- | --- | --- | --- |
| 普通 async 调用 | 继承当前 Task | 恢复 parent | 跨 Domain 才 enqueue |
| observed Task | 内嵌 TaskState | 发布 Task completion | 静态 storage/sync policy |
| 跨 Domain Task | 同一 TaskState | 发布 Task completion | 目标 Domain enqueue |
| detached block | 无 TaskState | 丢弃 result、回收 frame | 按需 enqueue |
| `extern async` Task | 同一 TaskState header | adapter 发布 completion | 无 Jiang resume body |

因此“统一状态机”指统一 source resume、Task completion word、wait/cancel 协议，不表示给 direct 或
detached 路径补造 TaskState。standard、fully-confined completion 共用同一 result/drop CFG；后者只在
证明 executor-local 后把协议操作改为 local。run-to-completion resume 也只对静态证明不会 suspend 的
direct DAG 生成，它是原状态机的裁剪版本，不是另一套可观察语义。

显式 Task 启动到不同 domain 时使用 `jil.CallCallee.domain_enqueue`；动态 executor 路径使用
`dynamic_domain_enqueue`。callee 同时携带可选 TaskState pointer 和 `handoff` 位。effect argument 不再
接受 const value；lowering 不解析用户类型上的 `enqueue` 成员，也不暴露 coroutine frame ABI。

LLVM backend 把普通 wake lowering 为
`__jiang_runtime_task_request(executor, task, context, resume)`，把当前 Job 上的 continuation 转移
lowering 为 `__jiang_runtime_task_handoff(...)`。静态 Domain identity 只用于取得 canonical executor，
不进入用户 ABI。JIL 不关心 runtime 使用单线程队列还是 worker pool，调度实现变化不改变
用户类型。

已知无环 direct async call 把 child frame 嵌入 caller frame。JIL lowering 用 concrete
`jil.InstanceKey` 记录正在构造的 coroutine；遇到 self/mutual-recursive 回边时不再递归形成无限
frame type，而是调用该 concrete callee 的 heap async-context start shim。start shim 和动态
async Fn/RawFn start 都携带当前 Task pointer，并以 `task_handoff` 转移 Job；caller resume、start
shim 和 runtime handoff 的返回块必须保持 codegen-empty，使 LLVM 能生成 tail call。

immutable local 直接绑定的 async lambda 也按已知 direct callee lowering：调用点保留 concrete lambda
DefId，把 capture env 和显式参数写入 parent-owned child frame，再直接调用 resume。参数到 frame 的
映射必须扫描 callee 源 JIL 的 `.param` local；不能假设第 N 个 ABI 参数恒等于 local N+1，因为
lambda receiver/env 是合成 local，而且未跨 suspend 的参数可能没有 frame field。

真正动态的 async Fn/RawFn start shim 使用 `jil.CoroutineFrameAllocRvalue`，completion shim 使用
`jil.CoroutineFrameFreeRvalue`。LLVM 将二者分别降低为
`__jiang_runtime_coroutine_frame_alloc(executor, size, alignment)` 和对应 free。它们与普通 Box
alloc/free 分离，保证 coroutine placement policy 可以变化而不污染语言级 heap allocation。
recursive backedge 和 detached Task initializer 复用同一 ABI。serial executor 的热路径使用无锁、
无原子的 executor-local size-class freelist；跨 executor 和 concurrent 路径使用 system provider
隐藏的 ABA-safe shared pool，JIL 和 runtime 均不依赖平台原子队列的具体布局。

0.4.6 的 backend 内建 enqueue 使用进程内单线程 FIFO。嵌套 enqueue 只追加节点，由最外层 drain
依次调用 resume，避免 completion 在当前 resume 栈内递归重入 continuation。该队列不提供跨线程
同步。

0.4.7 将 enqueue 实现移到 `runtime/scheduler.jiang`，backend 只生成稳定 ABI 调用。0.4.8 的
`system.thread` 为 macOS 和 hosted Linux 提供同一组 Queue、Group、Mutex、Task word wait/wake 与
shutdown 能力。macOS provider 使用 libdispatch/unfair lock/ulock；Linux hosted provider 使用
pthread queue/group 和 futex。Linux no-libc 的 Task word wait/wake 直接使用 futex syscall，但完整
多线程 scheduler 仍需要后续的 clone/thread startup provider。

所有 enqueue 都由 runtime Group 计入 shutdown 生命周期。shutdown 先等待已 enqueue continuation
完成，再停止并 join runtime 持有的 Linux serial worker，最后释放 executor、Queue、Group 与 provider
同步状态；macOS 的 libdispatch worker 仍由系统持有。backend 生成的阻塞 Task join 只调用
`__jiang_system_thread_wait_word` / `wake_word`，不直接引用 ulock 或 futex。provider 的 wait wrapper
负责吸收 EINTR 和伪唤醒，直到状态字不再等于 expected，避免把中断误当成 Task completion。

第三方 runtime 提供的 `extern async` 使用单隐藏参数 ABI：

```text
extern_async(args..., AsyncContinuation*) -> Void

AsyncContinuation {
  result_ptr: Void*
  caller_context: Void*
  resume_fn
}
```

continuation record 嵌入 caller coroutine frame，不要求堆分配。runtime 可以保存该稳定指针，完成后
先写 result，再调用 `resume_fn(caller_context)`。`result_ptr`、frame linkage 等 compiler 地址使用
专用 JIL 来源标记，但 backend 统一 lower 成 opaque `Void*`；需要访问真实对象的一侧必须先 cast
回具体指针类型。`Void*` 只允许传递、比较和 cast，不支持 `$.get()` / `$.set()`；用户
raw/reference 借用规则不因此放宽。

跨线程 Task completion 必须使用 release/acquire 同步，并通过 waiter claim CAS 防止丢唤醒或重复
resume。等待方先写 waiter context/function，再发布 armed 状态并重新 acquire 检查 ready；完成方发布
ready 后只在成功把 armed claim 为 notified 时调用 waiter。等待方若观察到 ready，只在成功撤销 armed
时直接继续，否则由已经 claim 的完成方负责恢复。serial domain 可以在证明不跨线程后消除原子操作。

0.4.8 的 scoped Task 由 parent 唯一回收；completion 不再与 observer 通过 ownership CAS 竞争释放。
`TaskRegion` 为 control state 和 child frame 分别选择 parent frame、caller stack 或 heap storage，
并让不重叠的结构化 Task 复用 frame slot。动态/递归 child 仍可只把 child frame 放到 heap。

0.4.8 中 `Task.cancel()` 只原子发布幂等 cancellation request，并把处理请求的工作调度到 task
execution domain；它不挂起 caller，也不消费 result。`Task.cancel_and_await()` 才会在发布请求后等待
completion 或 cancellation unwind 进入 terminal state。`await()` 与 `cancel_and_await()` 是 result 的
single-consumer operation，JIL 的 `TaskConsume` 与 borrow check 会诊断不同源码位置的重复消费。
`coroutine.check_cancelled()` 对根 cancellation context 执行原子 request claim；即使当前 direct child
使用 fully-confined 变体，这个访问也不能去原子化，因为根 Task 仍可能由另一 Domain 并发取消。
普通 `await()` 读到 cancelled terminal state 后转入 caller cancellation entry，继而清理 sibling；
`cancel_and_await()` 只释放目标 Task 并继续 caller。

0.4.8 将直接 `Task<T>` 固定为地址稳定的结构化子任务，并将 TaskState 作为 Task 的唯一内联字段。
`new Task` 在 heap 上直接初始化同一布局，所得 `Task<T>^` owner 可以传参、返回和存入聚合。
Semantic Model -> JIL lowering 在每条 scope exit、return、
throw 和 parent cancellation 路径上生成 cancel-all-then-join CFG：先向全部未消费 Task 发出取消，
再逐个生成可挂起 join，最后才进入普通 local drop。cancellation entry 在 BodyLowerer 内生成，因此
cleanup 新增的 suspend point 会参与 state dispatch 和 frame liveness。

Task lowering 保留 typed capture environment，并用 `CaptureBind(destination, environment)`
把环境中已有的 loans 绑定到 Task 根。该 statement 只表达 borrow-check 关系，不生成 backend
指令；frame rewrite、liveness、drop elaboration 和 fingerprint 必须保留它。Task frame 的
local/heap 选择只是 storage plan，不能决定引用捕获是否合法。`TaskRelease` 在执行结束后清除
对应 Task 根的 capture loans，因此直接 Task 与 owner Task 共用同一套 lifetime 传播。
`TaskOwnerRelease` 本身不证明 coroutine 已经结束；若 owner 根仍携带 capture loans，borrow
check 必须拒绝非阻塞析构。正常 await 路径先由 `TaskRelease` 结束 loans，随后释放空 owner。

当前实现覆盖 cancel-before-start 和自然恢复边界：observed Task frame 在 resume dispatch 前用 CAS 将
request 从 requested claim 为 cancelling，并从 cancellation entry 进入 drop elaboration 生成的清理链。
普通 async call 借出 continuation 前将 active-target word 从 inactive 发布为 passive suspend；
取消方看到 passive suspend 只保留 request 并等待自然 callback。callback 恢复后，resume boundary
将 passive suspend 作为无主动 handler 的取消入口处理，因此不会在外部仍持有 continuation 时
提前释放 frame。
parent 挂起于显式 `child.await()` 时，会把 child request 注册到 parent task-state；取消 parent
会先请求
取消 child，parent 只在 child terminal acknowledgement 恢复后进入自身 unwind。直接调用的 async child
继承根 Task 的 cancellation context；child 在自然恢复边界观察请求并先 unwind，再通过 continuation
恢复 parent，且只有根 Task claim 请求。

`coroutine.suspend(registration)` lower 为嵌入 caller frame 的 suspend continuation record。record 保存
进入 suspend 时的 executor；`Continuation.resume(value)` 取得 terminal ownership 并发布 result 后，
把 `context + resume` 调度回该 executor，不在任意 callback 线程直接执行 coroutine body。
`Continuation.on_cancel(handler)` 把本次 operation 的 handler 注册为 active cancel target；
`Continuation.resume(value)` 与取消方通过 pointer-sized tagged atomic word 竞争 terminal ownership。
registration 同步执行，因此 lowering 必须处理 cancel-before-registration、注册中取消、同步 resume 和
跨线程迟到 resume。只有取得 terminal ownership 的一方能够恢复 coroutine；取消方仍需等待外部
completion/cancel acknowledgement 后才能释放 continuation 所在 frame。类型级 `Cancellable` target
已经移除；每次 suspend 注册的 handler 与 child Task 复用同一个 tagged active-target 状态字。
cancellation cleanup 统一取消并等待已初始化的直接 Task，再为 drop elaboration 管理的
parameter/user local 合成 storage boundary；compiler temp 不伪造 marker，extern continuation record 等
跨 suspend temp 仍由 liveness 放入 frame。

coroutine frame 的 parent continuation 统一保存 `executor + context + resume`。普通 async child 继承
当前 Task pointer，但不创建新的 TaskState；完成时使用 handoff 对称转移到 parent continuation。
Task 根 completion 的 executor 为 null，表示直接执行内部 completion；跨 Domain 才进入 executor queue。
`extern async` 对 C 可见的 continuation 前三个 word 仍为 `result + context + resume`，其中 context 指向
完整 record，resume 是 runtime scheduling trampoline；record 尾部保存 executor、Task pointer 和真实
context/resume，因此外部 callback 不直接进入 coroutine body。

并发调度状态只存在于 TaskState 的固定 Job header，不进入每个 coroutine frame，也不再分配 resume
token。Job 区分 request 与 handoff：外部 wake/首次启动使用 request，内部 async completion、async
callable completion shim 和 coroutine continuation 转移使用 handoff。queue callback 调用 target 后不得
再访问 TaskState，Task completion 最终发布后同样禁止回读。

公开 `Domain` 只提供静态 `kind`、concrete `ExecutorType` 和同步 `make_executor`。JIL 不保存
concrete Executor value；lowering 为每个实际使用的 canonical const binding 生成
`DomainDescriptor`，其中包含 storage size/alignment 与 create/enqueue/destroy trampoline。
runtime 通过 descriptor 原地构造并 type erase Executor，向用户 `enqueue` 交付一次性的
`ExecutorJob`。同 binding 的 identity、serial gate、current executor 和 active frame
仍由 runtime 维护，不能下放给用户 Executor。

直接 `Task<T>` 不允许被嵌套 async closure 或 lambda 捕获；可移动的 `Task<T>^` owner
遵守普通 move、borrow、lifetime 与跨 Domain Sendable 规则。Task 可以运行于不同 domain，completion
仍将 waiter enqueue 回等待方所在 current domain。

动态 projection 中的 `Task<T>^` 被 `await()` 或 `cancel_and_await()` 消费时，lowering 在挂起前把
owner handle move 到稳定 local，并把源 element 置空。resume/release 只引用稳定 local，不能让
`IndexProjection.index_local` 隐式跨 suspend；frame rewrite 只改写 place base，不改写 projection
内部保存的 local id。

内建 macOS serial `DomainExecutor` 在创建 dispatch queue 时以 executor pointer 作为 queue-specific
key 和 context，因此当前 job 可以无 TLS、无额外 job allocation 地验证 serial Domain 执行权。
自定义 Executor 也走相同的 runtime identity 和 per-domain serial gate；不能从用户队列身份推导
Domain identity。concurrent Domain 共享 global queue，不能从底层 queue identity 推导具体 Domain；
其 same-executor 判断保持关闭。
generated coroutine resume 在统一入口保存并设置 executor 的 active frame，在统一出口恢复先前值。
serial scheduler 只有在当前 queue identity 与目标 executor 相同、且目标 frame 不是 active frame 时
才直接调用 resume；嵌套恢复其他 frame 会保存/恢复外层 frame，同步恢复当前 suspend frame 则继续
enqueue，避免在 registration 尚未返回时重入同一个 frame。
executor 的 tracking-enabled 位由 generated resume 首次 enter 时开启；旧 bootstrap 生成、尚未包含
enter/leave 的 coroutine 因此保持保守 enqueue，不会只更新 scheduler 后误启用 direct resume。

## 不变量

- JIL 不保存 AST id。
- JIL local 保留 `TypeId`；类型来源是 `TypeCheckStore`，不是 Semantic Model nullable type 字段。
- JIL 不保存 field offset、size、align 或 ABI 信息；这些事实只来自 `LayoutStore`。
- backend-specific symbol/mangling 不写入 JIL。
- 所有可发出的 JIL 必须满足 final verifier contract；`--verify-jil` 和相关 compiler test
  负责执行检查，backend 不接受“边生成边修复”的 CFG。
