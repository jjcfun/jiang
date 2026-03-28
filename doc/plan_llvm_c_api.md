# Jiang Stage1 LLVM C API 接入计划

## Summary

目标是在当前 Stage1 textual LLVM IR spike 已经验证可行的前提下，把实验性 LLVM 后端逐步迁到 LLVM C API，实现一个更稳定、可扩展的实验性后端入口。

本阶段不是把正式主线从 `JIR -> C` 切到 LLVM，而是回答下面三个问题：

- Jiang 当前的 JIR 是否足够稳定到可以直接映射到 LLVM C API
- 冻结后的 runtime ABI 是否足够支撑 LLVM C API 路径下的最小闭环
- 未来如果把 LLVM 路线继续扩大，最小阻塞点会落在 API 接入、JIR 设计还是 runtime 边界

完成标准：

- 仓库中存在基于 LLVM C API 的实验性后端入口
- 该入口至少覆盖当前 textual spike 已支持的最小语义子集
- 默认正式主线仍是 `JIR -> C -> cc`
- LLVM C API 路径有单独 smoke，并保持 `script/test.sh` 全通过

## Key Changes

### 1. 固定 LLVM C API 接入边界，不把它变成通用 FFI

本阶段只做宿主 C 层内的 LLVM C API 接入，不做 Jiang 语言级 FFI，也不做通用 `extern`。

明确边界：

- LLVM C API 只在宿主编译器实现中使用
- 不新增 Jiang 语言层的 `extern` / C header import 机制
- 不做 Jiang 的 LLVM bindings
- 不把 LLVM 暴露成公共标准库能力

也就是说，本阶段的结构应保持为：

```text
JIR -> C 宿主中的 LLVM C API bridge -> LLVM Module/IR
```

而不是：

```text
Jiang -> LLVM bindings -> LLVM
```

### 2. 引入最小 LLVM C API 构建链

当前 textual spike 不依赖 LLVM 开发库，只依赖工具链。下一步要补齐最小 API 接入所需构建能力：

- 在 `CMakeLists.txt` 中通过 `llvm-config` 获取 C flags / libs
- 引入最小头文件：
  - `llvm-c/Core.h`
  - `llvm-c/Analysis.h`
- 后续视需要再加：
  - `llvm-c/Target.h`
  - `llvm-c/TargetMachine.h`

本阶段要求：

- 若系统存在 LLVM C API 开发环境，则默认构建实验路径
- 若环境缺失，编译器主线不应被破坏
- 如果需要，可在 CMake 中把 LLVM API 路径挂成显式选项，避免硬依赖导致主线构建失败

### 3. 先迁移当前 textual spike 已覆盖的最小语义

LLVM C API 路径的第一步不是扩语言覆盖，而是复制当前已验证的最小子集：

- 基础类型：`Int`、`Bool`
- 最小聚合：`UInt8[]` 作为 UTF-8 字节串切片
- 表达式：整数字面量、字符串字面量、局部变量读写、算术、比较
- 控制流：`if`、`while`
- 函数：普通函数定义、参数、调用、`return`
- runtime / std 最小能力：
  - `print`
  - `assert`
  - `std.io.print`
  - `std.debug.assert`

实现顺序应以“与 textual spike 对齐”为原则，而不是一开始就扩大 JIR 指令覆盖。

### 4. 保留 textual emitter 作为对照路径

在 LLVM C API 路径成熟之前，不应删除当前 [src/llvm_gen.c](/Users/jjc/project/jiang/next/src/llvm_gen.c) 的 textual emitter 思路，而应保留一个清晰的对照面。

原因：

- 便于快速比较 JIR 到 LLVM 的形状是否一致
- 便于在 API 路径不稳定时快速回退
- 便于区分“JIR 设计问题”和“LLVM C API 接入问题”

允许的实现方式：

- 保留当前 emitter 文件，新增 API emitter 文件
- 或在现有 emitter 内部拆成 textual / api 两条后端路径

但要求：

- 对外仍保持一个实验性入口，不把 CLI 复杂度继续扩张
- 默认用户路径不被实验分支污染

### 5. 固定 runtime ABI 与 std 表面，不因 LLVM C API 扩张边界

本阶段仍然只围绕 Stage1 已冻结的 runtime ABI 工作：

- `__intrinsic_print`
- `__intrinsic_assert`
- `__intrinsic_read_file`
- `__intrinsic_file_exists`
- `__intrinsic_alloc_ints`
- `__intrinsic_alloc_bytes`

本阶段不新增新的 `__intrinsic_*`，也不改 `std` public surface：

- `std.io`
- `std.debug`
- `std.fs`

当前 API 路径优先补齐的仍是：

- `print/assert`
- `std.io.print`
- `std.debug.assert`

后续若继续扩大，则优先：

- `std.fs.exists`
- `std.fs.read_all`

### 6. 为后续正式后端评估保留清晰停止条件

本阶段的成功不等于“正式切 LLVM 主线”。只有在下面条件成立后，才值得继续推进：

- LLVM C API 路径覆盖当前 textual spike 最小子集
- smoke 稳定
- `script/test.sh` 不回归
- 接入后没有迫使 runtime ABI 或 JIR 发生大规模返工

如果遇到以下情况，应停止扩大，而不是继续堆实现：

- 需要重写大量 JIR 才能自然映射 LLVM
- 必须新增大量 runtime intrinsic 才能跑通当前最小 std 路径
- LLVM C API 路径的复杂度已经显著高于当前收益

出现这些情况时，应先回到“是否继续走 LLVM 主线”的架构评估，而不是默认继续推进。

## Test Plan

必须通过以下回归，才认为本阶段完成：

- `bash ./script/llvm_spike_smoke.sh`
- `bash ./script/test.sh`

并且 LLVM C API 路径至少需要覆盖：

- 纯 `Int/Bool` 控制流样例
- `import std; std.io.print(...)`
- `import std; std.debug.assert(...)`

若本阶段引入了新的实验性实现开关，还应确保：

- 无 LLVM C API 环境时，默认 `JIR -> C` 主线仍可构建
- textual spike 仍可作为对照路径使用

## Assumptions

- 本阶段不实现 Jiang 语言级 `extern`
- 本阶段不实现 Jiang 的 LLVM binding 库
- 本阶段不替换默认 `JIR -> C` 正式主线
- 本阶段不改 `jiang.build`、模块模型、runtime ABI、UTF-8 字节串语义
- 本阶段优先验证“LLVM C API 是否适合继续扩大”，而不是追求完整后端覆盖
