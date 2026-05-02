<p align="center">
  <img src="./doc/logo.svg" alt="Jiang logo" width="180">
</p>


## Jiang 语言
> Jiang语言的目标是成为系统编程语言的“银弹”，`All in one`是Jiang语言的核心思想。

[Jiang 语言指南](./doc/jiang.md)

## 阶段规划

> 目前jiang语言已处于`Stage1`开发阶段。

`Stage0` 的任务是快速验证 Jiang 语言原型，基于C语言实现，完全采用AI编程，现阶段已接近完成。如果有后续Bug，将在 Stage1 阶段修复并验证。

 `Stage1` 目标是实现 Jiang 语言自举。此阶段将采用AI辅助+人工编码+人工审核推进，并开始明确追求代码质量、结构稳定性与长期可维护性。


## 开发工具

项目已经提供基础的 VS Code 插件 `vscode-jiang`，目前支持 `.jiang` 源文件的词法高亮，覆盖关键字、字符串、数字、类型名、构造器以及部分 Jiang 特有语法形态，便于日常编写和原型验证。


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
./build/stage0c --emit-llvm tests/samples/minimal.jiang
./build/stage0c --emit-obj tests/samples/minimal.jiang -o minimal.o
./build/stage0c tests/samples/minimal.jiang -o minimal
```

说明：

- `--emit-llvm` 默认输出到标准输出；配合 `-o` 可写入 `.ll` 文件
- `--emit-obj` 直接输出目标文件
- `stage0c` 是 C 实现的 bootstrap compiler；stage1 自举产物使用 `jiangc` 作为正式编译器名
- 不带 `--emit-*` 时，编译器会先生成临时目标文件，再通过宿主 `cc` 链接出可执行文件
- 当前最小运行时边界仍由宿主 C 运行时提供，主要包括 `malloc`、`free`、`printf`、`abort`

## 测试

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/test.sh
```

## License

Apache License 2.0. See [LICENSE](./LICENSE).
