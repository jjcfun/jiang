# Coroutine runtime benchmark

Run the benchmark with the release-mode next compiler:

```sh
JIANGC=./build/bin/jiangc.next \
JIANG_BENCH_ITERATIONS=10000 \
JIANG_BENCH_RUNS=5 \
bash ./script/coroutine_bench.sh
```

The benchmark warms executor and allocator state before measuring these paths:

- direct async call;
- scoped `Task<T>` creation and await;
- `Task<T>^` allocation, parameter/return forwarding, and await;
- cross-Domain enqueue and await;
- immediate suspend/resume;
- cancellation before a scoped task starts;
- repeated work spread across eight serial Domains.

Each line reports elapsed monotonic time and nanoseconds per logical operation. Compare repeated runs from the same
compiler build and machine; the numbers are not a correctness gate and allocator cache hits do not change the JIL
allocation-structure baseline.

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
