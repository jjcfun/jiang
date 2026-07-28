# JIL 优化与基线

本文记录 JIL 分析、优化 pass 和 backend lowering 的可复现证据。
优化不能改变 Jiang 语言语义；普通尾调用无法证明安全时必须保留普通 `call`。
0.5.0 要实现并验证安全的直接尾递归优化，但不把尾递归消除提升为语言保证。

## 复现方法

从仓库根目录运行：

```bash
JIANGC=./build/bin/jiangc.next \
JIANG_P4_RUNS=3 \
bash ./script/jil_optimization_baseline.sh
```

脚本使用每次新建的 artifact cache，记录：

- compiler version、target、LLVM 版本和 host。
- debug/release 冷编译的 wall time 与 peak RSS。
- debug/release executable 大小。
- `benchmark/jil/tail_recursion.jiang` 的重复运行时间。
- 普通 tail/musttail 和 coroutine local handoff `musttail` 的 LLVM IR 计数。

运行时间先执行一次同一 binary 作为加载预热，再记录指定次数。数字只用于同一机器、
同一 compiler build 的前后比较，不作为跨机器性能承诺。

## 2026-07-28 优化前基线

环境：

```text
repository commit: 878f68b3
compiler build-id: db8c82e3ebf7d3252501c69620ea5be5d65febb7829dc8836bba2e69226267ec
compiler: jiang 0.5.0
target: arm64-apple-darwin25.5.0
LLVM: 22.1.8
host: Darwin arm64
```

编译与产物：

| case | elapsed ms | peak RSS KiB |
| --- | ---: | ---: |
| debug executable | 722.771 | 54352 |
| release executable | 890.112 | 68032 |
| tail LLVM IR | 472.995 | 47280 |
| coroutine LLVM IR | 1146.773 | 65040 |

| artifact | bytes |
| --- | ---: |
| tail debug executable | 112888 |
| tail release executable | 79520 |

预热后的尾递归样例运行时间：

| profile | run 1 ms | run 2 ms | run 3 ms |
| --- | ---: | ---: | ---: |
| debug | 12.130 | 11.214 | 10.429 |
| release | 2.087 | 2.104 | 2.137 |

LLVM 结构：

```text
tail sample plain tail calls: 0
tail sample musttail calls: 0
coroutine benchmark musttail calls: 4
```

这组数据说明当前 JIL/backend 没有为普通直接尾递归携带可验证的 tail fact。
release 运行时间可能受 LLVM 自身优化影响，不能据此证明常量栈；
P4 完成条件仍要求深递归压力测试以及目标机器码或等价 backend 证据。
coroutine local handoff 已有四处结构化 `musttail`，后续 pass 必须保持这些调用。

## 优化证据规则

- 新增 pass 前先固定输入、输出、不变量和 analysis invalidation。
- provenance/escape、tail eligibility 和 LLVM attribute proof 分开记录；
  “没有逃逸”不能自动推出 `nocapture`、`noalias`、`readonly` 或 `dereferenceable`。
- 普通 tail hint 不是完成证据。直接 self recursion 必须在主要可执行 target 上通过
  深递归常量栈压力测试，并检查目标代码没有递归增长调用栈。
- mutual recursion 与 sibling tail call 没有同等证明时保持普通调用。
- 没有可复现收益的 CFG cleanup 不进入主线；结论需附测量命令和数据。

## 0.5.0 pass 顺序与失效规则

当前顺序是：

```text
drop-elaborated JIL
  -> backend-input verifier
  -> provenance fixed point
  -> direct self-tail eligibility
  -> self-tail loop transform
  -> provenance invalidation/recompute
  -> backend-input verifier
  -> parameter-level attribute proof
  -> LLVM lowering
```

provenance 在每个 basic block 的 entry、terminator 前和 exit 保存 local 摘要，CFG join
逐来源取并集。当前来源集合包括 parameter、当前栈、global、heap、coroutine frame 和 unknown。
field、tuple、index、deref 与 aggregate 的传播使用 whole-local 保守摘要；未知或间接调用、
inline asm、外部存储、return/throw 和 coroutine capture 分别记录 escape sink。
coroutine frame layout 消费 `captured_by_coroutine` 中的当前栈/unknown 事实：出现此类 capture 时，
所有 `address_taken` local 都保守保留 frame field；精确 live-across-suspend 和 interference
analysis 继续决定其他 local 以及可复用槽位。

任何 CFG mutation 都使旧 block/local 状态失效。尾递归 transform 完成后立即重算 provenance，
backend 参数属性再从 final JIL 做参数级分析，不能读取变换前的 facts。0.5.0 不加入额外 CFG
cleanup：现有基线没有证明 unreachable 删除、空块穿透或常量分支折叠在 LLVM 优化之外还有
稳定收益，避免为无收益 pass 扩大 source location、cleanup edge 和 analysis invalidation 风险面。

## 参数属性证明

四项事实彼此独立：

| fact | 证明条件 | 保守拒绝 |
| --- | --- | --- |
| `nocapture` 概念事实 | 参数级 provenance 不进入 return、外部存储、调用或 coroutine capture | 任一未知调用或 escape sink |
| `noalias` | 参数是 borrow check 已验证的 `T&!` | shared/raw/fat reference |
| `readonly` | 没有 through-reference write，也没有把参数交给未知效果调用 | write、call、inline asm、coroutine capture |
| `dereferenceable` | safe reference 且 pointee 有 concrete layout | unsized trait object、slice value 或未知 layout |

LLVM 22 将旧 `nocapture` 拼写表示为 `captures(none)`。fat reference 在 LLVM ABI 中是 aggregate，
backend 不会把 pointer-only attribute 错加到 aggregate 参数。聚焦测试
`test/compiler/ir/jil/run/attributes.jiang` 同时验证四项证明互不替代，以及最终 LLVM
declaration 的属性翻译。

## 安全直接尾递归

eligibility 只接受解析到同一个 concrete `FunctionId` 和同一个 generic instance 的 direct
self call。只比较 `DefId` 不够：hosted entry wrapper 可能和 language main 共享 source identity，
但不是同一个 JIL function。除此之外还必须满足：

- continuation 只做返回值 forwarding，最终返回 call destination；
- call 没有 cleanup、initialized-place 或 suspend/handoff obligation；
- 当前 function 不是 coroutine resume/completion/source coroutine；
- 参数数量与 concrete function 一致，参数不需要 runtime drop；
- 参数 provenance 不含当前栈或 unknown，且此前没有当前栈引用逃逸。

通过后，transform 先把所有实参按原求值顺序写入新的 temporary，再统一更新参数 local，
最后跳回原 entry block。这样交换或相互依赖的参数不会被提前覆盖。新 statement 和回边保留
原 call terminator 的 source span。mutual-recursive SCC、sibling call、跨 function/module call
和任何未知条件都保持普通 `call`。

## 0.5.0 验证结果

聚焦验证命令：

```bash
TEST_ROOT=test/compiler \
TEST_FILTER='^test/compiler/ir/jil/run/(provenance|attributes|tail_recursion)\.jiang$' \
TEST_JOBS=1 JIANGC=./build/bin/jiangc.next bash ./script/test.sh

TEST_ROOT=test/lang \
TEST_FILTER='^test/lang/runtime/run/tail_recursion_constant_stack\.jiang$' \
TEST_RELEASE_RUNS=1 TEST_JOBS=1 TEST_TIMEOUT=30 \
JIANGC=./build/bin/jiangc.next bash ./script/test.sh
```

arm64 macOS 的 debug 与 release executable 都通过 200,000 层直接递归和引用参数 forwarding；
这比只观察 LLVM `tail` hint 更直接地证明当前受支持路径保持常量栈。x86_64 Linux 结构验证：

```bash
./build/bin/jiangc.next --target x86_64-unknown-linux-gnu \
  --emit-llvm -o /tmp/jiang-tail-x86_64.ll \
  test/lang/runtime/run/tail_recursion_constant_stack.jiang
```

固定 link symbol `__jiang_test_tail_count` 的 function body 只包含参数 temporary、entry 回边和
return，不包含 self call。普通 self-tail transform 不生成 `tail`/`musttail` hint。

coroutine local handoff 仍由结构化 backend lowering 生成 `musttail`。在 arm64 macOS 和
x86_64 Linux 上对 `benchmark/coroutine/task_baseline.jiang` emit LLVM，均保留 4 个
`musttail call`。这条硬约束只属于已验证的同 ABI coroutine handoff；普通尾调用无法证明时
仍是普通 `call`，Jiang 0.5.0 不向用户保证所有尾递归都会消除。
