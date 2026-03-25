# Bootstrap

这里放 Jiang 版编译器的自举起步代码。

当前阶段目标不是重写整个编译器，而是先确认 Stage0 编译器能够稳定编译一个最小的 Jiang 工具程序。

当前内容：

- `lexer.jiang`: 第一个 Stage1 程序，读取 `bootstrap/sample.jiang` 并输出最小 token 流
- `path_utils.jiang`: Stage1 当前使用的最小路径判断与 import 边界约束
- `source_loader.jiang`: 对 `std.fs` 的最小源码读取封装
- `sample.jiang`: 提供给 `lexer.jiang` 的固定输入样例

当前约定的模块加载边界：

- 标准库导入只考虑 `import std;` 以及 `std.io` / `std.assert` / `std.fs`
- 普通模块导入当前只接受相对路径且必须以 `.jiang` 结尾
- 暂不在 Stage1 第一版里处理绝对路径 import 或更复杂的路径标准化

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
```
