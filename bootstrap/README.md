# Bootstrap

这里放 Jiang 版编译器的自举起步代码。

当前阶段目标不是重写整个编译器，而是先确认 Stage0 编译器能够稳定编译一个最小的 Jiang 工具程序。

当前内容：

- `lexer.jiang`: 第一个 Stage1 程序，读取 `bootstrap/sample.jiang` 并输出最小 token 流
- `sample.jiang`: 提供给 `lexer.jiang` 的固定输入样例

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
