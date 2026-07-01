# Jiang 编译器开发流程

本文记录 Jiang release 分支的自举和验证流程。常规版本优先依赖上一版正式 release
编译器；只有当前 release 编译器无法直接编译新源码时，才启用 bootstrap / release
双 worktree 模式。

## 常规 release 模式

常规版本从 `main` 建立 `release/<version>`，使用上一版正式 release 的 `jiangc` 构建当前源码。
当当前版本需要 bootstrap 分支承接破坏性升级时，`release/<version>` 使用同版本 bootstrap
编译器继续开发和验证。0.4.3 属于这个模式。

推荐安装路径：

```text
~/.jiang/versions/<previous-version>/bin/jiangc
```

0.4.3 release 分支的默认 bootstrap compiler 是：

```text
../bootstrap-0.4.3/build/bin/jiangc.next
```

如果没有同级 bootstrap worktree，也可以把 0.4.3 bootstrap 编译器安装到本地环境：

```text
~/.jiang/versions/0.4.3/bin/jiangc
```

构建当前源码：

```bash
bash ./script/build_next.sh
```

默认构建链路：

```text
../bootstrap-0.4.3/build/bin/jiangc.next -> build/bin/jiangc.next
```

脚本会校验 bootstrap compiler 版本，0.4.3 release 分支只接受 `jiang 0.4.3`。
如果需要使用其它路径的同版本编译器，可以显式指定：

```bash
BOOTSTRAP_BIN=/path/to/jiang-0.4.3/bin/jiangc bash ./script/build_next.sh
```

提交前至少跑：

```bash
VERIFY=none bash ./script/build_next.sh
JIANGC=./build/bin/jiangc.next bash ./script/lang_check.sh
```

正式 release 前跑 stable bootstrap 和完整验证：

```bash
BOOTSTRAP_DEPTH=stable VERIFY=full bash ./script/build_next.sh
```

## 双 worktree 模式

当新版本需要引入上一版 release 编译器无法直接编译的语法、core 源码组织或编译器内部
结构时，使用两个 worktree：

- `bootstrap/<version>`：过渡编译器分支，只负责产出能编译正式版本的编译器。
- `release/<version>`：正式发布分支，使用 bootstrap 编译器继续开发和验证。

推荐目录：

```text
/Users/jjc/project/jiang/bootstrap-<version>  -> bootstrap/<version>
/Users/jjc/project/jiang/jiang                -> release/<version>
```

双 worktree 开发步骤：

1. 从上一个正式 release 建立 `bootstrap/<version>`。
2. 在 `bootstrap/<version>` 中实现最小过渡能力。
3. 用上一个正式 release 编译器构建 bootstrap 编译器。
4. 从 bootstrap checkpoint 建立或继续 `release/<version>`。
5. 在 `release/<version>` 中实现正式功能。
6. 用 bootstrap 编译器构建 `release/<version>`。
7. 在 `release/<version>` 上跑完整语言测试和 stable bootstrap。
8. 正式发布时只给 `release/<version>` 打用户可见 tag。

`bootstrap/<version>` 只需要一轮自举：

```bash
BOOTSTRAP_BIN=/path/to/previous/release/jiangc \
JIANG_VERSION=<version> \
bash ./script/build_next.sh
```

它的验证目标是产出可用的过渡编译器，不作为正式 release 发布。

`release/<version>` 使用 bootstrap 编译器验证：

```bash
BOOTSTRAP_BIN=/Users/jjc/project/jiang/bootstrap-<version>/build/bin/jiangc.next \
JIANG_VERSION=<version> \
bash ./script/build_next.sh
```

## 分支和 tag 规则

- `release/<version>` 是正式发布分支。
- `bootstrap/<version>` 只在破坏性升级时创建，是内部过渡分支。
- 一般不需要给 bootstrap 分支立即打 tag。
- 如果需要固定 bootstrap checkpoint，可以打内部 tag：`<version>-bootstrap`。
- 用户可见 release tag 只打在 `release/<version>` 上。

## 代码迁移规则

从后续实验分支 cherry-pick 代码到正式版本时：

- 只 pick 已经被当前 bootstrap compiler 支持的改动。
- 不直接 pick 被当前正式线重构替代的旧实现。
- 破坏性能力先落在 bootstrap 分支，正式功能再落在 release 分支。
- 每个大功能完成后提交一次；提交前跑完整语言测试。

## 注意事项

- 常规 release 分支不要依赖仓库内 `dist/` 展开路径作为默认 bootstrap；默认路径应指向
  `~/.jiang/versions/<previous-version>/bin/jiangc`。
- 如果 `~/.jiang/bin/jiangc` 被切到其它版本，不应影响 release 分支构建；脚本应使用固定
  versioned path。
- 双 worktree 模式下，不要在 `release/<version>` 上用旧 release 编译器判断是否支持新语法；
  正式线应以 `bootstrap/<version>` 编译器为准。
- 不要把 bootstrap 分支作为用户 release 发布。
- 不要让 bootstrap worktree 和 release worktree 混用 build 产物。
- 如果默认 worktree 要切到正式发布线，先确认其它实验分支的未提交改动已经提交或 stash。
