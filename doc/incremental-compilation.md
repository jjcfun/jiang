# Jiang Incremental Compilation v1

本文档只描述当前版本要实现的增量编译功能。v1 目标是先建立可验证的增量编译框架和稳定 key 规则，避免过早进入声明级、函数级、远程 cache 或完整 ABI invalidation。

## 当前目标

v1 做：

- 复用已有 `compiler/support/blake3.jiang` 计算增量编译 hash。
- 定义跨进程可复用的 `PackageKey` / `ModuleKey` / `ObjectKey`。
- root package 统一管理 `.jiang/cache/`。
- 建立 `incremental.jiang` 框架：session、cache key、module fingerprint、dirty reason、plan。
- `--incremental` 复用 whole-graph object cache：同一个 root/module graph 的 source hash、target、compiler version 和 codegen options 不变时直接复用最终 object。
- cache miss 时等价 full rebuild，并把成功生成的 object 写入 cache。

v1 不做：

- 不做 declaration/function-level incremental。
- 不落地跨进程可复用的 `DeclStableKey` / `TypeStableKey` / `InstanceStableKey`。
- 不做 per-module object reuse。
- 不做远程 cache 或跨机器共享。
- 不做 generic nominal layout cache。
- 不做 trait object / 完整 errorable ABI invalidation。
- 不保证 declaration reorder 不失效。

## 缓存目录

root package 统一管理本次构建涉及的所有 package cache：

```text
<root-package>/.jiang/cache/
  build.meta
  packages/
    <package-key>/
      package.meta
      modules/
        <module-key>.meta
        <module-key>.o
  objects/
    <object-key>.o
```

暂不创建 `.jiang/packages/`。path dependency 直接读取真实路径；以后支持 registry / remote package / lockfile 时，再用 `.jiang/packages/` 放下载或解包后的 immutable package source。

## Hash 规则

增量编译相关 hash 统一使用 BLAKE3 helper。输出宽度按用途选择，不引入第二套 hash 算法。

```text
Hash128 = UInt64[2]
Hash256 = UInt64[4]
```

当前 v1 默认使用 `Hash128`：

- file hash。
- manifest hash。
- package key。
- module key。
- object key。
- module fingerprint。

`Hash256` 只作为接口预留，后续用于远程 cache、不可信 cache、长期内容寻址或跨机器共享。

## Stable Key 规则

硬规则：凡是命名为 `*StableKey`、写入 cache meta、参与跨进程复用或作为落盘依赖图 identity 的 key，都必须只包含跨进程可重建字段。

禁止进入 stable key 的字段：

- `ModuleId` / `DeclId` / `TypeId` / `BindingId`。
- `Symbol.id`。
- arena pointer、AST pointer、HIR/JIR node pointer。
- 单独的 AST index。

允许进入 stable key 的字段：

- normalized package root 或 registry package identity。
- package name/version、manifest hash、dependency lock hash。
- normalized module path。
- source text 中的 name。
- stable enum id。
- BLAKE3 hash。
- 由 stable key 递归组成的 owner/type/generic argument key。

当前代码里的 `semantic.DeclSessionKey` 使用 `ModuleId`、`Symbol.id` 和 `ast_index`，只用于当前编译进程内查表；它不能写入 cache，也不能改名回 `DeclStableKey`。

v1 只定义这些跨进程 key：

```text
PackageKey = Hash128(package-name, package-version, canonical-package-root, manifest-hash, target-triple, compiler-version)
ModuleKey  = Hash128(package-key, normalized-module-path)
ObjectKey  = Hash128(package-key, graph-source-hash, target-triple, compiler-version, codegen-options)
```

`DeclStableKey` / `TypeStableKey` / `InstanceStableKey` 推迟到声明级增量阶段再实现。

## Module Cache Entry

v1 的 module cache meta 只保存当前模块级复用需要的字段：

```text
ModuleCacheEntry {
    compiler_version: String
    target_triple: String
    normalized_path: String
    source_hash: Hash128
    module_key: ModuleKey
    object_key: ObjectKey
    object_path: String
}
```

当前版本不使用 `public_api_hash` 做依赖方精准复用。跨 module/package 的 public API dirty propagation 和 per-module object 复用放到下一版；v1 复用的是整个 root graph 生成的最终 object。

## Compiler 框架

新增框架：

```text
compiler.jiang
  compile_file_incremental_to_object(...)
  compile_file_incremental_to_executable(...)

incremental.jiang
  Hash128 / Hash256
  PackageKey / ModuleKey / ObjectKey
  IncrementalSession
  ModuleFingerprint
  ModuleCacheEntry
  IncrementalPlan
  DirtyReason
```

入口职责：

```text
compile_file_incremental_to_executable(input, output)
  -> create CompilerContext
  -> resolve input to root package/root module
  -> create IncrementalSession(root cache dir, target/options)
  -> build module graph
  -> compute BLAKE3 graph source hash / object key
  -> if cache object exists:
       copy cache object to requested output
       return
  -> full rebuild object
  -> copy successful object to cache
```

`compiler.jiang` 只做高层调度，不直接操作 cache 文件细节；cache key、dirty plan、cache entry 数据结构放到 `incremental.jiang`。

## Codegen 边界

当前已有：

- `emit_jir_modules_object(ctx, inputs, path)`：多个 module 合并输出一个 object。
- `emit_jir_module_object(ctx, input, path)`：单个 JIR module 输出 object。

v1 先保留全量 object 输出行为，复用的是 whole-graph final object。per-module object reuse 放到 v2，因为当前 codegen 对 imported globals/functions 的 declare/define 边界仍按合并 LLVM module 路径工作，直接拆 per-module object 容易产生重复定义或缺失声明。

## 实施顺序

1. 新增 `incremental.jiang`，封装已有 BLAKE3 helper 为 `Hash128` / `Hash256` 和 key 类型。
2. 将现有 session-local `DeclStableKey` 重命名为 `DeclSessionKey`。
3. 在 `compiler.jiang` 增加 incremental compile 入口，先走 full rebuild fallback。
4. 为 module graph 生成 graph source hash / `ObjectKey`。
5. 实现 `.jiang/cache/objects/<object-key>.o` 读写。
6. `--incremental` 命中时复制 cached object，miss 时 full rebuild 并写 cache。

## 正确性原则

- cache miss 等价 full rebuild。
- 诊断失败不写 cache。
- key 中不得保存 session-local ID。
- 当前 v1 如果不能证明可复用，就重编。
