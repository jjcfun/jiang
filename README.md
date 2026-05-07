<p align="center">
  <img src="./doc/logo.svg" alt="Jiang logo" width="180">
</p>


## Jiang 语言
> Jiang语言的目标是成为系统编程语言的“银弹”，`All in one`是Jiang语言的核心思想。

[Jiang 语言指南](./doc/jiang.md)

## 阶段规划

> 目前 jiang 语言已完成 `Stage1` 自举基线，正在进入 Stage1 hardening / Stage2 准备阶段。

`Stage0` 的任务是快速验证 Jiang 语言原型，基于 C 语言实现，完全采用 AI 编程。后续只保留 bootstrap 兼容和必要 bugfix。

`Stage1` 编译器源码使用 Jiang 编写，已经打通自举构建、package/source/driver、resolver/type checker、HIR/JIR lowering、LLVM backend 和基础 runtime/builtin smoke。当前 Stage1 的重点是稳定语义身份、测试基线和文档边界，不把完整 ABI、trait object、LSP 或声明级增量编译作为 Stage1 完成条件。

`Stage2` 将在 Stage1 自举产物之上继续演进，重点进行编译器结构重构、代码质量提升、长期可维护性收敛，以及面向后续增量编译的内部表示整理。


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

### 从源码构建 Stage0

`stage0c` 是 C 实现的 bootstrap compiler。直接用 CMake 构建即可：

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config cmake -S . -B build
cmake --build build --target stage0c
```

也可以使用仓库脚本：

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/build_stage0.sh
```

### 从源码构建 Stage1

`Stage1` 编译器源码入口是 `compiler/jiangc.jiang`。构建流程是：

1. 先用 `stage0c` 把 `compiler/jiangc.jiang` 编译成 LLVM IR。
2. 再用 LLVM 21.1.x 对应的 `clang` 和 LLVM link flags 链接成 `jiangc`。

推荐直接运行构建脚本，它会构建 `build/stage1/jiangc` 并用它编译样例：

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/build_stage1.sh
```

等价的手动构建命令如下：

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config
STAGE1_BUILD_DIR=build/stage1
mkdir -p "$STAGE1_BUILD_DIR"

./build/stage0c --emit-llvm compiler/jiangc.jiang > "$STAGE1_BUILD_DIR/jiangc.ll"

read -r -a llvm_ldflags <<< "$("$LLVM_CONFIG" --ldflags)"
read -r -a llvm_libs <<< "$("$LLVM_CONFIG" --libs core analysis target native nativecodegen)"
read -r -a llvm_system_libs <<< "$("$LLVM_CONFIG" --system-libs)"

"$(dirname "$LLVM_CONFIG")/clang" "$STAGE1_BUILD_DIR/jiangc.ll" \
  -o "$STAGE1_BUILD_DIR/jiangc" \
  "${llvm_ldflags[@]}" \
  "${llvm_libs[@]}" \
  "${llvm_system_libs[@]}" \
  -lc++
```

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
- 当前最小运行时边界仍由宿主 C 运行时提供，主要包括 `malloc`、`free`、`exit`，以及 Stage0/兼容路径仍使用的 `printf` / `abort`

### 使用 Stage1 编译器

构建出 `build/stage1/jiangc` 后，可以用它编译 Jiang 源文件：

```bash
./build/stage1/jiangc --emit-llvm tests/samples/minimal.jiang
./build/stage1/jiangc --emit-obj -o minimal.o tests/samples/minimal.jiang
./build/stage1/jiangc -o minimal tests/samples/minimal.jiang
```

生成可执行文件后直接运行：

```bash
./minimal
echo $?
```

## 测试

```bash
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/test.sh
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/build_stage1.sh
LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/stage1_test.sh
```

## License

Apache License 2.0. See [LICENSE](./LICENSE).
