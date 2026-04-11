# Jiang Compiler Stage2

`compiler/` 现在是 Stage2 的正式主线目录。

当前仓库中的目录分工固定为：

- `src/`: 宿主 C 编译器
- `bootstrap/`: 已完成的 Stage1 Jiang 自举编译器主线
- `compiler/`: Stage2 主线目录

当前 Stage2 的目标不是再做一个临时 spike，而是逐步替代 Stage1 成为唯一继续演进的 Jiang 编译器主线。

当前固定分层：

- `entries/`: 顶层入口与工具程序
- `frontend/`: 词法、语法、HIR、模块加载
- `ir/`: 中间表示与 lowering，当前只保留 `jir/`
- `backend/`: 各后端实现，当前预留 `c` 与 `llvm`
- `ffi/`: 宿主 runtime 与外部库绑定
- `support/`: 通用基础设施
- `tests/`: Stage2 自己的样例、golden 和 smoke

当前约束：

- Stage2 不与 `bootstrap/` 混写
- Stage1 仍然是稳定基线
- `compiler/` 中的新实现优先按层落位，不再堆平铺文件

当前目录结构：

- `entries/`
- `frontend/`
- `ir/jir/`
- `backend/c/`
- `backend/llvm/`
- `ffi/runtime/`
- `ffi/llvm/`
- `support/`
- `tests/samples/`
- `tests/golden/`
- `tests/smoke/`

当前已经落地的能力：

- `script/build_stage2.sh` 可通过 `stage1c` 构建 `build/stage2c`
- `compiler/entries/compiler.jiang` 已支持 `emit-c` 与 `emit-llvm`
- `build/stage2c` 已支持最小正式 CLI：默认 `emit-c`、显式 `--emit-c|--emit-llvm`、`--help`
- Stage2 已具备：
  - 命令式函数基础语法
  - 多模块、`public`、alias import、导出表
  - `struct` / `enum` 基础语义
  - `UInt8` / `UInt8[]` / array / pointer 基础类型闭环
  - 数组到 slice 的最小转换规则（当前固定 `UInt8[N] -> UInt8[]`）
  - C 与 LLVM 双后端
- Stage2 当前已有稳定回归：
  - `script/stage2_complete_smoke.sh`
  - `script/stage2_emit_c_smoke.sh`
  - `script/stage2_run_smoke.sh`
  - `script/stage2_error_smoke.sh`
  - `script/stage2_llvm_smoke.sh`
  - `script/stage2_llvm_error_smoke.sh`
- `compiler/support/` 当前已提供：
  - `buffer_int`
  - `buffer_bytes`
  - `intern_pool`
  - `diagnostic`
  - `text_buffer`
  - `Span`
  - `SourceFile`

当前下一阶段的重点不是再起新骨架，而是收尾：

- 收紧 `unknown` 容忍路径
- 继续加强 `struct` / `enum` 在多模块、赋值、参数、返回值场景下的一致性
- 继续收紧 LLVM 与 C 后端的剩余差异
- 把 Stage2 收成唯一继续演进的主线

## Stage2 接管标准

Stage2 替代 Stage1 的验收标准固定为：

- `script/build_stage2.sh` 持续通过，且 `build/stage2c` 由 `stage1c` 构建
- `script/stage2_complete_smoke.sh` 持续通过
- `script/stage1_complete_smoke.sh` 持续通过，不因 Stage2 演进被打断
- `stage2c` 的 `emit-c` 与 `--emit-llvm` 都保持可用
- Stage2 已覆盖当前日常主语法子集：
  - 多模块与 `public`
  - `struct` / `enum`
  - `UInt8` / `UInt8[]` / array / pointer
  - 命令式函数、控制流、赋值、调用

达到以上条件后：

- `compiler/` 视为唯一继续演进的编译器主线
- `bootstrap/` 保持 Stage1 冻结基线职责
