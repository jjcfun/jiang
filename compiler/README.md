# Jiang Compiler Stage2

`compiler/` 现在是 Stage2 的正式主线目录，也是当前仓库里唯一继续演进的编译器主线。

从仓库定位上看，当前仓库已经应按 **Stage2 主线仓库** 理解：`src/`、`bootstrap/` 和历史 bootstrap 资产仍然存在，但它们已经退到 bootstrap、冷启动和历史回归职责。后续如果要把冷启动体系单独分仓，最自然的拆法是把 bootstrap orchestration 和历史阶段整理到独立的 `jiang-bootstrap` 仓库，而当前仓库继续只承载 Stage2+ 主线。

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
- Stage1 仍然是冻结的 bootstrap 基线
- `compiler/` 中的新实现优先按层落位，不再堆平铺文件
- 在 Stage2 正式稳定并允许统一语法迁移之前，`compiler/` 源码继续约束在 Stage1 可解析的语法子集内；新的 Stage2 用户语法优先先落到语言能力和测试，不直接重写 `compiler/` 自身源码风格

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

- `script/build_stage2.sh` 现在默认优先使用已有旧版 `stage2c` 作为 bootstrap compiler，并由它重编产出当前 `build/stage2c`
- 自动解析顺序现在是：`STAGE2_BOOTSTRAP_STAGE2 -> 本地 dist 包中的 jiang -> ~/.jiang/bin/jiang -> build/stage2c`
- 当前推荐的 bootstrap compiler 仍然是“上一版 release 包中的 `jiang` 可执行文件”
- 当本地没有可用的旧版 `stage2c` 时，构建链会回退到 `stage1c -> stage2c.bootstrap -> stage2c`
- Stage2 的正式 bootstrap contract 固定在 `compiler/BOOTSTRAP.md`
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

当前第一阶段目标已经完成。后续工作不再是“让 Stage2 可用”，而是：

- 在现有回归约束下继续演进语言与后端能力
- 把剩余 LLVM/C 边角差异作为维护项持续压缩
- 维持 `stage1 -> stage2` 构建链和 Stage2 单点验收入口长期绿色

## Stage2 接管标准

Stage2 替代 Stage1 的验收标准固定为：

- `script/build_stage2.sh` 持续通过，且最终 `build/stage2c` 由 Stage2 自重编产出
- 当前默认会按 `STAGE2_BOOTSTRAP_STAGE2 -> 本地 dist 包中的 jiang -> ~/.jiang/bin/jiang -> build/stage2c` 顺序自动解析 bootstrap compiler
- `script/stage2_complete_smoke.sh` 持续通过
- `script/stage1_complete_smoke.sh` 持续通过，不因 Stage2 演进被打断
- `stage2c` 的 `emit-c` 与 `--emit-llvm` 都保持可用
- `script/stage2_selfhost_smoke.sh` 能完成 `stage2c -> compiler/entries/compiler.jiang -> stage2c.selfhost -> stage2c.roundtrip` 的 roundtrip 验证
- Stage2 已覆盖当前日常主语法子集：
  - 多模块与 `public`
  - `struct` / `enum`
  - `UInt8` / `UInt8[]` / array / pointer
  - 命令式函数、控制流、赋值、调用

当前状态：

- `compiler/` 已按上述标准进入唯一继续演进主线
- `bootstrap/` 保持 Stage1 冻结基线职责
- `stage2_complete_smoke.sh` 是当前 Stage2 的正式单点验收入口
