# Jiang Stage0

新的 `stage0` 从零开始，当前固定实现路线是：

- C 语言实现前端与中间层
- LLVM C API 生成 LLVM IR
- 编译链固定为 `AST -> HIR -> JIR -> LLVM IR`
- LLVM 版本固定为 `21.1.*`

当前最小语法只支持：

```jiang
return 42;
```

## 构建

优先使用固定的 LLVM 21.1.x。推荐通过 `JIANG_LLVM_ROOT` 或 `LLVM_CONFIG` 显式指定：

```bash
cmake -S . -B build -DJIANG_LLVM_ROOT=/opt/homebrew/opt/llvm@21
cmake --build build
```

或者：

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config cmake -S . -B build
cmake --build build
```

如果 `llvm-config --version` 不是 `21.1.*`，配置阶段会直接失败。

## 使用

```bash
./build/jiangc --emit-llvm tests/samples/minimal.jiang
./build/jiangc --emit-obj tests/samples/minimal.jiang -o minimal.o
./build/jiangc tests/samples/minimal.jiang -o minimal
```

说明：

- `--emit-llvm` 默认输出到标准输出；配合 `-o` 可写入 `.ll` 文件
- `--emit-obj` 直接输出目标文件
- 不带 `--emit-*` 时，`jiangc` 会先生成临时目标文件，再通过宿主 `cc` 链接出可执行文件
- 当前最小运行时边界仍由宿主 C 运行时提供，主要包括 `malloc`、`free`、`printf`、`abort`

## 语言指南

[Jiang 语言指南](./doc/jiang.md)

## 测试

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/test.sh
```

## License

Apache License 2.0. See [LICENSE](./LICENSE).
