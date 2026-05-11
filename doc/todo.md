# Jiang TODO

当前目标：先把 stage1 自举链路的 IR 架构收稳，再继续增量编译后续阶段。

## 阶段 1：JIR 改为 CFG 形式

- [x] 定义 CFG JIR 数据结构。
  - [x] `JirCfgFunction` 持有 `BasicBlock[]`。
  - [x] `BasicBlock` 持有线性 `Instruction[]` 和 `Terminator`。
  - [x] `Instruction` 使用 `ValueId` 作为 operand/result。
  - [x] `Terminator` 覆盖 `return` / `branch` / `cond_branch` / `switch` / `throw`。
- [x] 将 tree JIR 的表达式/语句 lowering 改为 CFG lowering。
  - [x] 函数声明生成 CFG function + entry block。
  - [x] function/init declaration 不再挂旧 tree body，codegen 以 `cfg_function_id` 作为函数体入口。
  - [x] 直线语句直接生成 CFG marker/terminator，不再经单语句 tree block 过渡。
  - [x] 浅层表达式投影为 CFG instruction。
  - [x] codegen 支持最小 CFG literal return 闭环。
  - [x] CFG 表达式 lowering 按依赖顺序输出 literal/binary instruction。
  - [x] codegen 支持 CFG integer/bool binary return 闭环。
  - [x] codegen 支持已验证 CFG blocks 的 branch / cond_branch / return 翻译。
  - [x] `if` / `while` / `switch` / `try` 生成 block + terminator。
    - [x] `if` statement 生成 `cond_branch` + then/else/merge blocks。
    - [x] `while` 生成 cond/body/end blocks。
    - [x] `switch` 生成 case/default blocks。
    - [x] `try` 生成 normal/catch/end blocks。
    - [x] `for_range` 生成 local/cond/body/step/end blocks。
    - [x] `for_each` 生成 init/cond/bind/body/step/end blocks。
    - [x] `coalesce_control_local` 生成 some/none/end blocks。
  - [x] `if is-pattern` / `switch` case pattern bind 直接生成 CFG marker。
  - [x] call / field / index / cast / binary / aggregate init 生成 typed instruction。
  - [x] `lower_jir` 内部不再用 `JirBlock/JirStmt` 作为复杂表达式和 defer 的过渡 buffer。
    - [x] 函数/init body 不再保存 tree。
    - [x] 函数正文普通语句不再经 tree adapter。
    - [x] condition/switch pattern bind 不再经 tree wrapper。
    - [x] `defer` payload 改为 CFG inline lowering。
    - [x] `if` / `switch` / `try` / block expression value lowering 改为 CFG 原生。
    - [x] `??` / logic short-circuit expression lowering 改为 CFG 原生。
  - [x] 去掉 `lower_jir` 中旧 tree-to-CFG adapter 和旧 `JirBlock` lowering 链路。
  - [ ] 去掉 codegen 前仍需递归遍历表达式树的路径。
    - [x] CFG marker instruction 携带 local/assign/pattern/expr/for_each/coalesce 的 value operand。
    - [x] local/assign/pattern/for_each/coalesce/return 优先使用 CFG value coercion。
    - [x] call instruction codegen 使用 CFG operand，且不提前执行短路分支中的副作用。
    - [x] 删除旧 `expr_value_cg` 的 call / aggregate value fallback。
    - [ ] address/place projection、runtime builtin、struct init default/init call 仍需从表达式 fallback 收敛到 CFG operand。
- [x] 调整 call target 表示。
  - [x] call instruction 直接携带 `target_decl_id` 和可选 `JirCallTargetRef`。
  - [x] `resolve_jir_call_targets` 改为遍历 CFG instruction。
- [ ] 调整 type layout 和 codegen 输入。
  - [ ] `type_layout` 只依赖 concrete `TypeId` 和 concrete type decl。
  - [ ] codegen 按 block/instruction/terminator 翻译，不做泛型判断。
- [x] 将 `JirStmt` 中只作为 CFG marker metadata 使用的旧 tree 字段收缩成 metadata-only 结构。
- [x] 删除不再使用的 tree JIR clone 辅助结构。

## 阶段 2：完善 JIR 单态化

- [ ] 单态化输入输出统一为 CFG JIR。
- [ ] 实例 key 使用 `template module + template decl + concrete type args`。
- [x] clone function/init 时复制 CFG function。
  - [x] 建立 `old ValueId -> new ValueId` 映射。
  - [x] 保持 CFG block 顺序和 `BlockId` 稳定复制。
  - [ ] 类型替换只发生在 monomorph 阶段，不进入 codegen。
- [ ] 生成 concrete function/type instance。
  - [ ] 泛型函数调用改指向 concrete function。
  - [ ] concrete nominal type decl materialize 后再进入 layout。
- [ ] 移除 codegen 中所有泛型判断或查找补偿。
- [ ] 验证泛型 samples、method call、cross-module generic call。

## 阶段 3：完成 stage1 自举

- [x] stage1 编译 `compiler/interner.jiang` 不崩溃。
- [x] stage1 能编译完整 compiler graph。
- [x] stage1 生成的 compiler 能通过 `build_stage1` / compiler tests。
- [x] selfhost compiler 运行 `tests/samples/minimal.jiang` 不再失败。
  - [x] 不再以 trap / `-1` 退出。
- [x] selfhost compiler 能通过完整 `stage1_test`。
- [x] self-compile 产物与当前 stage1 行为一致。
- [ ] 清理临时兼容代码、调试输出和过渡 TODO。

## 后续：增量编译 v2/v3

- [ ] declaration/function-level incremental。
- [ ] `DeclStableKey` / `TypeStableKey` / `InstanceStableKey` 落盘实现。
- [ ] per-module object reuse。
- [ ] multi-object linker。
- [ ] cross-module public API dirty propagation。
