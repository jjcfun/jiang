# JIL 单态化设计

单态化运行在 template JIL 之后。每个源码函数只 lower 一份允许包含泛型参数的 template；
真正可达的 concrete function 由 `instance_jil(InstanceKey)` 按需生成。

```text
type check
  -> template_jil(DefId)
  -> borrow_check(template_jil)
  -> instance_jil(DefId + GenericArgs)
  -> drop elaboration / backend
```

## 身份与所有权

- `FunctionRef` 使用 `DefId + GenericArgs` 表示源码函数引用。
- `TemplateJilQuery` 缓存当前 JIL Store 中的 `TemplateId`。
- `InstanceJilQuery` 缓存当前 JIL Store 中的 concrete `FunctionId`。
- template 和 instance 共用同一套 JIL 数据结构，由各自的 owner 表区分。
- 不存在独立的全局实例 store，也不保存第二份 reachable instance 集合。

## 求值

初始 root 产生 concrete function。扫描 concrete body 中的 `FunctionRef` 后，把尚未生成的
`DefId + GenericArgs` 加入 worklist。单个 instance query 只替换自己的 type/const parameter，
不递归求值 callee；递归调用由 worklist 收口。

Layout 不由单态化预收集。instance、drop、ABI 或 backend 在真实使用点用 concrete `TypeId`
查询 `LayoutQuery`。

## 不变量

- template lowering 不读取 concrete instance 或 concrete Layout。
- borrow check 直接检查 generic template；源码 concrete instance 不重复检查。
- instance 输出不得包含未绑定泛型参数。
- drop elaboration 和 backend 只消费 concrete function。
- 单态化不重新 resolve、type check 或验证 trait bound。
- 未可达的泛型函数、vtable 和 Layout 不生成结果。
