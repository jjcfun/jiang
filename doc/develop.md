# Jiang 编译器开发流程

本文记录涉及破坏性编译器升级时的版本开发流程。目标是避免一个功能因为 bootstrap
编译器能力不足被拆散到多个用户可见版本中。

## 双 worktree 模式

当新版本需要引入当前 release 编译器无法直接编译的语法、core 源码组织或编译器内部
结构时，使用两个 worktree：

- `bootstrap/<version>`：过渡编译器分支，只负责产出能编译正式版本的编译器。
- `release/<version>`：正式发布分支，使用 bootstrap 编译器继续开发和验证。

推荐目录：

```text
/Users/jjc/project/jiang/bootstrap-<version>  -> bootstrap/<version>
/Users/jjc/project/jiang/jiang                -> release/<version>
```

例如 0.4.2：

```text
/Users/jjc/project/jiang/bootstrap-0.4.2  -> bootstrap/0.4.2
/Users/jjc/project/jiang/jiang            -> release/0.4.2
```

## 开发步骤

1. 从上一个正式 release 建立 `bootstrap/<version>`。
2. 在 `bootstrap/<version>` 中实现最小过渡能力。
3. 用上一个正式 release 编译器构建 bootstrap 编译器。
4. 从 bootstrap checkpoint 建立或继续 `release/<version>`。
5. 在 `release/<version>` 中实现正式功能。
6. 用 bootstrap 编译器构建 `release/<version>`。
7. 在 `release/<version>` 上跑完整语言测试和 stable bootstrap。
8. 正式发布时只给 `release/<version>` 打用户可见 tag。

## 验证规则

`bootstrap/<version>` 只需要一轮自举：

```bash
BOOTSTRAP_BIN=/path/to/previous/release/jiangc \
JIANG_VERSION=<version> \
bash ./script/build_next.sh
```

它的验证目标是产出可用的过渡编译器，不作为正式 release 发布。

`release/<version>` 必须使用 bootstrap 编译器验证：

```bash
BOOTSTRAP_BIN=/Users/jjc/project/jiang/bootstrap-<version>/build/jiangc.next \
JIANG_VERSION=<version> \
bash ./script/build_next.sh
```

提交前必须跑完整语言测试：

```bash
JIANGC=./build/jiangc.next bash ./script/lang_check.sh
```

正式 release 前再跑 stable bootstrap。

## 分支和 tag 规则

- `bootstrap/<version>` 是内部过渡分支。
- `release/<version>` 是正式发布分支。
- 一般不需要给 bootstrap 分支立即打 tag。
- 如果需要固定 bootstrap checkpoint，可以打内部 tag：`<version>-bootstrap`。
- 用户可见 release tag 只打在 `release/<version>` 上。

## 代码迁移规则

从后续实验分支 cherry-pick 代码到正式版本时：

- 只 pick 已经被 bootstrap 编译器支持的改动。
- 不直接 pick 被当前正式线重构替代的旧实现。
- 破坏性能力先落在 bootstrap 分支，正式功能再落在 release 分支。
- 每个大功能完成后提交一次；提交前跑完整语言测试。

## 注意事项

- 不要在 `release/<version>` 上用旧 release 编译器判断是否支持新语法；正式线应以
  `bootstrap/<version>` 编译器为准。
- 不要把 bootstrap 分支作为用户 release 发布。
- 不要让 bootstrap worktree 和 release worktree 混用 build 产物。
- 如果默认 worktree 要切到正式发布线，先确认其它实验分支的未提交改动已经提交或 stash。
