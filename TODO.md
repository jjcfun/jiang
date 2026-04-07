# Jiang 编译器 TODO

## 当前状态

- Stage0 已完成，并继续承担当前稳定宿主编译器职责。
- Stage1 已完成，`bootstrap/` 是当前唯一正式自举主线。
- `stage1c`、`bootstrap/jiang.build` 与 `script/stage1_complete_smoke.sh` 已固定为 Stage1 正式工作流。
- Stage2 已启动，`compiler/` 当前已具备最小 `frontend -> JIR -> C` 骨架与 `stage2_emit_c_smoke.sh`。

更详细的阶段说明见 [doc/develop.md](doc/develop.md)。

## 当前重点

- [x] 启动 Stage2 主线定义，并把 `compiler/` 收成新的正式实现目录
- [ ] 继续把 Stage2 从最小 `emit-c` 闭环扩到更完整的前端与后端能力
- [ ] 继续评估 LLVM 是否已经具备成为默认后端的条件
- [ ] 保持 Stage0 / Stage1 / LLVM 三条回归链持续稳定

## Stage2 功能安排

### 1. Stage2 编译器骨架

- [ ] 在 `compiler/` 中建立新的 Stage2 目录结构
- [ ] 明确 Stage2 的入口层、核心模块层、后端层边界
- [ ] 定义 Stage2 的正式 build / smoke / selfhost 验收路径
- [ ] 保持 `bootstrap/` 继续作为已完成的 Stage1 基线，不与 Stage2 实现混写

### 2. IR 与编译链重构

- [ ] 重新梳理 Stage2 的前端到后端分层
- [ ] 评估并确定是否引入 `AST -> HIR -> MIR -> backend` 的中间层结构
- [ ] 减少当前 Stage1 中为 C emitter 服务的过早耦合
- [ ] 为后续多后端共享 lowering 规则准备更稳定的 IR 边界

### 3. 默认后端决策

- [ ] 对比 C 后端与 LLVM 后端在代表性入口上的行为一致性
- [ ] 补充 LLVM 路径在 build system、bootstrap 入口和标准库上的稳定性证据
- [ ] 明确“LLVM 成为默认后端”的验收标准
- [ ] 如果 LLVM 条件不足，继续维持 C 为默认后端并缩小差异面

### 4. Stage2 自举路线

- [ ] 定义 Stage2 第一阶段允许依赖的语言子集
- [ ] 规划 Stage2 如何复用或替换当前 Stage1 的模块加载、符号、类型、IR 基础设施
- [ ] 设计 Stage2 从 `compiler/` 到自编译闭环的阶段性里程碑
- [ ] 明确 Stage2 与 Stage1 的切换条件，而不是并行长期双主线发展

### 5. 语言与编译器能力升级

- [ ] 规划自定义语法 / 子语言能力在 Stage2 的落点
- [ ] 规划更稳定的模块系统、包管理接口与工程能力边界
- [ ] 评估泛型、异步、所有权等高级特性的实现优先级
- [ ] 规划标准库从 Stage1 最小集合向 Stage2 完整集合的扩展路线

## 持续性任务

### 回归与质量

- [ ] 保持 `script/test.sh`、`script/stage1_complete_smoke.sh`、`script/test_llvm_backend.sh` 持续可用
- [ ] 对新增 Stage2 主线建立独立 smoke，避免破坏已完成的 Stage1 基线
- [ ] 继续压缩 warning、路径分叉和行为不一致问题

### 文档

- [ ] 让 `README.md` 继续只保留高层说明和正式入口
- [ ] 让 `doc/develop.md` 继续承担阶段设计、边界和内部路线说明
- [ ] 在 Stage2 启动后补一份独立的 Stage2 设计文档
