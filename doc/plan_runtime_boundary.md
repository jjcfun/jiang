# Jiang Stage1 Runtime Boundary Plan

## 目标

在 build system 收口之后，当前 Stage1 的下一阶段主线是固定 runtime ABI 与标准库 public surface，而不是直接切 LLVM 或启动 Stage2 重构。

本阶段完成条件：

- `include/runtime.h` 作为唯一宿主 runtime 入口继续成立
- Stage0 注册的 runtime intrinsic 固定为 6 个
- `std` 对外表面固定为 `std.io`、`std.debug`、`std.fs`
- `jiang.build`、标准库注入、bootstrap 入口与现有回归继续稳定
- libc 依赖集中在宿主 C 层，而不是继续向 Jiang 标准库表面扩散

## 当前固定边界

Stage1 runtime ABI：

- `__intrinsic_print`
- `__intrinsic_assert`
- `__intrinsic_read_file`
- `__intrinsic_file_exists`
- `__intrinsic_malloc`
- `__intrinsic_free`
- `__intrinsic_memmove`
- `__intrinsic_alloc_ints`
- `__intrinsic_alloc_bytes`

Stage1 public std surface：

- `std.io`
- `std.debug`
- `std.fs`

边界规则：

- 普通 Jiang 项目应优先使用 `std.*`
- raw intrinsic 允许标准库实现、bootstrap 编译器内部基础设施与编译器生成的过渡 glue 继续直接使用
- 裸 `print(...)` / `assert(...)` 继续保留为 Stage0 与测试兼容入口，但不是标准库 API

## 当前不做什么

- 不实现 `build.jiang`
- 不引入新的宿主 intrinsic
- 不把 `std.fs.File` 扩成真实 OS 文件描述符 API
- 不做 LLVM / 原生后端迁移
- 不追求完全去 libc

## 验收

- `bash ./script/runtime_boundary_smoke.sh`
- `bash ./script/test.sh`
