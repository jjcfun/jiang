# Jiang Stage0

新的 `stage0` 从零开始，当前固定实现路线是：

- C 语言实现前端与中间层
- LLVM C API 生成 LLVM IR
- 编译链固定为 `AST -> HIR -> JIR -> LLVM IR`

当前最小语法只支持：

```jiang
return 42;
```

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 使用

```bash
./build/jiangc --emit-llvm tests/samples/minimal.jiang
```

## 测试

```bash
bash ./script/test.sh
```
