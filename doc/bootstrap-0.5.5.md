# 0.5.5 必要自举阶段

0.5.4 stable 无法解析 `#package`，在声明处报告 `lang_dependency_not_found`。
正式源码迁移为导入 `package.jiang` 前，需要固定此阶段的源码 commit 并完成严格自举。

本阶段基于 `955cd1af6ac930d5cab835b1d5da60d116c3b3b4`，仅加入：

- 内置 `#package` 声明展开、单文件声明标记及重复声明诊断。
- 共享 `std.jiang.PackageInfo` 值类型和私有数组／只读切片存储。
- 只读结构体切片的编译期值导入、导出及 JIL 静态物化。
- 标准库 builtin 声明按源码位置识别，移除 INI 后不依赖清单中的 `std` 包名。
- 命名实参同时接受 `foo(a: 123)` 和 `foo(a = 123)`；工程源码仍保留旧语法。
- 对应声明、普通导入、字段／整体读取、切片和命名实参回归。

此阶段保留 INI 包加载，不包含正式版本的目录入口、依赖、Lang、generate 或缓存迁移。
版本标识为 `0.5.5-bootstrap`。不得用未固定的开发产物替代本阶段。

兼容实现固定于提交 `b56c5d4905002ef2255d3d644f00b6f4da4d50b8`。
bootstrap 只需由 0.5.4 stable 生成 next；feat 直接使用该 next 作种子。

固定实现提交后，在此 worktree 内执行：

```bash
BOOTSTRAP_RELEASE_VERSION=0.5.4 \
BOOTSTRAP_BIN=/Users/jjc/.jiang/versions/0.5.4/bin/jiangc \
BUILD_DIR="$PWD/build/named-arguments" COMPILER_BUILD_MODE=release \
BOOTSTRAP_DEPTH=next VERIFY=none bash script/build_next.sh

JIANGC=./build/named-arguments/bin/jiangc.next TEST_ROOT=test/lang \
TEST_FILTER='package/.*package_info|constant/run/global_const_struct_slice.jiang|named_argument|default_parameter' bash script/test.sh

JIANGC=./build/named-arguments/bin/jiangc.next TEST_ROOT=test/compiler \
TEST_FILTER='syntax/run/syntax.jiang' bash script/test.sh
```

`BOOTSTRAP_DEPTH=next` 只生成 bootstrap next，不要求 bootstrap 自身再生成 stable。
`VERIFY=none` 只分离脚本测试执行，不关闭语义检查。next 和相关回归通过后，用于 feat 的
`next -> stable`。保留实现 commit、种子和工具链身份、next 产物哈希及测试日志；构建期间不修改源码。
