# Language Testing 设计

`test/lang` 是源码级语言契约测试。测试按结果类型分层，把“能通过检查”“必须报错”
“能生成并运行”“能生成目标 IR”分开，而不是只靠端到端 smoke。

## 目录模型

```text
test/lang/<feature>/
  check/       期望 jiangc --check 成功
  fail/        期望 jiangc --check 失败，并用 expected 精确匹配诊断
  run/         期望 emit/link/run 成功，可用 expected-exit 匹配退出码
  emit/        期望 emit-llvm 成功，不要求运行
  diagnostic/ 未来用于精确检查多条诊断、span 和消息
```

`feature` 按语言功能命名，使用 snake_case。不要按编译阶段建目录；语言测试关注用户可见语义，
不是关注 parser/type_check/MIR 的内部实现。

## 覆盖原则

每个语言功能点都要按矩阵补齐用例：

- 正例：最小合法写法、完整显式写法、常见组合写法。
- 反例：语法错误、类型错误、名字解析错误、可见性/作用域错误。
- 边界：空列表、单元素、多元素、尾逗号、省略写法、嵌套写法。
- 交互：与泛型、trait、method、module、ownership、lifetime、default/named args 的组合。
- 后端：会影响 layout/MIR/backend 的功能必须至少有一个 `run/` 或 `emit/` 用例。
- 诊断：每个新增错误分支至少有一个 `fail/` 用例，并写 `// expected: code`。

覆盖目标不是“每个文件很多断言”，而是“每个语法/语义分支至少有一个稳定用例”。一个用例可以覆盖
多个正向组合，但反例应尽量一个文件对应一个 diagnostic，方便定位回归。

## 当前功能矩阵

详细覆盖状态见 `doc/compiler/lang-coverage.md`。0.2 需要优先覆盖这些 feature：

- `literal`：integer/float/char/string/bool/null，expected type 转换，CString NUL 语义。
- `type`：type suffix 顺序、handle 限制、raw pointer/many pointer/slice/array、errorable。
- `aggregate`：tuple、array、slice、struct 默认构造、union constructor。
- `function`：tail expr、call stmt、overload、default params、named args、constructor args。
- `control_flow`：if/switch/guard/while/for/range/defer/break/continue/Never 合并。
- `nominal`：struct/enum/union/init/deinit/static/instance method/member namespace。
- `generic`：generic type/function、where bound、trait conformance、associated type、monomorph。
- `ownership`：implicit copy、Movable、explicit move、borrow reference、drop/defer cleanup。
- `lifetime`：`@life` 语法、返回引用、字段引用、逃逸失败。
- `error_handling`：`T@E`、throw、try/catch、catch binding、未处理错误。
- `import`：file import、public import re-export、visibility。
- `package`：manifest dependency、package public surface、跨 package 访问、dependency cycle。
- `runtime`：main/runtime entry、CString、print/panic/assert 等 runtime-visible 能力。

## 补测试顺序

1. 先按 `doc/grammar.md` 的语法规则补 parser/type check 可见的 check/fail 用例。
2. 再按 `doc/language-design.md` 的语义章节补类型、所有权、lifetime、泛型和模块用例。
3. 最后给所有会影响 MIR/layout/backend 的功能补 `run/` 或 `emit/` 用例。

`script/lang_check.sh` 默认的 `run/` 会用 `jiangc --emit-llvm` 生成 LLVM IR，再用 LLVM clang
链接运行。需要覆盖 release object/executable 路径时，设置：

```bash
LANG_CHECK_RELEASE_RUNS=1 JIANGC=./build/jiangc.stable bash ./script/lang_check.sh
```

这会额外对所有 `run/` 用例执行 `jiangc --mode release -o ...`，覆盖 LLVM codegen opt level 2
和 `default<O2>` pass pipeline。

任何新增语言能力必须同步更新本矩阵；如果某个语义尚未定稿，应在对应 TODO 中标注，不能用临时
测试假定长期规则。
