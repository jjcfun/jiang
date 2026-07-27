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
