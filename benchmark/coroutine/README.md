# Coroutine runtime benchmark

Run the benchmark with the release-mode next compiler:

```sh
JIANGC=./build/bin/jiangc.next \
JIANG_BENCH_ITERATIONS=10000 \
JIANG_BENCH_RUNS=5 \
bash ./script/coroutine_bench.sh
```

The benchmark warms executor and allocator state before measuring these paths:

- ordinary synchronous call;
- same-Domain direct async handoff;
- scoped `Task<T>` creation and await;
- `Task<T>^` allocation, parameter/return forwarding, and await;
- cross-Domain enqueue and await;
- `global_domain`, `main_domain`, and a minimal custom Executor enqueue;
- a reused runtime inline Domain and per-operation runtime Domain/executor lifecycle;
- immediate suspend/resume;
- cancellation before a scoped task starts;
- repeated work spread across eight serial Domains.

Each line reports elapsed monotonic time, nanoseconds per logical operation, and Jiang runtime Job-wrapper allocations.
The allocation count excludes coroutine frames, explicit `new Task`, and allocations internal to the platform queue.
It is the structural Job-wrapper allocation count for each path, not allocator instrumentation: Task-backed
standard/custom enqueue reuses TaskState storage. Compare repeated runs from the same compiler build and machine;
the numbers are not a correctness gate. The synchronous leaf uses a no-allocation external optimization barrier so
LLVM cannot fold the entire baseline loop into a constant.

### 0.5.3 runtime Domain baseline

2026-08-27 on arm64 macOS 26.5, Jiang 0.5.3/LLVM 22.1.8, five runs of 100,000 iterations for each binary produced
these median values. The pre-runtime column uses the committed release HEAD built through the same 0.5.3 bootstrap
chain; its benchmark source differs only by the current `Void` and explicit `@life()` spellings required to compile it.

| Path | Pre-runtime ns/op | Runtime Domain ns/op | Job-wrapper allocations |
| --- | ---: | ---: | ---: |
| synchronous | 0.99 | 0.98 | 0 |
| same-domain | 4.82 | 4.86 | 0 |
| scoped-task | 12.71 | 12.93 | 0 |
| heap-owner | 140.84 | 142.88 | 0 |
| cross-domain serial | 3467.94 | 3546.42 | 0 |
| global-enqueue | 3060.86 | 3143.27 | 0 |
| main-enqueue | 4345.29 | 4636.47 | 0 |
| custom inline enqueue | 85.92 | 84.35 | 0 |
| reused runtime inline Domain | — | 127.31 | 0 |
| runtime inline Domain lifecycle | — | 299.49 | 0 |
| immediate-resume | 156.29 | 151.16 | 0 |
| cancel-before-start | 13.14 | 13.20 | 0 |
| eight serial Domains | 1760.21 | 1805.71 | 0 |

Static and owned scheduling have separate request, handoff, and callback envelopes while sharing one scheduler state
machine and Task ABI. Disassembly confirms that the static scoped-Task completion path has the same instruction shape
as the pre-runtime binary: it performs no executor load, ownership check, lease refcount, or TLS operation. The shortest
paths remain within measurement noise, while system-queue rows vary with host scheduling and are supporting evidence
rather than a correctness gate.

With the same inline Executor implementation, a reused runtime Domain costs 127.31 ns/op versus 84.35 ns/op for the
canonical static Domain, an absolute dynamic-identity cost of 42.96 ns/op. Creating and destroying the runtime Domain
and Executor on every operation costs 299.49 ns/op, a lifecycle increment of 172.18 ns/op over reuse.

### 0.5.0 development baseline

2026-07-30 on arm64 macOS 26.5, Jiang 0.5.0/LLVM 22.1.8, five runs of 10,000 iterations produced these median
values:

| Path | Median ns/op | Job-wrapper allocations |
| --- | ---: | ---: |
| synchronous | 1.0 | 0 |
| same-domain | 4.9 | 0 |
| scoped-task | 14.1 | 0 |
| heap-owner | 137.6 | 0 |
| cross-domain serial | 3505.5 | 0 |
| global-enqueue | 3057.3 | 0 |
| main-enqueue | 3904.6 | 0 |
| custom inline enqueue | 83.5 | 0 |
| immediate-resume | 146.5 | 0 |
| cancel-before-start | 14.4 | 0 |
| eight serial Domains | 1752.6 | 0 |

The preceding same-machine 1,000-iteration development sample measured the shared legacy rows at approximately
5/13/139/3795/270/27/1790 ns/op. The five-run medians show no unexplained regression in the synchronous,
same-Domain, Task-owner, serial cross-Domain, immediate-resume, cancellation, or multi-Domain paths.
After the main queue began reusing TaskState Job storage, its five-run median fell from 5219.5 to
3904.6 ns/op while keeping zero Job-wrapper allocations.

## Compiler scaling benchmark

`coroutine_compile_bench.sh` generates one large async function with configurable branch/Task/suspend-point count.
It measures the JIL-only `--check` path separately from LLVM IR generation, reports child-process peak RSS, and enables
`--jil-stats` to report the package JIL arena's monotonically accumulated used/reserved bytes:

```sh
JIANGC=./build/bin/jiangc.next \
JIANG_COMPILE_BENCH_BRANCHES=256 \
JIANG_COMPILE_BENCH_RUNS=3 \
bash ./script/coroutine_compile_bench.sh
```

Set `JIANG_COMPILE_BENCH_SHAPE` to `direct_branch`, `task`, or `branch_task` to separate ordinary suspend CFG,
sequential scoped-Task bookkeeping, and their interaction.

Compare branch counts such as 64/128/256/512. JIL arena usage should scale with CFG size; a disproportionate increase
between sizes is the signal to revisit pass-generation or per-body storage. The LLVM case includes the same frontend/JIL
work, so its elapsed/RSS delta over the JIL case approximates backend code-generation cost.
