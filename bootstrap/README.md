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
```
