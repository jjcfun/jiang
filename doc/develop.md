# Jiang 编译器开发流程

本文记录 Jiang 的自举和验证流程。常规开发使用上一版正式 release 编译器；只有该编译器
无法直接编译新源码时，才建立一个或多个最小 bootstrap 过渡阶段。

## 常规开发

当前 0.5.5 在独立功能分支开发，使用并排 `bootstrap/0.5.5` worktree 的固定阶段。
阶段源码提交为 `b56c5d4905002ef2255d3d644f00b6f4da4d50b8`，保留旧配置加载入口，
加入编译新源码必需的包声明与常量物化能力，并同时接受 `foo(a: 123)` 和 `foo(a = 123)`。
bootstrap 自身源码保留冒号，feat 分支直接迁移为只接受等号；不需要第二个 bootstrap 阶段。

该固定提交由 0.5.4 stable 编译出 bootstrap next，通过相关回归后直接作为 feat 的种子；
无需生成 bootstrap stable。feat 自身完成 `next -> stable`。
身份与验证记录位于阶段的 `build/named-arguments/identity.json`；该独立目录保留旧阶段产物。

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.5-bootstrap \
BOOTSTRAP_BIN=/path/to/bootstrap-0.5.5/build/named-arguments/bin/jiangc.next \
COMPILER_BUILD_MODE=release \
BOOTSTRAP_DEPTH=next VERIFY=none \
bash ./script/build_next.sh
```

脚本默认版本仍为 0.5.4，适用于该版本开始开发时；原生配置迁移后须显式指定上述固定阶段。
开发保留在功能分支，bootstrap 改动仅在自己的分支提交。

bootstrap compiler 固定使用仓库内的 `build/cache`。`build_next.sh` 在 stable 编译前后
清理该目录；current compiler 使用独立的
`build/artifact-cache/next/<version>`，不会随 bootstrap cache 一起删除。需要自定义时可设置
`NEXT_ARTIFACT_CACHE_DIR`，但它不能包含 `build/cache`，也不能位于 `build/cache` 内。

提交功能前优先运行相关语言测试。需要检查完整语言测试时：

```bash
VERIFY=none bash ./script/build_next.sh
JIANGC=./build/bin/jiangc.next bash ./script/lang_check.sh
```

正式 release 前生成 stable，并执行完整验证：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.5-bootstrap \
BOOTSTRAP_BIN=/path/to/bootstrap-0.5.5/build/named-arguments/bin/jiangc.next \
BOOTSTRAP_DEPTH=stable \
VERIFY=full \
bash ./script/build_next.sh
```

## 破坏性升级

如果上一版 stable 无法解析或编译新源码，先从上一版可编译的源码建立
`bootstrap/<version>`。过渡分支只实现让下一阶段可编译所需的最小能力，不作为用户 release。

如果一轮过渡仍不足，可以增加 `bootstrap/<version>-2`。不要预先固定阶段数量；每个阶段都必须
由前一阶段编译，并且其 `jiangc.next` 必须通过新语法所需的语言测试。

通用链路为：

```text
previous stable
  -> bootstrap/<version>
  -> bootstrap/<version>-2（仅在需要时）
  -> release/<version> next
  -> release/<version> stable
```

阶段数量不受单个 bootstrap 限制，但新增阶段必须由固定的前一阶段以 strict 模式编译，
只需生成并验证该阶段 next；最终仍须完成 release `next -> stable`。
不得通过关闭类型、借用或 lifetime 检查打通交接；临时工作区产物不能自行串接成 bootstrap 链。
只有固定前一阶段确实无法编译下一阶段时，才补充必要的 bootstrap 能力。

release 分支保留自己的线性提交历史；bootstrap 分支只提供编译下一阶段所需的过渡编译器。
各阶段必须使用独立 build 目录，不混用编译产物。

### 固定自举输入与重复构建

每个阶段使用固定的源码 commit/tag，并保留种子编译器、LLVM/linker 的版本及文件哈希。
分支名、未提交的工作区和临时编译器路径不能单独标识可复现输入。构建期间不要修改源码。

导入表达式迁移使用 `bootstrap/0.5.4` 过渡阶段：该阶段保留 stable 可编译的系统模块语法，
由 0.5.3 stable 构建；下一阶段使用其 `0.5.4-bootstrap` 产物：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.4-bootstrap \
BOOTSTRAP_BIN=/path/to/bootstrap-0.5.4/build/bin/jiangc.next \
BUILD_DIR="$PWD/build/repro-a" \
COMPILER_BUILD_MODE=release BOOTSTRAP_DEPTH=stable VERIFY=none \
bash script/build_next.sh
```

重复构建时更换为独立的 `BUILD_DIR`，保持其余输入一致；逐阶段比较同名产物。
`jiangc.next` 和 `jiangc` 的不同文件名可能影响平台签名，不能用二者直接比较代替同名重建验证。
逐字节比较失败时应定位差异，不默认忽略签名或其他元数据。
`VERIFY=none` 仅用于分离构建验证与测试执行，不替代正式发布的完整测试。
可复现构建也不等于无后门证明，不能替代独立工具链验证。

### 0.5.3 enum ADT 过渡

0.5.3 用 payload enum 替代普通 tagged union，并在 compiler 源码中使用 builtin `#doc`。
0.5.2 stable 不能直接解析迁移后的 release 编译器源码。唯一的 bootstrap 支持 payload enum，
并在 lexer 中跳过只影响文档产物的 `#doc`，自身不复制文档 AST、artifact 或 renderer：

```text
Jiang 0.5.2 stable
  -> bootstrap/0.5.3 next
  -> release/0.5.3 next
  -> release/0.5.3 stable
```

先在 `bootstrap/0.5.3` worktree 中直接使用已安装的 0.5.2 stable：

```bash
bash ./script/build_next.sh
```

bootstrap 只需生成 `build/bin/jiangc.next`，不生成 stable。release worktree 必须直接使用该 next
生成 release next 和 stable，不得从未记录身份的任意 `jiangc` 开始冷启动。

### 0.5.2 严格检查过渡模式

0.5.2 的 mutable receiver/place 写能力规则始终执行同一套分析。bootstrap2 使用 audit 编译
release next；生成的 next 随后以 strict 模式编译 stable。audit 仅是 bootstrap 交接内部使用的
过渡能力，release 编译器不提供切换检查模式的命令行参数，也不能将 audit 结果作为验证证据。

上述 audit 机制仅用于重现 0.5.2 历史阶段，应使用对应版本脚本。当前编译器和构建脚本
不提供非严格模式；release candidate 必须严格自举成功后才能进入发布验证。

## Linux 首次 hosted port seed

0.5.0 正式产物只有 macOS arm64 compiler。0.5.1 首次建立 Linux x86_64 hosted 自举时，
平台 seed 桥接“尚无 Linux 可执行 stable compiler”这一 host 缺口。最终 0.5.1 compiler source
包含 0.5.0 无法解析的迁移语法，因此首次 release seed 使用 `0.5.1-bootstrap` 过渡编译器；
发布后的常规构建直接使用 0.5.1 stable。

在已有兼容 0.5.1 compiler 的 host 上生成 Linux compiler ELF object：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.1 \
BOOTSTRAP_BIN=/path/to/compatible/jiangc \
bash ./script/linux_port_seed.sh emit-object
```

把 `build/linux-port-seed/jiangc-x86_64-linux-gnu.o`、同目录 manifest 与同一 source revision
传到 Linux x86_64。Linux host 必须先通过 `script/install_llvm.sh` 下载并校验 Jiang 固定的
LLVM 22.1.8 SDK；只有 LLVM 维护工作才使用 `--from-source`。脚本会校验 source revision、
object SHA-256 和 LLVM fork revision，然后完成
native link 与 `seed -> next -> stable` 两跳自举：

```bash
bash ./script/linux_port_seed.sh bootstrap
```

link 阶段会先编译运行 Linux hosted ABI probe，锁定当前 provider 使用的 `stat`、`dirent`、
pthread storage、`-pthread` 和 `-ldl` 边界。probe 通过后，manifest 记录 source、bootstrap、LLVM、
glibc、kernel、object、seed、next 和 stable 身份。只需诊断 native link 时也可以单独运行：

```bash
bash ./script/linux_port_seed.sh link
```

port seed 不能替代 release compiler，也不能绕过 `next -> stable` 和 release 验证。
macOS -> Linux hosted executable 仍不属于普通 cross compilation 承诺；跨 host 阶段只生成
可在 Linux 使用 native LLVM/toolchain 链接的 ELF object。

两跳自举完成后，先运行 Linux hosted process 聚焦门禁：

```bash
JIANGC=build/bin/jiangc bash ./script/linux_hosted_process_smoke.sh
```

该门禁覆盖 inherit/PATH、stdout pipe、128 KiB pipe drain、stderr discard 和 signal 退出码，
并由外层 shell 确认 discard 样例没有向父进程 stderr 泄漏内容。

main queue 与 pthread/futex 运行时聚焦门禁：

```bash
JIANGC=build/bin/jiangc bash ./script/linux_hosted_runtime_smoke.sh
```

该门禁覆盖 main-domain round-trip/shutdown/stress、serial/concurrent domain 和跨线程等待。

文件系统与 lang provider dynamic library 聚焦门禁：

```bash
JIANGC=build/bin/jiangc bash ./script/linux_hosted_fs_smoke.sh
JIANGC=build/bin/jiangc bash ./script/linux_hosted_dylib_smoke.sh
```

文件系统门禁覆盖读写、文件锁、file/dir 判断、dangling symlink 删除和原子替换；provider 对
partial result 与 `EINTR` 的循环边界由实现审计和 compiler system tests 共同约束。
dylib 门禁会真实构建、加载和调用 `.so` provider，并验证缓存失效与损坏产物诊断。

完整 Linux release 验证在 `linux-hosted-full.yml` 中执行两跳自举、全部 compiler/language tests、
打包和隔离安装 smoke。非 `release/**` 分支只通过 `workflow_dispatch` 手动运行；release 分支 push
自动触发。Linux package 由以下入口生成：

```bash
bash ./script/package_linux_release.sh
RELEASE_SMOKE_BUILD=0 bash ./script/release_smoke.sh
```

release smoke 使用安装后的 compiler 编译运行 Hello 与 hosted capability sample；`ABI.txt` 记录最终
ELF 的最低 glibc symbol version、解释器、动态库边界和 SHA-256。

## Jiang 0.4.9 的可复现自举链

0.4.9 的 lifetime 语法和编译器源码升级需要两个过渡编译器。发布后固定的链路为：

```text
Jiang 0.4.8 stable
  -> tag 0.4.9-bootstrap
  -> tag 0.4.9-bootstrap2
  -> tag 0.4.9 的 next
  -> Jiang 0.4.9 stable
```

在新机器上复现时，依次 checkout 对应 tag，并把前一阶段生成的 `build/bin/jiangc.next`
作为下一阶段的 `BOOTSTRAP_BIN`。最后在 `0.4.9` tag 上运行：

```bash
BOOTSTRAP_RELEASE_VERSION=0.4.9 \
BOOTSTRAP_BIN=/path/to/0.4.9-bootstrap2/build/bin/jiangc.next \
BOOTSTRAP_DEPTH=stable \
VERIFY=full \
bash ./script/build_next.sh
```

这些 tag 是历史自举输入。0.5.0 最初由 0.4.9 stable 建立 `bootstrap/0.5.0` next；
Domain/Executor ABI 跨越再由该 next 构建 `bootstrap/0.5.0-2`。发布链固定为：

```text
Jiang 0.4.9 stable
  -> bootstrap/0.5.0 next
  -> bootstrap/0.5.0-2 next
  -> release/0.5.0 next
  -> release/0.5.0 stable
```

复现 0.5.0 release 时应使用 `0.5.0-bootstrap2` tag 生成的 `jiangc.next`，不能退回 0.4.9
直接编译已经采用新 Domain ABI 的 compiler source。发布后的常规 `main` 开发直接使用
0.5.0 stable；`try ... catch` 新语法没有进入 compiler source，因此历史链无需第三层 transition。

## Jiang 0.5.1 的可复现自举链

0.5.1 的 generic initializer 与 `coroutine.sync` 迁移需要一个过渡编译器。发布链固定为：

```text
Jiang 0.5.0 stable
  -> tag 0.5.1-bootstrap next
  -> tag 0.5.1 的 next
  -> Jiang 0.5.1 stable
```

Linux 首次 seed 由 macOS 上的 `0.5.1-bootstrap` 生成 ELF object，再在 Linux 使用 native LLVM
和系统 toolchain 链接，并执行同一套 next/stable 两跳。GitHub Actions 通过固定 bootstrap commit
复现该过程；发布后的普通构建直接使用 Jiang 0.5.1 stable。

## 分支和 tag 规则

- `release/<version>` 是正式发布分支。
- `bootstrap/<version>` 只在破坏性升级时创建。
- 第二个过渡阶段使用 `bootstrap/<version>-2`，对应 tag `<version>-bootstrap2`。
- 第一个固定 checkpoint 使用 tag `<version>-bootstrap`。
- 用户 release tag `<version>` 只指向正式 release 源码。
- 发布后可以删除本地 bootstrap worktree；tag 保留可复现链。

## 迁移和验证规则

- 只迁移当前 bootstrap compiler 已支持的源码改动。
- 删除旧语法前，先保证下一阶段编译器可以解析新语法且相关语言测试通过。
- 闭包等后续破坏性语法若超过当前 stable 能力，再建立新的 bootstrap 链，不复用已发布版本的
  历史 worktree。
- 每完成一个功能可以提交；提交前运行直接相关测试。
- 测试失败时先修复该失败，不重复运行已经通过且与修改无关的部分。
- 完整自举和 full-test 留到发布验证阶段。
- 不为了通过测试恢复已经废弃的兼容语义。

## 注意事项

- 默认 bootstrap 使用固定 versioned path，不受 `~/.jiang/bin/jiangc` 当前指向影响。
- 不依赖仓库内 `dist/` 解压目录作为默认 bootstrap。
- bootstrap 和 release 阶段不能共享 build 产物。
- bootstrap checkpoint 必须能独立编译下一阶段，不能依赖未提交源码。
