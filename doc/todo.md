# Jiang TODO

当前状态：

- Stage1 前七阶段已完成，不再在 TODO 中逐项展开。
- 当前 TODO 只保留本版本要实现的增量编译 v1。
- 增量编译方案见 `doc/incremental-compilation.md`。

## 增量编译 v1

- [x] 新增 `compiler/incremental.jiang`。
  - [x] `Hash128 = UInt64[2]` 语义类型。
  - [x] `Hash256 = UInt64[4]` 语义类型。
  - [x] 封装已有 `support/blake3.jiang` 为 `hash128_bytes(...)` / `hash256_bytes(...)`。
  - [x] `PackageKey` / `ModuleKey` / `ObjectKey`。
  - [x] `IncrementalSession` / `ModuleFingerprint` / `ModuleCacheEntry` / `IncrementalPlan`。
- [x] 清理 stable key 命名。
  - [x] session-local key 统一命名为 `*SessionKey`。
  - [x] `*StableKey` 只能表示跨进程可重建字段。
  - [x] 禁止 `ModuleId` / `DeclId` / `TypeId` / `BindingId` / `Symbol.id` 写入 stable key。
- [x] 接入 compiler 框架。
  - [x] `compile_file_incremental_to_object(...)`。
  - [x] `compile_file_incremental_to_executable(...)`。
  - [x] v1 cache miss/full rebuild fallback。
  - [x] CLI 预留 incremental 入口。
- [x] 生成 module-level fingerprint。
  - [x] source hash。
  - [x] package key。
  - [x] module key。
  - [x] object key。
  - [x] dirty reason。
- [x] whole-graph object cache。
  - [x] graph source hash。
  - [x] `.jiang/cache/objects/<object-key>.o` path 规则。
  - [x] cache hit 时复制 cached object。
  - [x] cache miss 时 full rebuild 并写入 cache。
- [x] 验证。
  - [x] stage1 build。
  - [x] stage1 smoke。
  - [x] 最小 incremental hash/key 单元测试或 smoke。

## 暂不进入当前版本

- declaration/function-level incremental。
- `DeclStableKey` / `TypeStableKey` / `InstanceStableKey` 落盘实现。
- per-module object reuse。
- multi-object linker。
- cross-module public API 精准 dirty propagation。
- generic nominal layout cache。
- 远程 cache / 跨机器共享。
- trait object、完整 errorable ABI、target data layout invalidation。
