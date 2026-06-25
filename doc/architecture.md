# Jiang 编译器架构

Jiang 编译器围绕稳定的阶段边界组织。本文只保留整体架构、跨阶段边界和目录约定；
各阶段的详细设计放在对应阶段文档中。

## 顶层流程

```text
driver/cli -> pipeline.compile
                |
                v
        core + package source/AST -> HIR -> type/comptime facts -> MIR -> checked MIR -> backend output
                                      \           \          \          \
                                       ----------- layout facts --------
             \
              lang provider scan/parse -> public syntax tree -> internal AST
```

流程中的几个块对应：

- `core + package source/AST`：compiler-known core 源码、package manifest、source、syntax、
  module graph 和 resolve。
- `lang provider scan/parse`：`#alias { ... }` 调用 manifest dependency 中的 `type = lang`
  provider，provider 返回 public syntax tree，compiler 转换为内部 AST。
- `HIR`：resolve 直接生成的未类型化语义树。
- `type/comptime facts`：`TypeCheckStore`、`ComptimeStore` 和 `MonomorphStore`。
- `MIR`：HIR lowering 生成的 CFG。
- `checked MIR`：borrow check 后经过 drop elaboration 的 MIR。
- `backend output`：LLVM IR、object file 或 executable。
- `layout facts`：由 HIR、type facts 和 target layout 按需查询得到，供 MIR、borrow/drop 和
  backend 使用。

当前 root module 加载前会先加载 `src/core/core.jiang`。core 源码声明 compiler-known
trait、builtin named type 的 namespace 外壳、body-less builtin trait implementation，以及 `$`
intrinsic 接口。std 和用户 package 仍走普通 module graph；core package 不能由用户直接 import。

`layout` 是 store 层事实，不从 MIR body 生成。它消费 HIR、`TypeCheckStore`、
`MonomorphStore` 和 target layout。MIR lowering、borrow check、drop elaboration
和 backend 都可以按需查询 layout；各阶段不能绕过 `LayoutStore` 自己推导 field offset、size
或 ABI 表达。

`--check` 当前仍会跑到 MIR、borrow check 和 drop elaboration，保证源码级语言契约不只停在
type check。

## 架构规则

Jiang 编译器采用 `CompilerStore + Phase Contract + Pass Pipeline` 的开发模式。每个阶段只生产
本阶段事实，跨阶段访问必须通过显式 store API。新增功能先确定阶段归属，再实现代码。

### 总原则

- 编译器内部数据按事实表组织，不让 AST/HIR/MIR 节点直接持有跨阶段复杂对象。
- `DefId`、`HirId`、`TypeId`、`LayoutId`、`MirFunctionId` 都是 session-local handle。
- 跨次编译身份统一使用 `StableKey`，不能写入 session-local ID。
- fingerprint 只表示内容摘要，artifact cache key 只能由 `StableKey` 和编译配置组合计算得到。
- 每个事实只能有一个 owner store；其他模块只能查询或引用，不能复制一份并长期维护。
- 每张新增 store 都必须明确 owner、key、value、生命周期和失效条件。
- `span`、`SourceMap` 和 source offset 只用于诊断定位，不能参与符号身份、重载匹配或缓存 key。
- 自举修复必须优先修正语义或 IR 边界，不能通过绕过语法、跳过检查或
  backend 补语义解决。

### 阶段 Contract

- `syntax`
  - 生产：token、AST、语法诊断。
  - 消费：source text、keyword store、lang registry。
  - 禁止：名字解析、类型判断、布局、codegen 语义。
- `lang`
  - 生产：lang provider registry、provider dynamic library handle、public syntax tree expansion。
  - 消费：package manifest、artifact cache、host dynamic library loader、`std.jiang.syntax.Provider`。
  - 禁止：生成 HIR/MIR/backend IR、依赖普通 import/name resolve 查找 provider。
- `resolve`
  - 生产：core/module graph、namespace、`DefId`、名字绑定、HIR。
  - 消费：AST、source、package manifest、compiler-known core root。
  - 禁止：类型推导、layout、MIR/backend 逻辑。
- `hir`
  - 生产：resolved untyped HIR、HIR store。
  - 消费：resolve facts。
  - 禁止：保存 type check 结果、layout、backend symbol。
- `type_check`
  - 生产：`TypeCheckStore`、trait/overload/type facts、builtin operation lowering kind、
    trait companion type facts、const initializer 的 `ComptimeValue`。
  - 消费：HIR、resolve facts。
  - 禁止：改写 HIR、计算 ABI layout、生成 MIR。
- `comptime`
  - 生产：`ComptimeStore` 中的 const value，以及 `comptime {}` 选择出的顶层 item 集合。
  - 消费：AST/HIR、resolve facts、type facts、target facts。
  - 禁止：执行运行时副作用、生成 MIR/backend 节点、把 `ComptimeValue` 泄漏到 backend。
- `monomorph`
  - 生产：concrete generic instance 集合。
  - 消费：type facts、HIR generic template。
  - 禁止：生成目标代码、修改 type facts。
- `mir`
  - 生产：CFG、local、place、rvalue、terminator。
  - 消费：HIR、type facts、builtin operation lowering kind、monomorph、layout query。
  - 禁止：重新 resolve/type check、按源码文本重新判断 builtin operation、写 backend symbol。
  - 备注：`Trait.Any` 动态调用、`Trait.VTable` slot 和 `Trait.Receiver` 构造在 MIR lowering
    中消费 type check 已选出的 companion facts，不在 MIR 里重新做 trait lookup。
- `layout`
  - 生产：size、align、field index、ABI representation，包括 `Trait.Any` / `Trait.VTable` /
    `Trait.Receiver` 的 erased runtime representation。
  - 消费：TypeId、type facts、target data layout。
  - 禁止：类型推导、读取 MIR 控制流、插入 drop。
- `borrow_check`
  - 生产：borrow/drop safety 结果。
  - 消费：MIR、type facts、layout。
  - 禁止：修改 HIR/type facts、处理数据竞争策略。
- `drop_elaborate`
  - 生产：elaborated MIR CFG。
  - 消费：MIR、borrow store、drop/layout query。
  - 禁止：重新判断类型规则、生成 backend-only 节点。
- `backend`
  - 生产：LLVM IR、object、executable。
  - 消费：elaborated MIR、layout、target、symbols。
  - 禁止：语言语义判断、HIR fallback、修改 MIR/layout。
- `incremental`
  - 生产：`StableKey`、fingerprint、source interface / HIR template / object artifact metadata、
    package-level artifact key/path。
  - 消费：source、interface、object artifact。
  - 禁止：缓存 session-local HIR/type/MIR 对象。

如果某个实现需要违反上述 contract，优先修改前一阶段产出的事实，
而不是在后一阶段补临时逻辑。

### CompilerContext 与 CompilerStore

`CompilerContext` 是一次编译请求的运行上下文，保存配置、target、输出层和统一 store。
它不直接平铺业务事实表。

```text
CompilerContext
  options
  target
  reporter
  store: CompilerStore
```

`CompilerStore` 是所有业务事实集合的生命周期所有者。长期事实和单次 compilation cache 都挂在
这里，但每个 store 自己声明生命周期和失效规则。

```text
CompilerStore
  diagnostics
  sources
  source_map
  asts
  symbols
  keywords
  defs
  resolve
  hirs
  types
  typeck
  comptime
  monomorph
  layouts
  mirs
  borrow_check
  artifacts
  incremental
```

`DiagnosticStore` 放在 `CompilerStore` 内，因为它保存编译过程中产生的诊断事实。
`DiagnosticReporter` 不放进 `CompilerStore`，它只负责终端输出和未来 LSP 消息发布。

### Store 规则

- `CompilerStore` 是业务事实集合的生命周期所有者。
- `CompilerContext` 不直接平铺业务 store；所有业务 store 都通过 `ctx.store` 访问。
- 阶段产物只有确实被多个后续阶段消费时才挂入 `CompilerStore`。
- `syntax.Store` 是单次 compilation 的 parse cache，不作为跨阶段长期语义 store。
- `ResolveStore` 保存 package、module、namespace、import/export 和 def store，是名字事实 owner。
- `HirStore` 保存每个 `DefId` 的 HIR signature/body，是 HIR 事实 owner。
- `TypeStore` 保存 `TypeId -> TypeInfo` 的类型实体。
- `TypeCheckStore` 保存 node/def/call/pattern 的类型事实，不和 `TypeStore` 合并所有权语义。
- `ComptimeStore` 保存 `DefId -> ComptimeValue` 的编译期常量事实。它只服务 sema、
  public interface artifact 和 HIR->MIR lowering；MIR 之后的阶段只能看 `MirConst`、
  `MirGlobal` 和 `MirStaticValue`。
- `src/core` 是 compiler-known 源码入口，不作为用户可 import package；core 中的
  body-less trait implementation 只声明 builtin type 的 trait 关系，具体 lowering 仍由
  type check / MIR / layout 的 compiler-known facts 承接。
- `LayoutStore` 独立保存 concrete type layout；layout 不是 type check store 的一部分。
- `MirStore` 保存 MIR function/body；backend 不维护一份等价 MIR。
- `IncrementalSymbolStore` 保存 stable id 和当前 session id 的映射，不保存语义对象本体。

### Pass 规则

- 每个 pass 必须有明确输入和输出，不能顺手修复其他阶段遗漏的语义。
- pass 可以查询上游事实，但不能修改上游事实表。
- pass 修改 MIR 时只产生普通 MIR block、statement 和 terminator。
- backend 只能消费最终 elaborated MIR。
- backend 不消费 `ComptimeValue`。标量 const 必须在 MIR lowering 前降成 `MirConst`；
  复合 const 作为运行时值使用时，必须先 materialize 成 readonly `MirGlobal`。
- 如果 backend 需要理解语言级结构，说明 MIR 还没有表达清楚。
- layout query 可以被 MIR、borrow/drop 和 backend 使用，但 field offset/size/align 只能来自
  `LayoutStore`。

### 开发检查清单

新增或修改一个语言功能前，先回答：

1. 这个语义属于哪个阶段？
2. 这个阶段新增或修改哪张事实表？
3. 下游阶段能否只靠这些事实工作？
4. 是否引入了 session-local ID 到 artifact/cache？
5. 是否把 source span 当成身份或匹配条件？
6. 是否让 backend、layout 或 parser 承担了不属于它的语义？
7. 是否需要新增 lang test、smoke test 或 artifact/incremental test？

回答不清楚时，先调整阶段 contract 或数据模型，再写实现。

## 阶段边界

- `driver` 把进程参数转换成编译请求，直接创建 `CompilerContext` 并调用 pipeline。
- `pipeline` 以 package root file 为入口串联各阶段，并负责跨阶段错误处理。目录入口读取
  `package.ini`，文件入口把该文件作为 root source。
- `source` 负责 package manifest、路径处理、文件读取和 source ID。
- `core` 在 root module 前加载，提供 compiler-known trait、builtin type namespace 壳和
  intrinsic 声明；它不参与用户 import 解析。
- `syntax` 只产生 token 和 AST；详见 [AST 设计](compiler/ast.md)。
- `diagnostic` 负责诊断数据结构、终端输出和未来 LSP 位置转换。
- `source_map` 属于 `source` 模块，保存 `DefId` / `HirId` 到源码 span 的定位事实。
- `resolve` 负责 import、module graph、namespace 和名字解析，并直接生成 HIR；
  详见 [Resolve 设计](compiler/resolve.md)。
- `hir` 包含 resolved、未类型化的语义树；详见 [HIR 设计](compiler/hir.md)。
- `sema` 负责类型检查、trait、generic、overload 和类型转换；
  详见 [Type Check 设计](compiler/type-check.md)。
- `monomorph` 运行在 type check 之后，负责收集 concrete generic instances；
  详见 [Monomorph 设计](compiler/monomorph.md)。
- `mir` 包含 MIR 数据定义、HIR -> MIR lowering 和 drop elaboration；MIR lowering 可以查询
  layout 做布局相关的 representation 决策，但 layout 仍由 `layout` 模块统一计算；
  详见 [MIR 设计](compiler/mir.md)。
- `layout` 负责 concrete type layout 查询和缓存；详见 [Layout 设计](compiler/layout.md)。
- `borrow_check` 消费 MIR、`TypeCheckStore` 和 layout；详见
  [Borrow Check 设计](compiler/borrow-check.md)。
- `backend` 把 elaborated MIR 和 layout 转成 LLVM IR、object file 或可执行产物；
  详见 [Backend 设计](compiler/backend.md)。
- `incremental` 负责 hashing、cache key、依赖图和复用策略；详见
  [Incremental Compilation 设计](compiler/incremental.md)。
- `lang` 负责 `type = lang` provider discovery、wrapper dylib 构建、host dylib 加载和
  syntax-stage provider invocation；详见 [DSL / Lang Package](compiler/dsl.md)。
- `artifact` 保存 source/interface/object/package artifact 的 key、fingerprint、path 和物理容器
  适配；package-level 产物通过 `package_fingerprint` 和 `package_artifact` 统一失效规则。
- `store` 是跨阶段事实集合聚合点；普通阶段通过 store API 查询，
  不直接依赖其他阶段内部表。
- `support` 只放可复用容器和工具，不 import 编译阶段模块。

## CompilerStore

`CompilerStore` 是跨阶段事实集合的生命周期所有者。具体 entry/key 类型仍由 owner 模块定义。

- `store/api.jiang` 定义 `CompilerStore`。
- `resolve/interner.jiang` 定义 `SymbolStore`；关键字分类是 symbol 的附加事实。
- `resolve/def.jiang` 定义 `DefKind`、`Visibility`、`NameDomain`、`DefRecord` 和 `DefStore`。
- `resolve/namespace.jiang` 定义持久 namespace、namespace binding 和 lookup key。
- `resolve/store.jiang` 组合 package/module、import/export 和 namespace store。
- `ResolveStore.modules` 是 `ModuleId -> ModuleRecord` 主表。
- `ResolveStore.source_modules` 是 `SourceId -> ModuleId` 的辅助 store。
- `HirStore`、`TypeStore`、`TypeCheckStore`、`LayoutStore` 和 `IncrementalSymbolStore`
  都挂在 `CompilerStore`。
- `MonomorphStore`、`MirStore`、`ModuleGraph` 和 `BorrowCheckStore` 是单次 pipeline
  调用中的阶段产物。
- `syntax.Store` 是一次 `compile_package` 的临时 AST cache，不挂入 `CompilerStore` 长期状态。
- cache-backed query dependency tracking 后续按实际 artifact cache 需求继续收敛；当前不保留未接入的 cache 骨架。
- 后续需要缓存或依赖追踪的跨阶段问题，再在 `store/api.jiang` 增加高阶查询入口。

## 源码目录

```text
src/
  driver/       CLI 参数和命令入口
  source/       package、source file、source manager、source map
  syntax/       token、lexer、parser、flat AST
  lang/         lang package registry、wrapper dylib、provider runtime bridge
  artifact/     source .ji、object key、package artifact key/path、fingerprint
  diagnostic/   diagnostic、reporter
  core/         compiler-known core 源码入口、builtin trait/type 外壳、intrinsic 声明
  resolve/      symbol store、keyword store、import/module/name resolver
  sema/         type store、trait、generic、overload、type check、monomorph
  hir/          HIR 数据结构和 HIR store
  mir/          MIR 数据结构和 HIR -> MIR lowering
  layout/       concrete type layout 查询层
  borrow_check/ ownership、loan、lifetime 和 drop safety 检查
  backend/      target 和后端入口
  incremental/  cache key、fingerprint、依赖图、symbol store
  store/        compiler store、cache、id、key
  support/      arena、list、hash、unicode 等通用工具
```

## Support 容器

业务事实集合统一命名为 `Store`。`support` 只提供底层容器实现，
不建立额外的业务层命名分类。

- `HashTable<K, V>`：复杂 key 到 value 的 hash 映射；key 必须满足 `Hashable`，
  而 `Hashable` 继承 `Equatable`；`get` 返回 `V?`，`get_ref` 返回 `V&?`。
- `ArrayTable<Id, T>`：append-only 实体主表；`next_id` 和 `append`
  通过 `Indexable.from_index` 返回强类型 Id。
- `SideTable<Id, T>`：已有 Id 到附加数据的直接索引表，适合半稠密 side data。
- `MultiTable<K, V>`：一个 key 对多个 value，适合同名符号链、依赖边和反向索引。
- `InternTable<K, V>`：去重驻留表，维护 `K -> index` 和 `index -> V`。

`Indexable` 负责强类型 Id 和连续下标之间的转换：

```jiang
public trait Indexable {
    Int to_index();
    static Self from_index(Int index);
}
```

## 代码规范

这部分记录编译器源码本身的命名和组织规则。语言用户侧的语法设计放在
[语言设计](language-design.md)。

### 命名

- 类型、trait、enum、union、struct 使用 `UpperCamelCase`。
- 函数、方法、局部变量、字段、模块文件名使用 `lower_snake_case`。
- 常量使用 `SCREAMING_SNAKE_CASE`。
- 缩写词按普通单词处理，只首字母大写：
  - 使用 `CompilerStore`，不要用 `CompilerSTORE`。
  - 使用 `ModuleId`、`DefId`、`AstId`、`MirId`。
  - 使用 `LspServer`、`Utf8`、`Utf16`。
- 文件名使用 lower snake case：`source_manager.jiang`、`type_check.jiang`。
- 数据/模型文件优先使用名词：`token.jiang`、`type.jiang`。

### 后缀语义

- `*Id`：轻量句柄，通常实现 `Indexable`。
- `*Record`：实体表中的存储行。
- `*Entry`：lookup bucket、链表或临时索引中的条目。
- `*Key`：可 hash、可缓存或可持久化的查询键。
- `*Store`：业务事实集合；保存数据并按 key/id 查询数据的结构都使用这个后缀。
- `*System`：跨阶段状态聚合和生命周期所有者。

### 代码风格

- 不为无效状态增加默认构造；确实可能缺失时使用 optional。
- 不用 `none`、`invalid`、`-1` 这类哨兵表达正常业务缺失。
- 注释使用中文，解释结构意图和不变量，不复述代码。
- 单行不超过 120 个字符。
- 新增抽象必须降低真实复杂度，不能只为了“看起来分层”。

## 导入纪律

优先保持单向 import。如果两个模块互相需要，应把共享数据结构抽到更底层的
所有者里，而不是引入循环依赖。

## 测试目录

- `test/smoke/`：编译器内部模块和端到端 smoke，由脚本通过 `JIANGC` 指定被测编译器；
  稳定自举验证使用 `build/jiangc.next` 或 `build/jiangc`。
- `test/compiler/`：按编译阶段归档的测试目录，当前以 `.gitkeep` 保留结构。
- `test/compiler/fixture/`：编译器阶段测试的辅助输入。
- `test/lang/`：源码级语言语义用例，和 `test/smoke` 的内部模块 API 测试分开。
  目录按语言功能优先组织，每个功能目录内部再按测试结果类型分组；覆盖策略见
  [Language Testing 设计](compiler/lang-testing.md)。

`test/lang` 当前按语言功能组织，每个功能目录内部再按结果类型组织：

```text
test/lang/
  aggregate/
    check/
    fail/
  control_flow/
    check/
    fail/
  error_handling/
    check/
    fail/
  function/
    check/
    fail/
    run/
  generic/
    check/
    fail/
    run/
  import/
    check/
    fail/
  package/
    check/
    fail/
    run/
  lifetime/
    check/
  literal/
    check/
    fail/
    run/
  nominal/
    check/
    fail/
  ownership/
    check/
    fail/
    run/
  runtime/
    fail/
    run/
  type/
    check/
    fail/
  diagnostic/
```

- `check/`：期望 `jiangc --check` 成功。
- `fail/`：期望 `jiangc --check` 失败，可用 `// expected: diagnostic_code` 精确匹配诊断。
- `emit/`：期望 `jiangc --emit-llvm` 成功。
- `run/`：需要生成并运行目标程序的端到端用例，可用 `// expected-exit: N` 匹配退出码。
- `diagnostic/`：后续用于精确检查多条 diagnostic、span 和消息的用例。

运行方式：

```bash
JIANGC=/path/to/jiangc ./script/lang_check.sh
```

`lang_check.sh` 递归扫描 `*/check/*.jiang`、`*/fail/*.jiang`、`*/emit/*.jiang` 和
`*/run/*.jiang`，每个用例都会打印通过/失败状态。
