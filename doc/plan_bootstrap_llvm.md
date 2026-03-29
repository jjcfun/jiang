# Jiang Stage1 Bootstrap LLVM 收口计划

## Summary

目标是在当前 `--backend llvm` 已经成为可选完整后端的基础上，把 LLVM 路径推进到“至少能稳定覆盖一条真实 bootstrap 入口链”。

当前已确认的状态：

- `bootstrap/lexer.jiang` 可以稳定 `--emit-llvm`
- 生成的 `build/lexer.ll` 可以通过 `opt -passes=verify`
- 但 `clang -x ir build/lexer.ll` 与 `lli build/lexer.ll` 当前都会卡住

因此本阶段拆成两步：

1. 先把 bootstrap 的 LLVM IR 生成与验证固定成回归面
2. 再单独定位 bootstrap LLVM 执行/机器码生成卡住的问题

本阶段完成标准：

- `bootstrap/lexer.jiang` 的 LLVM IR 生成稳定
- bootstrap LLVM IR 有独立 smoke
- `TODO.md` / `develop.md` 明确 bootstrap LLVM 是当前后续优先级
- 不破坏当前 `script/test.sh` 与主测试集

## Key Changes

### 1. 固定 bootstrap LLVM 的“已验证边界”

当前 bootstrap LLVM 先只承诺这条链：

```text
bootstrap/lexer.jiang
-> jiangc --emit-llvm
-> build/lexer.ll
-> opt -passes=verify
```

这一步验证的是：

- bootstrap 模块图在 LLVM lowering 下可完成
- 真实 bootstrap 资产能组合成有效 LLVM IR
- 当前阻塞点不在前端或 IR 结构合法性

### 2. 把机器码生成问题单独隔离

当前已观察到：

- `xcrun clang -c -x ir build/lexer.ll ...` 卡住
- `xcrun clang -x ir build/lexer.ll -o ...` 卡住
- `lli build/lexer.ll` 也卡住

因此后续应优先定位：

- 是否存在 bootstrap 场景下特有的 pathological IR
- 是否是某类函数或控制流组合让 LLVM 后端退化
- 是否需要对大函数/大模块的 LLVM emission 做进一步收口

在问题未定位前，不把 bootstrap LLVM 运行态强行并入总回归。

### 3. 新增独立 smoke，而不是污染现有 Stage1 smoke

新增单独脚本：

- `script/bootstrap_llvm_smoke.sh`

它只负责：

- 生成 `bootstrap/lexer.jiang` 的 LLVM IR
- 验证 `.ll` 文件存在
- 调用 `opt -passes=verify`
- 检查产物中存在 `define i32 @main`

不要求运行生成的 bootstrap LLVM 产物。

### 4. 更新当前优先级说明

文档应明确：

- LLVM 后端对普通正向样例已经成立
- 当前下一优先级是 bootstrap LLVM 执行链
- 不是继续扩大语言特性，也不是直接切默认后端

## Test Plan

必须通过：

- `bash ./script/bootstrap_llvm_smoke.sh`
- `bash ./script/test_llvm_backend.sh`
- `bash ./script/test.sh`

## Assumptions

- 默认后端仍是 C
- LLVM 后端仍是可选完整后端
- 本阶段不新增 runtime intrinsic
- 本阶段不改 `jiang.build`
- 本阶段先固定 bootstrap LLVM IR 生成，不强求 bootstrap LLVM 可执行产物立即稳定
