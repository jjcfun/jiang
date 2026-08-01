# Jiang 编译器开发流程

本文记录 Jiang 的自举和验证流程。常规开发使用上一版正式 release 编译器；只有该编译器
无法直接编译新源码时，才建立一个或多个最小 bootstrap 过渡阶段。

## 常规开发

当前 `main` 默认使用已安装的 Jiang 0.5.0 stable：

```text
~/.jiang/versions/0.5.0/bin/jiangc
```

构建当前源码：

```bash
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
BOOTSTRAP_DEPTH=stable VERIFY=full bash ./script/build_next.sh
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

release 分支最终 rebase 到最后一个 bootstrap checkpoint，使源码历史和可复现自举顺序一致。
各阶段必须使用独立 build 目录，不混用编译产物。

## Linux 首次 hosted port seed

0.5.0 正式产物只有 macOS arm64 compiler。0.5.1 首次建立 Linux x86_64 hosted 自举时，
不创建 bootstrap 分支：语言语法和 compiler source 仍由 Jiang 0.5.0 stable 直接编译，
平台 seed 只桥接“尚无 Linux 可执行 stable compiler”这一 host 缺口。

在已有 Jiang 0.5.0 stable 的 host 上生成 Linux compiler ELF object：

```bash
bash ./script/linux_port_seed.sh emit-object
```

把 `build/linux-port-seed/jiangc-x86_64-linux-gnu.o` 与同一 source revision 传到
Linux x86_64。Linux host 必须先通过 `script/install_llvm.sh` 准备 Jiang 固定的 LLVM 22.1.8
fork，然后链接 port seed：

```bash
bash ./script/linux_port_seed.sh link
```

link 阶段会先编译运行 Linux hosted ABI probe，锁定当前 provider 使用的 `stat`、`dirent`、
pthread storage、`-pthread` 和 `-ldl` 边界。probe 通过后，脚本输出 object/compiler 的 SHA-256。
Linux port seed 只用于接续正常的 stable bootstrap：

```bash
BOOTSTRAP_BIN=build/bin/jiangc.linux-port-seed \
BOOTSTRAP_RELEASE_VERSION=0.5.0 \
VERIFY=none \
bash ./script/build_next.sh
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
