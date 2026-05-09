# Jiang TODO

当前目标：先把 stage1 自举链路的 IR 架构收稳，再继续增量编译后续阶段。

## 阶段 1：JIR 改为 CFG 形式

- [x] 定义 CFG JIR 数据结构。
  - [x] `JirCfgFunction` 持有 `BasicBlock[]`。
  - [x] `BasicBlock` 持有线性 `Instruction[]` 和 `Terminator`。
  - [x] `Instruction` 使用 `ValueId` 作为 operand/result。
  - [x] `Terminator` 覆盖 `return` / `branch` / `cond_branch` / `switch` / `throw`。
- [ ] 将 tree JIR 的表达式/语句 lowering 改为 CFG lowering。
  - [x] 函数声明生成 CFG function + entry block。
  - [x] 直线语句和浅层表达式投影为 CFG instruction。
  - [x] codegen 支持最小 CFG literal return 闭环。
  - [x] CFG 表达式 lowering 按依赖顺序输出 literal/binary instruction。
  - [x] codegen 支持 CFG integer/bool binary return 闭环。
  - [ ] `if` / `while` / `switch` / `try` 生成 block + terminator。
    - [x] `if` statement 生成 `cond_branch` + then/else/merge blocks。
    - [x] `while` 生成 cond/body/end blocks。
    - [ ] `switch` 生成 case/default blocks。
    - [ ] `try` 生成 normal/catch/end blocks。
  - [x] call / field / index / cast / binary / aggregate init 生成 typed instruction。
  - [ ] 去掉 codegen 前仍需递归遍历表达式树的路径。
- [ ] 调整 call target 表示。
  - [ ] call instruction 直接携带 `target_decl_id` 和可选 `JirCallTargetRef`。
  - [ ] `resolve_jir_call_targets` 改为遍历 CFG instruction。
- [ ] 调整 type layout 和 codegen 输入。
  - [ ] `type_layout` 只依赖 concrete `TypeId` 和 concrete type decl。
  - [ ] codegen 按 block/instruction/terminator 翻译，不做泛型判断。
- [ ] 删除不再使用的 tree JIR clone 辅助结构。

## 阶段 2：完善 JIR 单态化

- [ ] 单态化输入输出统一为 CFG JIR。
- [ ] 实例 key 使用 `template module + template decl + concrete type args`。
- [ ] clone function 时按 block/instruction 线性复制。
  - [ ] 建立 `old ValueId -> new ValueId` 映射。
  - [ ] 建立 `old BlockId -> new BlockId` 映射。
  - [ ] 类型替换只发生在 monomorph 阶段，不进入 codegen。
- [ ] 生成 concrete function/type instance。
  - [ ] 泛型函数调用改指向 concrete function。
  - [ ] concrete nominal type decl materialize 后再进入 layout。
- [ ] 移除 codegen 中所有泛型判断或查找补偿。
- [ ] 验证泛型 samples、method call、cross-module generic call。

## 阶段 3：完成 stage1 自举

- [ ] stage1 编译 `compiler/interner.jiang` 不崩溃。
- [ ] stage1 能编译完整 compiler graph。
- [ ] stage1 生成的 compiler 能通过 `build_stage1` / compiler tests。
- [ ] self-compile 产物与当前 stage1 行为一致。
- [ ] 清理临时兼容代码、调试输出和过渡 TODO。

## 后续：增量编译 v2/v3

- [ ] declaration/function-level incremental。
- [ ] `DeclStableKey` / `TypeStableKey` / `InstanceStableKey` 落盘实现。
- [ ] per-module object reuse。
- [ ] multi-object linker。
- [ ] cross-module public API dirty propagation。
