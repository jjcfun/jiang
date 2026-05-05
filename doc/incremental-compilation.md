# Jiang 增量编译设计

本文档描述 Jiang 编译器的增量编译方案。目标是先得到可落地、可验证的开发期加速，再逐步收紧到声明级和函数级粒度。

核心原则：

- `DeclId` / `TypeId` 不能裸复用，避免旧依赖误指向新实体。
- 模块不能用连续 ID range 表示拥有的声明和类型。
- 磁盘缓存不能依赖全局顺序 ID；顺序 ID 只作为内存表下标。
- Jiang 的 module 是单个 `.jiang` 文件；package 是 module 集合和依赖边界，缓存必须按 package/module 分片。
- 依赖传播必须有反向索引，不能每次扫描所有声明。
- `signature_hash` 不能承担所有 API 变化判断，需要拆成多类指纹。
- `extend` / `trait impl` 需要方法集合索引，否则方法查找和失效传播都会变慢。

---

## 1. 阶段目标

### 目标 1: 模块级增量

修改一个源文件后，只重新处理该模块和受影响模块。

能力范围：

- 文件 hash 变更检测。
- 模块 cache 加载。
- public API hash 比较。
- module-level dirty propagation。
- 未变模块复用旧 `module.o`。

这一阶段仍然以模块为 codegen 单元。大文件内改一个函数，仍会重新生成该模块的 `.o`，但不会重新编译整个项目。

### 目标 2: 声明级语义增量

把模块内变化缩小到声明级。

能力范围：

- 顶层声明稳定 ID。
- 每个声明独立记录 `signature_hash`、`body_hash`、`layout_hash`。
- 依赖图精确到 `DeclSig` / `DeclBody` / `TypeLayout` / `MethodSet`。
- 函数体变更不传播到只依赖签名的调用方。

### 目标 3: 函数级 codegen 增量

把 codegen 从模块 `.o` 收紧到函数/声明。

能力范围：

- 函数级 codegen cache。
- 未变函数复用旧 object section 或旧 backend artifact。
- 最终链接仍可全量，先不做增量 linker。

---

## 2. ID 策略

ID 分两层：

- **内存 ID**：编译进程内的快速数组下标，例如 `ModuleId`、`DeclId`、`TypeId`。
- **磁盘 stable key**：跨编译缓存使用的稳定身份，例如 package path、module path、qualified name、type key。

磁盘缓存里可以保存 ID 作为某个文件内部的压缩引用，但不能把它当作跨编译会话的真实身份。冷启动时必须先用 stable key 重建内存 ID 映射。

### 2.1 ID 类型

```
PackageId   // 当前编译进程内的 package
NameId      // 驻留字符串
ModuleId    // 源文件/模块
DeclId      // 顶层声明、extend、trait impl、方法
TypeId      // nominal / structural / generic instance 类型
```

阶段内临时 ID 不序列化：

```
ScopeId     // resolve 阶段临时作用域
BodyId      // 当前编译进程内的函数体/initializer
LocalId     // BodyId 内局部变量下标
```

### 2.2 不裸复用 ID

`DeclId` 和 `TypeId` 不能简单进入 free-list 后复用。旧依赖图、旧 `.o`、IDE 状态、诊断缓存可能仍然保存旧 ID。复用会导致旧实体被误认为新实体。

初始实现采用最简单策略：

```
DeclId / TypeId 只递增，不复用。
删除的实体标记为 tombstone。
```

如果以后确实需要回收 ID，必须改成：

```
EntityId:
    index: u32
    generation: u32
```

所有依赖边必须同时比较 `index` 和 `generation`。

### 2.3 Stable Key

package、module、decl、type 在磁盘上都使用 stable key。

```
PackageStableKey:
    canonical_package_root: String

ModuleStableKey:
    package: PackageStableKey
    relative_path: String       // package root 下的路径，例如 "src/main.jiang"

DeclStableKey:
    module: ModuleStableKey
    owner: DeclOwnerKey
    name: String                // 例如 "parse"
    kind: DeclKind
    generic_arity: UInt32
    parameter_labels: Vec<String>
    overload_key: String        // 参数标签、arity 和必要的消歧信息

DeclOwnerKey:
    module(ModuleStableKey)
    type_decl(DeclStableKey)
    trait_decl(DeclStableKey)
    extend_decl(ExtendStableKey)

ExtendStableKey:
    module: ModuleStableKey
    target_type: DiskTypeKey
    trait: DeclStableKey?
    source_anchor: String       // span 附近的规范文本 hash 或序号，用于同目标多 extend 消歧

TypeStableKey:
    builtin(name)
    nominal(decl: DeclStableKey, generic_args: Vec<TypeStableKey>)
    structural(key: DiskTypeKey)
    generic_instance(generic_decl: DeclStableKey, args: Vec<TypeStableKey>)

DiskTypeKey:
    builtin(name)
    nominal(decl: DeclStableKey, generic_args: Vec<DiskTypeKey>)
    pointer(pointee: DiskTypeKey, mutability)
    reference(pointee: DiskTypeKey, mutability)
    slice(element: DiskTypeKey)
    array(element: DiskTypeKey, length: UInt64)
    tuple(elements: Vec<DiskTypeKey>)
    function(params: Vec<DiskTypeKey>, result: DiskTypeKey)
    optional(inner: DiskTypeKey)
    errorable(value: DiskTypeKey, error: DiskTypeKey)
    generic_instance(generic_decl: DeclStableKey, args: Vec<DiskTypeKey>)
```

初始实现不需要把 `TypeStableKey` 完整序列化到所有位置；可以在 `provides.bin` / `depends.bin` 中使用 compact string 或 hash + debug name。关键原则是：磁盘依赖边不依赖内存 `TypeId`。

### 2.4 稳定声明匹配

模块重新 parse 后，需要把新声明和旧声明对齐。不能只按源码顺序匹配。

每个声明保存：

```
DeclStableKey:
    module: ModuleStableKey
    owner: DeclOwnerKey
    name: String
    kind: DeclKind
    generic_arity: UInt32
    parameter_labels: Vec<String>
    overload_key: String
```

匹配规则：

- `stable_key` 相同且声明种类兼容：保留旧 `DeclId`。
- `stable_key` 新出现：分配新 `DeclId`。
- 旧 `stable_key` 消失：旧 `DeclId` tombstone。

重命名在初始实现中视为删除 + 新增。

`overload_key` 不要求在函数签名变化时保持不变。初始实现可以把签名变化视为旧 overload 删除、新 overload 新增；后续如果需要更强的 rename/signature-change 跟踪，再引入 source-level declaration id。

---

## 3. 核心表结构

本节描述编译进程内的全局视图。磁盘缓存布局见第 7 节。内存里使用全局表是为了查询快；落盘时按 package/module/shard 组织，避免新增一个 ID 就重写全表。

### 3.1 PackageInfo

Jiang 的 package 由 `package.ini`、root module、直接依赖和 package 内所有文件 module 组成。package 是跨 package public API 传播的边界。

package 的 public API 不是“package 内所有 module 的 public 声明”。依赖 package 时实际导入的是 package root module，因此 package public API 定义为：

```
root module 的 export namespace
+ root module 通过 public import 递归暴露的模块命名空间
```

package 内未被 root module 暴露的 module 即使包含 public 声明，也只属于 package 内部实现；它们的变化不应传播到依赖 package。

```
PackageInfo:
    id: PackageId
    stable_key: PackageStableKey
    root_path: NameId
    manifest_path: NameId
    manifest_hash: UInt64

    package_name: NameId
    package_type: PackageType
    root_module: ModuleId
    modules: Vec<ModuleId>
    dependencies: Vec<PackageImportEdge>

    package_export_namespace_hash: UInt64
    package_public_signature_hash: UInt64
    package_public_type_layout_hash: UInt64
    package_public_method_set_hash: UInt64

PackageImportEdge:
    alias: NameId
    package: PackageId
    snapshot_export_namespace_hash: UInt64
    snapshot_public_signature_hash: UInt64
    snapshot_public_type_layout_hash: UInt64
    snapshot_public_method_set_hash: UInt64
```

package 级 hash 用于快速剪枝：root module 暴露的 public API 没变时，不需要进入依赖方 package 做模块级传播。

### 3.2 ModuleInfo

模块不使用 `decl_range` / `type_range`。增量更新后，一个模块拥有的声明和类型不一定连续。

```
ModuleInfo:
    id: ModuleId
    stable_key: ModuleStableKey
    package: PackageId
    source_path: NameId
    relative_path: NameId
    source_hash: UInt64

    decls: Vec<DeclId>
    exports: HashMap<NameId, DeclId>
    imports: Vec<ImportEdge>

    export_namespace_hash: UInt64
    public_signature_hash: UInt64
    public_type_layout_hash: UInt64
    public_method_set_hash: UInt64

    module_object_hash: UInt64
    status: ModuleStatus

ImportEdge:
    imported_module: ModuleId
    imported_package: PackageId?
    alias: NameId
    public_flag: Bool
    snapshot_export_namespace_hash: UInt64
    snapshot_public_signature_hash: UInt64
    snapshot_public_type_layout_hash: UInt64
    snapshot_public_method_set_hash: UInt64
```

这些 hash 分开存，是为了避免一个 `signature_hash` 同时承担名字解析、签名、布局和方法集合变化判断。

### 3.3 DeclInfo

```
DeclInfo:
    id: DeclId
    stable_key: DeclStableKey
    disk_stable_hash: UInt64
    owner_module: ModuleId
    kind: DeclKind
    name: NameId
    visibility: Visibility
    span_start: UInt32
    span_end: UInt32
    status: DeclStatus       // active / tombstone

    signature_hash: UInt64   // 名字、可见性、泛型参数、参数类型、返回类型等
    body_hash: UInt64        // 函数体、global initializer、泛型 body
    layout_hash: UInt64      // struct/enum/union layout
    method_set_hash: UInt64  // 类型内方法集合、extend/trait impl 方法集合

    type_id: TypeId?         // nominal type decl
    func_sig: FuncSig?
    generic_params: Vec<GenericParam>

    deps: Vec<Dependee>      // 正向依赖，重算本声明时用于移除旧反向边
```

### 3.4 TypeInfo

`TypeId` 必须来自 canonical interner。类型比较优先使用 `TypeId == TypeId`。

```
TypeInfo:
    id: TypeId
    kind: TypeKind
    hash: UInt64

    nominal:
        decl_id: DeclId
        generic_args: Vec<TypeId>

    structural:
        key: MemoryTypeKey
```

`MemoryTypeKey` 是结构类型在内存中的规范表示：

```
MemoryTypeKey:
    builtin(name)
    pointer(pointee: TypeId, mutability)
    reference(pointee: TypeId, mutability)
    slice(element: TypeId)
    array(element: TypeId, length)
    tuple(elements: Vec<TypeId>)
    function(params: Vec<TypeId>, result: TypeId)
    optional(inner: TypeId)
    errorable(value: TypeId, error: TypeId)
```

规则：

- nominal 类型由声明决定。
- structural 类型在内存中由 `MemoryTypeKey` 全局 intern。
- 泛型实例化由 `(generic_decl, args)` 全局 intern。
- 不把 structural 类型强行归属某个模块。
- 磁盘缓存使用 `DiskTypeKey`，不能把 `MemoryTypeKey` 或 `TypeId` 直接写入跨会话缓存。

---

## 4. 依赖图

### 4.1 Depender / Dependee

增量编译不是“模块依赖模块”，而是“某个分析单元依赖某个语义事实”。

内存中的依赖图可以使用 `ModuleId` / `DeclId` / `TypeId`。磁盘中的 `provides.bin` / `depends.bin` 必须使用 stable key 或 stable hash。冷启动时先把 stable key 映射回本次编译进程的 ID，再重建内存依赖图。

```
Depender:
    module(ModuleId)
    decl_sig(DeclId)
    decl_body(DeclId)
    type_layout(TypeId)
    method_set(ModuleId, TypeId)
    trait_solution(ModuleId, DeclId, TypeId)
    generic_inst(DeclId, Vec<TypeId>)
    codegen_decl(DeclId)
```

磁盘表达：

```
DiskDepender:
    module(ModuleStableKey)
    decl_sig(DeclStableKey)
    decl_body(DeclStableKey)
    type_layout(TypeStableKey)
    method_set(ModuleStableKey, TypeStableKey)
    trait_solution(ModuleStableKey, DeclStableKey, TypeStableKey)
    generic_inst(DeclStableKey, Vec<TypeStableKey>)
    codegen_decl(DeclStableKey)
```

```
Dependee:
    source_file(ModuleId)
    export_namespace(ModuleId)
    decl_signature(DeclId)
    decl_body(DeclId)
    type_layout(TypeId)
    method_set(ModuleId, TypeId)
    trait_impl_set(ModuleId, DeclId, TypeId)
    associated_type_binding(ModuleId, DeclId, TypeId, NameId)
    generic_inst(DeclId, Vec<TypeId>)
    generic_body(DeclId)
    embed_file(NameId)
    link_input(NameId)
```

磁盘表达：

```
DiskDependee:
    source_file(ModuleStableKey)
    export_namespace(ModuleStableKey)
    package_public_signature(PackageStableKey)
    package_public_type_layout(PackageStableKey)
    decl_signature(DeclStableKey)
    decl_body(DeclStableKey)
    type_layout(TypeStableKey)
    method_set(ModuleStableKey, TypeStableKey)
    trait_impl_set(ModuleStableKey, DeclStableKey, TypeStableKey)
    associated_type_binding(ModuleStableKey, DeclStableKey, TypeStableKey, String)
    generic_inst(DeclStableKey, Vec<TypeStableKey>)
    generic_body(DeclStableKey)
    embed_file(String)
    link_input(String)
```

### 4.2 正向边和反向边

缓存中同时保存：

```
forward_deps: HashMap<Depender, Vec<Dependee>>
reverse_deps: HashMap<Dependee, Vec<Depender>>
```

更新某个 depender 时：

1. 读取旧 `forward_deps[depender]`。
2. 从对应 `reverse_deps` 中移除旧反向边。
3. 重新分析 depender，产生新 deps。
4. 写入新的 forward/reverse 边。

这样变化传播可以直接查 `reverse_deps[changed_dependee]`，不扫描全项目声明。

### 4.3 依赖种类

常见场景：

| 场景 | 记录依赖 |
|---|---|
| `foo()` 调用普通函数 | `decl_signature(foo)` |
| inline 函数或泛型函数实例化 | `generic_body(foo)` 或 `decl_body(foo)` |
| 字段访问 `x.y` | `type_layout(type_of_x)` |
| 构造 struct | `type_layout(struct_type)` |
| 方法调用 `x.method()` | `method_set(current_module, type_of_x)` |
| trait bound 检查 | `trait_impl_set(current_module, trait, type)` |
| associated type projection | `associated_type_binding(current_module, trait, type, assoc_name)` |
| 泛型实例化结果复用 | `generic_inst(generic_decl, type_args)` |
| `@embedFile` 等资源 | `embed_file(path)` |

---

## 5. 失效传播

### 5.1 变化检测

文件变化后重新 parse 当前模块，并计算每个声明的新 hash。

```
ChangedDependee:
    source_file(module)
    export_namespace(module)
    decl_signature(decl)
    decl_body(decl)
    type_layout(type)
    method_set(module, type)
    trait_impl_set(module, trait, type)
    associated_type_binding(module, trait, type, assoc_name)
    generic_inst(generic_decl, type_args)
```

判断规则：

- `body_hash` 变化：产生 `decl_body(decl)`。
- `signature_hash` 变化：产生 `decl_signature(decl)`。
- `layout_hash` 变化：产生 `type_layout(type_id)`。
- public export 名字集合变化：产生 `export_namespace(module)`。
- public extend / trait impl 变化：产生 `method_set` 或 `trait_impl_set`。
- associated type 绑定变化：产生 `associated_type_binding`。
- 泛型实例化结果 fingerprint 变化：产生 `generic_inst`。

### 5.2 dirty 队列

```
dirty_dependees = changed_dependees
dirty_dependers = []

while dirty_dependees not empty:
    dependee = pop(dirty_dependees)
    for depender in reverse_deps[dependee]:
        if mark_dirty(depender):
            dirty_dependers.push(depender)

while dirty_dependers not empty:
    depender = pop_ready(dirty_dependers)
    result = reanalyze(depender)
    update_forward_and_reverse_deps(depender, result.deps)
    for changed in result.changed_dependees:
        dirty_dependees.push(changed)
```

`pop_ready` 需要避免依赖尚未更新的单元先运行。初始实现可以按模块拓扑序处理；声明级增量阶段再引入更细的 ready 队列。

### 5.3 循环依赖

当前 Jiang 的模块 import cycle 是编译错误。增量编译不需要为合法循环做 SCC 调度；检测到循环时直接失效相关模块并报告 diagnostic。

初始规则：

- import graph 保持 DAG。
- 发现 import cycle 时，参与循环的 module 标记为 failed。
- failed module 不写 successful cache，不复用旧 `object.o`。
- 依赖 failed module 的下游 module 也必须重新 report diagnostic。

如果后续语言允许循环 import，再引入 SCC 调度；在当前目标语义下不实现 SCC。

---

## 6. 方法查找和 trait impl

不把 extend 方法注入目标类型的 `TypeInfo`，避免污染类型的全局视图。但需要独立索引。

```
method_index:
    (visible_module: ModuleId, receiver_type: TypeId) -> Vec<DeclId>

trait_impl_index:
    (visible_module: ModuleId, trait_decl: DeclId, receiver_type: TypeId) -> Vec<DeclId>

associated_type_index:
    (visible_module: ModuleId, trait_decl: DeclId, receiver_type: TypeId, assoc_name: NameId) -> TypeId
```

`visible_module` 表示“在某个模块中可见的方法集合”。这允许 private extend 只影响 owner module。

方法调用时记录：

```
Dependee.method_set(current_module, receiver_type)
```

trait bound 检查时记录：

```
Dependee.trait_impl_set(current_module, trait_decl, receiver_type)
```

associated type projection 时记录：

```
Dependee.associated_type_binding(current_module, trait_decl, receiver_type, assoc_name)
```

extend 或 trait impl 变化时，更新索引并产生对应 dependee 变化。`trait_impl_set` 的 fingerprint 必须覆盖 required method 签名、impl 方法集合和 associated type 绑定；associated type 绑定也要有独立 dependee，方便只让投影使用方失效。

## 6.1 泛型实例化

泛型实例化不能只依赖泛型函数体。一个实例化结果至少依赖：

- generic declaration signature。
- generic declaration body。
- type arguments 的 layout / method set。
- trait impl set。
- associated type bindings。

实例化时记录：

```
Depender.generic_inst(generic_decl, type_args)
```

实例化结果可作为 dependee：

```
Dependee.generic_inst(generic_decl, type_args)
```

当泛型实例化的 type-check 结果、monomorphized body 或 codegen fingerprint 变化时，依赖该实例化结果的调用方必须失效。

---

## 7. 缓存布局

缓存按 build config / package / module 分片。全局文件只做很薄的索引，不存完整声明表或类型表。

```
.jiang_cache/
  manifest.bin
  configs/
    <config_key>/
      workspace_record.bin
      packages/
        <package_key>/
          package_record.bin
          package_deps.bin
          modules/
            <module_key>/
              module_record.bin
              provides.bin
              depends.bin
              diagnostics.bin
              object.o
      reverse_index/
        <shard>.bin      // 可选，后期优化；可由 depends.bin 重建
      objects/
        <hash>.o         // 函数级 codegen cache
```

```
config_key = hash(compiler_version, target, optimize_mode, backend_version, link_options, feature_flags)
package_key = hash(canonical_package_root_path)
module_key = hash(package_key, module_relative_path)
```

`package_key` 基于本地 canonical path，适合作为 local cache identity。它不保证跨机器、跨 checkout 路径复用；如果以后支持 remote/shared cache，需要改成 package manifest 内容 hash + dependency lock hash。

### manifest.bin

```
Manifest:
    cache_version: UInt32
    compiler_version_hash: UInt64
    known_configs: Vec<ConfigKey>
    known_packages: Vec<(PackageKey, canonical_package_root_path)>
```

`manifest.bin` 很小，可以整文件重写。它不保存 `next_decl_id` / `next_type_id`，因为磁盘身份不依赖顺序 ID。

### workspace_record.bin

```
WorkspaceRecord:
    config_key: ConfigKey
    root_package: PackageKey
    package_graph_hash: UInt64
    packages: Vec<PackageKey>
```

### package_record.bin

```
PackageRecord:
    package_key: PackageKey
    package_root_path: String
    package_ini_hash: UInt64
    package_name: String
    package_type: PackageType
    root_module: ModuleKey
    modules: Vec<ModuleKey>

    dependencies:
        alias: String
        package: PackageKey
        snapshot_export_namespace_hash: UInt64
        snapshot_public_signature_hash: UInt64
        snapshot_public_type_layout_hash: UInt64
        snapshot_public_method_set_hash: UInt64

    package_export_namespace_hash: UInt64
    package_public_signature_hash: UInt64
    package_public_type_layout_hash: UInt64
    package_public_method_set_hash: UInt64
```

package 级 record 负责跨 package 依赖剪枝。package 的 public hash 未变时，依赖 package 通常不需要进入模块级重算。

### module_record.bin

```
ModuleRecord:
    module_key: ModuleKey
    package_key: PackageKey
    relative_path: String
    source_hash: UInt64

    imports:
        import_text: String
        resolved_module: ModuleKey?
        resolved_package: PackageKey?
        alias: String
        public_flag: Bool

    export_namespace_hash: UInt64
    public_signature_hash: UInt64
    public_type_layout_hash: UInt64
    public_method_set_hash: UInt64

    object_hash: UInt64
    diagnostics_hash: UInt64
```

只对 successful module 复用 `object.o`。failed module 可以写 `diagnostics.bin`，但不能复用旧 object。

### provides.bin

`provides.bin` 描述该 module 提供的声明、导出命名空间和 public API。它不保存裸 `DeclId`。

```
ProvideEntry:
    stable_key: DeclStableKey
    visibility: Visibility
    symbol_name: String

    signature_fingerprint: UInt64
    body_fingerprint: UInt64
    layout_fingerprint: UInt64
    method_set_fingerprint: UInt64

ExportEntry:
    name: String
    namespace_kind: NamespaceKind
    target: ExportTarget
    visibility: Visibility
    binding_fingerprint: UInt64

ExportTarget:
    decl(DeclStableKey)
    module(ModuleStableKey)
    package(PackageStableKey)
    alias(DeclStableKey)
```

`ExportEntry.binding_fingerprint` 必须包含目标 stable key 和最终 symbol name。这样 `public alias f = a.foo` 改成 `public alias f = b.foo` 时，即使两个目标签名相同，依赖方 codegen 也会正确失效。

### depends.bin

`depends.bin` 描述该 module 内各分析单元依赖哪些语义事实。它使用 `DiskDepender` / `DiskDependee`。

```
DependEntry:
    depender: DiskDepender
    dependees: Vec<DiskDependee>
```

初始实现只写每个 module 的 `depends.bin`。冷启动时由所有 `depends.bin` 重建内存 `reverse_deps`。项目变大后，再把 `reverse_index/<shard>.bin` 作为加速索引。

### diagnostics.bin

`diagnostics.bin` 保存上次编译该 module 产生的 diagnostics。跳过 unchanged module 时，可以直接恢复 diagnostics。

```
DiagnosticCache:
    diagnostics_hash: UInt64
    source_hash: UInt64
    entries: Vec<DiagnosticEntry>
```

diagnostics cache 不参与 object 复用判定；object 复用必须要求 module status 为 successful。

### type cache

初始实现不单独持久化全局 `type_table`。冷启动时从 `provides.bin`、`depends.bin` 和 dirty module 的源码重新 canonicalize 类型，并重建内存 `TypeId`。

如果冷启动类型重建变慢，可以增加：

```
types/
  shards/
    <shard>.bin
  journal.bin
```

但 shard 中仍然保存 `TypeStableKey` / `DiskTypeKey`，不能依赖旧 `TypeId` 作为稳定身份。

### 原子写入

所有 cache 文件写入必须使用临时文件和原子 rename：

```
write file.tmp
flush file.tmp
rename file.tmp -> file
```

一个 module 的 `module_record.bin`、`provides.bin`、`depends.bin`、`diagnostics.bin` 和 `object.o` 必须带同一组 `source_hash` / fingerprint，冷启动时发现不一致就丢弃该 module cache。

---

## 8. 编译流水线

### 冷启动

```
1. 读取 manifest.bin，确定 config_key 和 root package。
2. 解析 root package 的 package.ini，递归解析 package dependencies。
3. 对每个 package：
   - package.ini hash 未变，加载 package_record.bin。
   - package.ini hash 变化，重新扫描 package module 列表和依赖。
4. 对每个 module：
   - source_hash 未变，加载 module_record.bin / provides.bin / depends.bin / diagnostics.bin。
   - source_hash 变化，重新 parse + declaration scan。
5. 用 stable key 重建本次编译进程内的 PackageId / ModuleId / DeclId。
6. 从 provides/depends 重新 canonicalize 类型，重建 TypeId 和 type interner。
7. 由所有 depends.bin 重建内存 forward_deps / reverse_deps。
8. 根据 package/module/provide fingerprint 计算 changed dependees。
9. 传播 dirty。
10. 只重算 dirty package/module/declaration。
11. 写回对应 package/module cache。
```

### watch 模式

开发阶段优先支持常驻进程。

```
jiangc --watch
```

常驻进程内保留：

```
package graph
module table
decl table
type interner
method index
trait impl index
forward/reverse deps
diagnostics
latest AST for opened/changed modules
```

watch 模式的性能主要来自内存状态复用，磁盘缓存主要用于冷启动。

---

## 9. Codegen 策略

### 初始策略: module.o

每个 dirty module 重新生成一个 `module.o`。

优点：

- 实现简单。
- 链接模型清晰。
- 适合先验证模块级增量。

缺点：

- 大模块中修改一个函数仍会重新 codegen 整个模块。

### 优化策略: decl/function object cache

当声明级语义增量稳定后，再引入：

```
CodegenUnit:
    function(DeclId)
    global(DeclId)
    type_metadata(TypeId)
```

每个 codegen unit 计算：

```
codegen_hash = hash(
    config_key,
    target,
    optimize_mode,
    backend_version,
    module_export_namespace_hash,
    decl_signature_hash,
    decl_body_hash,
    referenced_layout_hashes,
    referenced_binding_fingerprints,
    referenced_method_set_fingerprints,
    referenced_trait_impl_fingerprints,
)
```

命中时复用旧 object artifact。

模块级 `object.o` 的有效性也必须包含同类输入：`config_key`、backend version、module source hash、所有被 codegen 依赖的 layout / binding / method / trait fingerprints。只比较 `source_hash` 不足以判断 object 是否可复用。

---

## 10. 实施顺序

### A. 净化 IR

- HIR/JIR 不保存 AST 指针。
- 类型引用统一变成 `TypeId`。
- 声明引用统一变成 `DeclId`。
- span 用字节偏移保存，诊断需要源码时重新读取 source。

### B. 基础全局表

- 实现 `NameId` / `ModuleId` / `DeclId` / `TypeId`。
- 实现 package table、module table、decl table、type interner。
- `DeclId` 只递增，不复用。
- 模块保存 `decls: Vec<DeclId>`。
- 磁盘缓存使用 stable key，冷启动时重建本次编译进程的 ID。

### C. 模块级缓存

- 写 `manifest.bin`、`workspace_record.bin`、`package_record.bin`、`module_record.bin`、`provides.bin`。
- 未变 package/module 加载 record。
- dirty module 重新 parse/resolve/type_check/codegen。
- 未变模块复用 `module.o`。

### D. 依赖图

- 每个 module 写 `depends.bin`，使用 stable key 表达依赖。
- 冷启动时由 `depends.bin` 重建内存 forward/reverse deps。
- 实现 dirty propagation。
- 支持 `package_public_signature`、`export_namespace`、`decl_signature`、`type_layout`、`method_set`、`trait_impl_set`、`associated_type_binding`、`generic_inst`。

### E. 声明级重算

- 声明匹配使用 `DeclStableKey`。
- 声明 hash 拆成 `signature_hash` / `body_hash` / `layout_hash`。
- `provides.bin` 写出 `ProvideEntry` 和 `ExportEntry`，冷启动可恢复 export namespace。
- 函数体变化只重查当前函数体。
- 布局变化传播到字段访问、构造、按值传递等依赖方。
- 泛型实例化、trait impl 和 associated type projection 都进入依赖图。

### F. watch 模式

- 常驻进程保留表和依赖图。
- 文件变化后局部更新。
- 先输出 diagnostics，再按需产出 binary。

---

## 11. 验收标准

最小可用增量编译应该满足：

1. 修改私有函数体，只重新编译当前模块。
2. 修改 public 函数体，不重新 type_check 依赖模块。
3. 修改 public 函数签名，依赖调用方重新 type_check。
4. 修改 struct 字段，字段访问方重新 type_check。
5. 修改 private extend，只影响 owner module。
6. 修改 public extend，使用该方法集合的模块重新 type_check。
7. 修改 package public API，会传播到依赖 package；只修改 package private 实现，不传播到依赖 package。
8. 删除声明不会导致旧 `DeclId` 被新声明误用。
9. 冷启动加载缓存后，行为与全量编译一致。
10. 新增声明或类型不会要求重写整个全局表缓存，只重写对应 package/module cache。
