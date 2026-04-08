# Jiang Compiler Stage2

`compiler/` 现在是 Stage2 的正式骨架目录。

当前仓库中的目录分工固定为：

- `src/`: 宿主 C 编译器
- `bootstrap/`: 已完成的 Stage1 Jiang 自举编译器主线
- `compiler/`: Stage2 主线目录

当前 Stage2 的目标不是直接重写完整编译器，而是先固定分层：

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

当前第一阶段已经落地的里程碑：

- `compiler/entries/compiler.jiang` 已能读取单文件输入并输出 C 到 stdout
- `compiler/tests/samples/minimal.jiang` 作为固定 Stage2 样例
- `script/build_stage2.sh` 可构建 `build/stage2c`
- `script/stage2_emit_c_smoke.sh` 已验证 Stage2 emitted C 可被 `cc -c` 编译
- `compiler/support/` 已开始沉淀通用结构，目前包含 `Span` 和 `SourceFile`
- `compiler/support/` 现在还承载了 Stage2 动态数组、驻留池和诊断基础设施
- `compiler/support/text_buffer.jiang` 已提供最小文本构建器，供后续后端输出层复用
