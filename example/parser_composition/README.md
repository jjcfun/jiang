# 公共 parser 组合示例

使用支持这些 API 的 Jiang 0.5.5 编译器，在仓库根目录运行：

```sh
jiangc -o build/parser-composition-example example/parser_composition/app
./build/parser-composition-example
```

`provider/provider.jiang` 是完整的语言实现，`app/main.jiang` 展示调用方式。

- `name = expression;` 生成返回表达式的静态 Int 方法；`name;` 返回零。
  Parser 先尝试长形式，未匹配时回退保存点再解析短形式。
- RawBlock 由公共 parser 展开。Provider 只判断 Attribute、Member 或成员序列，
  不识别 `#doc` 名称，也不调用它的实现。
- 待附着 Attribute 放在 `parser.list<Attribute>()` 中，并显式附着到下一个成员。
  序列的第一个成员接收待附着 Attribute；空序列保留待附着内容，块末尾悬空则报错。
- 其他结果类别在成员位置报错。子语言内部的 Attribute 由子语言自身处理，不跨作用域猜测归属。
- 参数、语句、成员和 Attribute 都使用上下文列表。Provider 无可变持久状态，
  默认扫描 token 可重复读取，因此外层试探解析可以安全重试整个块。

此示例由 `test/compiler/lang_provider/run/parser_composition.jiang` 编译并运行。
