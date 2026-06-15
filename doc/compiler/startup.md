# Startup

Jiang 的启动路径分成三层：

- platform entry：操作系统或 hosted runtime 调用的入口。
- language entry：Jiang root module 的 `main`。
- startup state：platform entry 写入、Jiang system API 读取的启动时状态。

## Hosted Entry

0.3 系列的 hosted executable 入口仍是 C ABI 形状的 `main(argc, argv)`。backend 会把这个函数作为
runtime entry 生成出来，并在函数体开始时写入 `__jiang_startup_state`。

`argc` 保存到 `StartupState.arguments.length`，`argv` 保存到 `StartupState.arguments.raw`。
写入完成后，runtime entry 调用语言入口 `__jiang_main`。

## Language Entry

Jiang 源码中的 root module `main` 会被 backend lowering 为 `__jiang_main`。用户源码不直接声明
`__jiang_main`，也不接收 `argc/argv` 参数。需要启动参数时，通过 `system.process.arguments()` 读取。

0.3.1 支持的 `main` 返回类型：

- `()`：进程 exit code 为 0。
- 整数类型：返回值作为进程 exit code。

其他返回类型应在 type check 阶段报错。

## Startup State

`system/startup.jiang` 固定内部符号：

- `main`：hosted platform entry。
- `__jiang_main`：language entry。
- `__jiang_startup_state`：启动状态 global。

`StartupState` 只保存启动瞬间由平台入口交给语言运行时的初始事实。当前只包含
`ProgramArguments`。运行过程中会变化的 process 状态不放在这里。

`system.process.arguments()` 直接读取 `__jiang_startup_state.arguments`，不再通过 macOS
`_NSGetArgc/_NSGetArgv` 或 provider 临时符号读取 argv。

## Future Entries

no-libc `_start`、Wasm entry 和 Windows entry 后续也应遵循同一边界：平台入口负责初始化
`__jiang_startup_state`，然后调用 `__jiang_main`。真实 no-libc startup object、syscall 和 inline asm
后置到 proposal。
