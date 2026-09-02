# Jiang 编译器开发流程

本文记录 Jiang 的自举和验证流程。常规开发使用上一版正式 release 编译器；只有该编译器
无法直接编译新源码时，才建立一个或多个最小 bootstrap 过渡阶段。

## 常规开发

当前 0.5.3 release 源码已经使用 builtin `#doc`，使用 `bootstrap/0.5.3` 产出的 next：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.3 \
BOOTSTRAP_BIN=../bootstrap-0.5.3/build/bin/jiangc.next \
bash ./script/build_next.sh
```

发布后的常规开发应改用已安装的 0.5.3 stable。在下一版分支更新默认
bootstrap 版本前，显式指定：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.3 \
BOOTSTRAP_BIN="${JIANG_HOME:-$HOME/.jiang}/versions/0.5.3/bin/jiang" \
bash ./script/build_next.sh
```

脚本会校验 bootstrap compiler 版本。需要使用其他兼容编译器时，可以显式指定：

```bash
BOOTSTRAP_RELEASE_VERSION=<version> \
BOOTSTRAP_BIN=/path/to/compatible/jiangc \
bash ./script/build_next.sh
```

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
BOOTSTRAP_RELEASE_VERSION=0.5.3 \
BOOTSTRAP_BIN=../bootstrap-0.5.3/build/bin/jiangc.next \
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

release 分支保留自己的线性提交历史；bootstrap 分支只提供编译下一阶段所需的过渡编译器。
各阶段必须使用独立 build 目录，不混用编译产物。

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

`BOOTSTRAP_DEPTH=stable` 以严格检查构建 self-host candidate；任意借用或 lifetime 诊断
都会阻止候选生成。使用以下门槛验证：

```bash
BOOTSTRAP_CHECK_MODE=audit \
BOOTSTRAP_DEPTH=stable \
VERIFY=full \
bash ./script/build_next.sh
```

release candidate 只有在严格自举成功后才具备发布验证资格。

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
