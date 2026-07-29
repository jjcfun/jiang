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
- immediate suspend/resume;
- cancellation before a scoped task starts;
- repeated work spread across eight serial Domains.

Each line reports elapsed monotonic time, nanoseconds per logical operation, and Jiang runtime Job-wrapper allocations.
The allocation count excludes coroutine frames, explicit `new Task`, and allocations internal to the platform queue.
It is the structural Job-wrapper allocation count for each path, not allocator instrumentation: Task-backed
standard/custom enqueue reuses TaskState storage. Compare repeated runs from the same compiler build and machine;
the numbers are not a correctness gate. The synchronous leaf uses a no-allocation external optimization barrier so
LLVM cannot fold the entire baseline loop into a constant.

### 0.5.0 development baseline

2026-07-30 on arm64 macOS 26.5, Jiang 0.5.0/LLVM 22.1.8, five runs of 10,000 iterations produced these median
values:

| Path | Median ns/op | Job-wrapper allocations |
| --- | ---: | ---: |
| synchronous | 1.0 | 0 |
| same-domain | 4.8 | 0 |
| scoped-task | 12.7 | 0 |
| heap-owner | 132.4 | 0 |
| cross-domain serial | 3490.4 | 0 |
| global-enqueue | 3060.2 | 0 |
| main-enqueue | 5219.5 | 0 |
| custom inline enqueue | 101.1 | 0 |
| immediate-resume | 147.8 | 0 |
| cancel-before-start | 13.2 | 0 |
| eight serial Domains | 1719.6 | 0 |

The preceding same-machine 1,000-iteration development sample measured the shared legacy rows at approximately
5/13/139/3795/270/27/1790 ns/op. The five-run medians show no unexplained regression in the synchronous,
same-Domain, Task-owner, serial cross-Domain, immediate-resume, cancellation, or multi-Domain paths.

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
