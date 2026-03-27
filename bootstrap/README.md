# Bootstrap

这里放 Jiang 版编译器的自举起步代码。

当前阶段目标不是重写整个编译器，而是先确认 Stage0 编译器能够稳定编译一个最小的 Jiang 工具程序。

当前内容：

- `lexer.jiang`: 第一个 Stage1 程序，读取 `bootstrap/sample.jiang` 并输出 token 流
- `lexer.golden`: `lexer.jiang` 当前 token dump 的 golden 文本
- `lexer_core.jiang`: 可复用的 Stage1 lexer 模块
- `parser.jiang`: Stage1 parser 的 smoke driver，读取固定 fixture 并输出 parse dump
- `parser.golden`: `parser.jiang` 当前 parse dump 的 golden 文本
- `parser_core.jiang`: 可复用的 Stage1 parser 模块，负责把 token 流转换成节点树
- `parser_store.jiang`: 轻量节点池，使用整数 node id 和 child 链接保存 parse tree
- `hir.jiang`: Stage1 HIR 的 smoke driver，递归加载真实 bootstrap 模块并输出 HIR dump
- `hir.golden`: `hir.jiang` 当前 HIR dump 的 golden 文本
- `hir_core.jiang`: 负责把 parse tree lower 成树状 HIR，并完成最小名字解析与类型挂载
- `hir_store.jiang`: HIR 节点池，使用整数 node id 和 child 链接保存 HIR
- `symbol_store.jiang`: Stage1 的最小符号表
- `type_store.jiang`: Stage1 的最小类型表
- `module_loader.jiang`: 递归加载真实 bootstrap 模块图并驱动 parse/HIR lowering
- `module_paths.jiang`: 当前 Stage1 固定模块图与根模块集合
- `path_utils.jiang`: Stage1 当前使用的最小路径判断与 import 边界约束
- `source_loader.jiang`: 对 `std.fs` 的最小源码读取封装
- `sample.jiang`: 提供给 `lexer.jiang` 的固定输入样例
- `parser_sample.jiang`: 提供给 `parser.jiang` 的固定 parser fixture

当前约定的模块加载边界：

- 标准库导入只考虑 `import std;` 以及 `std.io` / `std.assert` / `std.fs`
- 普通模块导入当前只接受相对路径且必须以 `.jiang` 结尾
- 暂不在 Stage1 第一版里处理绝对路径 import 或更复杂的路径标准化

当前约定的 token dump 格式：

- 每行一个 token，格式为 `<kind> <start> <end> <lexeme>`
- `kind` 使用 `TokenKind` 枚举成员对应的小写 `snake_case` 名称
- `lexeme` 为原始词面文本，换行、制表符、反斜杠和双引号会转义成单行文本
- token 流末尾固定追加 `eof` token
- 输出必须与 `bootstrap/lexer.golden` 完全一致，`script/stage1_smoke.sh` 会做完整 diff

当前约定的 parse dump 格式：

- 第一行固定为 `bootstrap parser dump:\nprogram`
- 之后每行两个空格缩进一级，输出稳定的节点树
- 节点名使用 parser store 里的小写 `snake_case` 名称
- 声明节点会内联关键名字面量，例如 `func_decl public add`
- `import` 节点会带 `alias=` / `path=`，字符串与换行同样转义成单行文本
- 输出必须与 `bootstrap/parser.golden` 完全一致，`script/stage1_parser_smoke.sh` 会做完整 diff

当前约定的 HIR dump 格式：

- 第一行固定为 `bootstrap hir dump:\nprogram`
- 之后按模块输出递归加载后的真实 bootstrap 模块树，不再依赖单文件 fixture
- 每个 HIR 节点行会固定输出节点名，并在可用时带 `sym=`、`type=` 与 import `target=`
- 当前 HIR 负责名字解析、模块导入、最小类型挂载与语法糖规整，不做完整类型检查
- 输出必须与 `bootstrap/hir.golden` 完全一致，`script/stage1_hir_smoke.sh` 会做完整 diff

手动运行方式：

```bash
mkdir -p build
cd build
cmake ..
make
cd ..
./build/jiangc --stdlib-dir ./std ./bootstrap/lexer.jiang
./build/lexer
```

也可以直接运行：

```bash
./script/stage1_smoke.sh
./script/stage1_parser_smoke.sh
./script/stage1_hir_smoke.sh
```
