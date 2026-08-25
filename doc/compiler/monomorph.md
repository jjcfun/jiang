# JIL 实例化设计

每个源码函数只 lower 一份允许包含泛型参数的 generic JIL。可达实例由
`InstanceKey = DefId + GenericArgs` 标识；实例化是对 generic body 的读取视图，不生成第二份 CFG。

```text
type check
  -> source_jil(DefId)
  -> borrow_check(generic JIL)
  -> reachable InstanceKey worklist
  -> InstanceReader(generic body, InstanceKey)
  -> drop / ABI / Layout / backend
```

## 身份与所有权

- `FunctionRef` 使用 `DefId + GenericArgs` 表示源码函数引用。
- `SourceJilQuery` 保证每个 `DefId` 只生成一份 generic body。
- `JilInstanceQuery` 只保证一个 `InstanceKey` 的 demand 求值一次，不保存 concrete body。
- `Program` 拥有 source instance identity、可达集合和 compiler-derived function。
- `MonoItem` 统一引用 source `InstanceKey`、source resume mode 和编译器派生 `FunctionId`。
- backend 通过 `InstanceReader` 解释类型、const、callee 和 generic args，不自行复制或修改 CFG。

## 求值

language entry、export、global initializer 和 runtime root 建立初始实例。扫描 generic body 时，
`InstanceReader` 把引用解释为 concrete callee demand，再加入可达 worklist；递归调用由 worklist 收口。

Layout 不由实例化阶段预收集。drop、ABI、coroutine frame 或 backend 在真实消费点使用替换后的
concrete `TypeId` 查询 `LayoutQuery`。未可达的泛型实例、vtable、drop glue 和 Layout 不生成结果。

## 不变量

- source JIL lowering 不读取 concrete instance 或 concrete Layout。
- borrow check 每个 generic source definition 只执行一次。
- source instance 不拥有 concrete CFG 副本。
- 所有进入 Layout、ABI 和 LLVM lowering 的类型、const 与 callee 必须能通过 `InstanceReader` 得到
  concrete 结果。
- 实例化不重新 resolve、type check 或验证 trait bound。
