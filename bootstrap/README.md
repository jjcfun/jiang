# Bootstrap

这里放 Jiang 版编译器的自举起步代码。

当前约定已经重新固定：

- `bootstrap/` 对应仓库里的 Stage1 编译器主线
- `compiler/` 只保留为未来 Stage2 预留目录

当前阶段目标不是重写整个编译器，而是把 Stage1 收成一个真实入口可工作的 `bootstrap-first` 编译器：由 Jiang 实现 `AST -> HIR -> JIR -> C` 前中后端骨架，继续依赖系统 C 编译器完成最后一跳。

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
- `jir.jiang`: Stage1 JIR 的 smoke driver，当前固定加载 `bootstrap/codegen_sample.jiang` 并输出 JIR dump
- `jir.golden`: `jir.jiang` 当前 JIR dump 的 golden 文本
- `jir_store.jiang`: Stage1 的最小 JIR 节点池
- `jir_lower.jiang`: 负责把 HIR lower 成 Stage1 当前的 JIR，并内置最小 C emitter
- `compiler_core.jiang`: Stage1 compiler driver API，提供 `compile_entry(path, mode)`
- `compiler.jiang`: Stage1 codegen smoke driver，固定把 `bootstrap/codegen_sample.jiang` 编译成 C 并输出到 stdout
- `compiler_source_loader.jiang`: 真实入口 smoke wrapper，验证 `source_loader.jiang` 的 `mode_emit_c`
- `hir_parser_core.jiang`: 真实入口 smoke wrapper，验证 `parser_core.jiang` 的 `mode_dump_hir`
- `jir_parser_core.jiang`: 真实入口 smoke wrapper，验证 `parser_core.jiang` 的 `mode_dump_jir`
- `hir_compiler_core.jiang`: 真实入口 smoke wrapper，验证 `compiler_core.jiang` 的 `mode_dump_hir`
- `compiler_compiler_core.jiang`: 真实入口 smoke wrapper，验证 `compiler_core.jiang` 的 `mode_emit_c`
- `symbol_store.jiang`: Stage1 的最小符号表
- `type_store.jiang`: Stage1 的最小类型表
- `module_loader.jiang`: 递归加载真实 bootstrap 模块图并驱动 parse/HIR lowering
- `module_paths.jiang`: 当前 Stage1 固定模块图与根模块集合
- `path_utils.jiang`: Stage1 当前使用的最小路径判断与 import 边界约束
- `source_loader.jiang`: 对 `std.fs` 的最小源码读取封装
- `sample.jiang`: 提供给 `lexer.jiang` 的固定输入样例
- `parser_sample.jiang`: 提供给 `parser.jiang` 的固定 parser fixture
- `codegen_sample.jiang`: 提供给 `jir.jiang` / `compiler.jiang` 的最小 end-to-end codegen fixture

当前约定的模块加载边界：

- 标准库导入只考虑 `import std;` 以及 `std.io` / `std.debug` / `std.fs`
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

当前约定的 JIR / codegen 行为：

- `jir.jiang` 当前固定解析 `bootstrap/codegen_sample.jiang` 并输出稳定 JIR dump
- `compiler_core.compile_entry(path, mode)` 当前支持 `mode_dump_ast`、`mode_dump_hir`、`mode_dump_jir`、`mode_emit_c`
- `compile_entry(path, mode)` 当前按传入入口模块工作，不再只服务固定 fixture wrapper
- `mode_emit_c` 目前直接把生成的 C 写到 stdout，由 smoke 脚本重定向到 `build/stage1_out/*.c`
- 当前最小 C emitter 已覆盖 `Int`、`Bool`、局部变量、赋值、`if/else`、`while`、函数调用、`return` 和 `__intrinsic_print`
- 输出必须与 `bootstrap/jir.golden` 以及 `script/stage1_codegen_smoke.sh` 的固定结果一致

当前真实入口回归重点覆盖：

- `bootstrap/source_loader.jiang` 的 `mode_emit_c`
- `bootstrap/parser_core.jiang` 的 `mode_dump_hir` 与 `mode_dump_jir`
- `bootstrap/compiler_core.jiang` 的 `mode_dump_hir` 与 `mode_emit_c`
- `bootstrap/lexer_core.jiang` 的 `mode_dump_hir`
- `bootstrap/hir_core.jiang` 的 `mode_dump_hir`
- `bootstrap/module_loader.jiang` 的 `mode_dump_hir`
- `bootstrap/jir_lower.jiang` 的 `mode_emit_c`
- `bootstrap/parser_store.jiang` 的 `mode_emit_c`
- `bootstrap/buffer_int.jiang`、`bootstrap/buffer_bytes.jiang`、`bootstrap/intern_pool.jiang` 的 `mode_emit_c`
- `bootstrap/module_paths.jiang`、`bootstrap/token_store.jiang`、`bootstrap/hir_store.jiang`、`bootstrap/jir_store.jiang` 的 `mode_emit_c`
- `bootstrap/symbol_store.jiang`、`bootstrap/type_store.jiang` 的 `mode_emit_c`

当前 growable store 主链已收口：

- `buffer_int.jiang`、`buffer_bytes.jiang`、`intern_pool.jiang` 已成为当前唯一底层抽象
- `token_store.jiang`、`parser_store.jiang`、`hir_store.jiang`、`symbol_store.jiang`、`type_store.jiang`、`jir_store.jiang` 当前都已接到 growable buffer，并通过正式 smoke 回归
- 当前重点已转为保持真实模块图覆盖稳定，不再回退到固定容量实现

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
./script/stage1_jir_smoke.sh
./script/stage1_codegen_smoke.sh
./script/stage1_real_entry_smoke.sh
```

当前也可以通过实验性的 build system 入口驱动一条最小 real-entry 路径：

```bash
./build/jiangc build --manifest ./bootstrap/jiang.build
./bootstrap/build/lexer
```
