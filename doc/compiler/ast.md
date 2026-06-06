# AST 设计

AST 是 `syntax` 阶段的输出，只表达源码语法结构。它不进入长期 `CompilerStore`，
也不承担名字解析、类型推断或 lowered IR 的职责。

## 职责

- lexer 产生 token。
- parser 产生 flat AST。
- AST 保留源码语法结构和 source span。
- AST 不保存 `DefId`、`TypeId`、`HirId` 或任何跨阶段语义结果。

## 存储结构

AST 使用 flat table 结构：

```text
AstUnit.nodes    -> ArrayList<AstNode>
AstUnit.children -> ArrayList<AstId>
AstId            -> 单个 AstUnit 内部的有效 node index
```

`AstNode` 是固定大小节点，只包含 `span` 和 `AstData`。`AstData` 是 tagged union，
它本身就是节点种类，不再额外维护 `AstKind`。

变长子节点通过 `AstRange` 指向 `children` 中的一段连续 `AstId`。函数参数、调用实参、
结构体字段、block statement 都使用各自语义字段名保存 `AstRange`，但底层共用
`children`。

## Id 规则

`AstId` 本身不表示缺失值；所有 `AstId` 都必须指向有效节点，下标 `0` 合法。
可缺省字段使用 `AstId?`，不能用 `-1` 之类的哨兵值。`children` 中也不能加入缺失节点。

`AstId` 不属于全局 query id。跨文件或跨阶段引用 AST 时应显式携带
`AstSource`/`SourceFileId` 和 `AstId`，不能只传裸 `AstId`。

## Source

`AstUnit.source` 记录 AST 来源；它只能是普通 `SourceFileId` 或 virtual source。
测试、宏展开、REPL 片段使用 virtual source，不引入 none 状态。

当前 pipeline 会为一次 `compile_package` 创建临时 `syntax.Store`，保存 root/import closure
内的 AST。`syntax.Store` 用完即可释放，不挂到 `CompilerStore`。

## 不变量

- syntax 不理解名字解析和类型语义。
- AST 不直接作为长期缓存对象。
- AST span 只服务 source-level 诊断和 lower 阶段定位。
