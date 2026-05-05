# Jiang 增量编译与符号表设计方案

## 核心目标

- 修改一个源文件后，只重新编译**受影响的模块**，而不是整个项目
- 符号表支持按模块分区、序列化到磁盘、下次编译直接从缓存加载
- 依赖追踪细化到**声明级别**（而非模块级别），函数体变更不影响只依赖签名的模块

---

## 1. 稳定 ID 体系

所有语义实体用全局唯一整数 ID 标识。ID 在增量重编译中**保持稳定**——未变更的声明保留原 ID，只回收被删除声明的 ID。

### ID 种类独立编号

每种 ID 拥有**自己独立的全局计数器**，互不重叠。例如 `DeclId=5` 和 `TypeId=5` 是两个不同的实体，分别位于声明表和类型表中。
编译器通过**字段类型**（`DeclId` vs `TypeId`）在编译期区分，没有任何运行时开销或歧义。

```
NameId    — 驻留字符串索引 (global, 永不过期)         ← 独立的名称表
ModuleId  — 源文件/模块标识                           ← 独立的模块表
DeclId    — 顶层声明 (struct/enum/function/global/…)  ← 独立的声明表
TypeId    — 类型 (nominal 或 structural)              ← 独立的类型表
ScopeId   — 词法作用域, 仅 resolve 阶段使用, 不跨模块引用, 不序列化, 非全局表
BodyId    — 函数体/初始化器, 仅 lower/codegen 阶段使用, 不跨模块引用, 不序列化, 非全局表
LocalId   — 局部变量, 作用域限于一个 BodyId, 不跨模块引用, 不序列化, 非全局表
            (LocalId = BodyInfo.locals 中的下标)
```

如果将来需要一个统一的 "任意语义实体引用"，可以用 tagged union：

```
SemanticId :: DeclId | TypeId | ScopeId | BodyId | LocalId
```

但日常使用中直接通过字段类型区分更简洁高效。

### DeclId 稳定性机制

DeclId = 全局递增整数。每个 Decl 记录 `owner_module: ModuleId`。

当模块 M 被重新编译时：
- **签名未变的声明**：保留原 DeclId，覆盖其 DeclInfo
- **新增的声明**：分配新的 DeclId
- **被删除的声明**：DeclId 进入 free-list，标记为 deleted（供下次复用）

依赖方检查依赖的 DeclId：如果 DeclId 还在且签名未变，则无需重新类型检查。

---

## 2. 表结构 (Tables)

全局表以稠密数组存储，按 ID 下标 O(1) 访问，条目记录 `owner_module` 以支持按模块分区和序列化。
非全局表仅在各阶段临时使用，不跨阶段存活，不序列化。

### 全局持久表 (序列化到 .jiang_cache/)

> **为什么声明表和类型表要分开？**
> 
> - **声明表**存源级实体，条目大小差异大（struct 有 fields vec，function 有 params vec），数量等于源文件中声明的总数。extend/trait impl 的方法也各自为独立声明，不另建全局方法表
> - **类型表**存语义类型，条目小而均匀（kind 鉴别 + 少量字段），泛型实例化会产生大量条目（`Vec<i32>`、`Vec<String>`…），数量远大于声明数
> - 分开后各自紧凑、按需序列化（meta.bin / types.bin）、查询模式各不同

#### 2.1 名称表 (NameId → String)

```
names: Vec<UInt8[]>   // 驻留字符串池，永不回收
// 等价于现有的 Interner
```

#### 2.2 模块表 (ModuleId → ModuleInfo)

```
ModuleInfo:
    source_path: NameId            // 相对于项目根目录的源文件路径 (驻留), 例 "src/main.jiang"
    source_hash: UInt64            // 文件内容哈希 (用于变更检测)
    signature_hash: UInt64         // 该模块所有导出声明签名的哈希 (用于快速判断影响范围)
    deps: Vec<DepModule>           // 直接依赖的模块列表
    reverse_deps: Vec<ModuleId>    // 被哪些模块依赖 (反向边)
    decl_range: (DeclId, DeclId)   // 该模块拥有的声明 ID 区间 [start, end)
    type_range: (TypeId, TypeId)   // 该模块拥有的类型 ID 区间
    exports: HashMap<NameId, DeclId>  // public 导出的 name → DeclId
    status: ModuleStatus           // empty / parsed / resolved / type_checked / lowered / codegened

DepModule:
    module_id: ModuleId            // 依赖的模块
    snapshot_signature_hash: UInt64  // 上次 type_check 时所见依赖方的签名哈希
                                     // 冷启动后: 若依赖方新 signature_hash ≠ snapshot, 本模块变脏
```

### 2.3 声明表 (DeclId → DeclInfo)

```
DeclInfo:
    kind: DeclKind          // struct / enum / union / trait / function / global / alias / extend
    name: NameId
    owner_module: ModuleId
    span_start, span_end: Int    // 源文件字节偏移
    visibility: Visibility  // public / private
    extern_flag: Bool
    
    // 类型声明专用：
    type_id: TypeId         // 该声明定义的类型 (struct/enum/union 自身对应的 TypeId)
    
    // 结构体专用：
    fields: Vec<FieldInfo>  // 字段名 + TypeId + 默认值
    
    // 枚举专用：
    members: Vec<EnumMemberInfo>  // 成员名 + 判别值
    
    // Union 专用：
    variants: Vec<UnionVariantInfo>  // 变体名 + TypeId
    
    // 函数专用：
    func_sig: FuncSig       // 参数类型列表 + 返回类型
    func_params: Vec<ParamInfo>  // 参数名 + TypeId + mutable
    
    // Trait 专用：
    assoc_types: Vec<AssocTypeEntry>
    trait_methods: Vec<TraitMethodSig>
    
    // 全局变量专用：
    global_type: TypeId
    mutable: Bool
    
    // 泛型：
    generic_params: Vec<GenericParam>  // 泛型参数名 + 种类
    
    // 扩展块：
    extend_target: TypeId   // extend 的目标类型
    extend_methods: Vec<DeclId>  // extend 内定义的方法 DeclId
    
    // 依赖追踪 ↓↓↓
    deps: Vec<DepEdge>      // 此声明依赖哪些 DeclId/TypeId
```

### 2.4 类型表 (TypeId → TypeInfo)

```
TypeInfo:
    kind: TypeKind
    
    // Nominal 类型 (struct/enum/union/trait):
    decl_id: DeclId           // 指向声明
    generic_args: Vec<TypeId> // 泛型实参
    
    // Structural 类型:
    pointer:   PointerType { pointee: TypeId, mutable: Bool }
    slice:     SliceType { element: TypeId }
    array:     ArrayType { element: TypeId, length: Int }
    tuple:     TupleType { elements: Vec<TypeId> }
    function:  FuncSig { params: Vec<TypeId>, result: TypeId }
    optional:  OptionalType { inner: TypeId }
    // ... etc
    
    // 缓存 (用于快速结构等价比较):
    hash: UInt64
```

### 阶段内临时表 (不序列化, 不跨阶段存活)

#### 2.5 作用域表 (ScopeId → ScopeInfo)

```
ScopeInfo:
    kind: ScopeKind       // module / type_body / function_body / block / loop / match_arm
    parent: ScopeId?      // 父作用域
    owner_decl: DeclId?   // 所属声明 (函数体 → 函数 Decl, 类型体 → struct Decl)
    owner_module: ModuleId
    bindings: HashMap<NameId, BindingEntry>  // 该作用域内的名称绑定
    
BindingEntry:
    decl_id: DeclId?      // 绑定到声明 (顶层函数/类型)
    local_id: LocalId?    // 绑定到局部变量
    type_id: TypeId?      // 绑定到类型别名
```

### 2.6 体表 (BodyId → BodyInfo)

```
BodyInfo:
    owner_decl: DeclId          // 所属声明
    owner_scope: ScopeId
    locals: Vec<LocalInfo>      // 局部变量列表
    
    // HIR/JIR 存储 — 不在符号表本身，而是引用 IR 中的 StmtId/ExprId
    stmt_ids: Vec<StmtId>
    
LocalInfo:
    name: NameId
    type_id: TypeId
    mutable: Bool
    scope: ScopeId       // 局部变量所属的最内层作用域
```

### 2.7 全局表序列化策略

> **核心原则：每模块只重写自己的缓存文件，不改动其他模块的缓存。**

| 表 | 序列化方式 | 变更代价 |
|---|---|---|
| 名称表 | `names.bin`，append-only（新 intern 字符串追加写） | 追写新名字 |
| 模块表 | `manifest.bin`（id→source_path 映射，极轻） | 几十字节 |
| 声明表 | 每个模块写自己的 `meta.bin`（只含本模块 decls） | 只重写变更模块 |
| 类型表 | 每个模块写自己的 `types.bin`（只含本模块 types） | 只重写变更模块 |

---

## 3. 细粒度依赖追踪

### 3.1 DepEdge 结构

```
DepEdge:
    target_decl: DeclId?       // 依赖的声明 (函数、类型)
    target_type: TypeId?       // 直接依赖的类型
    kind: DepKind              // 依赖种类

DepKind:
    signature       // 仅依赖签名 (调用函数时依赖其参数/返回类型)
    type_layout     // 依赖类型布局 (字段访问、sizeof、alignof)
    body            // 依赖函数体 (内联、泛型实例化展开时)
```

### 3.2 依赖记录时机

| 阶段 | 记录的依赖 |
|------|-----------|
| Name Resolution | 每个名称引用 → 被引用 DeclId (kind=signature) |
| Type Check | 字段访问 → 字段类型 (kind=type_layout); 函数调用 → 函数签名 (kind=signature) |
| Monomorphization | 泛型实例化 → 源声明 + 所有实参类型 (kind=body) |

### 3.3 变更传播

当模块 M 中的声明 D 发生变化时：

```
1. 计算 D 的 "影响指纹"：
   - body 变了但 signature 没变 → 影响级别: LOW
   - signature 变了 → 影响级别: HIGH
   - type_layout 变了 → 影响级别: HIGH

2. 查找所有 decs[*].deps 中包含 D 的其他声明

3. 对每个受影响的声明 D'：
   - 如果 D 的影响级别是 LOW 且 D' 的依赖 kind 是 signature:
     → D' 不需要重新类型检查，但可能需要重新 codegen (如果 D' 内联了 D)
   - 否则:
     → D' 的所属模块需要重新类型检查
```

---

## 4. 编译流水线 (增量模式)

```
┌─────────────────────────────────────────────────────┐
│              增量编译入口                             │
│  changed_files = detect_changes(source_hashes)       │
└─────────────────────┬───────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────┐
│  Phase 1: 加载缓存                                   │
│  for each unchanged module:                          │
│    load from .jiang_cache/ → 恢复 DeclId, TypeId,    │
│    exports, deps                                     │
│  for each changed module:                            │
│    re-parse source → AST                             │
└─────────────────────┬───────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────┐
│  Phase 2: 依赖传播                                   │
│  dirty_set = changed_modules                         │
│  for each dirty_module in topological order:         │
│    for each dep_edge in dirty_module.affected_decls: │
│      if dep_edge.indicates_need_recheck:             │
│        dirty_set.insert(dep_edge.target_module)      │
└─────────────────────┬───────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────┐
│  Phase 3: 名称解析 + 类型检查 (仅 dirty 模块)        │
│  for each module in dirty_set (topological order):   │
│    - resolve names using exports of imported modules │
│    - compare resolved signatures with old (缓存)     │
│    - for unchanged decls: reuse old DeclId + TypeId  │
│    - for changed decls: allocate new DeclId/TypeId   │
│    - type check + record new deps                    │
└─────────────────────┬───────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────┐
│  Phase 4: Lower + Codegen (仅 dirty 模块)            │
│  for each module in dirty_set:                       │
│    - lower to HIR → JIR                              │
│    - codegen to LLVM IR                              │
│    - compile LLVM IR → .o                            │
│  link old .o + new .o → final binary                 │
│  update .jiang_cache/                                │
└─────────────────────────────────────────────────────┘
```

---

## 5. 缓存格式 (`.jiang_cache/`)

### 目录结构

```
.jiang_cache/
  modules/
    <module_id>/          # 每个模块一个目录
      meta.bin            # ModuleInfo + DeclInfo[] 序列化
      types.bin           # 该模块拥有的 TypeInfo[]
      module.o            # LLVM 编译产物 (.o 文件)
  manifest.bin            # 全局: module_id → source_path, 全局计数器, free-list
```

> **为什么没有 hir.bin 和 jir.bin？** HIR/JIR 只是 lower 的中间产物，生成 .o 后就可以丢弃。.o 有效时直接链接，.o 失效时重跑 lower→codegen（lower 是已解析 AST 的线性转换，成本远低于 type_check），缓存它们不划算。

### meta.bin 内容

```
ModuleMetadata:
    module_id: u32
    source_path: string
    source_hash: u64
    signature_hash: u64
    exports: [(NameId, DeclId)]
    deps: [(module_id: u32, snapshot_signature_hash: u64)]
    decls: [
        for each decl owned by this module:
            DeclInfo (所有字段)
    ]
```

### manifest.bin 内容

```
Manifest:
    version: u32                   // 缓存格式版本
    modules: [(u32, string)]       // module_id → source_path
    next_decl_id: u32              // 全局声明 ID 计数器
    next_type_id: u32              // 全局类型 ID 计数器
    free_decl_ids: [u32]           // 被删除声明的 ID，复用优先
    free_type_ids: [u32]           // 被删除类型的 ID
```

### 重要约束

- **缓存中不存储 AST 引用**：`ast.AstType` → 全部替换为 `TypeId`，`ast.Path` → 替换为 `DeclId`
- **不存储裸指针**：所有引用用 ID
- **缓存独立于 Arena 内存布局**：反序列化后可以放入新的 Arena 或堆分配的内存

---

## 6. 实施路线

### Phase A: AST 引用移除 (IR 净化，必须先做)

**当前问题**：HIR 和 JIR 定义中残留 `ast.AstType` 和 `ast.Path`。

**正确设计**：
- `type_check` 之后，`AstType` 已解析为 `TypeId`——HIR 不应再持有原始 AST 类型节点
- `lower_hir` 负责完成 AST 类型 → TypeId 的转换，HIR 中只存 TypeId
- JIR 只依赖 HIR 的结果，不直接引用 AST（AST 指针在增量编译中已失效）

**实施步骤**：
1. HIR 定义：`ast.AstType` → `TypeId`（字段类型体、coalesce 错误类型、cast 目标类型等）
2. HIR 定义：`ast.Path` → `DeclId`（结构体表达式路径、变体表达式路径、模式匹配路径）
3. `lower_hir` 负责所有 AST→ID 的转换，转换后 HIR 不引用任何 AST 节点
4. JIR 定义：移除所有 `ast.AstType` 和 `ast.Path` 字段，直接使用 `TypeId` 和 `DeclId`（这些值由 HIR 提供，JIR 只管消费）
5. 这是后续所有 Phase 的前提——序列化到 `.jiang_cache/` 的数据必须独立于 AST 内存布局

### Phase B: 稳定 ID 体系 + 全局符号表

1. 定义所有 ID 类型 (ModuleId / DeclId / TypeId / ScopeId / BodyId / LocalId)
   和表结构（模块、声明、类型）
2. 实现全局表：模块表、声明表、类型表 (共 3 张)
   实现阶段内临时表：作用域表、体表 (不序列化)
3. 实现序列化/反序列化 (二进制格式)
4. 实现 `.jiang_cache/` 读写
5. 编译后写出缓存，下次编译加载缓存 (先全量模式，验证正确性)

### Phase C: 依赖图与变更检测

1. 在 resolve + type_check 阶段记录 DepEdge
2. 实现文件哈希变更检测
3. 实现脏模块传播算法
4. 实现"只重编译脏模块"(但仍全量 link)

### Phase D: 缓存粒度收紧

1. 对未变更声明复用 DeclId/TypeId
2. 反序列化后直接获得类型信息，跳过 resolve
3. 对缓存命中的模块跳过 type_check

---

## 7. 关键设计决策

### 为什么 DeclId 全局递增，而不是 `(ModuleIndex, LocalIndex)` 的 packed id？

- 全局递增 id 在依赖追踪时不需要解包
- deleted 声明用 free-list 回收，避免 id 膨胀
- 跨模块引用是一种常态 (函数 A 调用 B，B 在另一个模块)

### 为什么 HIR/JIR 必须移除 AST 引用？

- AST 在每次 parse 后内存地址不同，依赖 AST 指针的序列化毫无意义
- `TypeId` 是稳定的——类型 `i32` 永远是同一个 TypeId
- `DeclId` 是稳定的——未变更的函数声明保留原 DeclId

### Module 级别的 signature_hash 是什么？

```
signature_hash = hash(
    for each exported decl:
        decl.hash_signature()  // 仅哈希签名，不含 body
)
```

如果 signature_hash 不变，依赖模块的**名称解析结果**不变 (名字指向同样的 DeclId，签名也相同)。此时只需重新 codegen 本模块，不需要重新 type_check 依赖模块。

### trait / extend 的方法如何查找？

不建全局方法表。extend 声明是独立的 `DeclInfo`（`kind = extend`），序列化在自己的 `owner_module` 缓存中。方法查找在 type_check 时按模块进行：

1. 类型体内定义的方法（`struct Point { fn x() ... }`）
2. 当前模块 **可见的** extend 块（按 `extend_target` 匹配目标类型）
3. 当前模块 **可见的** trait impl

`private` extend 只在 `owner_module` 内可见，不会污染其他模块的类型视图。模块 B 的 private extend 对模块 C 不可见——无需将方法注入目标类型的全局数据结构。

增量编译：extend 声明变更时，该声明本身的 DepEdge 会触发依赖方（引用了该 extend 的模块）重新 type_check。
