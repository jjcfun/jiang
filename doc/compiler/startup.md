# Startup

Jiang 的启动路径分成三层：

- platform entry：操作系统或 hosted runtime 调用的入口。
- language entry：Jiang root module 的 `main`。
- startup state：platform entry 写入、Jiang system API 读取的启动时状态。

## Hosted Entry

当前 hosted executable 入口仍是 C ABI 形状的 `main(argc, argv)`。它由
`system/startup.jiang` 中的普通函数定义，并通过 `@link_symbol("main")` 绑定到链接层符号。

`argc` 保存到 `StartupState.arguments.length`，`argv` 保存到 `StartupState.arguments.raw`。
写入完成后，hosted entry 调用语言入口 `__jiang_main`。

## Language Entry

Jiang 源码中的 root module `main` 会被 backend lowering 为 `__jiang_main`。用户源码不直接声明
`__jiang_main`，也不接收 `argc/argv` 参数。需要启动参数时，通过 `system.process.arguments()` 读取。

当前支持的 `main` 返回类型：

- `()`：进程 exit code 为 0。
- 整数类型：返回值作为进程 exit code。

其他返回类型应在 type check 阶段报错。

## Startup State

`system/startup.jiang` 固定内部链接符号：

- `main`：hosted platform entry。
- `__jiang_main`：language entry。
- `__jiang_startup_state`：启动状态 global。

`main` 和 `__jiang_startup_state` 都由源码普通声明定义，通过 `@link_symbol` 绑定链接层符号。
编译器不再在 LLVM lowering 中合成 startup state global。`__jiang_main` 仍由 JIL lowering 生成，
只负责把用户 root `main` 的返回值适配成进程 exit code。

`StartupState` 只保存启动瞬间由平台入口交给语言运行时的初始事实。当前只包含
`ProgramArguments`。运行过程中会变化的 process 状态不放在这里。

`system.process.arguments()` 直接读取 `__jiang_startup_state.arguments`，不再通过 macOS
`_NSGetArgc/_NSGetArgv` 或 provider 临时符号读取 argv。

## Future Entries

no-libc `_start`、Wasm entry 和 Windows entry 后续也应遵循同一边界：平台入口负责初始化
`__jiang_startup_state`，然后调用 `__jiang_main`。Linux no-libc `_start` 也应是源码普通函数，
并通过 `@link_symbol("_start")` 绑定链接层入口符号。
