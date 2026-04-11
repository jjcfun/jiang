# Jiang 编译器 TODO

## 当前状态

- Stage0 已完成，并继续承担当前稳定宿主编译器职责。
- Stage1 已完成，`bootstrap/` 是当前正式自举基线，`stage1c` 已固定。
- Stage2 已启动，并且已经具备：
  - `stage1c -> stage2c` 构建链
  - `frontend -> HIR -> JIR -> C`
  - `frontend -> HIR -> JIR -> LLVM`
  - 多模块、`public`、alias import、导出表
  - `struct` / `enum` 基础语义
  - `UInt8` 与 `UInt8[]` 的最小类型闭环
  - `emit-c` / `run` / `error` / `llvm` / `llvm-error` smoke

更详细的阶段说明见 [doc/develop.md](doc/develop.md)。

## 当前重点

- [ ] 收尾 Stage2 的数组 / 聚合类型规则（重点转到剩余聚合一致性）
- [ ] 收紧 Stage2 的类型系统与 `unknown` 容忍路径
- [ ] 继续收紧 LLVM 与 C 后端的剩余差异（下一步是数组到 slice 的转换与更完整聚合语义）
- [ ] 把 Stage2 收成唯一继续演进的主线

## Stage2 主计划

### 1. Slice 与字符串主线

- [x] 支持 `UInt8[]` 的 `.length`
- [x] 支持 `value[index]`
- [x] 支持 `value[index] = rhs`
- [x] 支持字符串字面量到 `UInt8[]`
- [x] 固定 slice 在 C / LLVM / runtime 中的统一 ABI
- [x] 为 slice 补更完整的反向 / 运行级 smoke

涉及文件：
- `compiler/frontend/token_store.jiang`
- `compiler/frontend/lexer.jiang`
- `compiler/frontend/parser_store.jiang`
- `compiler/frontend/parser.jiang`
- `compiler/frontend/hir_store.jiang`
- `compiler/frontend/hir.jiang`
- `compiler/ir/jir/jir_store.jiang`
- `compiler/ir/jir/jir_lower.jiang`
- `compiler/backend/c/emit_c.jiang`
- `compiler/backend/llvm/emit_llvm.jiang`
- `compiler/tests/samples/*`
- `script/stage2_*_smoke.sh`

### 2. 数组与聚合类型第一版

- [x] 支持数组类型
- [x] 支持数组字面量
- [x] 明确数组与 slice 的最小转换规则（当前固定 `UInt8[N] -> UInt8[]`）
- [x] 完成数组在 C / LLVM 中的表示
- [ ] 继续加强 `struct` / `enum` 在多模块、赋值、参数、返回值场景下的一致性
- [ ] 让 HIR/JIR 不再依赖 emitter 兜底判断聚合语义

涉及文件：
- `compiler/frontend/type_store.jiang`
- `compiler/frontend/hir.jiang`
- `compiler/ir/jir/jir_store.jiang`
- `compiler/ir/jir/jir_lower.jiang`
- `compiler/backend/c/emit_c.jiang`
- `compiler/backend/llvm/emit_llvm.jiang`

### 3. 类型系统第一版

- [x] 完成 builtin type：`Int` / `Bool` / `Void` / `UInt8`
- [x] 完成命名类型：`struct` / `enum`
- [x] 完成复合类型：array / slice / pointer
- [x] 完成赋值类型检查
- [x] 完成 `return` 类型检查
- [x] 完成 call args / 返回值传播
- [ ] 完成 field access / struct init / enum member / array index 的完整类型传播
- [ ] 收紧 `unknown`，减少当前“容忍后继续走”的路径

涉及文件：
- `compiler/frontend/symbol_store.jiang`
- `compiler/frontend/type_store.jiang`
- `compiler/frontend/hir.jiang`

### 4. 模块系统收尾

- [x] 继续稳定 direct import / transitive import 规则
- [x] 继续稳定 alias import 的命名空间解析
- [ ] 收紧 duplicate / visibility / export 诊断
- [x] 固定 imported type / function 在 HIR / JIR / LLVM 中的 identity
- [ ] 为后续 build graph 留出稳定模块边界

涉及文件：
- `compiler/frontend/module_loader.jiang`
- `compiler/frontend/path_utils.jiang`
- `compiler/frontend/hir.jiang`
- `compiler/frontend/symbol_store.jiang`
- `compiler/frontend/type_store.jiang`

### 5. LLVM 后端对齐

- [x] 补全 slice lowering
- [x] 补全数组 lowering
- [x] 继续补 pointer lowering
- [x] 与 C 后端在代表性样例上建立行为对齐回归
- [x] 完成 LLVM 错误路径与运行级验证

涉及文件：
- `compiler/backend/llvm/emit_llvm.jiang`
- `compiler/ffi/llvm/core.jiang`
- `compiler/ffi/llvm/llvm_shim.c`
- `script/stage2_llvm_smoke.sh`
- `script/stage2_llvm_error_smoke.sh`

### 6. Stage2 正式化

- [x] 收口 `stage2c` CLI（默认 `emit-c`，支持 `--emit-llvm` 与 `--help`）
- [x] 固定 Stage2 正式验收入口
- [ ] 保持 `stage1 -> stage2` 构建链持续绿色
- [ ] 明确 Stage2 替代 Stage1 的验收标准
- [ ] 在 Stage2 足够稳定后冻结 Stage1，只保留 bootstrap 基线职责

涉及文件：
- `compiler/entries/compiler.jiang`
- `compiler/stage2_driver.c`
- `script/build_stage2.sh`
- `script/test.sh`

## 持续性任务

### 回归与质量

- [x] 保持 `script/test.sh` 持续可用
- [x] 保持 `script/stage1_complete_smoke.sh` 持续可用
- [x] 保持 `script/stage2_emit_c_smoke.sh` 持续可用
- [x] 保持 `script/stage2_run_smoke.sh` 持续可用
- [x] 保持 `script/stage2_error_smoke.sh` 持续可用
- [x] 保持 `script/stage2_llvm_smoke.sh` 持续可用
- [x] 保持 `script/stage2_llvm_error_smoke.sh` 持续可用

### 文档

- [ ] 根 `README.md` 继续只保留高层说明
- [ ] `doc/develop.md` 继续承担阶段边界与实现状态说明
- [ ] `compiler/README.md` 继续承担 Stage2 主线结构与范围说明
