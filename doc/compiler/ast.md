# AST 设计

AST 是 `syntax` 阶段的输出，只表达源码语法结构。它不进入长期 `CompilerStore`，
也不承担名字解析、类型推断或 lowered IR 的职责。

## 职责

- lexer 产生 token。
- parser 产生 flat AST。
- AST 保留源码语法结构和 source span。
- AST 不保存 `DefId`、`TypeId`、`sem.NodeId` 或任何跨阶段语义结果。

## 存储结构

AST 使用 flat table 结构：

```text
AstUnit.nodes    -> ArrayList<AstNode>
AstUnit.children -> ArrayList<AstId>
AstId            -> 单个 AstUnit 内部的有效 node index
```

`AstNode` 是固定大小节点，只包含 `span` 和 `AstData`。`AstData` 是 tagged enum，
它本身就是节点种类，不再额外维护 `AstKind`。

变长子节点通过 `AstRange` 指向 `children` 中的一段连续 `AstId`。函数参数、调用实参、
结构体字段、block statement 都使用各自语义字段名保存 `AstRange`，但底层共用
`children`。

## Id 规则

`AstId` 本身不表示缺失值；所有 `AstId` 都必须指向有效节点，下标 `0` 合法。
可缺省字段使用 `AstId?`，不能用 `-1` 之类的哨兵值。`children` 中也不能加入缺失节点。

`AstId` 不属于全局 query id。它只在所属 `AstUnit` 内有意义；跨文件引用必须同时保留对应
`AstUnit`，不能把裸 `AstId` 当作全局或稳定身份。

## Source

`AstUnit.source_id` 记录 session-local `SourceId`。文件和 virtual source 都先进入 `SourceStore`，
由 `SourceKey.file(path)` / `SourceKey.virtual(name)` 表达来源类别；两者都具有非 optional identity
和文本，不为 virtual source 引入第二套 AST source 表示。

pipeline 把 owned root `AstUnit` 直接移交给 `ModuleResolver`。resolver 私有持有 root/import
closure 中尚未完成 lowering 的 AST；每个 module 发布 Semantic Model 后立即释放对应 AST。

## 不变量

- syntax 不理解名字解析和类型语义。
- AST 不直接作为长期缓存对象。
- AST span 只服务 source-level 诊断和 lower 阶段定位。
