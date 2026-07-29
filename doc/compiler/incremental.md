# Incremental Compilation 设计

增量编译只复用具有稳定身份的语义事实和构建产物。`DefId`、`TypeId`、Semantic Model node、
`jil.FunctionId` 等均为单次 compilation 的 handle，不能写入长期缓存。

## 持久边界

0.5.0 只持久化三类内容：

```text
cache/<context-key>/
  sources/<stable-source-id>.ji
  objects/<stable-source-id>.o
  objects/<stable-source-id>.mono.o
  builds/<target-id>.jbuild
```

- `.ji` 是单个 source 的纯语义 interface。
- `.o` 与 `.mono.o` 是 debug 模式的粗粒度 codegen work product。
- `.jbuild` 是单个 build target 的上次成功状态；只有 debug 保存 work-product index。
- release 使用 whole-package codegen，不保存细粒度 object 记录。

不存在 `.jd`、`.jai`、每 object sidecar、逐泛型实例 object 或 object closure。源码改名会形成新的
stable source identity；旧文件由显式 cache clean 清理，0.5.0 不实现自动 GC 或缓存数据库。

## Context 与稳定身份

`context-key` 隔离所有会改变语义或机器码的构建上下文：

- compiler build/schema 和 language version；
- target/ABI；
- LLVM/toolchain；
- debug/release backend profile；
- package/lang 配置产生的 import-resolution fingerprint。

源码内容变化不改变 context。stable source identity 由 package identity 与规范化包内相对路径
生成，不包含 `SourceId` 或源码 hash。同一 source 的 `.ji`、`.o` 和 `.mono.o` 共用这个 identity，
只由目录与固定后缀区分。

## `.ji`：纯语义 source interface

`.ji` header 保存：

- format/schema 与 compiler version；
- stable source identity；
- source mtime、size 和 source hash；
- package-visible 与 public interface fingerprint；
- section table 的位置和数量。

section table 当前描述 import summary、interface、generic template 和 source map。每项保存
offset、length 与内容 hash。`.ji` 不保存 object、链接输入、codegen summary、Semantic Model
会话对象或任何 session-local ID。

读取分阶段进行：

```text
source graph       -> header + compact import summary
resolve/type check -> interface section
monomorph          -> generic template section
```

读取 section 时使用 `read_at` 并校验 section hash，不为每个 source 长期保持文件句柄。
`--check` 只发布 `.ji`，不读取或修改 `.jbuild`，也不生成 object fingerprint。

## SourceDependencyGraph

`SourceDependencyGraph` 是单次 compilation 的 source 级依赖图：

- 节点由连续 `SourceNodeId` 标识，保存 stable source identity、相对路径、旧 `.ji` header 和 dirty；
- 正向边为 `source -> dependency`；
- 反向边为 `dependency -> importer`；
- identity 到 node 的 HashTable 只用于查找，不决定分析顺序。

构图从 root 递归进行。source metadata 未变时沿用 `.ji` header 的 source hash；metadata 改变时才
读取源码重新计算 hash。旧 import summary 缺失、损坏、context 不匹配或 source hash 变化时，从
当前源码重新收集 import。

完整构图后按 dependency SCC 拓扑顺序分析。SCC 内按 stable source identity 排序；任一成员 dirty
时保守分析整个 SCC。新的 interface fingerprint 与旧值相同则停止传播；发生变化时才标记直接
importer：

- 同 package 比较 package-visible fingerprint；
- 跨 package 比较 public fingerprint；
- private body 变化不传播；
- public signature、可见布局、public import/alias 和调用方可见泛型 body 变化会传播。

0.5.0 不追踪 importer 实际读取的具体声明。`QueryDependencyGraph` 保留给后续声明级 QueryCache。

`--check` 可以直接复用 fresh source 的 interface。debug/release 若未命中 `.jbuild` 快速路径，
仍需从源码恢复 codegen 所需的普通函数 body；纯语义 `.ji` 不保存这些 body。source graph
负责失效与分析顺序，不改变 release 的 whole-package object 粒度。

## Debug Codegen Unit

debug 按 stable source identity 建立最多两个 unit：

```text
source unit:
  普通非泛型函数、global、entry wrapper 和其他稳定定义

monomorph unit:
  该 source 的最终 JIL 实际引用的 concrete instance 闭包
```

闭包从 source unit 的 function/global roots 出发，覆盖 lowering 生成的 shim、析构函数和
coroutine 变体。同一实例可以进入多个调用方的 monomorph unit，并由 weak/linkonce 定义合并；
这样复用任一调用方 source object 时，它所需的泛型符号都由同 source identity 的 `.mono.o`
覆盖。unit 和实例 fingerprint 均稳定排序，不依赖 `ModuleId`、`DefId` 或 HashTable 遍历顺序。

source unit fingerprint 覆盖 source body、interface、layout、target、compiler 与 backend profile。
monomorph unit fingerprint 覆盖排序后的 concrete instance key、generic body/layout 与相同 codegen
上下文。普通 source body 变化不会无条件使 `.mono.o` stale；实例集合变化也不会无条件重编 `.o`。

复用只有一条路径：

1. 从当前 JIL 建立本轮 CGU plan。
2. 在当前 target 的 `.jbuild` 中查找 `(stable source identity, unit kind)`。
3. 比较 input fingerprint。
4. 用稳定路径检查 object。mtime/size 命中时不读取 object；metadata 变化时才重新 hash。
5. miss 时只生成对应 `.o` 或 `.mono.o`，同目录临时文件完成后原子替换。
6. link plan 只消费本轮 CGU，不恢复历史 link closure。

package-level provenance、属性和 verifier 事实只准备一次，再供所有 stale unit 使用。LLVM lowering
仍按 unit 执行，但不会为每个 object 重复跑全包分析。

多个 stale unit 使用 Jiang async `Task` 在 `global_domain` 并发 emission。任务之间不共享可变
LLVM context/module 或诊断 store；主任务按 stable unit 顺序收集结果。并发只缩短本轮 miss 的
codegen 时间，不改变 CGU fingerprint、object 路径、link plan 或 `.jbuild` 格式。单 unit miss
走直接路径，all-hit/no-op 不启动 worker。

## Release 与用户 object 输出

release 始终执行 whole-package codegen 和整体优化。输入失效时完整重建临时 package object，
链接完成后删除它；`.jbuild` 只保存最终成功快照，不保存 source/monomorph work product。

`--emit-obj -o file.o` 在所有模式下保持单文件用户契约，直接执行 whole-package object emission。
debug 内部的多个 work product 不暴露为用户输出。

## `.jbuild` 与快速 no-op

每个 target/context 只有一个 `.jbuild`。header 保存 format、context 与 invocation fingerprint。
`last_success` 保存：

- 可达 source 的相对路径列表；
- source stat 聚合 fingerprint；
- 最终输出的 mtime、size 与内容 fingerprint。

debug 额外保存每 source 最多两个固定 work-product 记录：

```text
stable source identity
unit kind
input fingerprint
object hash
object mtime / size
```

路径由 identity 与 kind 推导，不写入记录。`.jbuild` 不保存泛型成员列表、source graph、
Semantic Model、绝对源码路径或 link closure。

编译启动时先加载 `.jbuild`，对旧 reachable source 执行 `stat`。invocation、source snapshot 和
最终输出均命中时，在 parse、sema、JIL、codegen 和 linker 之前返回成功。新建但不可达的源码不属于
输入。metadata 变化会进入正常 source graph；这只能扩大重编范围，不能改变程序语义。

## 发布与并发

`.ji`、object 和 `.jbuild` 均采用“同目录临时文件 + 原子替换”。object 发布后才可写入
work-product index；只有链接和最终输出成功后才更新 `last_success`。进程中断可能造成下一次少量
重复 codegen，但不会产生错误命中。

并行 worker 只写各自的临时 object。所有 worker 成功并完成 join 后，主任务才按 stable unit
顺序发布 object 和 work-product；失败或取消时清理未发布临时文件，不留下可命中的半成品。

同一 target/context 使用 advisory build lock：

1. 无锁读取 `.jbuild` 尝试 no-op；
2. miss 后获取 target lock；
3. 等待完成后再次检查 no-op；
4. 持锁执行正常构建和 `.jbuild` 发布。

不同 target/context 不共享 target lock。发布缓存前再次比较本轮读取的 source snapshot；源码在
编译期间变化时放弃发布新 cache state。缓存缺失、损坏、截断或发布失败均按 miss 处理。

## 可观测统计

`--artifact-stats` 输出：

- interface hit/miss/stale；
- `.ji` section read 与 parsed source 数；
- object hit/miss/stale；
- emitted/reused unit 与 linked object 数；
- `.jbuild` no-op hit 数。

统计只用于测试和性能分析，不进入 cache key，也不改变调度结果。

## 后续边界

`QueryDependencyGraph` 当前只提供会话内 dependency/reverse-dependency 与失效遍历骨架。声明级
QueryCache 需要稳定 declaration key、自动依赖采集、cycle 处理与 source-level 保守 fallback，
不属于 0.5.0 的 source-level 增量范围。
